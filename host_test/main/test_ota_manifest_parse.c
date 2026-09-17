/*
 * Host-Unit-Tests fuer components/app_logic/ota_manifest_parse.
 * Fixture ist die reale manifest.json-Form aus tmp/manifest.json (produziert
 * von .github/workflows/Manual_Dual-Chip_Release_Build.yml).
 */

#include "unity.h"
#include "ota_manifest_parse.h"

#include <string.h>

static const char *k_fixture =
    "{"
    "\"version\":\"v0.0.1\","
    "\"p4_sha256\":\"208a4f3e98fed39e4808bb2080e5506db629dc103b17921fb9d5204ae97aaad\","
    "\"c6_version\":\"latest\","
    "\"c6_sha256\":\"33bb86317c830a2ae6818565a4b8e33406316b53314f6bd09e24172a0e336e7\""
    "}";

static void test_parses_all_fields(void) {
    ota_manifest_t m;
    TEST_ASSERT_TRUE(ota_manifest_parse(k_fixture, &m));
    TEST_ASSERT_EQUAL_STRING("v0.0.1", m.version);
    TEST_ASSERT_EQUAL_STRING("208a4f3e98fed39e4808bb2080e5506db629dc103b17921fb9d5204ae97aaad", m.p4_sha256);
    TEST_ASSERT_EQUAL_STRING("latest", m.c6_version);
    TEST_ASSERT_EQUAL_STRING("33bb86317c830a2ae6818565a4b8e33406316b53314f6bd09e24172a0e336e7", m.c6_sha256);
}

static void test_rejects_malformed_json(void) {
    ota_manifest_t m;
    TEST_ASSERT_FALSE(ota_manifest_parse("not json", &m));
    TEST_ASSERT_FALSE(ota_manifest_parse(NULL, &m));
    TEST_ASSERT_FALSE(ota_manifest_parse("[]", &m));
}

static void test_rejects_missing_field(void) {
    ota_manifest_t m;
    TEST_ASSERT_FALSE(ota_manifest_parse("{\"p4_sha256\":\"a\",\"c6_version\":\"b\",\"c6_sha256\":\"c\"}", &m));
    TEST_ASSERT_FALSE(ota_manifest_parse("{\"version\":\"v1\",\"c6_version\":\"b\",\"c6_sha256\":\"c\"}", &m));
}

static void test_rejects_wrong_type(void) {
    ota_manifest_t m;
    TEST_ASSERT_FALSE(ota_manifest_parse(
        "{\"version\":1,\"p4_sha256\":\"a\",\"c6_version\":\"b\",\"c6_sha256\":\"c\"}", &m));
}

static void test_rejects_empty_string_field(void) {
    ota_manifest_t m;
    TEST_ASSERT_FALSE(ota_manifest_parse(
        "{\"version\":\"\",\"p4_sha256\":\"a\",\"c6_version\":\"b\",\"c6_sha256\":\"c\"}", &m));
}

void test_ota_manifest_parse_run(void) {
    RUN_TEST(test_parses_all_fields);
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_rejects_missing_field);
    RUN_TEST(test_rejects_wrong_type);
    RUN_TEST(test_rejects_empty_string_field);
}
