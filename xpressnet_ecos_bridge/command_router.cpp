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

#if ENABLE_Z21_LAN
void CommandRouter::setZ21Interface(ProtocolInterface* z21_intf) {
    z21 = z21_intf;
    DEBUG_PRINT("Z21 LAN interface registered with router\n");
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
    
    // No echo check here - see the removal note on handleXpressNetFunctionCommand()

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

    // Broadcast to other protocols - drive-only, functions untouched here
    broadcastCommand(address, new_state, LocoSource::XPRESSNET, false);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = speed;
    last_command.direction = direction;
    last_command.functions = new_state.functions;
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
    
    // No echo check here. Real bug found live 2026-08-28: this genuine
    // incoming-command gate (distinct from wasRecentSource(), which only
    // governs outgoing forwarding) could suppress a real new XpressNet
    // command whenever echo_state had been touched recently by ANY other
    // source for this address - and after today's fixes, that's often:
    // direct XNet->Z21 fan-out and Ecos's own (now correctly-arriving)
    // confirmations both update echo_state, so a second genuine XNet
    // command arriving shortly after could get misread as an echo of
    // something else, dropped entirely (matches the live symptom: function
    // toggles intermittently just not registering). This check was mirror-
    // added defensively alongside Ecos's own echo protection when only two
    // protocols existed; XpressNet's master-polled bus has no real pathway
    // for the bridge's own outgoing broadcast to loop back in as a fake
    // incoming throttle request (the vendored library's notify* callbacks
    // only fire for genuine polled throttle replies), so there was never
    // an actual echo risk here to protect against. wasRecentSource() still
    // correctly prevents a real ping-pong loop on the way OUT.

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
    last_command.address = address;
    last_command.speed = new_state.speed;
    last_command.direction = new_state.direction;
    last_command.functions = new_state.functions;
    last_command.source = LocoSource::XPRESSNET;

    // Update echo prevention
    echo_state.last_loco_address = address;
    echo_state.last_command_type = 1;  // FUNCTION command type
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::XPRESSNET;
}

// ============================================================================
// HANDLE Z21 LAN COMMANDS
// ============================================================================
// Mirrors the XpressNet handlers above exactly (Phase 6 step 4) - a
// throttle-facing protocol always forwards to Ecos only; Ecos's own echo
// back through handleEcosCommand()/broadcastCommand() is what reaches
// every other throttle-facing protocol, including back to Z21 itself
// (filtered by echo prevention for whichever side originated it).

void CommandRouter::handleZ21Command(uint16_t address, uint8_t speed, uint8_t direction) {
    DEBUG_PRINTF("Z21: Loco %u Speed %u Dir %u\n", address, speed, direction);

    if (!isValidDccAddress(address)) {
        DEBUG_PRINTF("ERROR: Invalid Z21 address: %u\n", address);
        return;
    }
    if (!isValidSpeed(speed) || !isValidDirection(direction)) {
        DEBUG_PRINTF("ERROR: Invalid speed (%u) or direction (%u)\n", speed, direction);
        return;
    }

    // No echo check here - Z21 is UDP client-server, not a shared bus, so
    // there's no physical path for the bridge's own outgoing broadcast to
    // loop back as a fake incoming client request - see the removal note
    // on handleXpressNetFunctionCommand() for the fuller reasoning
    // (applies equally here; this check caused the same intermittent
    // dropped-command symptom).

    LocoState new_state;
    new_state.dcc_address = address;
    new_state.speed = speed;
    new_state.direction = direction;
    new_state.functions = 0;
    new_state.last_source = LocoSource::Z21_LAN;

    LocoState existing;
    bool already_subscribed = false;
    if (state_engine.getLoco(address, existing)) {
        new_state.functions = existing.functions;
        already_subscribed = existing.subscribed_to_ecos;
    }
    new_state.subscribed_to_ecos = true;

    if (!state_engine.addOrUpdateLoco(address, new_state)) {
        DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u (state engine full?)\n", address);
        return;
    }

    if (!already_subscribed) {
        DEBUG_STATE_PRINTF("New loco from Z21: requesting Ecos subscription\n");
        requestEcosSubscription(address);
    }

    // Drive-only, functions untouched here
    broadcastCommand(address, new_state, LocoSource::Z21_LAN, false);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = speed;
    last_command.direction = direction;
    last_command.functions = new_state.functions;
    last_command.source = LocoSource::Z21_LAN;

    echo_state.last_loco_address = address;
    echo_state.last_command_type = 0;  // SPEED command type
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::Z21_LAN;
}

void CommandRouter::handleZ21FunctionCommand(uint16_t address, uint32_t functions) {
    DEBUG_PRINTF("Z21: Loco %u Functions 0x%08lx\n", address, (unsigned long)functions);

    if (!isValidDccAddress(address)) {
        DEBUG_PRINTF("ERROR: Invalid Z21 address: %u\n", address);
        return;
    }

    // No echo check here - see handleZ21Command()/handleXpressNetFunctionCommand().

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

        requestEcosSubscription(address);
    } else {
        new_state.functions = functions;
        new_state.last_source = LocoSource::Z21_LAN;
        state_engine.addOrUpdateLoco(address, new_state);
    }

    broadcastCommand(address, new_state, LocoSource::Z21_LAN);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = new_state.speed;
    last_command.direction = new_state.direction;
    last_command.functions = new_state.functions;
    last_command.source = LocoSource::Z21_LAN;

    echo_state.last_loco_address = address;
    echo_state.last_command_type = 1;  // FUNCTION command type
    echo_state.last_timestamp_ms = now_ms();
    echo_state.last_source = LocoSource::Z21_LAN;
}

// ============================================================================
// HANDLE ECOS COMMANDS
// ============================================================================

void CommandRouter::handleEcosCommand(uint16_t address, uint8_t speed, uint8_t direction,
                                       bool has_speed, bool has_direction) {
    /*
     * Process speed/direction update from Ecos
     *
     * Flow:
     * 1. Create/update locomotive in state engine
     * 2. Broadcast to other protocols (broadcastCommand() skips whichever
     *    one was the recent source, per destination - see wasRecentSource())
     * 3. Update echo prevention state
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

    // Get existing state or create new
    LocoState new_state;
    bool changed = true;  // a brand-new loco is always worth announcing
    if (!state_engine.getLoco(address, new_state)) {
        // New from Ecos
        new_state.dcc_address = address;
        new_state.speed = has_speed ? speed : 0;
        new_state.direction = has_direction ? direction : 1;
        new_state.functions = 0;
        new_state.subscribed_to_ecos = true;

        if (!state_engine.addOrUpdateLoco(address, new_state)) {
            DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u\n", address);
            return;
        }

        DEBUG_STATE_PRINTF("New loco from Ecos: %u (marked subscribed)\n", address);
    } else {
        // Update existing - only overwrite the field(s) this Ecos event
        // actually reported. A speed-only event must not silently reset
        // direction back to whatever default byte accompanied it, and vice
        // versa - the same class of bug already fixed for function merging.
        //
        // Real bug found live 2026-08-28: this used to broadcast
        // unconditionally, even when Ecos's event just re-confirmed a
        // value the bridge already had (a redundant echo of our own
        // background traffic - baseline queries, Z21 activity - not a
        // genuine change). Every one of those spuriously re-triggered
        // XpressNet's "stolen" push (pushLocoStateToOwningSlot()), which a
        // real MultiMaus visibly flashes even for a no-op update. Only
        // treat this as a real change if a reported field actually differs
        // from what was already known.
        changed = (has_speed && speed != new_state.speed) ||
                  (has_direction && direction != new_state.direction);

        if (has_speed) {
            new_state.speed = speed;
        }
        if (has_direction) {
            new_state.direction = direction;
        }
        new_state.subscribed_to_ecos = true;
        new_state.last_source = LocoSource::ECOS;
        state_engine.addOrUpdateLoco(address, new_state);
    }

    if (!changed) {
        DEBUG_ECOS_PRINTF("Ecos: Loco %u speed/direction unchanged, not re-broadcasting\n", address);
        return;
    }

    // Broadcast to XpressNet/Z21 (if enabled) - drive-only, functions untouched here
    broadcastCommand(address, new_state, LocoSource::ECOS, false);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = new_state.speed;
    last_command.direction = new_state.direction;
    last_command.functions = new_state.functions;
    last_command.source = LocoSource::ECOS;

    // Deliberately NOT touching echo_state here. Real bug found live
    // 2026-08-28: this used to unconditionally stamp echo_state to ECOS,
    // which nothing ever actually needs to check (wasRecentSource() is
    // only ever called for XPRESSNET/Z21_LAN, never ECOS) - but doing so
    // clobbered a still-relevant XPRESSNET/Z21_LAN attribution whenever two
    // of that protocol's commands were in flight close together: the first
    // one's Ecos confirmation would overwrite echo_state to ECOS, so the
    // second command's own confirmation, arriving shortly after, no longer
    // looked like an echo of a recent XpressNet/Z21 command and got pushed
    // straight back to it - XpressNet visibly flashing "stolen" for its own
    // command, confirmed live with only XpressNet and Ecos connected (Z21
    // powered off entirely, ruling out any Z21 involvement).
}

void CommandRouter::handleEcosFunctionCommand(uint16_t address, uint32_t functions, uint32_t functions_mask) {
    /*
     * Process function state update from Ecos. Forwarding to other
     * protocols (broadcastCommand()) skips whichever one was the recent
     * source, per destination - see wasRecentSource().
     */

    DEBUG_ECOS_PRINTF("Ecos: Loco %u Functions 0x%08x (mask 0x%08x)\n", address, functions, functions_mask);

    // Validate
    if (!isValidDccAddress(address)) {
        DEBUG_ECOS_PRINTF("ERROR: Invalid Ecos address: %u\n", address);
        return;
    }

    // Get existing or create
    LocoState new_state;
    bool known = state_engine.getLoco(address, new_state);
    uint32_t existing_functions = known ? new_state.functions : 0;

    if (!known) {
        new_state.dcc_address = address;
        new_state.speed = 0;
        new_state.direction = 1;
    }

    // Merge: only apply the bits Ecos actually reported (functions_mask),
    // keeping every other already-known function bit as-is. Real bug found
    // in codebase audit 2026-08-03: this used to overwrite the entire
    // bitmap unconditionally, so a single Ecos event reporting just one
    // changed function (e.g. only F3) would silently clobber every other
    // already-known function state back to 0, since the parser only ever
    // sets bits it actually saw in that specific event.
    new_state.functions = (existing_functions & ~functions_mask) | (functions & functions_mask);
    new_state.subscribed_to_ecos = true;
    new_state.last_source = LocoSource::ECOS;

    // Same "did anything actually change" check as handleEcosCommand() -
    // a redundant Ecos echo that merges back to the same bitmap shouldn't
    // spuriously re-trigger XpressNet's "stolen" push.
    bool changed = !known || (new_state.functions != existing_functions);

    if (!state_engine.addOrUpdateLoco(address, new_state)) {
        DEBUG_STATE_PRINTF("ERROR: Failed to add loco %u\n", address);
        return;
    }

    if (!changed) {
        DEBUG_ECOS_PRINTF("Ecos: Loco %u functions unchanged, not re-broadcasting\n", address);
        return;
    }

    // Broadcast to XpressNet
    broadcastCommand(address, new_state, LocoSource::ECOS);

    total_commands_count++;
    last_command.address = address;
    last_command.speed = new_state.speed;
    last_command.direction = new_state.direction;
    last_command.functions = new_state.functions;
    last_command.source = LocoSource::ECOS;

    // Deliberately NOT touching echo_state here - see handleEcosCommand()'s
    // comment for why.
}

// ============================================================================
// ACCESSORY / TURNOUT COMMANDS
// ============================================================================

void CommandRouter::handleAccessoryCommand(uint16_t address, bool diverging, LocoSource source) {
    DEBUG_PRINTF("Accessory: Addr=%u -> %s (source=%d)\n",
                 address, diverging ? "diverging" : "straight", (int)source);

    if (!isValidDccAddress(address)) {
        DEBUG_PRINTF("ERROR: Invalid accessory address: %u\n", address);
        return;
    }

    last_accessory.address = address;
    last_accessory.diverging = diverging;

    // v1: XpressNet -> Ecos only (Phase 5 step 10). Phase 7 step 1 adds
    // Z21 -> Ecos support. No Ecos-sourced accessory path exists yet, and no
    // echo prevention is needed for that reason - Ecos has no way to report an
    // accessory change back to us that could ever loop back to XpressNet/Z21.
    if (source == LocoSource::XPRESSNET || source == LocoSource::Z21_LAN) {
        #if ENABLE_ECOS_LAN
        if (ecos != nullptr) {
            ecos->sendAccessoryCommand(address, diverging);
        }
        #endif
    }

    total_commands_count++;
}

// ============================================================================
// EMERGENCY STOP / RESUME
// ============================================================================

void CommandRouter::emergencyStopAll(LocoSource source) {
    int count = state_engine.getLocoCount();
    DEBUG_STATE_PRINTF("Emergency stop: forcing %d loco(s) to speed 0 (source=%s)\n",
                       count, locoSourceToString(source));

    for (int i = 0; i < count; i++) {
        LocoState* loco = state_engine.getLocoByIndex(i);
        if (!loco) {
            continue;
        }

        loco->speed = 0;

        // Per-loco speed sync always runs regardless of source - every
        // throttle's displayed speed for whatever loco it has selected
        // needs updating either way.
        #if ENABLE_XPRESSNET
        if (xpressnet != nullptr) {
            xpressnet->sendSpeedCommand(loco->dcc_address, 0, loco->direction);
        }
        #endif
        #if ENABLE_Z21_LAN
        if (z21 != nullptr) {
            z21->sendSpeedCommand(loco->dcc_address, 0, loco->direction);
        }
        #endif
    }

    // The system-wide stop only goes to whichever side didn't already
    // originate it - Ecos gets one real system-wide stop command (not a
    // per-loco loop - "equivalent to the STOP button on the Ecos" per the
    // official spec), XpressNet gets a bus broadcast. Skipping the
    // originating side avoids re-telling it something it already knows;
    // for XpressNet specifically it also avoids a second, redundant
    // xnet.setPower() call on top of the one onPowerStateChange() already
    // sent as its wire-protocol echo.
    #if ENABLE_ECOS_LAN
    if (source != LocoSource::ECOS && ecos != nullptr) {
        ecos->sendEmergencyStop();
    }
    #endif

    #if ENABLE_XPRESSNET
    if (source != LocoSource::XPRESSNET && xpressnet != nullptr) {
        xpressnet->sendEmergencyStop();
    }
    #endif

    #if ENABLE_Z21_LAN
    if (source != LocoSource::Z21_LAN && z21 != nullptr) {
        z21->sendEmergencyStop();
    }
    #endif
}

void CommandRouter::resumeOperation(LocoSource source) {
    DEBUG_STATE_PRINTF("Resume operation requested (source=%s)\n", locoSourceToString(source));

    // Deliberately NOT restoring any loco's previous speed here - matches
    // real command-station safety behavior, the operator must re-throttle
    // manually after a stop.
    #if ENABLE_ECOS_LAN
    if (source != LocoSource::ECOS && ecos != nullptr) {
        ecos->sendResumeOperation();
    }
    #endif

    #if ENABLE_XPRESSNET
    if (source != LocoSource::XPRESSNET && xpressnet != nullptr) {
        xpressnet->sendResumeOperation();
    }
    #endif

    #if ENABLE_Z21_LAN
    if (source != LocoSource::Z21_LAN && z21 != nullptr) {
        z21->sendResumeOperation();
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

bool CommandRouter::wasRecentSource(uint16_t address, LocoSource source) const {
    // Real bug found live 2026-08-28: ECHO_PREVENTION_WINDOW (500ms) is far
    // shorter than Ecos's real confirmation round trip has shown itself to
    // be under real load today (TCP latency, paced baseline queries,
    // Ecos's own processing) - so a protocol's own command's confirmation
    // could arrive AFTER this window closed, get misread as an
    // independent Ecos-side change, and get pushed straight back to that
    // same protocol as if someone else had changed it (XpressNet visibly
    // flashing "stolen" for its own commands). Widened to
    // ECOS_ECHO_ATTRIBUTION_WINDOW_MS, generous enough to cover realistic
    // round-trip variance - the tradeoff (rarely, a genuine independent
    // Ecos-direct change made within that window of recent throttle
    // activity might be attributed to the throttle instead) is far less
    // disruptive than the false-positive "stolen" flashes this caused.
    unsigned long now = now_ms();
    if (now - echo_state.last_timestamp_ms > ECOS_ECHO_ATTRIBUTION_WINDOW_MS) {
        return false;
    }
    if (address != echo_state.last_loco_address) {
        return false;
    }
    return echo_state.last_source == source;
}

void CommandRouter::broadcastCommand(uint16_t address, const LocoState& state, LocoSource source,
                                     bool functions_changed) {
    /*
     * Send command to all protocols except the source
     *
     * If came from XpressNet, send to Ecos
     * If came from Ecos, send to XpressNet
     *
     * functions_changed=false (passed by the drive-only handlers) skips
     * resending the function bitmap. Real bug found live 2026-08-28: a
     * plain speed/direction command used to unconditionally resend the
     * WHOLE function bitmap downstream too. Harmless in steady state (it's
     * a no-op resend of the same values), but destructive right after a
     * bridge reboot: StateEngine starts functions=0 for every loco, and the
     * very FIRST drive command from a reconnecting throttle - fired before
     * Ecos's own subscription reply had a chance to populate the real
     * value - would blast that all-zero bitmap at Ecos, silently wiping out
     * whatever real function state (e.g. sound toggles) Ecos actually had.
     * Confirmed live: F8/F9/F10/F11 stayed genuinely desynced from Ecos
     * across reconnects, unfixable by re-selecting the loco, because every
     * subsequent speed change re-asserted the bridge's stale belief.
     */

    if (source == LocoSource::XPRESSNET || source == LocoSource::Z21_LAN) {
        // Forward to Ecos, the persistent source of truth for this loco's
        // state.
        #if ENABLE_ECOS_LAN
        if (ecos != nullptr) {
            DEBUG_PRINTF("Broadcasting %s command to Ecos\n", locoSourceToString(source));
            ecos->sendSpeedCommand(address, state.speed, state.direction);
            if (functions_changed) {
                ecos->sendFunctionCommand(address, state.functions);
            }
        }
        #endif

        // ALSO fan out directly, immediately, to every OTHER throttle-
        // facing protocol - do not wait for Ecos to echo this back first.
        // Real design change 2026-08-28, at the user's prompting: this used
        // to rely entirely on Ecos's own confirmation event completing the
        // loop to the other protocol (the ECOS branch below), which turned
        // out to be fragile in practice - Ecos's echo behavior around
        // control ownership, its own reply framing, and this bridge's own
        // baseline-query/function-send traffic all turned out to be able to
        // interfere with whether or when that confirmation ever arrived
        // (see CHANGELOG 2026-08-28 for the specific bugs). There's no
        // actual need to wait: the bridge already knows exactly what
        // changed and for which loco, so telling the other protocol
        // directly is simpler and doesn't depend on Ecos's cooperation.
        // Ecos's own echo (below) still reaches this same protocol too when
        // it arrives - redundant but harmless (same value re-applied).
        #if ENABLE_XPRESSNET
        if (source != LocoSource::XPRESSNET && xpressnet != nullptr) {
            DEBUG_PRINTF("Broadcasting %s command directly to XpressNet\n", locoSourceToString(source));
            xpressnet->sendSpeedCommand(address, state.speed, state.direction);
            if (functions_changed) {
                xpressnet->sendFunctionCommand(address, state.functions);
            }
            xpressnet->pushLocoStateToOwningSlot(address);
        }
        #endif
        #if ENABLE_Z21_LAN
        if (source != LocoSource::Z21_LAN && z21 != nullptr) {
            DEBUG_PRINTF("Broadcasting %s command directly to Z21\n", locoSourceToString(source));
            z21->sendSpeedCommand(address, state.speed, state.direction);
            if (functions_changed) {
                z21->sendFunctionCommand(address, state.functions);
            }
        }
        #endif
    }
    else if (source == LocoSource::ECOS) {
        // Send to every throttle-facing protocol EXCEPT whichever one just
        // sent the command that produced this Ecos update (if any) - that
        // protocol already has its own knowledge of what it just did
        // (XpressNet optimistically updates its own display locally; Z21
        // gets its own immediate confirmation via broadcastConfirmedState()).
        //
        // Real bug found live 2026-08-28: this used to be gated by a single
        // shared isEchoCommand() check up in handleEcosCommand()/
        // handleEcosFunctionCommand() that dropped the ENTIRE update when it
        // matched ANY recent source - correct for not echoing XpressNet's
        // own command back to XpressNet, but with three protocols that also
        // meant Z21 (or XpressNet) never learned about a change that
        // genuinely originated from the OTHER throttle-facing protocol.
        // wasRecentSource() checks per destination instead, so the
        // non-originating protocol still gets forwarded to.
        #if ENABLE_XPRESSNET
        if (xpressnet != nullptr && !wasRecentSource(address, LocoSource::XPRESSNET)) {
            DEBUG_PRINTF("Broadcasting Ecos command to XpressNet\n");
            xpressnet->sendSpeedCommand(address, state.speed, state.direction);
            if (functions_changed) {
                xpressnet->sendFunctionCommand(address, state.functions);
            }
            // A MultiMaus that already has this loco selected doesn't reliably
            // apply the plain broadcast above to its own display/button-latch
            // model - only a directed reply it recognizes as authoritative.
            // No-op if no XpressNet slot currently has this address selected.
            xpressnet->pushLocoStateToOwningSlot(address);
        }
        #endif
        #if ENABLE_Z21_LAN
        if (z21 != nullptr && !wasRecentSource(address, LocoSource::Z21_LAN)) {
            DEBUG_PRINTF("Broadcasting Ecos command to Z21\n");
            z21->sendSpeedCommand(address, state.speed, state.direction);
            if (functions_changed) {
                z21->sendFunctionCommand(address, state.functions);
            }
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

    #if ENABLE_Z21_LAN
    status.z21_status = (z21 != nullptr) ?
                         z21->getStatus() :
                         ComponentStatus::DISCONNECTED;
    status.z21_client_count = (z21 != nullptr) ? z21->getActiveClientCount() : 0;
    status.z21_last_message_age_ms = (z21 != nullptr) ?
                         z21->getLastMessageAgeMs() :
                         ProtocolInterface::NO_TIMESTAMP;
    if (z21 != nullptr) {
        z21->getLastMessageSourceIp(status.z21_last_message_ip, sizeof(status.z21_last_message_ip));
    } else {
        status.z21_last_message_ip[0] = '\0';
    }
    #else
    status.z21_status = ComponentStatus::DISCONNECTED;
    status.z21_client_count = 0;
    status.z21_last_message_age_ms = ProtocolInterface::NO_TIMESTAMP;
    status.z21_last_message_ip[0] = '\0';
    #endif

    // Deferred OLED fields (Phase 5 step 8)
    #if ENABLE_XPRESSNET
    status.xnet_last_message_age_ms = (xpressnet != nullptr) ?
                         xpressnet->getLastMessageAgeMs() :
                         ProtocolInterface::NO_TIMESTAMP;
    #else
    status.xnet_last_message_age_ms = ProtocolInterface::NO_TIMESTAMP;
    #endif

    #if ENABLE_ECOS_LAN
    status.ecos_heartbeat_latency_ms = (ecos != nullptr) ?
                         ecos->getLastHeartbeatLatencyMs() :
                         ProtocolInterface::NO_TIMESTAMP;
    #else
    status.ecos_heartbeat_latency_ms = ProtocolInterface::NO_TIMESTAMP;
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
    status.last_command_functions = last_command.functions;
    status.last_command_source = last_command.source;

    // Last accessory command (Phase 5 step 10, v1)
    status.last_accessory_address = last_accessory.address;
    status.last_accessory_diverging = last_accessory.diverging;

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
    DEBUG_ECHO_PRINTF("Window:         %lu ms\n", (unsigned long)ECOS_ECHO_ATTRIBUTION_WINDOW_MS);

    if (age < ECOS_ECHO_ATTRIBUTION_WINDOW_MS) {
        DEBUG_ECHO_PRINTF("Status:         ACTIVE (wasRecentSource() would attribute an Ecos echo to this source)\n");
    } else {
        DEBUG_ECHO_PRINTF("Status:         INACTIVE (window expired)\n");
    }
    
    DEBUG_ECHO_PRINT("=============================\n\n");
}

#endif  // ENABLE_DEBUG
