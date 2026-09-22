/*
 * Host unit tests for components/app_logic/heap_watch.
 *
 * The policy behind the heap instrumentation added while chasing the OTA
 * download that starved the SDIO link to the C6
 * (review/ota-sdio-buffer-2026-09-22.md). Two properties matter for it to be
 * usable as a measurement: an episode has to have a visible start *and* end
 * in the log, and it must not drown the log while it lasts — the failing OTA
 * run produced ~180 OOM lines in nine minutes with no numbers on any of them.
 */

#include "unity.h"
#include "heap_watch.h"

#define LOW_FREE    (32u * 1024u)
#define LOW_LARGEST (8u * 1024u)
#define REPORT_MS   30000
#define LOW_MS      2000

static void init(heap_watch_t *w) {
    heap_watch_init(w, LOW_FREE, LOW_LARGEST, REPORT_MS, LOW_MS);
}

/* A comfortable sample: nothing here is near either threshold. */
static heap_watch_sample_t ok_sample(void) {
    heap_watch_sample_t s = { .dma_free = 200000, .dma_largest = 100000,
                              .dma_min_free = 180000, .psram_free = 20000000 };
    return s;
}

static void test_first_sample_always_reports(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    TEST_ASSERT_EQUAL_INT_MESSAGE(HEAP_WATCH_REPORT, heap_watch_observe(&w, &s, 0),
                                  "no baseline line, so later numbers have nothing to compare against");
}

static void test_quiet_between_periodic_reports(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    (void)heap_watch_observe(&w, &s, 0);
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_SILENT, heap_watch_observe(&w, &s, 5000));
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_SILENT, heap_watch_observe(&w, &s, REPORT_MS - 1));
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_REPORT, heap_watch_observe(&w, &s, REPORT_MS));
}

static void test_crossing_into_low_reports_immediately(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    (void)heap_watch_observe(&w, &s, 0);

    /* Plenty free overall, but too fragmented to hand out an SDIO buffer. */
    s.dma_largest = LOW_LARGEST - 1;
    TEST_ASSERT_EQUAL_INT_MESSAGE(HEAP_WATCH_LOW, heap_watch_observe(&w, &s, 1000),
                                  "fragmentation alone must trip the warning");
    TEST_ASSERT_TRUE(heap_watch_is_low(&w));
    TEST_ASSERT_EQUAL_INT(1, w.episodes);
}

static void test_low_episode_is_rate_limited_but_keeps_reporting(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    (void)heap_watch_observe(&w, &s, 0);
    s.dma_free = LOW_FREE - 1;
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_LOW, heap_watch_observe(&w, &s, 1000));
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_SILENT, heap_watch_observe(&w, &s, 1500));
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_LOW, heap_watch_observe(&w, &s, 1000 + LOW_MS));
}

static void test_recovery_reports_even_if_not_due(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    (void)heap_watch_observe(&w, &s, 0);
    s.dma_free = LOW_FREE - 1;
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_LOW, heap_watch_observe(&w, &s, 1000));

    s = ok_sample();
    TEST_ASSERT_EQUAL_INT_MESSAGE(HEAP_WATCH_REPORT, heap_watch_observe(&w, &s, 1100),
                                  "an episode with no end line cannot be measured");
    TEST_ASSERT_FALSE(heap_watch_is_low(&w));
}

static void test_new_minimum_reports_between_periods(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    (void)heap_watch_observe(&w, &s, 0);

    /* Still far above the thresholds, but the trend is what matters here. */
    s.dma_largest -= HEAP_WATCH_NEW_MIN_MARGIN;
    TEST_ASSERT_EQUAL_INT_MESSAGE(HEAP_WATCH_REPORT, heap_watch_observe(&w, &s, 100),
                                  "a slow drift between two periodic lines would be invisible");
}

static void test_a_silent_sample_does_not_consume_the_margin(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    (void)heap_watch_observe(&w, &s, 0);

    /* Half the margin: not worth a line on its own... */
    s.dma_largest -= HEAP_WATCH_NEW_MIN_MARGIN / 2;
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_SILENT, heap_watch_observe(&w, &s, 100));
    /* ...and it must not have moved the bar, so the full drop still reports. */
    s.dma_largest -= HEAP_WATCH_NEW_MIN_MARGIN / 2;
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_REPORT, heap_watch_observe(&w, &s, 200));
}

static void test_zero_arguments_fall_back_to_defaults(void) {
    heap_watch_t w;
    heap_watch_init(&w, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT(HEAP_WATCH_LOW_FREE, (unsigned)w.low_free);
    TEST_ASSERT_EQUAL_UINT(HEAP_WATCH_LOW_LARGEST, (unsigned)w.low_largest);
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_REPORT_PERIOD_MS, (int)w.report_period_ms);
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_LOW_PERIOD_MS, (int)w.low_period_ms);
}

static void test_null_arguments_are_silent(void) {
    heap_watch_t w;
    init(&w);
    heap_watch_sample_t s = ok_sample();
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_SILENT, heap_watch_observe(NULL, &s, 0));
    TEST_ASSERT_EQUAL_INT(HEAP_WATCH_SILENT, heap_watch_observe(&w, NULL, 0));
    TEST_ASSERT_FALSE(heap_watch_is_low(NULL));
}

void test_heap_watch_run(void) {
    RUN_TEST(test_first_sample_always_reports);
    RUN_TEST(test_quiet_between_periodic_reports);
    RUN_TEST(test_crossing_into_low_reports_immediately);
    RUN_TEST(test_low_episode_is_rate_limited_but_keeps_reporting);
    RUN_TEST(test_recovery_reports_even_if_not_due);
    RUN_TEST(test_new_minimum_reports_between_periods);
    RUN_TEST(test_a_silent_sample_does_not_consume_the_margin);
    RUN_TEST(test_zero_arguments_fall_back_to_defaults);
    RUN_TEST(test_null_arguments_are_silent);
}
