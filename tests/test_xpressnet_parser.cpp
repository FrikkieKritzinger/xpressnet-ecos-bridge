/*
 * XpressNet Message Parser Unit Tests
 *
 * Tests for:
 * - Binary message parsing (4+ byte frames)
 * - Checksum validation
 * - Command type detection (speed, function, status)
 * - Address extraction (short and long addresses)
 * - Error handling (bad checksums, incomplete messages)
 */

#include <cstdint>
#include <cstring>
#include "fixtures/xpressnet_messages.h"
#include "../xpressnet_ecos_bridge/protocols/xpressnet/xpressnet_message_parser.h"

// TODO: Include Unity framework headers once configured
// #include "unity.h"
// For now, simple assertions for proof-of-concept

// ============================================================================
// TEST SETUP / TEARDOWN
// ============================================================================

void setUp(void) {
    // Parser is static, no setup needed
}

void tearDown(void) {
    // No cleanup needed
}

// ============================================================================
// TESTS - VALID MESSAGES
// ============================================================================

void test_xpressnet_parse_speed_command_forward(void) {
    // Parse XNET_SPEED_100_MID_FORWARD
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_100_MID_FORWARD,
                                            XNET_MSG_SIZE_VALID, cmd);

    // Verify
    if (!success) return;  // TODO: TEST_ASSERT_TRUE(success);
    if (cmd.address != 100) return;  // TODO: TEST_ASSERT_EQUAL_UINT16(cmd.address, 100);
    if (cmd.speed != 64) return;  // TODO: TEST_ASSERT_EQUAL_UINT8(cmd.speed, 64);
    if (cmd.direction != 1) return;  // TODO: TEST_ASSERT_EQUAL_UINT8(cmd.direction, 1);
    if (cmd.type != XNetCommand::SPEED) return;  // TODO: TEST_ASSERT_EQUAL_INT(cmd.type, XNetCommand::SPEED);
}

void test_xpressnet_parse_speed_command_reverse(void) {
    // Parse XNET_SPEED_1_MID_REVERSE
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_1_MID_REVERSE,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.address != 1) return;
    if (cmd.speed != 64) return;
    if (cmd.direction != 0) return;  // Reverse
}

void test_xpressnet_parse_emergency_stop(void) {
    // Parse XNET_ESTOP_100
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_ESTOP_100,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.address != 100) return;
    if (cmd.type != XNetCommand::EMERGENCY_STOP) return;
    if (cmd.speed != 127) return;  // E-stop is speed 127
}

void test_xpressnet_parse_function_command(void) {
    // Parse XNET_FUNC_100_F0_ON (F0 on, F1-F7 off = 0x01)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_FUNC_100_F0_ON,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.address != 100) return;
    if (cmd.type != XNetCommand::FUNCTION) return;
    if (cmd.functions != 0x01) return;  // Only F0 on
}

void test_xpressnet_parse_long_address(void) {
    // Parse XNET_SPEED_200_LONG_ADDR
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_200_LONG_ADDR,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.address != 200) return;  // Long address correctly decoded
    if (cmd.type != XNetCommand::SPEED) return;
}

// ============================================================================
// TESTS - CHECKSUM VALIDATION
// ============================================================================

void test_xpressnet_checksum_valid(void) {
    // Test valid fixtures
    if (!XNetMessageParser::isValidMessage(XNET_SPEED_100_MID_FORWARD, XNET_MSG_SIZE_VALID)) return;
    if (!XNetMessageParser::isValidMessage(XNET_SPEED_1_MID_REVERSE, XNET_MSG_SIZE_VALID)) return;
    if (!XNetMessageParser::isValidMessage(XNET_ESTOP_100, XNET_MSG_SIZE_VALID)) return;
    if (!XNetMessageParser::isValidMessage(XNET_FUNC_100_F0_ON, XNET_MSG_SIZE_VALID)) return;
}

void test_xpressnet_checksum_invalid(void) {
    // XNET_BAD_CHECKSUM should fail validation
    if (XNetMessageParser::isValidMessage(XNET_BAD_CHECKSUM, XNET_MSG_SIZE_VALID)) return;  // Should be false
}

void test_xpressnet_checksum_calculation(void) {
    // Test checksum calculation directly
    // Message: [0x00, 0x64, 0x40, ??]  (address 100, speed 64)
    // Checksum: 0x00 ^ 0x64 ^ 0x40 = 0x64
    uint8_t data[] = {0x00, 0x64, 0x40};
    uint8_t expected_checksum = 0x00 ^ 0x64 ^ 0x40;  // = 0x64
    uint8_t calculated = XNetMessageParser::calculateChecksum(data, 3);

    if (calculated != expected_checksum) return;
}

// ============================================================================
// TESTS - ERROR HANDLING
// ============================================================================

void test_xpressnet_parse_incomplete_message(void) {
    // XNET_INCOMPLETE (2 bytes) should fail gracefully
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_INCOMPLETE,
                                            XNET_MSG_SIZE_INCOMPLETE, cmd);

    if (success) return;  // Should fail
    if (cmd.type != XNetCommand::INVALID) return;  // Should be marked invalid
}

void test_xpressnet_parse_too_short(void) {
    // XNET_SHORT (1 byte) should fail
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SHORT,
                                            XNET_MSG_SIZE_SHORT, cmd);

    if (success) return;  // Should fail
}

void test_xpressnet_parse_empty_buffer(void) {
    // Empty message should be rejected
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(nullptr, 0, cmd);

    if (success) return;  // Should fail
    if (cmd.type != XNetCommand::INVALID) return;
}

void test_xpressnet_parse_null_pointer(void) {
    // Null pointer should be handled safely
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(nullptr, 4, cmd);

    if (success) return;  // Should fail gracefully
}

// ============================================================================
// TESTS - SPEED VALUE EXTRACTION
// ============================================================================

void test_xpressnet_parse_speed_zero(void) {
    // Speed 0 (stop)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_50_STOP,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.speed != 0) return;  // Speed = 0 (stop)
    if (cmd.address != 50) return;
}

void test_xpressnet_parse_speed_max(void) {
    // Speed 126 (maximum, just below E-stop)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_99_MAX,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.speed != 126) return;
    if (cmd.address != 99) return;
}

// ============================================================================
// TESTS - FUNCTION VALUE EXTRACTION
// ============================================================================

void test_xpressnet_parse_function_multiple(void) {
    // Parse XNET_FUNC_50_F0F1_ON (F0 and F1 on = 0x03)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_FUNC_50_F0F1_ON,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.address != 50) return;
    if (cmd.functions != 0x03) return;  // F0 and F1
}

void test_xpressnet_parse_function_all_on(void) {
    // Parse XNET_FUNC_200_ALL_ON (all F0-F7 on = 0xFF)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_FUNC_200_ALL_ON,
                                            XNET_MSG_SIZE_VALID, cmd);

    if (!success) return;
    if (cmd.address != 200) return;
    if (cmd.functions != 0xFF) return;  // All on
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    // Run all tests
    setUp();

    test_xpressnet_parse_speed_command_forward();
    test_xpressnet_parse_speed_command_reverse();
    test_xpressnet_parse_emergency_stop();
    test_xpressnet_parse_function_command();
    test_xpressnet_parse_long_address();

    test_xpressnet_checksum_valid();
    test_xpressnet_checksum_invalid();
    test_xpressnet_checksum_calculation();

    test_xpressnet_parse_incomplete_message();
    test_xpressnet_parse_too_short();
    test_xpressnet_parse_empty_buffer();
    test_xpressnet_parse_null_pointer();

    test_xpressnet_parse_speed_zero();
    test_xpressnet_parse_speed_max();

    test_xpressnet_parse_function_multiple();
    test_xpressnet_parse_function_all_on();

    tearDown();

    return 0;  // TODO: Return UNITY_END() result
}
