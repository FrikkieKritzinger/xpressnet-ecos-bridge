/*
 * XpressNet Message Parser Implementation
 *
 * Parses binary XpressNet protocol messages into structured command objects.
 * Handles checksum validation, format validation, and extraction of
 * address, speed, direction, and function bits from raw message bytes.
 */

#include "xpressnet_message_parser.h"
#include "../../utils/debug.h"
#include <cstring>

// ============================================================================
// PUBLIC API - MAIN PARSING METHOD
// ============================================================================

bool XNetMessageParser::parse(const uint8_t* buffer, uint8_t length, XNetCommand& cmd) {
    // Validate input
    if (!buffer || length < 4) {
        cmd.type = XNetCommand::INVALID;
        return false;
    }

    // Store raw message for debugging
    cmd.raw_len = length;
    memcpy(cmd.raw_data, buffer, length);

    // Validate checksum
    if (!isValidMessage(buffer, length)) {
        DEBUG_XNET_PRINTF("XpressNet: Checksum error, dropping message\n");
        cmd.type = XNetCommand::INVALID;
        return false;
    }

    // Determine message type
    cmd.type = determineCommandType(buffer, length);

    if (cmd.type == XNetCommand::INVALID) {
        return false;
    }

    // Extract address
    cmd.address = extractAddress(buffer[0], buffer[1]);

    if (!isValidDccAddress(cmd.address)) {
        DEBUG_XNET_PRINTF("XpressNet: Invalid address %u\n", cmd.address);
        cmd.type = XNetCommand::INVALID;
        return false;
    }

    // Parse based on type
    switch (cmd.type) {
        case XNetCommand::SPEED: {
            bool is_estop = false;
            extractSpeedDirection(buffer[2], cmd.speed, cmd.direction, is_estop);

            if (is_estop) {
                cmd.type = XNetCommand::EMERGENCY_STOP;
            }
            return true;
        }

        case XNetCommand::EMERGENCY_STOP: {
            bool is_estop = false;
            extractSpeedDirection(buffer[2], cmd.speed, cmd.direction, is_estop);
            return is_estop;
        }

        case XNetCommand::FUNCTION: {
            // Function data in buffer[2]
            // Determine which function range (F0-F7, F8-F15, etc.)
            uint8_t function_range = 0;  // Default F0-F7
            if (length >= 5) {
                // Could extract range from higher byte if needed
                // For now, assume range 0
            }
            cmd.functions = extractFunctions(buffer[2], function_range);
            return true;
        }

        case XNetCommand::STATUS:
            // Status message - minimal parsing for now
            if (length >= 4) {
                cmd.functions = buffer[2];  // Device info in this byte
                return true;
            }
            return false;

        default:
            return false;
    }
}

// ============================================================================
// CHECKSUM CALCULATION & VALIDATION
// ============================================================================

uint8_t XNetMessageParser::calculateChecksum(const uint8_t* buffer, uint8_t length) {
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < length; i++) {
        checksum ^= buffer[i];  // XOR of all bytes
    }
    return checksum;
}

bool XNetMessageParser::isValidMessage(const uint8_t* buffer, uint8_t length) {
    if (length < 4) {
        return false;  // Minimum message is 4 bytes
    }

    // Calculate expected checksum for all bytes except last
    uint8_t calculated = calculateChecksum(buffer, length - 1);

    // Compare with actual checksum (last byte)
    uint8_t actual = buffer[length - 1];

    return (calculated == actual);
}

// ============================================================================
// ADDRESS EXTRACTION
// ============================================================================

uint16_t XNetMessageParser::extractAddress(uint8_t high, uint8_t low) {
    // XpressNet address format:
    // Short address (1-99): encoded in low byte
    // Long address (100-9999): split across high and low bytes
    //
    // Simplified implementation: treat as 16-bit address
    // High byte may contain format/type info

    // Check if short address (bit 6 of high byte indicates short address)
    bool is_short = isShortAddress(high);

    if (is_short) {
        // Short address (1-99) - in low byte, high byte has type info
        return (uint16_t)(low & 0x7F);  // Mask out any control bits
    } else {
        // Long address (100-9999). Strip known marker/flag bits from the
        // high byte before reconstructing - 0x40 (short-address flag) and
        // 0x20 (function-command flag, see determineCommandType) are not
        // part of the address value itself.
        uint8_t high_address_bits = high & ~(0x40 | 0x20);
        uint16_t address = ((uint16_t)high_address_bits << 8) | low;
        address &= 0x3FFF;  // 14-bit address field
        return address;
    }
}

bool XNetMessageParser::isShortAddress(uint8_t high_byte) {
    // XpressNet encodes short addresses with bit 6 set in high byte
    return (high_byte & 0x40) != 0;
}

// ============================================================================
// SPEED & DIRECTION EXTRACTION
// ============================================================================

void XNetMessageParser::extractSpeedDirection(uint8_t data,
                                             uint8_t& speed,
                                             uint8_t& direction,
                                             bool& is_estop) {
    // XpressNet speed format:
    // Bit 7: Direction (0=forward, 1=reverse)
    // Bits 6-0: Speed (0-126, with 127=E-stop)

    // Extract speed (bits 6-0)
    speed = data & 0x7F;

    // Extract direction (bit 7)
    // Note: XpressNet uses opposite convention to DCC
    // XpressNet: 0=forward, 1=reverse
    // Our system: 0=reverse, 1=forward
    // So we need to INVERT
    direction = (data & 0x80) ? 0 : 1;

    // Check for E-stop
    is_estop = (speed == 127);

    // Speed 127 is preserved as-is: it's the documented E-stop marker
    // (XNetCommand::speed doc: "0-126 (0=stop, 127=E-stop)"). CommandRouter
    // is responsible for converting E-stop to speed=0 when broadcasting.
}

// ============================================================================
// FUNCTION EXTRACTION
// ============================================================================

uint32_t XNetMessageParser::extractFunctions(uint8_t data, uint8_t function_range) {
    // XpressNet sends functions in 8-bit chunks
    // F0-F7: data byte
    // F8-F15: separate packet (if sent)
    // etc.

    uint32_t functions = 0;

    // Shift the byte to the correct position in the 32-bit bitmap
    // function_range 0 = bits 0-7 (F0-F7)
    // function_range 1 = bits 8-15 (F8-F15)
    // etc.
    functions = ((uint32_t)data) << (function_range * 8);

    return functions;
}

// ============================================================================
// MESSAGE TYPE DETECTION
// ============================================================================

XNetCommand::Type XNetMessageParser::determineCommandType(const uint8_t* buffer, uint8_t length) {
    if (length < 4) {
        return XNetCommand::INVALID;
    }

    uint8_t data_byte = buffer[2];

    // Function command: bit 5 (0x20) of the address-high byte marks this as
    // a function message rather than a speed message. Must be checked FIRST:
    // a function bitmap byte (e.g. 0xFF, all F0-F7 on) can otherwise collide
    // with the E-stop/speed patterns below, which only look at data_byte.
    if (length == 4 && (buffer[0] & 0x20) != 0) {
        return XNetCommand::FUNCTION;
    }

    // E-stop: speed = 127 (bits 6-0 = 1111111)
    if ((data_byte & 0x7F) == 0x7F) {
        return XNetCommand::EMERGENCY_STOP;
    }

    // Speed command: typical data byte with speed in bits 6-0
    // (data & 0x7F) is the speed value
    if ((data_byte & 0x7F) <= 126) {
        return XNetCommand::SPEED;
    }

    // Status message: specific address patterns or length markers
    if (length >= 5 && (buffer[0] & 0x0F) == 0x00 && (buffer[1] & 0x0F) == 0x00) {
        // Broadcast/status message
        return XNetCommand::STATUS;
    }

    return XNetCommand::INVALID;
}
