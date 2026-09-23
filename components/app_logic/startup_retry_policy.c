#include "startup_retry_policy.h"

/* Delay before retry N (1-based: delay before the 2nd, 3rd, ... attempt).
 * Short first, longer after: a transient DNS/SDIO hiccup usually clears in
 * a few seconds, but if it hasn't by the last retry there is little point
 * hammering it faster than the SDIO link itself can recover. */
static const int s_delay_ms[] = { 3000, 8000, 15000 };
#define DELAY_COUNT (int)(sizeof(s_delay_ms) / sizeof(s_delay_ms[0]))

void startup_retry_policy_init(startup_retry_policy_t *p, int max_attempts) {
    if (!p) return;
    p->attempt = 0;
    p->max_attempts = (max_attempts > 0) ? max_attempts : STARTUP_RETRY_MAX_ATTEMPTS;
}

bool startup_retry_policy_next(startup_retry_policy_t *p, int *delay_ms) {
    if (!p) return false;
    p->attempt++;
    if (p->attempt >= p->max_attempts) return false;

    int idx = p->attempt - 1;
    if (idx >= DELAY_COUNT) idx = DELAY_COUNT - 1;
    if (delay_ms) *delay_ms = s_delay_ms[idx];
    return true;
}
