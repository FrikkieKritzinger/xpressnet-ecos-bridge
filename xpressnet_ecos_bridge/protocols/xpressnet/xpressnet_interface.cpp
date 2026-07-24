/*
 * XpressNet Interface Implementation - Master Device
 *
 * Handles all XpressNet protocol communication:
 * - Receives commands from throttles via RS485 serial
 * - Parses binary XpressNet messages
 * - Routes commands through state engine and command router
 * - Sends speed/direction and function updates to bus
 * - Manages connection status and timeouts
 *
 * Non-blocking, cooperative multitasking design.
 * Update must be fast enough to not miss message windows (~20-50ms).
 */

#include "xpressnet_interface.h"
#include "xpressnet_message_parser.h"
#include "../../command_router.h"
#include "../../config.h"
#include "../../definitions.h"
#include "../../utils/debug.h"
#include <Arduino.h>

// ============================================================================
// CONSTRUCTOR & INITIALIZATION
// ============================================================================

XpressNetInterface::XpressNetInterface()
    : current_status(ComponentStatus::DISCONNECTED),
      device_count(0),
      last_message_time(0),
      bus_connect_time(0),
      serial(&Serial1),
      buffer_index(0),
      router(nullptr) {
    memset(message_buffer, 0, sizeof(message_buffer));
    memset(&current_command, 0, sizeof(current_command));
}

XpressNetInterface::~XpressNetInterface() {
    // Cleanup if needed
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool XpressNetInterface::begin() {
    #if ENABLE_XPRESSNET
        DEBUG_XNET_PRINTF("Initializing XpressNet interface...\n");

        // Configure RS485 control pins
        pinMode(XPRESSNET_DE_PIN, OUTPUT);  // Driver Enable
        pinMode(XPRESSNET_RX_PIN, INPUT);   // RX
        pinMode(XPRESSNET_TX_PIN, OUTPUT);  // TX

        // Start in receiver mode (DE=LOW, no explicit RE needed if tied to ground)
        digitalWrite(XPRESSNET_DE_PIN, LOW);

        // Initialize Serial1 for XpressNet (9600 baud, 8N1)
        // XpressNet uses 9-bit protocol, but Arduino handles it at 8-bit level
        // ESP8266 Serial1: RX fixed to D7 (GPIO13), TX configurable (default D8/GPIO15)
        Serial1.begin(XPRESSNET_BAUD, SERIAL_8N1, SERIAL_FULL, XPRESSNET_TX_PIN);

        if (!Serial1) {
            DEBUG_XNET_PRINTF("ERROR: Serial1 initialization failed!\n");
            current_status = ComponentStatus::ERROR;
            return false;
        }

        // Set status to CONNECTING and reset timer
        current_status = ComponentStatus::CONNECTING;
        last_message_time = millis();
        bus_connect_time = 0;

        DEBUG_XNET_PRINTF("XpressNet interface initialized (baud=%u, pins RX=%u TX=%u)\n",
                         XPRESSNET_BAUD, XPRESSNET_RX_PIN, XPRESSNET_TX_PIN);

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

    // Update bus status (check for timeouts, etc.)
    updateBusStatus();

    // Process all available incoming messages (non-blocking)
    // XpressNet has tight message timing (~20-50ms windows)
    // So we must process quickly and not block
    while (processIncomingMessage()) {
        // Continue processing while messages available
        // Each iteration handles one complete message
        yield();  // Let ESP8266 handle background tasks
    }
}

// ============================================================================
// INCOMING MESSAGE PROCESSING
// ============================================================================

bool XpressNetInterface::processIncomingMessage() {
    // Non-blocking serial read - check if data available
    if (!Serial1.available()) {
        return false;  // No data available
    }

    // Read one byte
    int byte_value = Serial1.read();
    if (byte_value < 0) {
        return false;  // Read failed
    }

    uint8_t byte = (uint8_t)byte_value;

    // XpressNet message format:
    // Typical: [Addr_High][Addr_Low][Data][Checksum]
    // Length: 3-6 bytes depending on message type

    // Simple state machine to accumulate bytes
    if (buffer_index >= MAX_XNET_MESSAGE_LENGTH) {
        // Buffer full - reset (message too long)
        buffer_index = 0;
        return false;
    }

    message_buffer[buffer_index++] = byte;

    // Try to parse complete message
    // Minimum message is 4 bytes (address high/low + data + checksum)
    if (buffer_index < 4) {
        return false;  // Incomplete message
    }

    // Check if message appears complete
    // For now, assume 4-byte messages are most common
    // (Speed commands: addr_hi, addr_lo, speed_dir, checksum)
    if (buffer_index >= 4) {
        // Attempt to parse
        if (XNetMessageParser::parse(message_buffer, buffer_index, current_command)) {
            // Valid message parsed!
            buffer_index = 0;  // Reset for next message

            // Update timing
            last_message_time = millis();
            if (bus_connect_time == 0) {
                bus_connect_time = last_message_time;
            }

            // Route the command through router
            handleCommand(current_command);

            return true;  // Successfully processed one message
        } else if (buffer_index >= 6) {
            // Could be longer message - try parsing as 6 bytes
            if (XNetMessageParser::parse(message_buffer, buffer_index, current_command)) {
                buffer_index = 0;
                last_message_time = millis();
                handleCommand(current_command);
                return true;
            } else {
                // Message doesn't parse - skip first byte and try again
                // (resynchronize to message boundary)
                for (int i = 0; i < buffer_index - 1; i++) {
                    message_buffer[i] = message_buffer[i + 1];
                }
                buffer_index--;
                return false;
            }
        }
    }

    return false;  // Message still being accumulated
}

// ============================================================================
// BUS STATUS & TIMEOUT MANAGEMENT
// ============================================================================

void XpressNetInterface::updateBusStatus() {
    unsigned long now = millis();
    unsigned long time_since_message = now - last_message_time;

    if (current_status == ComponentStatus::CONNECTING) {
        // Still waiting for first message
        if (time_since_message > 3000) {
            // 3 seconds without any message while connecting
            current_status = ComponentStatus::DISCONNECTED;
            DEBUG_XNET_PRINTF("XpressNet: Bus disconnected (no initial activity)\n");
        }
    } else if (current_status == ComponentStatus::CONNECTED) {
        // Connected - watch for timeout
        if (time_since_message > BUS_TIMEOUT) {
            current_status = ComponentStatus::DISCONNECTED;
            device_count = 0;
            bus_connect_time = 0;
            DEBUG_XNET_PRINTF("XpressNet: Bus timeout (no messages for %lu ms)\n", BUS_TIMEOUT);
        }
    }
}

// ============================================================================
// COMMAND HANDLING
// ============================================================================

void XpressNetInterface::handleCommand(const XNetCommand& cmd) {
    // Validate command
    if (cmd.type == XNetCommand::INVALID || cmd.type == XNetCommand::UNKNOWN) {
        DEBUG_XNET_PRINTF("XpressNet: Ignoring invalid command\n");
        return;
    }

    // Validate address
    if (!isValidDccAddress(cmd.address)) {
        DEBUG_XNET_PRINTF("XpressNet: Invalid address %u\n", cmd.address);
        return;
    }

    // Update connection status on first valid message
    if (current_status == ComponentStatus::CONNECTING) {
        current_status = ComponentStatus::CONNECTED;
        DEBUG_XNET_PRINTF("XpressNet: Bus connected! First message from device\n");
    }

    // Route based on command type
    if (!router) {
        DEBUG_XNET_PRINTF("XpressNet: WARNING - router not set, dropping command\n");
        return;
    }

    switch (cmd.type) {
        case XNetCommand::SPEED:
            DEBUG_XNET_PRINTF("XpressNet RX: Speed - Addr=%u Speed=%u Dir=%u\n",
                            cmd.address, cmd.speed, cmd.direction);
            // Route to command router (updates state and broadcasts to Ecos)
            router->handleXpressNetCommand(cmd.address, cmd.speed, cmd.direction);
            break;

        case XNetCommand::EMERGENCY_STOP:
            DEBUG_XNET_PRINTF("XpressNet RX: E-STOP - Addr=%u\n", cmd.address);
            // E-stop: set speed to 0
            router->handleXpressNetCommand(cmd.address, 0, cmd.direction);
            break;

        case XNetCommand::FUNCTION:
            DEBUG_XNET_PRINTF("XpressNet RX: Function - Addr=%u Fn=0x%08x\n",
                            cmd.address, cmd.functions);
            // Route function command
            router->handleXpressNetFunctionCommand(cmd.address, cmd.functions);
            break;

        case XNetCommand::STATUS:
            // Status message (e.g., device list)
            updateDeviceCount(cmd);
            break;

        default:
            break;
    }
}

// ============================================================================
// OUTGOING COMMANDS - BROADCAST TO BUS
// ============================================================================

void XpressNetInterface::sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    if (current_status != ComponentStatus::CONNECTED) {
        // Not connected - queue or drop
        return;
    }

    if (!isValidDccAddress(address) || !isValidSpeed(speed) || !isValidDirection(direction)) {
        DEBUG_XNET_PRINTF("XpressNet TX: Invalid speed command - addr=%u speed=%u dir=%u\n",
                         address, speed, direction);
        return;
    }

    uint8_t packet[MAX_XNET_MESSAGE_LENGTH];
    uint8_t len = buildSpeedPacket(packet, address, speed, direction);

    sendPacket(packet, len);

    DEBUG_XNET_PRINTF("XpressNet TX: Speed - Addr=%u Speed=%u Dir=%u\n",
                     address, speed, direction);
}

void XpressNetInterface::sendFunctionCommand(uint16_t address, uint32_t functions) {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    if (!isValidDccAddress(address)) {
        return;
    }

    // XpressNet sends functions in groups (F0-F7, F8-F15, etc.)
    // Send all function ranges that have changes
    uint8_t packet[MAX_XNET_MESSAGE_LENGTH];

    for (uint8_t range = 0; range < 4; range++) {
        uint8_t len = buildFunctionPacket(packet, address, functions, range);
        if (len > 0) {
            sendPacket(packet, len);
        }
    }

    DEBUG_XNET_PRINTF("XpressNet TX: Functions - Addr=%u Fn=0x%08x\n",
                     address, functions);
}

// ============================================================================
// PACKET BUILDING
// ============================================================================

uint8_t XpressNetInterface::buildSpeedPacket(uint8_t* buffer, uint16_t address,
                                            uint8_t speed, uint8_t direction) {
    // XpressNet speed packet format:
    // [Addr_High][Addr_Low][Speed_Dir][Checksum]

    if (!buffer) return 0;

    // Split address into high and low bytes
    uint8_t addr_high = (address >> 8) & 0xFF;
    uint8_t addr_low = address & 0xFF;

    // Encode speed and direction
    // Bit 7: direction (0=forward, 1=reverse)
    // Bits 6-0: speed (0-126)
    uint8_t speed_byte = speed;
    if (direction == 0) {
        // Forward - clear bit 7
        speed_byte &= 0x7F;
    } else {
        // Reverse - set bit 7
        speed_byte |= 0x80;
    }

    buffer[0] = addr_high;
    buffer[1] = addr_low;
    buffer[2] = speed_byte;
    buffer[3] = XNetMessageParser::calculateChecksum(buffer, 3);

    return 4;  // 4-byte message
}

uint8_t XpressNetInterface::buildFunctionPacket(uint8_t* buffer, uint16_t address,
                                               uint32_t functions, uint8_t function_range) {
    // XpressNet function packets - one per 8 functions (F0-F7, F8-F15, etc.)
    // Format: [Addr_High][Addr_Low][Fn_Bits][Checksum]

    if (!buffer || function_range > 3) return 0;

    uint8_t addr_high = (address >> 8) & 0xFF;
    uint8_t addr_low = address & 0xFF;

    // Extract the relevant function bits for this range
    // F0-F7: bits 0-7
    // F8-F15: bits 8-15
    // F16-F23: bits 16-23
    // F24-F31: bits 24-31
    uint8_t fn_byte = (functions >> (function_range * 8)) & 0xFF;

    buffer[0] = addr_high;
    buffer[1] = addr_low;
    buffer[2] = fn_byte;
    buffer[3] = XNetMessageParser::calculateChecksum(buffer, 3);

    return 4;  // 4-byte message
}

void XpressNetInterface::sendPacket(const uint8_t* buffer, uint8_t length) {
    if (!buffer || length == 0 || length > MAX_XNET_MESSAGE_LENGTH) {
        return;
    }

    // Switch to transmit mode (DE=HIGH)
    digitalWrite(XPRESSNET_DE_PIN, HIGH);

    // Send packet
    for (uint8_t i = 0; i < length; i++) {
        Serial1.write(buffer[i]);
    }

    // Flush transmission queue
    Serial1.flush();

    // Switch back to receive mode (DE=LOW)
    digitalWrite(XPRESSNET_DE_PIN, LOW);

    // Small delay to ensure we're in receive mode before next data arrives
    // (XpressNet throttles may respond quickly)
    delayMicroseconds(100);
}

// ============================================================================
// STATUS UPDATES
// ============================================================================

void XpressNetInterface::updateDeviceCount(const XNetCommand& cmd) {
    // Extract device count from status message if available
    // For now, just note that we received a status message
    // (Detailed parsing of status format depends on XpressNet implementation)
    (void)cmd;  // Placeholder
}
