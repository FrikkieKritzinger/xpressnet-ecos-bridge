/*
 * Setup Web Form Implementation - Pure HTML Generation and Validation
 */

#include "setup_web_form.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ============================================================================
// IPV4 FORMAT VALIDATION
// ============================================================================

bool isValidIpv4Format(const char* ip) {
    if (!ip) return false;

    int octet_count = 0;
    int digit_count = 0;
    long value = 0;

    for (const char* p = ip; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digit_count++;
            if (digit_count > 3 || value > 255) return false;
        } else if (c == '.' || c == '\0') {
            if (digit_count == 0) return false;  // empty octet, e.g. leading/consecutive dot
            octet_count++;
            value = 0;
            digit_count = 0;
            if (c == '\0') break;
        } else {
            return false;  // invalid character
        }
    }
    return octet_count == 4;
}

// ============================================================================
// FIELD VALIDATION / APPLICATION
// ============================================================================

// Timeout fields are shown/submitted in seconds; reject anything outside a
// sane 1s-1hr range (covers the real defaults - 120s bus timeout, 300s loco
// inactivity - with generous headroom for retuning).
static const long MIN_TIMEOUT_S = 1;
static const long MAX_TIMEOUT_S = 3600;

static bool parseTimeoutSeconds(const char* value, uint32_t* out_ms) {
    if (!value || value[0] == '\0') return false;
    char* endptr;
    long seconds = strtol(value, &endptr, 10);
    if (endptr == value || *endptr != '\0') return false;  // not a clean integer
    if (seconds < MIN_TIMEOUT_S || seconds > MAX_TIMEOUT_S) return false;
    *out_ms = (uint32_t)(seconds * 1000);
    return true;
}

bool validateAndApplyField(EepromConfig& config, const char* field_name, const char* field_value) {
    if (!field_name || !field_value) return false;

    if (strcmp(field_name, "wifi_ssid") == 0) {
        size_t len = strlen(field_value);
        if (len == 0 || len >= sizeof(config.wifi_ssid)) return false;
        strncpy(config.wifi_ssid, field_value, sizeof(config.wifi_ssid) - 1);
        config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
        return true;
    }

    if (strcmp(field_name, "wifi_password") == 0) {
        size_t len = strlen(field_value);
        if (len >= sizeof(config.wifi_password)) return false;  // empty allowed (open network)
        strncpy(config.wifi_password, field_value, sizeof(config.wifi_password) - 1);
        config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
        return true;
    }

    if (strcmp(field_name, "ecos_ip") == 0) {
        if (!isValidIpv4Format(field_value)) return false;
        strncpy(config.ecos_ip, field_value, sizeof(config.ecos_ip) - 1);
        config.ecos_ip[sizeof(config.ecos_ip) - 1] = '\0';
        return true;
    }

    if (strcmp(field_name, "xnet_bus_timeout_s") == 0) {
        return parseTimeoutSeconds(field_value, &config.xnet_bus_timeout_ms);
    }

    if (strcmp(field_name, "loco_inactivity_timeout_s") == 0) {
        return parseTimeoutSeconds(field_value, &config.loco_inactivity_timeout_ms);
    }

    if (strcmp(field_name, "bridge_ip") == 0) {
        if (!isValidIpv4Format(field_value)) return false;  // required, empty rejected too
        strncpy(config.bridge_ip, field_value, sizeof(config.bridge_ip) - 1);
        config.bridge_ip[sizeof(config.bridge_ip) - 1] = '\0';
        return true;
    }

    if (strcmp(field_name, "bridge_gateway") == 0) {
        if (!isValidIpv4Format(field_value)) return false;
        strncpy(config.bridge_gateway, field_value, sizeof(config.bridge_gateway) - 1);
        config.bridge_gateway[sizeof(config.bridge_gateway) - 1] = '\0';
        return true;
    }

    if (strcmp(field_name, "bridge_subnet") == 0) {
        if (!isValidIpv4Format(field_value)) return false;
        strncpy(config.bridge_subnet, field_value, sizeof(config.bridge_subnet) - 1);
        config.bridge_subnet[sizeof(config.bridge_subnet) - 1] = '\0';
        return true;
    }

    // Unknown field name - not an error, just nothing to apply.
    return true;
}

// ============================================================================
// HTML GENERATION
// ============================================================================

// 255.255.255.0 (/24) is the standard mask for a typical home network -
// shown as a pre-filled suggestion when bridge_subnet is still blank
// (first boot), not silently written to EEPROM unless the user actually
// submits the form with it left as-is.
static const char* const kDefaultSubnetSuggestion = "255.255.255.0";

size_t buildConfigPageHtml(char* buffer, size_t buffer_size, const EepromConfig& config) {
    if (!buffer || buffer_size == 0) return 0;

    const char* subnet_display =
        (config.bridge_subnet[0] != '\0') ? config.bridge_subnet : kDefaultSubnetSuggestion;

    int len = snprintf(buffer, buffer_size,
        "<!DOCTYPE html><html><head><title>XNet-Ecos Bridge Setup</title>"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>"
        "body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 12px}"
        "h1{font-size:1.3em}fieldset{margin-bottom:16px}"
        "label{display:block;margin-top:8px}"
        "input[type=text],input[type=password],input[type=number]{width:100%%;box-sizing:border-box;padding:6px}"
        "input[type=submit]{padding:10px 20px;font-size:1em}"
        "</style></head><body>"
        "<h1>XpressNet-Ecos Bridge - Setup</h1>"
        "<form method=\"POST\" action=\"/save\">"
        "<fieldset><legend>WiFi</legend>"
        "<label>SSID<input type=\"text\" name=\"wifi_ssid\" value=\"%s\" maxlength=\"32\" required></label>"
        "<label>Password<input type=\"password\" name=\"wifi_password\" value=\"%s\" maxlength=\"64\"></label>"
        "</fieldset>"
        "<fieldset><legend>Ecos</legend>"
        "<label>Ecos IP<input type=\"text\" name=\"ecos_ip\" value=\"%s\" required></label>"
        "</fieldset>"
        "<fieldset><legend>Timeouts (seconds)</legend>"
        "<label>XNet bus timeout<input type=\"number\" name=\"xnet_bus_timeout_s\" value=\"%lu\" min=\"1\" max=\"3600\" required></label>"
        "<label>Loco inactivity timeout<input type=\"number\" name=\"loco_inactivity_timeout_s\" value=\"%lu\" min=\"1\" max=\"3600\" required></label>"
        "</fieldset>"
        "<fieldset><legend>Bridge Static IP (required - no DHCP)</legend>"
        "<label>IP<input type=\"text\" name=\"bridge_ip\" value=\"%s\" required></label>"
        "<label>Gateway<input type=\"text\" name=\"bridge_gateway\" value=\"%s\" required></label>"
        "<label>Subnet<input type=\"text\" name=\"bridge_subnet\" value=\"%s\" required></label>"
        "</fieldset>"
        "<input type=\"submit\" value=\"Save and Reboot\">"
        "</form>"
        "<p><a href=\"/update\">Firmware Update</a></p>"
        "</body></html>",
        config.wifi_ssid,
        config.wifi_password,
        config.ecos_ip,
        (unsigned long)(config.xnet_bus_timeout_ms / 1000),
        (unsigned long)(config.loco_inactivity_timeout_ms / 1000),
        config.bridge_ip,
        config.bridge_gateway,
        subnet_display);

    if (len < 0 || (size_t)len >= buffer_size) return 0;
    return (size_t)len;
}

size_t buildSavedConfirmationHtml(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return 0;

    int len = snprintf(buffer, buffer_size,
        "<!DOCTYPE html><html><head><title>Saved</title>"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head><body>"
        "<h1>Settings saved</h1>"
        "<p>Rebooting into normal operation...</p>"
        "</body></html>");

    if (len < 0 || (size_t)len >= buffer_size) return 0;
    return (size_t)len;
}

size_t buildUpdatePageHtml(char* buffer, size_t buffer_size, const char* current_version,
                           const char* current_build_info) {
    if (!buffer || buffer_size == 0 || !current_version || !current_build_info) return 0;

    int len = snprintf(buffer, buffer_size,
        "<!DOCTYPE html><html><head><title>Firmware Update</title>"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>"
        "body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 12px}"
        "h1{font-size:1.3em}"
        "input[type=submit]{padding:10px 20px;font-size:1em}"
        "</style></head><body>"
        "<h1>XpressNet-Ecos Bridge - Firmware Update</h1>"
        "<p>Current version: %s<br>Build: %s</p>"
        "<form method=\"POST\" action=\"/doupdate\" enctype=\"multipart/form-data\">"
        "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
        "<input type=\"submit\" value=\"Upload and Flash\">"
        "</form>"
        "<p><a href=\"/\">Back to Settings</a></p>"
        "</body></html>",
        current_version, current_build_info);

    if (len < 0 || (size_t)len >= buffer_size) return 0;
    return (size_t)len;
}
