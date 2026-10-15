#include "hold_gesture.h"

void hold_gesture_init(hold_gesture_t *h) {
    h->pressed = false;
    h->fired = false;
    h->pressed_at_ms = 0;
}

void hold_gesture_press(hold_gesture_t *h, uint32_t now_ms) {
    h->pressed = true;
    h->fired = false;
    h->pressed_at_ms = now_ms;
}

void hold_gesture_release(hold_gesture_t *h) {
    h->pressed = false;
}

bool hold_gesture_poll(hold_gesture_t *h, uint32_t now_ms, uint32_t hold_ms) {
    if (!h->pressed || h->fired) return false;
    if ((uint32_t)(now_ms - h->pressed_at_ms) < hold_ms) return false;
    h->fired = true;
    return true;
}
