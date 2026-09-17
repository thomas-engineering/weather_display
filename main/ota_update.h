#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

/* Dual-chip OTA orchestration: fetches the latest GitHub Release's manifest,
 * updates the ESP32-P4 via esp_https_ota() with a post-flash SHA-256 check
 * against the manifest (esp_https_ota validates the image header itself, but
 * knows nothing about the externally-published hash), reboots, and on the
 * following boot optionally updates the ESP32-C6 coprocessor over esp-hosted's
 * own RPC OTA path. See tmp/ota_update_plan.md and the doc comment on
 * app_weather_ota_start() in app_weather.h for the background.
 *
 * Hardware/network adapter — not host-tested (see CLAUDE.md's
 * "Grenzen der Testebenen"). The manifest-parsing and version-compare logic
 * this calls into (components/app_logic/ota_manifest_parse.c,
 * ota_version_compare.c) is host-tested.
 *
 * Every function here is blocking and must be called from a worker task,
 * never the LVGL task. */

#include <stdbool.h>

typedef enum {
    OTA_RESULT_UP_TO_DATE,
    OTA_RESULT_UPDATED_REBOOTING, /* P4 image flashed and hash-verified; caller must esp_restart() */
    OTA_RESULT_ERROR,
} ota_result_t;

typedef enum {
    OTA_ERR_NONE,
    OTA_ERR_NETWORK,      /* couldn't reach GitHub, or a non-2xx/timeout on any request */
    OTA_ERR_MANIFEST,     /* manifest.json missing from the release or failed to parse */
    OTA_ERR_CHECKSUM,     /* downloaded P4 image's SHA-256 didn't match the manifest */
    OTA_ERR_FLASH,        /* esp_https_ota itself failed (bad image, flash write error, ...) */
    OTA_ERR_COPROCESSOR,  /* C6 phase failed (chunk checksum mismatch, RPC error, ...) */
    OTA_ERR_CANCELLED,    /* ota_update_request_cancel() was called mid-run */
} ota_error_t;

typedef struct {
    ota_result_t status;
    ota_error_t  error; /* only meaningful when status == OTA_RESULT_ERROR */
} ota_outcome_t;

/* percent is 0-100, meaningful only during the P4 download phase (the C6
 * phase over RPC doesn't report incremental progress back to the UI). */
typedef void (*ota_progress_cb_t)(int percent, void *ctx);

/* Fetches the latest release, compares its version against the running
 * firmware, and if newer downloads+flashes the P4 image. Does NOT call
 * esp_restart() itself — the caller does that on OTA_RESULT_UPDATED_REBOOTING
 * so it can update the UI/persist state first. `update_coprocessor` is
 * carried through only to be echoed back by ota_update_resume_after_boot()
 * on the next boot; this call never touches the C6. */
ota_outcome_t ota_update_run(ota_progress_cb_t on_progress, void *progress_ctx);

/* Cooperative cancel: sets a flag that ota_update_run() polls between HTTP
 * reads/esp_https_ota_perform() iterations. Safe to call from any task
 * (single bool, one writer here, one reader inside ota_update_run()) — in
 * practice always the LVGL task reacting to the dialog's "Cancel" button,
 * while ota_update_run() itself runs on its own dedicated OTA task (see
 * app_weather.c) so this never blocks. A cancel requested before or after a
 * run (no run in progress) is simply cleared at the next ota_update_run()
 * call, so callers don't need to pair every cancel with a run. */
void ota_update_request_cancel(void);

/* Call once at startup, after Wi-Fi is up, before anything else touches the
 * OTA subsystem. If the running partition is still pending verification
 * (i.e. this is the first boot after ota_update_run() flashed a new P4
 * image), optionally updates the C6 coprocessor per `update_coprocessor`
 * (the persisted app_prefs setting) and then marks the app valid, cancelling
 * the rollback timer. A no-op if the running partition is already valid. */
void ota_update_resume_after_boot(bool update_coprocessor);

#endif
