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

#include "command_router.h"
#include "utils/debug.h"
#include "utils/timing.h"
#include "utils/memory.h"

// Forward declarations for optional protocol interfaces
#if ENABLE_XPRESSNET
    #include "protocols/xpressnet/xpressnet_interface.h"
#endif

#if ENABLE_ECOS_LAN
    #include "protocols/ecos/ecos_interface.h"
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
void CommandRouter::setXpressNetInterface(XpressNetInterface* xnet) {
    /*
     * Register XpressNet interface after it's created
     * Called from main.ino during setup
     */
    xpressnet = xnet;
    DEBUG_PRINT("XpressNet interface registered with router\n");
}
#endif

#if ENABLE_ECOS_LAN
void CommandRouter::setEcosInterface(EcosInterface* ecos_intf) {
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
        return;
    }
    
    // Create/update locomotive state
    LocoState new_state;
    new_state.dcc_address = address;
    new_state.speed = speed;
    new_state.direction = direction;
    new_state.functions = 0;  // Preserve existing functions
    new_state.last_source = LocoSource::XPRESSNET;
    new_state.subscribed_to_ecos = false;
    
    // If locomotive exists, preserve functions
    LocoState existing;
    if (state_engine.getLoco(address, existing)) {
        new_state.functions = existing.functions;  // Keep existing function states
        new_state.subscribed_to_ecos = existing.subscribed_to_ecos;
    }
    
    // Add or update in state engine
    if (!state_engine.addOrUpdateLoco(address, new_state)) {
        DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u (state engine full?)\n", address);
        return;
    }
    
    // If this is new loco, request Ecos subscription
    if (!new_state.subscribed_to_ecos) {
        DEBUG_STATE_PRINTF("New loco from XpressNet: requesting Ecos subscription\n");
        requestEcosSubscription(address);
    }
    
    // Broadcast to other protocols
    broadcastCommand(address, new_state, LocoSource::XPRESSNET);
    
    // Update echo prevention state
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 0;  // SPEED command type
    echo_state.last_timestamp_ms = millis();
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
    
    DEBUG_XNET_PRINTF("XpressNet: Loco %u Functions 0x%08lx\n", address, functions);
    
    // Validate address
    if (!isValidDccAddress(address)) {
        DEBUG_XNET_PRINTF("ERROR: Invalid XpressNet address: %u\n", address);
        return;
    }
    
    // Check echo prevention
    if (isEchoCommand(address, LocoSource::XPRESSNET)) {
        DEBUG_ECHO_PRINTF("Echo suppressed: Loco %u function command (from Ecos)\n", address);
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
        new_state.subscribed_to_ecos = false;
        
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
    
    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 1;  // FUNCTION command type
    echo_state.last_timestamp_ms = millis();
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
    
    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 0;  // SPEED
    echo_state.last_timestamp_ms = millis();
    echo_state.last_source = LocoSource::ECOS;
}

void CommandRouter::handleEcosFunctionCommand(uint16_t address, uint32_t functions) {
    /*
     * Process function state update from Ecos
     */
    
    DEBUG_ECOS_PRINTF("Ecos: Loco %u Functions 0x%08lx\n", address, functions);
    
    // Validate
    if (!isValidDccAddress(address)) {
        DEBUG_ECOS_PRINTF("ERROR: Invalid Ecos address: %u\n", address);
        return;
    }
    
    // Check echo prevention
    if (isEchoCommand(address, LocoSource::ECOS)) {
        DEBUG_ECHO_PRINTF("Echo suppressed: Loco %u function command (from XpressNet)\n", address);
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
    
    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 1;  // FUNCTION
    echo_state.last_timestamp_ms = millis();
    echo_state.last_source = LocoSource::ECOS;
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
     * - Update status
     * - Log diagnostics
     */
    
    // Expunge locos inactive for 5 minutes
    int removed = state_engine.expungeInactiveLocos();
    if (removed > 0) {
        DEBUG_STATE_PRINTF("Expunged %d inactive locomotives\n", removed);
    }
    
    // Log status (if debug enabled)
    if (last_status_update == 0 || millis() - last_status_update > 60000) {
        last_status_update = millis();
        
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
    
    unsigned long now = millis();
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
        // TODO: Ecos interface will need a subscribeToLoco() method
        // For now, just log the intent
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
    
    // Device count (XpressNet specific - would need to get from interface)
    status.xnet_device_count = 0;  // TODO: Get from XpressNet interface
    
    // WiFi (would need WiFi interface - for now, just 0)
    status.wifi_rssi = 0;  // TODO: Get from WiFi
    
    // Uptime
    status.uptime_ms = millis();
    
    // Diagnostics
    status.total_commands = 0;  // TODO: Track in router
    status.current_heap_bytes = ESP.getFreeHeap();
    
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
    
    unsigned long age = millis() - echo_state.last_timestamp_ms;
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
