/*
 * Setup Web Form Unit Tests
 *
 * Tests for setup_web_form.h/.cpp - pure HTML generation and field
 * validation logic, no Arduino/ESP8266WebServer dependency (see
 * setup_mode.h for the Arduino-only glue this doesn't cover).
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "setup_web_form.h"
#include "eeprom_config.h"

void setUp(void) {}
void tearDown(void) {}

static EepromConfig makeTestConfig(void) {
    EepromConfig config;
    eepromConfigLoadDefaults(config);
    strncpy(config.wifi_ssid, "TestSSID", sizeof(config.wifi_ssid) - 1);
    strncpy(config.wifi_password, "TestPass123", sizeof(config.wifi_password) - 1);
    strncpy(config.ecos_ip, "192.168.0.50", sizeof(config.ecos_ip) - 1);
    config.xnet_bus_timeout_ms = 120000;
    config.loco_inactivity_timeout_ms = 300000;
    strncpy(config.bridge_ip, "192.168.0.200", sizeof(config.bridge_ip) - 1);
    strncpy(config.bridge_gateway, "192.168.0.1", sizeof(config.bridge_gateway) - 1);
    strncpy(config.bridge_subnet, "255.255.255.0", sizeof(config.bridge_subnet) - 1);
    return config;
}

// ============================================================================
// IPV4 FORMAT VALIDATION
// ============================================================================

void test_ipv4_accepts_normal_address(void) {
    TEST_ASSERT_TRUE(isValidIpv4Format("192.168.0.50"));
}

void test_ipv4_accepts_all_zero(void) {
    TEST_ASSERT_TRUE(isValidIpv4Format("0.0.0.0"));
}

void test_ipv4_accepts_all_max(void) {
    TEST_ASSERT_TRUE(isValidIpv4Format("255.255.255.255"));
}

void test_ipv4_rejects_empty(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format(""));
}

void test_ipv4_rejects_null(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format(nullptr));
}

void test_ipv4_rejects_too_few_octets(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("1.2.3"));
}

void test_ipv4_rejects_too_many_octets(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("1.2.3.4.5"));
}

void test_ipv4_rejects_octet_over_255(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("256.1.1.1"));
}

void test_ipv4_rejects_consecutive_dots(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("1..1.1"));
}

void test_ipv4_rejects_trailing_dot(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("1.2.3."));
}

void test_ipv4_rejects_leading_dot(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format(".1.2.3"));
}

void test_ipv4_rejects_non_numeric(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("abc.def.ghi.jkl"));
}

void test_ipv4_rejects_trailing_garbage(void) {
    TEST_ASSERT_FALSE(isValidIpv4Format("1.2.3.4a"));
}

// ============================================================================
// FIELD VALIDATION / APPLICATION
// ============================================================================

void test_field_wifi_ssid_accepts_valid(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "wifi_ssid", "MyHomeWifi"));
    TEST_ASSERT_EQUAL_STRING("MyHomeWifi", config.wifi_ssid);
}

void test_field_wifi_ssid_rejects_empty(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "wifi_ssid", ""));
    TEST_ASSERT_EQUAL_STRING("TestSSID", config.wifi_ssid);  // unchanged
}

void test_field_wifi_ssid_rejects_overlong(void) {
    EepromConfig config = makeTestConfig();
    const char* too_long = "123456789012345678901234567890123";  // 33 chars, buffer holds 32+null
    TEST_ASSERT_FALSE(validateAndApplyField(config, "wifi_ssid", too_long));
}

void test_field_wifi_password_accepts_empty(void) {
    // Open networks are legitimate - empty password isn't an error.
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "wifi_password", ""));
    TEST_ASSERT_EQUAL_STRING("", config.wifi_password);
}

void test_field_ecos_ip_accepts_valid(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "ecos_ip", "10.0.0.5"));
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", config.ecos_ip);
}

void test_field_ecos_ip_rejects_invalid(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "ecos_ip", "not-an-ip"));
    TEST_ASSERT_EQUAL_STRING("192.168.0.50", config.ecos_ip);  // unchanged
}

void test_field_xnet_bus_timeout_converts_seconds_to_ms(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "xnet_bus_timeout_s", "60"));
    TEST_ASSERT_EQUAL_UINT32(60000, config.xnet_bus_timeout_ms);
}

void test_field_xnet_bus_timeout_rejects_zero(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "xnet_bus_timeout_s", "0"));
}

void test_field_xnet_bus_timeout_rejects_too_large(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "xnet_bus_timeout_s", "999999"));
}

void test_field_xnet_bus_timeout_rejects_non_numeric(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "xnet_bus_timeout_s", "abc"));
}

void test_field_loco_inactivity_timeout_converts_seconds_to_ms(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "loco_inactivity_timeout_s", "180"));
    TEST_ASSERT_EQUAL_UINT32(180000, config.loco_inactivity_timeout_ms);
}

void test_field_bridge_ip_accepts_valid(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "bridge_ip", "192.168.1.100"));
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", config.bridge_ip);
}

void test_field_bridge_ip_rejects_empty(void) {
    // Mandatory since the design revision - no DHCP fallback via an
    // empty/optional field anymore.
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "bridge_ip", ""));
}

void test_field_bridge_gateway_rejects_empty(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "bridge_gateway", ""));
}

void test_field_bridge_subnet_rejects_empty(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_FALSE(validateAndApplyField(config, "bridge_subnet", ""));
}

void test_field_unknown_name_is_ignored(void) {
    EepromConfig config = makeTestConfig();
    TEST_ASSERT_TRUE(validateAndApplyField(config, "not_a_real_field", "whatever"));
}

// ============================================================================
// HTML GENERATION
// ============================================================================

void test_html_contains_current_wifi_ssid(void) {
    EepromConfig config = makeTestConfig();
    char buffer[4096];
    size_t len = buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "TestSSID"));
}

void test_html_contains_current_ecos_ip(void) {
    EepromConfig config = makeTestConfig();
    char buffer[4096];
    buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "192.168.0.50"));
}

void test_html_contains_timeout_in_seconds_not_ms(void) {
    EepromConfig config = makeTestConfig();  // 120000ms bus timeout -> "120"
    char buffer[4096];
    buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "value=\"120\""));
}

void test_html_contains_bridge_ip_fields(void) {
    EepromConfig config = makeTestConfig();
    char buffer[4096];
    buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "192.168.0.200"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "192.168.0.1"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "255.255.255.0"));
}

void test_html_rejects_undersized_buffer(void) {
    EepromConfig config = makeTestConfig();
    char buffer[16];  // far too small for the real page
    size_t len = buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_EQUAL(0, len);
}

void test_saved_confirmation_html_non_empty(void) {
    char buffer[512];
    size_t len = buildSavedConfirmationHtml(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Saved"));
}

void test_saved_confirmation_html_rejects_undersized_buffer(void) {
    char buffer[8];
    size_t len = buildSavedConfirmationHtml(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, len);
}

void test_html_suggests_default_subnet_when_blank(void) {
    // Real gap found live 2026-08-27: a first-boot config has no
    // compile-time default for the bridge's subnet, so the page used to
    // show it truly blank - now suggests the standard home-network mask.
    EepromConfig config = makeTestConfig();
    config.bridge_subnet[0] = '\0';
    char buffer[4096];
    buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "value=\"255.255.255.0\""));
}

void test_html_keeps_saved_subnet_when_not_blank(void) {
    // The suggestion must not override a real, already-saved value.
    EepromConfig config = makeTestConfig();  // bridge_subnet = "255.255.255.0" already
    strncpy(config.bridge_subnet, "255.255.0.0", sizeof(config.bridge_subnet) - 1);
    char buffer[4096];
    buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "value=\"255.255.0.0\""));
}

void test_config_page_links_to_update_page(void) {
    EepromConfig config = makeTestConfig();
    char buffer[4096];
    buildConfigPageHtml(buffer, sizeof(buffer), config);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "href=\"/update\""));
}

// ============================================================================
// UPDATE PAGE (Phase 6 step 3)
// ============================================================================

void test_update_page_shows_current_version(void) {
    char buffer[4096];
    size_t len = buildUpdatePageHtml(buffer, sizeof(buffer), "1.0.0", "Aug 27 2026 14:32:10");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "1.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Aug 27 2026 14:32:10"));
}

void test_update_page_posts_to_doupdate(void) {
    char buffer[4096];
    buildUpdatePageHtml(buffer, sizeof(buffer), "1.0.0", "test-build");
    TEST_ASSERT_NOT_NULL(strstr(buffer, "action=\"/doupdate\""));
}

void test_update_page_links_back_to_settings(void) {
    char buffer[4096];
    buildUpdatePageHtml(buffer, sizeof(buffer), "1.0.0", "test-build");
    TEST_ASSERT_NOT_NULL(strstr(buffer, "href=\"/\""));
}

void test_update_page_rejects_undersized_buffer(void) {
    char buffer[16];
    size_t len = buildUpdatePageHtml(buffer, sizeof(buffer), "1.0.0", "test-build");
    TEST_ASSERT_EQUAL(0, len);
}

void test_update_page_rejects_null_version(void) {
    char buffer[4096];
    size_t len = buildUpdatePageHtml(buffer, sizeof(buffer), nullptr, "test-build");
    TEST_ASSERT_EQUAL(0, len);
}

void test_update_page_rejects_null_build_info(void) {
    char buffer[4096];
    size_t len = buildUpdatePageHtml(buffer, sizeof(buffer), "1.0.0", nullptr);
    TEST_ASSERT_EQUAL(0, len);
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ipv4_accepts_normal_address);
    RUN_TEST(test_ipv4_accepts_all_zero);
    RUN_TEST(test_ipv4_accepts_all_max);
    RUN_TEST(test_ipv4_rejects_empty);
    RUN_TEST(test_ipv4_rejects_null);
    RUN_TEST(test_ipv4_rejects_too_few_octets);
    RUN_TEST(test_ipv4_rejects_too_many_octets);
    RUN_TEST(test_ipv4_rejects_octet_over_255);
    RUN_TEST(test_ipv4_rejects_consecutive_dots);
    RUN_TEST(test_ipv4_rejects_trailing_dot);
    RUN_TEST(test_ipv4_rejects_leading_dot);
    RUN_TEST(test_ipv4_rejects_non_numeric);
    RUN_TEST(test_ipv4_rejects_trailing_garbage);

    RUN_TEST(test_field_wifi_ssid_accepts_valid);
    RUN_TEST(test_field_wifi_ssid_rejects_empty);
    RUN_TEST(test_field_wifi_ssid_rejects_overlong);
    RUN_TEST(test_field_wifi_password_accepts_empty);
    RUN_TEST(test_field_ecos_ip_accepts_valid);
    RUN_TEST(test_field_ecos_ip_rejects_invalid);
    RUN_TEST(test_field_xnet_bus_timeout_converts_seconds_to_ms);
    RUN_TEST(test_field_xnet_bus_timeout_rejects_zero);
    RUN_TEST(test_field_xnet_bus_timeout_rejects_too_large);
    RUN_TEST(test_field_xnet_bus_timeout_rejects_non_numeric);
    RUN_TEST(test_field_loco_inactivity_timeout_converts_seconds_to_ms);
    RUN_TEST(test_field_bridge_ip_accepts_valid);
    RUN_TEST(test_field_bridge_ip_rejects_empty);
    RUN_TEST(test_field_bridge_gateway_rejects_empty);
    RUN_TEST(test_field_bridge_subnet_rejects_empty);
    RUN_TEST(test_field_unknown_name_is_ignored);

    RUN_TEST(test_html_contains_current_wifi_ssid);
    RUN_TEST(test_html_contains_current_ecos_ip);
    RUN_TEST(test_html_contains_timeout_in_seconds_not_ms);
    RUN_TEST(test_html_contains_bridge_ip_fields);
    RUN_TEST(test_html_rejects_undersized_buffer);
    RUN_TEST(test_saved_confirmation_html_non_empty);
    RUN_TEST(test_saved_confirmation_html_rejects_undersized_buffer);
    RUN_TEST(test_html_suggests_default_subnet_when_blank);
    RUN_TEST(test_html_keeps_saved_subnet_when_not_blank);
    RUN_TEST(test_config_page_links_to_update_page);

    RUN_TEST(test_update_page_shows_current_version);
    RUN_TEST(test_update_page_posts_to_doupdate);
    RUN_TEST(test_update_page_links_back_to_settings);
    RUN_TEST(test_update_page_rejects_undersized_buffer);
    RUN_TEST(test_update_page_rejects_null_version);
    RUN_TEST(test_update_page_rejects_null_build_info);

    return UNITY_END();
}
