/*
 * Host-Unit-Tests fuer components/app_logic/weather_forecast_parse.
 * Laeuft nativ auf dem Linux-Target, ohne Chip und ohne Emulator.
 *
 * Die Fixture ist eine verkuerzte, aber strukturell echte Open-Meteo-Antwort
 * (zwei Tage, der erste mit vollen 24 Stunden, der zweite mit nur 3 — genau
 * der Fall, den app_weather.c's "letzter Tag ist unvollstaendig" Pfad
 * abdecken muss).
 */

#include "unity.h"
#include "weather_forecast_parse.h"

#include <string.h>

static const char *k_fixture =
    "{"
    "\"utc_offset_seconds\":3600,"
    "\"current\":{"
        "\"temperature_2m\":15.7,"
        "\"relative_humidity_2m\":68,"
        "\"apparent_temperature\":13.9,"
        "\"precipitation_probability\":35,"
        "\"wind_speed_10m\":12.0,"
        "\"weather_code\":0"
    "},"
    "\"hourly\":{"
        "\"time\":["
            "\"2026-09-08T00:00\",\"2026-09-08T01:00\",\"2026-09-08T02:00\",\"2026-09-08T03:00\","
            "\"2026-09-08T04:00\",\"2026-09-08T05:00\",\"2026-09-08T06:00\",\"2026-09-08T07:00\","
            "\"2026-09-08T08:00\",\"2026-09-08T09:00\",\"2026-09-08T10:00\",\"2026-09-08T11:00\","
            "\"2026-09-08T12:00\",\"2026-09-08T13:00\",\"2026-09-08T14:00\",\"2026-09-08T15:00\","
            "\"2026-09-08T16:00\",\"2026-09-08T17:00\",\"2026-09-08T18:00\",\"2026-09-08T19:00\","
            "\"2026-09-08T20:00\",\"2026-09-08T21:00\",\"2026-09-08T22:00\",\"2026-09-08T23:00\","
            "\"2026-09-09T00:00\",\"2026-09-09T01:00\",\"2026-09-09T02:00\""
        "],"
        "\"temperature_2m\":["
            "10,10,10,10,10,10,10,10,10,10,10,10,"
            "20,20,20,20,20,20,20,20,20,20,20,20,"
            "8,8,8"
        "],"
        "\"precipitation\":["
            "0,0,0,0,0,0,0,0,0,0,0,0,"
            "1,1,1,1,1,1,1,1,1,1,1,1,"
            "0,0,0"
        "]"
    "},"
    "\"daily\":{"
        "\"time\":[\"2026-09-08\",\"2026-09-09\"],"
        "\"weather_code\":[0,61],"
        "\"temperature_2m_max\":[20.0,16.0],"
        "\"temperature_2m_min\":[10.0,8.0],"
        "\"apparent_temperature_max\":[18.5,14.0],"
        "\"apparent_temperature_min\":[8.5,6.0],"
        "\"precipitation_probability_max\":[35,80],"
        "\"wind_speed_10m_max\":[12.0,24.5]"
    "}"
    "}";

static void test_parses_current_conditions(void) {
    wfp_forecast_t f;
    TEST_ASSERT_TRUE(weather_forecast_parse(k_fixture, &f));

    TEST_ASSERT_EQUAL_INT(3600, f.utc_offset_sec);
    TEST_ASSERT_EQUAL_INT(0, f.weather_code);
    TEST_ASSERT_EQUAL_INT(68, f.humidity_pct);
    TEST_ASSERT_EQUAL_INT(35, f.precip_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.7f, f.temp_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.9f, f.feels_like_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, f.wind_kmh);
}

static void test_parses_daily_series(void) {
    wfp_forecast_t f;
    TEST_ASSERT_TRUE(weather_forecast_parse(k_fixture, &f));

    TEST_ASSERT_EQUAL_INT(2, f.day_count);

    TEST_ASSERT_EQUAL_STRING("2026-09-08", f.days[0].iso_date);
    TEST_ASSERT_EQUAL_INT(0, f.days[0].weather_code);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, f.days[0].temp_max_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, f.days[0].temp_min_c);

    TEST_ASSERT_EQUAL_STRING("2026-09-09", f.days[1].iso_date);
    TEST_ASSERT_EQUAL_INT(61, f.days[1].weather_code);
    TEST_ASSERT_EQUAL_INT(80, f.days[1].precip_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.5f, f.days[1].wind_max_kmh);
}

static void test_slices_hourly_per_day_including_partial_last_day(void) {
    wfp_forecast_t f;
    TEST_ASSERT_TRUE(weather_forecast_parse(k_fixture, &f));

    TEST_ASSERT_TRUE(f.hourly[0].valid);
    TEST_ASSERT_EQUAL_INT(24, f.hourly[0].count);
    TEST_ASSERT_EQUAL_INT(0, f.hourly[0].hour[0]);
    TEST_ASSERT_EQUAL_INT(12, f.hourly[0].hour[12]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, f.hourly[0].temp_c[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, f.hourly[0].temp_c[12]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, f.hourly[0].precip_mm[12]);

    /* Day 2 only has 3 hourly entries in the fixture. */
    TEST_ASSERT_TRUE(f.hourly[1].valid);
    TEST_ASSERT_EQUAL_INT(3, f.hourly[1].count);
    TEST_ASSERT_EQUAL_INT(0, f.hourly[1].hour[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, f.hourly[1].temp_c[0]);
}

static void test_missing_sunrise_sunset_uv_leaves_uv_invalid(void) {
    /* k_fixture predates the 2026-09-12 sync's sunrise/sunset/uv_index_max
     * fields — parsing an older/smaller response must still succeed, just
     * with uv_valid left false and the ISO strings empty, not fail outright. */
    wfp_forecast_t f;
    TEST_ASSERT_TRUE(weather_forecast_parse(k_fixture, &f));
    TEST_ASSERT_FALSE(f.uv_valid);
    TEST_ASSERT_EQUAL_STRING("", f.sunrise_iso);
    TEST_ASSERT_EQUAL_STRING("", f.sunset_iso);
}

static const char *k_fixture_with_sun_uv =
    "{"
    "\"utc_offset_seconds\":3600,"
    "\"current\":{\"temperature_2m\":15.7,\"weather_code\":0},"
    "\"daily\":{"
        "\"time\":[\"2026-09-08\",\"2026-09-09\"],"
        "\"weather_code\":[0,61],"
        "\"temperature_2m_max\":[20.0,16.0],"
        "\"temperature_2m_min\":[10.0,8.0],"
        "\"sunrise\":[\"2026-09-08T06:47\",\"2026-09-09T06:48\"],"
        "\"sunset\":[\"2026-09-08T19:32\",\"2026-09-09T19:30\"],"
        "\"uv_index_max\":[5.4,3.1]"
    "}"
    "}";

static void test_parses_sunrise_sunset_uv_for_today_only(void) {
    wfp_forecast_t f;
    TEST_ASSERT_TRUE(weather_forecast_parse(k_fixture_with_sun_uv, &f));
    TEST_ASSERT_TRUE(f.uv_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.4f, f.uv_index_max);
    TEST_ASSERT_EQUAL_STRING("2026-09-08T06:47", f.sunrise_iso);
    TEST_ASSERT_EQUAL_STRING("2026-09-08T19:32", f.sunset_iso);
}

static void test_rejects_malformed_json(void) {
    wfp_forecast_t f;
    TEST_ASSERT_FALSE(weather_forecast_parse("not json", &f));
    TEST_ASSERT_FALSE(weather_forecast_parse(NULL, &f));
}

static void test_rejects_json_missing_current_or_daily(void) {
    wfp_forecast_t f;
    TEST_ASSERT_FALSE(weather_forecast_parse("{\"daily\":{}}", &f));
    TEST_ASSERT_FALSE(weather_forecast_parse("{\"current\":{}}", &f));
}

/* Called from test_main.c's single main() — the host_test project builds one
 * executable for every component under test, so only one file may define
 * main()/setUp()/tearDown(). */
void test_weather_forecast_parse_run(void) {
    RUN_TEST(test_parses_current_conditions);
    RUN_TEST(test_parses_daily_series);
    RUN_TEST(test_slices_hourly_per_day_including_partial_last_day);
    RUN_TEST(test_missing_sunrise_sunset_uv_leaves_uv_invalid);
    RUN_TEST(test_parses_sunrise_sunset_uv_for_today_only);
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_rejects_json_missing_current_or_daily);
}
