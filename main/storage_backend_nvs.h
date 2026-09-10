#ifndef STORAGE_BACKEND_NVS_H
#define STORAGE_BACKEND_NVS_H

#include "storage_backend.h"

/* The ESP32 NVS-backed storage_backend_t — the only medium implemented today.
 * NVS already wear-levels its partition internally; this backend does not
 * duplicate that, it just exposes NVS through the plain storage_backend_t
 * contract so storage_record.c (components/app_logic/, hardware-free) never
 * touches nvs.h directly. Stateless: every call opens/closes its own handle,
 * same as the nvs_open()-per-call pattern main/app_prefs.c and
 * main/app_wifi.c already used before this layer existed. */
extern const storage_backend_t storage_backend_nvs;

#endif
