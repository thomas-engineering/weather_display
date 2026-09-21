/*
 * Host-Unit-Tests fuer components/app_logic/network_status_policy.
 *
 * Deckt den WLAN-Ausfall-Regressionsfall ab: ein manueller Refresh (Retry-
 * Button in der Fehlerleiste) schlaegt fehl und laesst den Fehler-Toast
 * stehen ("stays until tapped away"); ein spaeterer, still laufender
 * Refresh (periodischer Auto-Refresh, Standortwechsel, Reconnect) laedt die
 * Wetterdaten erfolgreich, ohne selbst einen Toast zu zeigen. Vor dem Fix in
 * weather_ui.c / dieser Policy blieb der alte Fehler-Toast dann fuer immer
 * stehen, obwohl die Daten laengst geladen waren.
 */

#include "unity.h"
#include "network_status_policy.h"

static void test_erfolgreicher_manueller_refresh_zeigt_erfolgs_toast(void) {
    network_status_policy_t p;
    network_status_policy_reset(&p);

    network_status_policy_on_fetch(&p, true, true);

    TEST_ASSERT_FALSE(p.error_active);
    TEST_ASSERT_EQUAL_INT(NSP_TOAST_SUCCESS, p.toast);
}

static void test_fehlgeschlagener_manueller_refresh_zeigt_fehler_toast(void) {
    network_status_policy_t p;
    network_status_policy_reset(&p);

    network_status_policy_on_fetch(&p, false, true);

    TEST_ASSERT_TRUE(p.error_active);
    TEST_ASSERT_EQUAL_INT(NSP_TOAST_ERROR, p.toast);
}

static void test_stiller_fehlschlag_zeigt_keinen_toast(void) {
    network_status_policy_t p;
    network_status_policy_reset(&p);

    /* Periodischer Auto-Refresh schlaegt fehl: Fehlerleiste ja, Toast nein —
     * Claude Design zeigt den Toast nur beim manuellen Refresh. */
    network_status_policy_on_fetch(&p, false, false);

    TEST_ASSERT_TRUE(p.error_active);
    TEST_ASSERT_EQUAL_INT(NSP_TOAST_HIDDEN, p.toast);
}

static void test_wlan_ausfall_dann_stille_erholung_schliesst_alten_fehler_toast(void) {
    network_status_policy_t p;
    network_status_policy_reset(&p);

    /* Nutzer tippt Retry im Fehlerbanner, WLAN ist noch weg. */
    network_status_policy_on_fetch(&p, false, true);
    TEST_ASSERT_TRUE(p.error_active);
    TEST_ASSERT_EQUAL_INT(NSP_TOAST_ERROR, p.toast);

    /* WLAN kommt zurueck; der naechste Refresh laeuft still (Auto-Refresh /
     * Reconnect), nicht ueber den manuellen Pfad. */
    network_status_policy_on_fetch(&p, true, false);

    TEST_ASSERT_FALSE(p.error_active);
    /* Regression: der Fehler-Toast blieb hier vorher auf NSP_TOAST_ERROR
     * stehen, weil ihn ausser einem manuellen Refresh nie jemand anfasste. */
    TEST_ASSERT_EQUAL_INT(NSP_TOAST_HIDDEN, p.toast);
}

static void test_stiller_erfolg_ohne_vorherigen_fehler_laesst_toast_versteckt(void) {
    network_status_policy_t p;
    network_status_policy_reset(&p);

    /* Kein Fehler war je sichtbar — ein stiller Erfolg darf keinen Toast
     * aus dem Nichts aufreissen. */
    network_status_policy_on_fetch(&p, true, false);

    TEST_ASSERT_FALSE(p.error_active);
    TEST_ASSERT_EQUAL_INT(NSP_TOAST_HIDDEN, p.toast);
}

void test_network_status_policy_run(void) {
    RUN_TEST(test_erfolgreicher_manueller_refresh_zeigt_erfolgs_toast);
    RUN_TEST(test_fehlgeschlagener_manueller_refresh_zeigt_fehler_toast);
    RUN_TEST(test_stiller_fehlschlag_zeigt_keinen_toast);
    RUN_TEST(test_wlan_ausfall_dann_stille_erholung_schliesst_alten_fehler_toast);
    RUN_TEST(test_stiller_erfolg_ohne_vorherigen_fehler_laesst_toast_versteckt);
}
