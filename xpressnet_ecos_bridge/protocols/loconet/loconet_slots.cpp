#include "loconet_slots.h"

#include <cstring>

LnSlotServer::LnSlotServer() {
    memset(slots, 0, sizeof(slots));
    memset(replies, 0, sizeof(replies));
    dispatch_slot = 0;
    trk = LN_TRK_POWER | LN_TRK_RUNNING | LN_TRK_LOCONET_1_1;
    reply_head = 0;
    reply_count = 0;
    dropped_replies = 0;
}

// ============================================================================
// REPLY QUEUE
// ============================================================================

bool LnSlotServer::peekReply(LnFrame& out) const {
    if (reply_count == 0) {
        return false;
    }
    out = replies[reply_head];
    return true;
}

void LnSlotServer::dropReply() {
    if (reply_count == 0) {
        return;
    }
    reply_head = (uint8_t)((reply_head + 1) % MAX_REPLIES);
    reply_count--;
}

void LnSlotServer::queueReply(const LnFrame& f) {
    if (reply_count == MAX_REPLIES) {
        // The bus has been unavailable for a long time; the oldest reply is
        // the least likely to still be wanted (the throttle will have asked
        // again), so make room by dropping it.
        dropReply();
        dropped_replies++;
    }
    uint8_t tail = (uint8_t)((reply_head + reply_count) % MAX_REPLIES);
    replies[tail] = f;
    reply_count++;
}

void LnSlotServer::buildSlotRead(uint8_t slot_number, LnFrame& out) const {
    const LnSlot& s = slots[slot_number];
    memset(&out, 0, sizeof(out));
    out.data[0] = LN_OPC_SL_RD_DATA;
    out.data[1] = 14;
    out.data[2] = slot_number;
    out.data[3] = s.stat1;
    out.data[4] = (uint8_t)(s.address & 0x7F);
    out.data[5] = s.speed;
    out.data[6] = s.dirf;
    out.data[7] = trk;
    out.data[8] = s.ss2;
    out.data[9] = (uint8_t)((s.address >> 7) & 0x7F);
    out.data[10] = s.snd;
    out.data[11] = s.id1;
    out.data[12] = s.id2;
    out.len = 14;
    out.data[13] = lnChecksum(out.data, 13);
}

void LnSlotServer::queueSlotRead(uint8_t slot_number) {
    LnFrame f;
    buildSlotRead(slot_number, f);
    queueReply(f);
}

void LnSlotServer::queueLongAck(uint8_t opcode_without_msb, uint8_t code) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    f.data[0] = LN_OPC_LONG_ACK;
    f.data[1] = opcode_without_msb;
    f.data[2] = code;
    f.len = 4;
    f.data[3] = lnChecksum(f.data, 3);
    queueReply(f);
}

// ============================================================================
// SLOT TABLE
// ============================================================================

uint8_t LnSlotServer::findSlotByAddress(uint16_t address) const {
    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        if ((slots[i].stat1 & LN_STAT1_BUSY_MASK) != LN_STAT1_FREE && slots[i].address == address) {
            return i;
        }
    }
    return 0;
}

uint8_t LnSlotServer::allocateSlot(uint16_t address, uint32_t now_ms) {
    uint8_t chosen = 0;

    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        if ((slots[i].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_FREE) {
            chosen = i;
            break;
        }
    }

    // No free slot: reclaim the least recently used one nobody has IN_USE.
    // Throttles probe an address on every key press, so unclaimed (COMMON)
    // slots pile up quickly; a real master purges them the same way.
    if (chosen == 0) {
        uint32_t oldest = 0;
        for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
            if ((slots[i].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE || i == dispatch_slot) {
                continue;
            }
            uint32_t age = now_ms - slots[i].last_use_ms;
            if (chosen == 0 || age > oldest) {
                chosen = i;
                oldest = age;
            }
        }
    }

    if (chosen == 0) {
        return 0;
    }

    // Spec: a newly loaded address starts at speed 0, forward, functions off,
    // 128 step mode, and is not yet in use by anyone.
    memset(&slots[chosen], 0, sizeof(LnSlot));
    slots[chosen].address = address;
    slots[chosen].stat1 = LN_STAT1_COMMON | LN_STAT1_STEPS_128;
    slots[chosen].dirf = LN_DIRF_FORWARD;
    slots[chosen].last_use_ms = now_ms;
    return chosen;
}

// ============================================================================
// FRAME HANDLING
// ============================================================================

void LnSlotServer::handleFrame(const LnFrame& f, uint32_t now_ms) {
    if (f.len < 2) {
        return;
    }
    const uint8_t* b = f.data;

    switch (b[0]) {
        case LN_OPC_LOCO_ADR:
            if (f.len == 4) handleLocoAdr(f, now_ms);
            break;

        case LN_OPC_RQ_SL_DATA:
            if (f.len == 4 && isValidSlot(b[1])) {
                queueSlotRead(b[1]);
            }
            break;

        case LN_OPC_MOVE_SLOTS:
            if (f.len == 4) handleMoveSlots(f, now_ms);
            break;

        case LN_OPC_SLOT_STAT1:
            if (f.len == 4 && isValidSlot(b[1])) {
                slots[b[1]].stat1 = b[2];
                slots[b[1]].last_use_ms = now_ms;
            }
            break;

        case LN_OPC_LOCO_SPD:
            if (f.len == 4 && isValidSlot(b[1])) {
                slots[b[1]].speed = b[2] & 0x7F;
                slots[b[1]].last_use_ms = now_ms;
            }
            break;

        case LN_OPC_LOCO_DIRF:
            if (f.len == 4 && isValidSlot(b[1])) {
                slots[b[1]].dirf = b[2] & 0x7F;
                slots[b[1]].last_use_ms = now_ms;
            }
            break;

        case LN_OPC_LOCO_SND:
            if (f.len == 4 && isValidSlot(b[1])) {
                slots[b[1]].snd = b[2] & 0x7F;
                slots[b[1]].last_use_ms = now_ms;
            }
            break;

        case LN_OPC_WR_SL_DATA:
            if (f.len == 14) handleWriteSlotData(f, now_ms);
            break;

        case LN_OPC_GPON:
            trk |= LN_TRK_POWER | LN_TRK_RUNNING;
            break;

        case LN_OPC_GPOFF:
            trk &= (uint8_t)~LN_TRK_POWER;
            break;

        case LN_OPC_IDLE:
            trk &= (uint8_t)~LN_TRK_RUNNING;
            break;

        default:
            break;
    }
}

void LnSlotServer::handleLocoAdr(const LnFrame& f, uint32_t now_ms) {
    uint16_t address = (uint16_t)(((f.data[1] & 0x7F) << 7) | (f.data[2] & 0x7F));

    uint8_t slot_number = findSlotByAddress(address);
    if (slot_number == 0) {
        slot_number = allocateSlot(address, now_ms);
    }
    if (slot_number == 0) {
        // Spec: no free slot -> LONG_ACK <B4><3F><00>
        queueLongAck(LN_LACK_OPC_LOCO_ADR, 0x00);
        return;
    }
    slots[slot_number].last_use_ms = now_ms;
    queueSlotRead(slot_number);
}

void LnSlotServer::handleMoveSlots(const LnFrame& f, uint32_t now_ms) {
    uint8_t src = f.data[1];
    uint8_t dst = f.data[2];

    if (src == 0) {
        // Dispatch GET
        if (dispatch_slot != 0) {
            uint8_t s = dispatch_slot;
            slots[s].stat1 = (uint8_t)((slots[s].stat1 & ~LN_STAT1_BUSY_MASK) | LN_STAT1_IN_USE);
            slots[s].last_use_ms = now_ms;
            dispatch_slot = 0;
            queueSlotRead(s);
        } else {
            queueLongAck(LN_LACK_OPC_MOVE_SLOTS, 0x00);
        }
        return;
    }

    if (!isValidSlot(src) || (slots[src].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_FREE) {
        queueLongAck(LN_LACK_OPC_MOVE_SLOTS, 0x00);
        return;
    }

    if (src == dst) {
        // Null move = select. Always allowed, even on a slot another throttle
        // holds IN_USE: stealing is free in this bridge by design.
        slots[src].stat1 = (uint8_t)((slots[src].stat1 & ~LN_STAT1_BUSY_MASK) | LN_STAT1_IN_USE);
        slots[src].last_use_ms = now_ms;
        if (dispatch_slot == src) {
            dispatch_slot = 0;
        }
        queueSlotRead(src);
        return;
    }

    if (dst == 0) {
        // Dispatch PUT: a one-deep stack, the slot is released for whoever
        // does a dispatch get.
        dispatch_slot = src;
        slots[src].stat1 = (uint8_t)((slots[src].stat1 & ~LN_STAT1_BUSY_MASK) | LN_STAT1_COMMON);
        slots[src].last_use_ms = now_ms;
        queueSlotRead(src);
        return;
    }

    // Moving one slot onto another is not supported.
    queueLongAck(LN_LACK_OPC_MOVE_SLOTS, 0x00);
}

void LnSlotServer::handleWriteSlotData(const LnFrame& f, uint32_t now_ms) {
    const uint8_t* b = f.data;
    uint8_t slot_number = b[2];

    if (slot_number == 0x7C) {
        // Programming track: CV programming is out of scope for the bridge.
        queueLongAck(0x7F, LN_LACK_PROGRAMMER);
        return;
    }
    if (!isValidSlot(slot_number)) {
        return;
    }

    LnSlot& s = slots[slot_number];
    s.stat1 = b[3];
    s.address = (uint16_t)(((b[9] & 0x7F) << 7) | (b[4] & 0x7F));
    s.speed = b[5] & 0x7F;
    s.dirf = b[6] & 0x7F;
    s.ss2 = b[8] & 0x7F;
    s.snd = b[10] & 0x7F;
    s.id1 = b[11] & 0x7F;
    s.id2 = b[12] & 0x7F;
    s.last_use_ms = now_ms;
    queueLongAck(LN_LACK_OPC_WR_SL_DATA, 0x7F);
}
