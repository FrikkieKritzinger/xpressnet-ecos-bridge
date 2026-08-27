/*
 * Z21 LAN Protocol - Pure Packet Encode/Decode Logic
 *
 * Phase 6 step 4. Confirmed against the official Z21 LAN Protocol
 * Specification v1.13 (docs/z21-lan-protokoll-en.pdf, gitignored -
 * redistribution-restricted like the ESU Ecos PDF - consult that before
 * changing any of this).
 *
 * No Arduino/WiFiUDP dependency - operates on plain byte buffers, so this
 * is fully native-testable. The actual UDP socket handling and client
 * session table live in z21lan_interface.h/.cpp instead, which is
 * Arduino-only like ecos_interface.cpp/setup_mode.cpp.
 *
 * Byte order: all multi-byte fields are little-endian (per spec section
 * 1.2.1), except within a single X-Bus sub-packet where byte order
 * follows the tables in the spec directly (Adr_MSB before Adr_LSB, etc).
 */

#ifndef Z21_PROTOCOL_H
#define Z21_PROTOCOL_H

#include <cstdint>
#include <cstddef>

// ============================================================================
// Z21-LAN HEADERS (top-level dataset header, not the X-Bus sub-header)
// ============================================================================

#define Z21_HEADER_LAN_X                0x0040  // X-Bus protocol tunneling (LAN_X_xxx)
#define Z21_HEADER_GET_SERIAL_NUMBER    0x0010
#define Z21_HEADER_LOGOFF               0x0030
#define Z21_HEADER_SET_BROADCASTFLAGS   0x0050

// ============================================================================
// X-BUS SUB-HEADERS (first byte of Data when Header == Z21_HEADER_LAN_X)
// ============================================================================

// X-Header 0x21 is shared by four distinct requests, disambiguated by
// DB0: GET_VERSION(0x21), GET_STATUS(0x24), SET_TRACK_POWER_OFF(0x80),
// SET_TRACK_POWER_ON(0x81) - one named constant for the shared header,
// used with the DB0 sub-codes below rather than four aliases for the
// same numeric value.
#define Z21_X_HEADER_SYSTEM         0x21
#define Z21_X_DB0_GET_VERSION       0x21
#define Z21_X_DB0_GET_STATUS        0x24
#define Z21_X_DB0_TRACK_POWER_OFF   0x80
#define Z21_X_DB0_TRACK_POWER_ON    0x81
#define Z21_X_BC_TRACK_POWER        0x61  // + DB0 0x00(off)/0x01(on)
#define Z21_X_STATUS_CHANGED        0x62
#define Z21_X_SET_STOP              0x80
#define Z21_X_BC_STOPPED            0x81
#define Z21_X_GET_FIRMWARE_VERSION  0xF1
#define Z21_X_FIRMWARE_VERSION      0xF3
#define Z21_X_GET_LOCO_INFO         0xE3  // + DB0 0xF0
#define Z21_X_SET_LOCO_DRIVE        0xE4  // + DB0 0x1S
#define Z21_X_SET_LOCO_FUNCTION     0xE4  // + DB0 0xF8
#define Z21_X_LOCO_INFO             0xEF
#define Z21_X_UNKNOWN_COMMAND       0x61  // + DB0 0x82

// ============================================================================
// SPEED STEP MODES (the 0x1S nibble in LAN_X_SET_LOCO_DRIVE's DB0)
// ============================================================================

#define Z21_SPEED_STEPS_14   0
#define Z21_SPEED_STEPS_28   2
#define Z21_SPEED_STEPS_128  3

/**
 * XOR checksum over a byte range - Z21's X-Bus tunneled packets end with
 * one, computed as the XOR of every byte from the X-Header through the
 * last data byte (not including DataLen/Header, not including itself).
 */
uint8_t z21Checksum(const uint8_t* data, size_t len);

/**
 * Decode a Z21 loco address from its two-byte wire encoding.
 * @param adr_msb First byte (top 2 bits are the >=128 marker, ignored here)
 * @param adr_lsb Second byte
 */
uint16_t z21DecodeAddress(uint8_t adr_msb, uint8_t adr_lsb);

/**
 * Encode a DCC address into Z21's two-byte wire format, setting the
 * 0xC0 high-bit marker on the MSB for addresses >= 128 per spec
 * convention (the spec says receivers must ignore these bits anyway,
 * but setting them matches how real Z21 traffic looks).
 */
void z21EncodeAddress(uint16_t address, uint8_t& adr_msb, uint8_t& adr_lsb);

/**
 * Decode a LAN_X_SET_LOCO_DRIVE speed byte (RVVVVVVV, meaning dependent
 * on step_mode) into direction + this bridge's internal 0-126 speed
 * range (DCC_MAX_SPEED, definitions.h) - the same range XpressNet
 * already uses, so downstream code (CommandRouter, Ecos) needs no
 * per-protocol special-casing.
 *
 * Both "Stop" and "E-Stop" wire codes decode to speed 0 - this bridge
 * doesn't model a distinct emergency-stop speed value (same
 * simplification already made for XpressNet's own 128-step handling),
 * and treating E-Stop as "stop" is the safe direction to simplify in,
 * unlike silently dropping the command (which would leave a loco
 * still moving on what the operator intended as an emergency stop).
 *
 * @param step_mode One of Z21_SPEED_STEPS_14/28/128
 * @param speed_byte The RVVVVVVV wire byte
 * @param out_direction 1=forward, 0=reverse
 * @param out_speed 0-126
 * @return false if step_mode is unrecognized (out params left unset)
 */
bool z21DecodeSpeed(uint8_t step_mode, uint8_t speed_byte, uint8_t& out_direction, uint8_t& out_speed);

/**
 * Encode this bridge's internal direction + 0-126 speed into a Z21
 * RVVVVVVV byte using 128-step encoding (KKK=4) - LAN_X_LOCO_INFO
 * replies always report speed steps this way regardless of what a
 * client last sent, since 128-step is the finest-resolution superset
 * and our own internal state is already a 1:1 fit for it (no lossy
 * rescale needed on the way out).
 */
uint8_t z21EncodeSpeed128(uint8_t direction, uint8_t speed);

/**
 * Decode a LAN_X_SET_LOCO_FUNCTION data byte (TTNNNNNN) into a function
 * index (0-31) and switch type. TT: 0=off, 1=on, 2=toggle, 3=invalid.
 * @return false if TT was the invalid 3 (0b11) value
 */
bool z21DecodeFunctionCommand(uint8_t data_byte, uint8_t& out_function_index, uint8_t& out_switch_type);

#define Z21_FUNCTION_SWITCH_OFF    0
#define Z21_FUNCTION_SWITCH_ON     1
#define Z21_FUNCTION_SWITCH_TOGGLE 2

/**
 * Build a LAN_X_LOCO_INFO reply/broadcast packet (the message pushed to
 * subscribed clients whenever a loco's state changes, and the reply to
 * LAN_X_GET_LOCO_INFO). Always reports 128 speed steps (see
 * z21EncodeSpeed128()) and F0-F28 (DB4-DB7) - F29-F31 (the optional
 * 15-byte DataLen variant) aren't populated, matching this bridge's
 * scope elsewhere (F0-F28 is already the practical ceiling XpressNet
 * handles too).
 * @param functions Bitmap F0-F31 (bit0=F0 ... bit31=F31), same
 *        convention used throughout this codebase
 * @return bytes written, or 0 if buffer_size was too small
 */
size_t z21BuildLocoInfo(uint8_t* buffer, size_t buffer_size, uint16_t address,
                        uint8_t direction, uint8_t speed, uint32_t functions);

/**
 * Build a LAN_X_BC_TRACK_POWER_OFF/ON broadcast packet.
 * @param power_on false = OFF, true = ON
 */
size_t z21BuildTrackPowerBroadcast(uint8_t* buffer, size_t buffer_size, bool power_on);

/**
 * Build a LAN_X_BC_STOPPED broadcast packet (emergency stop notification).
 */
size_t z21BuildStoppedBroadcast(uint8_t* buffer, size_t buffer_size);

/**
 * Build a LAN_GET_SERIAL_NUMBER reply. This bridge doesn't have a real
 * Z21 serial number - uses a fixed placeholder value, since clients only
 * use this for identification/discovery bookkeeping, not validation.
 */
size_t z21BuildSerialNumberReply(uint8_t* buffer, size_t buffer_size);

/**
 * Build a LAN_X_STATUS_CHANGED reply (reply to LAN_X_GET_STATUS) -
 * reports track power on, no emergency stop, no short circuit, no
 * programming mode (this bridge doesn't support CV programming - see
 * CLAUDE.md's Phase 5 planning notes on why that's out of scope
 * entirely, not just deferred).
 * @param emergency_stop_active Reflects this bridge's own E-stop state
 */
size_t z21BuildStatusChangedReply(uint8_t* buffer, size_t buffer_size, bool emergency_stop_active);

/**
 * Build a LAN_X_GET_FIRMWARE_VERSION reply. Reports a fixed placeholder
 * version (1.30 - a real, unremarkable Z21 firmware version number),
 * not this bridge's own FIRMWARE_VERSION (version.h) - some Z21 clients
 * may gate feature availability on the reported firmware version, and a
 * plausible real-looking value is safer than an arbitrary one.
 */
size_t z21BuildFirmwareVersionReply(uint8_t* buffer, size_t buffer_size);

/**
 * Build a LAN_X_UNKNOWN_COMMAND reply, sent for any request this bridge
 * doesn't recognize/support - lets a client know its request was
 * received but not understood, rather than silently ignored.
 */
size_t z21BuildUnknownCommandReply(uint8_t* buffer, size_t buffer_size);

#endif  // Z21_PROTOCOL_H
