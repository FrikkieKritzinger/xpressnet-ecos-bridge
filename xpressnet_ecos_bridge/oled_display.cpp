/*
 * OLED Display Driver Implementation
 * 
 * SSD1306 128x64 I2C display on Wemos D1 Mini
 * 
 * Handles:
 * - 4-page information display
 * - Auto-rotation every 5 seconds
 * - Non-blocking updates
 * - Error/status popups with auto-dismiss
 * - Connection status indicators
 */

#include <cstdint>
#include <cstring>
#include <Arduino.h>
#include "display/oled_display.h"
#include "config.h"
#include "definitions.h"
#include "utils/debug.h"

// Screen dimensions constant
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OledDisplay::OledDisplay()
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1),  // No reset pin
      current_page(PAGE_MAIN),
      page_cycle_task(5000)  // 5 second auto-cycle
{
    // Initialize popup message state
    popup_message.message[0] = '\0';
    popup_message.show_time = 0;
    popup_message.is_error = false;
    popup_message.active = false;
    
    DEBUG_PRINT("OledDisplay created (128x64 SSD1306)\n");
}


// ============================================================================
// INITIALIZATION
// ============================================================================

bool OledDisplay::begin() {
    /*
     * Initialize I2C and OLED display
     * 
     * Steps:
     * 1. Start I2C on D1/D2 (SCL/SDA)
     * 2. Initialize SSD1306 at address 0x3C
     * 3. Clear display
     * 4. Show startup message
     * 5. Return status
     */
    
    DEBUG_STARTUP_PRINT("Initializing OLED display...\n");
    
    // Start I2C
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);  // D2=SDA, D1=SCL
    
    // Initialize display
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        DEBUG_STARTUP_PRINTF("ERROR: OLED not found at address 0x%02X\n", OLED_ADDRESS);
        return false;
    }
    
    // Clear and show startup
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println(F("XpressNet-Ecos"));
    display.setCursor(20, 30);
    display.println(F("Bridge"));
    display.setCursor(15, 45);
    display.println(F("Initializing..."));
    display.display();
    
    DEBUG_STARTUP_PRINT("OLED display initialized successfully\n");
    return true;
}

// ============================================================================
// UPDATE - NON-BLOCKING MAIN FUNCTION
// ============================================================================

void OledDisplay::update(const SystemStatus& status) {
    /*
     * Update display with current status
     * Called periodically (every 500ms from main loop)
     * 
     * Responsibilities:
     * 1. Check if popup should clear
     * 2. Check if page should auto-cycle
     * 3. Draw appropriate screen based on current_page
     * 4. Draw popup if active
     * 5. Send to display
     * 
     * All non-blocking - no waits or delays
     */
    
    // Cache current status for access by draw functions
    current_status = status;
    
    // Check if error/status popup should auto-clear
    if (popup_message.active && shouldClearPopup()) {
        popup_message.active = false;
        DEBUG_TIMING_PRINT("Popup auto-cleared\n");
    }
    
    // Check if page should auto-cycle (every 5 seconds)
    if (page_cycle_task.shouldExecute()) {
        cycleNextPage();
    }
    
    // Clear display buffer
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Draw appropriate page
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
    
    // Send buffer to physical display
    display.display();
}

// ============================================================================
// PAGE 0: MAIN SCREEN - Live Status
// ============================================================================

void OledDisplay::drawMainScreen() {
    /*
     * ═ XpressNet-Ecos Bridge ═
     * XNet:✓ Ecos:✓ WiFi:✓
     * Locos: N  Heap: XXkB
     * ───────────────────────
     * Last: Loco 247
     * Speed: 100  Dir: Fwd
     * Fn: F0 F1 F2
     * [Page 1/3]  ◀ ▶
     */
    
    // Title
    display.setTextSize(1);
    display.setCursor(15, 0);
    display.println(F("XpressNet-Ecos Bridge"));
    
    // Connection status row (simplified for 128px width)
    display.setCursor(0, 10);
    display.print(F("XNet:"));
    drawStatusIcon(30, 10, current_status.xnet_status);
    display.print(F(" Ecos:"));
    drawStatusIcon(65, 10, current_status.ecos_status);
    
    // Status line
    display.setCursor(0, 20);
    display.print(F("Locos:"));
    display.print(current_status.active_locos);
    display.print(F(" Heap:"));
    display.print(current_status.current_heap_bytes / 1024);
    display.print(F("KB"));
    
    // Divider
    display.drawLine(0, 28, 128, 28, SSD1306_WHITE);
    
    // Last locomotive info
    display.setCursor(0, 33);
    display.print(F("Last: Loco (TBD)"));
    
    display.setCursor(0, 43);
    display.print(F("Speed: (TBD)  Dir: (TBD)"));
    
    display.setCursor(0, 53);
    display.print(F("Fn: (TBD)"));
    
    // Footer with page indicator
    display.setCursor(0, 61);
    display.setTextSize(1);
    display.print(F("[Page 1/3]"));
}

// ============================================================================
// PAGE 1: DEVICE STATUS
// ============================================================================

void OledDisplay::drawDeviceStatusScreen() {
    /*
     * ════ DEVICE STATUS ═════
     * Device: XNet-Ecos Bridge
     * IP: 192.168.1.105
     * Uptime: HH:MM:SS
     * Heap: XXkB / 80KB
     */
    
    // Title
    display.setCursor(25, 0);
    display.setTextSize(1);
    display.println(F("DEVICE STATUS"));
    display.drawLine(0, 8, 128, 8, SSD1306_WHITE);
    
    // Device name
    display.setCursor(0, 12);
    display.print(F("XNet-Ecos Bridge"));
    
    // IP address
    display.setCursor(0, 22);
    display.print(F("IP: 192.168.1.105"));
    
    // Uptime
    display.setCursor(0, 32);
    display.print(F("Uptime: "));
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
    
    // Heap
    display.setCursor(0, 42);
    uint32_t heap_kb = current_status.current_heap_bytes / 1024;
    uint8_t heap_percent = (current_status.current_heap_bytes * 100) / 81920;
    display.print(F("Heap: "));
    display.print(heap_kb);
    display.print(F("/80KB ("));
    display.print(heap_percent);
    display.print(F("%)"));
    
    // Memory warning
    if (current_status.current_heap_bytes < 10000) {
        display.setCursor(0, 52);
        display.println(F("WARNING: Low memory!"));
    }
    
    // Footer
    display.setCursor(0, 61);
    display.print(F("[Page 2/3]"));
}

// ============================================================================
// PAGE 2: XPRESSNET DETAILS
// ============================================================================

void OledDisplay::drawXpressNetScreen() {
    /*
     * ════ XPRESSNET ════════
     * Status: CONNECTED
     * Devices: 3
     * Active: 5
     * Last Msg: 0.5s ago
     * [Page 3/3]
     */
    
    // Title
    display.setCursor(20, 0);
    display.setTextSize(1);
    display.println(F("XPRESSNET"));
    display.drawLine(0, 8, 128, 8, SSD1306_WHITE);
    
    // Status
    display.setCursor(0, 12);
    display.print(F("Status: "));
    const char* status_str = statusToString(current_status.xnet_status);
    display.println(status_str);
    
    // Devices and locos
    display.setCursor(0, 22);
    display.print(F("Devices: 0"));  // TODO: Get from interface
    
    display.setCursor(0, 32);
    display.print(F("Active Locos: "));
    display.println(current_status.active_locos);
    
    // Last message
    display.setCursor(0, 42);
    display.print(F("Last Msg: N/A"));  // TODO: Get from interface
    
    // Footer
    display.setCursor(0, 61);
    display.print(F("[Page 3/3]"));
}

// ============================================================================
// PAGE 3: ECOS STATUS
// ============================================================================

void OledDisplay::drawEcosScreen() {
    /*
     * ════ ECOS LAN ═════════
     * Status: CONNECTED
     * IP: 192.168.1.100
     * Heartbeat: ✓ OK
     * Subscribed: N locos
     */
    
    // Title
    display.setCursor(30, 0);
    display.setTextSize(1);
    display.println(F("ECOS LAN"));
    display.drawLine(0, 8, 128, 8, SSD1306_WHITE);
    
    // Status
    display.setCursor(0, 12);
    display.print(F("Status: "));
    const char* status_str = statusToString(current_status.ecos_status);
    display.println(status_str);
    
    // Ecos IP
    display.setCursor(0, 22);
    display.print(F("IP: "));
    display.println(ECOS_IP);
    
    // Heartbeat
    display.setCursor(0, 32);
    display.print(F("Heartbeat: "));
    if (current_status.ecos_status == ComponentStatus::CONNECTED) {
        display.print(F("OK"));
    } else {
        display.print(F("N/A"));
    }
    
    // Subscribed locos
    display.setCursor(0, 42);
    display.print(F("Subscribed: "));
    display.print(current_status.active_locos);
    display.println(F(" locos"));
    
    // Footer
    display.setCursor(0, 61);
    display.print(F("[Page 4/3]"));
}

// ============================================================================
// ERROR/STATUS POPUP OVERLAY
// ============================================================================

void OledDisplay::drawErrorPopup() {
    /*
     * Draw popup overlay with message
     * Layout:
     * ┌──────────────────────┐
     * │ Message text here    │
     * └──────────────────────┘
     */
    
    // Draw dark background box
    display.fillRect(10, 20, 108, 24, SSD1306_WHITE);
    
    // Draw message text in black
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(15, 25);
    display.println(popup_message.message);
    
    // Restore white text color
    display.setTextColor(SSD1306_WHITE);
}

// ============================================================================
// HELPER: DRAW CONNECTION STATUS ICON
// ============================================================================

void OledDisplay::drawStatusIcon(int x, int y, ComponentStatus status) {
    /*
     * Draw small status indicator icon
     * ✓ = Connected
     * ✗ = Disconnected
     * ◇ = Connecting
     * ! = Error
     */
    
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

// ============================================================================
// PUBLIC: SHOW ERROR MESSAGE
// ============================================================================

void OledDisplay::showErrorMessage(const char* message) {
    /*
     * Display error message popup
     * Auto-dismisses after 3 seconds
     */
    
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
    /*
     * Display status message popup (non-error)
     * Auto-dismisses after 2 seconds
     */
    
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
    /*
     * Move to next page in rotation
     * Wraps from PAGE_ECOS → PAGE_MAIN
     */
    
    current_page = (DisplayPage)((current_page + 1) % PAGE_COUNT);
    DEBUG_TIMING_PRINTF("Display page changed to %d\n", current_page);
}

// ============================================================================
// PRIVATE: POPUP EXPIRY CHECK
// ============================================================================

bool OledDisplay::shouldClearPopup() const {
    /*
     * Check if popup has been displayed long enough
     * 
     * Timeout:
     * - Error: 3 seconds
     * - Status: 2 seconds
     */
    
    if (!popup_message.active) {
        return false;
    }
    
    unsigned long timeout = popup_message.is_error ? 3000 : 2000;
    unsigned long age = millis() - popup_message.show_time;
    
    return age > timeout;
}