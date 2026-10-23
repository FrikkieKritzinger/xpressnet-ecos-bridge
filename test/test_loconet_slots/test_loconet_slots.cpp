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
static LnSlotEvent send(std::initializer_list<uint8_t> bytes) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    uint8_t n = 0;
    for (uint8_t b : bytes) {
        f.data[n++] = b;
    }
    f.data[n] = lnChecksum(f.data, n);
    f.len = n + 1;
    TEST_ASSERT_TRUE(lnChecksumValid(f.data, f.len));
    return server.handleFrame(f, now_ms);
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
    TEST_ASSERT_EQUAL_HEX8(0x06, r.data[7]);      // TRK: running, LocoNet 1.1, power OFF until Ecos says GO
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

void test_loco_adr_never_reports_a_slot_as_in_use(void) {
    // A slot a throttle already holds must not make the next select ask the
    // user to "steal" it - stealing is free here.
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});          // now IN_USE
    while (server.hasReply()) popReply();

    send({LN_OPC_LOCO_ADR, 0x00, 0x03});            // select the same loco again
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, r.data[2]);          // same slot
    TEST_ASSERT_EQUAL_HEX8(LN_STAT1_COMMON, r.data[3] & LN_STAT1_BUSY_MASK);

    // ...and the throttle's null move still takes it.
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    TEST_ASSERT_EQUAL_HEX8(LN_STAT1_IN_USE, popReply().data[3] & LN_STAT1_BUSY_MASK);
}

void test_null_move_on_a_free_slot_answers_with_the_empty_slot(void) {
    // Not a LONG_ACK refusal: that makes a Digitrax throttle ask the user to
    // "steal" the loco. The empty slot read sends it back to selecting.
    LnSlotEvent ev = send({LN_OPC_MOVE_SLOTS, 0x05, 0x05});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_NONE, ev.flags);
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(5, r.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0, r.data[3] & LN_STAT1_BUSY_MASK);   // FREE
    TEST_ASSERT_EQUAL_UINT8(0, r.data[4]);                        // no address
}

void test_drive_frames_for_an_empty_slot_are_not_stored(void) {
    send({LN_OPC_LOCO_SPD, 0x01, 0x7E});
    send({LN_OPC_LOCO_DIRF, 0x01, 0x10});
    send({LN_OPC_LOCO_SND, 0x01, 0x05});
    TEST_ASSERT_EQUAL_UINT8(0, server.slot(1).speed);
    TEST_ASSERT_EQUAL_UINT8(0, server.slot(1).dirf);
    TEST_ASSERT_EQUAL_UINT8(0, server.slot(1).snd);
}

void test_moving_a_free_slot_elsewhere_is_still_refused(void) {
    send({LN_OPC_MOVE_SLOTS, 0x05, 0x07});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3A, r.data[1]);
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
    TEST_ASSERT_EQUAL_HEX8(0x06, server.trackStatus());   // power off until told otherwise
    send({LN_OPC_GPON});
    TEST_ASSERT_EQUAL_HEX8(0x07, server.trackStatus());
    send({LN_OPC_GPOFF});
    TEST_ASSERT_EQUAL_HEX8(0x06, server.trackStatus());
    send({LN_OPC_GPON});
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
    for (uint8_t a = 1; a <= 10; a++) {      // queue holds 8
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

// ============================================================================
// ROUTING EVENTS (what a throttle's frame changed, for the other protocols)
// ============================================================================

// Loads address 3 into slot 1 and selects it (IN_USE), draining the replies.
static void selectLoco3() {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    while (server.hasReply()) {
        popReply();
    }
}

void test_speed_conversion_between_loconet_and_bridge(void) {
    TEST_ASSERT_EQUAL_UINT8(0, lnSpeedToBridge(0));
    TEST_ASSERT_EQUAL_UINT8(0, lnSpeedToBridge(1));     // 1 = emergency stop
    TEST_ASSERT_EQUAL_UINT8(1, lnSpeedToBridge(2));
    TEST_ASSERT_EQUAL_UINT8(126, lnSpeedToBridge(127));
    TEST_ASSERT_EQUAL_UINT8(0, lnSpeedFromBridge(0));
    TEST_ASSERT_EQUAL_UINT8(2, lnSpeedFromBridge(1));
    TEST_ASSERT_EQUAL_UINT8(127, lnSpeedFromBridge(126));
}

void test_speed_frame_reports_a_drive_event(void) {
    selectLoco3();
    LnSlotEvent ev = send({LN_OPC_LOCO_SPD, 0x01, 0x10});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_DRIVE, ev.flags);
    TEST_ASSERT_EQUAL_UINT16(3, ev.address);
    TEST_ASSERT_EQUAL_UINT8(15, ev.speed);
    TEST_ASSERT_EQUAL_UINT8(1, ev.direction);
}

void test_repeating_the_same_speed_reports_nothing(void) {
    selectLoco3();
    send({LN_OPC_LOCO_SPD, 0x01, 0x10});
    LnSlotEvent ev = send({LN_OPC_LOCO_SPD, 0x01, 0x10});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_NONE, ev.flags);
}

void test_emergency_stop_speed_maps_to_bridge_speed_zero(void) {
    selectLoco3();
    send({LN_OPC_LOCO_SPD, 0x01, 0x10});
    LnSlotEvent ev = send({LN_OPC_LOCO_SPD, 0x01, 0x01});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_DRIVE, ev.flags);
    TEST_ASSERT_EQUAL_UINT8(0, ev.speed);
}

void test_direction_bit_is_a_drive_event_not_a_function_event(void) {
    selectLoco3();
    LnSlotEvent ev = send({LN_OPC_LOCO_DIRF, 0x01, 0x00});   // real capture: reverse
    TEST_ASSERT_EQUAL_HEX8(LN_EV_DRIVE, ev.flags);
    TEST_ASSERT_EQUAL_UINT8(0, ev.direction);
    ev = send({LN_OPC_LOCO_DIRF, 0x01, 0x20});               // forward again
    TEST_ASSERT_EQUAL_HEX8(LN_EV_DRIVE, ev.flags);
    TEST_ASSERT_EQUAL_UINT8(1, ev.direction);
}

void test_dirf_function_bits_map_to_f0_to_f4(void) {
    selectLoco3();
    LnSlotEvent ev = send({LN_OPC_LOCO_DIRF, 0x01, 0x30});   // forward + F0 (real capture)
    TEST_ASSERT_EQUAL_HEX8(LN_EV_FUNCTIONS, ev.flags);
    TEST_ASSERT_EQUAL_HEX32(0x1FF, ev.func_mask);
    TEST_ASSERT_EQUAL_HEX32(0x001, ev.func_bits);

    ev = send({LN_OPC_LOCO_DIRF, 0x01, 0x29});               // forward + F1 + F4 (real: 09)
    TEST_ASSERT_EQUAL_HEX32(0x012, ev.func_bits);
}

void test_snd_function_bits_map_to_f5_to_f8(void) {
    selectLoco3();
    LnSlotEvent ev = send({LN_OPC_LOCO_SND, 0x01, 0x08});    // real capture: F8
    TEST_ASSERT_EQUAL_HEX8(LN_EV_FUNCTIONS, ev.flags);
    TEST_ASSERT_EQUAL_HEX32(0x100, ev.func_bits);
    ev = send({LN_OPC_LOCO_SND, 0x01, 0x04});               // F7
    TEST_ASSERT_EQUAL_HEX32(0x080, ev.func_bits);
}

void test_functions_f9_to_f12_arrive_as_a_dcc_packet(void) {
    // Real capture: ED 0B 7F 24 02 03 21 00 00 00 62 = loco 3, 0xA1 = F9 on.
    LnSlotEvent ev = send({LN_OPC_IMM_PACKET, 0x0B, 0x7F, 0x24, 0x02, 0x03, 0x21, 0x00, 0x00, 0x00});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_FUNCTIONS, ev.flags);
    TEST_ASSERT_EQUAL_UINT16(3, ev.address);
    TEST_ASSERT_EQUAL_HEX32(0x1E00, ev.func_mask);
    TEST_ASSERT_EQUAL_HEX32(0x0200, ev.func_bits);
    LnFrame ack = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, ack.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7D, ack.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, ack.data[2]);
}

void test_functions_f13_to_f20_use_the_0xde_packet(void) {
    // Real capture: ...34 02 03 5E 11 = loco 3, 0xDE group, data 0x11 = F13+F17.
    LnSlotEvent ev = send({LN_OPC_IMM_PACKET, 0x0B, 0x7F, 0x34, 0x02, 0x03, 0x5E, 0x11, 0x00, 0x00});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_FUNCTIONS, ev.flags);
    TEST_ASSERT_EQUAL_HEX32(0x1FE000, ev.func_mask);
    TEST_ASSERT_EQUAL_HEX32(0x22000, ev.func_bits);
}

void test_functions_f21_to_f28_use_the_0xdf_packet(void) {
    // Real capture: ...34 02 03 5F 08 = 0xDF group, data 0x08 = F24.
    LnSlotEvent ev = send({LN_OPC_IMM_PACKET, 0x0B, 0x7F, 0x34, 0x02, 0x03, 0x5F, 0x08, 0x00, 0x00});
    TEST_ASSERT_EQUAL_HEX32(0x1FE00000, ev.func_mask);
    TEST_ASSERT_EQUAL_HEX32(0x01000000, ev.func_bits);
}

void test_immediate_packet_with_a_long_address(void) {
    // Long address 1211 = 0xC4 0xBB, then 0xA1 (F9): the three bit-7s sit in DHI.
    LnSlotEvent ev = send({LN_OPC_IMM_PACKET, 0x0B, 0x7F, 0x34, 0x07, 0x44, 0x3B, 0x21, 0x00, 0x00});
    TEST_ASSERT_EQUAL_UINT16(1211, ev.address);
    TEST_ASSERT_EQUAL_HEX32(0x0200, ev.func_bits);
}

void test_unrecognised_immediate_packet_is_acknowledged_but_not_reported(void) {
    LnSlotEvent ev = send({LN_OPC_IMM_PACKET, 0x0B, 0x7F, 0x24, 0x00, 0x03, 0x40, 0x00, 0x00, 0x00});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_NONE, ev.flags);
    TEST_ASSERT_TRUE(server.hasReply());
}

void test_slot_write_right_after_selecting_is_not_a_command(void) {
    // The UT6 writes the slot back (EF) as it selects; that repeats what we
    // told it and must never reach the layout.
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    LnSlotEvent ev = send({LN_OPC_WR_SL_DATA, 14, 0x01, 0x33, 0x03, 0x30, 0x20, 0x07,
                           0x00, 0x00, 0x00, 0x4F, 0x64});
    // It takes the slot, so it is a selection - but never a drive command.
    TEST_ASSERT_EQUAL_HEX8(LN_EV_SELECT, ev.flags);
    TEST_ASSERT_EQUAL_UINT16(3, ev.address);
}

void test_null_move_reports_a_selection(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    LnSlotEvent ev = send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_SELECT, ev.flags);
    TEST_ASSERT_EQUAL_UINT16(3, ev.address);
}

void test_loco_adr_probe_is_not_a_selection(void) {
    // The UT6 probes an address on every key press; only taking the slot counts.
    LnSlotEvent ev = send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_NONE, ev.flags);
}

void test_slot_status_write_to_in_use_reports_a_selection(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    LnSlotEvent ev = send({LN_OPC_SLOT_STAT1, 0x01, 0x33});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_SELECT, ev.flags);
    TEST_ASSERT_EQUAL_UINT16(3, ev.address);
    // Releasing it (what the UT6 does when you leave a loco) is not one.
    ev = send({LN_OPC_SLOT_STAT1, 0x01, 0x13});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_NONE, ev.flags);
}

void test_dispatch_get_reports_a_selection(void) {
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x00});
    LnSlotEvent ev = send({LN_OPC_MOVE_SLOTS, 0x00, 0x00});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_SELECT, ev.flags);
    TEST_ASSERT_EQUAL_UINT16(3, ev.address);
}

void test_slot_write_on_a_held_slot_reports_what_changed(void) {
    selectLoco3();
    LnSlotEvent ev = send({LN_OPC_WR_SL_DATA, 14, 0x01, 0x33, 0x03, 0x30, 0x20, 0x07,
                           0x00, 0x00, 0x00, 0x4F, 0x64});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_DRIVE, ev.flags);
    TEST_ASSERT_EQUAL_UINT8(47, ev.speed);
}

void test_power_frames_report_events(void) {
    TEST_ASSERT_EQUAL_HEX8(LN_EV_POWER_ON, send({LN_OPC_GPON}).flags);
    TEST_ASSERT_EQUAL_HEX8(LN_EV_POWER_OFF, send({LN_OPC_GPOFF}).flags);
    TEST_ASSERT_EQUAL_HEX8(LN_EV_ESTOP, send({LN_OPC_IDLE}).flags);
}

// ============================================================================
// SLOTS LOADED WITH THE LOCO'S REAL STATE
// ============================================================================

static LnLocoSnapshot g_snapshot;
static int g_lookup_calls;
static bool lookupStub(uint16_t address, LnLocoSnapshot* out, void* context) {
    (void)context;
    g_lookup_calls++;
    if (address != 3) {
        return false;
    }
    *out = g_snapshot;
    return true;
}

void test_selecting_a_moving_loco_shows_its_real_state(void) {
    g_snapshot.speed = 40;
    g_snapshot.direction = 0;
    g_snapshot.functions = 0x01 | 0x04;     // F0 and F2
    g_lookup_calls = 0;
    server.setLocoLookup(lookupStub, nullptr);

    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(41, r.data[5]);          // speed 40 -> LocoNet 41
    TEST_ASSERT_EQUAL_HEX8(0x12, r.data[6]);         // reverse, F0 (0x10) + F2 (0x02)
    TEST_ASSERT_EQUAL_UINT8(1, g_lookup_calls);
}

void test_unknown_loco_falls_back_to_a_stopped_forward_slot(void) {
    server.setLocoLookup(lookupStub, nullptr);
    send({LN_OPC_LOCO_ADR, 0x00, 0x09});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(0, r.data[5]);
    TEST_ASSERT_EQUAL_HEX8(0x20, r.data[6]);
}

void test_selection_refreshes_from_current_state_again(void) {
    g_snapshot.speed = 10;
    g_snapshot.direction = 1;
    g_snapshot.functions = 0;
    server.setLocoLookup(lookupStub, nullptr);
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    popReply();
    g_snapshot.speed = 60;                           // someone else changed it
    send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(61, r.data[5]);
}

// ============================================================================
// CHANGES FROM OTHER PROTOCOLS PUSHED TO THE THROTTLE
// ============================================================================

void test_external_speed_is_pushed_as_a_speed_frame(void) {
    selectLoco3();
    TEST_ASSERT_EQUAL_UINT8(1, server.applyExternalDrive(3, 50, 1, now_ms));
    LnFrame f = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LOCO_SPD, f.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, f.data[1]);           // slot
    TEST_ASSERT_EQUAL_UINT8(51, f.data[2]);          // bridge 50 -> LocoNet 51
    TEST_ASSERT_FALSE(server.hasReply());            // direction unchanged
    TEST_ASSERT_EQUAL_UINT8(51, server.slot(1).speed);
}

void test_external_change_that_matches_the_slot_sends_nothing(void) {
    selectLoco3();
    server.applyExternalDrive(3, 50, 1, now_ms);
    popReply();
    server.applyExternalDrive(3, 50, 1, now_ms);
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_external_direction_change_is_pushed_as_dirf(void) {
    selectLoco3();
    server.applyExternalDrive(3, 0, 0, now_ms);
    LnFrame f = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LOCO_DIRF, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[2]);         // reverse, no functions
}

void test_external_functions_are_pushed_as_dirf_and_snd(void) {
    selectLoco3();
    TEST_ASSERT_EQUAL_UINT8(1, server.applyExternalFunctions(3, 0x01 | 0x20, now_ms));   // F0 + F5
    LnFrame dirf = popReply();
    LnFrame snd = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LOCO_DIRF, dirf.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x30, dirf.data[2]);      // forward + F0
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LOCO_SND, snd.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, snd.data[2]);       // F5
}

void test_external_change_for_a_loco_no_throttle_holds_does_nothing(void) {
    selectLoco3();
    TEST_ASSERT_EQUAL_UINT8(0, server.applyExternalDrive(99, 50, 1, now_ms));
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_external_emergency_stop_and_resume_queue_the_bus_frames(void) {
    server.applyExternalResume();
    LnFrame on = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_GPON, on.data[0]);
    TEST_ASSERT_EQUAL_HEX8(LN_TRK_POWER | LN_TRK_RUNNING, server.trackStatus() & 0x03);

    // A stop from another protocol is "track power off" (what the UT6's own
    // power button sends), not LocoNet's emergency-stop broadcast.
    server.applyExternalEmergencyStop();
    LnFrame off = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_GPOFF, off.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, server.trackStatus() & LN_TRK_POWER);
    TEST_ASSERT_EQUAL_UINT8(LN_TRK_RUNNING, server.trackStatus() & LN_TRK_RUNNING);
}

// Real capture after a bridge reboot: the UT6 still remembered slot 1 and sent
// B5 01 13 (release) then BA 01 01 (select) into an EMPTY slot table.
void test_status_write_to_an_empty_slot_is_ignored(void) {
    send({LN_OPC_SLOT_STAT1, 0x01, 0x13});
    TEST_ASSERT_EQUAL_UINT8(0, server.slot(1).stat1);
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_select_after_reboot_reads_as_empty_so_the_throttle_logs_on_again(void) {
    send({LN_OPC_SLOT_STAT1, 0x01, 0x13});
    LnSlotEvent ev = send({LN_OPC_MOVE_SLOTS, 0x01, 0x01});
    TEST_ASSERT_EQUAL_HEX8(LN_EV_NONE, ev.flags);   // no phantom selection
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, r.data[3] & LN_STAT1_BUSY_MASK);   // reads as empty
    // ...and a normal log-on then works.
    send({LN_OPC_LOCO_ADR, 0x00, 0x03});
    TEST_ASSERT_EQUAL_UINT8(1, popReply().data[2]);
}

// ============================================================================
// COALESCED SLOT READS FOR THROTTLE DISPLAYS
// ============================================================================

void test_external_change_marks_the_slot_for_a_pushed_slot_read(void) {
    selectLoco3();
    server.applyExternalDrive(3, 50, 1, now_ms);
    while (server.hasReply()) popReply();       // drop the per-change frames

    now_ms += LN_SLOT_PUSH_INTERVAL_MS;
    server.pushDirtySlots(now_ms);
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, r.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, r.data[2]);
    TEST_ASSERT_EQUAL_UINT8(51, r.data[5]);     // carries the new speed
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_a_burst_of_changes_becomes_one_up_to_date_slot_read(void) {
    selectLoco3();
    server.pushDirtySlots(now_ms + 1000);       // nothing dirty yet
    for (uint8_t speed = 10; speed <= 40; speed += 10) {
        server.applyExternalDrive(3, speed, 1, now_ms);
    }
    while (server.hasReply()) popReply();

    server.pushDirtySlots(now_ms + 2000);
    LnFrame r = popReply();
    TEST_ASSERT_EQUAL_UINT8(41, r.data[5]);     // the last value, 40 -> 41
    TEST_ASSERT_FALSE(server.hasReply());
    server.pushDirtySlots(now_ms + 3000);
    TEST_ASSERT_FALSE(server.hasReply());       // nothing left to push
}

void test_slot_reads_for_one_slot_are_rate_limited(void) {
    selectLoco3();
    server.applyExternalDrive(3, 10, 1, now_ms);
    while (server.hasReply()) popReply();
    server.pushDirtySlots(now_ms + 1000);
    popReply();

    server.applyExternalDrive(3, 20, 1, now_ms + 1010);
    while (server.hasReply()) popReply();
    // The interval runs from the previous push (at +1000), not from the change.
    server.pushDirtySlots(now_ms + 1000 + LN_SLOT_PUSH_INTERVAL_MS - 1);
    TEST_ASSERT_FALSE(server.hasReply());       // too soon, stays dirty
    server.pushDirtySlots(now_ms + 1000 + LN_SLOT_PUSH_INTERVAL_MS);
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, popReply().data[0]);
}

void test_byte_frames_can_be_switched_off(void) {
    selectLoco3();
    server.setPushByteFrames(false);
    server.applyExternalDrive(3, 50, 1, now_ms);
    server.applyExternalFunctions(3, 0x01, now_ms);
    TEST_ASSERT_FALSE(server.hasReply());       // no A0/A1/A2 ...
    server.pushDirtySlots(now_ms + 1000);
    TEST_ASSERT_TRUE(server.hasReply());        // ... but the slot read still goes out
}

// ============================================================================
// DEV EXPERIMENT: imitate a second throttle taking over a held slot
// ============================================================================

void test_simulated_steal_emits_the_frames_a_thief_produces(void) {
    selectLoco3();
    TEST_ASSERT_EQUAL_UINT8(1, server.simulateSteal(0x05, 0x0B, 40, now_ms));

    LnFrame move = popReply();                       // the thief's null move
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_MOVE_SLOTS, move.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, move.data[1]);
    TEST_ASSERT_EQUAL_UINT8(1, move.data[2]);

    LnFrame read = popReply();                       // the master's slot read
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_SL_RD_DATA, read.data[0]);

    LnFrame write = popReply();                      // the thief's slot write
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_WR_SL_DATA, write.data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, write.data[2]);
    TEST_ASSERT_EQUAL_UINT8(40, write.data[5]);      // new speed
    TEST_ASSERT_EQUAL_HEX8(0x10, write.data[6]);     // direction flipped, F0 on
    TEST_ASSERT_EQUAL_HEX8(0x05, write.data[11]);    // the thief's ID, not the holder's
    TEST_ASSERT_EQUAL_HEX8(0x0B, write.data[12]);

    LnFrame ack = popReply();
    TEST_ASSERT_EQUAL_HEX8(LN_OPC_LONG_ACK, ack.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x6F, ack.data[1]);
    TEST_ASSERT_FALSE(server.hasReply());
}

void test_simulated_steal_needs_a_held_slot(void) {
    TEST_ASSERT_EQUAL_UINT8(0, server.simulateSteal(0x05, 0x0B, 40, now_ms));
    TEST_ASSERT_FALSE(server.hasReply());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_simulated_steal_emits_the_frames_a_thief_produces);
    RUN_TEST(test_simulated_steal_needs_a_held_slot);
    RUN_TEST(test_loco_adr_never_reports_a_slot_as_in_use);
    RUN_TEST(test_status_write_to_an_empty_slot_is_ignored);
    RUN_TEST(test_select_after_reboot_reads_as_empty_so_the_throttle_logs_on_again);
    RUN_TEST(test_external_change_marks_the_slot_for_a_pushed_slot_read);
    RUN_TEST(test_a_burst_of_changes_becomes_one_up_to_date_slot_read);
    RUN_TEST(test_slot_reads_for_one_slot_are_rate_limited);
    RUN_TEST(test_byte_frames_can_be_switched_off);

    RUN_TEST(test_speed_conversion_between_loconet_and_bridge);
    RUN_TEST(test_speed_frame_reports_a_drive_event);
    RUN_TEST(test_repeating_the_same_speed_reports_nothing);
    RUN_TEST(test_emergency_stop_speed_maps_to_bridge_speed_zero);
    RUN_TEST(test_direction_bit_is_a_drive_event_not_a_function_event);
    RUN_TEST(test_dirf_function_bits_map_to_f0_to_f4);
    RUN_TEST(test_snd_function_bits_map_to_f5_to_f8);
    RUN_TEST(test_functions_f9_to_f12_arrive_as_a_dcc_packet);
    RUN_TEST(test_functions_f13_to_f20_use_the_0xde_packet);
    RUN_TEST(test_functions_f21_to_f28_use_the_0xdf_packet);
    RUN_TEST(test_immediate_packet_with_a_long_address);
    RUN_TEST(test_unrecognised_immediate_packet_is_acknowledged_but_not_reported);
    RUN_TEST(test_slot_write_right_after_selecting_is_not_a_command);
    RUN_TEST(test_null_move_reports_a_selection);
    RUN_TEST(test_loco_adr_probe_is_not_a_selection);
    RUN_TEST(test_slot_status_write_to_in_use_reports_a_selection);
    RUN_TEST(test_dispatch_get_reports_a_selection);
    RUN_TEST(test_slot_write_on_a_held_slot_reports_what_changed);
    RUN_TEST(test_power_frames_report_events);
    RUN_TEST(test_selecting_a_moving_loco_shows_its_real_state);
    RUN_TEST(test_unknown_loco_falls_back_to_a_stopped_forward_slot);
    RUN_TEST(test_selection_refreshes_from_current_state_again);
    RUN_TEST(test_external_speed_is_pushed_as_a_speed_frame);
    RUN_TEST(test_external_change_that_matches_the_slot_sends_nothing);
    RUN_TEST(test_external_direction_change_is_pushed_as_dirf);
    RUN_TEST(test_external_functions_are_pushed_as_dirf_and_snd);
    RUN_TEST(test_external_change_for_a_loco_no_throttle_holds_does_nothing);
    RUN_TEST(test_external_emergency_stop_and_resume_queue_the_bus_frames);

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
    RUN_TEST(test_null_move_on_a_free_slot_answers_with_the_empty_slot);
    RUN_TEST(test_moving_a_free_slot_elsewhere_is_still_refused);
    RUN_TEST(test_drive_frames_for_an_empty_slot_are_not_stored);
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
