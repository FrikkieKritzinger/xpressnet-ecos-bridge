#include "loconet_protocol.h"

#include <cstdio>
#include <cstring>

// ============================================================================
// HELPERS
// ============================================================================

uint8_t lnOpcodeFixedLength(uint8_t opcode) {
    if ((opcode & 0x80) == 0) {
        return 0;
    }
    switch ((opcode >> 5) & 0x03) {
        case 0: return 2;
        case 1: return 4;
        case 2: return 6;
        default: return 0;  // variable: byte count follows the opcode
    }
}

uint8_t lnChecksum(const uint8_t* data, uint8_t len_without_checksum) {
    uint8_t x = 0xFF;
    for (uint8_t i = 0; i < len_without_checksum; i++) {
        x ^= data[i];
    }
    return x;
}

bool lnChecksumValid(const uint8_t* data, uint8_t len) {
    if (len < 2) {
        return false;
    }
    uint8_t x = 0;
    for (uint8_t i = 0; i < len; i++) {
        x ^= data[i];
    }
    return x == 0xFF;
}

const char* lnOpcodeName(uint8_t opcode) {
    switch (opcode) {
        case LN_OPC_BUSY:         return "OPC_BUSY";
        case LN_OPC_GPOFF:        return "OPC_GPOFF";
        case LN_OPC_GPON:         return "OPC_GPON";
        case LN_OPC_IDLE:         return "OPC_IDLE";
        case LN_OPC_LOCO_SPD:     return "OPC_LOCO_SPD";
        case LN_OPC_LOCO_DIRF:    return "OPC_LOCO_DIRF";
        case LN_OPC_LOCO_SND:     return "OPC_LOCO_SND";
        case LN_OPC_SW_REQ:       return "OPC_SW_REQ";
        case LN_OPC_SW_REP:       return "OPC_SW_REP";
        case LN_OPC_INPUT_REP:    return "OPC_INPUT_REP";
        case LN_OPC_LONG_ACK:     return "OPC_LONG_ACK";
        case LN_OPC_SLOT_STAT1:   return "OPC_SLOT_STAT1";
        case LN_OPC_CONSIST_FUNC: return "OPC_CONSIST_FUNC";
        case LN_OPC_UNLINK_SLOTS: return "OPC_UNLINK_SLOTS";
        case LN_OPC_LINK_SLOTS:   return "OPC_LINK_SLOTS";
        case LN_OPC_MOVE_SLOTS:   return "OPC_MOVE_SLOTS";
        case LN_OPC_RQ_SL_DATA:   return "OPC_RQ_SL_DATA";
        case LN_OPC_SW_STATE:     return "OPC_SW_STATE";
        case LN_OPC_SW_ACK:       return "OPC_SW_ACK";
        case LN_OPC_LOCO_ADR:     return "OPC_LOCO_ADR";
        case LN_OPC_PEER_XFER:    return "OPC_PEER_XFER";
        case LN_OPC_SL_RD_DATA:   return "OPC_SL_RD_DATA";
        case LN_OPC_IMM_PACKET:   return "OPC_IMM_PACKET";
        case LN_OPC_WR_SL_DATA:   return "OPC_WR_SL_DATA";
        default:                  return "OPC_UNKNOWN";
    }
}

void lnFormatFrame(const LnFrame& frame, char* out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    size_t pos = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < frame.len && pos + 4 < out_size; i++) {
        pos += snprintf(out + pos, out_size - pos, "%02X ", frame.data[i]);
    }
    if (frame.len > 0 && pos + 1 < out_size) {
        snprintf(out + pos, out_size - pos, " %s", lnOpcodeName(frame.data[0]));
    }
}

static void appendFunctions(char* out, size_t out_size, size_t& pos, uint8_t bits, uint8_t first_fn, const uint8_t* order) {
    // order[i] = the bit holding function first_fn + i
    bool any = false;
    for (uint8_t i = 0; i < 4 || (first_fn == 0 && i < 5); i++) {
        if (bits & order[i]) {
            pos += snprintf(out + pos, out_size - pos, any ? ",%u" : " F%u", (unsigned)(first_fn + i));
            any = true;
        }
    }
    if (!any) {
        pos += snprintf(out + pos, out_size - pos, " none");
    }
}

void lnDescribeFrame(const LnFrame& frame, char* out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (frame.len < 2) {
        return;
    }
    const uint8_t* d = frame.data;
    size_t pos = 0;
    switch (d[0]) {
        case LN_OPC_GPON:  snprintf(out, out_size, "Track power ON"); break;
        case LN_OPC_GPOFF: snprintf(out, out_size, "Track power OFF"); break;
        case LN_OPC_IDLE:  snprintf(out, out_size, "Emergency stop"); break;
        case LN_OPC_BUSY:  snprintf(out, out_size, "Bus busy"); break;
        case LN_OPC_LOCO_ADR: {
            unsigned address = (d[1] << 7) | d[2];
            snprintf(out, out_size, "Find loco %u", address);
            break;
        }
        case LN_OPC_MOVE_SLOTS:
            if (d[1] == d[2]) {
                snprintf(out, out_size, "S%u select", d[1]);
            } else if (d[2] == 0) {
                snprintf(out, out_size, "S%u dispatch", d[1]);
            } else if (d[1] == 0) {
                snprintf(out, out_size, "Take dispatched S%u", d[2]);
            } else {
                snprintf(out, out_size, "S%u move to S%u", d[1], d[2]);
            }
            break;
        case LN_OPC_RQ_SL_DATA: snprintf(out, out_size, "S%u read request", d[1]); break;
        case LN_OPC_SLOT_STAT1: snprintf(out, out_size, "S%u status change", d[1]); break;
        case LN_OPC_LOCO_SPD:
            if (d[2] == 0) {
                snprintf(out, out_size, "S%u speed 0", d[1]);
            } else if (d[2] == 1) {
                snprintf(out, out_size, "S%u E-STOP", d[1]);
            } else {
                snprintf(out, out_size, "S%u speed %u", d[1], d[2] - 1);
            }
            break;
        case LN_OPC_LOCO_DIRF: {
            static const uint8_t order[5] = {0x10, 0x01, 0x02, 0x04, 0x08};
            pos += snprintf(out, out_size, "S%u %s", d[1], (d[2] & 0x20) ? "Fwd" : "Rev");
            appendFunctions(out, out_size, pos, d[2], 0, order);
            break;
        }
        case LN_OPC_LOCO_SND: {
            static const uint8_t order[4] = {0x01, 0x02, 0x04, 0x08};
            pos += snprintf(out, out_size, "S%u", d[1]);
            appendFunctions(out, out_size, pos, d[2], 5, order);
            break;
        }
        case LN_OPC_SL_RD_DATA:
        case LN_OPC_WR_SL_DATA:
            if (frame.len >= 14) {
                unsigned address = (d[9] << 7) | d[4];
                snprintf(out, out_size, "S%u %s %u", d[2],
                         d[0] == LN_OPC_WR_SL_DATA ? "write" : "data", address);
            } else {
                snprintf(out, out_size, "%s", lnOpcodeName(d[0]) + 4);
            }
            break;
        case LN_OPC_IMM_PACKET: snprintf(out, out_size, "Function packet"); break;
        case LN_OPC_LONG_ACK:   snprintf(out, out_size, "Ack for %02X", d[1] | 0x80); break;
        case LN_OPC_SW_REQ: {
            unsigned address = (((d[2] & 0x0F) << 7) | d[1]) + 1;
            snprintf(out, out_size, "Turnout %u %s", address, (d[2] & 0x20) ? "closed" : "thrown");
            break;
        }
        default:
            snprintf(out, out_size, "%s", lnOpcodeName(d[0]) + 4);
            break;
    }
}

// ============================================================================
// FRAME PARSER
// ============================================================================

LnFrameParser::LnFrameParser() {
    memset(buffer, 0, sizeof(buffer));
    memset(&completed, 0, sizeof(completed));
    memset(&rejected_frame, 0, sizeof(rejected_frame));
    memset(&counters, 0, sizeof(counters));
    index = 0;
    expected_len = 0;
    last_byte_ms = 0;
}

void LnFrameParser::reset() {
    index = 0;
    expected_len = 0;
}

void LnFrameParser::beginMessage(uint8_t opcode) {
    buffer[0] = opcode;
    index = 1;
    expected_len = lnOpcodeFixedLength(opcode);
}

LnFrameParser::Result LnFrameParser::feed(uint8_t byte, uint32_t now_ms) {
    counters.bytes++;
    last_byte_ms = now_ms;

    // An opcode byte may only start a message, so one arriving mid-message
    // means the previous message was cut short (collision break, noise).
    if (byte & 0x80) {
        Result result = NONE;
        if (index != 0) {
            counters.framing_errors++;
            result = FRAMING_ERROR;
        }
        beginMessage(byte);
        return result;
    }

    if (index == 0) {
        counters.stray_bytes++;
        return NONE;
    }

    buffer[index++] = byte;

    // Variable-length message: the second byte is the total byte count.
    if (expected_len == 0 && index == 2) {
        if (byte < 3 || byte > LN_MAX_FRAME_BYTES) {
            counters.framing_errors++;
            reset();
            return FRAMING_ERROR;
        }
        expected_len = byte;
    }

    if (expected_len != 0 && index == expected_len) {
        bool valid = lnChecksumValid(buffer, index);
        if (valid) {
            memcpy(completed.data, buffer, index);
            completed.len = index;
            counters.frames_ok++;
        } else {
            memcpy(rejected_frame.data, buffer, index);
            rejected_frame.len = index;
            counters.bad_checksum++;
        }
        reset();
        return valid ? FRAME : BAD_CHECKSUM;
    }

    return NONE;
}

void LnFrameParser::tick(uint32_t now_ms) {
    if (index != 0 && (uint32_t)(now_ms - last_byte_ms) >= PARTIAL_TIMEOUT_MS) {
        counters.timeouts++;
        reset();
    }
}

bool lnResyncDue(bool needed, uint32_t now_ms, uint32_t last_resync_ms,
                 uint32_t last_activity_ms, uint32_t interval_ms, uint32_t quiet_ms) {
    if (!needed || interval_ms == 0) {
        return false;
    }
    if ((uint32_t)(now_ms - last_resync_ms) < interval_ms) {
        return false;
    }
    return (uint32_t)(now_ms - last_activity_ms) >= quiet_ms;
}
