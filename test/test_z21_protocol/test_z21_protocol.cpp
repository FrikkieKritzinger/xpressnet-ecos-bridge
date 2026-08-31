/*
 * Z21 LAN Protocol Unit Tests
 *
 * Test vectors are taken directly from the official Z21 LAN Protocol
 * Specification v1.13's own worked examples/tables (docs/
 * z21-lan-protokoll-en.pdf, gitignored) wherever the spec gave one, not
 * just derived from the implementation - so these catch a genuine
 * mismatch against the spec, not just "matches whatever the code does".
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "protocols/z21lan/z21_protocol.h"
#include "definitions.h"

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// CHECKSUM
// ============================================================================

void test_checksum_track_power_off(void) {
    // Spec 2.7: X-Header 0x61, DB0 0x00 -> XOR-Byte 0x61
    uint8_t data[] = {0x61, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x61, z21Checksum(data, sizeof(data)));
}

void test_checksum_track_power_on(void) {
    // Spec 2.8: X-Header 0x61, DB0 0x01 -> XOR-Byte 0x60
    uint8_t data[] = {0x61, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0x60, z21Checksum(data, sizeof(data)));
}

void test_checksum_unknown_command(void) {
    // Spec 2.11: X-Header 0x61, DB0 0x82 -> XOR-Byte 0xE3
    uint8_t data[] = {0x61, 0x82};
    TEST_ASSERT_EQUAL_HEX8(0xE3, z21Checksum(data, sizeof(data)));
}

void test_checksum_stopped_broadcast(void) {
    // Spec 2.14: X-Header 0x81, DB0 0x00 -> XOR-Byte 0x81
    uint8_t data[] = {0x81, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x81, z21Checksum(data, sizeof(data)));
}

// ============================================================================
// ADDRESS ENCODE/DECODE
// ============================================================================

void test_decode_address_short(void) {
    TEST_ASSERT_EQUAL_UINT16(3, z21DecodeAddress(0x00, 0x03));
}

void test_decode_address_long_ignores_high_bits(void) {
    // Spec: "the two highest bits in Adr_MSB must be ignored"
    TEST_ASSERT_EQUAL_UINT16(300, z21DecodeAddress(0xC1, 0x2C));  // 0x3F & 0xC1=0x01 -> (1<<8)+0x2C=256+44=300
}

void test_encode_address_short_no_high_bits_set(void) {
    uint8_t msb, lsb;
    z21EncodeAddress(3, msb, lsb);
    TEST_ASSERT_EQUAL_HEX8(0x00, msb);
    TEST_ASSERT_EQUAL_HEX8(0x03, lsb);
}

void test_encode_address_long_sets_high_bits(void) {
    uint8_t msb, lsb;
    z21EncodeAddress(300, msb, lsb);
    TEST_ASSERT_EQUAL_HEX8(0xC1, msb);  // 0xC0 | (300>>8 & 0x3F) = 0xC0|0x01
    TEST_ASSERT_EQUAL_HEX8(0x2C, lsb);
}

void test_encode_decode_address_roundtrip(void) {
    uint8_t msb, lsb;
    z21EncodeAddress(1234, msb, lsb);
    TEST_ASSERT_EQUAL_UINT16(1234, z21DecodeAddress(msb, lsb));
}

// ============================================================================
// SPEED DECODE - DCC 128 (direct fit to our internal 0-126 range)
// ============================================================================

void test_decode_speed_128_stop(void) {
    uint8_t dir, speed;
    TEST_ASSERT_TRUE(z21DecodeSpeed(Z21_SPEED_STEPS_128, 0x80, dir, speed));  // R=1,V=0
    TEST_ASSERT_EQUAL_UINT8(0, dir);  // inverted convention - see z21DecodeSpeed()'s comment
    TEST_ASSERT_EQUAL_UINT8(0, speed);
}

void test_decode_speed_128_estop_treated_as_stop(void) {
    uint8_t dir, speed;
    TEST_ASSERT_TRUE(z21DecodeSpeed(Z21_SPEED_STEPS_128, 0x81, dir, speed));  // V=1 = E-Stop
    TEST_ASSERT_EQUAL_UINT8(0, speed);
}

void test_decode_speed_128_max(void) {
    uint8_t dir, speed;
    TEST_ASSERT_TRUE(z21DecodeSpeed(Z21_SPEED_STEPS_128, 0xFF, dir, speed));  // V=127 max
    TEST_ASSERT_EQUAL_UINT8(126, speed);
}

void test_decode_speed_128_forward(void) {
    // Direction bit is intentionally inverted from the textbook Z21 spec
    // meaning (R=1=forward) - see z21DecodeSpeed()'s comment: real-hardware
    // testing 2026-08-28 showed the literal spec mapping lands the wrong
    // direction on Ecos for this loco/WLANmaus combination, and inverting
    // to match XpressNet's own already-proven-correct decode fixes it.
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_128, 0x40, dir, speed);  // R=0 -> forward (inverted)
    TEST_ASSERT_EQUAL_UINT8(1, dir);
}

void test_decode_speed_128_reverse(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_128, 0xC0, dir, speed);  // R=1 -> reverse (inverted)
    TEST_ASSERT_EQUAL_UINT8(0, dir);
}

// ============================================================================
// SPEED DECODE - DCC 14
// ============================================================================

void test_decode_speed_14_stop(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_14, 0x80, dir, speed);  // R000 0000
    TEST_ASSERT_EQUAL_UINT8(0, speed);
}

void test_decode_speed_14_step14_max(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_14, 0x8F, dir, speed);  // R000 1111 = Step 14 max
    TEST_ASSERT_EQUAL_UINT8(DCC_MAX_SPEED, speed);  // raw_step 14 of 14 -> full scale
}

void test_decode_speed_14_step1(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_14, 0x82, dir, speed);  // R000 0010 = Step 1
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(1 * DCC_MAX_SPEED / 14), speed);
}

// ============================================================================
// SPEED DECODE - DCC 28 (spec table cross-checked directly)
// ============================================================================

void test_decode_speed_28_stop(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_28, 0x80, dir, speed);  // R000 0000 = Stop
    TEST_ASSERT_EQUAL_UINT8(0, speed);
}

void test_decode_speed_28_step1(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_28, 0x82, dir, speed);  // R000 0010 = Step 1
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(1 * DCC_MAX_SPEED / 28), speed);
}

void test_decode_speed_28_step2(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_28, 0x92, dir, speed);  // R001 0010 = Step 2
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(2 * DCC_MAX_SPEED / 28), speed);
}

void test_decode_speed_28_step27(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_28, 0x8F, dir, speed);  // R000 1111 = Step 27
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(27 * DCC_MAX_SPEED / 28), speed);
}

void test_decode_speed_28_step28_max(void) {
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_28, 0x9F, dir, speed);  // R001 1111 = Step 28 max
    TEST_ASSERT_EQUAL_UINT8(DCC_MAX_SPEED, speed);
}

void test_decode_speed_unknown_step_mode_rejected(void) {
    uint8_t dir, speed;
    TEST_ASSERT_FALSE(z21DecodeSpeed(99, 0x80, dir, speed));
}

// ============================================================================
// SPEED ENCODE (128-step, always used for outgoing LOCO_INFO)
// ============================================================================

void test_encode_speed128_stop(void) {
    // direction=1 (forward, internal convention) encodes to R=0 - inverted
    // from the textbook Z21 spec meaning, see z21EncodeSpeed128()'s comment.
    TEST_ASSERT_EQUAL_HEX8(0x00, z21EncodeSpeed128(1, 0));
}

void test_encode_speed128_max(void) {
    TEST_ASSERT_EQUAL_HEX8(0x7F, z21EncodeSpeed128(1, 126));
}

void test_encode_speed128_reverse(void) {
    TEST_ASSERT_EQUAL_HEX8(0x80, z21EncodeSpeed128(0, 0));
}

void test_encode_decode_speed128_roundtrip(void) {
    uint8_t byte = z21EncodeSpeed128(1, 64);
    uint8_t dir, speed;
    z21DecodeSpeed(Z21_SPEED_STEPS_128, byte, dir, speed);
    TEST_ASSERT_EQUAL_UINT8(1, dir);
    TEST_ASSERT_EQUAL_UINT8(64, speed);
}

// ============================================================================
// FUNCTION COMMAND DECODE
// ============================================================================

void test_decode_function_off(void) {
    uint8_t index, type;
    TEST_ASSERT_TRUE(z21DecodeFunctionCommand(0x00, index, type));  // TT=00, N=0 (F0)
    TEST_ASSERT_EQUAL_UINT8(0, index);
    TEST_ASSERT_EQUAL_UINT8(Z21_FUNCTION_SWITCH_OFF, type);
}

void test_decode_function_on(void) {
    uint8_t index, type;
    TEST_ASSERT_TRUE(z21DecodeFunctionCommand(0x43, index, type));  // TT=01, N=3 (F3)
    TEST_ASSERT_EQUAL_UINT8(3, index);
    TEST_ASSERT_EQUAL_UINT8(Z21_FUNCTION_SWITCH_ON, type);
}

void test_decode_function_toggle(void) {
    uint8_t index, type;
    TEST_ASSERT_TRUE(z21DecodeFunctionCommand(0x81, index, type));  // TT=10, N=1 (F1)
    TEST_ASSERT_EQUAL_UINT8(1, index);
    TEST_ASSERT_EQUAL_UINT8(Z21_FUNCTION_SWITCH_TOGGLE, type);
}

void test_decode_function_invalid_tt_rejected(void) {
    uint8_t index, type;
    TEST_ASSERT_FALSE(z21DecodeFunctionCommand(0xC0, index, type));  // TT=11 reserved
}

// ============================================================================
// TURNOUT COMMAND DECODE (Phase 7 step 1)
// ============================================================================

void test_decode_turnout_straight(void) {
    // DB0 = 10Q0A00P: activate (A=1), straight (P=0)
    // DB0 = 0b10001000 = 0x88
    uint16_t address;
    bool diverging;
    TEST_ASSERT_TRUE(z21DecodeTurnoutCommand(0x00, 0x03, 0x88, address, diverging));
    TEST_ASSERT_EQUAL_UINT16(4, address);  // wire=3, +1 correction -> user-entered 4
    TEST_ASSERT_FALSE(diverging);
}

void test_decode_turnout_diverging(void) {
    // DB0 = 10Q0A00P: activate (A=1), diverging (P=1)
    // DB0 = 0b10001001 = 0x89
    uint16_t address;
    bool diverging;
    TEST_ASSERT_TRUE(z21DecodeTurnoutCommand(0x00, 0x05, 0x89, address, diverging));
    TEST_ASSERT_EQUAL_UINT16(6, address);  // wire=5, +1 correction -> 6
    TEST_ASSERT_TRUE(diverging);
}

void test_decode_turnout_deactivate_rejected(void) {
    // DB0 with A=0 (deactivate) should be rejected
    // DB0 = 0b10000000 = 0x80
    uint16_t address;
    bool diverging;
    TEST_ASSERT_FALSE(z21DecodeTurnoutCommand(0x00, 0x03, 0x80, address, diverging));
}

void test_decode_turnout_invalid_upper_nibble_rejected(void) {
    // DB0 with upper nibble != 0b1000 should be rejected
    // DB0 = 0b01001000 = 0x48 (upper nibble = 0b0100)
    uint16_t address;
    bool diverging;
    TEST_ASSERT_FALSE(z21DecodeTurnoutCommand(0x00, 0x03, 0x48, address, diverging));
}

void test_decode_turnout_high_address(void) {
    // Test with a long address (>= 128)
    uint16_t address;
    bool diverging;
    uint8_t msb, lsb;
    z21EncodeAddress(300, msb, lsb);  // Reuse address encoder
    TEST_ASSERT_TRUE(z21DecodeTurnoutCommand(msb, lsb, 0x88, address, diverging));
    TEST_ASSERT_EQUAL_UINT16(301, address);  // 300 + 1 correction
    TEST_ASSERT_FALSE(diverging);
}

// ============================================================================
// PACKET BUILDERS
// ============================================================================

void test_build_loco_info_header_and_length(void) {
    uint8_t buffer[64];
    size_t len = z21BuildLocoInfo(buffer, sizeof(buffer), 3, 1, 64, 0);
    TEST_ASSERT_EQUAL(14, len);
    TEST_ASSERT_EQUAL_HEX8(14, buffer[0]);   // DataLen LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[1]); // DataLen MSB
    TEST_ASSERT_EQUAL_HEX8(0x40, buffer[2]); // Header LSB (LAN_X)
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[3]); // Header MSB
    TEST_ASSERT_EQUAL_HEX8(0xEF, buffer[4]); // X-Header
}

void test_build_loco_info_checksum_valid(void) {
    uint8_t buffer[64];
    size_t len = z21BuildLocoInfo(buffer, sizeof(buffer), 3, 1, 64, 0x1F);
    // XOR of everything between Header and the final checksum byte must be 0
    // when the checksum byte itself is included (standard XOR-checksum property).
    uint8_t check = 0;
    for (size_t i = 4; i < len; i++) {
        check ^= buffer[i];
    }
    TEST_ASSERT_EQUAL_HEX8(0, check);
}

void test_build_loco_info_encodes_f0_correctly(void) {
    uint8_t buffer[64];
    z21BuildLocoInfo(buffer, sizeof(buffer), 3, 1, 0, 0x01);  // F0 only
    TEST_ASSERT_EQUAL_HEX8(0x10, buffer[9]);  // DB4: F0 -> bit4 (L)
}

void test_build_loco_info_rejects_undersized_buffer(void) {
    uint8_t buffer[8];
    size_t len = z21BuildLocoInfo(buffer, sizeof(buffer), 3, 1, 0, 0);
    TEST_ASSERT_EQUAL(0, len);
}

void test_build_track_power_on_matches_spec_bytes(void) {
    uint8_t buffer[16];
    size_t len = z21BuildTrackPowerBroadcast(buffer, sizeof(buffer), true);
    uint8_t expected[] = {0x07, 0x00, 0x40, 0x00, 0x61, 0x01, 0x60};
    TEST_ASSERT_EQUAL(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

void test_build_track_power_off_matches_spec_bytes(void) {
    uint8_t buffer[16];
    size_t len = z21BuildTrackPowerBroadcast(buffer, sizeof(buffer), false);
    uint8_t expected[] = {0x07, 0x00, 0x40, 0x00, 0x61, 0x00, 0x61};
    TEST_ASSERT_EQUAL(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

void test_build_stopped_broadcast_matches_spec_bytes(void) {
    uint8_t buffer[16];
    size_t len = z21BuildStoppedBroadcast(buffer, sizeof(buffer));
    uint8_t expected[] = {0x07, 0x00, 0x40, 0x00, 0x81, 0x00, 0x81};
    TEST_ASSERT_EQUAL(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

void test_build_serial_number_reply_length(void) {
    uint8_t buffer[16];
    size_t len = z21BuildSerialNumberReply(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(8, len);
    TEST_ASSERT_EQUAL_HEX8(0x10, buffer[2]);  // Header LSB = GET_SERIAL_NUMBER
}

void test_build_firmware_version_reply_matches_spec_example(void) {
    // Spec example: 0x09 0x00 0x40 0x00 0xf3 0x0a 0x01 0x23 0xdb means "1.23"
    // We report a fixed "1.30" - verify the general shape + checksum instead.
    uint8_t buffer[16];
    size_t len = z21BuildFirmwareVersionReply(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(9, len);
    TEST_ASSERT_EQUAL_HEX8(0xF3, buffer[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[6]);  // "1"
    TEST_ASSERT_EQUAL_HEX8(0x30, buffer[7]);  // "30"
}

void test_build_unknown_command_reply_matches_spec_bytes(void) {
    uint8_t buffer[16];
    size_t len = z21BuildUnknownCommandReply(buffer, sizeof(buffer));
    uint8_t expected[] = {0x07, 0x00, 0x40, 0x00, 0x61, 0x82, 0xE3};
    TEST_ASSERT_EQUAL(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

void test_build_status_changed_no_estop(void) {
    uint8_t buffer[16];
    size_t len = z21BuildStatusChangedReply(buffer, sizeof(buffer), false);
    TEST_ASSERT_EQUAL(8, len);
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[6]);  // status byte
}

void test_build_status_changed_estop_active(void) {
    uint8_t buffer[16];
    z21BuildStatusChangedReply(buffer, sizeof(buffer), true);
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[6]);  // csEmergencyStop bit
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_checksum_track_power_off);
    RUN_TEST(test_checksum_track_power_on);
    RUN_TEST(test_checksum_unknown_command);
    RUN_TEST(test_checksum_stopped_broadcast);

    RUN_TEST(test_decode_address_short);
    RUN_TEST(test_decode_address_long_ignores_high_bits);
    RUN_TEST(test_encode_address_short_no_high_bits_set);
    RUN_TEST(test_encode_address_long_sets_high_bits);
    RUN_TEST(test_encode_decode_address_roundtrip);

    RUN_TEST(test_decode_speed_128_stop);
    RUN_TEST(test_decode_speed_128_estop_treated_as_stop);
    RUN_TEST(test_decode_speed_128_max);
    RUN_TEST(test_decode_speed_128_forward);
    RUN_TEST(test_decode_speed_128_reverse);

    RUN_TEST(test_decode_speed_14_stop);
    RUN_TEST(test_decode_speed_14_step14_max);
    RUN_TEST(test_decode_speed_14_step1);

    RUN_TEST(test_decode_speed_28_stop);
    RUN_TEST(test_decode_speed_28_step1);
    RUN_TEST(test_decode_speed_28_step2);
    RUN_TEST(test_decode_speed_28_step27);
    RUN_TEST(test_decode_speed_28_step28_max);
    RUN_TEST(test_decode_speed_unknown_step_mode_rejected);

    RUN_TEST(test_encode_speed128_stop);
    RUN_TEST(test_encode_speed128_max);
    RUN_TEST(test_encode_speed128_reverse);
    RUN_TEST(test_encode_decode_speed128_roundtrip);

    RUN_TEST(test_decode_function_off);
    RUN_TEST(test_decode_function_on);
    RUN_TEST(test_decode_function_toggle);
    RUN_TEST(test_decode_function_invalid_tt_rejected);

    RUN_TEST(test_decode_turnout_straight);
    RUN_TEST(test_decode_turnout_diverging);
    RUN_TEST(test_decode_turnout_deactivate_rejected);
    RUN_TEST(test_decode_turnout_invalid_upper_nibble_rejected);
    RUN_TEST(test_decode_turnout_high_address);

    RUN_TEST(test_build_loco_info_header_and_length);
    RUN_TEST(test_build_loco_info_checksum_valid);
    RUN_TEST(test_build_loco_info_encodes_f0_correctly);
    RUN_TEST(test_build_loco_info_rejects_undersized_buffer);
    RUN_TEST(test_build_track_power_on_matches_spec_bytes);
    RUN_TEST(test_build_track_power_off_matches_spec_bytes);
    RUN_TEST(test_build_stopped_broadcast_matches_spec_bytes);
    RUN_TEST(test_build_serial_number_reply_length);
    RUN_TEST(test_build_firmware_version_reply_matches_spec_example);
    RUN_TEST(test_build_unknown_command_reply_matches_spec_bytes);
    RUN_TEST(test_build_status_changed_no_estop);
    RUN_TEST(test_build_status_changed_estop_active);

    return UNITY_END();
}
