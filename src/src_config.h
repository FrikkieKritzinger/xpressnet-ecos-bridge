/*
 * XpressNet-Ecos Bridge - Master Configuration File
 * 
 * This is the "God" config file - all hardware selection and feature toggles here.
 * This is the ONLY file users should need to modify for their hardware setup.
 * 
 * Compile-time configuration via preprocessor directives.
 * Features not enabled here are compiled out entirely (zero runtime overhead).
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// PROTOCOL ENABLEMENT (Compile-time toggles)
// ============================================================================
// Set 1 to enable, 0 to disable. Disabled features = no code, no overhead.

#define ENABLE_XPRESSNET        1    // XpressNet interface (hardwired, timing-critical)
#define ENABLE_ECOS_LAN         1    // Ecos LAN interface (WiFi, XML-based)
#define ENABLE_LOCONET          0    // LocoNet interface (future)
#define ENABLE_Z21_LAN          0    // Z21 LAN protocol (future)
#define ENABLE_OLED_DISPLAY     1    // OLED status display

// ============================================================================
// XPRESSNET CONFIGURATION
// ============================================================================
// XpressNet uses non-standard 9+1 bit serial protocol
// Requires MAX485 RS485 interface module

#if ENABLE_XPRESSNET
    // Pin assignments (Wemos D1 Mini pin names)
    #define XPRESSNET_RX_PIN        13   // D7 - Serial1 RX input
    #define XPRESSNET_TX_PIN        15   // D8 - Serial1 TX output
    #define XPRESSNET_DE_PIN        14   // D5 - Driver Enable (HIGH=TX, LOW=RX)
    #define XPRESSNET_RE_PIN        12   // D6 - Receiver Enable (LOW=enabled)
    
    // Serial configuration
    #define XPRESSNET_BAUD          9600 // XpressNet standard baudrate (DO NOT CHANGE)
    
    // Timing and lifecycle
    #define XPRESSNET_TIMEOUT       300000  // 5 minutes - remove inactive locos from state engine
    #define XPRESSNET_POLL_INTERVAL 20     // ms - how often to check for new messages
#endif

// ============================================================================
// ECOS LAN CONFIGURATION
// ============================================================================
// ESU Ecos command system - communicates via XML over TCP/WiFi
// Requires WiFi connectivity

#if ENABLE_ECOS_LAN
    // Network configuration
    #define ECOS_IP                 "192.168.1.100"  // IP address of your Ecos
    #define ECOS_PORT               15471            // Standard Ecos port (do not change)
    
    // WiFi configuration (if not already configured)
    // Note: Could also be loaded from EEPROM, but for now hardcoded
    #define WIFI_SSID               "YourWiFiNetwork"
    #define WIFI_PASSWORD           "YourPassword"
    
    // Timeouts
    #define ECOS_TIMEOUT            5000            // TCP connection timeout (ms)
    #define ECOS_HEARTBEAT_INTERVAL 30000           // Heartbeat to Ecos (ms)
    #define ECOS_RECONNECT_INTERVAL 10000           // Try reconnect every X ms
#endif

// ============================================================================
// LOCONET CONFIGURATION (Future)
// ============================================================================

#if ENABLE_LOCONET
    // TODO: Define LocoNet pins and configuration when implementing
    #define LOCONET_RX_PIN          0    // Placeholder
    #define LOCONET_TX_PIN          0    // Placeholder
#endif

// ============================================================================
// Z21 LAN CONFIGURATION (Future)
// ============================================================================

#if ENABLE_Z21_LAN
    // TODO: Define Z21 configuration when implementing
    #define Z21_PORT                21105  // Placeholder
#endif

// ============================================================================
// OLED DISPLAY CONFIGURATION
// ============================================================================
// SSD1306 I2C OLED display - shows status and information

#if ENABLE_OLED_DISPLAY
    // I2C pins (standard for Wemos D1 Mini)
    #define OLED_SDA_PIN            4     // D2 - I2C Data
    #define OLED_SCL_PIN            5     // D1 - I2C Clock
    
    // Display specifications
    #define OLED_ADDRESS            0x3C  // I2C address (0x3C or 0x3D)
    #define OLED_WIDTH              128   // pixels
    #define OLED_HEIGHT             64    // pixels (0.96" display)
    
    // Timing
    #define OLED_UPDATE_INTERVAL    500   // ms - how often to refresh display
#endif

// ============================================================================
// STATE ENGINE CONFIGURATION
// ============================================================================
// In-memory locomotive state tracking (no persistence)

#define MAX_LOCOS                   50    // Maximum locomotives in state engine
#define LOCO_INACTIVITY_TIMEOUT     300000  // 5 minutes - remove if no updates
#define LOCO_EXPIRY_CHECK_INTERVAL  30000   // Check for expired locos every X ms

// ============================================================================
// ECHO PREVENTION CONFIGURATION
// ============================================================================
// Prevents command loops when updates come from Ecos and bounce back to XpressNet

#define ECHO_PREVENTION_WINDOW      500   // ms - ignore echoed commands within this window

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
// Enable/disable serial debug output

#define ENABLE_DEBUG                1     // Master debug enable
#define DEBUG_BAUD                  115200  // Serial monitor speed

// Specific debug output (only if ENABLE_DEBUG is 1)
#define DEBUG_STARTUP               1     // Show startup messages
#define DEBUG_XPRESSNET             1     // Show XpressNet messages
#define DEBUG_ECOS                  1     // Show Ecos TCP messages
#define DEBUG_STATE_ENGINE          1     // Show state engine operations
#define DEBUG_ECHO_PREVENTION       1     // Show echo prevention decisions
#define DEBUG_TIMING                0     // Show loop timing (verbose)
#define DEBUG_MEMORY                0     // Show heap usage (verbose)

// ============================================================================
// ADVANCED CONFIGURATION (Usually don't change these)
// ============================================================================

// Timing thresholds
#define MAIN_LOOP_TIMEOUT           500   // ms - prevent watchdog reset
#define WATCHDOG_RESET              yield()  // ESP8266 specific

// Memory settings
#define MAX_TCP_BUFFER_SIZE         1024  // bytes - Ecos message buffer
#define MAX_XNET_MESSAGE_LENGTH     32    // bytes - XpressNet message

// Feature flags for future expansion
#define ENABLE_EEPROM_CONFIG        0     // Save config to EEPROM (future)
#define ENABLE_WEB_CONFIG           0     // Web-based configuration (future)
#define ENABLE_OTA_UPDATE           0     // Over-the-air firmware updates (future)

// ============================================================================
// VALIDATION - Don't modify below this line
// ============================================================================

#if !ENABLE_XPRESSNET && !ENABLE_ECOS_LAN && !ENABLE_LOCONET && !ENABLE_Z21_LAN
    #error "At least one protocol must be enabled!"
#endif

#if ENABLE_XPRESSNET && !ENABLE_OLED_DISPLAY
    #warning "XpressNet without display - consider adding OLED for status feedback"
#endif

#endif  // CONFIG_H
