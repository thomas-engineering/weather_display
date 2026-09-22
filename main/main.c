/* Weather display firmware — Waveshare ESP32-P4-WIFI6-Touch-LCD-7B.
 *
 * Brings up the 1024x600 MIPI-DSI panel and GT911 touch through the board BSP,
 * builds the LVGL UI imported from the Claude Design project, connects to Wi-Fi
 * (via the on-board ESP32-C6 over esp_hosted) and feeds the UI live Open-Meteo data.
 */

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "weather_ui.h"
#include "app_wifi.h"
#include "app_weather.h"
#include "app_prefs.h"
#include "app_favorites.h"
#include "app_light.h"
#include "task_heartbeat.h"
#include "app_heap_probe.h"
#include "sdkconfig.h"

static const char *TAG = "app";

/* ---- UI callbacks: all of these run on the LVGL task, so they must not block.
 * Every one of them just posts to the weather worker's queue. ---------------- */

static void on_search(const char *query)          { app_weather_search(query); }
static void on_select_city(int idx)               { app_weather_select_city(idx); }
static void on_refresh(void)                      { app_weather_refresh(); }
static void on_wifi_scan(void)                    { app_weather_wifi_scan(); }
static void on_favorite_toggle(int result_index)  { app_weather_toggle_favorite(result_index); }
static void on_favorite_select(int slot_index)    { app_weather_select_favorite(slot_index); }
static void on_favorite_remove(int slot_index)    { app_weather_remove_favorite(slot_index); }
static void on_ota_start(void)                    { app_weather_ota_start(); }
static void on_ota_cancel(void)                   { app_weather_ota_cancel(); }
static void on_ota_toggle_auto_update(bool on)    { app_weather_ota_toggle_auto_update(on); }
static void on_ota_toggle_update_coprocessor(bool on) { app_weather_ota_toggle_update_coprocessor(on); }

static void on_wifi_connect(const char *ssid, const char *password) {
    app_weather_wifi_connect(ssid, password);
}

static void on_wifi_forget(void) { app_weather_wifi_forget(); }

/* Applies immediately on every drag tick; only persists to NVS once the drag
 * settles, so dragging the slider doesn't wear out flash.
 *
 * bsp_display_brightness_set() itself is a cheap PWM duty write, but the BSP
 * function wrapping it also logs an ESP_LOGI line on every call (managed
 * component, not ours to edit) — LVGL's indev poll fires a
 * LV_EVENT_VALUE_CHANGED roughly every 30ms while dragging, so that used to
 * mean ~33 blocking UART writes/s from inside lv_timer_handler()'s own event
 * dispatch, on the same task that also polls the touch controller (found by
 * firmware-auditor Category G). Skipping the call when the percent hasn't
 * actually changed cuts that to one write per percentage point crossed. */
static void on_brightness(int percent, bool final) {
    static int s_last_applied = -1;
    if (percent != s_last_applied) {
        bsp_display_brightness_set(percent);
        s_last_applied = percent;
    }
    if (final) app_prefs_save_brightness(percent);
}

static void on_brightness_adaptive(bool on) {
    app_prefs_save_brightness_adaptive(on);
    app_light_set_adaptive(on);
}

/* Runs on light_sensor_task, not the LVGL task — see app_light.h's doc
 * comment on app_light_unavailable_cb_t. Mirrors a user turning the switch
 * off themselves: persist it so it doesn't come back on at the next boot,
 * and reflect it in both the switch's checked state and its enabled state
 * (a camera that just proved it can't stream shouldn't invite retrying from
 * the UI either). */
static void on_light_unavailable(void) {
    app_prefs_save_brightness_adaptive(false);
    if (bsp_display_lock(1000)) {
        weather_ui_set_brightness_adaptive(false);
        weather_ui_set_brightness_adaptive_available(false);
        bsp_display_unlock();
    } else {
        ESP_LOGW(TAG, "display lock timed out, adaptive switch will show stale state until next interaction");
    }
}

/* Runs on app_light's own sensor task, not the LVGL task — bsp_display_brightness_set()
 * is a plain LEDC duty write (safe from anywhere), but weather_ui_set_brightness()
 * touches LVGL objects and needs the display lock like any other cross-task UI update.
 * Deliberately doesn't persist to NVS: the stored "brightness" is the manual value to
 * fall back to if adaptive mode is turned off, not whatever the sensor last picked. */
static void on_ambient_brightness(int percent) {
    bsp_display_brightness_set(percent);
    if (bsp_display_lock(100)) {
        weather_ui_set_brightness(percent);
        bsp_display_unlock();
    } else {
        /* The backlight itself is already correct (bsp_display_brightness_set()
         * above doesn't need the lock) — only the settings slider's displayed
         * value missed this update. Self-corrects on the next sample a few
         * seconds later, which calls this same function again. */
        ESP_LOGW(TAG, "display lock timed out, brightness slider will lag one sample behind");
    }
}

static void on_settings_changed(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf, int auto_refresh_minutes) {
    app_prefs_save_settings(lang, t, w, tf, auto_refresh_minutes);
    /* Units are converted for display inside weather_ui.c, but the date/weekday
     * strings and the "real feel" sentence are composed by the app — so a language
     * or clock-format change needs a re-render, not a re-fetch. */
    app_weather_set_language(lang);
}

#if CONFIG_WEATHER_TOUCH_DEBUG
/* Reports where the touch controller thinks each press landed. Compare against
 * where the finger actually was to derive the right swap_xy / mirror_x / mirror_y
 * combination for this panel. */
static void touch_probe_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    static bool was_down;
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) continue;
        bool down = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
        if (down && !was_down) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            ESP_LOGW("touch", "reported x=%d y=%d   (panel is 1024x600)", (int)p.x, (int)p.y);
        }
        was_down = down;
    }
}
#endif

/* Diagnostic for the reported "laggy/missed taps" issue: this timer runs on
 * the LVGL task, the same task that polls the touch controller (touch runs in
 * polling mode — see the README's "GT911" note — since the panel's INT line
 * isn't wired to any GPIO). If the LVGL task itself gets delayed (lock
 * contention, the camera task's I2C traffic, CPU competition), touch reads
 * are delayed by exactly the same amount, which is what a human perceives as
 * a laggy or dropped tap. This has no known-good-vs-bad threshold of its own —
 * it just makes stalls visible in the log instead of only in a finger. Left
 * on permanently rather than behind CONFIG_WEATHER_TOUCH_DEBUG: the cost is
 * one integer subtraction every 20ms plus an ESP_LOGW on the rare tick that's
 * actually late.
 *
 * This alone can't say *why* a gap happened — lock wait, a slow widget
 * callback, and genuine flush/PPA cost all produce the same number (found by
 * firmware-auditor Category G). display_lock_probe.c's WARN line (emitted at
 * the moment a slow bsp_display_lock() hold is released) is correlatable by
 * timestamp against a warning here: if one lands within the same lvgl_stall
 * window, the lock wait explains it; if not, look at flush/PPA or a widget
 * callback instead. */
static void lvgl_stall_probe_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    static int64_t last_us;
    int64_t now_us = esp_timer_get_time();
    if (last_us != 0) {
        int64_t gap_ms = (now_us - last_us) / 1000;
        /* Nominal period is 20ms; only flag gaps that would actually be
         * noticeable as lag, not routine jitter. */
        if (gap_ms > 60) {
            ESP_LOGW(TAG, "lvgl_stall: task was blocked for %lldms (expected ~20ms) "
                          "-- touch reads were delayed by the same amount",
                     (long long)gap_ms);
        }
    }
    last_us = now_us;
}

/* Periodic liveness check for this project's own tasks — see
 * task_heartbeat.h for why CONFIG_ESP_TASK_WDT_INIT alone doesn't cover this.
 * Runs in the esp_timer task, well away from any of the tasks it's checking,
 * so a hang in one of them can't also stop this from firing. */
static void heartbeat_check_cb(void *arg) {
    (void)arg;
    task_heartbeat_check();
    /* Same timer, same reason it exists: a periodic look from outside every
     * task at something no single task can see. app_heap_probe rate-limits
     * itself, so 5s here only sets the resolution, not the log volume. */
    app_heap_probe_tick("idle");
}

/* Investigated using ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE + a hardware panel
 * mirror (esp_lcd_panel_mirror()) instead of software ROTATE_180, to dodge a
 * buffer-switch-release stall that cost up to ~260ms per tap under
 * TRIPLE_PARTIAL — on ESP-IDF v6.0.2, whose DPI panel driver didn't expose
 * the on_frame_buf_complete callback that mode wants, only the coarser
 * on_refresh_done. NONE mode alone measurably fixed the stall (confirmed on
 * hardware: 61-80ms, all explained by genuine render/flush cost), but NONE
 * mode refuses any adapter-side rotation, and hardware panel mirroring turned
 * out not to be a substitute on this panel: all four mirror_x/mirror_y
 * combinations were tested on hardware and none produced an upright image
 * (three gave an identical upside-down result, one gave visible garbage).
 * This EK79007 panel runs in MIPI DSI video mode (continuous DPI pixel
 * streaming) rather than command mode, and MADCTL-style mirroring is a
 * GRAM-addressing concept that doesn't apply to panels with no internal
 * framebuffer to re-address — consistent with mirror_x appearing to do
 * nothing observable across the tests. Back to the known-working
 * ROTATE_180 + TRIPLE_PARTIAL below. Switched to ESP-IDF 6.1 on 2026-09-18
 * (see design/README.md-adjacent memory notes), whose esp_lcd_mipi_dsi.h
 * does expose on_frame_buf_complete, and esp_lvgl_adapter's own CMake probe
 * (ESP_LCD_DPI_HAS_FRAME_BUF_COMPLETE_CB) confirmed picking it up — no
 * on_refresh_done-deprecated warning in the build. Whether that actually
 * shrinks the stall is still unmeasured (needs touch-driven on-device
 * timing, not just a boot log); treat it as still an open question, not a
 * confirmed fix, until someone taps through it and checks lvgl_stall. */

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_prefs_t prefs;
    app_prefs_load(&prefs);
    app_favorites_load();

    /* The BSP's own "ESP32_P4_EV" tag logs an INFO line on every
     * bsp_display_brightness_set() call (managed component, not ours to
     * edit) — without this, that line would still fire from inside
     * on_brightness()'s LVGL-task duty write whenever the deduplication
     * above lets a call through (found by firmware-auditor Category G). */
    esp_log_level_set("ESP32_P4_EV", ESP_LOG_WARN);

    /* Panel + touch. Rotation and touch mirroring match the vendor's own LVGL
     * example for this board (09_lvgl_demo_v9). TRIPLE_PARTIAL is the known
     * tradeoff here: it occasionally stalled the LVGL/touch task for up to
     * ~260ms on ESP-IDF v6.0.2, whose DPI panel driver lacked the
     * on_frame_buf_complete callback this mode wants for buffer-switch
     * release — see the comment above app_main() for what was tried and
     * ruled out, and for why IDF 6.1 (current) may or may not actually
     * improve this; it's the mode that actually renders this panel
     * upright either way. */
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_180,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        /* Measured on this unit, not copied from the vendor example: with the
         * example's mirror_x/mirror_y = 1 every tap landed point-mirrored (a tap at
         * the top-left reported (959,486) on a 1024x600 panel). The adapter already
         * accounts for ROTATE_180, so mirroring again inverted both axes a second
         * time. X still tracks X, so no axis swap is needed. */
        .touch_flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    /* ESP_LV_ADAPTER_DEFAULT_CONFIG() leaves this at -1 (no affinity), which lets
     * the scheduler run LVGL — and its touch-indev polling — on core 0, the same
     * core app_light.c pins its sensor task to. Pinning LVGL to core 1 removes
     * the *CPU* contention between the two (no more fighting over core 0 time
     * slices) instead of chasing it through priorities.
     *
     * It does NOT remove the *bus* contention: the two also share the I2C bus
     * (GT911 touch, OV5647 SCCB), and the I2C master's bus mutex isn't
     * preemptible or core-aware — a touch read on core 1 still waits out an
     * in-flight SCCB transaction the camera is running on core 0, same as if
     * both were on the same core. In practice this costs little: SCCB traffic
     * only bursts during start_streaming()'s VIDIOC_S_FMT (app_light.c) and its
     * retries, not per captured frame, but an earlier version of this comment
     * claimed the contention was "entirely removed", which isn't true and let
     * a real (if usually small) latency source go unmeasured (found by
     * firmware-auditor Category G). */
    cfg.lv_adapter_cfg.task_core_id = 1;
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    ESP_ERROR_CHECK(display != NULL ? ESP_OK : ESP_FAIL);
    bsp_display_brightness_set(prefs.brightness);

    /* Probe for the optional OV5647 before building the UI, so the Settings
     * panel's adaptive-brightness switch starts in the right enabled/disabled
     * state instead of flipping right after boot. Safe with no camera fitted
     * — see app_light.h. Needs the BSP's shared I2C bus, already up as part of
     * bsp_display_start_with_config() (touch bring-up brings it up too). */
    /* Callback registered before init: app_light_init() creates the sampling
     * task, so this ordering guarantees the callback exists before that task
     * could ever fire it — no dependence on the sample interval being long
     * enough to win the race. */
    app_light_set_callback(on_ambient_brightness);
    app_light_set_unavailable_callback(on_light_unavailable);
    bool have_light_sensor = app_light_init(bsp_i2c_get_handle());

    /* Baseline before the network comes up, so a later reading has something
     * to be compared against — see review/ota-sdio-buffer-2026-09-22.md. */
    app_heap_probe_init();
    app_heap_probe_log_now("boot");

    esp_timer_handle_t hb_timer;
    const esp_timer_create_args_t hb_timer_args = { .callback = &heartbeat_check_cb, .name = "hb_check" };
    ESP_ERROR_CHECK(esp_timer_create(&hb_timer_args, &hb_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(hb_timer, 5 * 1000 * 1000));

    ESP_ERROR_CHECK(bsp_display_lock(-1) ? ESP_OK : ESP_ERR_TIMEOUT);
    weather_ui_create(lv_screen_active());
    weather_ui_set_callbacks(on_search, on_select_city, on_refresh, on_settings_changed);
    weather_ui_set_wifi_callbacks(on_wifi_scan, on_wifi_connect, on_wifi_forget);
    weather_ui_set_brightness_callback(on_brightness);
    weather_ui_set_brightness_adaptive_callback(on_brightness_adaptive);
    weather_ui_set_favorite_callbacks(on_favorite_toggle, on_favorite_select, on_favorite_remove);
    weather_ui_set_ota_callbacks(on_ota_start, on_ota_cancel, on_ota_toggle_auto_update, on_ota_toggle_update_coprocessor);
    /* FIX: weather_ui_set_language()/weather_ui_set_units() both end by firing
     * the settings-changed callback (so language/unit-only edits from the UI
     * get persisted), which re-saves *every* field of app_prefs including
     * ui.auto_refresh_minutes — still 0 at that point if this call came
     * after them. That silently clobbered a freshly loaded non-zero
     * auto-refresh interval back to "Off" in s_prefs (and in NVS, via the
     * debounced save) on every cold boot, even though the Settings dialog
     * looked right afterward — it renders from ui.auto_refresh_minutes,
     * which this line does set correctly, just too late to matter to the
     * save that already fired. Setting it first avoids the whole race. */
    weather_ui_set_auto_refresh(prefs.auto_refresh_minutes);
    weather_ui_set_ota_auto_update(prefs.ota_auto_update);
    weather_ui_set_ota_update_coprocessor(prefs.ota_update_coprocessor);
    weather_ui_set_language(prefs.lang);
    weather_ui_set_units(prefs.temp_unit, prefs.wind_unit, prefs.time_fmt);
    weather_ui_set_brightness(prefs.brightness);
    weather_ui_set_brightness_adaptive_available(have_light_sensor);
    /* A camera once fitted and later removed shouldn't leave adaptive mode
     * silently stuck on — only honor the saved preference if it's actually
     * usable right now. */
    bool adaptive_on = have_light_sensor && prefs.brightness_adaptive;
    weather_ui_set_brightness_adaptive(adaptive_on);
    app_light_set_adaptive(adaptive_on);
    weather_ui_set_loading(true);
#if CONFIG_WEATHER_TOUCH_DEBUG
    lv_timer_create(touch_probe_cb, 30, NULL);
    ESP_LOGW(TAG, "touch debug on: tap the four corners and watch the log");
#endif
    lv_timer_create(lvgl_stall_probe_cb, 20, NULL);
    bsp_display_unlock();

    app_wifi_init();

    /* The initial connect attempt (and, on success, the SNTP sync) used to
     * block right here — up to ~30s for the connect timeout plus 15s for
     * SNTP — before the Wi-Fi setup screen or anything else on this screen
     * became reachable at all, on every boot where the stored network was
     * merely out of range (the common case, not the exception, once
     * CONFIG_WEATHER_WIFI_MAX_RETRY's disconnect-triggered burst is spent).
     * Moved into weather_task's own startup (main/app_weather.c) instead,
     * which already owns every other blocking network call and already goes
     * through s_network_mutex's finite timeout — found by firmware-auditor
     * Category K. */
    bool have_creds = app_wifi_have_credentials();
    if (bsp_display_lock(1000)) {
        weather_ui_set_network_status(have_creds ? WX_NET_RECONNECTING : WX_NET_OFFLINE);
        if (!have_creds) {
            weather_ui_set_loading(false);
            weather_ui_set_error(weather_strings[prefs.lang].error);
        }
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "starting weather worker");
    app_weather_start();

    /* No-op unless this boot follows a P4 OTA update (running partition still
     * ESP_OTA_IMG_PENDING_VERIFY) — see ota_update_resume_after_boot(). Posted
     * to the worker queue app_weather_start() just created regardless of
     * Wi-Fi state; that queue is FIFO, and the worker's own startup code
     * (its initial connect attempt) runs before it ever gets to processing
     * this command, so it still only reaches the network once Wi-Fi is up
     * or that attempt has given up. */
    app_weather_ota_resume_after_boot();

    if (!have_creds) {
        /* Nothing stored: put the setup screen up so the display can be joined
         * to a network with nothing but the touchscreen. A stored-but-gone
         * network is handled by weather_task's own initial connect attempt
         * instead (it opens this same screen itself if that attempt fails). */
        ESP_LOGI(TAG, "no stored network; opening Wi-Fi setup");
        if (bsp_display_lock(1000)) {
            weather_ui_open_wifi_setup();
            bsp_display_unlock();
        }
    }
}
