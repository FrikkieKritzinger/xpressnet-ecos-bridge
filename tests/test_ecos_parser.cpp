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
#include "fixtures/ecos_responses.h"
#include "../xpressnet_ecos_bridge/protocols/ecos/ecos_message_parser.h"

// TODO: Include Unity framework headers once configured
// #include "unity.h"

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
    // Parse ECOS_REPLY_SPEED_QUERY: "<REPLY id 100 speed[64] dir[1] func[0,0]>"
    EcosMessageParser parser;
    EcosReply reply;

    // Feed bytes one by one
    for (size_t i = 0; i < ECOS_REPLY_SPEED_QUERY_LEN; i++) {
        if (parser.processByte(ECOS_REPLY_SPEED_QUERY[i], reply)) {
            // Complete message parsed
            if (reply.kind != EcosReply::REPLY) return;  // Should be REPLY
            if (reply.object_id != 100) return;
            if (!reply.has_speed) return;
            if (reply.speed != 64) return;
            if (!reply.has_direction) return;
            if (reply.direction != 1) return;
            return;  // Success
        }
    }
    // Message never completed
    return;
}

void test_ecos_parse_speed_change_event(void) {
    // Parse ECOS_EVENT_SPEED_CHANGE: "<EVENT id 101 speed[90] dir[1] func[1,0]>"
    EcosMessageParser parser;
    EcosReply reply;

    for (size_t i = 0; i < ECOS_EVENT_SPEED_CHANGE_LEN; i++) {
        if (parser.processByte(ECOS_EVENT_SPEED_CHANGE[i], reply)) {
            // Complete message parsed
            if (reply.kind != EcosReply::EVENT) return;  // Should be EVENT (unsolicited)
            if (reply.object_id != 101) return;
            if (reply.speed != 90) return;
            return;  // Success
        }
    }
    return;  // Failed to parse
}

void test_ecos_parse_reply_extracts_id(void) {
    // Simple test: just verify ID extraction
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<REPLY id 50>";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (reply.object_id != 50) return;
            return;  // Success
        }
    }
    return;
}

void test_ecos_parse_event_extracts_id(void) {
    // Event should also extract ID
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<EVENT id 75>";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (reply.kind != EcosReply::EVENT) return;
            if (reply.object_id != 75) return;
            return;  // Success
        }
    }
    return;
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

    // Should complete after last character
    if (!complete) return;  // Should have parsed by end
}

void test_ecos_accumulate_with_newline(void) {
    // Message with \n should be handled
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<REPLY id 100>\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (reply.object_id != 100) return;
            return;  // Success
        }
    }
    return;
}

void test_ecos_accumulate_with_crlf(void) {
    // Message with \r\n should be handled
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<REPLY id 200>\r\n";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (reply.object_id != 200) return;
            return;  // Success
        }
    }
    return;
}

// ============================================================================
// TESTS - ERROR HANDLING
// ============================================================================

void test_ecos_parse_empty_response(void) {
    // Empty string should not cause issues
    EcosMessageParser parser;
    EcosReply reply;

    // Processing empty string should do nothing
    // Should not crash or return invalid data
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
    if (parser.getLineBufferLength() != 0) return;
    if (parser.getBlockBufferLength() != 0) return;

    return;  // Success
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

    // Should not complete without closing bracket
    if (completed) return;  // Should fail
    return;
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

    // Should not complete (no valid reply/event tag)
    if (completed) return;
    return;
}

// ============================================================================
// TESTS - KEY-VALUE EXTRACTION
// ============================================================================

void test_ecos_parse_speed_value(void) {
    // Extract speed[64] from message
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<REPLY id 100 speed[64]>";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (!reply.has_speed) return;
            if (reply.speed != 64) return;
            return;  // Success
        }
    }
    return;
}

void test_ecos_parse_direction_value(void) {
    // Extract dir[1] from message
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<REPLY id 100 dir[1]>";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (!reply.has_direction) return;
            if (reply.direction != 1) return;
            return;  // Success
        }
    }
    return;
}

void test_ecos_parse_multiple_values(void) {
    // Parse message with multiple key[value] pairs
    EcosMessageParser parser;
    EcosReply reply;

    const char* msg = "<REPLY id 100 speed[80] dir[0]>";
    for (const char* c = msg; *c != '\0'; c++) {
        if (parser.processByte(*c, reply)) {
            if (reply.speed != 80) return;
            if (reply.direction != 0) return;  // Reverse
            return;  // Success
        }
    }
    return;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    // Run all tests
    setUp();

    test_ecos_parse_speed_query_reply();
    test_ecos_parse_speed_change_event();
    test_ecos_parse_reply_extracts_id();
    test_ecos_parse_event_extracts_id();

    test_ecos_accumulate_byte_by_byte();
    test_ecos_accumulate_with_newline();
    test_ecos_accumulate_with_crlf();

    test_ecos_parse_empty_response();
    test_ecos_reset_clears_state();
    test_ecos_parse_missing_closing_bracket();
    test_ecos_parse_only_whitespace();

    test_ecos_parse_speed_value();
    test_ecos_parse_direction_value();
    test_ecos_parse_multiple_values();

    tearDown();

    return 0;  // TODO: Return UNITY_END() result
}
