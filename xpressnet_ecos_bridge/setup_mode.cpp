/*
 * Setup Mode Implementation - AP + Web Server, RTC Flag, Button/Fallback
 */

#include "setup_mode.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Arduino.h>
#include <cstring>
#include "setup_web_form.h"
#include "eeprom_store.h"
#include "utils/debug.h"
#include "config.h"

// ============================================================================
// RTC-MEMORY "ENTER SETUP MODE NEXT BOOT" FLAG
// ============================================================================

#define RTC_SETUP_FLAG_MAGIC 0x53455455UL  // arbitrary sentinel ("SETU")
#define RTC_SETUP_FLAG_OFFSET 0            // word offset within RTC user memory

struct RtcSetupFlag {
    uint32_t magic;
    uint32_t flag;
};

void requestSetupModeOnNextBoot() {
    RtcSetupFlag rtc_flag;
    rtc_flag.magic = RTC_SETUP_FLAG_MAGIC;
    rtc_flag.flag = 1;
    ESP.rtcUserMemoryWrite(RTC_SETUP_FLAG_OFFSET, (uint32_t*)&rtc_flag, sizeof(rtc_flag));
}

bool consumeSetupModeRequest() {
    RtcSetupFlag rtc_flag;
    if (!ESP.rtcUserMemoryRead(RTC_SETUP_FLAG_OFFSET, (uint32_t*)&rtc_flag, sizeof(rtc_flag))) {
        return false;
    }
    if (rtc_flag.magic != RTC_SETUP_FLAG_MAGIC || rtc_flag.flag != 1) {
        return false;  // cold-boot garbage, not a genuine request
    }

    // One-shot: clear it so a later normal reboot doesn't loop back in.
    RtcSetupFlag cleared;
    cleared.magic = 0;
    cleared.flag = 0;
    ESP.rtcUserMemoryWrite(RTC_SETUP_FLAG_OFFSET, (uint32_t*)&cleared, sizeof(cleared));
    return true;
}

// ============================================================================
// WEB SERVER
// ============================================================================

static ESP8266WebServer g_setup_server(SETUP_MODE_WEB_PORT);
static ESP8266HTTPUpdateServer g_http_updater;
static EepromConfig* g_setup_config = nullptr;
static char g_html_buffer[4096];
static char g_ap_ip_string[16];

static void handleRoot() {
    size_t len = buildConfigPageHtml(g_html_buffer, sizeof(g_html_buffer), *g_setup_config);
    if (len > 0) {
        g_setup_server.send(200, "text/html", g_html_buffer);
    } else {
        g_setup_server.send(500, "text/plain", "Internal error building the config page.");
    }
}

static void handleUpdatePage() {
    size_t len = buildUpdatePageHtml(g_html_buffer, sizeof(g_html_buffer), FIRMWARE_VERSION, FIRMWARE_BUILD_INFO);
    if (len > 0) {
        g_setup_server.send(200, "text/html", g_html_buffer);
    } else {
        g_setup_server.send(500, "text/plain", "Internal error building the update page.");
    }
}

static void handleSave() {
    EepromConfig updated = *g_setup_config;

    static const char* kFields[] = {
        "wifi_ssid", "wifi_password", "ecos_ip",
        "xnet_bus_timeout_s", "loco_inactivity_timeout_s",
        "bridge_ip", "bridge_gateway", "bridge_subnet"
    };

    bool all_valid = true;
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); i++) {
        const char* name = kFields[i];
        if (!g_setup_server.hasArg(name)) {
            DEBUG_PRINTF("Setup: missing required field %s\n", name);
            all_valid = false;
            continue;
        }
        String value = g_setup_server.arg(name);
        if (!validateAndApplyField(updated, name, value.c_str())) {
            DEBUG_PRINTF("Setup: rejected field %s\n", name);
            all_valid = false;
        }
    }

    if (!all_valid) {
        g_setup_server.send(400, "text/plain",
            "One or more fields were invalid or missing - go back and try again.");
        return;
    }

    *g_setup_config = updated;
    eepromStoreSave(*g_setup_config);

    size_t len = buildSavedConfirmationHtml(g_html_buffer, sizeof(g_html_buffer));
    g_setup_server.send(200, "text/html", len > 0 ? g_html_buffer : "Saved. Rebooting...");
    g_setup_server.client().flush();
    delay(300);  // let the response reach the browser before rebooting - Setup Mode isn't timing-critical like normal bridging
    ESP.restart();
}

void setupModeBegin(EepromConfig& config) {
    g_setup_config = &config;

    WiFi.mode(WIFI_AP);
    if (strlen(SETUP_MODE_AP_PASSWORD) > 0) {
        WiFi.softAP(SETUP_MODE_AP_SSID, SETUP_MODE_AP_PASSWORD);
    } else {
        WiFi.softAP(SETUP_MODE_AP_SSID);
    }

    strncpy(g_ap_ip_string, WiFi.softAPIP().toString().c_str(), sizeof(g_ap_ip_string) - 1);
    g_ap_ip_string[sizeof(g_ap_ip_string) - 1] = '\0';

    DEBUG_PRINTF("Setup Mode: AP '%s' started, IP %s\n", SETUP_MODE_AP_SSID, g_ap_ip_string);

    g_setup_server.on("/", HTTP_GET, handleRoot);
    g_setup_server.on("/save", HTTP_POST, handleSave);
    g_setup_server.on("/update", HTTP_GET, handleUpdatePage);

    // ESP8266HTTPUpdateServer (official core library) owns the actual
    // upload/flash handling at "/doupdate" - not hand-rolled here. It
    // writes the new image into the flash region reserved after the
    // running sketch (confirmed via this board's actual linker script,
    // eagle.flash.4m1m.ld - the ~2MB "empty" region between the sketch
    // and the filesystem), verifies it completely, and only then lets
    // eboot swap it in on reboot - a failed/interrupted upload leaves
    // the currently-running firmware untouched, no USB recovery needed
    // for that failure mode. No auth (matches this project's posture
    // elsewhere - see config.h). handleUpdatePage() above serves our own
    // styled "/update" entry page instead of this library's generic one
    // (which also offers an unneeded "FileSystem" upload option - this
    // project has no filesystem use); the custom page's form posts
    // directly to "/doupdate".
    g_http_updater.setup(&g_setup_server, "/doupdate");

    g_setup_server.begin();
}

void setupModeUpdate() {
    g_setup_server.handleClient();
}

const char* setupModeGetApIpString() {
    return g_ap_ip_string;
}

// ============================================================================
// BUTTON / WIFI FALLBACK TRIGGERS (normal operation only)
// ============================================================================

void checkSetupButton() {
    static bool pin_initialized = false;
    static unsigned long press_start_ms = 0;
    static bool triggered = false;

    if (!pin_initialized) {
        pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
        pin_initialized = true;
    }

    // Active-low: a momentary pushbutton wired between SETUP_BUTTON_PIN and
    // GND (see config.h for why this needs real wiring - the board's only
    // onboard button is a hardware RST, unusable for this).
    bool pressed = (digitalRead(SETUP_BUTTON_PIN) == LOW);

    if (!pressed) {
        press_start_ms = 0;
        return;
    }

    if (press_start_ms == 0) {
        press_start_ms = millis();
        return;
    }

    if (!triggered && (millis() - press_start_ms >= SETUP_BUTTON_HOLD_MS)) {
        triggered = true;
        DEBUG_PRINTF("Setup button held - entering Setup Mode on reboot\n");
        requestSetupModeOnNextBoot();
        ESP.restart();
    }
}

void checkWifiFallback() {
    static bool initialized = false;
    static unsigned long last_connected_ms = 0;

    if (!initialized) {
        last_connected_ms = millis();
        initialized = true;
    }

    if (WiFi.status() == WL_CONNECTED) {
        last_connected_ms = millis();
        return;
    }

    if (millis() - last_connected_ms >= WIFI_FALLBACK_TIMEOUT_MS) {
        DEBUG_PRINTF("WiFi disconnected for too long - entering Setup Mode on reboot\n");
        requestSetupModeOnNextBoot();
        ESP.restart();
    }
}
