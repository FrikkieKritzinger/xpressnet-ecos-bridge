/*
 * Abstract Interface Base Classes
 * 
 * Defines the interface contracts that all protocol implementations must follow.
 * This allows clean modularity - protocols can be added/removed without touching core code.
 * 
 * Pattern: Template Method / Strategy pattern
 * Each concrete implementation (XpressNet, Ecos, etc.) inherits and implements these.
 */

#ifndef INTERFACE_BASE_H
#define INTERFACE_BASE_H

#include "../config.h"
#include "../definitions.h"

// ============================================================================
// PROTOCOL INTERFACE BASE CLASS
// ============================================================================

/**
 * ProtocolInterface - Abstract base class for all protocol implementations
 * 
 * Each protocol (XpressNet, Ecos, LocoNet, Z21) implements this interface.
 * Allows main loop to treat all protocols the same way.
 */
class ProtocolInterface {
public:
    virtual ~ProtocolInterface() = default;
    
    /**
     * Initialize the protocol interface
     * Called once at startup
     * @return true if successful, false if hardware not available
     */
    virtual bool begin() = 0;
    
    /**
     * Non-blocking update - process any pending communication
     * Called every main loop iteration
     * Should never block the caller
     * 
     * Responsibilities:
     * - Read any available incoming data
     * - Parse complete messages when available
     * - Call router->handle*Command() for new commands
     * - Handle network/bus events (connect/disconnect)
     */
    virtual void update() = 0;
    
    /**
     * Send speed/direction command
     * Protocol-specific formatting handled by subclass
     * May queue internally if connection not available
     * 
     * @param address DCC locomotive address
     * @param speed 0-126
     * @param direction 0=reverse, 1=forward
     */
    virtual void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) = 0;
    
    /**
     * Send function state(s)
     * @param address DCC locomotive address
     * @param functions Bitmap of F0-F31 states
     */
    virtual void sendFunctionCommand(uint16_t address, uint32_t functions) = 0;
    
    /**
     * Get current protocol status
     * @return ComponentStatus (CONNECTED, DISCONNECTED, ERROR, etc.)
     */
    virtual ComponentStatus getStatus() const = 0;
    
    /**
     * Get human-readable protocol name (for display)
     * @return string like "XpressNet", "Ecos", "LocoNet"
     */
    virtual const char* getName() const = 0;
};

// ============================================================================
// DISPLAY INTERFACE BASE CLASS
// ============================================================================

/**
 * DisplayInterface - Abstract base class for all display implementations
 * 
 * Supports different displays (OLED, LCD, serial, web, etc.)
 * Main loop doesn't know about specific display - just calls interface methods
 */
class DisplayInterface {
public:
    virtual ~DisplayInterface() = default;
    
    /**
     * Initialize display
     * Called once at startup
     * @return true if successful, false if hardware not available
     */
    virtual bool begin() = 0;
    
    /**
     * Update display with current system status
     * Non-blocking - should return quickly
     * Called periodically from main loop
     * 
     * @param status Current system status snapshot
     */
    virtual void update(const SystemStatus& status) = 0;
    
    /**
     * Show a status message (one-off display)
     * E.g., "Connecting to Ecos..."
     * Optional - default implementation does nothing
     * 
     * @param message Brief message to display
     */
    virtual void showMessage(const char* message) {
        // Default: do nothing
        (void)message;
    }
    
    /**
     * Show debug information
     * E.g., memory usage, timing stats
     * Optional - for development/testing
     */
    virtual void showDebugInfo() {
        // Default: do nothing
    }
};

// ============================================================================
// TIMING UTILITY BASE CLASS
// ============================================================================

/**
 * TimedTask - Helper for periodic non-blocking execution
 * 
 * Usage:
 *   TimedTask task(500);  // Every 500ms
 *   if (task.shouldExecute()) {
 *       doSomething();
 *   }
 * 
 * Simpler than millis() comparisons scattered everywhere
 */
class TimedTask {
public:
    /**
     * Create a task that triggers every interval_ms
     * @param interval_ms How often to trigger (milliseconds)
     */
    explicit TimedTask(unsigned long interval_ms)
        : last_execute(0), interval(interval_ms) {}
    
    /**
     * Check if it's time to execute
     * @return true if interval has passed since last call
     */
    bool shouldExecute() {
        unsigned long now = millis();
        if (now - last_execute >= interval) {
            last_execute = now;
            return true;
        }
        return false;
    }
    
    /**
     * Reset the timer to current time
     */
    void reset() {
        last_execute = millis();
    }
    
    /**
     * Change the interval
     * @param new_interval New interval in milliseconds
     */
    void setInterval(unsigned long new_interval) {
        interval = new_interval;
    }

private:
    unsigned long last_execute;  // Last execution timestamp
    unsigned long interval;      // Target interval (ms)
};

#endif  // INTERFACE_BASE_H
