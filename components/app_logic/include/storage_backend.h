#ifndef STORAGE_BACKEND_H
#define STORAGE_BACKEND_H

#include <stddef.h>

/* Return codes for storage_backend_t calls. Deliberately not esp_err_t —
 * this header has no IDF dependency, so a host-test fake backend can
 * implement the same contract and run on the host build. */
#define STORAGE_OK        0
#define STORAGE_NOT_FOUND (-1)
#define STORAGE_ERR       (-2)

/* A minimal blob key-value store contract. One backend instance per medium.
 * storage_backend_nvs.c (main/) is the only implementation today; an
 * SD-card or EEPROM backend is a matter of implementing these same four
 * calls, not touching app_prefs.c/app_wifi.c or storage_record.c again. */
typedef struct {
    /* On call, *inout_len is the caller's buffer size; on STORAGE_OK it is
     * set to the actual stored length. STORAGE_NOT_FOUND if the key does
     * not exist. buf may be left partially written on any non-OK return. */
    int (*read)(void *ctx, const char *ns, const char *key, void *buf, size_t *inout_len);
    int (*write)(void *ctx, const char *ns, const char *key, const void *buf, size_t len);
    int (*erase)(void *ctx, const char *ns, const char *key);
    int (*commit)(void *ctx);
    void *ctx;
} storage_backend_t;

#endif
