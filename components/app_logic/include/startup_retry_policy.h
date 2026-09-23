#ifndef STARTUP_RETRY_POLICY_H
#define STARTUP_RETRY_POLICY_H

#include <stdbool.h>

/* Hardware-free "keep trying a startup fetch a few times" policy. No IDF
 * headers, no driver/, no freertos/ — see CLAUDE.md's "Codeorganisation".
 *
 * The boot-time weather fetch used to get exactly one attempt: if it failed,
 * the device showed "Could not load weather" until the next scheduled
 * refresh, up to 30 minutes later. The getaddrinfo() EAI_FAIL failures seen
 * on hardware 2026-09-23 turned out to be lwIP binding to local ports the C6
 * never forwards (fixed in the top-level CMakeLists.txt); this retry stays
 * as a safety net for genuinely transient failures. SNTP doesn't use it:
 * lwIP keeps retrying SNTP in the background on its own.
 *
 * This policy only decides the retry schedule; main/app_weather.c owns the
 * actual fetch and the vTaskDelay(). */

typedef struct {
    int attempt;       /* 0-based count of attempts made so far */
    int max_attempts;  /* total attempts allowed, including the first */
} startup_retry_policy_t;

/* max_attempts <= 0 falls back to the default below. */
#define STARTUP_RETRY_MAX_ATTEMPTS 4

void startup_retry_policy_init(startup_retry_policy_t *p, int max_attempts);

/* Call after an attempt fails. Records the attempt and, if another is
 * allowed, returns true with *delay_ms set to how long to wait before it.
 * Returns false (leaving *delay_ms untouched) once max_attempts is used up. */
bool startup_retry_policy_next(startup_retry_policy_t *p, int *delay_ms);

#endif
