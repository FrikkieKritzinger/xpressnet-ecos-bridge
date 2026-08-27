/*
 * Setup Web Form - Pure HTML Generation and Field Validation
 *
 * Phase 6 step 2. Builds the Setup Mode config page and validates/applies
 * submitted field values - no Arduino/ESP8266WebServer dependency, so this
 * is fully native-testable. The actual HTTP glue (reading server.arg(),
 * sending the response) lives in setup_mode.cpp instead, which is
 * Arduino-only like ecos_interface.cpp/eeprom_store.cpp.
 *
 * Timeout fields are shown/submitted in whole seconds (friendlier than raw
 * milliseconds) - buildConfigPageHtml()/validateAndApplyField() both do the
 * ms<->s conversion internally; EepromConfig itself always stores ms.
 */

#ifndef SETUP_WEB_FORM_H
#define SETUP_WEB_FORM_H

#include <cstddef>
#include "eeprom_config.h"

/**
 * Render the full Setup Mode config page (a self-contained HTML document,
 * no external CSS/JS) into `buffer`, pre-filled with `config`'s current
 * values.
 * @return bytes written (excluding null terminator), or 0 if buffer_size
 *         was too small to fit the page.
 */
size_t buildConfigPageHtml(char* buffer, size_t buffer_size, const EepromConfig& config);

/**
 * Render a minimal confirmation page shown after a successful save, before
 * the reboot.
 * @return bytes written (excluding null terminator), or 0 if buffer_size
 *         was too small.
 */
size_t buildSavedConfirmationHtml(char* buffer, size_t buffer_size);

/**
 * Validate a single submitted field value and, if valid, apply it into
 * `config`. Unknown field names are ignored (return true - not an error,
 * just nothing to do). Known-but-invalid values (e.g. a malformed IP, an
 * out-of-range timeout, an empty SSID) are rejected without modifying
 * `config`.
 * @param field_name One of: wifi_ssid, wifi_password, ecos_ip,
 *        xnet_bus_timeout_s, loco_inactivity_timeout_s,
 *        bridge_ip, bridge_gateway, bridge_subnet
 * @param field_value The raw submitted value (already URL-decoded by the
 *        caller - ESP8266WebServer's server.arg() does this itself)
 * @return true if the field was recognized and valid (or unknown/ignored),
 *         false if the value was rejected
 */
bool validateAndApplyField(EepromConfig& config, const char* field_name, const char* field_value);

/**
 * True if `ip` looks like a well-formed dotted-quad IPv4 address (four
 * numeric octets 0-255, separated by dots, nothing else). Doesn't check
 * reachability/reserved ranges, just syntax.
 */
bool isValidIpv4Format(const char* ip);

/**
 * Render the OTA firmware update page (Phase 6 step 3) - shows the
 * current version/build and a single firmware-upload form. Posts to
 * "/doupdate" (ESP8266HTTPUpdateServer's registered path, not this
 * page's own URL) - the actual upload/flash handling is that official
 * library's, not hand-rolled here; this is just a styled entry page
 * matching the rest of Setup Mode's look, without the "FileSystem"
 * upload option the library's own default page also offers (this
 * project has no filesystem/SPIFFS use to update).
 * @param current_version e.g. FIRMWARE_VERSION from version.h - the
 *        primary, user-facing identifier
 * @param current_build_info e.g. FIRMWARE_BUILD_INFO from config.h - a
 *        secondary, more precise compile date/time, useful for
 *        disambiguating builds sharing the same not-yet-bumped version
 * @return bytes written (excluding null terminator), or 0 if buffer_size
 *         was too small to fit the page.
 */
size_t buildUpdatePageHtml(char* buffer, size_t buffer_size, const char* current_version,
                           const char* current_build_info);

#endif  // SETUP_WEB_FORM_H
