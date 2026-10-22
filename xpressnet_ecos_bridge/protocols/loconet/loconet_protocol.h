/*
 * LocoNet Protocol - pure framing and decoding helpers
 *
 * No Arduino dependency, so it is unit-tested natively. Everything here is
 * written from the Digitrax "LocoNet Personal Use Edition 1.0" specification
 * (docs/loconet-personal-use-edition-1.0.pdf, gitignored - private-use
 * licence). Opcode constants are our own and deliberately do not copy any
 * third-party library's header.
 *
 * Message framing (spec, "MESSAGE Format"):
 * - Opcode byte has bit 7 set and may only be the first byte; every other
 *   byte, including the checksum, has bit 7 clear.
 * - Opcode bits 6-5 give the length: 00 = 2 bytes, 01 = 4, 10 = 6,
 *   11 = N bytes, where the second byte is a 7-bit byte count.
 * - Checksum is the one's complement of the XOR of all other bytes, so the
 *   XOR of a whole valid message is 0xFF.
 */

#ifndef LOCONET_PROTOCOL_H
#define LOCONET_PROTOCOL_H

#include <cstdint>
#include <cstddef>

// Largest message we buffer. The spec's variable-length messages seen in
// practice (slot read/write, peer transfer, immediate packet) are 14-16
// bytes; anything declaring more than this is treated as a framing error.
#define LN_MAX_FRAME_BYTES 32

// ============================================================================
// OPCODES (spec "LocoNet OPCODE SUMMARY")
// ============================================================================

#define LN_OPC_BUSY          0x81
#define LN_OPC_GPOFF         0x82
#define LN_OPC_GPON          0x83
#define LN_OPC_IDLE          0x85
#define LN_OPC_LOCO_SPD      0xA0
#define LN_OPC_LOCO_DIRF     0xA1
#define LN_OPC_LOCO_SND      0xA2
#define LN_OPC_SW_REQ        0xB0
#define LN_OPC_SW_REP        0xB1
#define LN_OPC_INPUT_REP     0xB2
#define LN_OPC_LONG_ACK      0xB4
#define LN_OPC_SLOT_STAT1    0xB5
#define LN_OPC_CONSIST_FUNC  0xB6
#define LN_OPC_UNLINK_SLOTS  0xB8
#define LN_OPC_LINK_SLOTS    0xB9
#define LN_OPC_MOVE_SLOTS    0xBA
#define LN_OPC_RQ_SL_DATA    0xBB
#define LN_OPC_SW_STATE      0xBC
#define LN_OPC_SW_ACK        0xBD
#define LN_OPC_LOCO_ADR      0xBF
#define LN_OPC_PEER_XFER     0xE5
#define LN_OPC_SL_RD_DATA    0xE7
#define LN_OPC_IMM_PACKET    0xED
#define LN_OPC_WR_SL_DATA    0xEF

// ============================================================================
// FRAME PARSER
// ============================================================================

struct LnFrame {
    uint8_t data[LN_MAX_FRAME_BYTES];
    uint8_t len;
};

struct LnParserStats {
    uint32_t bytes;           // every byte fed in
    uint32_t frames_ok;       // complete frames with a good checksum
    uint32_t bad_checksum;    // complete frames whose checksum failed
    uint32_t framing_errors;  // truncated by a new opcode, or bad length
    uint32_t stray_bytes;     // data bytes seen outside any message
    uint32_t timeouts;        // partial frames abandoned by tick()
};

class LnFrameParser {
public:
    enum Result {
        NONE,            // byte consumed, frame still incomplete (or ignored)
        FRAME,           // a complete, checksum-valid frame is ready: frame()
        BAD_CHECKSUM,    // a complete frame failed its checksum (discarded)
        FRAMING_ERROR    // the byte ended/aborted a message abnormally
    };

    LnFrameParser();

    Result feed(uint8_t byte, uint32_t now_ms);

    // Drops a partially received frame that has been idle too long. Spec
    // senders transmit back-to-back, so a gap this large means the rest of
    // the frame is never coming (e.g. a collision break).
    void tick(uint32_t now_ms);

    void reset();

    const LnFrame& frame() const { return completed; }
    // The most recent complete frame that failed its checksum (for logging).
    const LnFrame& rejected() const { return rejected_frame; }
    const LnParserStats& stats() const { return counters; }
    bool hasPartial() const { return index != 0; }

    // Idle time after which a partial frame is abandoned.
    static const uint32_t PARTIAL_TIMEOUT_MS = 20;

private:
    uint8_t buffer[LN_MAX_FRAME_BYTES];
    uint8_t index;
    uint8_t expected_len;      // 0 until known (variable length: after byte 2)
    uint32_t last_byte_ms;
    LnFrame completed;
    LnFrame rejected_frame;
    LnParserStats counters;

    void beginMessage(uint8_t opcode);
};

// ============================================================================
// HELPERS
// ============================================================================

// Length implied by the opcode alone: 2, 4 or 6, or 0 for "variable, read the
// byte count" (and for non-opcode bytes).
uint8_t lnOpcodeFixedLength(uint8_t opcode);

// XOR checksum byte for the first (len - 1) bytes of a message.
uint8_t lnChecksum(const uint8_t* data, uint8_t len_without_checksum);

// True if the whole message (including checksum) XORs to 0xFF.
bool lnChecksumValid(const uint8_t* data, uint8_t len);

// Spec name for an opcode, or "OPC_UNKNOWN" for anything not in the spec.
const char* lnOpcodeName(uint8_t opcode);

// "BF 00 05 FB  OPC_LOCO_ADR" style text for logging. Always NUL-terminates.
void lnFormatFrame(const LnFrame& frame, char* out, size_t out_size);

#endif  // LOCONET_PROTOCOL_H
