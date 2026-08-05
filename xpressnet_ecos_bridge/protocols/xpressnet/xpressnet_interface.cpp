/*
 * XpressNet Interface Implementation - Master Device
 *
 * Thin wrapper around Philipp Gahtow's XpressNetMaster library
 * (libraries/XpressNetMaster). The library owns the half-duplex single-wire
 * serial framing (62500 baud, 8N1+parity call-byte marker) and XpressNet
 * message/checksum handling; this file only translates between its
 * weak-symbol callback API and CommandRouter.
 *
 * Non-blocking, cooperative multitasking design.
 */

#include "xpressnet_interface.h"
#include "../../command_router.h"
#include "../../config.h"
#include "../../definitions.h"
#include "../../utils/debug.h"
#include <Arduino.h>

// ============================================================================
// CALLBACK DISPATCH
// ============================================================================
// XpressNetMasterClass calls these as weak-symbol free functions - they can't
// be member functions. Only one XpressNetInterface exists (one physical bus),
// so a single file-scope pointer is enough to route callbacks back into it.

static XpressNetInterface* g_xnet_instance = nullptr;

void notifyXNetLocoDrive128(uint16_t Address, uint8_t Speed) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoDrive128(Address, Speed);
    }
}

void notifyXNetLocoDrive14(uint16_t Address, uint8_t Speed) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoDriveStepped(Address, Speed, 14);
    }
}

void notifyXNetLocoDrive27(uint16_t Address, uint8_t Speed) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoDriveStepped(Address, Speed, 27);
    }
}

void notifyXNetLocoDrive28(uint16_t Address, uint8_t Speed) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoDriveStepped(Address, Speed, 28);
    }
}

void notifyXNetgiveLocoInfo(uint8_t UserOps, uint16_t Address) {
    if (g_xnet_instance) {
        g_xnet_instance->onGiveLocoInfo(UserOps, Address);
    }
}

void notifyXNetgiveLocoMM(uint8_t UserOps, uint16_t Address) {
    if (g_xnet_instance) {
        g_xnet_instance->onGiveLocoMM(UserOps, Address);
    }
}

void notifyXNetgiveLocoFunc(uint8_t UserOps, uint16_t Address) {
    if (g_xnet_instance) {
        g_xnet_instance->onGiveLocoFunc(UserOps, Address);
    }
}

void notifyXNetPower(uint8_t State) {
    if (g_xnet_instance) {
        g_xnet_instance->onPowerStateChange(State);
    }
}

void notifyXNetLocoFunc1(uint16_t Address, uint8_t Func1) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoFunctionGroup(Address, 1, Func1);
    }
}

void notifyXNetLocoFunc2(uint16_t Address, uint8_t Func2) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoFunctionGroup(Address, 2, Func2);
    }
}

void notifyXNetLocoFunc3(uint16_t Address, uint8_t Func3) {
    if (g_xnet_instance) {
        g_xnet_instance->onLocoFunctionGroup(Address, 3, Func3);
    }
}

void notifyXNetLocoFuncX(uint16_t Address, uint8_t group, uint8_t Func) {
    // group 0x04 = F13-F20, 0x05 = F21-F28. Groups 0x06+ (F29 and beyond)
    // fall outside our uint32_t F0-F31 bitmap - ignore them.
    if (g_xnet_instance && (group == 0x04 || group == 0x05)) {
        g_xnet_instance->onLocoFunctionGroup(Address, group, Func);
    }
}

// ============================================================================
// CONSTRUCTOR & INITIALIZATION
// ============================================================================

XpressNetInterface::XpressNetInterface()
    : current_status(ComponentStatus::DISCONNECTED),
      last_message_time(0),
      bus_connect_time(0),
      was_master_mode(true),
      led_off_at_ms(0),
      router(nullptr) {
    g_xnet_instance = this;
}

XpressNetInterface::~XpressNetInterface() {
    if (g_xnet_instance == this) {
        g_xnet_instance = nullptr;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool XpressNetInterface::begin() {
    #if ENABLE_XPRESSNET
        DEBUG_XNET_PRINTF("Initializing XpressNet interface (data=D%u control=D%u baud=%u)...\n",
                         XPRESSNET_DATA_PIN, XPRESSNET_CONTROL_PIN, XPRESSNET_BAUD);

        // Note: XpressNetMasterClass::setup() hangs forever (its own infinite
        // loop) if the SoftwareSerial pin configuration is invalid - there is
        // no recoverable failure path for that case.
        xnet.setup(Loco128, XPRESSNET_DATA_PIN, XPRESSNET_CONTROL_PIN);

        // Broadcast track-power-on ("Alles An") - without this, throttles may
        // show themselves as active (they hear a valid master) while still
        // withholding real drive/response commands, since they've never been
        // told the track is actually powered.
        xnet.setPower(csNormal);

        // TEMP: onboard LED (GPIO2/D4 on this board - not used by anything
        // else here) as a visual bus-activity indicator. Active-low: HIGH=off.
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, HIGH);

        current_status = ComponentStatus::CONNECTING;
        last_message_time = millis();
        bus_connect_time = 0;

        DEBUG_XNET_PRINTF("XpressNet interface initialized\n");

        return true;

    #else
        DEBUG_PRINTF("XpressNet disabled in config.h\n");
        return false;
    #endif
}

// ============================================================================
// MAIN UPDATE LOOP - MOST TIME-CRITICAL
// ============================================================================

void XpressNetInterface::update() {
    #if !ENABLE_XPRESSNET
        return;  // If disabled, do nothing
    #endif

    updateBusStatus();

    // TEMP: non-blocking turn-off for the activity LED - see markBusActivity().
    if (led_off_at_ms != 0 && (long)(millis() - led_off_at_ms) >= 0) {
        digitalWrite(LED_BUILTIN, HIGH);  // active-low: HIGH=off
        led_off_at_ms = 0;
    }

    // Pumps the library's SoftwareSerial read/parse state machine; fires
    // notifyXNet* callbacks synchronously for any complete message found.
    xnet.update();
}

// ============================================================================
// BUS STATUS & TIMEOUT MANAGEMENT
// ============================================================================

void XpressNetInterface::updateBusStatus() {
    unsigned long now = millis();
    unsigned long time_since_message = now - last_message_time;

    if (current_status == ComponentStatus::CONNECTING) {
        if (time_since_message > 3000) {
            current_status = ComponentStatus::DISCONNECTED;
            DEBUG_XNET_PRINTF("XpressNet: Bus disconnected (no initial activity)\n");
        }
    } else if (current_status == ComponentStatus::CONNECTED) {
        if (time_since_message > BUS_TIMEOUT) {
            current_status = ComponentStatus::DISCONNECTED;
            bus_connect_time = 0;
            DEBUG_XNET_PRINTF("XpressNet: Bus timeout (no messages for %lu ms)\n", BUS_TIMEOUT);
        }
    }

    // Tripwire: this bridge is designed to be the sole XpressNet master on
    // its bus (throttles are slaves, Ecos is off-bus over WiFi). The library
    // can only demote itself to slave mode if it sees a call byte addressed
    // to the legacy slave address - i.e. a second real master on the bus,
    // which should never happen here. Edge-triggered so it logs once per
    // transition rather than spamming every loop iteration.
    bool is_master_mode = xnet.getOperationModeMaster();
    if (was_master_mode && !is_master_mode) {
        DEBUG_XNET_PRINTF("XpressNet: WARNING - demoted from MASTER to SLAVE mode! "
                           "Check for a second XpressNet master on the bus.\n");
    } else if (!was_master_mode && is_master_mode) {
        DEBUG_XNET_PRINTF("XpressNet: Returned to MASTER mode\n");
    }
    was_master_mode = is_master_mode;
}

void XpressNetInterface::markBusActivity() {
    // TEMP: flash the activity LED on for ACTIVITY_LED_MS - see update()
    // for the non-blocking turn-off.
    digitalWrite(LED_BUILTIN, LOW);  // active-low: LOW=on
    led_off_at_ms = millis() + ACTIVITY_LED_MS;

    last_message_time = millis();
    if (bus_connect_time == 0) {
        bus_connect_time = last_message_time;
    }
    // Real bug found on hardware 2026-07-31: this only handled the
    // CONNECTING->CONNECTED transition, so once updateBusStatus() had
    // already timed out to DISCONNECTED (e.g. the initial 3s grace period
    // elapsing before a throttle first spoke), the status display got stuck
    // on "Disconnected" forever even once real traffic started arriving -
    // commands were still processed correctly (this method's caller had
    // already parsed a real message), only the reported status was wrong.
    if (current_status != ComponentStatus::CONNECTED) {
        current_status = ComponentStatus::CONNECTED;
        DEBUG_XNET_PRINTF("XpressNet: Bus connected! First message from device\n");
    }
}

// ============================================================================
// INCOMING COMMAND HANDLING (called from the notifyXNet* callbacks)
// ============================================================================

void XpressNetInterface::onLocoDrive128(uint16_t address, uint8_t speed_byte) {
    if (!isValidDccAddress(address)) {
        DEBUG_XNET_PRINTF("XpressNet: Invalid address %u\n", address);
        return;
    }

    markBusActivity();

    if (!router) {
        DEBUG_XNET_PRINTF("XpressNet: WARNING - router not set, dropping command\n");
        return;
    }

    // RVVVVVVV: bit7=direction (0=forward,1=reverse on the wire), bits6-0=speed.
    // Our internal convention is inverted (direction: 0=reverse, 1=forward).
    uint8_t speed = speed_byte & 0x7F;
    uint8_t direction = (speed_byte & 0x80) ? 0 : 1;

    if (!isValidSpeed(speed) || !isValidDirection(direction)) {
        // Rejects the reserved V=127 step steps (real Lenz "V=1 is emergency
        // stop, V=2-127 are speed steps 1-126" nuance is not modeled yet -
        // see CLAUDE.md Future Improvements).
        DEBUG_XNET_PRINTF("XpressNet: Invalid speed byte 0x%02x for addr %u\n", speed_byte, address);
        return;
    }

    DEBUG_XNET_PRINTF("XpressNet RX: Speed - Addr=%u Speed=%u Dir=%u\n", address, speed, direction);
    router->handleXpressNetCommand(address, speed, direction);
}

void XpressNetInterface::onLocoDriveStepped(uint16_t address, uint8_t speed_byte, uint8_t max_steps) {
    if (!isValidDccAddress(address)) {
        DEBUG_XNET_PRINTF("XpressNet: Invalid address %u\n", address);
        return;
    }

    markBusActivity();

    if (!router) {
        DEBUG_XNET_PRINTF("XpressNet: WARNING - router not set, dropping command\n");
        return;
    }

    uint8_t direction = (speed_byte & 0x80) ? 0 : 1;
    uint8_t raw_speed;

    if (max_steps == 14) {
        // Same RVVVVVVV layout as 128-step, just a smaller meaningful range.
        raw_speed = speed_byte & 0x7F;
    } else {
        // 27/28-step wire format bit-interleaves the 5-bit speed value -
        // reverses the scramble XpressNetMasterClass::setSpeed() applies:
        // v = ((s&0x0F)<<1) | ((s>>4)&0x01) | dir
        raw_speed = ((speed_byte >> 1) & 0x0F) | ((speed_byte & 0x01) << 4);
    }

    if (raw_speed > max_steps) {
        // Reserved/emergency-stop code for this step mode - not modeled,
        // same simplification as onLocoDrive128's V=127 rejection.
        DEBUG_XNET_PRINTF("XpressNet: Invalid %u-step speed byte 0x%02x for addr %u\n",
                           max_steps, speed_byte, address);
        return;
    }

    // Rescale into our internal 128-step-based 0-126 range.
    uint8_t speed = (uint8_t)(((uint16_t)raw_speed * DCC_MAX_SPEED) / max_steps);

    DEBUG_XNET_PRINTF("XpressNet RX: Speed (%u-step) - Addr=%u RawSpeed=%u Speed=%u Dir=%u\n",
                       max_steps, address, raw_speed, speed, direction);
    router->handleXpressNetCommand(address, speed, direction);
}

void XpressNetInterface::resolveLocoStateForReply(uint16_t address, uint8_t& speed_byte, uint32_t& functions) {
    LocoState loco;
    bool known = router && router->getStateEngine().getLoco(address, loco);

    uint8_t speed = known ? loco.speed : 0;
    uint8_t direction = known ? loco.direction : 1;
    functions = known ? loco.functions : 0;

    speed_byte = speed & 0x7F;
    if (direction == 0) {
        speed_byte |= 0x80;  // reverse
    }
}

void XpressNetInterface::onGiveLocoInfo(uint8_t user_ops, uint16_t address) {
    if (!isValidDccAddress(address)) {
        return;
    }

    // A throttle asking for loco info is a real, successfully-parsed message
    // from a device on the bus - just as much proof of bus activity as a
    // drive/function command. Real bug: without this, a throttle that only
    // re-polls loco info (to keep its own display in sync) rather than
    // sending new drive commands looked like it had gone silent, and status
    // flipped to DISCONNECTED after BUS_TIMEOUT even though it never left.
    markBusActivity();

    uint8_t speed_byte;
    uint32_t functions;
    resolveLocoStateForReply(address, speed_byte, functions);

    // F5-F12 packed into one byte (low nibble F5-F8, high nibble F9-F12) -
    // exact nibble order not independently verified against the Lenz spec;
    // worst case this only affects the initial display until the next
    // broadcast corrects it.
    uint8_t func_5to12 = (buildFunctionGroupByte(functions, 3) << 4) | buildFunctionGroupByte(functions, 2);

    xnet.SetLocoInfo(user_ops, Loco128, speed_byte, buildFunctionGroupByte(functions, 1), func_5to12);

    DEBUG_XNET_PRINTF("XpressNet TX: LocoInfo reply - Addr=%u (declaring 128-step)\n", address);
}

void XpressNetInterface::onGiveLocoMM(uint8_t user_ops, uint16_t address) {
    if (!isValidDccAddress(address)) {
        return;
    }

    // See onGiveLocoInfo() - a real parsed request from a device, same as a
    // drive/function command for bus-activity purposes.
    markBusActivity();

    uint8_t speed_byte;
    uint32_t functions;
    resolveLocoStateForReply(address, speed_byte, functions);

    xnet.SetLocoInfoMM(user_ops, Loco128, speed_byte,
                        buildFunctionGroupByte(functions, 1),     // F0-F4
                        buildFunctionGroupByte(functions, 2),     // F5-F8
                        buildFunctionGroupByte(functions, 3),     // F9-F12
                        buildFunctionGroupByte(functions, 0x04)); // F13-F20

    DEBUG_XNET_PRINTF("XpressNet TX: LocoInfoMM reply - Addr=%u (declaring 128-step)\n", address);
}

void XpressNetInterface::onGiveLocoFunc(uint8_t user_ops, uint16_t address) {
    if (!isValidDccAddress(address)) {
        return;
    }

    // See onGiveLocoInfo() - a real parsed request from a device on the bus.
    markBusActivity();

    uint8_t speed_byte;
    uint32_t functions;
    resolveLocoStateForReply(address, speed_byte, functions);

    xnet.SetFktStatus(user_ops,
                       buildFunctionGroupByte(functions, 0x04),   // F13-F20
                       buildFunctionGroupByte(functions, 0x05));  // F21-F28

    DEBUG_XNET_PRINTF("XpressNet TX: FktStatus reply (F13-F28) - Addr=%u\n", address);
}

void XpressNetInterface::onPowerStateChange(uint8_t state) {
    // See onGiveLocoInfo() - a real parsed request from a device on the bus.
    markBusActivity();

    DEBUG_XNET_PRINTF("XpressNet: Power state change request - state=0x%02x, echoing acknowledgment\n", state);
    xnet.setPower(state);

    if (!router) {
        return;
    }

    // csEmergencyStop/csTrackVoltageOff mean "stop moving"; csNormal means
    // resumed. Short-circuit/service-mode bits aren't treated as a stop
    // request here - this only reacts to an explicit e-stop or track-power-
    // off, matching the Phase 5 backlog item this implements.
    if (state == csNormal) {
        router->resumeOperation(LocoSource::XPRESSNET);
    } else if (state & (csEmergencyStop | csTrackVoltageOff)) {
        router->emergencyStopAll(LocoSource::XPRESSNET);
    }
}

void XpressNetInterface::onLocoFunctionGroup(uint16_t address, uint8_t group, uint8_t bits) {
    if (!isValidDccAddress(address)) {
        DEBUG_XNET_PRINTF("XpressNet: Invalid address %u\n", address);
        return;
    }

    markBusActivity();

    if (!router) {
        DEBUG_XNET_PRINTF("XpressNet: WARNING - router not set, dropping command\n");
        return;
    }

    // CommandRouter::handleXpressNetFunctionCommand overwrites the full
    // bitmap, but XpressNet fragments F0-F31 across up to 5 separate
    // messages - start from the loco's current known state and merge in
    // only the bits this group covers.
    LocoState existing;
    uint32_t functions = 0;
    if (router->getStateEngine().getLoco(address, existing)) {
        functions = existing.functions;
    }

    switch (group) {
        case 1: {
            // Byte layout: 0 0 0 F0 F4 F3 F2 F1 (F0 sits at bit4, a real
            // Lenz XpressNet quirk from the original 5-function message).
            static const uint8_t func_bit[5] = {0, 1, 2, 3, 4};
            static const uint8_t byte_bit[5] = {4, 0, 1, 2, 3};
            for (uint8_t i = 0; i < 5; i++) {
                uint32_t mask = (1UL << func_bit[i]);
                functions &= ~mask;
                if (bits & (1 << byte_bit[i])) functions |= mask;
            }
            break;
        }
        case 2:  // F5-F8, bit0=F5..bit3=F8
            functions &= ~(0x0FUL << 5);
            functions |= (uint32_t)(bits & 0x0F) << 5;
            break;
        case 3:  // F9-F12, bit0=F9..bit3=F12
            functions &= ~(0x0FUL << 9);
            functions |= (uint32_t)(bits & 0x0F) << 9;
            break;
        case 0x04:  // F13-F20, bit0=F13..bit7=F20
            functions &= ~(0xFFUL << 13);
            functions |= (uint32_t)bits << 13;
            break;
        case 0x05:  // F21-F28, bit0=F21..bit7=F28
            functions &= ~(0xFFUL << 21);
            functions |= (uint32_t)bits << 21;
            break;
        default:
            return;  // Unknown/out-of-range group - shouldn't happen, notifyXNetLocoFuncX already filters
    }

    DEBUG_XNET_PRINTF("XpressNet RX: Function group %u - Addr=%u Fn=0x%08lx\n", group, address, (unsigned long)functions);
    router->handleXpressNetFunctionCommand(address, functions);
}

// ============================================================================
// OUTGOING COMMANDS - BROADCAST TO BUS
// ============================================================================

void XpressNetInterface::sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    // Real bug found on hardware 2026-07-31: this used to bail out unless
    // current_status == CONNECTED, but that status only reflects "have we
    // recently heard a throttle speak" (it drops to DISCONNECTED after just
    // BUS_TIMEOUT/5s of RX silence). A master must keep transmitting
    // regardless - it doesn't need to have recently heard from a throttle
    // to legitimately broadcast a loco's new state. This silently dropped
    // 88 of 89 Ecos-originated updates in one real test (only the first,
    // arriving just before the 5s timeout tripped, ever reached the bus).
    if (!isValidDccAddress(address) || !isValidSpeed(speed) || !isValidDirection(direction)) {
        DEBUG_XNET_PRINTF("XpressNet TX: Invalid speed command - addr=%u speed=%u dir=%u\n",
                         address, speed, direction);
        return;
    }

    uint8_t speed_byte = speed & 0x7F;
    if (direction == 0) {
        speed_byte |= 0x80;  // reverse
    }

    xnet.setSpeed(address, Loco128, speed_byte);

    DEBUG_XNET_PRINTF("XpressNet TX: Speed - Addr=%u Speed=%u Dir=%u\n", address, speed, direction);
}

void XpressNetInterface::sendFunctionCommand(uint16_t address, uint32_t functions) {
    // See sendSpeedCommand() - a master must keep transmitting regardless of
    // whether it has recently heard a throttle speak.
    if (!isValidDccAddress(address)) {
        return;
    }

    xnet.setFunc0to4(address, buildFunctionGroupByte(functions, 1));
    xnet.setFunc5to8(address, buildFunctionGroupByte(functions, 2));
    xnet.setFunc9to12(address, buildFunctionGroupByte(functions, 3));
    xnet.setFunc13to20(address, buildFunctionGroupByte(functions, 0x04));
    xnet.setFunc21to28(address, buildFunctionGroupByte(functions, 0x05));

    DEBUG_XNET_PRINTF("XpressNet TX: Functions - Addr=%u Fn=0x%08lx\n", address, (unsigned long)functions);
}

void XpressNetInterface::sendEmergencyStop() {
    xnet.setPower(csEmergencyStop);
    DEBUG_XNET_PRINTF("XpressNet TX: Emergency stop broadcast (Ecos-originated)\n");
}

void XpressNetInterface::sendResumeOperation() {
    xnet.setPower(csNormal);
    DEBUG_XNET_PRINTF("XpressNet TX: Resume operation broadcast (Ecos-originated)\n");
}

void XpressNetInterface::pushLocoStateToOwningSlot(uint16_t address) {
    if (!isValidDccAddress(address)) {
        return;
    }

    uint8_t speed_byte;
    uint32_t functions;
    resolveLocoStateForReply(address, speed_byte, functions);

    xnet.PushExternalLocoUpdate(address, Loco128, speed_byte, buildFunctionGroupByte(functions, 1));

    DEBUG_XNET_PRINTF("XpressNet TX: Pushed external update to owning slot (if any) - Addr=%u\n", address);
}

unsigned long XpressNetInterface::getLastMessageAgeMs() const {
    if (last_message_time == 0) {
        return NO_TIMESTAMP;
    }
    return millis() - last_message_time;
}

// ============================================================================
// OUTGOING FUNCTION GROUP ENCODING (mirrors onLocoFunctionGroup's decode)
// ============================================================================

uint8_t XpressNetInterface::buildFunctionGroupByte(uint32_t functions, uint8_t group) {
    switch (group) {
        case 1: {
            uint8_t b = 0;
            if (functions & (1UL << 0)) b |= (1 << 4);  // F0 -> bit4
            if (functions & (1UL << 1)) b |= (1 << 0);  // F1 -> bit0
            if (functions & (1UL << 2)) b |= (1 << 1);  // F2 -> bit1
            if (functions & (1UL << 3)) b |= (1 << 2);  // F3 -> bit2
            if (functions & (1UL << 4)) b |= (1 << 3);  // F4 -> bit3
            return b;
        }
        case 2:
            return (uint8_t)((functions >> 5) & 0x0F);   // F5-F8
        case 3:
            return (uint8_t)((functions >> 9) & 0x0F);   // F9-F12
        case 0x04:
            return (uint8_t)((functions >> 13) & 0xFF);  // F13-F20
        case 0x05:
            return (uint8_t)((functions >> 21) & 0xFF);  // F21-F28
        default:
            return 0;
    }
}
