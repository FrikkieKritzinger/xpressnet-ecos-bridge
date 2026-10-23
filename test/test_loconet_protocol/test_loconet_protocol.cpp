/*
 * LocoNet Protocol Unit Tests
 *
 * Tests for protocols/loconet/loconet_protocol.h/.cpp - framing, checksum
 * and decoding helpers (no Arduino dependency). Expected values come from
 * the Digitrax LocoNet Personal Use Edition 1.0 spec: bit-7 opcode flag,
 * length in opcode bits 6-5, XOR of a whole valid message == 0xFF. The 15-byte
 * vectors are real frames captured from a live LocoNet, quoted in Gahtow's
 * Z21 firmware comments.
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "protocols/loconet/loconet_protocol.h"

static LnFrameParser parser;
static uint32_t now_ms;

void setUp(void) {
    parser = LnFrameParser();
    now_ms = 1000;
}
void tearDown(void) {}

// Feeds bytes one at a time; returns the result of the last byte.
static LnFrameParser::Result feedAll(const uint8_t* bytes, size_t n) {
    LnFrameParser::Result r = LnFrameParser::NONE;
    for (size_t i = 0; i < n; i++) {
        r = parser.feed(bytes[i], now_ms);
    }
    return r;
}

// ============================================================================
// HELPERS
// ============================================================================

void test_fixed_length_from_opcode_bits(void) {
    TEST_ASSERT_EQUAL_UINT8(2, lnOpcodeFixedLength(LN_OPC_GPON));       // 100xxxxx
    TEST_ASSERT_EQUAL_UINT8(4, lnOpcodeFixedLength(LN_OPC_LOCO_ADR));   // 101xxxxx
    TEST_ASSERT_EQUAL_UINT8(4, lnOpcodeFixedLength(LN_OPC_LOCO_SPD));
    TEST_ASSERT_EQUAL_UINT8(6, lnOpcodeFixedLength(0xC0));              // 110xxxxx
    TEST_ASSERT_EQUAL_UINT8(0, lnOpcodeFixedLength(LN_OPC_SL_RD_DATA)); // 111xxxxx variable
    TEST_ASSERT_EQUAL_UINT8(0, lnOpcodeFixedLength(0x05));              // not an opcode
}

void test_checksum_is_complement_of_xor(void) {
    const uint8_t loco_adr[] = {LN_OPC_LOCO_ADR, 0x00, 0x05};
    TEST_ASSERT_EQUAL_HEX8(0x45, lnChecksum(loco_adr, 3));
    const uint8_t gpon[] = {LN_OPC_GPON};
    TEST_ASSERT_EQUAL_HEX8(0x7C, lnChecksum(gpon, 1));
}

void test_checksum_valid_accepts_good_and_rejects_bad(void) {
    const uint8_t good[] = {0xBF, 0x00, 0x05, 0x45};
    const uint8_t bad[] = {0xBF, 0x00, 0x05, 0x46};
    TEST_ASSERT_TRUE(lnChecksumValid(good, 4));
    TEST_ASSERT_FALSE(lnChecksumValid(bad, 4));
    TEST_ASSERT_FALSE(lnChecksumValid(good, 1));
}

void test_opcode_names(void) {
    TEST_ASSERT_EQUAL_STRING("OPC_GPON", lnOpcodeName(0x83));
    TEST_ASSERT_EQUAL_STRING("OPC_LOCO_ADR", lnOpcodeName(0xBF));
    TEST_ASSERT_EQUAL_STRING("OPC_SL_RD_DATA", lnOpcodeName(0xE7));
    TEST_ASSERT_EQUAL_STRING("OPC_UNKNOWN", lnOpcodeName(0xD4));
}

void test_format_frame(void) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    const uint8_t bytes[] = {0xBF, 0x00, 0x05, 0x45};
    memcpy(f.data, bytes, 4);
    f.len = 4;
    char out[64];
    lnFormatFrame(f, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("BF 00 05 45  OPC_LOCO_ADR", out);
}

void test_format_frame_truncates_safely(void) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    const uint8_t bytes[] = {0xBF, 0x00, 0x05, 0x45};
    memcpy(f.data, bytes, 4);
    f.len = 4;
    char out[8];
    lnFormatFrame(f, out, sizeof(out));
    TEST_ASSERT_TRUE(strlen(out) < sizeof(out));
}

// ============================================================================
// FRAME PARSER - fixed-length messages
// ============================================================================

void test_two_byte_message(void) {
    const uint8_t gpon[] = {0x83, 0x7C};
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, feedAll(gpon, 2));
    TEST_ASSERT_EQUAL_UINT8(2, parser.frame().len);
    TEST_ASSERT_EQUAL_HEX8(0x83, parser.frame().data[0]);
    TEST_ASSERT_EQUAL_UINT32(1, parser.stats().frames_ok);
}

void test_busy_nop_is_a_valid_frame(void) {
    // Spec: <81><7E> is the master's "time burner" NOP.
    const uint8_t busy[] = {0x81, 0x7E};
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, feedAll(busy, 2));
}

void test_four_byte_message(void) {
    const uint8_t loco_adr[] = {0xBF, 0x00, 0x05, 0x45};
    TEST_ASSERT_EQUAL(LnFrameParser::NONE, feedAll(loco_adr, 3));
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, parser.feed(0x45, now_ms));
    TEST_ASSERT_EQUAL_UINT8(4, parser.frame().len);
    TEST_ASSERT_EQUAL_HEX8(0x05, parser.frame().data[2]);
}

void test_bad_checksum_rejected_and_kept_for_logging(void) {
    const uint8_t bad[] = {0xBF, 0x00, 0x05, 0x46};
    TEST_ASSERT_EQUAL(LnFrameParser::BAD_CHECKSUM, feedAll(bad, 4));
    TEST_ASSERT_EQUAL_UINT32(1, parser.stats().bad_checksum);
    TEST_ASSERT_EQUAL_UINT32(0, parser.stats().frames_ok);
    TEST_ASSERT_EQUAL_UINT8(4, parser.rejected().len);
    TEST_ASSERT_EQUAL_HEX8(0x46, parser.rejected().data[3]);
}

void test_back_to_back_messages(void) {
    const uint8_t two[] = {0x83, 0x7C, 0xBF, 0x00, 0x05, 0x45};
    int frames = 0;
    for (size_t i = 0; i < sizeof(two); i++) {
        if (parser.feed(two[i], now_ms) == LnFrameParser::FRAME) {
            frames++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, frames);
    TEST_ASSERT_EQUAL_UINT32(2, parser.stats().frames_ok);
}

// ============================================================================
// FRAME PARSER - variable-length messages (real captured frames)
// ============================================================================

void test_variable_length_peer_xfer_frame(void) {
    const uint8_t frame[] = {0xE5, 0x0F, 0x00, 0x49, 0x4B, 0x0B, 0x00, 0x15,
                             0x00, 0x52, 0x11, 0x15, 0x00, 0x00, 0x5F};
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, feedAll(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(15, parser.frame().len);
    TEST_ASSERT_EQUAL_HEX8(0xE5, parser.frame().data[0]);
}

void test_variable_length_immediate_packet_frame(void) {
    const uint8_t frame[] = {0xED, 0x0F, 0x01, 0x49, 0x42, 0x0D, 0x00, 0x15,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F};
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, feedAll(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(15, parser.frame().len);
}

void test_variable_length_count_too_small_is_framing_error(void) {
    parser.feed(0xE7, now_ms);
    TEST_ASSERT_EQUAL(LnFrameParser::FRAMING_ERROR, parser.feed(0x02, now_ms));
    TEST_ASSERT_FALSE(parser.hasPartial());
    TEST_ASSERT_EQUAL_UINT32(1, parser.stats().framing_errors);
}

void test_variable_length_count_too_large_is_framing_error(void) {
    parser.feed(0xE7, now_ms);
    TEST_ASSERT_EQUAL(LnFrameParser::FRAMING_ERROR, parser.feed(0x7F, now_ms));
    TEST_ASSERT_FALSE(parser.hasPartial());
}

// ============================================================================
// FRAME PARSER - resynchronisation
// ============================================================================

void test_new_opcode_mid_message_aborts_and_restarts(void) {
    // A 4-byte message cut short by a fresh opcode (e.g. a collision break).
    parser.feed(0xBF, now_ms);
    parser.feed(0x00, now_ms);
    TEST_ASSERT_EQUAL(LnFrameParser::FRAMING_ERROR, parser.feed(0x83, now_ms));
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, parser.feed(0x7C, now_ms));
    TEST_ASSERT_EQUAL_HEX8(0x83, parser.frame().data[0]);
    TEST_ASSERT_EQUAL_UINT32(1, parser.stats().framing_errors);
}

void test_stray_data_bytes_outside_a_message_are_ignored(void) {
    TEST_ASSERT_EQUAL(LnFrameParser::NONE, parser.feed(0x12, now_ms));
    TEST_ASSERT_EQUAL(LnFrameParser::NONE, parser.feed(0x00, now_ms));
    TEST_ASSERT_EQUAL_UINT32(2, parser.stats().stray_bytes);
    // ...and the next real message still parses.
    const uint8_t gpon[] = {0x83, 0x7C};
    TEST_ASSERT_EQUAL(LnFrameParser::FRAME, feedAll(gpon, 2));
}

void test_partial_frame_dropped_after_idle_timeout(void) {
    parser.feed(0xBF, now_ms);
    parser.feed(0x00, now_ms);
    TEST_ASSERT_TRUE(parser.hasPartial());

    parser.tick(now_ms + LnFrameParser::PARTIAL_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(parser.hasPartial());

    parser.tick(now_ms + LnFrameParser::PARTIAL_TIMEOUT_MS);
    TEST_ASSERT_FALSE(parser.hasPartial());
    TEST_ASSERT_EQUAL_UINT32(1, parser.stats().timeouts);
}

void test_tick_does_nothing_when_idle(void) {
    parser.tick(now_ms + 100000);
    TEST_ASSERT_EQUAL_UINT32(0, parser.stats().timeouts);
}

void test_byte_counter_counts_everything(void) {
    const uint8_t mixed[] = {0x12, 0x83, 0x7C};
    feedAll(mixed, 3);
    TEST_ASSERT_EQUAL_UINT32(3, parser.stats().bytes);
}

// ============================================================================
// FORCED-DISCONNECT RESYNC DECISION
// ============================================================================

// Arguments: needed, now, last_resync, last_activity, interval, quiet.
void test_resync_due_when_needed_interval_elapsed_and_bus_quiet(void) {
    TEST_ASSERT_TRUE(lnResyncDue(true, 100000, 70000, 90000, 20000, 3000));
}

void test_resync_not_due_when_nothing_changed(void) {
    TEST_ASSERT_FALSE(lnResyncDue(false, 100000, 0, 0, 20000, 3000));
}

void test_resync_not_due_inside_the_interval(void) {
    TEST_ASSERT_FALSE(lnResyncDue(true, 100000, 90000, 0, 20000, 3000));
    TEST_ASSERT_TRUE(lnResyncDue(true, 110000, 90000, 0, 20000, 3000));   // exactly one interval
}

void test_resync_waits_for_the_bus_to_go_quiet(void) {
    // A throttle was active 1 s ago - someone may be turning the knob.
    TEST_ASSERT_FALSE(lnResyncDue(true, 100000, 0, 99000, 20000, 3000));
    TEST_ASSERT_TRUE(lnResyncDue(true, 102000, 0, 99000, 20000, 3000));   // 3 s of quiet
}

void test_resync_disabled_by_a_zero_interval(void) {
    TEST_ASSERT_FALSE(lnResyncDue(true, 100000, 0, 0, 0, 3000));
}

void test_resync_survives_the_millisecond_counter_wrapping(void) {
    uint32_t last = 0xFFFFF000u;                  // just before the wrap
    TEST_ASSERT_FALSE(lnResyncDue(true, 0x00001000u, last, 0, 20000, 3000));   // 8 s later
    TEST_ASSERT_TRUE(lnResyncDue(true, 0x00005000u, last, 0, 20000, 3000));    // 24 s later
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_fixed_length_from_opcode_bits);
    RUN_TEST(test_checksum_is_complement_of_xor);
    RUN_TEST(test_checksum_valid_accepts_good_and_rejects_bad);
    RUN_TEST(test_opcode_names);
    RUN_TEST(test_format_frame);
    RUN_TEST(test_format_frame_truncates_safely);

    RUN_TEST(test_two_byte_message);
    RUN_TEST(test_busy_nop_is_a_valid_frame);
    RUN_TEST(test_four_byte_message);
    RUN_TEST(test_bad_checksum_rejected_and_kept_for_logging);
    RUN_TEST(test_back_to_back_messages);

    RUN_TEST(test_variable_length_peer_xfer_frame);
    RUN_TEST(test_variable_length_immediate_packet_frame);
    RUN_TEST(test_variable_length_count_too_small_is_framing_error);
    RUN_TEST(test_variable_length_count_too_large_is_framing_error);

    RUN_TEST(test_new_opcode_mid_message_aborts_and_restarts);
    RUN_TEST(test_stray_data_bytes_outside_a_message_are_ignored);
    RUN_TEST(test_partial_frame_dropped_after_idle_timeout);
    RUN_TEST(test_tick_does_nothing_when_idle);
    RUN_TEST(test_byte_counter_counts_everything);

    RUN_TEST(test_resync_due_when_needed_interval_elapsed_and_bus_quiet);
    RUN_TEST(test_resync_not_due_when_nothing_changed);
    RUN_TEST(test_resync_not_due_inside_the_interval);
    RUN_TEST(test_resync_waits_for_the_bus_to_go_quiet);
    RUN_TEST(test_resync_disabled_by_a_zero_interval);
    RUN_TEST(test_resync_survives_the_millisecond_counter_wrapping);

    return UNITY_END();
}
