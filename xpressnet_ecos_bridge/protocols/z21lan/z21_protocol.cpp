/*
 * Z21 LAN Protocol Implementation - Pure Packet Encode/Decode Logic
 */

#include "z21_protocol.h"
#include <cstring>
#include "../../definitions.h"

uint8_t z21Checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum ^= data[i];
    }
    return sum;
}

uint16_t z21DecodeAddress(uint8_t adr_msb, uint8_t adr_lsb) {
    return (uint16_t)((adr_msb & 0x3F) << 8) | adr_lsb;
}

void z21EncodeAddress(uint16_t address, uint8_t& adr_msb, uint8_t& adr_lsb) {
    adr_lsb = (uint8_t)(address & 0xFF);
    adr_msb = (uint8_t)((address >> 8) & 0x3F);
    if (address >= 128) {
        adr_msb |= 0xC0;
    }
}

bool z21DecodeSpeed(uint8_t step_mode, uint8_t speed_byte, uint8_t& out_direction, uint8_t& out_speed) {
    out_direction = (speed_byte & 0x80) ? 1 : 0;

    if (step_mode == Z21_SPEED_STEPS_128) {
        // RVVVVVVV, V=0 Stop, V=1 E-Stop, V=2..127 -> steps 1..126 - a
        // near-1:1 fit for our own internal 0-126 range already.
        uint8_t v = speed_byte & 0x7F;
        out_speed = (v <= 1) ? 0 : (uint8_t)(v - 1);
        return true;
    }

    if (step_mode == Z21_SPEED_STEPS_14) {
        // R000 VVVV, V=0 Stop, V=1 E-Stop, V=2..15 -> steps 1..14.
        uint8_t v = speed_byte & 0x0F;
        uint8_t raw_step = (v <= 1) ? 0 : (uint8_t)(v - 1);
        out_speed = (uint8_t)(((uint16_t)raw_step * DCC_MAX_SPEED) / 14);
        return true;
    }

    if (step_mode == Z21_SPEED_STEPS_28) {
        // R00V5 VVVV - V5 (bit4) adds an odd/even interleave on top of the
        // 14-step encoding's VVVV (bits3-0), doubling the resolution.
        // VVVV=0 -> Stop (either V5), VVVV=1 -> E-Stop (either V5).
        // For VVVV in [2,15]: step = 2*VVVV - 3 + V5 (verified against
        // every row of the spec's own DCC-28 coding table, e.g.
        // V5=0,VVVV=2 -> step1; V5=1,VVVV=2 -> step2; V5=1,VVVV=15 -> step28).
        uint8_t v5 = (speed_byte >> 4) & 0x01;
        uint8_t vvvv = speed_byte & 0x0F;
        uint8_t raw_step;
        if (vvvv <= 1) {
            raw_step = 0;
        } else {
            raw_step = (uint8_t)(2 * vvvv - 3 + v5);
        }
        out_speed = (uint8_t)(((uint16_t)raw_step * DCC_MAX_SPEED) / 28);
        return true;
    }

    return false;
}

uint8_t z21EncodeSpeed128(uint8_t direction, uint8_t speed) {
    uint8_t v = (speed == 0) ? 0 : (uint8_t)(speed + 1);
    uint8_t byte = v & 0x7F;
    if (direction) {
        byte |= 0x80;
    }
    return byte;
}

bool z21DecodeFunctionCommand(uint8_t data_byte, uint8_t& out_function_index, uint8_t& out_switch_type) {
    uint8_t tt = (data_byte >> 6) & 0x03;
    if (tt == 0x03) {
        return false;  // reserved/invalid per spec
    }
    out_switch_type = tt;
    out_function_index = data_byte & 0x3F;
    return true;
}

// ============================================================================
// PACKET BUILDERS
// ============================================================================

size_t z21BuildLocoInfo(uint8_t* buffer, size_t buffer_size, uint16_t address,
                        uint8_t direction, uint8_t speed, uint32_t functions) {
    // DataLen(2) + Header(2) + X-Header(1) + DB0..DB7(8) + XOR(1) = 14 bytes
    static const size_t kPacketLen = 14;
    if (!buffer || buffer_size < kPacketLen) return 0;

    uint8_t adr_msb, adr_lsb;
    z21EncodeAddress(address, adr_msb, adr_lsb);

    // DB4 = 0 D S L F G H J : D=double traction(0), S=smartsearch(0),
    // L=F0 at bit4, F=F4 at bit3, G=F3 at bit2, H=F2 at bit1, J=F1 at bit0 -
    // a Z21-specific scramble, distinct from XpressNet's own F0-at-bit4
    // convention despite looking superficially similar.
    uint8_t db4 = 0;
    if (functions & (1UL << 0)) db4 |= 0x10;  // F0 -> L (bit4)
    if (functions & (1UL << 1)) db4 |= 0x01;  // F1 -> J (bit0)
    if (functions & (1UL << 2)) db4 |= 0x02;  // F2 -> H (bit1)
    if (functions & (1UL << 3)) db4 |= 0x04;  // F3 -> G (bit2)
    if (functions & (1UL << 4)) db4 |= 0x08;  // F4 -> F (bit3)

    // DB5-DB7: F5-F28, sequential (bit0 = lowest function in each byte).
    uint8_t db5 = (uint8_t)((functions >> 5) & 0xFF);   // F5-F12
    uint8_t db6 = (uint8_t)((functions >> 13) & 0xFF);  // F13-F20
    uint8_t db7 = (uint8_t)((functions >> 21) & 0xFF);  // F21-F28

    uint8_t data[9];
    data[0] = Z21_X_LOCO_INFO;
    data[1] = adr_msb;
    data[2] = adr_lsb;
    data[3] = 0x04;  // 0000BKKK: KKK=4 (128 speed steps), B=0 (not busy)
    data[4] = z21EncodeSpeed128(direction, speed);
    data[5] = db4;
    data[6] = db5;
    data[7] = db6;
    data[8] = db7;

    uint8_t xor_byte = z21Checksum(data, sizeof(data));

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_LAN_X & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_LAN_X >> 8) & 0xFF);
    memcpy(buffer + offset, data, sizeof(data));
    offset += sizeof(data);
    buffer[offset++] = xor_byte;

    return offset;
}

size_t z21BuildTrackPowerBroadcast(uint8_t* buffer, size_t buffer_size, bool power_on) {
    static const size_t kPacketLen = 7;
    if (!buffer || buffer_size < kPacketLen) return 0;

    uint8_t data[2];
    data[0] = Z21_X_BC_TRACK_POWER;
    data[1] = power_on ? 0x01 : 0x00;
    uint8_t xor_byte = z21Checksum(data, sizeof(data));

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_LAN_X & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_LAN_X >> 8) & 0xFF);
    memcpy(buffer + offset, data, sizeof(data));
    offset += sizeof(data);
    buffer[offset++] = xor_byte;

    return offset;
}

size_t z21BuildStoppedBroadcast(uint8_t* buffer, size_t buffer_size) {
    static const size_t kPacketLen = 7;
    if (!buffer || buffer_size < kPacketLen) return 0;

    uint8_t data[2];
    data[0] = Z21_X_BC_STOPPED;
    data[1] = 0x00;
    uint8_t xor_byte = z21Checksum(data, sizeof(data));

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_LAN_X & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_LAN_X >> 8) & 0xFF);
    memcpy(buffer + offset, data, sizeof(data));
    offset += sizeof(data);
    buffer[offset++] = xor_byte;

    return offset;
}

size_t z21BuildSerialNumberReply(uint8_t* buffer, size_t buffer_size) {
    static const size_t kPacketLen = 8;
    if (!buffer || buffer_size < kPacketLen) return 0;

    // Fixed placeholder serial number - only used by clients for
    // identification bookkeeping, never validated against anything.
    static const uint32_t kSerial = 123456;

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_GET_SERIAL_NUMBER & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_GET_SERIAL_NUMBER >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(kSerial & 0xFF);
    buffer[offset++] = (uint8_t)((kSerial >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)((kSerial >> 16) & 0xFF);
    buffer[offset++] = (uint8_t)((kSerial >> 24) & 0xFF);

    return offset;
}

size_t z21BuildStatusChangedReply(uint8_t* buffer, size_t buffer_size, bool emergency_stop_active) {
    static const size_t kPacketLen = 8;
    if (!buffer || buffer_size < kPacketLen) return 0;

    uint8_t status = emergency_stop_active ? 0x01 : 0x00;  // csEmergencyStop bit only - track power always on, no short circuit, no programming mode

    uint8_t data[3];
    data[0] = Z21_X_STATUS_CHANGED;
    data[1] = 0x22;  // fixed per spec (part of the X-Header pair for this reply)
    data[2] = status;
    uint8_t xor_byte = z21Checksum(data, sizeof(data));

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_LAN_X & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_LAN_X >> 8) & 0xFF);
    memcpy(buffer + offset, data, sizeof(data));
    offset += sizeof(data);
    buffer[offset++] = xor_byte;

    return offset;
}

size_t z21BuildFirmwareVersionReply(uint8_t* buffer, size_t buffer_size) {
    static const size_t kPacketLen = 9;
    if (!buffer || buffer_size < kPacketLen) return 0;

    uint8_t data[4];
    data[0] = Z21_X_FIRMWARE_VERSION;
    data[1] = 0x0A;
    data[2] = 0x01;  // V_MSB - BCD "1"
    data[3] = 0x30;  // V_LSB - BCD "30" -> firmware version "1.30"
    uint8_t xor_byte = z21Checksum(data, sizeof(data));

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_LAN_X & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_LAN_X >> 8) & 0xFF);
    memcpy(buffer + offset, data, sizeof(data));
    offset += sizeof(data);
    buffer[offset++] = xor_byte;

    return offset;
}

size_t z21BuildUnknownCommandReply(uint8_t* buffer, size_t buffer_size) {
    static const size_t kPacketLen = 7;
    if (!buffer || buffer_size < kPacketLen) return 0;

    uint8_t data[2];
    data[0] = Z21_X_UNKNOWN_COMMAND;
    data[1] = 0x82;
    uint8_t xor_byte = z21Checksum(data, sizeof(data));

    size_t offset = 0;
    buffer[offset++] = (uint8_t)(kPacketLen & 0xFF);
    buffer[offset++] = (uint8_t)((kPacketLen >> 8) & 0xFF);
    buffer[offset++] = (uint8_t)(Z21_HEADER_LAN_X & 0xFF);
    buffer[offset++] = (uint8_t)((Z21_HEADER_LAN_X >> 8) & 0xFF);
    memcpy(buffer + offset, data, sizeof(data));
    offset += sizeof(data);
    buffer[offset++] = xor_byte;

    return offset;
}
