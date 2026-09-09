/*
 * Host-Unit-Tests fuer components/app_logic/light_policy.
 * Laeuft nativ auf dem Linux-Target, ohne Chip und ohne Emulator.
 *
 * Deckt die Luma-zu-Helligkeit-Kennlinie ab, die main/app_light.c nur noch
 * aufruft: Erstsample, EMA-Einschwingen, Clamping an beiden Enden, die
 * Hysterese-Schwelle und das Zuruecksetzen beim Ausschalten.
 */

#include "unity.h"
#include "light_policy.h"

void setUp(void) {}
void tearDown(void) {}

static void test_erstes_sample_wird_immer_gemeldet(void) {
    light_policy_t p;
    light_policy_reset(&p);

    int pct = -1;
    bool reported = light_policy_sample(&p, 60, &pct);

    TEST_ASSERT_TRUE(reported);
    /* ema startet beim ersten Sample direkt auf dem Messwert, kein Einschwingen. */
    TEST_ASSERT_EQUAL_INT(LIGHT_POLICY_BRIGHTNESS_MIN +
                               (60 * (LIGHT_POLICY_BRIGHTNESS_MAX - LIGHT_POLICY_BRIGHTNESS_MIN)) /
                                   LIGHT_POLICY_LUMA_CEILING,
                           pct);
}

static void test_clamping_am_unteren_ende(void) {
    light_policy_t p;
    light_policy_reset(&p);

    int pct = -1;
    light_policy_sample(&p, 0, &pct);

    TEST_ASSERT_EQUAL_INT(LIGHT_POLICY_BRIGHTNESS_MIN, pct);
}

static void test_clamping_am_oberen_ende(void) {
    light_policy_t p;
    light_policy_reset(&p);

    /* 255 liegt weit ueber LIGHT_POLICY_LUMA_CEILING (130) — die Kennlinie muss
     * trotzdem bei BRIGHTNESS_MAX kappen, nicht ueberlaufen. */
    int pct = -1;
    light_policy_sample(&p, 255, &pct);

    TEST_ASSERT_EQUAL_INT(LIGHT_POLICY_BRIGHTNESS_MAX, pct);
}

static void test_ema_schwingt_ueber_mehrere_samples_ein(void) {
    light_policy_t p;
    light_policy_reset(&p);

    int pct;
    light_policy_sample(&p, 0, &pct);   /* ema=0 */
    light_policy_sample(&p, 120, &pct); /* ema=(0*3+120)/4=30 */
    light_policy_sample(&p, 120, &pct); /* ema=(30*3+120)/4=52 */

    TEST_ASSERT_EQUAL_INT(52, p.ema_luma);
}

static void test_kleine_aenderung_wird_unterdrueckt(void) {
    light_policy_t p;
    light_policy_reset(&p);

    int pct;
    light_policy_sample(&p, 60, &pct); /* meldet: erstes Sample */
    int first_pct = pct;

    /* Ein minimal anderer Messwert darf die Hintergrundbeleuchtung nicht
     * bei jedem Sample neu anfassen. */
    bool reported = light_policy_sample(&p, 61, &pct);

    TEST_ASSERT_FALSE(reported);
    TEST_ASSERT_EQUAL_INT(first_pct, p.last_reported_pct);
}

static void test_grosse_aenderung_wird_gemeldet(void) {
    light_policy_t p;
    light_policy_reset(&p);

    int pct;
    light_policy_sample(&p, 10, &pct);
    int first_pct = pct;

    bool reported = light_policy_sample(&p, 200, &pct);

    TEST_ASSERT_TRUE(reported);
    TEST_ASSERT_NOT_EQUAL(first_pct, pct);
}

static void test_reset_macht_naechstes_sample_wieder_unbedingt(void) {
    light_policy_t p;
    light_policy_reset(&p);

    int pct;
    light_policy_sample(&p, 60, &pct);
    light_policy_reset(&p);

    TEST_ASSERT_EQUAL_INT(-1, p.ema_luma);
    TEST_ASSERT_EQUAL_INT(-1, p.last_reported_pct);

    /* Nach dem Reset zaehlt auch ein Wert nahe am zuletzt gemeldeten wieder
     * als "erstes Sample" und wird gemeldet, nicht durch die Hysterese
     * unterdrueckt. */
    bool reported = light_policy_sample(&p, 61, &pct);
    TEST_ASSERT_TRUE(reported);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_erstes_sample_wird_immer_gemeldet);
    RUN_TEST(test_clamping_am_unteren_ende);
    RUN_TEST(test_clamping_am_oberen_ende);
    RUN_TEST(test_ema_schwingt_ueber_mehrere_samples_ein);
    RUN_TEST(test_kleine_aenderung_wird_unterdrueckt);
    RUN_TEST(test_grosse_aenderung_wird_gemeldet);
    RUN_TEST(test_reset_macht_naechstes_sample_wieder_unbedingt);
    return UNITY_END();
}
