/*
 * Setup Mode - AP + Web Config Server, Button/Fallback Triggers
 *
 * Phase 6 step 2. Arduino-only glue (WiFi AP, ESP8266WebServer, RTC user
 * memory, GPIO) - the actual HTML/validation logic lives in
 * setup_web_form.h/.cpp instead, which is native-testable. Excluded from
 * env:native like ecos_interface.cpp/eeprom_store.cpp.
 */

#ifndef SETUP_MODE_H
#define SETUP_MODE_H

#include "eeprom_config.h"

/**
 * Request that the NEXT boot enter Setup Mode instead of normal operation.
 * Backed by ESP8266 RTC user memory - survives a soft ESP.restart() but
 * clears on power loss, giving exactly the "next boot only" semantics
 * needed here (as opposed to EEPROM, which would need an explicit clear
 * and costs a real flash write cycle for what's a purely transient signal).
 * Does not itself reboot - call ESP.restart() after.
 */
void requestSetupModeOnNextBoot();

/**
 * Check whether the previous boot requested Setup Mode, and clear the
 * request if so (one-shot - a subsequent normal reboot won't loop back
 * into Setup Mode). Call once, early in setup().
 * @return true if Setup Mode was requested
 */
bool consumeSetupModeRequest();

/**
 * Start Setup Mode: switches to AP mode (config.h's SETUP_MODE_AP_SSID),
 * starts the web server on SETUP_MODE_WEB_PORT. Call once from setup()
 * instead of the normal protocol interfaces' begin() calls - Setup Mode
 * and normal bridging operation are mutually exclusive.
 * @param config Loaded EEPROM config to show/edit - the same instance the
 *        caller keeps using afterward (setup mode writes into it directly
 *        on save, then reboots).
 */
void setupModeBegin(EepromConfig& config);

/**
 * Non-blocking - call every loop() iteration while in Setup Mode. Pumps
 * the web server; a valid /save submission writes to EEPROM and reboots
 * into normal operation on its own (this function does not return in
 * that case).
 */
void setupModeUpdate();

/**
 * The Setup Mode AP's IP as a string (e.g. "192.168.4.1"), captured by
 * setupModeBegin(). Only valid after that's been called - lets the
 * caller (the .ino, for the OLED message) get this without needing to
 * touch ESP8266WiFi types directly.
 */
const char* setupModeGetApIpString();

/**
 * Poll the dedicated setup button (SETUP_BUTTON_PIN, see config.h) during
 * NORMAL operation (not while already in Setup Mode) - call every loop()
 * iteration. Non-blocking; tracks hold duration via millis() across
 * calls. Once held for SETUP_BUTTON_HOLD_MS, requests Setup Mode and
 * reboots.
 */
void checkSetupButton();

/**
 * Track how long WiFi has been continuously disconnected during NORMAL
 * operation - call every loop() iteration. Once WIFI_FALLBACK_TIMEOUT_MS
 * of continuous disconnection has elapsed, requests Setup Mode and
 * reboots - defense-in-depth recovery for a bad WiFi credential change
 * (or a router password change) that doesn't require physically pressing
 * the button.
 */
void checkWifiFallback();

#endif  // SETUP_MODE_H
