/*
 * XpressNet Message Parser - Binary Protocol Handler
 *
 * Parses XpressNet binary messages received from throttles (slave devices).
 * XpressNet uses non-standard 9-bit serial at 9600 baud with RS485 physical layer.
 *
 * Protocol format (simplified):
 * - Speed command: [AddressHigh][AddressLow][SpeedDir][Checksum]
 * - Function command: [AddressHigh][AddressLow][FunctionBits][Checksum]
 * - Status messages: Device status, etc.
 */

#ifndef XPRESSNET_MESSAGE_PARSER_H
#define XPRESSNET_MESSAGE_PARSER_H

#include <cstdint>
#include "../../definitions.h"

// ============================================================================
// MESSAGE TYPES & CONSTANTS
// ============================================================================

// XpressNet message byte indices (typical packet structure)
#define XNET_MSG_ADDR_HIGH      0    // Address byte (high)
#define XNET_MSG_ADDR_LOW       1    // Address byte (low)
#define XNET_MSG_DATA           2    // Speed, direction, or function data
#define XNET_MSG_CHECKSUM       3    // Checksum (XOR of all bytes)

// XpressNet command types (encoded in message data byte)
#define XNET_CMD_SPEED_FWD      0x00  // Speed command, forward
#define XNET_CMD_SPEED_REV      0x80  // Speed command, reverse (bit 7 set)
#define XNET_CMD_EMERGENCY_STOP 0x7F  // Speed = 127 (special case)

// Function command bits (for F0-F31)
#define XNET_FN_F0_MASK         0x10  // F0 (lights)
#define XNET_FN_F1_F4_MASK      0x1F  // F1-F4 in lower 4 bits
#define XNET_FN_F5_F8_MASK      0x1F  // F5-F8
#define XNET_FN_F9_F12_MASK     0x1F  // F9-F12
// ... and so on for F13-F31 (may require multiple packets)

// ============================================================================
// PARSED COMMAND STRUCTURE
// ============================================================================

/**
 * Represents a parsed XpressNet command (not raw bytes, but meaningful data)
 */
struct XNetCommand {
    enum Type {
        UNKNOWN = 0,
        SPEED = 1,           // Speed and direction change
        EMERGENCY_STOP = 2,  // E-stop (speed 127)
        FUNCTION = 3,        // Function F0-F31 toggle
        INVALID = 4          // Malformed message
    };

    Type type;              // What type of command
    uint16_t address;       // DCC address (0-9999)
    uint8_t speed;          // 0-126 (0=stop, 127=E-stop)
    uint8_t direction;      // 0=reverse, 1=forward
    uint32_t functions;     // Bitmap of F0-F31 (1=on, 0=off)

    // For debugging/validation
    uint8_t raw_data[10];   // Raw message bytes
    uint8_t raw_len;        // Number of raw bytes
};

// ============================================================================
// MESSAGE PARSER CLASS
// ============================================================================

class XNetMessageParser {
public:
    /**
     * Parse a raw XpressNet message into a structured command
     * Validates checksum and format
     *
     * @param buffer Raw message bytes from serial port
     * @param length Number of bytes in buffer
     * @param[out] cmd Parsed command (populated if parsing succeeds)
     * @return true if parsed successfully, false if malformed/invalid
     */
    static bool parse(const uint8_t* buffer, uint8_t length, XNetCommand& cmd);

    /**
     * Calculate XpressNet checksum (XOR of all data bytes)
     * @param buffer Message bytes
     * @param length Number of bytes (not including checksum itself)
     * @return checksum byte
     */
    static uint8_t calculateChecksum(const uint8_t* buffer, uint8_t length);

    /**
     * Validate a complete message (length, checksum, format)
     * @param buffer Raw message bytes
     * @param length Number of bytes
     * @return true if valid, false if malformed
     */
    static bool isValidMessage(const uint8_t* buffer, uint8_t length);

    /**
     * Extract DCC address from XpressNet message bytes
     * XpressNet packs address as [AddressHigh][AddressLow]
     * @param high High byte
     * @param low Low byte
     * @return uint16_t DCC address (1-9999)
     */
    static uint16_t extractAddress(uint8_t high, uint8_t low);

    /**
     * Extract speed and direction from data byte
     * Bit 7 = direction (0=forward, 1=reverse)
     * Bits 6-0 = speed (0-126, or 127=E-stop)
     * @param data Data byte from message
     * @param[out] speed Speed value 0-126
     * @param[out] direction 0=forward, 1=reverse
     * @param[out] is_estop true if this is E-stop (127)
     */
    static void extractSpeedDirection(uint8_t data,
                                     uint8_t& speed,
                                     uint8_t& direction,
                                     bool& is_estop);

    /**
     * Extract function bits from data byte(s)
     * Different function ranges may come in separate packets
     * F0-F7 in one byte, F8-F15 in another, etc.
     * @param data Data byte
     * @param function_range 0=F0-F7, 1=F8-F15, 2=F16-F23, 3=F24-F31
     * @return uint32_t Bitmap of functions (shifted to correct bit positions)
     */
    static uint32_t extractFunctions(uint8_t data, uint8_t function_range);

    /**
     * Determine message type based on format
     * @param buffer Raw message
     * @param length Message length
     * @return CommandType (SPEED, FUNCTION, STATUS, etc.)
     */
    static XNetCommand::Type determineCommandType(const uint8_t* buffer, uint8_t length);

private:
    // No instances needed - all static methods
    XNetMessageParser() = delete;

    /**
     * Decode address encoding (may be short or long form)
     * @param high_byte First address byte
     * @param low_byte Second address byte
     * @return true if short address, false if long
     */
    static bool isShortAddress(uint8_t high_byte);
};

#endif  // XPRESSNET_MESSAGE_PARSER_H
