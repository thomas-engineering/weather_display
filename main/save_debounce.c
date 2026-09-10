#include "save_debounce.h"

void save_debounce_fire(esp_timer_handle_t *timer_slot, const char *name,
                         esp_timer_cb_t cb, uint32_t delay_us) {
    if (!*timer_slot) {
        const esp_timer_create_args_t args = {
            .callback = cb,
            .name = name,
        };
        if (esp_timer_create(&args, timer_slot) != ESP_OK) return;
    }
    if (esp_timer_is_active(*timer_slot)) esp_timer_stop(*timer_slot);
    esp_timer_start_once(*timer_slot, delay_us);
}
