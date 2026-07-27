/*
 * XpressNet Test Message Fixtures
 *
 * Hardcoded valid and invalid XpressNet messages for unit testing.
 * Format: [Address High] [Address Low] [Data] [Checksum]
 */

#ifndef XPRESSNET_MESSAGES_H
#define XPRESSNET_MESSAGES_H

#include <cstdint>

// ============================================================================
// VALID SPEED COMMANDS
// ============================================================================

// Speed command: Loco 100, Speed 64 (mid-speed), Forward direction
// Checksum: 0x00 ^ 100 ^ 0x40 = 0x24
static const uint8_t XNET_SPEED_100_MID_FORWARD[] = {0x00, 100, 0x40, 0x24};

// Speed command: Loco 50, Speed 0 (stop), Forward
// Checksum: 0x00 ^ 50 ^ 0x00 = 0x32
static const uint8_t XNET_SPEED_50_STOP[] = {0x00, 50, 0x00, 0x32};

// Speed command: Loco 99, Speed 126 (max speed), Forward
// Checksum: 0x00 ^ 99 ^ 0x7E = 0x1D
static const uint8_t XNET_SPEED_99_MAX[] = {0x00, 99, 0x7E, 0x1D};

// Speed command: Loco 1, Speed 64 (mid), Reverse (bit 7 set)
// Checksum: 0x00 ^ 1 ^ 0xC0 = 0xC1
static const uint8_t XNET_SPEED_1_MID_REVERSE[] = {0x00, 1, 0xC0, 0xC1};

// Speed command: Loco 200 (long address), Speed 100
// Bit 6 (0x40) of the high byte means SHORT address (see isShortAddress()) -
// it must be CLEAR here. 200 fits in the low byte alone (200 < 256), so the
// high byte just needs the upper address bits, which are 0 for this address.
// Checksum: 0x00 ^ 200 ^ 0x64 = 0xAC
static const uint8_t XNET_SPEED_200_LONG_ADDR[] = {0x00, 200, 0x64, 0xAC};

// ============================================================================
// EMERGENCY STOP
// ============================================================================

// E-stop: Loco 100 (speed = 127 means e-stop)
// Checksum: 0x00 ^ 100 ^ 0x7F = 0x1B
static const uint8_t XNET_ESTOP_100[] = {0x00, 100, 0x7F, 0x1B};

// ============================================================================
// FUNCTION COMMANDS (F0-F7)
// ============================================================================

// Function command: Loco 100, F0=on, F1-F7=off
// Bit 5 (0x20) of the address-high byte marks this as a FUNCTION message
// (see determineCommandType/extractAddress) - address bits themselves stay 0.
// Checksum: 0x20 ^ 100 ^ 0x01 = 0x45
static const uint8_t XNET_FUNC_100_F0_ON[] = {0x20, 100, 0x01, 0x45};

// Function command: Loco 50, F0=on, F1=on, F2-F7=off
// Checksum: 0x20 ^ 50 ^ 0x03 = 0x11
static const uint8_t XNET_FUNC_50_F0F1_ON[] = {0x20, 50, 0x03, 0x11};

// Function command: Loco 200, F0-F7 all on (0xFF)
// Bit 6 (0x40) must stay CLEAR - it marks SHORT address, not long (see
// XNET_SPEED_200_LONG_ADDR above for why). Bit 5 (0x20) marks FUNCTION.
// Checksum: 0x20 ^ 200 ^ 0xFF = 0x17
static const uint8_t XNET_FUNC_200_ALL_ON[] = {0x20, 200, 0xFF, 0x17};

// ============================================================================
// INVALID MESSAGES
// ============================================================================

// Bad checksum: Loco 100, Speed 64, wrong checksum (should be 0x40, is 0x41)
static const uint8_t XNET_BAD_CHECKSUM[] = {0x00, 100, 0x40, 0x41};

// Incomplete message (only 2 bytes)
static const uint8_t XNET_INCOMPLETE[] = {0x00, 100};

// Too short after minimum
static const uint8_t XNET_SHORT[] = {0x00};

// ============================================================================
// MESSAGE SIZES
// ============================================================================

static const int XNET_MSG_SIZE_VALID = 4;
static const int XNET_MSG_SIZE_INCOMPLETE = 2;
static const int XNET_MSG_SIZE_SHORT = 1;

#endif  // XPRESSNET_MESSAGES_H
