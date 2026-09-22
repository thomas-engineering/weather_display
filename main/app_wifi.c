#include "app_wifi.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "nvs.h"
#include "storage_record.h"
#include "storage_backend_nvs.h"
#include "wifi_reconnect_policy.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "wifi";
static const char *NS = "wifi";
#define RECORD_KEY "cred"
#define RECORD_VERSION 1

/* On-disk layout of the "cred" record — one blob instead of separate "ssid"
 * and "pass" keys, so a power loss mid-save can't pair a new SSID with the
 * previous network's password (see main/app_prefs.c's prefs_payload_t for
 * the same reasoning in more detail). */
typedef struct __attribute__((packed)) {
    char ssid[33];
    char pass[65];
} wifi_cred_payload_t;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* Once the fast disconnect-triggered retry burst (CONFIG_WEATHER_WIFI_MAX_RETRY
 * attempts, back-to-back) is exhausted, keep trying at this slower cadence
 * indefinitely instead of giving up — an outage that outlasts the burst (a
 * router reboot, a longer drop) would otherwise strand the device until a
 * human reconnects it by hand. */
#define WIFI_RECONNECT_INTERVAL_MS (15 * 1000)

/* Every decision about what to do after a Wi-Fi event lives in
 * components/app_logic/wifi_reconnect_policy.c, which is host-tested
 * (./scripts/host-test.sh) against the exact stall sequences this file used
 * to get stuck in. What is left here is the adapter: translate driver events
 * into policy events, and carry out the actions the policy hands back.
 *
 * Don't put reconnect decisions back in this file — they are not reachable
 * from any test here (see CLAUDE.md's "Grenzen der Testebenen"). */
static wifi_reconnect_policy_t s_policy;
static SemaphoreHandle_t s_policy_lock;

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;

/* The reconnect worker. esp_wifi_connect() is a synchronous RPC to the C6 and
 * blocks for up to the transport's 5s timeout, so it must not run on the
 * event-loop task (it would stall every other event) nor on the esp_timer
 * task (it would stall every other timer, including main.c's heartbeat
 * check). This task exists to be the one place that is allowed to block on
 * it. Its wait timeout doubles as the periodic retry cadence, which is why
 * there is no esp_timer here any more. */
static TaskHandle_t s_worker;
static SemaphoreHandle_t s_wake;
static bool s_connect_pending;
static bool s_timer_armed;

static char s_ssid[33];
static char s_pass[65];

static void policy_lock(void)   { if (s_policy_lock) xSemaphoreTake(s_policy_lock, portMAX_DELAY); }
static void policy_unlock(void) { if (s_policy_lock) xSemaphoreGive(s_policy_lock); }

/* Feeds one event to the policy and carries out whatever it asks for. Safe
 * from any task; the actual connect call is handed to s_worker rather than
 * being made here, so a caller on the event loop never blocks on the RPC. */
static void policy_event(wifi_reconnect_event_t ev) {
    policy_lock();
    wifi_reconnect_action_t a = wifi_reconnect_policy_event(&s_policy, ev);
    if (a.arm_timer)    s_timer_armed = true;
    if (a.disarm_timer) s_timer_armed = false;
    if (a.connect)      s_connect_pending = true;
    policy_unlock();

    if (a.signal_up)     xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    if (a.signal_failed) xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
    if (a.connect && s_wake) xSemaphoreGive(s_wake);
}

static void wifi_worker_task(void *arg) {
    (void)arg;
    for (;;) {
        policy_lock();
        bool armed = s_timer_armed;
        policy_unlock();

        TickType_t wait = armed ? pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS) : portMAX_DELAY;
        bool woken = (xSemaphoreTake(s_wake, wait) == pdTRUE);

        policy_lock();
        bool want_connect = s_connect_pending;
        s_connect_pending = false;
        policy_unlock();

        if (want_connect) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                /* No STA_DISCONNECTED event follows a refused connect, so
                 * without telling the policy the retry chain would simply end
                 * here and the device would sit offline with the AP in range.
                 * This is the stall that motivated the whole policy split. */
                ESP_LOGW(TAG, "esp_wifi_connect refused: %s", esp_err_to_name(err));
                policy_event(WRP_EV_CONNECT_REJECTED);
            }
        } else if (!woken) {
            policy_event(WRP_EV_TIMER_TICK);
        }
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            policy_event(WRP_EV_STA_START);
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "disconnected (reason %d)", e ? e->reason : 0);
            policy_event(WRP_EV_DISCONNECTED);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        switch (id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
            ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
            policy_event(WRP_EV_GOT_IP);
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            /* The association can stay up while the address goes away (lease
             * expiry, DHCP server restart). No disconnect event follows, so
             * this is the only notice we get — without handling it the device
             * kept reporting "Online" while every request failed. */
            ESP_LOGW(TAG, "lost ip; reconnecting");
            policy_event(WRP_EV_LOST_IP);
            break;
        default:
            break;
        }
    }
}

static void save_credentials(const char *ssid, const char *pass) {
    wifi_cred_payload_t c = {0};
    snprintf(c.ssid, sizeof c.ssid, "%s", ssid ? ssid : "");
    snprintf(c.pass, sizeof c.pass, "%s", pass ? pass : "");
    storage_record_save(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, &c, sizeof c);
}

/* One-time upgrade path from this project's first NVS layout, which kept
 * "ssid" and "pass" as separate scalar keys — see the matching comment in
 * app_prefs.c's migrate_from_legacy_keys() for why the legacy keys are read
 * once and left in place rather than erased. */
static void migrate_from_legacy_keys(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof s_ssid;
    if (nvs_get_str(h, "ssid", s_ssid, &n) != ESP_OK) s_ssid[0] = '\0';
    n = sizeof s_pass;
    if (nvs_get_str(h, "pass", s_pass, &n) != ESP_OK) s_pass[0] = '\0';
    nvs_close(h);
    if (s_ssid[0]) save_credentials(s_ssid, s_pass);
}

static void load_credentials(void) {
    s_ssid[0] = s_pass[0] = '\0';
    wifi_cred_payload_t c;
    if (storage_record_load(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, &c, sizeof c)) {
        memcpy(s_ssid, c.ssid, sizeof c.ssid);
        memcpy(s_pass, c.pass, sizeof c.pass);
    } else {
        migrate_from_legacy_keys();
    }
    if (!s_ssid[0]) {
        /* Kconfig fallback, for a device flashed with a network already known. */
        snprintf(s_ssid, sizeof s_ssid, "%s", CONFIG_WEATHER_WIFI_SSID);
        snprintf(s_pass, sizeof s_pass, "%s", CONFIG_WEATHER_WIFI_PASSWORD);
    }
}

/* ESP_OK or "already done" are both fine — the BSP may have initialised these. */
static bool ok_or_done(esp_err_t err) { return err == ESP_OK || err == ESP_ERR_INVALID_STATE; }

void app_wifi_init(void) {
    s_events = xEventGroupCreate();
    s_policy_lock = xSemaphoreCreateMutex();
    s_wake = xSemaphoreCreateBinary();
    if (!s_events || !s_policy_lock || !s_wake) { ESP_LOGE(TAG, "out of memory"); return; }

    /* Credentials first, before anything that can fail below. Each of the
     * bring-up steps used to return early ahead of this, which left s_ssid
     * empty — so a device whose co-processor was merely slow to come up
     * reported "no network stored" and sat on the setup screen with perfectly
     * good credentials in NVS and the AP in range. */
    load_credentials();
    wifi_reconnect_policy_init(&s_policy, CONFIG_WEATHER_WIFI_MAX_RETRY, s_ssid[0] != '\0');

    if (!ok_or_done(esp_netif_init())) { ESP_LOGE(TAG, "esp_netif_init failed"); return; }
    if (!ok_or_done(esp_event_loop_create_default())) { ESP_LOGE(TAG, "event loop failed"); return; }
    /* Exactly one STA netif for the life of the app: creating a second one trips
     * lwip's "netif already added" assert. */
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (!ok_or_done(esp_wifi_init(&cfg))) { ESP_LOGE(TAG, "esp_wifi_init failed"); return; }

    /* ESP_EVENT_ANY_ID on IP_EVENT too, not just GOT_IP: LOST_IP is the only
     * notice we get when the address goes away under a live association. */
    esp_event_handler_instance_t any_wifi, any_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_ip));

    /* Up before esp_wifi_start(), so the STA_START event it produces already
     * has somewhere to hand its connect. */
    if (xTaskCreate(wifi_worker_task, "wifi_reconn", 4096, NULL, 5, &s_worker) != pdPASS) {
        ESP_LOGE(TAG, "could not start reconnect worker");
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) { ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err)); return; }

    /* esp_wifi_start() is asynchronous over the SDIO link to the C6: STA_START and
     * the netif bring-up land afterwards. Scanning or connecting into that window
     * drives a second start action through the same netif and asserts in lwip.
     * Let the interface settle before anyone else touches it. */
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "station ready (credentials: %s)", s_ssid[0] ? "stored" : "none");
}

bool app_wifi_have_credentials(void) { return s_ssid[0] != '\0'; }

bool app_wifi_is_connected(void) {
    policy_lock();
    bool r = s_policy.connected;
    policy_unlock();
    return r;
}

/* True whenever there's a network to rejoin and the driver hasn't given up on
 * it yet — covers both the fast disconnect-triggered retry burst and the
 * slower periodic retry that follows. False once connected, and also false
 * with nothing to reconnect to (no stored credentials, or the user forgot
 * the network via app_wifi_forget()). */
bool app_wifi_is_reconnecting(void) {
    policy_lock();
    bool r = wifi_reconnect_policy_needs_recovery(&s_policy);
    policy_unlock();
    return r;
}

/* For the Settings > Device information dialog (Claude Design, 2026-09-10).
 * Buffers must be at least 16 bytes (IPSTR is "%d.%d.%d.%d"). Returns false
 * (buffers left untouched) if not connected or the netif has no address yet. */
bool app_wifi_get_ip_info(char *ip, size_t ip_len, char *dns, size_t dns_len, char *gw, size_t gw_len) {
    if (!app_wifi_is_connected() || !s_sta_netif) return false;
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) return false;
    snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
    snprintf(gw, gw_len, IPSTR, IP2STR(&ip_info.gw));
    esp_netif_dns_info_t dns_info = {0};
    if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
        snprintf(dns, dns_len, IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    } else {
        snprintf(dns, dns_len, "-");
    }
    return true;
}

bool app_wifi_verify_link(void) {
    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) return true;

    /* ESP_ERR_WIFI_NOT_CONNECT here means the driver knows we are not
     * associated while our own state still said we were — the disconnect
     * event either never arrived or was lost. Correct it and let the policy
     * start recovery the same way a real event would. */
    ESP_LOGW(TAG, "link check failed (%s); treating as disconnected", esp_err_to_name(err));
    policy_event(WRP_EV_DISCONNECTED);
    return false;
}

void app_wifi_force_reconnect(void) {
    if (!s_ssid[0]) return;
    ESP_LOGW(TAG, "forcing reconnect to \"%s\"", s_ssid);
    esp_wifi_disconnect();
    /* Same shape as a user-initiated connect: full retry budget, and the
     * disconnect we just caused is swallowed rather than answered with a
     * competing attempt. Deliberately does not wait for the outcome. */
    policy_event(WRP_EV_MANUAL_CONNECT);
}

/* Shared by both connect paths. */
static bool connect_locked(const char *ssid, const char *pass) {
    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wc = { 0 };
    snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", pass);
    /* An empty passphrase means an open network; demanding WPA2 would reject it. */
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    esp_wifi_disconnect();
    /* The policy swallows exactly the one disconnect the call above produces,
     * so it doesn't answer our own teardown with a competing connect. */
    policy_event(WRP_EV_MANUAL_CONNECT);

    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) return true;
    /* Returning false does not mean we stopped trying: the burst and/or the
     * periodic retry are still running, which is what app_wifi_is_reconnecting()
     * reports to the UI. */
    ESP_LOGE(TAG, "could not connect to \"%s\" within 30s", ssid);
    return false;
}

bool app_wifi_connect(void) {
    if (!s_ssid[0]) return false;
    return connect_locked(s_ssid, s_pass);
}

bool app_wifi_connect_with(const char *ssid, const char *password) {
    if (!ssid || !ssid[0]) return false;
    if (!connect_locked(ssid, password ? password : "")) return false;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    snprintf(s_pass, sizeof s_pass, "%s", password ? password : "");
    save_credentials(s_ssid, s_pass);
    ESP_LOGI(TAG, "stored credentials for \"%s\"", s_ssid);
    return true;
}

void app_wifi_forget(void) {
    storage_backend_nvs.erase(storage_backend_nvs.ctx, NS, RECORD_KEY);
    /* Also erase the pre-storage-record legacy keys: load_credentials()'s
     * migration path reads them whenever the "cred" record is absent, so
     * leaving them in place here would resurrect a forgotten network on the
     * next boot. */
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
        nvs_commit(h);
        nvs_close(h);
    }
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    policy_event(WRP_EV_FORGET);
    esp_wifi_disconnect();
    ESP_LOGI(TAG, "forgot stored network");
}

int app_wifi_scan(wx_wifi_network_t *out, int max) {
    if (!out || max <= 0) return 0;

    /* A scan drops the current association. Telling the policy about the whole
     * scan window rather than just suppressing reconnects means a disconnect
     * landing inside it is deferred to the end instead of being dropped — the
     * old code swallowed it outright and relied on a single unchecked
     * esp_wifi_connect() afterwards to repair things. */
    policy_event(WRP_EV_SCAN_BEGIN);
    wifi_scan_config_t scfg = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        policy_event(WRP_EV_SCAN_END);
        return 0;
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) { policy_event(WRP_EV_SCAN_END); return 0; }

    uint16_t want = n;
    wifi_ap_record_t *recs = calloc(want, sizeof(wifi_ap_record_t));
    if (!recs) { esp_wifi_clear_ap_list(); policy_event(WRP_EV_SCAN_END); return 0; }
    esp_wifi_scan_get_ap_records(&want, recs);

    int count = 0;
    for (int i = 0; i < want && count < max; i++) {
        if (recs[i].ssid[0] == '\0') continue;
        /* esp_wifi returns records strongest-first; de-duplicate repeated SSIDs
         * (mesh/roaming APs) so the list shows each network once. */
        bool dup = false;
        for (int j = 0; j < count; j++)
            if (strcmp(out[j].ssid, (const char *)recs[i].ssid) == 0) { dup = true; break; }
        if (dup) continue;

        snprintf(out[count].ssid, sizeof out[count].ssid, "%s", (const char *)recs[i].ssid);
        out[count].secured = (recs[i].authmode != WIFI_AUTH_OPEN);
        int rssi = recs[i].rssi;                         /* dBm, roughly -30 .. -90 */
        int bars = (rssi >= -55) ? 3 : (rssi >= -67) ? 2 : (rssi >= -78) ? 1 : 0;
        out[count].strength = bars;
        count++;
    }
    free(recs);
    esp_wifi_clear_ap_list();

    /* Scanning knocked us off; the policy decides whether to re-associate. */
    policy_event(WRP_EV_SCAN_END);

    ESP_LOGI(TAG, "scan found %d networks", count);
    return count;
}

bool app_wifi_sync_time(int timeout_ms) {
    static bool started = false;
    if (!started) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&cfg) != ESP_OK) return false;
        started = true;
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timed out; clock may be wrong");
        return false;
    }
    time_t now = time(NULL);
    ESP_LOGI(TAG, "clock set: %ld", (long)now);
    return true;
}
