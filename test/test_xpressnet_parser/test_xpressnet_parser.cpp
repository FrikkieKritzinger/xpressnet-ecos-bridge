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
#include <unity.h>
#include "fixtures/xpressnet_messages.h"
#include "protocols/xpressnet/xpressnet_message_parser.h"

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

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(100, cmd.address);
    TEST_ASSERT_EQUAL_UINT8(64, cmd.speed);
    TEST_ASSERT_EQUAL_UINT8(1, cmd.direction);
    TEST_ASSERT_EQUAL_INT(XNetCommand::SPEED, cmd.type);
}

void test_xpressnet_parse_speed_command_reverse(void) {
    // Parse XNET_SPEED_1_MID_REVERSE
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_1_MID_REVERSE,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(1, cmd.address);
    TEST_ASSERT_EQUAL_UINT8(64, cmd.speed);
    TEST_ASSERT_EQUAL_UINT8(0, cmd.direction);  // Reverse
}

void test_xpressnet_parse_emergency_stop(void) {
    // Parse XNET_ESTOP_100
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_ESTOP_100,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(100, cmd.address);
    TEST_ASSERT_EQUAL_INT(XNetCommand::EMERGENCY_STOP, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(127, cmd.speed);  // E-stop is speed 127
}

void test_xpressnet_parse_function_command(void) {
    // Parse XNET_FUNC_100_F0_ON (F0 on, F1-F7 off = 0x01)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_FUNC_100_F0_ON,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(100, cmd.address);
    TEST_ASSERT_EQUAL_INT(XNetCommand::FUNCTION, cmd.type);
    TEST_ASSERT_EQUAL_UINT32(0x01, cmd.functions);  // Only F0 on
}

void test_xpressnet_parse_long_address(void) {
    // Parse XNET_SPEED_200_LONG_ADDR
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_200_LONG_ADDR,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(200, cmd.address);  // Long address correctly decoded
    TEST_ASSERT_EQUAL_INT(XNetCommand::SPEED, cmd.type);
}

// ============================================================================
// TESTS - LOW-LEVEL HELPERS (called directly, not just via parse())
// ============================================================================

void test_xpressnet_extract_address_short_form(void) {
    // Bit 0x40 set in the high byte means SHORT address: the low byte IS
    // the address (masked to 7 bits). This branch is never exercised via
    // parse() with the current fixtures (they all use the long-address
    // path), so it's tested directly here.
    uint16_t address = XNetMessageParser::extractAddress(0x40, 50);
    TEST_ASSERT_EQUAL_UINT16(50, address);
}

void test_xpressnet_determine_command_type_rejects_short_buffer(void) {
    uint8_t data[] = {0x00, 0x64};
    XNetCommand::Type type = XNetMessageParser::determineCommandType(data, 2);
    TEST_ASSERT_EQUAL_INT(XNetCommand::INVALID, type);
}

// ============================================================================
// TESTS - CHECKSUM VALIDATION
// ============================================================================

void test_xpressnet_checksum_valid(void) {
    // Test valid fixtures
    TEST_ASSERT_TRUE(XNetMessageParser::isValidMessage(XNET_SPEED_100_MID_FORWARD, XNET_MSG_SIZE_VALID));
    TEST_ASSERT_TRUE(XNetMessageParser::isValidMessage(XNET_SPEED_1_MID_REVERSE, XNET_MSG_SIZE_VALID));
    TEST_ASSERT_TRUE(XNetMessageParser::isValidMessage(XNET_ESTOP_100, XNET_MSG_SIZE_VALID));
    TEST_ASSERT_TRUE(XNetMessageParser::isValidMessage(XNET_FUNC_100_F0_ON, XNET_MSG_SIZE_VALID));
}

void test_xpressnet_checksum_invalid(void) {
    // XNET_BAD_CHECKSUM should fail validation
    TEST_ASSERT_FALSE(XNetMessageParser::isValidMessage(XNET_BAD_CHECKSUM, XNET_MSG_SIZE_VALID));
}

void test_xpressnet_parse_rejects_bad_checksum(void) {
    // parse() itself must reject a bad-checksum message (not just isValidMessage()
    // called in isolation) - this exercises parse()'s own checksum-failure branch.
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_BAD_CHECKSUM, XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_EQUAL_INT(XNetCommand::INVALID, cmd.type);
}

void test_xpressnet_isValidMessage_rejects_short_buffer(void) {
    // isValidMessage() has its own length<4 guard, independent of parse()'s
    uint8_t data[] = {0x00};
    TEST_ASSERT_FALSE(XNetMessageParser::isValidMessage(data, 1));
}

void test_xpressnet_checksum_calculation(void) {
    // Test checksum calculation directly
    // Message: [0x00, 0x64, 0x40, ??]  (address 100, speed 64)
    // Checksum: 0x00 ^ 0x64 ^ 0x40 = 0x64
    uint8_t data[] = {0x00, 0x64, 0x40};
    uint8_t expected_checksum = 0x00 ^ 0x64 ^ 0x40;  // = 0x64
    uint8_t calculated = XNetMessageParser::calculateChecksum(data, 3);

    TEST_ASSERT_EQUAL_UINT8(expected_checksum, calculated);
}

// ============================================================================
// TESTS - ERROR HANDLING
// ============================================================================

void test_xpressnet_parse_incomplete_message(void) {
    // XNET_INCOMPLETE (2 bytes) should fail gracefully
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_INCOMPLETE,
                                            XNET_MSG_SIZE_INCOMPLETE, cmd);

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_EQUAL_INT(XNetCommand::INVALID, cmd.type);  // Should be marked invalid
}

void test_xpressnet_parse_too_short(void) {
    // XNET_SHORT (1 byte) should fail
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SHORT,
                                            XNET_MSG_SIZE_SHORT, cmd);

    TEST_ASSERT_FALSE(success);
}

void test_xpressnet_parse_empty_buffer(void) {
    // Empty message should be rejected
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(nullptr, 0, cmd);

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_EQUAL_INT(XNetCommand::INVALID, cmd.type);
}

void test_xpressnet_parse_null_pointer(void) {
    // Null pointer should be handled safely
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(nullptr, 4, cmd);

    TEST_ASSERT_FALSE(success);  // Should fail gracefully
}

// ============================================================================
// TESTS - SPEED VALUE EXTRACTION
// ============================================================================

void test_xpressnet_parse_speed_zero(void) {
    // Speed 0 (stop)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_50_STOP,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT8(0, cmd.speed);  // Speed = 0 (stop)
    TEST_ASSERT_EQUAL_UINT16(50, cmd.address);
}

void test_xpressnet_parse_speed_max(void) {
    // Speed 126 (maximum, just below E-stop)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_SPEED_99_MAX,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT8(126, cmd.speed);
    TEST_ASSERT_EQUAL_UINT16(99, cmd.address);
}

// ============================================================================
// TESTS - FUNCTION VALUE EXTRACTION
// ============================================================================

void test_xpressnet_parse_function_multiple(void) {
    // Parse XNET_FUNC_50_F0F1_ON (F0 and F1 on = 0x03)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_FUNC_50_F0F1_ON,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(50, cmd.address);
    TEST_ASSERT_EQUAL_UINT32(0x03, cmd.functions);  // F0 and F1
}

void test_xpressnet_parse_function_all_on(void) {
    // Parse XNET_FUNC_200_ALL_ON (all F0-F7 on = 0xFF)
    XNetCommand cmd;
    bool success = XNetMessageParser::parse(XNET_FUNC_200_ALL_ON,
                                            XNET_MSG_SIZE_VALID, cmd);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_UINT16(200, cmd.address);
    TEST_ASSERT_EQUAL_UINT32(0xFF, cmd.functions);  // All on
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_xpressnet_parse_speed_command_forward);
    RUN_TEST(test_xpressnet_parse_speed_command_reverse);
    RUN_TEST(test_xpressnet_parse_emergency_stop);
    RUN_TEST(test_xpressnet_parse_function_command);
    RUN_TEST(test_xpressnet_parse_long_address);

    RUN_TEST(test_xpressnet_extract_address_short_form);
    RUN_TEST(test_xpressnet_determine_command_type_rejects_short_buffer);

    RUN_TEST(test_xpressnet_checksum_valid);
    RUN_TEST(test_xpressnet_checksum_invalid);
    RUN_TEST(test_xpressnet_parse_rejects_bad_checksum);
    RUN_TEST(test_xpressnet_isValidMessage_rejects_short_buffer);
    RUN_TEST(test_xpressnet_checksum_calculation);

    RUN_TEST(test_xpressnet_parse_incomplete_message);
    RUN_TEST(test_xpressnet_parse_too_short);
    RUN_TEST(test_xpressnet_parse_empty_buffer);
    RUN_TEST(test_xpressnet_parse_null_pointer);

    RUN_TEST(test_xpressnet_parse_speed_zero);
    RUN_TEST(test_xpressnet_parse_speed_max);

    RUN_TEST(test_xpressnet_parse_function_multiple);
    RUN_TEST(test_xpressnet_parse_function_all_on);

    return UNITY_END();
}
