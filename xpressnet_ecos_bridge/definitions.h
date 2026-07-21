/*
 * Global Type Definitions and Constants
 * 
 * Central location for all custom types, enums, and constants used across the project.
 * Included by most other files.
 */

#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#include <cstdint>
#include <cstring>
#include <Arduino.h>
#include "config.h"

// ============================================================================
// ENUMERATION TYPES
// ============================================================================

/**
 * Locomotive command source - which protocol did this command come from?
 * Used for echo prevention and debugging
 */
enum class LocoSource {
    UNKNOWN = 0,
    XPRESSNET = 1,
    ECOS = 2,
    LOCONET = 3,       // Future
    Z21_LAN = 4        // Future
};

/**
 * System component status
 * Used for display and diagnostics
 */
enum class ComponentStatus {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    ERROR = 3
};

// ============================================================================
// LOCOMOTIVE STATE STRUCTURE
// ============================================================================

/**
 * LocoState - Complete state of a controlled locomotive
 * Stored in state engine, updated from both XpressNet and Ecos
 * 
 * Size: ~30 bytes (48 with padding)
 * Max 50 locos = ~1.5KB RAM
 */
struct LocoState {
    // Identity
    uint16_t dcc_address;           // DCC address (0-9999), short or long
    
    // Control state
    uint8_t speed;                  // Speed: 0-126 (0 = stop)
    uint8_t direction;              // Direction: 0 = reverse, 1 = forward
    uint32_t functions;             // Function bits F0-F31 (1 bit each)
    
    // Metadata
    LocoSource last_source;         // Where last command came from
    unsigned long last_update_ms;   // Timestamp of last update (for expiry)
    bool subscribed_to_ecos;        // Is Ecos sending us updates for this loco?
    bool unknown;                   // Placeholder for unknown locos (future)
    
    // Comparison operator for searching
    bool operator==(uint16_t addr) const {
        return dcc_address == addr;
    }
};

// ============================================================================
// COMMAND MESSAGE STRUCTURES
// ============================================================================

/**
 * Generic command message - used internally for routing
 * Different protocols parse their formats into this structure
 */
struct CommandMessage {
    enum Type {
        SPEED = 1,
        DIRECTION = 2,
        FUNCTION = 3,
        EMERGENCY_STOP = 4
    };
    
    uint16_t loco_address;          // Target locomotive
    Type command_type;              // What kind of command
    uint8_t speed;                  // For SPEED commands
    uint8_t direction;              // For DIRECTION commands
    uint32_t function_state;        // For FUNCTION commands (bitmap)
    LocoSource source;              // Which protocol sent this
};

/**
 * System status snapshot
 * Used for display and status reporting
 */
struct SystemStatus {
    // Component status
    ComponentStatus xnet_status;
    ComponentStatus ecos_status;
    
    // Session info
    int active_locos;               // Number of locos in state engine
    uint8_t xnet_device_count;      // Number of XpressNet devices seen
    
    // Network info
    int wifi_rssi;                  // WiFi signal strength (-100 to 0 dBm)
    unsigned long uptime_ms;        // Time since boot
    
    // Diagnostic
    uint32_t total_commands;        // Lifetime command count
    uint32_t current_heap_bytes;    // Available heap (changed to uint32_t for larger values)
};

// ============================================================================
// CONSTANTS - TIMING
// ============================================================================

// These are defined in config.h but referenced here for completeness
// All times in milliseconds

// Don't add constants here - keep them in config.h
// This file is for TYPES only

// ============================================================================
// CONSTANTS - DCC STANDARDS
// ============================================================================

// DCC protocol constants (non-configurable)
#define DCC_MIN_ADDRESS             1
#define DCC_MAX_ADDRESS             9999
#define DCC_MIN_SPEED               0
#define DCC_MAX_SPEED               126     // 127 is emergency stop
#define DCC_FUNCTION_COUNT          32      // F0 through F31

// XpressNet specific
#define XPRESSNET_MAX_DEVICES       31      // 1 master + 30 slaves
#define XPRESSNET_MAX_LOCOS         10000   // Theory limit

// State engine limits
#define STATE_ENGINE_MAX_LOCOS      MAX_LOCOS  // From config.h

// ============================================================================
// INLINE UTILITY FUNCTIONS
// ============================================================================

/**
 * Check if a DCC address is valid
 */
inline bool isValidDccAddress(uint16_t address) {
    return (address >= DCC_MIN_ADDRESS && address <= DCC_MAX_ADDRESS);
}

/**
 * Check if a speed value is valid
 */
inline bool isValidSpeed(uint8_t speed) {
    return (speed <= DCC_MAX_SPEED);
}

/**
 * Check if a direction is valid
 */
inline bool isValidDirection(uint8_t direction) {
    return (direction == 0 || direction == 1);
}

/**
 * Convert LocoSource enum to string for debugging
 */
inline const char* locoSourceToString(LocoSource source) {
    switch (source) {
        case LocoSource::XPRESSNET: return "XpressNet";
        case LocoSource::ECOS:      return "Ecos";
        case LocoSource::LOCONET:   return "LocoNet";
        case LocoSource::Z21_LAN:   return "Z21";
        default:                     return "Unknown";
    }
}

/**
 * Convert ComponentStatus enum to string
 */
inline const char* statusToString(ComponentStatus status) {
    switch (status) {
        case ComponentStatus::DISCONNECTED: return "Disconnected";
        case ComponentStatus::CONNECTING:   return "Connecting";
        case ComponentStatus::CONNECTED:    return "Connected";
        case ComponentStatus::ERROR:        return "Error";
        default:                            return "Unknown";
    }
}

#endif  // DEFINITIONS_H
