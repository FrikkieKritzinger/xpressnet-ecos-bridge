/*
 * OLED Display Driver Implementation
 * Yellow/Blue 0.96" 128x64 SSD1306
 * 
 * Yellow section (rows 0-15): Headers + connection status
 * Blue section (rows 16-63): Content details
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "display/oled_display.h"
#include "display/boot_logo.h"
#include "config.h"
#include "definitions.h"
#include "utils/debug.h"
#include "utils/memory.h"

// ============================================================================
// SCREEN LAYOUT CONSTANTS
// ============================================================================

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

// Color split position (where yellow ends, blue begins)
#define COLOR_SPLIT_Y   16

// Layout positioning (optimized for yellow/blue display)
#define YELLOW_TITLE_Y      0     // Title in yellow
#define YELLOW_STATUS_Y     8     // Status bar in yellow
#define BLUE_CONTENT_Y     17     // Start of blue content (17 gives some gap)
#define LINE_HEIGHT_SMALL   8     // Small text line height
#define LINE_HEIGHT_LARGE  10     // Large text line height

// Header icon zone (title row, y=0). WiFi is infrastructure, not
// protocol-specific, so it's global - drawn on every page in the rightmost
// corner. Each protocol's connection icon is page-local instead (drawn only
// by that protocol's own screen, just left of the WiFi icon) - the pattern
// to follow when LocoNet/Z21 are added, since there won't be room to show
// every protocol's status on every page at once.
#define ICON_Y               0
#define WIFI_ICON_X        115     // rightmost corner - every page
#define PROTOCOL_ICON_X    102     // just left of WiFi icon - page-local only

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OledDisplay::OledDisplay()
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1),
      current_page(PAGE_MAIN),
      page_cycle_task(5000)
{
    popup_message.message[0] = '\0';
    popup_message.show_time = 0;
    popup_message.is_error = false;
    popup_message.active = false;

    strncpy(ecos_ip, ECOS_IP, sizeof(ecos_ip) - 1);
    ecos_ip[sizeof(ecos_ip) - 1] = '\0';

    DEBUG_PRINT("OledDisplay created (128x64 SSD1306 Yellow/Blue)\n");
}

void OledDisplay::setEcosIp(const char* ip) {
    strncpy(ecos_ip, ip, sizeof(ecos_ip) - 1);
    ecos_ip[sizeof(ecos_ip) - 1] = '\0';
}

void OledDisplay::showSetupMode(const char* ap_ssid, const char* ap_ip) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 4);
    display.print(F("*** SETUP MODE ***"));
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);

    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("Connect to WiFi:"));
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(ap_ssid);

    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("Browse to:"));
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 4);
    display.print(F("http://"));
    display.print(ap_ip);

    display.display();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool OledDisplay::begin() {
    DEBUG_STARTUP_PRINT("Initializing OLED display...\n");

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        DEBUG_STARTUP_PRINTF("ERROR: OLED not found at address 0x%02X\n", OLED_ADDRESS);
        return false;
    }

    // Boot splash: shown for OLED_BOOT_LOGO_DURATION_MS from here, while
    // setup() continues on to init XpressNet/Ecos and the main loop starts
    // connecting in the background - see update()'s early-return gate.
    boot_started_ms = millis();
    drawBootLogo();

    DEBUG_STARTUP_PRINT("OLED display initialized successfully\n");
    return true;
}

// ============================================================================
// BOOT SPLASH (OmniConnect logo)
// ============================================================================

void OledDisplay::drawBootLogo() {
    // "OmniConnect" text lives in the top 16 rows - the same yellow/header
    // band every other page splits at COLOR_SPLIT_Y - and the logo bitmap
    // starts below that line. Keeps the whole splash usable regardless of
    // whether this ends up being a full-white OLED or a yellow/blue one:
    // neither the text nor the logo mark straddles the physical color
    // split, unlike the original top-to-bottom layout.
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    const char* title = "OmniConnect";
    int text_width = (int)strlen(title) * 6;  // 6px/char at text size 1
    display.setCursor((SCREEN_WIDTH - text_width) / 2, 4);
    display.print(title);

    // No divider line here (unlike the regular pages) - the physical
    // yellow/blue color split at this same row already does that visually
    // on the two-tone display, and a drawn line was redundant/unwanted on it.

    // logo.png's icon mark only - the wordmark/tagline were cropped out
    // during conversion, too fine-detailed to stay legible at this
    // resolution (see display/boot_logo.h).
    display.drawBitmap((SCREEN_WIDTH - BOOT_LOGO_WIDTH) / 2, COLOR_SPLIT_Y + 4,
                        boot_logo_bitmap, BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT,
                        SSD1306_WHITE);

    display.display();
}

// ============================================================================
// UPDATE - NON-BLOCKING MAIN FUNCTION
// ============================================================================

void OledDisplay::update(const SystemStatus& status) {
    current_status = status;

    if (!boot_logo_done) {
        if (millis() - boot_started_ms < OLED_BOOT_LOGO_DURATION_MS) {
            return;  // still showing the static splash - nothing else to draw
        }
        boot_logo_done = true;
        // Give PAGE_MAIN a full dwell instead of page_cycle_task firing
        // immediately (its internal timer started at construction, long
        // before the splash window ended).
        page_cycle_task.reset();
    }

    if (popup_message.active && shouldClearPopup()) {
        popup_message.active = false;
        DEBUG_TIMING_PRINT("Popup auto-cleared\n");
    }
    
    if (page_cycle_task.shouldExecute()) {
        cycleNextPage();
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Header icon cluster - same position on every page, drawn before the
    // page-specific title/content so it's never overwritten.
    drawHeaderIcons();

    // Draw page content
    switch (current_page) {
        case PAGE_MAIN:
            drawMainScreen();
            break;
        case PAGE_DEVICE:
            drawDeviceStatusScreen();
            break;
        case PAGE_XNET:
            drawXpressNetScreen();
            break;
        case PAGE_ECOS:
            drawEcosScreen();
            break;
        case PAGE_Z21:
            drawZ21Screen();
            break;
        default:
            current_page = PAGE_MAIN;
            drawMainScreen();
            break;
    }

    // Page-position dots (replaces the old per-page "[Page X/4]" footer text)
    drawPageIndicator();

    // Draw popup overlay if active
    if (popup_message.active) {
        drawErrorPopup();
    }

    display.display();
}

void OledDisplay::buildActiveFunctionsLabel(uint32_t functions, char* buf, size_t buf_size) {
    if (buf_size == 0) {
        return;
    }
    buf[0] = '\0';

    if (functions == 0) {
        strncpy(buf, "none", buf_size);
        buf[buf_size - 1] = '\0';
        return;
    }

    // Reserve room for a trailing "..." (3 chars) + null terminator
    // upfront, so there's always room to signal truncation instead of
    // silently dropping the entry that didn't quite fit (real bug: the
    // original version only checked for ellipsis room *after* failing to
    // fit the next entry, by which point the buffer could already be full).
    size_t ellipsis_reserve = (buf_size > 4) ? 3 : 0;
    size_t usable = buf_size - 1 - ellipsis_reserve;

    size_t used = 0;
    bool first = true;
    bool truncated = false;
    for (uint8_t fn = 0; fn < DCC_FUNCTION_COUNT; fn++) {
        if (!(functions & (1UL << fn))) {
            continue;
        }

        char entry[5];  // ",31\0" worst case
        int entry_len = snprintf(entry, sizeof(entry), first ? "%u" : ",%u", fn);
        if (entry_len < 0) {
            break;
        }

        if (used + (size_t)entry_len > usable) {
            truncated = true;
            break;
        }

        memcpy(buf + used, entry, entry_len);
        used += entry_len;
        first = false;
    }

    buf[used] = '\0';
    if (truncated && ellipsis_reserve > 0) {
        strcat(buf, "...");
    }
}

// ============================================================================
// PAGE 0: MAIN SCREEN - Live Status
// ============================================================================

void OledDisplay::drawMainScreen() {
    // ========== YELLOW SECTION (Header) ==========
    // Left-aligned (was centered at x=20) - the header icon cluster now
    // occupies the top-right corner of this row starting at x=86, and this
    // title is just long enough (14 chars * 6px = 84px) to nearly reach it
    // from x=0.
    display.setTextSize(1);
    display.setCursor(0, YELLOW_TITLE_Y);
    display.println(F("XpressNet-Ecos"));
    
    // Status bar in yellow - XNet/Ecos connection state has its own
    // page-local icon now (see drawXpressNetScreen()/drawEcosScreen()), not
    // shown here on the main overview page
    display.setCursor(0, YELLOW_STATUS_Y);
    display.print(F("Locos: "));
    display.print(current_status.active_locos);
    
    // ========== BLUE SECTION (Content) ==========
    
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // Memory info
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("Heap:"));
    display.print(current_status.current_heap_bytes / 1024);
    display.print(F("KB Mem:"));
    uint8_t mem_percent = (current_status.current_heap_bytes * 100) / 81920;
    display.print(mem_percent);
    display.print(F("%"));
    
    // Last command
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    if (current_status.last_command_address > 0) {
        display.print(F("Last: Loco "));
        display.print(current_status.last_command_address);
    } else {
        display.print(F("Last: (none yet)"));
    }

    // Speed and direction
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    if (current_status.last_command_address > 0) {
        display.print(F("Spd: "));
        display.print(current_status.last_command_speed);
        display.print(current_status.last_command_direction ? F(" Dir: Fwd") : F(" Dir: Rev"));
    } else {
        display.print(F("Spd: -- Dir: --"));
    }

    // Functions - comma-separated list of active function numbers,
    // truncated with "..." if it overflows the line's ~17 usable
    // characters after the "Fn: " prefix (see buildActiveFunctionsLabel()).
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("Fn: "));
    if (current_status.last_command_address > 0) {
        char fn_label[18];
        buildActiveFunctionsLabel(current_status.last_command_functions, fn_label, sizeof(fn_label));
        display.print(fn_label);
    } else {
        display.print(F("--"));
    }

    // Last accessory/turnout command (Phase 5 step 10, v1) - just the
    // single most recent one, not a per-address table.
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 4);
    if (current_status.last_accessory_address > 0) {
        display.print(F("Acc "));
        display.print(current_status.last_accessory_address);
        display.print(current_status.last_accessory_diverging ? F(": Div") : F(": Str"));
    } else {
        display.print(F("Acc: (none yet)"));
    }
}

// ============================================================================
// PAGE 1: DEVICE STATUS
// ============================================================================

void OledDisplay::drawDeviceStatusScreen() {
    // ========== YELLOW SECTION ==========
    // Left-aligned - see drawMainScreen() for why (leaves room for the
    // header icon cluster at x=86+)
    display.setTextSize(1);
    display.setCursor(0, YELLOW_TITLE_Y);
    display.println(F("DEVICE STATUS"));
    
    display.setCursor(18, YELLOW_STATUS_Y);
    display.println(F("XNet-Ecos Bridge"));
    
    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // IP address
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("IP: "));
    display.print(WiFi.localIP());
    
    // Uptime
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(F("Up: "));
    unsigned long seconds = current_status.uptime_ms / 1000;
    unsigned long hours = seconds / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    unsigned long secs = seconds % 60;
    
    if (hours < 10) display.print(F("0"));
    display.print(hours);
    display.print(F(":"));
    if (minutes < 10) display.print(F("0"));
    display.print(minutes);
    display.print(F(":"));
    if (secs < 10) display.print(F("0"));
    display.print(secs);
    
    // Heap info
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    uint32_t heap_kb = current_status.current_heap_bytes / 1024;
    uint8_t heap_percent = (current_status.current_heap_bytes * 100) / 81920;
    display.print(F("Heap: "));
    display.print(heap_kb);
    display.print(F("/80KB ("));
    display.print(heap_percent);
    display.print(F("%)"));
    
    // CPU freq (RSSI now lives only in the header icon, shown on every page -
    // no need to duplicate it here as text too)
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("CPU: "));
    display.print(getCpuFreqMhz());
    display.print(F("MHz"));
    
    // Firmware version (Phase 6 step 3) - a conventional version number
    // (version.h), not a raw build timestamp - easier to recognize and
    // track across releases. Confirms an OTA update actually took effect.
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 4);
    display.print(F("FW: v"));
    display.print(F(FIRMWARE_VERSION));

    // Memory warning if critical - separate line, doesn't compete with
    // the always-shown firmware line above.
    if (current_status.current_heap_bytes < 10000) {
        display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 5);
        display.println(F("!WARNING: Low memory"));
    }
}
// ============================================================================
// PAGE 2: XPRESSNET DETAILS
// ============================================================================

void OledDisplay::drawXpressNetScreen() {
    // ========== YELLOW SECTION ==========
    // Left-aligned - see drawMainScreen() for why (leaves room for the
    // header icon cluster at x=86+)
    display.setTextSize(1);
    display.setCursor(0, YELLOW_TITLE_Y);
    display.println(F("XPRESSNET"));

    // Connection icon - page-local (only shown here, not on other pages -
    // see drawHeaderIcons() for the reasoning)
    drawConnectionIcon(PROTOCOL_ICON_X, ICON_Y, current_status.xnet_status);

    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // Active locos
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("Active: "));
    display.print(current_status.active_locos);
    display.print(F(" locos"));

    // Last message timing
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(F("Last Msg: "));
    if (current_status.xnet_last_message_age_ms == ProtocolInterface::NO_TIMESTAMP) {
        display.print(F("N/A"));
    } else {
        display.print(current_status.xnet_last_message_age_ms / 1000);
        display.print(F("s"));
    }

    // Command counter
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    display.print(F("Commands: "));
    display.print(current_status.total_commands);

    // Echo prevention counter
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("Echo Prev: "));
    display.print(current_status.echo_prevented_count);
}

// ============================================================================
// PAGE 3: ECOS STATUS
// ============================================================================

void OledDisplay::drawEcosScreen() {
    // ========== YELLOW SECTION ==========
    // Left-aligned - see drawMainScreen() for why (leaves room for the
    // header icon cluster at x=86+)
    display.setTextSize(1);
    display.setCursor(0, YELLOW_TITLE_Y);
    display.println(F("ECOS LAN"));

    // Connection icon - page-local (only shown here, not on other pages -
    // see drawHeaderIcons() for the reasoning)
    drawConnectionIcon(PROTOCOL_ICON_X, ICON_Y, current_status.ecos_status);

    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // Ecos IP
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("IP: "));
    display.println(ecos_ip);
    
    // Heartbeat
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(F("Heartbeat: "));
    if (current_status.ecos_status == ComponentStatus::CONNECTED) {
        display.print(F("OK"));     // Changed from ✓
    } else {
        display.print(F("NO"));     // Changed from ✗
    }
    
    // Subscribed locos
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    display.print(F("Subscribed: "));
    display.print(current_status.active_locos);
    display.print(F(" locos"));
    
    // Latency
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("Latency: "));
    if (current_status.ecos_heartbeat_latency_ms == ProtocolInterface::NO_TIMESTAMP) {
        display.print(F("N/A"));
    } else {
        display.print(current_status.ecos_heartbeat_latency_ms);
        display.print(F("ms"));
    }
}

// ============================================================================
// PAGE 4: Z21 LAN STATUS
// ============================================================================

void OledDisplay::drawZ21Screen() {
    // ========== YELLOW SECTION ==========
    display.setTextSize(1);
    display.setCursor(0, YELLOW_TITLE_Y);
    display.println(F("Z21 LAN"));

    // Connection icon - page-local. Unlike XNet/Ecos, "connected" here
    // means "at least one client currently active", not "socket bound" -
    // see Z21LanInterface::getStatus()'s comment for why a UDP socket
    // being bound isn't a meaningful signal on its own.
    drawConnectionIcon(PROTOCOL_ICON_X, ICON_Y, current_status.z21_status);

    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);

    // Client count - the one piece of information no other page has any
    // equivalent of (XpressNet has no concept of "how many devices" on
    // its shared bus; Ecos is a single TCP link).
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("Clients: "));
    display.print(current_status.z21_client_count);

    // Last message age
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(F("Last Msg: "));
    if (current_status.z21_last_message_age_ms == ProtocolInterface::NO_TIMESTAMP) {
        display.print(F("N/A"));
    } else {
        display.print(current_status.z21_last_message_age_ms / 1000);
        display.print(F("s"));
    }

    // Source of that last message - deliberately the single most recent
    // sender, not a list of every connected client (which could be any
    // number on a layout with more throttles than this project's own test
    // setup) - "who's actively driving right now" is the useful signal.
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    display.print(F("From: "));
    if (current_status.z21_last_message_ip[0] == '\0') {
        display.print(F("N/A"));
    } else {
        display.print(current_status.z21_last_message_ip);
    }
}

// ============================================================================
// ERROR/STATUS POPUP OVERLAY
// ============================================================================

void OledDisplay::drawErrorPopup() {
    // Draw semi-transparent background
    display.fillRect(8, 20, 112, 24, SSD1306_WHITE);
    
    // Draw message text in black
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(12, 25);
    display.println(popup_message.message);
    
    // Restore white text color for next draw
    display.setTextColor(SSD1306_WHITE);
}

// ============================================================================
// HELPER: HEADER ICONS (WiFi bars global; connection icons are page-local)
// ============================================================================
//
// A previous version of this display used Unicode glyphs (checkmark/X/
// diamond) here via the default font - confirmed on real hardware to render
// as garbage, since the SSD1306's built-in font only covers ASCII. Every
// icon below is hand-drawn with GFX primitives (circles/lines/rects) instead
// of relying on any font glyph, which sidesteps that failure entirely.

void OledDisplay::drawHeaderIcons() {
    // WiFi is infrastructure, not protocol-specific - shown on every page,
    // rightmost corner. Each interface's own connection icon (XNet/Ecos/
    // future LocoNet/Z21) is drawn by that interface's own screen function
    // instead, at PROTOCOL_ICON_X, so it only appears on its own page.
    drawWifiSignalIcon(WIFI_ICON_X, ICON_Y, current_status.wifi_rssi);
}

void OledDisplay::drawConnectionIcon(int x, int y, ComponentStatus status) {
    // ~7x8px, hand-drawn - no font glyphs involved
    switch (status) {
        case ComponentStatus::CONNECTED:
            display.fillCircle(x + 3, y + 4, 3, SSD1306_WHITE);
            break;
        case ComponentStatus::DISCONNECTED:
            display.drawLine(x, y + 1, x + 6, y + 7, SSD1306_WHITE);
            display.drawLine(x + 6, y + 1, x, y + 7, SSD1306_WHITE);
            break;
        case ComponentStatus::CONNECTING:
            display.drawCircle(x + 3, y + 4, 3, SSD1306_WHITE);
            break;
        case ComponentStatus::ERROR:
            display.fillTriangle(x, y + 7, x + 6, y + 7, x + 3, y + 1, SSD1306_WHITE);
            break;
    }
}

void OledDisplay::drawWifiSignalIcon(int x, int y, int rssi_dbm) {
    // Classic 4-bar signal gauge, increasing height, bottom-aligned within a
    // 9px-tall cell. Bars below the strength threshold are simply left
    // undrawn (a 2px-wide "hollow" rect would look filled anyway at this
    // scale, so there's no point drawing an outline for them).
    uint8_t bars;
    if (rssi_dbm >= -55) bars = 4;
    else if (rssi_dbm >= -65) bars = 3;
    else if (rssi_dbm >= -75) bars = 2;
    else if (rssi_dbm >= -85) bars = 1;
    else bars = 0;

    static const uint8_t bar_heights[4] = {3, 5, 7, 9};
    int bar_x = x;
    for (uint8_t i = 0; i < 4; i++) {
        if (i < bars) {
            uint8_t h = bar_heights[i];
            display.fillRect(bar_x, y + (9 - h), 2, h, SSD1306_WHITE);
        }
        bar_x += 3;  // 2px bar + 1px gap
    }
}

// ============================================================================
// HELPER: PAGE-POSITION INDICATOR (replaces the old "[Page X/4]" footer text)
// ============================================================================

void OledDisplay::drawPageIndicator() {
    const int dot_spacing = 8;
    const int dot_radius = 2;
    int total_width = (PAGE_COUNT - 1) * dot_spacing;
    int start_x = (SCREEN_WIDTH - total_width) / 2;
    int y = SCREEN_HEIGHT - 4;

    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        int cx = start_x + i * dot_spacing;
        if (i == current_page) {
            display.fillCircle(cx, y, dot_radius, SSD1306_WHITE);
        } else {
            display.drawCircle(cx, y, dot_radius, SSD1306_WHITE);
        }
    }
}

// ============================================================================
// PUBLIC: SHOW ERROR MESSAGE
// ============================================================================

void OledDisplay::showErrorMessage(const char* message) {
    strncpy(popup_message.message, message, sizeof(popup_message.message) - 1);
    popup_message.message[sizeof(popup_message.message) - 1] = '\0';
    popup_message.is_error = true;
    popup_message.show_time = millis();
    popup_message.active = true;
    
    DEBUG_PRINTF("Error popup: %s\n", message);
}

// ============================================================================
// PUBLIC: SHOW STATUS MESSAGE
// ============================================================================

void OledDisplay::showStatusMessage(const char* message) {
    strncpy(popup_message.message, message, sizeof(popup_message.message) - 1);
    popup_message.message[sizeof(popup_message.message) - 1] = '\0';
    popup_message.is_error = false;
    popup_message.show_time = millis();
    popup_message.active = true;
    
    DEBUG_PRINTF("Status popup: %s\n", message);
}

// ============================================================================
// PUBLIC: PAGE NAVIGATION
// ============================================================================

void OledDisplay::nextPage() {
    cycleNextPage();
}

void OledDisplay::prevPage() {
    current_page = (DisplayPage)((current_page + PAGE_COUNT - 1) % PAGE_COUNT);
}

// ============================================================================
// PRIVATE: PAGE CYCLING
// ============================================================================

void OledDisplay::cycleNextPage() {
    current_page = (DisplayPage)((current_page + 1) % PAGE_COUNT);
    DEBUG_TIMING_PRINTF("Display page changed to %d\n", current_page);
}

// ============================================================================
// PRIVATE: POPUP EXPIRY CHECK
// ============================================================================

bool OledDisplay::shouldClearPopup() const {
    if (!popup_message.active) {
        return false;
    }
    
    unsigned long timeout = popup_message.is_error ? 3000 : 2000;
    unsigned long age = millis() - popup_message.show_time;
    
    return age > timeout;
}