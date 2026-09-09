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
#include "app_light.h"
#include "sdkconfig.h"

static const char *TAG = "app";

/* ---- UI callbacks: all of these run on the LVGL task, so they must not block.
 * Every one of them just posts to the weather worker's queue. ---------------- */

static void on_search(const char *query)          { app_weather_search(query); }
static void on_select_city(int idx)               { app_weather_select_city(idx); }
static void on_refresh(void)                      { app_weather_refresh(); }
static void on_wifi_scan(void)                    { app_weather_wifi_scan(); }

static void on_wifi_connect(const char *ssid, const char *password) {
    app_weather_wifi_connect(ssid, password);
}

static void on_wifi_forget(void) { app_weather_wifi_forget(); }

/* Applies immediately (cheap PWM duty write) on every drag tick; only persists to
 * NVS once the drag settles, so dragging the slider doesn't wear out flash. */
static void on_brightness(int percent, bool final) {
    bsp_display_brightness_set(percent);
    if (final) app_prefs_save_brightness(percent);
}

static void on_brightness_adaptive(bool on) {
    app_prefs_save_brightness_adaptive(on);
    app_light_set_adaptive(on);
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

static void on_settings_changed(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf) {
    app_prefs_save_settings(lang, t, w, tf);
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
 * actually late. */
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

/* Investigated using ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE + a hardware panel
 * mirror (esp_lcd_panel_mirror()) instead of software ROTATE_180, to dodge a
 * buffer-switch-release stall that cost up to ~260ms per tap under
 * TRIPLE_PARTIAL (ESP-IDF v6.0.2's DPI panel driver doesn't expose the
 * on_frame_buf_complete callback that mode needs, only the coarser
 * on_refresh_done). NONE mode alone measurably fixed the stall (confirmed on
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
 * ROTATE_180 + TRIPLE_PARTIAL below; the occasional large stall is an
 * accepted limitation until esp_lvgl_adapter or ESP-IDF exposes proper
 * buffer-release timing for MIPI DSI. */

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_prefs_t prefs;
    app_prefs_load(&prefs);

    /* Panel + touch. Rotation and touch mirroring match the vendor's own LVGL
     * example for this board (09_lvgl_demo_v9). TRIPLE_PARTIAL is the known
     * tradeoff here: it occasionally stalls the LVGL/touch task for up to
     * ~260ms (ESP-IDF v6.0.2's DPI panel driver lacks the on_frame_buf_complete
     * callback this mode wants for buffer-switch release), but it's the mode
     * that actually renders this panel upright — see the comment above
     * app_main() for what was tried and ruled out. */
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
     * core app_light.c pins its sensor task to. The two also share the I2C bus
     * (GT911 touch, OV5647 SCCB), and a touch read has to wait out an in-flight
     * I2C transaction from the camera regardless of task priority, since a bus
     * mutex isn't preemptible. Pinning LVGL to core 1 removes that contention
     * entirely instead of chasing it through priorities. */
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
    bool have_light_sensor = app_light_init(bsp_i2c_get_handle());

    ESP_ERROR_CHECK(bsp_display_lock(-1) ? ESP_OK : ESP_ERR_TIMEOUT);
    weather_ui_create(lv_screen_active());
    weather_ui_set_callbacks(on_search, on_select_city, on_refresh, on_settings_changed);
    weather_ui_set_wifi_callbacks(on_wifi_scan, on_wifi_connect, on_wifi_forget);
    weather_ui_set_brightness_callback(on_brightness);
    weather_ui_set_brightness_adaptive_callback(on_brightness_adaptive);
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

    bool online = false;
    if (app_wifi_have_credentials()) {
        online = app_wifi_connect();
        if (online) app_wifi_sync_time(15000);
    }

    if (bsp_display_lock(1000)) {
        weather_ui_set_network_status(online ? WX_NET_ONLINE : WX_NET_OFFLINE);
        if (!online) {
            weather_ui_set_loading(false);
            weather_ui_set_error(weather_strings[prefs.lang].error);
        }
        bsp_display_unlock();
    }

    /* The worker owns every blocking call from here on, including the Wi-Fi scan
     * and connect the setup screen triggers — so start it before opening that screen. */
    ESP_LOGI(TAG, "starting weather worker");
    app_weather_start();

    if (!online) {
        /* Nothing stored, or the stored network is gone: put the setup screen up so
         * the display can be joined to a network with nothing but the touchscreen. */
        ESP_LOGI(TAG, "no connection; opening Wi-Fi setup");
        if (bsp_display_lock(1000)) {
            weather_ui_open_wifi_setup();
            bsp_display_unlock();
        }
    }
}
