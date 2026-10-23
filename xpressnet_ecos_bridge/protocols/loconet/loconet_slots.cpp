#include "loconet_slots.h"

#include <cstring>

LnSlotServer::LnSlotServer() {
    memset(slots, 0, sizeof(slots));
    memset(replies, 0, sizeof(replies));
    dispatch_slot = 0;
    // Power OFF until the real state is known: Ecos starts in STOP and the
    // bridge learns its actual state when it connects (or an operator presses
    // GO). Telling a throttle "on" first would be wrong more often than not.
    trk = LN_TRK_RUNNING | LN_TRK_LOCONET_1_1;
    reply_head = 0;
    reply_count = 0;
    dropped_replies = 0;
    lookup = nullptr;
    lookup_context = nullptr;
    pending_select_address = 0;
    dirty_slots = 0;
    resync_needed = false;
    push_byte_frames = true;
    memset(last_push_ms, 0, sizeof(last_push_ms));
}

// ============================================================================
// UNIT CONVERSIONS
// ============================================================================

uint8_t lnSpeedToBridge(uint8_t ln_speed) {
    ln_speed &= 0x7F;
    return ln_speed < 2 ? 0 : (uint8_t)(ln_speed - 1);
}

uint8_t lnSpeedFromBridge(uint8_t bridge_speed) {
    if (bridge_speed == 0) {
        return 0;
    }
    return bridge_speed >= 126 ? 127 : (uint8_t)(bridge_speed + 1);
}

// DIRF carries F0 in bit 4 and F1-F4 in bits 0-3; SND carries F5-F8 in bits
// 0-3 (spec "SLOT DIRF byte" / "SLOT SOUND"). Bridge function bitmap: bit n = Fn.
static uint8_t functionsToDirfBits(uint32_t f) {
    return (uint8_t)(((f >> 1) & 0x0F) | ((f & 0x01) ? 0x10 : 0));
}

static uint8_t functionsToSnd(uint32_t f) {
    return (uint8_t)((f >> 5) & 0x0F);
}

static uint32_t dirfSndToFunctions(uint8_t dirf, uint8_t snd) {
    uint32_t f = 0;
    f |= (dirf & 0x10) ? 0x01u : 0u;
    f |= (uint32_t)(dirf & 0x0F) << 1;
    f |= (uint32_t)(snd & 0x0F) << 5;
    return f;
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
        // The bus has been unavailable for a long time; the oldest frame is
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

void LnSlotServer::queueSlotByte(uint8_t opcode, uint8_t slot_number, uint8_t value) {
    LnFrame f;
    memset(&f, 0, sizeof(f));
    f.data[0] = opcode;
    f.data[1] = slot_number;
    f.data[2] = value & 0x7F;
    f.len = 4;
    f.data[3] = lnChecksum(f.data, 3);
    queueReply(f);
}

// ============================================================================
// STATE FROM THE REST OF THE BRIDGE
// ============================================================================

void LnSlotServer::refreshFromLookup(uint8_t slot_number) {
    LnSlot& s = slots[slot_number];
    if (lookup == nullptr || s.address == 0) {
        return;
    }
    LnLocoSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.direction = 1;
    if (!lookup(s.address, &snap, lookup_context)) {
        return;
    }
    s.speed = lnSpeedFromBridge(snap.speed);
    s.dirf = (uint8_t)((snap.direction ? LN_DIRF_FORWARD : 0) | functionsToDirfBits(snap.functions));
    s.snd = functionsToSnd(snap.functions);
}

uint8_t LnSlotServer::applyExternalDrive(uint16_t address, uint8_t bridge_speed, uint8_t direction, uint32_t now_ms) {
    uint8_t affected = 0;
    uint8_t ln_speed = lnSpeedFromBridge(bridge_speed);
    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        LnSlot& s = slots[i];
        if ((s.stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_FREE || s.address != address) {
            continue;
        }
        affected++;
        s.last_use_ms = now_ms;
        uint8_t dirf = (uint8_t)((s.dirf & ~LN_DIRF_FORWARD) | (direction ? LN_DIRF_FORWARD : 0));
        if (dirf != s.dirf) {
            s.dirf = dirf;
            if (push_byte_frames) queueSlotByte(LN_OPC_LOCO_DIRF, i, dirf);
            markSlotChanged(i);
        }
        // A stored emergency-stop (1) already means stopped.
        bool same = (ln_speed == s.speed) || (ln_speed == 0 && s.speed == 1);
        if (!same) {
            s.speed = ln_speed;
            if (push_byte_frames) queueSlotByte(LN_OPC_LOCO_SPD, i, ln_speed);
            markSlotChanged(i);
        }
    }
    return affected;
}

uint8_t LnSlotServer::applyExternalFunctions(uint16_t address, uint32_t functions, uint32_t now_ms) {
    uint8_t affected = 0;
    uint8_t dirf_bits = functionsToDirfBits(functions);
    uint8_t snd = functionsToSnd(functions);
    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        LnSlot& s = slots[i];
        if ((s.stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_FREE || s.address != address) {
            continue;
        }
        affected++;
        s.last_use_ms = now_ms;
        uint8_t dirf = (uint8_t)((s.dirf & LN_DIRF_FORWARD) | dirf_bits);
        if (dirf != s.dirf) {
            s.dirf = dirf;
            if (push_byte_frames) queueSlotByte(LN_OPC_LOCO_DIRF, i, dirf);
            markSlotChanged(i);
        }
        if (snd != s.snd) {
            s.snd = snd;
            if (push_byte_frames) queueSlotByte(LN_OPC_LOCO_SND, i, snd);
            markSlotChanged(i);
        }
    }
    return affected;
}

void LnSlotServer::markSlotChanged(uint8_t slot_number) {
    dirty_slots |= (1UL << slot_number);
    // Only a slot a throttle actually holds shows a stale display.
    if ((slots[slot_number].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE) {
        resync_needed = true;
    }
}

void LnSlotServer::pushDirtySlots(uint32_t now_ms) {
    for (uint8_t i = 1; i <= LN_SLOT_COUNT; i++) {
        if (!(dirty_slots & (1UL << i))) {
            continue;
        }
        if ((uint32_t)(now_ms - last_push_ms[i]) < LN_SLOT_PUSH_INTERVAL_MS) {
            continue;   // stays dirty; the next call after the interval sends the latest values
        }
        queueSlotRead(i);
        dirty_slots &= ~(1UL << i);
        last_push_ms[i] = now_ms;
    }
}

void LnSlotServer::applyExternalEmergencyStop() {
    // The other protocols' stop means track power off (Ecos STOP cuts track
    // power), which on LocoNet is OPC_GPOFF - what a Digitrax throttle's own
    // power button sends - not OPC_IDLE (a broadcast emergency stop that
    // leaves power on).
    trk &= (uint8_t)~LN_TRK_POWER;
    LnFrame f;
    memset(&f, 0, sizeof(f));
    f.data[0] = LN_OPC_GPOFF;
    f.len = 2;
    f.data[1] = lnChecksum(f.data, 1);
    queueReply(f);
}

void LnSlotServer::applyExternalResume() {
    trk |= LN_TRK_POWER | LN_TRK_RUNNING;
    LnFrame f;
    memset(&f, 0, sizeof(f));
    f.data[0] = LN_OPC_GPON;
    f.len = 2;
    f.data[1] = lnChecksum(f.data, 1);
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

LnSlotEvent LnSlotServer::handleFrame(const LnFrame& f, uint32_t now_ms) {
    LnSlotEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.direction = 1;
    if (f.len < 2) {
        return ev;
    }
    const uint8_t* b = f.data;
    pending_select_address = 0;

    switch (b[0]) {
        case LN_OPC_LOCO_ADR:
            if (f.len == 4) handleLocoAdr(f, now_ms);
            break;

        case LN_OPC_RQ_SL_DATA:
            if (f.len == 4 && isValidSlot(b[1])) {
                refreshFromLookup(b[1]);
                queueSlotRead(b[1]);
            }
            break;

        case LN_OPC_MOVE_SLOTS:
            if (f.len == 4) handleMoveSlots(f, now_ms);
            break;

        case LN_OPC_SLOT_STAT1:
            // Ignored for a slot nothing is loaded in. After a bridge reboot a
            // throttle still remembers its old slot and "releases" it (B5 .. 13)
            // before re-selecting; accepting that on an empty slot made the
            // following null move succeed, leaving the throttle driving a
            // phantom loco at address 0 whose commands went nowhere (live
            // 2026-10-23). Refusing it makes the throttle log on properly.
            if (f.len == 4 && isValidSlot(b[1]) &&
                (slots[b[1]].stat1 & LN_STAT1_BUSY_MASK) != LN_STAT1_FREE) {
                bool was_in_use = (slots[b[1]].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE;
                slots[b[1]].stat1 = b[2];
                slots[b[1]].last_use_ms = now_ms;
                if (!was_in_use && (b[2] & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE) {
                    pending_select_address = slots[b[1]].address;
                }
            }
            break;

        case LN_OPC_LOCO_SPD:
        case LN_OPC_LOCO_DIRF:
        case LN_OPC_LOCO_SND:
            // Nothing is stored for a slot with no loco in it, so an empty slot
            // keeps reading as truly empty even while a throttle that has not
            // noticed yet (e.g. right after a bridge restart) keeps sending.
            if (f.len == 4 && isValidSlot(b[1]) &&
                (slots[b[1]].stat1 & LN_STAT1_BUSY_MASK) != LN_STAT1_FREE) {
                LnSlot& s = slots[b[1]];
                uint8_t old_speed = s.speed;
                uint8_t old_dirf = s.dirf;
                uint8_t old_snd = s.snd;
                if (b[0] == LN_OPC_LOCO_SPD) s.speed = b[2] & 0x7F;
                if (b[0] == LN_OPC_LOCO_DIRF) s.dirf = b[2] & 0x7F;
                if (b[0] == LN_OPC_LOCO_SND) s.snd = b[2] & 0x7F;
                s.last_use_ms = now_ms;
                reportChange(b[1], old_speed, old_dirf, old_snd, ev);
            }
            break;

        case LN_OPC_WR_SL_DATA:
            if (f.len == 14 && isValidSlot(b[2])) {
                // Report what this write changed, but ONLY once the throttle
                // already holds the slot (IN_USE): the write a throttle sends
                // right after selecting repeats the values we just gave it,
                // and a selection must never turn into a command.
                LnSlot& s = slots[b[2]];
                bool was_in_use = (s.stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE;
                uint8_t old_speed = s.speed;
                uint8_t old_dirf = s.dirf;
                uint8_t old_snd = s.snd;
                uint16_t old_address = s.address;
                handleWriteSlotData(f, now_ms);
                if (was_in_use && s.address == old_address) {
                    reportChange(b[2], old_speed, old_dirf, old_snd, ev);
                } else if (!was_in_use && (s.stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE) {
                    pending_select_address = s.address;
                }
            } else if (f.len == 14) {
                handleWriteSlotData(f, now_ms);
            }
            break;

        case LN_OPC_IMM_PACKET:
            if (f.len == 11) {
                ev = handleImmediatePacket(f);
            }
            break;

        case LN_OPC_GPON:
            trk |= LN_TRK_POWER | LN_TRK_RUNNING;
            ev.flags |= LN_EV_POWER_ON;
            break;

        case LN_OPC_GPOFF:
            trk &= (uint8_t)~LN_TRK_POWER;
            ev.flags |= LN_EV_POWER_OFF;
            break;

        case LN_OPC_IDLE:
            trk &= (uint8_t)~LN_TRK_RUNNING;
            ev.flags |= LN_EV_ESTOP;
            break;

        default:
            break;
    }

    if (pending_select_address != 0 && ev.flags == LN_EV_NONE) {
        ev.flags = LN_EV_SELECT;
        ev.address = pending_select_address;
    }
    return ev;
}

// Describes how a slot differs from its previous values. Only real
// differences are reported, so a throttle repeating the same value (they
// re-send periodically) generates no traffic elsewhere.
void LnSlotServer::reportChange(uint8_t slot_number, uint8_t old_speed, uint8_t old_dirf,
                                uint8_t old_snd, LnSlotEvent& ev) const {
    const LnSlot& s = slots[slot_number];
    if (s.address == 0) {
        return;
    }
    ev.address = s.address;
    ev.speed = lnSpeedToBridge(s.speed);
    ev.direction = (s.dirf & LN_DIRF_FORWARD) ? 1 : 0;

    if (s.speed != old_speed || ((s.dirf ^ old_dirf) & LN_DIRF_FORWARD)) {
        ev.flags |= LN_EV_DRIVE;
    }

    uint32_t before = dirfSndToFunctions(old_dirf, old_snd);
    uint32_t after = dirfSndToFunctions(s.dirf, s.snd);
    if (before != after) {
        ev.flags |= LN_EV_FUNCTIONS;
        ev.func_mask = 0x1FF;  // a slot defines F0-F8 together
        ev.func_bits = after & 0x1FF;
    }
}

LnSlotEvent LnSlotServer::handleImmediatePacket(const LnFrame& f) {
    // <ED><0B><7F><REPS><DHI><IM1>..<IM5><CHK>: a DCC packet of up to 5 bytes
    // whose bit 7s are collected in DHI (bit n-1 = IMn.7). Throttles use this
    // for functions F9 and up, addressed by DCC address rather than slot.
    LnSlotEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.direction = 1;

    // Spec: acknowledge with LONG_ACK <7D><7F>, whatever the packet is.
    queueLongAck(0x7D, 0x7F);

    uint8_t count = (uint8_t)((f.data[3] >> 4) & 0x07);
    uint8_t dhi = f.data[4];
    if (count < 2 || count > 5) {
        return ev;
    }
    uint8_t im[5];
    for (uint8_t i = 0; i < 5; i++) {
        im[i] = (uint8_t)(f.data[5 + i] | (((dhi >> i) & 1) ? 0x80 : 0));
    }

    uint16_t address;
    uint8_t idx;
    if ((im[0] & 0xC0) == 0xC0) {
        address = (uint16_t)(((im[0] & 0x3F) << 8) | im[1]);
        idx = 2;
    } else {
        address = im[0];
        idx = 1;
    }
    if (address == 0 || idx >= count) {
        return ev;
    }

    uint8_t instr = im[idx];
    uint32_t mask = 0;
    uint32_t bits = 0;
    if ((instr & 0xF0) == 0xA0) {                   // 1010DDDD: F9-F12
        mask = 0x1E00;
        bits = (uint32_t)(instr & 0x0F) << 9;
    } else if ((instr & 0xF0) == 0xB0) {            // 1011DDDD: F5-F8
        mask = 0x1E0;
        bits = (uint32_t)(instr & 0x0F) << 5;
    } else if (instr == 0xDE && idx + 1 < count) {  // F13-F20
        mask = 0x1FE000;
        bits = (uint32_t)im[idx + 1] << 13;
    } else if (instr == 0xDF && idx + 1 < count) {  // F21-F28
        mask = 0x1FE00000;
        bits = (uint32_t)im[idx + 1] << 21;
    } else {
        return ev;
    }

    ev.flags = LN_EV_FUNCTIONS;
    ev.address = address;
    ev.func_mask = mask;
    ev.func_bits = bits;
    return ev;
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

    // Stealing is free here, so never make a throttle ask for it: a slot still
    // marked IN_USE (left over from an earlier selection, or re-created by the
    // throttle itself after a bridge reboot) is offered as COMMON, i.e. free
    // to take. A throttle that sees IN_USE in this reply stops and asks the
    // user "steal this loco?" - the very prompt this design avoids (live
    // 2026-10-23). The throttle's follow-up null move still marks it IN_USE.
    if ((slots[slot_number].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_IN_USE) {
        slots[slot_number].stat1 = (uint8_t)((slots[slot_number].stat1 & ~LN_STAT1_BUSY_MASK) | LN_STAT1_COMMON);
    }

    refreshFromLookup(slot_number);
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
            pending_select_address = slots[s].address;
            refreshFromLookup(s);
            queueSlotRead(s);
        } else {
            queueLongAck(LN_LACK_OPC_MOVE_SLOTS, 0x00);
        }
        return;
    }

    if (!isValidSlot(src)) {
        queueLongAck(LN_LACK_OPC_MOVE_SLOTS, 0x00);
        return;
    }

    if ((slots[src].stat1 & LN_STAT1_BUSY_MASK) == LN_STAT1_FREE) {
        // A throttle selecting a slot this bridge no longer has - typically
        // right after the bridge restarted under it. Answering with the
        // spec's "illegal move" LONG_ACK makes a Digitrax throttle ask the
        // user "steal this loco?" (live 2026-10-23); answering with the
        // slot's own read, which shows it empty, is what it gets when it
        // asks about the slot at power-up, and sends it back to selecting.
        if (src == dst) {
            queueSlotRead(src);
        } else {
            queueLongAck(LN_LACK_OPC_MOVE_SLOTS, 0x00);
        }
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
        pending_select_address = slots[src].address;
        refreshFromLookup(src);
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
