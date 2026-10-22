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

// What the rest of the bridge currently believes about a loco, in the bridge's
// internal units (speed 0-126, direction 1 = forward, F0-F31 bitmap). Used to
// load a slot with the loco's REAL state when a throttle selects it, so
// selecting a moving loco does not stop it.
struct LnLocoSnapshot {
    uint8_t speed;
    uint8_t direction;
    uint32_t functions;
};

// Looks up (and, if the loco is new to the bridge, starts tracking) a loco.
// Returns false if nothing is known. Supplied by LocoNetInterface.
typedef bool (*LnLocoLookup)(uint16_t address, LnLocoSnapshot* out, void* context);

// What a received frame changed, for the interface to route onward.
#define LN_EV_NONE        0x00
#define LN_EV_DRIVE       0x01   // speed and/or direction changed: speed, direction valid
#define LN_EV_FUNCTIONS   0x02   // functions changed: func_mask/func_bits valid
#define LN_EV_POWER_ON    0x04
#define LN_EV_POWER_OFF   0x08
#define LN_EV_ESTOP       0x10
#define LN_EV_SELECT      0x20   // a throttle took this slot (IN_USE): address valid

struct LnSlotEvent {
    uint8_t flags;
    uint16_t address;      // DCC address the change applies to (0 = none)
    uint8_t speed;         // bridge units 0-126
    uint8_t direction;     // 1 = forward
    uint32_t func_mask;    // which function bits this frame defines
    uint32_t func_bits;    // their new values (only bits inside func_mask)
};

// Slot speed byte <-> bridge speed. LocoNet: 0 = stop, 1 = emergency stop,
// 2..127 = speed steps. Bridge: 0 = stop, 1..126 = speed steps.
uint8_t lnSpeedToBridge(uint8_t ln_speed);
uint8_t lnSpeedFromBridge(uint8_t bridge_speed);

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

    // Processes one received frame, queueing any replies, and reports what it
    // changed so the interface can route it to the other protocols.
    LnSlotEvent handleFrame(const LnFrame& frame, uint32_t now_ms);

    // Optional: where to get a loco's real current state.
    void setLocoLookup(LnLocoLookup fn, void* context) { lookup = fn; lookup_context = context; }

    // A change that came from another protocol: update every slot holding
    // this address and queue the LocoNet frames that make a throttle's display
    // follow (OPC_LOCO_SPD / _DIRF / _SND). Only values that actually differ
    // are queued. Returns how many slots were affected.
    uint8_t applyExternalDrive(uint16_t address, uint8_t bridge_speed, uint8_t direction, uint32_t now_ms);
    uint8_t applyExternalFunctions(uint16_t address, uint32_t functions, uint32_t now_ms);

    // Track state pushed from another protocol: queues OPC_IDLE (emergency
    // stop) / OPC_GPON and updates the TRK byte.
    void applyExternalEmergencyStop();
    void applyExternalResume();

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
    static const uint8_t MAX_REPLIES = 8;

    LnSlot slots[LN_SLOT_COUNT + 1];   // index 0 unused
    uint8_t dispatch_slot;
    uint8_t trk;
    LnLocoLookup lookup;
    void* lookup_context;
    uint16_t pending_select_address;   // set while handling a frame that takes a slot
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
    LnSlotEvent handleImmediatePacket(const LnFrame& f);
    void refreshFromLookup(uint8_t slot_number);
    void queueSlotByte(uint8_t opcode, uint8_t slot_number, uint8_t value);
    void reportChange(uint8_t slot_number, uint8_t old_speed, uint8_t old_dirf,
                      uint8_t old_snd, LnSlotEvent& ev) const;
};

#endif  // LOCONET_SLOTS_H
