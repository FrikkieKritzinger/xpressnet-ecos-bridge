/*
 * OLED Display Driver - SSD1306 I2C 128x64
 * 
 * Features:
 * - 4-page rotation (Main, Device, XpressNet, Ecos)
 * - Auto-cycle every 5 seconds
 * - Error popup system (auto-dismiss after 3 seconds)
 * - Connection status indicators
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

private:
    // Hardware objects
    Adafruit_SSD1306 display;
    
    // Display state
    enum DisplayPage {
        PAGE_MAIN = 0,
        PAGE_DEVICE = 1,
        PAGE_XNET = 2,
        PAGE_ECOS = 3,
        PAGE_COUNT = 4
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
    
    // Status snapshot (cached from update)
    SystemStatus current_status;
    
    // Helper methods
    void drawMainScreen();
    void drawDeviceStatusScreen();
    void drawXpressNetScreen();
    void drawEcosScreen();
    void drawErrorPopup();
    
    void drawStatusIcon(int x, int y, ComponentStatus status);
	void drawStatusText(int x, int y, ComponentStatus status);
    
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