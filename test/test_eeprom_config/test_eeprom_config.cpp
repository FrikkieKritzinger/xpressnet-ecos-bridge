/*
 * EEPROM Config Unit Tests
 *
 * Tests for eeprom_config.h/.cpp - the pure struct/defaults/validation
 * logic (no EEPROM.h dependency, see eeprom_store.h for the Arduino-only
 * hardware wrapper this deliberately excludes).
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "eeprom_config.h"
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// DEFAULTS SEEDING
// ============================================================================

void test_defaults_sets_valid_magic_and_version(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);

    TEST_ASSERT_EQUAL_UINT32(EEPROM_CONFIG_MAGIC, config.magic);
    TEST_ASSERT_EQUAL_UINT16(EEPROM_CONFIG_VERSION, config.version);
}

void test_defaults_matches_config_h_values(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);

    TEST_ASSERT_EQUAL_STRING(WIFI_SSID, config.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING(WIFI_PASSWORD, config.wifi_password);
    TEST_ASSERT_EQUAL_STRING(ECOS_IP, config.ecos_ip);
    TEST_ASSERT_EQUAL_UINT32(XPRESSNET_BUS_TIMEOUT, config.xnet_bus_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(LOCO_INACTIVITY_TIMEOUT, config.loco_inactivity_timeout_ms);
}

void test_defaults_bridge_ip_blank(void) {
    // No compile-time equivalent exists for the bridge's own static IP -
    // stays blank until Setup Mode is used to fill it in.
    EepromConfig config;
    eepromConfigLoadDefaults(config);

    TEST_ASSERT_EQUAL_STRING("", config.bridge_ip);
    TEST_ASSERT_EQUAL_STRING("", config.bridge_gateway);
    TEST_ASSERT_EQUAL_STRING("", config.bridge_subnet);
}

void test_defaults_config_is_valid_but_not_complete(void) {
    // Real gap found live 2026-08-27: a freshly-seeded config passes
    // eepromConfigIsValid() (correct checksum) while bridge_ip is still
    // blank - eepromConfigIsComplete() must catch that so Setup Mode
    // doesn't get skipped on a later boot.
    EepromConfig config;
    eepromConfigLoadDefaults(config);

    TEST_ASSERT_TRUE(eepromConfigIsValid(config));
    TEST_ASSERT_FALSE(eepromConfigIsComplete(config));
}

void test_complete_requires_bridge_ip(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    strncpy(config.bridge_gateway, "192.168.0.1", sizeof(config.bridge_gateway) - 1);
    strncpy(config.bridge_subnet, "255.255.255.0", sizeof(config.bridge_subnet) - 1);
    config.checksum = eepromConfigChecksum(config);

    TEST_ASSERT_FALSE(eepromConfigIsComplete(config));  // bridge_ip still blank
}

void test_complete_true_when_all_bridge_fields_set(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    strncpy(config.bridge_ip, "192.168.0.51", sizeof(config.bridge_ip) - 1);
    strncpy(config.bridge_gateway, "192.168.0.1", sizeof(config.bridge_gateway) - 1);
    strncpy(config.bridge_subnet, "255.255.255.0", sizeof(config.bridge_subnet) - 1);
    config.checksum = eepromConfigChecksum(config);

    TEST_ASSERT_TRUE(eepromConfigIsComplete(config));
}

void test_complete_false_for_invalid_config(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    strncpy(config.bridge_ip, "192.168.0.51", sizeof(config.bridge_ip) - 1);
    strncpy(config.bridge_gateway, "192.168.0.1", sizeof(config.bridge_gateway) - 1);
    strncpy(config.bridge_subnet, "255.255.255.0", sizeof(config.bridge_subnet) - 1);
    // Deliberately not recomputing the checksum - all bridge fields set,
    // but the struct itself is now corrupted/stale.

    TEST_ASSERT_FALSE(eepromConfigIsComplete(config));
}

void test_defaults_produces_valid_config(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);

    TEST_ASSERT_TRUE(eepromConfigIsValid(config));
}

// ============================================================================
// CHECKSUM / VALIDATION
// ============================================================================

void test_checksum_is_deterministic(void) {
    EepromConfig a, b;
    eepromConfigLoadDefaults(a);
    eepromConfigLoadDefaults(b);

    TEST_ASSERT_EQUAL_UINT16(eepromConfigChecksum(a), eepromConfigChecksum(b));
}

void test_checksum_changes_when_field_changes(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    uint16_t original_checksum = eepromConfigChecksum(config);

    strncpy(config.ecos_ip, "10.0.0.99", sizeof(config.ecos_ip) - 1);

    TEST_ASSERT_NOT_EQUAL(original_checksum, eepromConfigChecksum(config));
}

void test_invalid_rejects_wrong_magic(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    config.magic = 0xDEADBEEF;

    TEST_ASSERT_FALSE(eepromConfigIsValid(config));
}

void test_invalid_rejects_wrong_version(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    config.version = EEPROM_CONFIG_VERSION + 1;

    TEST_ASSERT_FALSE(eepromConfigIsValid(config));
}

void test_invalid_rejects_corrupted_field(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    // Simulate a torn/corrupted write: a field changed after the checksum
    // was computed, without recomputing it - exactly what a blank/partial
    // EEPROM read would produce.
    config.xnet_bus_timeout_ms = 99999;

    TEST_ASSERT_FALSE(eepromConfigIsValid(config));
}

void test_invalid_rejects_blank_erased_flash(void) {
    EepromConfig config;
    memset(&config, 0xFF, sizeof(config));

    TEST_ASSERT_FALSE(eepromConfigIsValid(config));
}

void test_invalid_rejects_all_zero(void) {
    EepromConfig config;
    memset(&config, 0x00, sizeof(config));

    TEST_ASSERT_FALSE(eepromConfigIsValid(config));
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_defaults_sets_valid_magic_and_version);
    RUN_TEST(test_defaults_matches_config_h_values);
    RUN_TEST(test_defaults_bridge_ip_blank);
    RUN_TEST(test_defaults_config_is_valid_but_not_complete);
    RUN_TEST(test_complete_requires_bridge_ip);
    RUN_TEST(test_complete_true_when_all_bridge_fields_set);
    RUN_TEST(test_complete_false_for_invalid_config);
    RUN_TEST(test_defaults_produces_valid_config);

    RUN_TEST(test_checksum_is_deterministic);
    RUN_TEST(test_checksum_changes_when_field_changes);

    RUN_TEST(test_invalid_rejects_wrong_magic);
    RUN_TEST(test_invalid_rejects_wrong_version);
    RUN_TEST(test_invalid_rejects_corrupted_field);
    RUN_TEST(test_invalid_rejects_blank_erased_flash);
    RUN_TEST(test_invalid_rejects_all_zero);

    return UNITY_END();
}
