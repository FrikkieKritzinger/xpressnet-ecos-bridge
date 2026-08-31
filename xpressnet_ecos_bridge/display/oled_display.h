/*
 * OLED Display Driver - SSD1306 I2C 128x64
 * 
 * Features:
 * - 4-page rotation (Main, Device, XpressNet, Ecos)
 * - Auto-cycle every 5 seconds
 * - Error popup system (auto-dismiss after 3 seconds)
 * - Hand-drawn icons: WiFi signal (global, every page) + per-interface
 *   connection status (page-local - only shown on that interface's own page)
 * - Live locomotive display
 * - Non-blocking updates via TimedTask
 * 
 * Hardware:
 * - Adafruit_SSD1306 library (128x64)
 * - I2C: SDA=GPIO4 (D2), SCL=GPIO5 (D1)
 * - Address: 0x3C
 * 
 * Dependencies:
 * - Adafruit_SSD1306
 * - Adafruit_GFX
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <cstdint>
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "../interfaces/interface_base.h"
#include "../definitions.h"
#include "../utils/timing.h"

// Forward declaration
class CommandRouter;

class OledDisplay : public DisplayInterface {
public:
    /**
     * Constructor - initialize display object
     */
    OledDisplay();
    
    /**
     * Initialize I2C and OLED display
     * Called once from setup()
     * @return true if successful, false if hardware unavailable
     */
    bool begin() override;
    
    /**
     * Update display with current system status
     * Non-blocking - runs every OLED_UPDATE_INTERVAL
     * Handles page rotation and popup timing
     * 
     * @param status Current SystemStatus snapshot
     */
    void update(const SystemStatus& status) override;
    
    /**
     * Show temporary error message
     * Auto-dismisses after 3 seconds
     * 
     * @param message Error message to display
     */
    void showErrorMessage(const char* message);
    
    /**
     * Show status message (e.g., "Connecting to Ecos...")
     * Clears after 2 seconds
     * 
     * @param message Status message
     */
    void showStatusMessage(const char* message);
    
    /**
     * Manually advance to next page
     * (For future button support)
     */
    void nextPage();
    
    /**
     * Manually go to previous page
     * (For future button support)
     */
    void prevPage();
    
    /**
     * Get current page number (0-3)
     */
    uint8_t getCurrentPage() const { return current_page; }

    /**
     * Set the Ecos IP to show on the Ecos page (Phase 6 step 1 - EEPROM
     * value, may differ from the compile-time ECOS_IP default). Call
     * before/independent of update(); defaults to ECOS_IP until called.
     */
    void setEcosIp(const char* ip);

    /**
     * Show a dedicated static "SETUP MODE" screen (Phase 6 step 2) - not
     * part of the normal paged rotation, no auto-refresh needed since
     * nothing on it changes while Setup Mode is active. Call once when
     * entering Setup Mode; the caller (setup_mode.cpp) owns not calling
     * the normal update() afterward until back in normal operation.
     * @param ap_ssid The Setup Mode AP's SSID to connect to
     * @param ap_ip The AP's IP to browse to (typically 192.168.4.1)
     */
    void showSetupMode(const char* ap_ssid, const char* ap_ip);

private:
    // Hardware objects
    Adafruit_SSD1306 display;
    
    // Display state
    enum DisplayPage {
        PAGE_MAIN = 0,
        PAGE_DEVICE = 1,
        PAGE_XNET = 2,
        PAGE_ECOS = 3,
        PAGE_Z21 = 4,
        PAGE_COUNT = 5
    };
    
    DisplayPage current_page;
    
    // Error/status message state
    struct MessageState {
        char message[64];
        unsigned long show_time;
        bool is_error;           // true=error (highlighted), false=status
        bool active;
    };
    
    MessageState popup_message;
    
    // Timing tasks
    TimedTask page_cycle_task;

    // Boot splash state - see begin()/update() and OLED_BOOT_LOGO_DURATION_MS
    unsigned long boot_started_ms = 0;
    bool boot_logo_done = false;

    // Status snapshot (cached from update)
    SystemStatus current_status;

    // Ecos IP to display - see setEcosIp()
    char ecos_ip[16];
    
    // Helper methods
    void drawMainScreen();
    void drawDeviceStatusScreen();
    void drawXpressNetScreen();
    void drawEcosScreen();
    void drawZ21Screen();
    void drawErrorPopup();
    void drawBootLogo();

    /**
     * Build a comma-separated list of active function numbers (e.g.
     * "0,3,7") into buf, truncated with "..." if it would overflow
     * buf_size - real layouts rarely have more than a handful of
     * functions on at once, so this stays short in practice. Writes
     * "none" if functions == 0. Always null-terminates within buf_size.
     */
    static void buildActiveFunctionsLabel(uint32_t functions, char* buf, size_t buf_size);
    
    // Header icons - hand-drawn shapes only, never font glyphs (the SSD1306
    // default font has no Unicode checkmark/X glyphs; confirmed to render
    // as garbage on real hardware). drawHeaderIcons() draws only the WiFi
    // signal icon (infrastructure, global - every page); drawConnectionIcon()
    // is called directly by each interface's own screen function instead, so
    // it only appears on that interface's own page - the pattern for future
    // interfaces (LocoNet, Z21) once there's no room to show all of them.
    void drawHeaderIcons();
    void drawConnectionIcon(int x, int y, ComponentStatus status);
    void drawWifiSignalIcon(int x, int y, int rssi_dbm);

    // Compact page-position dots, replacing the old "[Page X/4]" footer text
    void drawPageIndicator();
    
    /**
     * Check if popup should be cleared
     * Returns true if message has expired
     */
    bool shouldClearPopup() const;
    
    /**
     * Cycle to next page in rotation
     */
    void cycleNextPage();
};

#endif  // OLED_DISPLAY_H