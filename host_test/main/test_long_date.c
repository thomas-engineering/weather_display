/* Host unit tests for components/app_logic/long_date. */

#include "unity.h"
#include "long_date.h"
#include <string.h>

static const char *const k_wdays[7] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };
static const char *const k_months[12] = { "Jan", "Feb", "Mar", "Apr", "Mai", "Jun",
                                          "Jul", "Aug", "Sep", "Okt", "Nov", "Dez" };

/* 2026-11-02 is a Monday; tm_wday 1, tm_mon 10. */
#define Y 2026
#define MONTH0 10
#define MDAY 2
#define WDAY 1

static void test_each_style_orders_and_separates_the_pieces(void) {
    char out[64];
    long_date_format(out, sizeof out, LONG_DATE_MDY, Y, MONTH0, MDAY, WDAY, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("Mo, Nov 2, 2026", out);
    long_date_format(out, sizeof out, LONG_DATE_D_DOT_MY, Y, MONTH0, MDAY, WDAY, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("Mo, 2. Nov 2026", out);
    long_date_format(out, sizeof out, LONG_DATE_D_DE_M_DE_Y, Y, MONTH0, MDAY, WDAY, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("Mo, 2 de Nov de 2026", out);
    long_date_format(out, sizeof out, LONG_DATE_D_M_Y, Y, MONTH0, MDAY, WDAY, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("Mo 2 Nov 2026", out);
}

static void test_out_of_range_indices_fall_back_to_the_first_name(void) {
    char out[64];
    long_date_format(out, sizeof out, LONG_DATE_MDY, Y, -1, 5, 7, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("So, Jan 5, 2026", out);
    long_date_format(out, sizeof out, LONG_DATE_MDY, Y, 12, 5, -3, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("So, Jan 5, 2026", out);
}

static void test_a_short_buffer_is_cut_and_terminated(void) {
    char out[8];
    memset(out, 'x', sizeof out);
    long_date_format(out, sizeof out, LONG_DATE_MDY, Y, MONTH0, MDAY, WDAY, k_wdays, k_months);
    TEST_ASSERT_EQUAL_STRING("Mo, Nov", out);
}

static void test_a_zero_sized_buffer_is_left_alone(void) {
    char out[2] = { 'a', 'b' };
    long_date_format(out, 0, LONG_DATE_MDY, Y, MONTH0, MDAY, WDAY, k_wdays, k_months);
    TEST_ASSERT_EQUAL_CHAR('a', out[0]);
}

void test_long_date_run(void) {
    RUN_TEST(test_each_style_orders_and_separates_the_pieces);
    RUN_TEST(test_out_of_range_indices_fall_back_to_the_first_name);
    RUN_TEST(test_a_short_buffer_is_cut_and_terminated);
    RUN_TEST(test_a_zero_sized_buffer_is_left_alone);
}
