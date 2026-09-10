/*
 * Host-Unit-Tests fuer components/app_logic/storage_record.
 * Deckt die beiden Eigenschaften ab, die main/app_prefs.c und main/app_wifi.c
 * jetzt voraussetzen: ein defektes oder falsch versioniertes Blob wird als
 * "nicht vorhanden" gemeldet (nie als falsch interpretierter Wert), und ein
 * unveraendertes Speichern loest keinen echten Schreibzugriff aus.
 */

#include "unity.h"
#include "storage_record.h"
#include "fake_storage_backend.h"
#include <string.h>

typedef struct __attribute__((packed)) {
    uint32_t a;
    uint16_t b;
} test_payload_t;

#define TEST_VERSION 1

static fake_storage_backend_t s_fake;
static storage_backend_t s_backend;

static void fresh_backend(void) {
    fake_storage_backend_reset(&s_fake);
    s_backend = fake_storage_backend_as_backend(&s_fake);
}

static void test_roundtrip(void) {
    fresh_backend();
    test_payload_t in = { .a = 0xDEADBEEF, .b = 42 };
    TEST_ASSERT_TRUE(storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in));

    test_payload_t out = {0};
    TEST_ASSERT_TRUE(storage_record_load(&s_backend, "ns", "key", TEST_VERSION, &out, sizeof out));
    TEST_ASSERT_EQUAL_UINT32(in.a, out.a);
    TEST_ASSERT_EQUAL_UINT16(in.b, out.b);
}

static void test_fehlender_datensatz_wird_gemeldet(void) {
    fresh_backend();
    test_payload_t out;
    TEST_ASSERT_FALSE(storage_record_load(&s_backend, "ns", "key", TEST_VERSION, &out, sizeof out));
}

static void test_falsche_version_wird_wie_fehlend_behandelt(void) {
    fresh_backend();
    test_payload_t in = { .a = 1, .b = 2 };
    storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in);

    test_payload_t out;
    TEST_ASSERT_FALSE(storage_record_load(&s_backend, "ns", "key", TEST_VERSION + 1, &out, sizeof out));
}

static void test_beschaedigtes_blob_wird_wie_fehlend_behandelt(void) {
    fresh_backend();
    test_payload_t in = { .a = 1, .b = 2 };
    storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in);

    /* Ein Bit im gespeicherten Blob kippen, direkt im Fake-Backend — simuliert
     * eine Flash-Bitfehler-artige Beschaedigung nach dem Schreiben. */
    s_fake.blob[s_fake.len - 1] ^= 0xFF;

    test_payload_t out;
    TEST_ASSERT_FALSE(storage_record_load(&s_backend, "ns", "key", TEST_VERSION, &out, sizeof out));
}

static void test_unveraendertes_speichern_schreibt_nicht_erneut(void) {
    fresh_backend();
    test_payload_t in = { .a = 7, .b = 8 };
    TEST_ASSERT_TRUE(storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in));
    TEST_ASSERT_EQUAL_INT(1, s_fake.write_calls);

    /* Zweiter Save mit identischem Inhalt — das ist die eigentliche
     * Verschleiss-Reduktion: kein zweiter Flash-Schreibzugriff. */
    TEST_ASSERT_TRUE(storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in));
    TEST_ASSERT_EQUAL_INT(1, s_fake.write_calls);
    TEST_ASSERT_EQUAL_INT(1, s_fake.commit_calls);
}

static void test_geaendertes_speichern_schreibt_erneut(void) {
    fresh_backend();
    test_payload_t in = { .a = 7, .b = 8 };
    storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in);

    in.b = 9;
    TEST_ASSERT_TRUE(storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in));
    TEST_ASSERT_EQUAL_INT(2, s_fake.write_calls);

    test_payload_t out;
    TEST_ASSERT_TRUE(storage_record_load(&s_backend, "ns", "key", TEST_VERSION, &out, sizeof out));
    TEST_ASSERT_EQUAL_UINT16(9, out.b);
}

static void test_backend_fehler_beim_schreiben_wird_gemeldet(void) {
    fresh_backend();
    s_fake.fail_write = true;
    test_payload_t in = { .a = 1, .b = 2 };
    TEST_ASSERT_FALSE(storage_record_save(&s_backend, "ns", "key", TEST_VERSION, &in, sizeof in));
}

/* Called from test_main.c's single main() — see the matching comment in
 * test_light_policy.c. */
void test_storage_record_run(void) {
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_fehlender_datensatz_wird_gemeldet);
    RUN_TEST(test_falsche_version_wird_wie_fehlend_behandelt);
    RUN_TEST(test_beschaedigtes_blob_wird_wie_fehlend_behandelt);
    RUN_TEST(test_unveraendertes_speichern_schreibt_nicht_erneut);
    RUN_TEST(test_geaendertes_speichern_schreibt_erneut);
    RUN_TEST(test_backend_fehler_beim_schreiben_wird_gemeldet);
}
