/*
 * OLED Display - Stub for Phase 3 Implementation
 * 
 * This is a skeleton that will be fully implemented in Phase 3.
 * For now, it provides the minimal interface to allow compilation.
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <cstdint>
#include "../interfaces/interface_base.h"
#include "../definitions.h"

class OledDisplay : public DisplayInterface {
public:
    OledDisplay() {}
    
    bool begin() override {
        // TODO: Phase 3 - Initialize I2C OLED display (SSD1306)
        // Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
        // Initialize Adafruit_SSD1306 at OLED_ADDRESS
        return false;
    }
    
    void update(const SystemStatus& status) override {
        // TODO: Phase 3 - Update OLED with current status
        // Display WiFi/Ecos status, locomotive count, etc.
        (void)status;
    }
    
    void showMessage(const char* message) override {
        // TODO: Phase 3 - Display temporary message
        (void)message;
    }
    
    void showDebugInfo() override {
        // TODO: Phase 3 - Display debug information (heap, timing, etc.)
    }
};

#endif  // OLED_DISPLAY_H
