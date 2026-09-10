#include "storage_backend_nvs.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Stateless: every call opens its own handle and closes it before
 * returning, write()/erase() committing before close. So commit() below has
 * nothing left to flush for this backend — it's kept as a real, callable
 * function (not folded into write()'s return) so storage_record.c's
 * write()-then-commit() sequence works unchanged for any future backend
 * that does need a deferred commit. */

static int nvs_backend_read(void *ctx, const char *ns, const char *key, void *buf, size_t *inout_len) {
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return STORAGE_NOT_FOUND;
    esp_err_t err = nvs_get_blob(h, key, buf, inout_len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return STORAGE_NOT_FOUND;
    return err == ESP_OK ? STORAGE_OK : STORAGE_ERR;
}

static int nvs_backend_write(void *ctx, const char *ns, const char *key, const void *buf, size_t len) {
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return STORAGE_ERR;
    esp_err_t err = nvs_set_blob(h, key, buf, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? STORAGE_OK : STORAGE_ERR;
}

static int nvs_backend_erase(void *ctx, const char *ns, const char *key) {
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return STORAGE_ERR;
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? STORAGE_OK : STORAGE_ERR;
}

static int nvs_backend_commit(void *ctx) {
    (void)ctx;
    return STORAGE_OK;
}

const storage_backend_t storage_backend_nvs = {
    .read = nvs_backend_read,
    .write = nvs_backend_write,
    .erase = nvs_backend_erase,
    .commit = nvs_backend_commit,
    .ctx = NULL,
};
