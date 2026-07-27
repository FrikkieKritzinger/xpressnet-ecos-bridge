/*
 * Ecos Command Builder Unit Tests
 *
 * Tests for:
 * - Speed/direction/function command generation
 * - Query/request/release/get command generation
 * - Buffer-too-small rejection (all builders return 0 below their minimum size)
 * - Event/end-code parsing helpers
 *
 * Note: the real API is a set of free functions (ecosBuild*Cmd) operating on
 * an Ecos *object ID* (not the DCC address) and producing a trailing '\n' -
 * e.g. "set(1000, speed[64])\n". The DCC-address-to-object-ID mapping is a
 * StateEngine/CommandRouter concern, not the builder's.
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "protocols/ecos/ecos_protocol.h"

// ============================================================================
// TEST SETUP / TEARDOWN
// ============================================================================

void setUp(void) {
    // Builders are free functions, no setup needed
}

void tearDown(void) {
    // No cleanup needed
}

static char cmd_buffer[256];

// ============================================================================
// TESTS - SPEED COMMANDS
// ============================================================================

void test_ecos_build_speed_command_mid_speed(void) {
    int len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), 100, 64);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, speed[64])\n", cmd_buffer);
}

void test_ecos_build_speed_command_zero(void) {
    int len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), 50, 0);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(50, speed[0])\n", cmd_buffer);
}

void test_ecos_build_speed_command_max(void) {
    int len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), 99, 126);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(99, speed[126])\n", cmd_buffer);
}

void test_ecos_build_speed_command_buffer_too_small(void) {
    char tiny[10];
    int len = ecosBuildSetSpeedCmd(tiny, sizeof(tiny), 100, 64);
    TEST_ASSERT_EQUAL_INT(0, len);  // Must reject buffers under 60 bytes
}

// ============================================================================
// TESTS - DIRECTION COMMANDS
// ============================================================================

void test_ecos_build_direction_command_forward(void) {
    int len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 1);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, direction[1])\n", cmd_buffer);
}

void test_ecos_build_direction_command_reverse(void) {
    int len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 0);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, direction[0])\n", cmd_buffer);
}

// ============================================================================
// TESTS - FUNCTION COMMANDS
// ============================================================================

void test_ecos_build_function_f0_on(void) {
    int len = ecosBuildSetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 0, 1);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, func[0,1])\n", cmd_buffer);
}

void test_ecos_build_function_f31_off(void) {
    // F31 is the highest valid function index
    int len = ecosBuildSetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 31, 0);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, func[31,0])\n", cmd_buffer);
}

void test_ecos_build_function_invalid_index_rejected(void) {
    // Function index 32 is out of range (only F0-F31 exist)
    int len = ecosBuildSetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 32, 1);
    TEST_ASSERT_EQUAL_INT(0, len);
}

// ============================================================================
// TESTS - QUERY OBJECTS
// ============================================================================

void test_ecos_build_query_objects_request(void) {
    int len = ecosBuildQueryObjectsCmd(cmd_buffer, sizeof(cmd_buffer));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("queryObjects(10, addr, name)\n", cmd_buffer);
}

void test_ecos_build_query_objects_buffer_too_small(void) {
    char tiny[10];
    int len = ecosBuildQueryObjectsCmd(tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_INT(0, len);  // Must reject buffers under 50 bytes
}

// ============================================================================
// TESTS - REQUEST / RELEASE (SUBSCRIBE / UNSUBSCRIBE)
// ============================================================================

void test_ecos_build_request_view_mode(void) {
    int len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer), 1000, ECOS_MODE_VIEW);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("request(1000, view)\n", cmd_buffer);
}

void test_ecos_build_request_control_mode(void) {
    int len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer), 1000, ECOS_MODE_CONTROL);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("request(1000, control)\n", cmd_buffer);
}

void test_ecos_build_request_control_force(void) {
    int len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer), 1000, ECOS_MODE_CONTROL, true);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("request(1000, control, force)\n", cmd_buffer);
}

void test_ecos_build_request_view_force_ignored(void) {
    // force_control only applies to "control" mode; view mode ignores it
    int len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer), 1000, ECOS_MODE_VIEW, true);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("request(1000, view)\n", cmd_buffer);
}

void test_ecos_build_release_view(void) {
    int len = ecosBuildReleaseCmd(cmd_buffer, sizeof(cmd_buffer), 1000, ECOS_MODE_VIEW);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("release(1000, view)\n", cmd_buffer);
}

void test_ecos_build_release_control(void) {
    int len = ecosBuildReleaseCmd(cmd_buffer, sizeof(cmd_buffer), 1000, ECOS_MODE_CONTROL);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("release(1000, control)\n", cmd_buffer);
}

// ============================================================================
// TESTS - GET (PROPERTY QUERY)
// ============================================================================

void test_ecos_build_get_speed_property(void) {
    int len = ecosBuildGetCmd(cmd_buffer, sizeof(cmd_buffer), 100, "speed");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("get(100, speed)\n", cmd_buffer);
}

void test_ecos_build_get_null_property_rejected(void) {
    int len = ecosBuildGetCmd(cmd_buffer, sizeof(cmd_buffer), 100, nullptr);
    TEST_ASSERT_EQUAL_INT(0, len);
}

// ============================================================================
// TESTS - COMMAND FORMAT
// ============================================================================

void test_ecos_command_includes_trailing_newline(void) {
    // All builders terminate the command with '\n' (caller does not add one)
    int len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), 100, 64);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_CHAR('\n', cmd_buffer[len - 1]);
}

// ============================================================================
// TESTS - PARSING HELPERS
// ============================================================================

void test_ecos_parse_event_object_id(void) {
    uint16_t id = ecosParseEventObjectId("<EVENT 1000>");
    TEST_ASSERT_EQUAL_UINT16(1000, id);
}

void test_ecos_parse_event_object_id_not_an_event(void) {
    uint16_t id = ecosParseEventObjectId("<REPLY 1000>");
    TEST_ASSERT_EQUAL_UINT16(0, id);  // Not an <EVENT line, must return 0
}

void test_ecos_parse_end_code_ok(void) {
    int16_t code = ecosParseEndCode("<END 0 (OK)>");
    TEST_ASSERT_EQUAL_INT16(0, code);
}

void test_ecos_parse_end_code_error(void) {
    int16_t code = ecosParseEndCode("<END 1 (Error)>");
    TEST_ASSERT_EQUAL_INT16(1, code);
}

void test_ecos_parse_end_code_multi_digit(void) {
    // Exercises the multi-digit accumulation loop (single-digit codes
    // 0/1 never touch it)
    int16_t code = ecosParseEndCode("<END 12 (Warning)>");
    TEST_ASSERT_EQUAL_INT16(12, code);
}

void test_ecos_parse_end_code_invalid_line(void) {
    int16_t code = ecosParseEndCode("garbage");
    TEST_ASSERT_EQUAL_INT16(-1, code);
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ecos_build_speed_command_mid_speed);
    RUN_TEST(test_ecos_build_speed_command_zero);
    RUN_TEST(test_ecos_build_speed_command_max);
    RUN_TEST(test_ecos_build_speed_command_buffer_too_small);

    RUN_TEST(test_ecos_build_direction_command_forward);
    RUN_TEST(test_ecos_build_direction_command_reverse);

    RUN_TEST(test_ecos_build_function_f0_on);
    RUN_TEST(test_ecos_build_function_f31_off);
    RUN_TEST(test_ecos_build_function_invalid_index_rejected);

    RUN_TEST(test_ecos_build_query_objects_request);
    RUN_TEST(test_ecos_build_query_objects_buffer_too_small);

    RUN_TEST(test_ecos_build_request_view_mode);
    RUN_TEST(test_ecos_build_request_control_mode);
    RUN_TEST(test_ecos_build_request_control_force);
    RUN_TEST(test_ecos_build_request_view_force_ignored);

    RUN_TEST(test_ecos_build_release_view);
    RUN_TEST(test_ecos_build_release_control);

    RUN_TEST(test_ecos_build_get_speed_property);
    RUN_TEST(test_ecos_build_get_null_property_rejected);

    RUN_TEST(test_ecos_command_includes_trailing_newline);

    RUN_TEST(test_ecos_parse_event_object_id);
    RUN_TEST(test_ecos_parse_event_object_id_not_an_event);
    RUN_TEST(test_ecos_parse_end_code_ok);
    RUN_TEST(test_ecos_parse_end_code_error);
    RUN_TEST(test_ecos_parse_end_code_multi_digit);
    RUN_TEST(test_ecos_parse_end_code_invalid_line);

    return UNITY_END();
}
