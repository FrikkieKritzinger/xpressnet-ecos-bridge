/*
 * OLED Display Driver Implementation
 * Yellow/Blue 0.96" 128x64 SSD1306
 * 
 * Yellow section (rows 0-15): Headers + connection status
 * Blue section (rows 16-63): Content details
 */

#include <cstdint>
#include <cstring>
#include <Arduino.h>
#include "display/oled_display.h"
#include "config.h"
#include "definitions.h"
#include "utils/debug.h"

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
    
    DEBUG_PRINT("OledDisplay created (128x64 SSD1306 Yellow/Blue)\n");
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
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Title in yellow
    display.setCursor(20, YELLOW_TITLE_Y);
    display.println(F("XpressNet-Ecos"));
    
    // Subtitle in yellow
    display.setCursor(35, YELLOW_STATUS_Y);
    display.println(F("Bridge"));
    
    // Content in blue
    display.setCursor(20, BLUE_CONTENT_Y + 10);
    display.println(F("Initializing..."));
    
    display.display();
    
    DEBUG_STARTUP_PRINT("OLED display initialized successfully\n");
    return true;
}

// ============================================================================
// UPDATE - NON-BLOCKING MAIN FUNCTION
// ============================================================================

void OledDisplay::update(const SystemStatus& status) {
    current_status = status;
    
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
        default:
            current_page = PAGE_MAIN;
            drawMainScreen();
            break;
    }
    
    // Draw popup overlay if active
    if (popup_message.active) {
        drawErrorPopup();
    }
    
    display.display();
}

// ============================================================================
// PAGE 0: MAIN SCREEN - Live Status
// ============================================================================

void OledDisplay::drawMainScreen() {
    // ========== YELLOW SECTION (Header) ==========
    display.setTextSize(1);
    display.setCursor(20, YELLOW_TITLE_Y);
    display.println(F("XpressNet-Ecos"));
    
     // Status bar in yellow - CHANGED: Use text instead of icons
    display.setCursor(0, YELLOW_STATUS_Y);
    display.print(F("XNet:"));
    drawStatusText(30, YELLOW_STATUS_Y, current_status.xnet_status);
    display.print(F(" Ecos:"));
    drawStatusText(80, YELLOW_STATUS_Y, current_status.ecos_status);
    display.print(F(" L:"));
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
    
    // Last command (placeholder for now)
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(F("Last: Loco (TBD)"));
    
    // Speed and direction
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    display.print(F("Spd: (TBD) Dir: (TBD)"));
    
    // Functions
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("Fn: (TBD)"));
    
    // Footer with page indicator
    display.setCursor(0, 57);  // Fixed position, not calculated
    display.print(F("[Page 1/4]"));
}

// ============================================================================
// PAGE 1: DEVICE STATUS
// ============================================================================

void OledDisplay::drawDeviceStatusScreen() {
    // ========== YELLOW SECTION ==========
    display.setTextSize(1);
    display.setCursor(30, YELLOW_TITLE_Y);
    display.println(F("DEVICE STATUS"));
    
    display.setCursor(18, YELLOW_STATUS_Y);
    display.println(F("XNet-Ecos Bridge"));
    
    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // IP address
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("IP: 192.168.1.105"));
    
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
    
    // CPU and IRAM
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("CPU: 80MHz IRAM: 92%"));
    
    // Memory warning if critical
    if (current_status.current_heap_bytes < 10000) {
        display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 4);
        display.println(F("!WARNING: Low memory"));
    }
    
    // Footer
    display.setCursor(0, 57);  // Fixed position, not calculated
    display.print(F("[Page 2/4]"));
}
// ============================================================================
// PAGE 2: XPRESSNET DETAILS
// ============================================================================

void OledDisplay::drawXpressNetScreen() {
    // ========== YELLOW SECTION ==========
    display.setTextSize(1);
    display.setCursor(35, YELLOW_TITLE_Y);
    display.println(F("XPRESSNET"));
    
    display.setCursor(0, YELLOW_STATUS_Y);
    display.print(F("Status: "));
    const char* status_str = statusToString(current_status.xnet_status);
    display.println(status_str);
    
    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // Devices count
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("Devices: 0"));
    
    // Active locos
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL);
    display.print(F("Active: "));
    display.print(current_status.active_locos);
    display.print(F(" locos"));
    
    // Last message timing
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 2);
    display.print(F("Last Msg: N/A"));
    
    // Command counter
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 3);
    display.print(F("Commands: 0"));
    
    // Echo prevention counter
    display.setCursor(0, BLUE_CONTENT_Y + LINE_HEIGHT_SMALL * 4);
    display.print(F("Echo Prev: 0"));
    
    // Footer
    display.setCursor(0, 57);  // Fixed position, not calculated
    display.print(F("[Page 3/4]"));
}

// ============================================================================
// PAGE 3: ECOS STATUS
// ============================================================================

void OledDisplay::drawEcosScreen() {
    // ========== YELLOW SECTION ==========
    display.setTextSize(1);
    display.setCursor(40, YELLOW_TITLE_Y);
    display.println(F("ECOS LAN"));
    
    display.setCursor(0, YELLOW_STATUS_Y);
    display.print(F("Status: "));
    const char* status_str = statusToString(current_status.ecos_status);
    display.println(status_str);
    
    // ========== BLUE SECTION ==========
    display.drawLine(0, COLOR_SPLIT_Y, 128, COLOR_SPLIT_Y, SSD1306_WHITE);
    
    // Ecos IP
    display.setCursor(0, BLUE_CONTENT_Y);
    display.print(F("IP: "));
    display.println(ECOS_IP);
    
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
    display.print(F("Latency: 125ms"));
    
    // Footer
    display.setCursor(0, 57);  // Fixed position, not calculated
    display.print(F("[Page 4/4]"));
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
// HELPER: DRAW CONNECTION STATUS ICON
// ============================================================================

void OledDisplay::drawStatusIcon(int x, int y, ComponentStatus status) {
    display.setCursor(x, y);
    
    switch (status) {
        case ComponentStatus::CONNECTED:
            display.print(F("✓"));
            break;
        case ComponentStatus::DISCONNECTED:
            display.print(F("✗"));
            break;
        case ComponentStatus::CONNECTING:
            display.print(F("◇"));
            break;
        case ComponentStatus::ERROR:
            display.print(F("!"));
            break;
    }
}

void OledDisplay::drawStatusText(int x, int y, ComponentStatus status) {
    display.setCursor(x, y);
    
    switch (status) {
        case ComponentStatus::CONNECTED:
            display.print(F("OK"));
            break;
        case ComponentStatus::DISCONNECTED:
            display.print(F("NO"));
            break;
        case ComponentStatus::CONNECTING:
            display.print(F(".."));
            break;
        case ComponentStatus::ERROR:
            display.print(F("ER"));
            break;
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