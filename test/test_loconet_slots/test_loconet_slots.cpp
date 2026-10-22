/*
 * LocoNet Slot Server Unit Tests
 *
 * Tests for protocols/loconet/loconet_slots.h/.cpp - the master-side slot
 * handling. Expected behaviour is from the Digitrax LocoNet Personal Use
 * Edition 1.0 spec; the request frames are real captures from a Digitrax UT6
 * on the bridge's own LocoNet (2026-10-22).
 */

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <unity.h>
#include "protocols/loconet/loconet_slots.h"

static LnSlotServer server;
static uint32_t now_ms;

void setUp(void) {
    server = LnSlotServer();
    now_ms = 1000;
}
void tearDown(void) {}

// Builds a frame from the given bytes (checksum appended) and feeds it in.
static void send(std::initializer_list<uint8_t> bytes) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    uint8_t n = 0;
    for (uint8_t b : bytes) {
        f.data[n++] = b;
    }
    f.data[n] = lnChecksum(f.data, n);
    f.len = n + 1;
    TEST_ASSERT_TRUE(lnChecksumValid(f.data, f.len));
    server.handleFrame(f, now_ms);
}

static LnFrame popReply() {
    LnFrame f;
    TEST_ASSERT_TRUE(server.peekReply(f));
    server.dropReply();
    TEST_ASSERT_TRUE(lnChecksumValid(f.data, f.len));
    return f;
}

// ============================================================================
// REPLY FORMAT
// ============================================================================

void test_slot_read_layout(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(14, r.len);
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(14, r.data[1]);       // byte count
    TEST_ASSERT_EQUAL_UINT8(1, r.data[2]);        // slot number
    TEST_ASSERT_EQUAL_HEX8(0x13, r.data[3]);      // COMMON | 128 steps
    TEST_ASSERT_EQUAL_UINT8(3, r.data[4]);        // address low
    TEST_ASSERT_EQUAL_UINT8(0, r.data[5]);        // speed 0
    TEST_ASSERT_EQUAL_HEX8(0x20, r.data[6]);      // forward, functions off
    TEST_ASSERT_EQUAL_HEX8(0x07, r.data[7]);      // TRK: power, running, LocoNet 1.1
    TEST_ASSERT_EQUAL_UINT8(0, r.data[9]);        // address high
}

// ============================================================================
// OPC_LOCO_ADR
// ============================================================================

void test_loco_adr_loads_new_address_into_first_free_slot(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});   // real capture: BF 00 03 43
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(1, r.data[2]);
    TEST_ASSERT_EQUAL_UINT16(3, server.slot(1).address);
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_loco_adr_same_address_returns_same_slot(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    TEST_ASSERT_EQUAL_UINT8(1, popReply().data[2]);
}

void test_loco_adr_different_addresses_get_different_slots(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    send({LN_OPC_LOCO_ADR, 0x00, 0x0C});
    TEST_ASSERT_EQUAL_UINT8(1, popReply().data[2]);
    TEST_ASSERT_EQUAL_UINT8(2, popReply().data[2]);
}

void test_loco_adr_long_address_split_across_two_bytes(void) {
    send({LN_OPC_LOCO_ADR, 0x09, 0x3B});   // real capture: BF 09 3B 72 = 1211
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT16(1211, server.slot(1).address);
    TEST_ASSERT_EQUAL_UINT8(0x3B, r.data[4]);   // ADR  = low 7 bits
    TEST_ASSERT_EQUAL_UINT8(0x09, r.data[9]);   // ADR2 = high 7 bits
}

void test_loco_adr_address_zero_is_answered(void) {
    // The UT6 probes address 0 (analog loco) as it starts an entry; an
    // unanswered request makes it retry, so it gets a normal slot.
    send({LN_OPC_LOCO_ADR, 0x00, 0x00});   // real capture: BF 00 00 40
    TEST_ASSERT_EQUAL_UINT8(1, popReply().data[2]);
}

void test_loco_adr_with_no_free_slot_replies_long_ack_fail(void) {
    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        send({LN_OPC_LOCO_ADR, 0x00, i});
        popReply();
        send({LN_OPC_MOVE_SLOTS, i, i});          // every slot IN_USE
        popReply();
    }
    send({LN_OPC_LOCO_ADR, 0x00, 100});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, r.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, r.data[2]);
}

void test_loco_adr_reclaims_oldest_unused_slot_when_table_is_full(void) {
    // Probing addresses fills the table with COMMON slots; the oldest one is
    // recycled rather than refusing the request.
    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        now_ms += 10;
        send({LN_OPC_LOCO_ADR, 0x00, i});
        popReply();
    }
    now_ms += 10;
    send({LN_OPC_LOCO_ADR, 0x00, 100});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, r.data[2]);          // slot 1 was the oldest
    TEST_ASSERT_EQUAL_UINT16(100, server.slot(1).address);
}

// ============================================================================
// OPC_RQ_SL_DATA
// ============================================================================

void test_rq_sl_data_returns_the_slot(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x05});
    popReply();
    send({LN_OPC_RQ_SL_DATA, 0x01, 0x00});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(5, r.data[4]);
}

void test_rq_sl_data_for_unused_slot_reports_it_free(void) {
    // Real capture: BB 01 00 45 on power-up, before anything was loaded. A
    // FREE answer tells the throttle its old slot was purged so it logs on.
    send({LN_OPC_RQ_SL_DATA, 0x01, 0x00});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, r.data[3] & LN_STAT1_BUSY_MASK);
}

void test_rq_sl_data_for_system_slot_is_ignored(void) {
    send({LN_OPC_RQ_SL_DATA, 124, 0x00});
    send({LN_OPC_RQ_SL_DATA, 0x00, 0x00});
    TEST_ASSERT_FALSE(server.hasReply());
}

// ============================================================================
// OPC_MOVE_SLOTS
// ============================================================================

void test_null_move_selects_the_slot(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});   // real capture: BA 01 01 45
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(LN_STAT1_IN_USE, r.data[3] & LN_STAT1_BUSY_MASK);
    TEST_ASSERT_EQUAL_HEX8(LN_STAT1_IN_USE, server.slot(1).stat1 & LN_STAT1_BUSY_MASK);
}

void test_null_move_succeeds_even_when_already_in_use(void) {
    // Stealing is free by design: a second throttle selecting an IN_USE slot
    // is never refused.
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
}

void test_null_move_on_a_free_slot_is_refused(void) {
    send({LN_OPC_MOVE_SLOTS, 0x05, 0x05});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3A, r.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, r.data[2]);
}

void test_dispatch_put_marks_the_slot_and_releases_it(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x00});   // real capture: BA 01 00 44
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(1, server.dispatchSlot());
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(LN_STAT1_COMMON, server.slot(1).stat1 & LN_STAT1_BUSY_MASK);
}

void test_dispatch_get_returns_the_dispatched_slot_in_use(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x00});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x00, 0x00});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, r.data[2]);
    TEST_ASSERT_EQUAL_HEX8(LN_STAT1_IN_USE, r.data[3] & LN_STAT1_BUSY_MASK);
    TEST_ASSERT_EQUAL_UINT8(0, server.dispatchSlot());
}

void test_dispatch_get_with_nothing_dispatched_fails(void) {
    send({LN_OPC_MOVE_SLOTS, 0x00, 0x00});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3A, r.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, r.data[2]);
}

void test_move_between_two_slots_is_refused(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x07});
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, popReply().data[0]);
}

// ============================================================================
// SLOT UPDATES FROM THROTTLES
// ============================================================================

void test_speed_dirf_and_sound_update_the_slot_without_a_reply(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_LOCO_SPD, 0x01, 0x40});
    send({LN_OPC_LOCO_DIRF, 0x01, 0x11});    // reverse, F0 on, F1 on
    send({LN_OPC_LOCO_SND, 0x01, 0x05});
    TEST_ASSERT_FALSE(server.hasReply());
    TEST_ASSERT_EQUAL_UINT8(0x40, server.slot(1).speed);
    TEST_ASSERT_EQUAL_HEX8(0x11, server.slot(1).dirf);
    TEST_ASSERT_EQUAL_HEX8(0x05, server.slot(1).snd);
}

void test_updates_for_invalid_slots_are_ignored(void) {
    send({LN_OPC_LOCO_SPD, 0x00, 0x40});
    send({LN_OPC_LOCO_SPD, 0x7B, 0x40});
    send({LN_OPC_LOCO_SPD, 0x7F, 0x40});
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_slot_stat1_write_updates_status(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    send({LN_OPC_SLOT_STAT1, 0x01, 0x33});
    TEST_ASSERT_EQUAL_HEX8(0x33, server.slot(1).stat1);
    TEST_ASSERT_FALSE(server.hasReply());
}

// ============================================================================
// OPC_WR_SL_DATA
// ============================================================================

void test_write_slot_data_updates_slot_and_acknowledges(void) {
    send({LN_OPC_WR_SL_DATA, 14, 0x02, 0x33, 0x2A, 0x30, 0x25, 0x07,
          0x00, 0x00, 0x03, 0x00, 0x00});
    TEST_ASSERT_EQUAL_UINT16(42, server.slot(2).address);
    TEST_ASSERT_EQUAL_UINT8(0x30, server.slot(2).speed);
    TEST_ASSERT_EQUAL_HEX8(0x25, server.slot(2).dirf);
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x6F, r.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, r.data[2]);
}

void test_write_to_programming_slot_is_refused_as_not_implemented(void) {
    send({LN_OPC_WR_SL_DATA, 14, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, r.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, r.data[2]);   // spec: function not implemented
}

// ============================================================================
// TRACK POWER
// ============================================================================

void test_power_frames_change_the_track_status_byte(void) {
    TEST_ASSERT_EQUAL_HEX8(0x07, server.trackStatus());
    send({LN_OPC_GPOFF});
    TEST_ASSERT_EQUAL_HEX8(0x06, server.trackStatus());
    send({LN_OPC_GPON});
    TEST_ASSERT_EQUAL_HEX8(0x07, server.trackStatus());
    send({LN_OPC_IDLE});
    TEST_ASSERT_EQUAL_HEX8(0x05, server.trackStatus());
    TEST_ASSERT_FALSE(server.hasReply());
}

// ============================================================================
// REPLY QUEUE
// ============================================================================

void test_replies_come_out_in_order(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    send({LN_OPC_LOCO_ADR, 0x00, 0x04});
    send({LN_OPC_LOCO_ADR, 0x00, 0x05});
    TEST_ASSERT_EQUAL_UINT8(3, popReply().data[4]);
    TEST_ASSERT_EQUAL_UINT8(4, popReply().data[4]);
    TEST_ASSERT_EQUAL_UINT8(5, popReply().data[4]);
}

void test_full_reply_queue_drops_the_oldest(void) {
    for (uint8_t a = 1; a <= 8; a++) {       // queue holds 6
        send({LN_OPC_LOCO_ADR, 0x00, a});
    }
    TEST_ASSERT_EQUAL_UINT32(2, server.repliesDropped());
    TEST_ASSERT_EQUAL_UINT8(3, popReply().data[4]);   // 1 and 2 were dropped
}

void test_short_or_unknown_frames_are_ignored(void) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    f.len = 1;
    server.handleFrame(f, now_ms);
    send({0xD0, 0x01, 0x02});          // an opcode the server does not handle
    send({LN_OPC_SW_REQ, 0x01, 0x10}); // turnouts are not a slot-server job
    TEST_ASSERT_FALSE(server.hasReply());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_slot_read_layout);

    RUN_TEST(test_loco_adr_loads_new_address_into_first_free_slot);
    RUN_TEST(test_loco_adr_same_address_returns_same_slot);
    RUN_TEST(test_loco_adr_different_addresses_get_different_slots);
    RUN_TEST(test_loco_adr_long_address_split_across_two_bytes);
    RUN_TEST(test_loco_adr_address_zero_is_answered);
    RUN_TEST(test_loco_adr_with_no_free_slot_replies_long_ack_fail);
    RUN_TEST(test_loco_adr_reclaims_oldest_unused_slot_when_table_is_full);

    RUN_TEST(test_rq_sl_data_returns_the_slot);
    RUN_TEST(test_rq_sl_data_for_unused_slot_reports_it_free);
    RUN_TEST(test_rq_sl_data_for_system_slot_is_ignored);

    RUN_TEST(test_null_move_selects_the_slot);
    RUN_TEST(test_null_move_succeeds_even_when_already_in_use);
    RUN_TEST(test_null_move_on_a_free_slot_is_refused);
    RUN_TEST(test_dispatch_put_marks_the_slot_and_releases_it);
    RUN_TEST(test_dispatch_get_returns_the_dispatched_slot_in_use);
    RUN_TEST(test_dispatch_get_with_nothing_dispatched_fails);
    RUN_TEST(test_move_between_two_slots_is_refused);

    RUN_TEST(test_speed_dirf_and_sound_update_the_slot_without_a_reply);
    RUN_TEST(test_updates_for_invalid_slots_are_ignored);
    RUN_TEST(test_slot_stat1_write_updates_status);

    RUN_TEST(test_write_slot_data_updates_slot_and_acknowledges);
    RUN_TEST(test_write_to_programming_slot_is_refused_as_not_implemented);

    RUN_TEST(test_power_frames_change_the_track_status_byte);

    RUN_TEST(test_replies_come_out_in_order);
    RUN_TEST(test_full_reply_queue_drops_the_oldest);
    RUN_TEST(test_short_or_unknown_frames_are_ignored);

    return UNITY_END();
}
