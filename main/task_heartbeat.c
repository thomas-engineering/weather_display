#include "task_heartbeat.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "heartbeat";

typedef struct {
    const char *name;
    int64_t budget_us;
} hb_meta_t;

/* Budgets are deliberately generous — this isn't a tight-loop watchdog, it's
 * "did this task make progress at all in the last few minutes". weather_task
 * bounds a single do_refresh()/Wi-Fi op by NETWORK_MUTEX_TIMEOUT_MS (60s) plus
 * a couple of 15s HTTP timeouts; the OTA tasks are bounded by OTA_TOTAL_BUDGET_US
 * (10 min) plus the same mutex wait and some flash/reboot overhead. */
static const hb_meta_t s_meta[HB_COUNT] = {
    [HB_WEATHER]      = { "weather",      120 * 1000000LL },
    [HB_LIGHT_SENSOR] = { "light_sensor",  30 * 1000000LL },
    [HB_OTA]          = { "ota",          900 * 1000000LL },
    [HB_OTA_RESUME]   = { "ota_resume",   900 * 1000000LL },
};

static int64_t s_last_touch_us[HB_COUNT];
static bool s_active[HB_COUNT];

void task_heartbeat_touch(task_heartbeat_id_t id) {
    if (id < 0 || id >= HB_COUNT) return;
    s_last_touch_us[id] = esp_timer_get_time();
    s_active[id] = true;
}

void task_heartbeat_mark_idle(task_heartbeat_id_t id) {
    if (id < 0 || id >= HB_COUNT) return;
    s_active[id] = false;
}

void task_heartbeat_check(void) {
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < HB_COUNT; i++) {
        if (!s_active[i]) continue;
        int64_t age_us = now - s_last_touch_us[i];
        if (age_us > s_meta[i].budget_us) {
            ESP_LOGE(TAG, "task '%s' has not progressed in %llds (budget %llds)",
                     s_meta[i].name, (long long)(age_us / 1000000),
                     (long long)(s_meta[i].budget_us / 1000000));
        }
    }
}
