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
    // Real property is "dir", not "direction" - confirmed against real
    // hardware 2026-07-31 ("direction" is rejected as "unknown option").
    int len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 1);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, dir[1])\n", cmd_buffer);
}

void test_ecos_build_direction_command_reverse(void) {
    int len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), 100, 0);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(100, dir[0])\n", cmd_buffer);
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

void test_ecos_build_query_accessory_objects_request(void) {
    int len = ecosBuildQueryAccessoryObjectsCmd(cmd_buffer, sizeof(cmd_buffer));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("queryObjects(11, addr)\n", cmd_buffer);
}

void test_ecos_build_query_accessory_objects_buffer_too_small(void) {
    char tiny[10];
    int len = ecosBuildQueryAccessoryObjectsCmd(tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_INT(0, len);
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
// TESTS - SYSTEM-WIDE STOP/GO (Phase 5 step 2)
// ============================================================================

void test_ecos_build_system_stop(void) {
    int len = ecosBuildSystemStopCmd(cmd_buffer, sizeof(cmd_buffer));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(1, stop)\n", cmd_buffer);
}

void test_ecos_build_system_go(void) {
    int len = ecosBuildSystemGoCmd(cmd_buffer, sizeof(cmd_buffer));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(1, go)\n", cmd_buffer);
}

void test_ecos_build_system_stop_rejects_undersized_buffer(void) {
    char tiny_buffer[5];
    int len = ecosBuildSystemStopCmd(tiny_buffer, sizeof(tiny_buffer));
    TEST_ASSERT_EQUAL_INT(0, len);
}

// ============================================================================
// TESTS - BASELINE STATE QUERY (Phase 6 step 4 follow-up, 2026-08-28)
// ============================================================================

void test_ecos_build_get_speed(void) {
    int len = ecosBuildGetPropertyCmd(cmd_buffer, sizeof(cmd_buffer), 1009, "speed");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("get(1009, speed)\n", cmd_buffer);
}

void test_ecos_build_get_dir(void) {
    int len = ecosBuildGetPropertyCmd(cmd_buffer, sizeof(cmd_buffer), 1009, "dir");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("get(1009, dir)\n", cmd_buffer);
}

void test_ecos_build_get_function(void) {
    int len = ecosBuildGetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), 1009, 8);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("get(1009, func[8])\n", cmd_buffer);
}

void test_ecos_build_get_function_rejects_out_of_range(void) {
    int len = ecosBuildGetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), 1009, 32);
    TEST_ASSERT_EQUAL_INT(0, len);
}

void test_ecos_build_accessory_straight(void) {
    // Port letter confirmed live 2026-08-05 - r=straight, g=diverging
    // (the initial spec-inferred guess was backwards)
    int len = ecosBuildSetAccessoryCmd(cmd_buffer, sizeof(cmd_buffer), 5, false);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(11, switch[DCC5r])\n", cmd_buffer);
}

void test_ecos_build_accessory_diverging(void) {
    int len = ecosBuildSetAccessoryCmd(cmd_buffer, sizeof(cmd_buffer), 5, true);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("set(11, switch[DCC5g])\n", cmd_buffer);
}

void test_ecos_build_accessory_rejects_undersized_buffer(void) {
    char tiny_buffer[5];
    int len = ecosBuildSetAccessoryCmd(tiny_buffer, sizeof(tiny_buffer), 5, false);
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
    RUN_TEST(test_ecos_build_query_accessory_objects_request);
    RUN_TEST(test_ecos_build_query_accessory_objects_buffer_too_small);

    RUN_TEST(test_ecos_build_request_view_mode);
    RUN_TEST(test_ecos_build_request_control_mode);
    RUN_TEST(test_ecos_build_request_control_force);
    RUN_TEST(test_ecos_build_request_view_force_ignored);

    RUN_TEST(test_ecos_build_release_view);
    RUN_TEST(test_ecos_build_release_control);

    RUN_TEST(test_ecos_build_system_stop);
    RUN_TEST(test_ecos_build_system_go);
    RUN_TEST(test_ecos_build_get_speed);
    RUN_TEST(test_ecos_build_get_dir);
    RUN_TEST(test_ecos_build_get_function);
    RUN_TEST(test_ecos_build_get_function_rejects_out_of_range);
    RUN_TEST(test_ecos_build_system_stop_rejects_undersized_buffer);
    RUN_TEST(test_ecos_build_accessory_straight);
    RUN_TEST(test_ecos_build_accessory_diverging);
    RUN_TEST(test_ecos_build_accessory_rejects_undersized_buffer);

    RUN_TEST(test_ecos_command_includes_trailing_newline);

    RUN_TEST(test_ecos_parse_event_object_id);
    RUN_TEST(test_ecos_parse_event_object_id_not_an_event);
    RUN_TEST(test_ecos_parse_end_code_ok);
    RUN_TEST(test_ecos_parse_end_code_error);
    RUN_TEST(test_ecos_parse_end_code_multi_digit);
    RUN_TEST(test_ecos_parse_end_code_invalid_line);

    return UNITY_END();
}
