/*
 * Command Router - Bridge Commands Between Protocols
 * 
 * Routes commands from one protocol to another:
 * - XpressNet command → Ecos LAN
 * - Ecos update → XpressNet devices
 * - LocoNet → Others (future)
 * 
 * Implements echo prevention to avoid loops:
 * - When Ecos sends update back after we sent command
 * - 500ms window to detect echoes
 * 
 * No persistence - relies on StateEngine for state storage
 */

#ifndef COMMAND_ROUTER_H
#define COMMAND_ROUTER_H

#include <cstdint>
#include "config.h"
#include "definitions.h"
#include "state_engine.h"
#include "interfaces/interface_base.h"

class CommandRouter {
public:
    /**
     * Initialize router with references to protocols
     * These are set after protocol interfaces are created
     */
    CommandRouter();

    /**
     * Set reference to XpressNet interface (called after creation)
     * Accepts the abstract ProtocolInterface so tests can inject a mock
     * without depending on the concrete (Arduino-coupled) XpressNetInterface.
     */
    #if ENABLE_XPRESSNET
    void setXpressNetInterface(ProtocolInterface* xnet);
    #endif

    /**
     * Set reference to Ecos interface (called after creation)
     * Accepts the abstract ProtocolInterface so tests can inject a mock
     * without depending on the concrete (Arduino-coupled) EcosInterface.
     */
    #if ENABLE_ECOS_LAN
    void setEcosInterface(ProtocolInterface* ecos);
    #endif
    
    /**
     * Handle incoming command from XpressNet
     * 1. Update state engine
     * 2. Check echo prevention
     * 3. Broadcast to other protocols
     */
    void handleXpressNetCommand(uint16_t address, uint8_t speed, uint8_t direction);
    
    /**
     * Handle incoming function command from XpressNet
     */
    void handleXpressNetFunctionCommand(uint16_t address, uint32_t functions);
    
    /**
     * Handle incoming command from Ecos
     * 1. Update state engine
     * 2. Check echo prevention
     * 3. Broadcast to other protocols
     */
    void handleEcosCommand(uint16_t address, uint8_t speed, uint8_t direction);
    
    /**
     * Handle incoming function command from Ecos
     * @param functions Bitmap F0-F31, but only the bits set in functions_mask
     *                  are actually applied - unset bits keep their
     *                  previously-known value rather than being clobbered
     *                  to 0, since a single Ecos event often only reports
     *                  the one function that actually changed.
     * @param functions_mask Which bits in functions are meaningful
     */
    void handleEcosFunctionCommand(uint16_t address, uint32_t functions, uint32_t functions_mask);

    /**
     * Bus-wide emergency stop / track power off. Forces every known loco's
     * speed to 0 (in StateEngine and broadcast to XpressNet), plus tells
     * whichever side didn't originate the request about the global stop -
     * Ecos gets its real system-wide stop command, XpressNet gets a bus
     * broadcast - so the stop propagates bidirectionally regardless of
     * which device (a throttle or Ecos itself) triggered it.
     *
     * @param source Which protocol originated this request - that side is
     *               NOT re-notified (it already knows/acknowledged this
     *               itself), only the other one is.
     *
     * Deliberately bypasses the single-address broadcastCommand()/echo-
     * prevention path - that machinery tracks one most-recent command at a
     * time and would just get thrashed by iterating every loco here.
     */
    void emergencyStopAll(LocoSource source);

    /**
     * Resume normal operation after an emergency stop / track power off.
     * Tells whichever side didn't originate the request. Deliberately does
     * NOT restore any locomotive's previous speed - matches real
     * command-station safety behavior, the operator must re-throttle
     * manually.
     *
     * @param source Which protocol originated this request - not re-notified.
     */
    void resumeOperation(LocoSource source);

    /**
     * Periodic housekeeping
     * - Check for inactive locos
     * - Send heartbeats
     * Called from main loop every ~30 seconds
     */
    void update();
    
    /**
     * Get system status for display
     */
    SystemStatus getSystemStatus() const;
    
    /**
     * Get reference to state engine (for direct query if needed)
     */
    StateEngine& getStateEngine() { return state_engine; }
    const StateEngine& getStateEngine() const { return state_engine; }
    
    /**
     * Debug: Print echo prevention state
     * Only compiled if ENABLE_DEBUG
     */
    #if ENABLE_DEBUG
    void debugPrintEchoState() const;
    #endif

private:
    StateEngine state_engine;
    
    // Interface references (initialized in setters)
    #if ENABLE_XPRESSNET
    ProtocolInterface* xpressnet = nullptr;
    #endif

    #if ENABLE_ECOS_LAN
    ProtocolInterface* ecos = nullptr;
    #endif
    
    // Echo prevention state
    struct EchoPreventionState {
        uint16_t last_loco_address;
        uint8_t last_command_type;      // SPEED, FUNCTION, etc.
        unsigned long last_timestamp_ms;
        LocoSource last_source;         // Which protocol sent it
    };
    
    EchoPreventionState echo_state = {0, 0, 0, LocoSource::UNKNOWN};

    // Diagnostic counters (for display/status - see getSystemStatus())
    uint32_t total_commands_count = 0;
    uint32_t echo_prevented_count = 0;

    // Last drive command processed, for display (see getSystemStatus())
    struct LastCommandInfo {
        uint16_t address = 0;
        uint8_t speed = 0;
        uint8_t direction = 0;
        LocoSource source = LocoSource::UNKNOWN;
    };
    LastCommandInfo last_command;

    // Periodic task tracking
    unsigned long last_expiry_check = 0;
    unsigned long last_status_update = 0;
    
    // Helper methods
    /**
     * Check if incoming command is an echo of our recent outgoing command
     * @param address Locomotive address
     * @param source Which protocol this came from
     * @return true if echo (suppress), false if new command (process)
     */
    bool isEchoCommand(uint16_t address, LocoSource source) const;
    
    /**
     * Broadcast command to all active protocols except source
     */
    void broadcastCommand(uint16_t address, const LocoState& state, LocoSource source);
    
    /**
     * Attempt to subscribe to unknown locomotive on Ecos
     * When XpressNet sends command for unknown loco, request Ecos to start sending updates
     */
    void requestEcosSubscription(uint16_t address);
};

#endif  // COMMAND_ROUTER_H
