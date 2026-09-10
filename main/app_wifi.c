#include "app_wifi.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "nvs.h"
#include "storage_record.h"
#include "storage_backend_nvs.h"
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

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;
static int  s_retries;
static bool s_connected;
static bool s_want_reconnect = true;
/* Only auto-connect from the STA_START event when we actually have a network to
 * join; calling esp_wifi_connect() with an empty config just errors (0x300a). */
static bool s_connect_on_start;

static char s_ssid[33];
static char s_pass[65];

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_connect_on_start) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (!s_want_reconnect) return;
        if (s_retries < CONFIG_WEATHER_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retries++;
            ESP_LOGW(TAG, "retry %d/%d", s_retries, CONFIG_WEATHER_WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        s_retries = 0;
        s_connected = true;
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
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

    if (!ok_or_done(esp_netif_init())) { ESP_LOGE(TAG, "esp_netif_init failed"); return; }
    if (!ok_or_done(esp_event_loop_create_default())) { ESP_LOGE(TAG, "event loop failed"); return; }
    /* Exactly one STA netif for the life of the app: creating a second one trips
     * lwip's "netif already added" assert. */
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (!ok_or_done(esp_wifi_init(&cfg))) { ESP_LOGE(TAG, "esp_wifi_init failed"); return; }

    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &got_ip));

    load_credentials();
    s_connect_on_start = (s_ssid[0] != '\0');

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) { ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err)); return; }

    /* esp_wifi_start() is asynchronous over the SDIO link to the C6: STA_START and
     * the netif bring-up land afterwards. Scanning or connecting into that window
     * drives a second start action through the same netif and asserts in lwip.
     * Let the interface settle before anyone else touches it. */
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "station ready (credentials: %s)", s_connect_on_start ? "stored" : "none");
}

bool app_wifi_have_credentials(void) { return s_ssid[0] != '\0'; }
bool app_wifi_is_connected(void) { return s_connected; }

/* For the Settings > Device information dialog (Claude Design, 2026-09-10).
 * Buffers must be at least 16 bytes (IPSTR is "%d.%d.%d.%d"). Returns false
 * (buffers left untouched) if not connected or the netif has no address yet. */
bool app_wifi_get_ip_info(char *ip, size_t ip_len, char *dns, size_t dns_len, char *gw, size_t gw_len) {
    if (!s_connected || !s_sta_netif) return false;
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

/* Shared by both connect paths. */
static bool connect_locked(const char *ssid, const char *pass) {
    s_want_reconnect = true;
    s_connect_on_start = true;
    s_retries = 0;
    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wc = { 0 };
    snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", pass);
    /* An empty passphrase means an open network; demanding WPA2 would reject it. */
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    esp_wifi_disconnect();
    esp_wifi_connect();

    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) return true;
    ESP_LOGE(TAG, "could not connect to \"%s\"", ssid);
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
    s_connect_on_start = false;
    s_want_reconnect = false;
    esp_wifi_disconnect();
    s_connected = false;
    ESP_LOGI(TAG, "forgot stored network");
}

int app_wifi_scan(wx_wifi_network_t *out, int max) {
    if (!out || max <= 0) return 0;

    /* A scan drops the current association; don't let the event handler fight it. */
    s_want_reconnect = false;
    wifi_scan_config_t scfg = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scfg, true);
    s_want_reconnect = true;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        if (s_ssid[0] && !s_connected) esp_wifi_connect();
        return 0;
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) { if (s_ssid[0] && !s_connected) esp_wifi_connect(); return 0; }

    uint16_t want = n;
    wifi_ap_record_t *recs = calloc(want, sizeof(wifi_ap_record_t));
    if (!recs) { esp_wifi_clear_ap_list(); return 0; }
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

    /* Scanning knocked us off; re-associate so the forecast keeps refreshing. */
    if (s_ssid[0] && !s_connected) esp_wifi_connect();

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
