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
// Checksum calculation: 100 ^ 0 ^ 0x40 = 0x40
static const uint8_t XNET_SPEED_100_MID_FORWARD[] = {0x00, 100, 0x40, 0x40};

// Speed command: Loco 50, Speed 0 (stop), Forward
// Checksum: 50 ^ 0 ^ 0x00 = 0x32
static const uint8_t XNET_SPEED_50_STOP[] = {0x00, 50, 0x00, 0x32};

// Speed command: Loco 99, Speed 126 (max speed), Forward
// Checksum: 99 ^ 0 ^ 0x7E = 0x7F
static const uint8_t XNET_SPEED_99_MAX[] = {0x00, 99, 0x7E, 0x7F};

// Speed command: Loco 1, Speed 64 (mid), Reverse (bit 7 set)
// Checksum: 1 ^ 0 ^ 0xC0 = 0xC1
static const uint8_t XNET_SPEED_1_MID_REVERSE[] = {0x00, 1, 0xC0, 0xC1};

// Speed command: Loco 200 (long address), Speed 100
// Long address encoding: high byte has bit 6 set, plus address upper bits
// Address 200 = 0x00C8; encoded as long addr: high=(0x40 | (200>>8)) = 0x40, low=200&0xFF
// Checksum: 0x40 ^ 200 ^ 0x64 = ...
static const uint8_t XNET_SPEED_200_LONG_ADDR[] = {0x40, 200, 0x64, 0x24};

// ============================================================================
// EMERGENCY STOP
// ============================================================================

// E-stop: Loco 100 (speed = 127 means e-stop)
// Checksum: 100 ^ 0 ^ 0x7F = 0x7F
static const uint8_t XNET_ESTOP_100[] = {0x00, 100, 0x7F, 0x7F};

// ============================================================================
// FUNCTION COMMANDS (F0-F7)
// ============================================================================

// Function command: Loco 100, F0=on, F1-F7=off
// Checksum: 100 ^ 0 ^ 0x01 = 0x65
static const uint8_t XNET_FUNC_100_F0_ON[] = {0x00, 100, 0x01, 0x65};

// Function command: Loco 50, F0=on, F1=on, F2-F7=off
// Checksum: 50 ^ 0 ^ 0x03 = 0x51
static const uint8_t XNET_FUNC_50_F0F1_ON[] = {0x00, 50, 0x03, 0x51};

// Function command: Loco 200, F0-F7 all on (0xFF)
// Checksum: 0x40 ^ 200 ^ 0xFF = 0xBF
static const uint8_t XNET_FUNC_200_ALL_ON[] = {0x40, 200, 0xFF, 0xBF};

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
