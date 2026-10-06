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

static void test_prerelease_numeric_identifiers(void) {
    TEST_ASSERT_TRUE(ota_is_newer("0.0.8-test.9", "0.0.8-test.10"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.8-test.10", "0.0.8-test.9"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.8-test.3", "0.0.8-test.3"));
    /* A test build of the next version is newer than the current release,
     * and the real release of that version outranks its own test builds. */
    TEST_ASSERT_TRUE(ota_is_newer("0.0.7", "0.0.8-test.1"));
    TEST_ASSERT_TRUE(ota_is_newer("0.0.8-test.12", "0.0.8"));
    TEST_ASSERT_FALSE(ota_is_newer("0.0.8", "0.0.8-test.12"));
    /* More identifiers win a tie; numeric ranks below alphanumeric. */
    TEST_ASSERT_TRUE(ota_is_newer("0.0.8-test", "0.0.8-test.1"));
    TEST_ASSERT_TRUE(ota_is_newer("0.0.8-1", "0.0.8-test"));
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
    RUN_TEST(test_prerelease_numeric_identifiers);
    RUN_TEST(test_newer_minor_major);
    RUN_TEST(test_equal_is_not_newer);
    RUN_TEST(test_leading_v_ignored);
    RUN_TEST(test_prerelease_precedence);
    RUN_TEST(test_missing_components_default_to_zero);
    RUN_TEST(test_unparseable_input_fails_closed);
}
