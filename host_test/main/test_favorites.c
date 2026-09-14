/*
 * Host-Unit-Tests fuer components/app_logic/favorites: Zuordnung per
 * Lat/Lon, erster freier Slot, No-Op wenn alle sechs Slots belegt sind.
 */

#include "unity.h"
#include "favorites.h"
#include <string.h>

static void fresh(app_favorite_t favs[APP_FAVORITES_MAX]) {
    memset(favs, 0, sizeof(app_favorite_t) * APP_FAVORITES_MAX);
}

static void test_toggle_fuellt_ersten_freien_slot(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    TEST_ASSERT_TRUE(app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f));
    TEST_ASSERT_TRUE(favs[0].used);
    TEST_ASSERT_EQUAL_STRING("Berlin", favs[0].name);
    TEST_ASSERT_EQUAL_STRING("Germany", favs[0].country);
    TEST_ASSERT_EQUAL_FLOAT(52.52f, favs[0].lat);
    TEST_ASSERT_EQUAL_FLOAT(13.405f, favs[0].lon);
}

static void test_toggle_entfernt_bereits_favorisierte_stadt(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f);
    TEST_ASSERT_TRUE(app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f));
    TEST_ASSERT_FALSE(favs[0].used);
}

static void test_toggle_erkennt_slot_unabhaengig_von_slot_position(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f);
    app_favorites_toggle(favs, "Paris", "France", 48.8566f, 2.3522f);
    /* Removing the second-added city must clear slot 1, not slot 0. */
    TEST_ASSERT_TRUE(app_favorites_toggle(favs, "Paris", "France", 48.8566f, 2.3522f));
    TEST_ASSERT_TRUE(favs[0].used);
    TEST_ASSERT_FALSE(favs[1].used);
}

static void test_toggle_ist_noop_wenn_alle_slots_belegt(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    for (int i = 0; i < APP_FAVORITES_MAX; i++) {
        char name[8];
        snprintf(name, sizeof name, "c%d", i);
        TEST_ASSERT_TRUE(app_favorites_toggle(favs, name, "x", (float)i, 0.0f));
    }
    TEST_ASSERT_FALSE(app_favorites_toggle(favs, "overflow", "x", 99.0f, 99.0f));
    for (int i = 0; i < APP_FAVORITES_MAX; i++) TEST_ASSERT_TRUE(favs[i].used);
}

static void test_remove_leert_slot(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f);
    app_favorites_remove(favs, 0);
    TEST_ASSERT_FALSE(favs[0].used);
}

static void test_remove_mit_ungueltigem_index_ist_noop(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f);
    app_favorites_remove(favs, -1);
    app_favorites_remove(favs, APP_FAVORITES_MAX);
    TEST_ASSERT_TRUE(favs[0].used);
}

static void test_contains_erkennt_slot_per_koordinate(void) {
    app_favorite_t favs[APP_FAVORITES_MAX];
    fresh(favs);
    app_favorites_toggle(favs, "Berlin", "Germany", 52.52f, 13.405f);
    TEST_ASSERT_TRUE(app_favorites_contains(favs, 52.52f, 13.405f));
    TEST_ASSERT_FALSE(app_favorites_contains(favs, 48.8566f, 2.3522f));
}

void test_favorites_run(void) {
    RUN_TEST(test_toggle_fuellt_ersten_freien_slot);
    RUN_TEST(test_toggle_entfernt_bereits_favorisierte_stadt);
    RUN_TEST(test_toggle_erkennt_slot_unabhaengig_von_slot_position);
    RUN_TEST(test_toggle_ist_noop_wenn_alle_slots_belegt);
    RUN_TEST(test_remove_leert_slot);
    RUN_TEST(test_remove_mit_ungueltigem_index_ist_noop);
    RUN_TEST(test_contains_erkennt_slot_per_koordinate);
}
