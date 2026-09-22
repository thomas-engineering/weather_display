#ifndef TASK_HEARTBEAT_H
#define TASK_HEARTBEAT_H

#include <stdbool.h>

/* Liveness monitor for this project's own long-running tasks — a
 * complement to CONFIG_ESP_TASK_WDT_INIT, which here only ever watches the
 * two idle tasks (esp_task_wdt_add() is never called on any task this
 * project creates). A task blocked *asleep* — the exact shape of every real
 * hang this project has hit (a wedged SDIO/RPC call, an unbounded mutex
 * wait) — keeps the idle task running fine and so goes completely
 * unnoticed by that watchdog (found by firmware-auditor Category K).
 *
 * Each monitored task calls task_heartbeat_touch() from a point in its own
 * loop reached at least once per its budget under normal operation — not
 * from inside a single long blocking call, since the point is to notice
 * when a *whole iteration* stops happening, not to paper over one. A
 * periodic check (task_heartbeat_check(), from main.c's own timer) logs an
 * ERROR line for any active task whose last touch is older than its
 * budget. */

typedef enum {
    HB_WEATHER,      /* main/app_weather.c: weather_task */
    HB_LIGHT_SENSOR, /* main/app_light.c: light_sensor_task */
    HB_OTA,          /* main/app_weather.c: ota_worker_task, only while running */
    HB_OTA_RESUME,   /* main/app_weather.c: ota_resume_task, only while running */
    HB_COUNT,
} task_heartbeat_id_t;

void task_heartbeat_touch(task_heartbeat_id_t id);

/* Marks `id` as not currently running, so task_heartbeat_check() skips it
 * instead of flagging a task that isn't supposed to be alive right now
 * (the OTA tasks only exist for the duration of one check/download). */
void task_heartbeat_mark_idle(task_heartbeat_id_t id);

/* Whether `id` is currently marked as running — the OTA ids are the useful
 * ones, since they are the only tasks that come and go. Lets a caller hold
 * off on something disruptive (main/app_wifi.c defers a recovery restart)
 * while an update is in flight, without inventing a second piece of
 * bookkeeping for the same fact. */
bool task_heartbeat_is_active(task_heartbeat_id_t id);

void task_heartbeat_check(void);

#endif
