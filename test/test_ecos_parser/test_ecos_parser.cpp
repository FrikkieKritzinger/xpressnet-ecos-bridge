/*
 * Ecos Message Parser Unit Tests
 *
 * Tests for:
 * - Text-based protocol parsing (key[value] extraction)
 * - Line accumulation (handle byte-by-byte input)
 * - Reply/Event framing (<REPLY>, <EVENT>, <END>)
 * - Multi-line message handling
 * - Error handling (malformed messages, invalid formats)
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "fixtures/ecos_responses.h"
#include "protocols/ecos/ecos_message_parser.h"

// ============================================================================
// TEST SETUP / TEARDOWN
// ============================================================================

void setUp(void) {
    // Parser created for each test
}

void tearDown(void) {
    // No cleanup needed
}

// ============================================================================
// TESTS - VALID MESSAGES
// ============================================================================

void test_ecos_parse_speed_query_reply(void) {
    // Parse ECOS_REPLY_SPEED_QUERY (3-line block: marker, property line, <END>)
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    for (const char* c = ECOS_REPLY_SPEED_QUERY; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_INT(EcosReply::REPLY, reply.kind);
    TEST_ASSERT_EQUAL_UINT16(100, reply.object_id);
    TEST_ASSERT_TRUE(reply.has_speed);
    TEST_ASSERT_EQUAL_UINT8(64, reply.speed);
    TEST_ASSERT_TRUE(reply.has_direction);
    TEST_ASSERT_EQUAL_UINT8(1, reply.direction);
}

void test_ecos_parse_speed_change_event(void) {
    // Parse ECOS_EVENT_SPEED_CHANGE (3-line block: marker, property line, <END>)
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    for (const char* c = ECOS_EVENT_SPEED_CHANGE; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_INT(EcosReply::EVENT, reply.kind);  // Unsolicited
    TEST_ASSERT_EQUAL_UINT16(101, reply.object_id);
    TEST_ASSERT_EQUAL_UINT8(90, reply.speed);
}

void test_ecos_parse_reply_extracts_id(void) {
    // Simple test: just verify ID extraction
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY get(50, view)>\n50\n<END 0 (OK)>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_UINT16(50, reply.object_id);
}

void test_ecos_parse_event_extracts_id(void) {
    // Event should also extract ID
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<EVENT 75>\n<END 0 (OK)>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_INT(EcosReply::EVENT, reply.kind);
    TEST_ASSERT_EQUAL_UINT16(75, reply.object_id);
}

// ============================================================================
// TESTS - LINE ACCUMULATION
// ============================================================================

void test_ecos_accumulate_byte_by_byte(void) {
    // Feed ECOS_BYTE_BY_BYTE one byte at a time
    EcosMessageParser parser;
    EcosReply reply;
    bool complete = false;

    for (size_t i = 0; i < sizeof(ECOS_BYTE_BY_BYTE) - 1; i++) {
        if (parser.processByte(ECOS_BYTE_BY_BYTE[i], reply)) {
            complete = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(complete);  // Should have parsed by end
}

void test_ecos_accumulate_with_newline(void) {
    // Message with \n should be handled
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY get(100, view)>\n100\n<END 0 (OK)>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_UINT16(100, reply.object_id);
}

void test_ecos_accumulate_with_crlf(void) {
    // Message with \r\n should be handled
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY get(200, view)>\r\n200\r\n<END 0 (OK)>\r\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_UINT16(200, reply.object_id);
}

// ============================================================================
// TESTS - ERROR HANDLING
// ============================================================================

void test_ecos_parse_empty_response(void) {
    // A freshly constructed parser that's fed nothing should stay idle -
    // no crash, no spurious completion, buffers empty.
    EcosMessageParser parser;
    EcosReply reply;

    TEST_ASSERT_EQUAL_INT(0, parser.getLineBufferLength());
    TEST_ASSERT_EQUAL_INT(0, parser.getBlockBufferLength());
}

void test_ecos_reset_clears_state(void) {
    // Parser should reset to initial state
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg1 = "<REPLY id 100>";
    for (const char* c = msg1; *c != '\0'; c++) {
        parser.processByte(*c, reply);
    }

    // Reset parser
    parser.reset();

    // Buffer lengths should be 0
    TEST_ASSERT_EQUAL_INT(0, parser.getLineBufferLength());
    TEST_ASSERT_EQUAL_INT(0, parser.getBlockBufferLength());
}

void test_ecos_parse_missing_closing_bracket(void) {
    // "<REPLY id 100" (no closing >)
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY id 100";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_FALSE(completed);  // Should not complete without closing bracket
}

void test_ecos_parse_only_whitespace(void) {
    // Just newlines should not parse as valid message
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "\n\n\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_FALSE(completed);  // No valid reply/event tag
}

// ============================================================================
// TESTS - KEY-VALUE EXTRACTION
// ============================================================================

void test_ecos_parse_speed_value(void) {
    // Extract speed[64] from message
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY get(100, view)>\n100 speed[64]\n<END 0 (OK)>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_TRUE(reply.has_speed);
    TEST_ASSERT_EQUAL_UINT8(64, reply.speed);
}

void test_ecos_parse_direction_value(void) {
    // Extract direction[1] from message
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY get(100, view)>\n100 direction[1]\n<END 0 (OK)>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_TRUE(reply.has_direction);
    TEST_ASSERT_EQUAL_UINT8(1, reply.direction);
}

void test_ecos_parse_multiple_values(void) {
    // Parse message with multiple key[value] pairs
    EcosMessageParser parser;
    EcosReply reply;
    bool completed = false;

    const char* msg = "<REPLY get(100, view)>\n100 speed[80] direction[0]\n<END 0 (OK)>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            completed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_UINT8(80, reply.speed);
    TEST_ASSERT_EQUAL_UINT8(0, reply.direction);  // Reverse
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ecos_parse_speed_query_reply);
    RUN_TEST(test_ecos_parse_speed_change_event);
    RUN_TEST(test_ecos_parse_reply_extracts_id);
    RUN_TEST(test_ecos_parse_event_extracts_id);

    RUN_TEST(test_ecos_accumulate_byte_by_byte);
    RUN_TEST(test_ecos_accumulate_with_newline);
    RUN_TEST(test_ecos_accumulate_with_crlf);

    RUN_TEST(test_ecos_parse_empty_response);
    RUN_TEST(test_ecos_reset_clears_state);
    RUN_TEST(test_ecos_parse_missing_closing_bracket);
    RUN_TEST(test_ecos_parse_only_whitespace);

    RUN_TEST(test_ecos_parse_speed_value);
    RUN_TEST(test_ecos_parse_direction_value);
    RUN_TEST(test_ecos_parse_multiple_values);

    return UNITY_END();
}
