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
