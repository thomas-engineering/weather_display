#include "ota_update.h"

#include "app_prefs.h"
#include "ota_manifest_parse.h"
#include "ota_types.h"
#include "ota_version_compare.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
/* SHA-256 via the PSA Crypto API, not the classic mbedtls_sha256_* context
 * functions — this IDF version's mbedtls (tf-psa-crypto) no longer exposes
 * mbedtls/sha256.h as a public header; psa/crypto.h is the supported path
 * (esp-tls itself already uses it, unconditionally — there's no Kconfig
 * gate to check). */
#include "psa/crypto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota_update";

/* Cooperative cancel flag — see ota_update_request_cancel()'s doc comment in
 * ota_update.h. Single bool, one writer (the LVGL task's Cancel handler),
 * one reader (this file's own run loop), so no lock is needed; `volatile`
 * is enough to keep the compiler from caching the read across loop
 * iterations. */
static volatile bool s_cancel_requested;

void ota_update_request_cancel(void) {
    s_cancel_requested = true;
}

#define GITHUB_RELEASES_URL "https://api.github.com/repos/thomas-engineering/weather_display/releases/latest"
#define OTA_USER_AGENT       "esp32-p4-weather-display"
#define JSON_BUF_MAX         (32 * 1024)
#define URL_MAX              256

/* esp-hosted's RPC OTA write caps a chunk at EH_RPC_OTA_CHUNK_MAX (1536 B,
 * eh_host_feat_rpc_ext_v2_types.h — not a public include path, so mirrored
 * here as a plain constant rather than pulled in). */
#define C6_OTA_CHUNK_SIZE 1536

/* esp_http_client's request-line tx buffer defaults to 512 B
 * (DEFAULT_HTTP_BUF_SIZE), sized for a short path — every URL fetched in
 * this file follows a GitHub redirect first, and GitHub's presigned S3
 * redirect targets run 600-1000+ characters (X-Amz-* query params), which
 * overflows that default and fails the request with "Out of buffer" before
 * it's even sent. */
#define OTA_HTTP_TX_BUF_SIZE 2048

/* esp_http_client's built-in redirect handling (esp_http_check_response(),
 * esp_http_client_set_redirection()) only runs inside esp_http_client_perform()
 * — it never fires for the manual open()+fetch_headers()+read() streaming
 * style used here and in app_weather.c's http_get(). GitHub's
 * browser_download_url always 302s to an objects.githubusercontent.com URL,
 * so every asset fetched in this file needs the redirect followed by hand.
 * Returns the final (non-redirect) status code, or a negative esp_err_t on
 * a connection/redirect failure. Leaves the client open and positioned
 * right after fetch_headers() on success, same as a plain open()+
 * fetch_headers() call. */
/* -1 signals "never got an HTTP status at all" (connect/redirect failure) —
 * distinct from any real status code, which is always >= 100. A prior
 * version returned negated esp_err_t values here, but ESP_FAIL is itself -1,
 * so -ESP_FAIL came out as +1 and printed as the nonsensical "HTTP 1" in the
 * caller's log line instead of a clear connection-failure message. */
static int http_open_following_redirects(esp_http_client_handle_t c) {
    for (int redirects = 0; redirects < 5; redirects++) {
        if (esp_http_client_open(c, 0) != ESP_OK) return -1;
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status < 300 || status >= 400) return status;

        esp_err_t err = esp_http_client_set_redirection(c);
        esp_http_client_close(c);
        if (err != ESP_OK) return -1;
    }
    return -1;
}

/* ---- small HTTP GET helper, mirrors app_weather.c's http_get() but sized
 * for the small JSON documents (release info, manifest.json) fetched here —
 * the multi-MB firmware images go through esp_https_ota instead, never
 * through this. Sets the User-Agent GitHub's API requires. Caller frees. ---- */
static char *fetch_small(const char *url) {
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = OTA_HTTP_TX_BUF_SIZE,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    esp_http_client_set_header(c, "User-Agent", OTA_USER_AGENT);

    char *body = NULL;
    int status = http_open_following_redirects(c);
    if (status != 200) {
        if (status < 0) ESP_LOGE(TAG, "connection failed for %.100s", url);
        else            ESP_LOGE(TAG, "HTTP %d for %.100s", status, url);
        goto done;
    }
    int64_t clen = esp_http_client_get_content_length(c);
    size_t cap = (clen > 0 && clen < JSON_BUF_MAX) ? (size_t)clen + 1 : 8192;
    body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) goto done;

    size_t len = 0;
    for (;;) {
        if (len + 1 >= cap) {
            if (cap >= JSON_BUF_MAX) { ESP_LOGE(TAG, "response over %d B", JSON_BUF_MAX); free(body); body = NULL; goto done; }
            size_t ncap = cap * 2 > JSON_BUF_MAX ? JSON_BUF_MAX : cap * 2;
            char *nb = heap_caps_realloc(body, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nb) { free(body); body = NULL; goto done; }
            body = nb; cap = ncap;
        }
        int r = esp_http_client_read(c, body + len, cap - len - 1);
        if (r < 0) { free(body); body = NULL; goto done; }
        if (r == 0) break;
        len += r;
    }
    body[len] = '\0';

done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return body;
}

static bool find_asset_url(const cJSON *release, const char *asset_name, char *out, size_t out_len) {
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
    if (!cJSON_IsArray(assets)) return false;
    int n = cJSON_GetArraySize(assets);
    for (int i = 0; i < n; i++) {
        const cJSON *a = cJSON_GetArrayItem(assets, i);
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(a, "name");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url");
        if (cJSON_IsString(name) && name->valuestring && strcmp(name->valuestring, asset_name) == 0 &&
            cJSON_IsString(url) && url->valuestring) {
            snprintf(out, out_len, "%s", url->valuestring);
            return true;
        }
    }
    return false;
}

/* Fetches the latest release's asset list and the manifest.json it points
 * to. Returns false (logging why) on any network/parse failure. */
static bool fetch_manifest_and_urls(ota_manifest_t *manifest,
                                     char *p4_url, size_t p4_url_len,
                                     char *c6_url, size_t c6_url_len) {
    char *release_json = fetch_small(GITHUB_RELEASES_URL);
    if (!release_json) { ESP_LOGE(TAG, "could not fetch %s", GITHUB_RELEASES_URL); return false; }

    cJSON *release = cJSON_Parse(release_json);
    free(release_json);
    if (!release) { ESP_LOGE(TAG, "release JSON did not parse"); return false; }

    char manifest_url[URL_MAX];
    bool ok = find_asset_url(release, "manifest.json", manifest_url, sizeof manifest_url) &&
              find_asset_url(release, "firmware_p4.bin", p4_url, p4_url_len) &&
              find_asset_url(release, "esp32c6_hosted_slave.bin", c6_url, c6_url_len);
    cJSON_Delete(release);
    if (!ok) { ESP_LOGE(TAG, "latest release is missing one of manifest.json/firmware_p4.bin/esp32c6_hosted_slave.bin"); return false; }

    char *manifest_json = fetch_small(manifest_url);
    if (!manifest_json) { ESP_LOGE(TAG, "could not fetch %s", manifest_url); return false; }
    bool parsed = ota_manifest_parse(manifest_json, manifest);
    free(manifest_json);
    if (!parsed) { ESP_LOGE(TAG, "manifest.json did not parse"); return false; }
    return true;
}

static void bytes_to_hex(const uint8_t *bytes, size_t n, char *out /* >= 2n+1 */) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hex[bytes[i] >> 4];
        out[2 * i + 1] = hex[bytes[i] & 0x0f];
    }
    out[2 * n] = '\0';
}

/* Reads `len` bytes back from `part` and returns their lowercase hex SHA-256
 * in `out_hex` (>= 65 B). False on a partition read error. */
static bool sha256_of_partition(const esp_partition_t *part, size_t len, char *out_hex) {
    uint8_t *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return false;

    psa_crypto_init(); /* idempotent; cheap to call again if something already has */
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) { free(buf); return false; }

    bool ok = true;
    for (size_t off = 0; off < len; ) {
        size_t chunk = len - off < 4096 ? len - off : 4096;
        if (esp_partition_read(part, off, buf, chunk) != ESP_OK ||
            psa_hash_update(&op, buf, chunk) != PSA_SUCCESS) { ok = false; break; }
        off += chunk;
    }
    free(buf);

    uint8_t digest[32];
    size_t digest_len = 0;
    if (ok) ok = (psa_hash_finish(&op, digest, sizeof digest, &digest_len) == PSA_SUCCESS);
    if (!ok) { psa_hash_abort(&op); return false; }

    bytes_to_hex(digest, digest_len, out_hex);
    return true;
}

static bool hex_equal_ci(const char *a, const char *b) {
    if (strlen(a) != strlen(b)) return false;
    for (; *a; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return true;
}

/* ---- P4 phase -------------------------------------------------------------- */

static ota_outcome_t run_p4_update(const char *p4_url, const char *expected_sha256,
                                    ota_progress_cb_t on_progress, void *progress_ctx) {
    ota_outcome_t out = { .status = OTA_RESULT_ERROR, .error = OTA_ERR_FLASH };

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) { out.error = OTA_ERR_FLASH; return out; }

    esp_http_client_config_t http_cfg = {
        .url = p4_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = OTA_HTTP_TX_BUF_SIZE,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_https_ota_begin: %s", esp_err_to_name(err)); out.error = OTA_ERR_NETWORK; return out; }

    int image_size = esp_https_ota_get_image_size(handle); /* -1 if chunked/unknown */
    for (;;) {
        if (s_cancel_requested) {
            ESP_LOGW(TAG, "P4 OTA cancelled");
            esp_https_ota_abort(handle);
            out.error = OTA_ERR_CANCELLED;
            return out;
        }
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        if (on_progress) {
            int read = esp_https_ota_get_image_len_read(handle);
            int percent = (image_size > 0 && read >= 0) ? (int)((int64_t)read * 100 / image_size) : 50;
            if (percent > 99) percent = 99;
            on_progress(percent, progress_ctx);
        }
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "P4 OTA download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        out.error = OTA_ERR_NETWORK;
        return out;
    }

    int image_len = esp_https_ota_get_image_len_read(handle);
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish: %s", esp_err_to_name(err));
        out.error = OTA_ERR_FLASH;
        return out;
    }

    /* esp_https_ota_finish() already switched the boot partition to `target`
     * — verify against the manifest's hash before trusting that, and revert
     * if it doesn't match rather than reboot into an unverified image. */
    char actual_sha256[65];
    if (image_len <= 0 || !sha256_of_partition(target, (size_t)image_len, actual_sha256)) {
        ESP_LOGE(TAG, "could not compute P4 image hash for verification");
        esp_ota_set_boot_partition(running);
        out.error = OTA_ERR_FLASH;
        return out;
    }
    if (!hex_equal_ci(actual_sha256, expected_sha256)) {
        ESP_LOGE(TAG, "P4 image SHA-256 mismatch: got %s, manifest says %s", actual_sha256, expected_sha256);
        esp_ota_set_boot_partition(running);
        out.error = OTA_ERR_CHECKSUM;
        return out;
    }

    if (on_progress) on_progress(100, progress_ctx);
    out.status = OTA_RESULT_UPDATED_REBOOTING;
    out.error = OTA_ERR_NONE;
    return out;
}

ota_outcome_t ota_update_run(ota_progress_cb_t on_progress, void *progress_ctx) {
    s_cancel_requested = false; /* clear any stale request left over from a prior run */

    ota_manifest_t manifest;
    char p4_url[URL_MAX], c6_url[URL_MAX];
    if (!fetch_manifest_and_urls(&manifest, p4_url, sizeof p4_url, c6_url, sizeof c6_url)) {
        return (ota_outcome_t){ .status = OTA_RESULT_ERROR, .error = OTA_ERR_MANIFEST };
    }
    if (s_cancel_requested) return (ota_outcome_t){ .status = OTA_RESULT_ERROR, .error = OTA_ERR_CANCELLED };

    const esp_app_desc_t *running = esp_app_get_description();
    if (!ota_is_newer(running->version, manifest.version)) {
        ESP_LOGI(TAG, "up to date: running %s, latest release %s", running->version, manifest.version);
        return (ota_outcome_t){ .status = OTA_RESULT_UP_TO_DATE, .error = OTA_ERR_NONE };
    }
    if (s_cancel_requested) return (ota_outcome_t){ .status = OTA_RESULT_ERROR, .error = OTA_ERR_CANCELLED };

    ESP_LOGI(TAG, "updating P4 from %s to %s", running->version, manifest.version);
    return run_p4_update(p4_url, manifest.p4_sha256, on_progress, progress_ctx);
}

/* ---- C6 phase, run only from ota_update_resume_after_boot() --------------- */

/* Streams `c6_url`'s body through esp-hosted's RPC OTA a chunk at a time,
 * verifying the manifest's SHA-256 over the bytes sent before activating.
 * Follows the pattern in
 * managed_components/espressif__esp_hosted/examples/ota/coprocessor_ota/mcu_host/main/main.c:
 * activate() marks the new image pending-boot and reboots the C6 itself; the
 * host then deinits and restarts to resync, matching that example. */
static bool run_c6_update(const char *c6_url, const char *expected_sha256) {
    esp_http_client_config_t cfg = {
        .url = c6_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = OTA_HTTP_TX_BUF_SIZE,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "User-Agent", OTA_USER_AGENT);

    bool ok = false;
    int status = http_open_following_redirects(c);
    if (status != 200) {
        if (status < 0) ESP_LOGE(TAG, "connection failed for %.100s", c6_url);
        else            ESP_LOGE(TAG, "HTTP %d for %.100s", status, c6_url);
        goto done;
    }

    if (esp_hosted_slave_ota_begin() != ESP_OK) { ESP_LOGE(TAG, "esp_hosted_slave_ota_begin failed"); goto done; }

    psa_crypto_init();
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS) goto done;

    uint8_t chunk[C6_OTA_CHUNK_SIZE];
    bool write_failed = false;
    for (;;) {
        int r = esp_http_client_read(c, (char *)chunk, sizeof chunk);
        if (r < 0) { write_failed = true; break; }
        if (r == 0) break;
        if (esp_hosted_slave_ota_write(chunk, (uint32_t)r) != ESP_OK ||
            psa_hash_update(&sha, chunk, (size_t)r) != PSA_SUCCESS) { write_failed = true; break; }
    }

    uint8_t digest[32];
    size_t digest_len = 0;
    bool hash_ok = !write_failed && psa_hash_finish(&sha, digest, sizeof digest, &digest_len) == PSA_SUCCESS;
    if (!hash_ok) psa_hash_abort(&sha);

    if (write_failed) { ESP_LOGE(TAG, "C6 image download/write failed"); goto done; }
    if (!hash_ok) { ESP_LOGE(TAG, "could not compute C6 image hash"); goto done; }
    if (esp_hosted_slave_ota_end() != ESP_OK) { ESP_LOGE(TAG, "esp_hosted_slave_ota_end failed"); goto done; }

    char actual_sha256[65];
    bytes_to_hex(digest, digest_len, actual_sha256);
    if (!hex_equal_ci(actual_sha256, expected_sha256)) {
        ESP_LOGE(TAG, "C6 image SHA-256 mismatch: got %s, manifest says %s", actual_sha256, expected_sha256);
        goto done;
    }

    if (esp_hosted_slave_ota_activate() != ESP_OK) { ESP_LOGE(TAG, "esp_hosted_slave_ota_activate failed"); goto done; }
    ok = true;

done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return ok;
}

void ota_update_resume_after_boot(bool update_coprocessor) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return; /* not a post-update boot — nothing to do */
    }

    ESP_LOGI(TAG, "first boot after a P4 update; running desc, and checking coprocessor if requested");

    if (update_coprocessor) {
        ota_manifest_t manifest;
        char p4_url[URL_MAX], c6_url[URL_MAX];
        if (fetch_manifest_and_urls(&manifest, p4_url, sizeof p4_url, c6_url, sizeof c6_url)) {
            if (run_c6_update(c6_url, manifest.c6_sha256)) {
                ESP_LOGI(TAG, "C6 coprocessor updated; restarting host to resync");
                esp_hosted_deinit();
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
                return; /* unreachable */
            }
            ESP_LOGE(TAG, "C6 update failed; keeping the coprocessor's current firmware");
        } else {
            ESP_LOGE(TAG, "could not fetch manifest for the coprocessor update; skipping it this boot");
        }
    }

    esp_ota_mark_app_valid_cancel_rollback();
}
