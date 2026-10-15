#ifndef HOLD_GESTURE_H
#define HOLD_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

/* Hardware-free "press and hold" detector. The UI feeds it press/release
 * events and a periodic poll with a millisecond clock; it reports the hold
 * exactly once per press. Used for the hidden screen saver trigger (hold the
 * header clock), see docs/screensaver-test.md. */

typedef struct {
    bool pressed;
    bool fired;
    uint32_t pressed_at_ms;
} hold_gesture_t;

void hold_gesture_init(hold_gesture_t *h);
void hold_gesture_press(hold_gesture_t *h, uint32_t now_ms);
/* Also the right call when the finger slides off (LVGL's PRESS_LOST). */
void hold_gesture_release(hold_gesture_t *h);
/* True once, when the press has lasted at least hold_ms. Unsigned subtraction,
 * so a wrapping millisecond counter is fine. */
bool hold_gesture_poll(hold_gesture_t *h, uint32_t now_ms, uint32_t hold_ms);

#endif
