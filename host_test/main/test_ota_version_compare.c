/* Host-Unit-Tests fuer components/app_logic/ota_version_compare. */

#include "unity.h"
#include "ota_version_compare.h"

static void test_newer_patch(void) {
    TEST_ASSERT_TRUE(ota_is_newer("0.0.1", "0.0.2"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.2", "0.0.1"));
}

static void test_newer_minor_major(void) {
    TEST_ASSERT_TRUE(ota_is_newer("1.2.9", "1.3.0"));
    TEST_ASSERT_TRUE(ota_is_newer("1.9.9", "2.0.0"));
    TEST_ASSERT_FALSE(ota_is_newer("2.0.0", "1.9.9"));
}

static void test_equal_is_not_newer(void) {
    TEST_ASSERT_FALSE(ota_is_newer("1.2.3", "1.2.3"));
    TEST_ASSERT_FALSE(ota_is_newer("v1.2.3", "1.2.3"));
}

static void test_leading_v_ignored(void) {
    TEST_ASSERT_TRUE(ota_is_newer("v0.0.1", "v0.0.2"));
    TEST_ASSERT_TRUE(ota_is_newer("0.0.1", "V0.0.2"));
}

static void test_prerelease_precedence(void) {
    /* A plain release outranks the same version with a pre-release suffix. */
    TEST_ASSERT_TRUE(ota_is_newer("0.0.1-pre1", "0.0.1"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.1", "0.0.1-pre1"));
    /* Two pre-releases of the same version: string compare of the suffix. */
    TEST_ASSERT_TRUE(ota_is_newer("0.0.1-pre1", "0.0.1-pre2"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.1-pre2", "0.0.1-pre1"));
}

static void test_missing_components_default_to_zero(void) {
    TEST_ASSERT_TRUE(ota_is_newer("1", "1.0.1"));
    TEST_ASSERT_FALSE(ota_is_newer("1.0.1", "1"));
}

static void test_unparseable_input_fails_closed(void) {
    TEST_ASSERT_FALSE(ota_is_newer("garbage", "0.0.1"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.1", "garbage"));
    TEST_ASSERT_FALSE(ota_is_newer(NULL, "0.0.1"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.1", NULL));
}

void test_ota_version_compare_run(void) {
    RUN_TEST(test_newer_patch);
    RUN_TEST(test_newer_minor_major);
    RUN_TEST(test_equal_is_not_newer);
    RUN_TEST(test_leading_v_ignored);
    RUN_TEST(test_prerelease_precedence);
    RUN_TEST(test_missing_components_default_to_zero);
    RUN_TEST(test_unparseable_input_fails_closed);
}
