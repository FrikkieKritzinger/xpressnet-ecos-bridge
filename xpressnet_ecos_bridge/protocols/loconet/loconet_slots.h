/*
 * LocoNet Slot Server - the "master" side of slot handling
 *
 * Pure logic (no Arduino dependency, unit-tested natively). Takes received
 * LocoNet frames and produces the reply frames a LocoNet master must send,
 * following the Digitrax LocoNet Personal Use Edition 1.0 spec:
 *
 * - OPC_LOCO_ADR   -> slot read (existing slot, or a newly loaded one) or a
 *                     "no free slot" LONG_ACK
 * - OPC_RQ_SL_DATA -> slot read
 * - OPC_MOVE_SLOTS -> null move = select (IN_USE), dst 0 = dispatch put,
 *                     src 0 = dispatch get
 * - OPC_SLOT_STAT1 / LOCO_SPD / LOCO_DIRF / LOCO_SND / WR_SL_DATA update the
 *                     stored slot; WR_SL_DATA is acknowledged with LONG_ACK
 *
 * Design decision (agreed 2026-09-23): this slot model is internal to the
 * LocoNet interface and is NEVER a gate on anything else. A null move always
 * succeeds even on a slot another throttle has IN_USE (a "steal" costs
 * nothing here), and nothing in this file knows about CommandRouter. Later
 * steps read the stored slot state to forward commands and push changes back.
 */

#ifndef LOCONET_SLOTS_H
#define LOCONET_SLOTS_H

#include <cstdint>
#include "loconet_protocol.h"

// Loco slots are numbered 1..LN_SLOT_COUNT (slot 0 is the dispatch slot and
// 120-127 are system slots in the spec; we never hand those out).
#define LN_SLOT_COUNT 20

// Slot STAT1 byte (spec "SLOT STATUS1")
#define LN_STAT1_BUSY        0x20   // D5
#define LN_STAT1_ACTIVE      0x10   // D4
#define LN_STAT1_FREE        0x00   // D5,D4 = 00
#define LN_STAT1_COMMON      0x10   // 01: refreshed, in no throttle's hands
#define LN_STAT1_IDLE        0x20   // 10: not refreshed
#define LN_STAT1_IN_USE      0x30   // 11
#define LN_STAT1_BUSY_MASK   0x30
#define LN_STAT1_STEPS_MASK  0x07   // D2-D0
#define LN_STAT1_STEPS_128   0x03   // 011 = 128 speed step mode

// Slot DIRF byte (spec "SLOT DIRF byte")
#define LN_DIRF_FORWARD      0x20   // D5: 1 = forward
#define LN_DIRF_F0           0x10

// Global track status (TRK) byte
#define LN_TRK_POWER         0x01   // DCC packets on
#define LN_TRK_RUNNING       0x02   // 0 = paused / emergency stop
#define LN_TRK_LOCONET_1_1   0x04   // master implements LocoNet 1.1 (not a DT200)

// LONG_ACK codes we send
#define LN_LACK_OPC_MOVE_SLOTS  0x3A   // MOVE_SLOTS & 0x7F
#define LN_LACK_OPC_LOCO_ADR    0x3F   // fail code for LOCO_ADR is <3F><00>
#define LN_LACK_OPC_WR_SL_DATA  0x6F   // WR_SL_DATA & 0x7F
#define LN_LACK_PROGRAMMER      0x7F   // programming track: <7F><7F> = not implemented

struct LnSlot {
    uint16_t address;     // 0 = analog/unused marker, valid together with stat1
    uint8_t stat1;
    uint8_t speed;        // 0 stop, 1 emergency stop, 2..127
    uint8_t dirf;
    uint8_t snd;
    uint8_t ss2;
    uint8_t id1;
    uint8_t id2;
    uint32_t last_use_ms;
};

class LnSlotServer {
public:
    LnSlotServer();

    // Processes one received frame, queueing any replies.
    void handleFrame(const LnFrame& frame, uint32_t now_ms);

    // Reply queue (oldest first).
    bool hasReply() const { return reply_count != 0; }
    bool peekReply(LnFrame& out) const;
    void dropReply();

    // Slot access. Valid slot numbers are 1..LN_SLOT_COUNT.
    bool isValidSlot(uint8_t slot) const { return slot >= 1 && slot <= LN_SLOT_COUNT; }
    const LnSlot& slot(uint8_t n) const { return slots[n]; }
    uint8_t dispatchSlot() const { return dispatch_slot; }
    uint8_t trackStatus() const { return trk; }
    uint32_t repliesDropped() const { return dropped_replies; }

    // Builds the 14-byte OPC_SL_RD_DATA for a slot (also used by tests).
    void buildSlotRead(uint8_t slot_number, LnFrame& out) const;

private:
    static const uint8_t MAX_REPLIES = 6;

    LnSlot slots[LN_SLOT_COUNT + 1];   // index 0 unused
    uint8_t dispatch_slot;
    uint8_t trk;
    LnFrame replies[MAX_REPLIES];
    uint8_t reply_head;
    uint8_t reply_count;
    uint32_t dropped_replies;

    void queueReply(const LnFrame& f);
    void queueSlotRead(uint8_t slot_number);
    void queueLongAck(uint8_t opcode_without_msb, uint8_t code);

    uint8_t findSlotByAddress(uint16_t address) const;
    uint8_t allocateSlot(uint16_t address, uint32_t now_ms);

    void handleLocoAdr(const LnFrame& f, uint32_t now_ms);
    void handleMoveSlots(const LnFrame& f, uint32_t now_ms);
    void handleWriteSlotData(const LnFrame& f, uint32_t now_ms);
};

#endif  // LOCONET_SLOTS_H
