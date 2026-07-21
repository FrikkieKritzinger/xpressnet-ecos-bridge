/*
 * XpressNet-Ecos Bridge - Main Sketch
 * 
 * IMPORTANT: This file MUST be named xpressnet_ecos_bridge.ino (matching the folder name)
 * Arduino IDE requires the main .ino file to match the project folder name.
 * 
 * ESP8266 (Wemos D1 Mini) entry point
 * 
 * Architecture:
 * - Modular protocol interfaces (XpressNet, Ecos, LocoNet, Z21)
 * - Central state engine (in-memory locomotive tracking)
 * - Command router (bridges between protocols)
 * - Display interface (OLED status)
 * 
 * All features enabled/disabled via config.h
 */

#include "config.h"

#include "definitions.h"
#include "state_engine.h"
#include "command_router.h"
#include "interfaces/interface_base.h"

// Protocol interfaces - conditionally included based on config.h
#if ENABLE_XPRESSNET
    #include "protocols/xpressnet/xpressnet_interface.h"
#endif

#if ENABLE_ECOS_LAN
    #include "protocols/ecos/ecos_interface.h"
#endif

#if ENABLE_LOCONET
    #include "protocols/loconet/loconet_interface.h"
#endif

#if ENABLE_Z21_LAN
    #include "protocols/z21lan/z21lan_interface.h"
#endif

// Display interface
#if ENABLE_OLED_DISPLAY
    #include "display/oled_display.h"
#endif

// Utilities
#include "utils/debug.h"
#include "utils/timing.h"

// ============================================================================
// GLOBAL OBJECTS - Static allocation (no dynamic memory)
// ============================================================================

// Command router and state engine
CommandRouter router;

// Protocol interfaces (only created if enabled)
#if ENABLE_XPRESSNET
    XpressNetInterface xnet_interface;
#endif

#if ENABLE_ECOS_LAN
    EcosInterface ecos_interface;
#endif

#if ENABLE_LOCONET
    LocoNetInterface loconet_interface;
#endif

#if ENABLE_Z21_LAN
    Z21LanInterface z21_interface;
#endif

// Display interface
#if ENABLE_OLED_DISPLAY
    OledDisplay display;
#endif

// ============================================================================
// TIMING TASKS
// ============================================================================

// Periodic housekeeping tasks
TimedTask expiry_check_task(LOCO_EXPIRY_CHECK_INTERVAL);
TimedTask status_update_task(5000);     // Every 5 seconds
TimedTask debug_output_task(10000);     // Every 10 seconds (if debug enabled)

// ============================================================================
// ARDUINO SETUP - Called once at startup
// ============================================================================

void setup() {
    // Initialize serial for debug output
    #if ENABLE_DEBUG
        Serial.begin(DEBUG_BAUD);
        delay(100);
        debugPrintf("\n\n=== XpressNet-Ecos Bridge Starting ===\n");
        debugPrintf("Version: 0.1-dev\n");
        debugPrintf("Build: %s %s\n", __DATE__, __TIME__);
    #endif
    
    // Initialize protocols (order doesn't matter)
    
    #if ENABLE_XPRESSNET
        debugPrintf("Initializing XpressNet interface...\n");
        if (!xnet_interface.begin()) {
            debugPrintf("ERROR: XpressNet initialization failed!\n");
        } else {
            debugPrintf("XpressNet interface initialized\n");
            router.setXpressNetInterface(&xnet_interface);
        }
    #endif
    
    #if ENABLE_ECOS_LAN
        debugPrintf("Initializing Ecos LAN interface...\n");
        if (!ecos_interface.begin()) {
            debugPrintf("ERROR: Ecos LAN initialization failed!\n");
        } else {
            debugPrintf("Ecos LAN interface initialized\n");
            router.setEcosInterface(&ecos_interface);
        }
    #endif
    
    #if ENABLE_LOCONET
        debugPrintf("Initializing LocoNet interface...\n");
        if (!loconet_interface.begin()) {
            debugPrintf("ERROR: LocoNet initialization failed!\n");
        } else {
            debugPrintf("LocoNet interface initialized\n");
        }
    #endif
    
    #if ENABLE_Z21_LAN
        debugPrintf("Initializing Z21 LAN interface...\n");
        if (!z21_interface.begin()) {
            debugPrintf("ERROR: Z21 LAN initialization failed!\n");
        } else {
            debugPrintf("Z21 LAN interface initialized\n");
        }
    #endif
    
    // Initialize display
    #if ENABLE_OLED_DISPLAY
        debugPrintf("Initializing OLED display...\n");
        if (!display.begin()) {
            debugPrintf("ERROR: OLED display initialization failed!\n");
        } else {
            debugPrintf("OLED display initialized\n");
        }
    #endif
    
    debugPrintf("=== Setup Complete - Ready to operate ===\n\n");
}

// ============================================================================
// ARDUINO LOOP - Main event loop (runs forever)
// ============================================================================

void loop() {
    // ========================================================================
    // PHASE 1: TIME-CRITICAL OPERATIONS (XpressNet priority)
    // ========================================================================
    // These must run every iteration to avoid missing messages
    
    #if ENABLE_XPRESSNET
        // XpressNet is most timing-sensitive - update first
        // Message receive window is ~20-50ms, must not miss
        xnet_interface.update();
    #endif
    
    // ========================================================================
    // PHASE 2: PROTOCOL COMMUNICATION (other protocols)
    // ========================================================================
    // Read from all enabled protocols
    // These are non-blocking so order doesn't matter
    
    #if ENABLE_ECOS_LAN
        ecos_interface.update();
    #endif
    
    #if ENABLE_LOCONET
        loconet_interface.update();
    #endif
    
    #if ENABLE_Z21_LAN
        z21_interface.update();
    #endif
    
    // ========================================================================
    // PHASE 3: PERIODIC HOUSEKEEPING (low priority)
    // ========================================================================
    // These run on timers, not every loop iteration
    
    // Check for inactive locos (expire after timeout)
    if (expiry_check_task.shouldExecute()) {
        router.update();  // Internal call to StateEngine::expungeInactiveLocos()
        #if ENABLE_DEBUG
            debugPrintf("Expiry check: %d locos active\n", 
                       router.getStateEngine().getLocoCount());
        #endif
    }
    
    // Status update (periodic diagnostics)
    if (status_update_task.shouldExecute()) {
        #if ENABLE_DEBUG
            SystemStatus status = router.getSystemStatus();
            debugPrintf("Status: XNet=%s Ecos=%s Locos=%d\n",
                       statusToString(status.xnet_status),
                       statusToString(status.ecos_status),
                       status.active_locos);
        #endif
    }
    
    // Debug output (very verbose, only if enabled)
    #if ENABLE_DEBUG && DEBUG_TIMING
        if (debug_output_task.shouldExecute()) {
            debugPrintf("Heap: %u bytes\n", ESP.getFreeHeap());
        }
    #endif
    
    // ========================================================================
    // PHASE 4: DISPLAY UPDATE (lowest priority)
    // ========================================================================
    // Display update can wait - it's not time-critical
    
    #if ENABLE_OLED_DISPLAY
        static TimedTask display_update_task(OLED_UPDATE_INTERVAL);
        if (display_update_task.shouldExecute()) {
            SystemStatus status = router.getSystemStatus();
            display.update(status);
        }
    #endif
    
    // ========================================================================
    // PHASE 5: WATCHDOG RESET
    // ========================================================================
    // ESP8266 requires periodic yield() to avoid watchdog timeout
    // Must be called at least every 500ms
    
    yield();  // Let ESP8266 handle background tasks (WiFi, etc.)
}

// ============================================================================
// DEBUG HELPER - Printf-style debug output
// ============================================================================

/**
 * Printf-style debug output (only if ENABLE_DEBUG)
 * Use like: debugPrintf("Value: %d\n", value);
 * 
 * This is a convenience wrapper that calls the macro from utils/debug.h
 */
void debugPrintf(const char* format, ...) {
    #if ENABLE_DEBUG
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.print(buffer);
    #else
        (void)format;  // Avoid unused parameter warning
    #endif
}

// ============================================================================
// STARTUP BANNER
// ============================================================================

/*
Expected startup sequence on Serial Monitor (115200 baud):

=== XpressNet-Ecos Bridge Starting ===
Version: 0.1-dev
Build: Jan 21 2024 14:35:10
Initializing XpressNet interface...
XpressNet interface initialized
Initializing Ecos LAN interface...
Initializing WiFi... (may take 10-15 seconds)
WiFi Connected! IP: 192.168.1.X
Connecting to Ecos at 192.168.1.100:15471...
Ecos connected!
Initializing OLED display...
OLED display initialized
=== Setup Complete - Ready to operate ===

Then continuously:
- XpressNet commands received and processed
- Ecos updates sent/received
- Display updates every 500ms
- Periodic status checks
*/
