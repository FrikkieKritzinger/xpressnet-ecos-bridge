/*
 * Command Router Implementation - Protocol Bridge
 * 
 * Routes commands between protocols:
 * - XpressNet → Ecos
 * - Ecos → XpressNet
 * - Future: LocoNet, Z21
 * 
 * Implements echo prevention to avoid command loops
 * Manages locomotive subscriptions with Ecos
 * Tracks system status for display
 */

#include <cstdint>
#include <Arduino.h>
#include "command_router.h"
#include "utils/debug.h"
#include "utils/timing.h"
#include "utils/memory.h"
#include "utils/now_ms.h"

// WiFi.RSSI() for SystemStatus - real core only, native tests have no WiFi stack
#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#endif

// ============================================================================
// CONSTRUCTOR
// ============================================================================

CommandRouter::CommandRouter() {
    /*
     * Initialize router with empty state engine
     * Protocol interfaces are set later via setters
     */
    
    DEBUG_PRINT("\nCommandRouter initialized\n");
}

// ============================================================================
// SET PROTOCOL INTERFACES
// ============================================================================

#if ENABLE_XPRESSNET
void CommandRouter::setXpressNetInterface(ProtocolInterface* xnet) {
    /*
     * Register XpressNet interface after it's created
     * Called from main.ino during setup
     */
    xpressnet = xnet;
    DEBUG_PRINT("XpressNet interface registered with router\n");
}
#endif

#if ENABLE_ECOS_LAN
void CommandRouter::setEcosInterface(ProtocolInterface* ecos_intf) {
    /*
     * Register Ecos interface after it's created
     * Called from main.ino during setup
     */
    ecos = ecos_intf;
    DEBUG_PRINT("Ecos interface registered with router\n");
}
#endif

// ============================================================================
// HANDLE XPRESSNET COMMANDS
// ============================================================================

void CommandRouter::handleXpressNetCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    /*
     * Process speed/direction command from XpressNet
     * 
     * Flow:
     * 1. Create/update locomotive in state engine
     * 2. Check if unknown loco (needs Ecos subscription)
     * 3. Check echo prevention (is this echoed from Ecos?)
     * 4. Broadcast to other protocols (Ecos)
     * 5. Update echo prevention state
     */
    
    DEBUG_XNET_PRINTF("XpressNet: Loco %u Speed %u Dir %u\n", address, speed, direction);
    
    // Validate inputs
    if (!isValidDccAddress(address)) {
        DEBUG_XNET_PRINTF("ERROR: Invalid XpressNet address: %u\n", address);
        return;
    }
    if (!isValidSpeed(speed) || !isValidDirection(direction)) {
        DEBUG_XNET_PRINTF("ERROR: Invalid speed (%u) or direction (%u)\n", speed, direction);
        return;
    }
    
    // Check echo prevention BEFORE adding to state engine
    if (isEchoCommand(address, LocoSource::XPRESSNET)) {
        DEBUG_ECHO_PRINTF("Echo suppressed: Loco %u speed command (from Ecos)\n", address);
        echo_prevented_count++;
        return;
    }

    // Create/update locomotive state
    LocoState new_state;
    new_state.dcc_address = address;
    new_state.speed = speed;
    new_state.direction = direction;
    new_state.functions = 0;  // Preserve existing functions
    new_state.last_source = LocoSource::XPRESSNET;

    // If locomotive exists, preserve functions and subscription state
    LocoState existing;
    bool already_subscribed = false;
    if (state_engine.getLoco(address, existing)) {
        new_state.functions = existing.functions;  // Keep existing function states
        already_subscribed = existing.subscribed_to_ecos;
    }
    // Mark subscribed as soon as we ask, not only once Ecos confirms -
    // subscribeToLoco() is a fire-and-forget TCP query with no clean way to
    // correlate Ecos's async reply back to this specific request. Real bug
    // found on hardware 2026-07-31: this flag was previously only ever set
    // true by the Ecos-initiated path, so a loco discovered via XpressNet
    // re-requested a subscription on every single subsequent update forever.
    new_state.subscribed_to_ecos = true;

    // Add or update in state engine
    if (!state_engine.addOrUpdateLoco(address, new_state)) {
        DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u (state engine full?)\n", address);
        return;
    }

    // If this is new loco, request Ecos subscription
    if (!already_subscribed) {
        DEBUG_STATE_PRINTF("New loco from XpressNet: requesting Ecos subscription\n");
        requestEcosSubscription(address);
    }
    
    // Broadcast to other protocols
    broadcastCommand(address, new_state, LocoSource::XPRESSNET);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = speed;
    last_command.direction = direction;
    last_command.source = LocoSource::XPRESSNET;

    // Update echo prevention state
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 0;  // SPEED command type
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::XPRESSNET;
}

// ============================================================================
// HANDLE XPRESSNET FUNCTION COMMANDS
// ============================================================================

void CommandRouter::handleXpressNetFunctionCommand(uint16_t address, uint32_t functions) {
    /*
     * Process function state command from XpressNet
     * 
     * Functions are F0-F31 (32-bit bitmap)
     */
    
    DEBUG_XNET_PRINTF("XpressNet: Loco %u Functions 0x%08x\n", address, functions);
    
    // Validate address
    if (!isValidDccAddress(address)) {
        DEBUG_XNET_PRINTF("ERROR: Invalid XpressNet address: %u\n", address);
        return;
    }
    
    // Check echo prevention
    if (isEchoCommand(address, LocoSource::XPRESSNET)) {
        DEBUG_ECHO_PRINTF("Echo suppressed: Loco %u function command (from Ecos)\n", address);
        echo_prevented_count++;
        return;
    }

    // Get existing state or create new
    LocoState new_state;
    if (!state_engine.getLoco(address, new_state)) {
        // New locomotive - create entry
        new_state.dcc_address = address;
        new_state.speed = 0;
        new_state.direction = 1;
        new_state.functions = functions;
        new_state.subscribed_to_ecos = true;  // mark now; see handleXpressNetCommand for why

        if (!state_engine.addOrUpdateLoco(address, new_state)) {
            DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u\n", address);
            return;
        }

        // Request subscription for new loco
        requestEcosSubscription(address);
    } else {
        // Update existing loco
        new_state.functions = functions;
        new_state.last_source = LocoSource::XPRESSNET;
        state_engine.addOrUpdateLoco(address, new_state);
    }
    
    // Broadcast to other protocols
    broadcastCommand(address, new_state, LocoSource::XPRESSNET);

    total_commands_count++;

    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 1;  // FUNCTION command type
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::XPRESSNET;
}

// ============================================================================
// HANDLE ECOS COMMANDS
// ============================================================================

void CommandRouter::handleEcosCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    /*
     * Process speed/direction update from Ecos
     * 
     * Flow:
     * 1. Create/update locomotive in state engine
     * 2. Check echo prevention (is this echoed from XpressNet?)
     * 3. Broadcast to XpressNet
     * 4. Update echo prevention state
     */
    
    DEBUG_ECOS_PRINTF("Ecos: Loco %u Speed %u Dir %u\n", address, speed, direction);
    
    // Validate
    if (!isValidDccAddress(address)) {
        DEBUG_ECOS_PRINTF("ERROR: Invalid Ecos address: %u\n", address);
        return;
    }
    if (!isValidSpeed(speed) || !isValidDirection(direction)) {
        DEBUG_ECOS_PRINTF("ERROR: Invalid speed (%u) or direction (%u)\n", speed, direction);
        return;
    }
    
    // Check echo prevention BEFORE updating
    if (isEchoCommand(address, LocoSource::ECOS)) {
        DEBUG_ECHO_PRINTF("Echo suppressed: Loco %u speed command (from XpressNet)\n", address);
        echo_prevented_count++;
        return;
    }

    // Get existing state or create new
    LocoState new_state;
    if (!state_engine.getLoco(address, new_state)) {
        // New from Ecos
        new_state.dcc_address = address;
        new_state.speed = speed;
        new_state.direction = direction;
        new_state.functions = 0;
        new_state.subscribed_to_ecos = true;
        
        if (!state_engine.addOrUpdateLoco(address, new_state)) {
            DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u\n", address);
            return;
        }
        
        DEBUG_STATE_PRINTF("New loco from Ecos: %u (marked subscribed)\n", address);
    } else {
        // Update existing
        new_state.speed = speed;
        new_state.direction = direction;
        new_state.subscribed_to_ecos = true;
        new_state.last_source = LocoSource::ECOS;
        state_engine.addOrUpdateLoco(address, new_state);
    }
    
    // Broadcast to XpressNet (if enabled)
    broadcastCommand(address, new_state, LocoSource::ECOS);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = speed;
    last_command.direction = direction;
    last_command.source = LocoSource::ECOS;

    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 0;  // SPEED
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::ECOS;
}

void CommandRouter::handleEcosFunctionCommand(uint16_t address, uint32_t functions) {
    /*
     * Process function state update from Ecos
     */
    
    DEBUG_ECOS_PRINTF("Ecos: Loco %u Functions 0x%08x\n", address, functions);
    
    // Validate
    if (!isValidDccAddress(address)) {
        DEBUG_ECOS_PRINTF("ERROR: Invalid Ecos address: %u\n", address);
        return;
    }
    
    // Check echo prevention
    if (isEchoCommand(address, LocoSource::ECOS)) {
        DEBUG_ECHO_PRINTF("Echo suppressed: Loco %u function command (from XpressNet)\n", address);
        echo_prevented_count++;
        return;
    }

    // Get existing or create
    LocoState new_state;
    if (!state_engine.getLoco(address, new_state)) {
        new_state.dcc_address = address;
        new_state.speed = 0;
        new_state.direction = 1;
        new_state.functions = functions;
        new_state.subscribed_to_ecos = true;
        
        if (!state_engine.addOrUpdateLoco(address, new_state)) {
            DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u\n", address);
            return;
        }
    } else {
        new_state.functions = functions;
        new_state.subscribed_to_ecos = true;
        new_state.last_source = LocoSource::ECOS;
        state_engine.addOrUpdateLoco(address, new_state);
    }
    
    // Broadcast to XpressNet
    broadcastCommand(address, new_state, LocoSource::ECOS);

    total_commands_count++;

    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 1;  // FUNCTION
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::ECOS;
}

// ============================================================================
// EMERGENCY STOP / RESUME
// ============================================================================

void CommandRouter::emergencyStopAll() {
    int count = state_engine.getLocoCount();
    DEBUG_STATE_PRINTF("Emergency stop: forcing %d loco(s) to speed 0\n", count);

    for (int i = 0; i < count; i++) {
        LocoState* loco = state_engine.getLocoByIndex(i);
        if (!loco) {
            continue;
        }

        loco->speed = 0;

        #if ENABLE_XPRESSNET
        if (xpressnet != nullptr) {
            xpressnet->sendSpeedCommand(loco->dcc_address, 0, loco->direction);
        }
        #endif
    }

    // Ecos gets one real system-wide stop command, not a per-loco loop -
    // "equivalent to the STOP button on the Ecos" per the official spec.
    #if ENABLE_ECOS_LAN
    if (ecos != nullptr) {
        ecos->sendEmergencyStop();
    }
    #endif
}

void CommandRouter::resumeOperation() {
    DEBUG_STATE_PRINT("Resume operation requested\n");

    // Deliberately NOT restoring any loco's previous speed here - matches
    // real command-station safety behavior, the operator must re-throttle
    // manually after a stop.
    #if ENABLE_ECOS_LAN
    if (ecos != nullptr) {
        ecos->sendResumeOperation();
    }
    #endif
}

// ============================================================================
// PERIODIC HOUSEKEEPING
// ============================================================================

void CommandRouter::update() {
    /*
     * Called periodically from main loop (every 30 seconds)
     *
     * Responsibilities:
     * - Expunge inactive locos
     * - Unsubscribe from Ecos for removed locos
     * - Update status
     * - Log diagnostics
     */

    // Expunge locos inactive for 5 minutes, capturing which ones were removed
    uint16_t removed_addresses[MAX_LOCOS];
    int removed = state_engine.expungeInactiveLocos(removed_addresses, MAX_LOCOS);

    if (removed > 0) {
        DEBUG_STATE_PRINTF("Expunged %d inactive locomotives\n", removed);

        // Unsubscribe from Ecos for each removed loco
        #if ENABLE_ECOS_LAN
        if (ecos != nullptr) {
            for (int i = 0; i < removed; i++) {
                ecos->unsubscribeFromLoco(removed_addresses[i]);
            }
        }
        #endif
    }

    // Log status (if debug enabled)
    if (last_status_update == 0 || now_ms() - last_status_update > 60000) {
        last_status_update = now_ms();

        SystemStatus status = getSystemStatus();
        DEBUG_STATE_PRINTF("Status: XNet=%s Ecos=%s Active=%d\n",
                          statusToString(status.xnet_status),
                          statusToString(status.ecos_status),
                          status.active_locos);
    }
}

// ============================================================================
// PRIVATE HELPER FUNCTIONS
// ============================================================================

bool CommandRouter::isEchoCommand(uint16_t address, LocoSource source) const {
    /*
     * Check if incoming command is an echo of recent outgoing command
     * 
     * Echo scenario:
     * 1. XpressNet sends speed command for loco 100
     * 2. We send to Ecos
     * 3. Ecos sends back update for loco 100
     * 4. We check: is this an echo?
     *    - Same address? YES
     * - Same source (direction)? NO (came from Ecos, not XpressNet)
     *    - Within 500ms? YES
     *    - Result: SUPPRESS (it's an echo)
     * 
     * We want to prevent sending back to the originating protocol
     * within the 500ms window
     */
    
    unsigned long now = now_ms();
    unsigned long time_since_last = now - echo_state.last_timestamp_ms;
    
    // Not an echo if more than 500ms has passed
    if (time_since_last > ECHO_PREVENTION_WINDOW) {
        return false;
    }
    
    // Not an echo if different loco
    if (address != echo_state.last_loco_address) {
        return false;
    }
    
    // This is an echo if:
    // - Same loco
    // - Same source (the source this came from is the source we last sent to)
    // - Within 500ms window
    
    // If last update was from XpressNet and this is also XpressNet, it's not an echo
    // (it's a separate XpressNet command)
    if (echo_state.last_source == source) {
        return false;  // Same source, not an echo
    }
    
    // Different source, same loco, within window = ECHO
    return true;
}

void CommandRouter::broadcastCommand(uint16_t address, const LocoState& state, LocoSource source) {
    /*
     * Send command to all protocols except the source
     * 
     * If came from XpressNet, send to Ecos
     * If came from Ecos, send to XpressNet
     */
    
    if (source == LocoSource::XPRESSNET) {
        // Send to Ecos
        #if ENABLE_ECOS_LAN
        if (ecos != nullptr) {
            DEBUG_PRINTF("Broadcasting XpressNet command to Ecos\n");
            ecos->sendSpeedCommand(address, state.speed, state.direction);
            ecos->sendFunctionCommand(address, state.functions);
        }
        #endif
    }
    else if (source == LocoSource::ECOS) {
        // Send to XpressNet
        #if ENABLE_XPRESSNET
        if (xpressnet != nullptr) {
            DEBUG_PRINTF("Broadcasting Ecos command to XpressNet\n");
            xpressnet->sendSpeedCommand(address, state.speed, state.direction);
            xpressnet->sendFunctionCommand(address, state.functions);
        }
        #endif
    }
}

void CommandRouter::requestEcosSubscription(uint16_t address) {
    /*
     * Request Ecos to start sending updates for locomotive
     * Called when XpressNet sends command for unknown loco
     */

    #if ENABLE_ECOS_LAN
    if (ecos != nullptr) {
        DEBUG_STATE_PRINTF("Requesting Ecos subscription for loco %u\n", address);
        ecos->subscribeToLoco(address);
    }
    #endif
}

// ============================================================================
// GET SYSTEM STATUS
// ============================================================================

SystemStatus CommandRouter::getSystemStatus() const {
    /*
     * Gather current system status for display
     * 
     * Returns: SystemStatus struct with all metrics
     */
    
    SystemStatus status;
    
    // Protocol status
    #if ENABLE_XPRESSNET
    status.xnet_status = (xpressnet != nullptr) ? 
                         xpressnet->getStatus() : 
                         ComponentStatus::DISCONNECTED;
    #else
    status.xnet_status = ComponentStatus::DISCONNECTED;
    #endif
    
    #if ENABLE_ECOS_LAN
    status.ecos_status = (ecos != nullptr) ? 
                         ecos->getStatus() : 
                         ComponentStatus::DISCONNECTED;
    #else
    status.ecos_status = ComponentStatus::DISCONNECTED;
    #endif
    
    // State engine
    status.active_locos = state_engine.getLocoCount();

    // WiFi signal strength
    #ifdef ARDUINO_ARCH_ESP8266
        status.wifi_rssi = WiFi.RSSI();
    #else
        status.wifi_rssi = 0;
    #endif

    // Uptime
    status.uptime_ms = now_ms();

    // Diagnostics
    status.total_commands = total_commands_count;
    status.echo_prevented_count = echo_prevented_count;
    status.current_heap_bytes = ESP.getFreeHeap();

    // Last drive command (for display)
    status.last_command_address = last_command.address;
    status.last_command_speed = last_command.speed;
    status.last_command_direction = last_command.direction;
    status.last_command_source = last_command.source;

    return status;
}

// ============================================================================
// DEBUG FUNCTIONS
// ============================================================================

#if ENABLE_DEBUG

void CommandRouter::debugPrintEchoState() const {
    /*
     * Print current echo prevention state
     */
    
    DEBUG_ECHO_PRINTF("\n=== Echo Prevention State ===\n");
    DEBUG_ECHO_PRINTF("Last loco:      %u\n", echo_state.last_loco_address);
    DEBUG_ECHO_PRINTF("Last command:   %u\n", echo_state.last_command_type);
    DEBUG_ECHO_PRINTF("Last source:    %s\n", locoSourceToString(echo_state.last_source));
    
    unsigned long age = now_ms() - echo_state.last_timestamp_ms;
    DEBUG_ECHO_PRINTF("Age:            %lu ms\n", age);
    DEBUG_ECHO_PRINTF("Window:         %lu ms\n", (unsigned long)ECHO_PREVENTION_WINDOW);
    
    if (age < ECHO_PREVENTION_WINDOW) {
        DEBUG_ECHO_PRINTF("Status:         ACTIVE (echoes will be suppressed)\n");
    } else {
        DEBUG_ECHO_PRINTF("Status:         INACTIVE (echoes not suppressed)\n");
    }
    
    DEBUG_ECHO_PRINT("=============================\n\n");
}

#endif  // ENABLE_DEBUG
