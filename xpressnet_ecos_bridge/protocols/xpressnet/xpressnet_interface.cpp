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
}

void XpressNetInterface::markBusActivity() {
    last_message_time = millis();
    if (bus_connect_time == 0) {
        bus_connect_time = last_message_time;
    }
    if (current_status == ComponentStatus::CONNECTING) {
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
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

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
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

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
