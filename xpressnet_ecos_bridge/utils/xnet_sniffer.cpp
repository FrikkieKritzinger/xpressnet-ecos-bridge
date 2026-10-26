#include "xnet_sniffer.h"

#include <Arduino.h>
#include <SoftwareSerial.h>
#include "../config.h"
#include "debug.h"

#if ENABLE_XNET_SNIFFER

// XpressNet: 62500 baud, 8 data bits + a ninth "call byte" bit. The ninth bit
// is read back as the parity bit of a SPACE-parity frame (see the vendored
// XpressNetMaster's XNetReceive()).
static SoftwareSerial g_sniff;
static const uint8_t MAX_DATA = 24;

static bool g_have_call = false;
static uint8_t g_call = 0;
static uint8_t g_data[MAX_DATA];
static uint8_t g_len = 0;
static uint32_t g_bytes = 0;
static uint32_t g_messages = 0;
static unsigned long g_last_report_ms = 0;
static unsigned long g_last_byte_ms = 0;

static const char* callKind(uint8_t call) {
    uint8_t type = (call >> 5) & 0x03;
    if (call == 0x60 || call == 0xE0) return "broadcast";
    switch (type) {
        case 0: return "request-ack";
        case 2: return "inquiry";
        case 3: return "master->slave";
        default: return "other";
    }
}

static void emitMessage() {
    if (!g_have_call || g_len == 0) {
        return;
    }
    char hex[MAX_DATA * 3 + 1];
    size_t pos = 0;
    for (uint8_t i = 0; i < g_len; i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", g_data[i]);
    }
    g_messages++;
    // "inquiry" + data = a slave answering its poll; "master->slave" / "broadcast"
    // + data = the command station talking.
    DEBUG_PRINTF("XN [%lu] call %02X (%s slot %u): %s\n", millis(), g_call,
                callKind(g_call), (unsigned)(g_call & 0x1F), hex);
}

void xnetSnifferBegin() {
    // Driver off: RE/DE low = receive only. Nothing is ever transmitted.
    pinMode(XPRESSNET_CONTROL_PIN, OUTPUT);
    digitalWrite(XPRESSNET_CONTROL_PIN, LOW);
    g_sniff.begin(XPRESSNET_BAUD, SWSERIAL_8S1, XPRESSNET_DATA_PIN, -1, false, 256);
    DEBUG_PRINTF("XpressNet SNIFFER: receive-only on GPIO%d at %d baud\n",
                XPRESSNET_DATA_PIN, XPRESSNET_BAUD);
}

void xnetSnifferUpdate() {
    while (g_sniff.available() > 0) {
        uint8_t data = (uint8_t)g_sniff.read();
        bool call_byte = g_sniff.readParity();
        g_bytes++;
        g_last_byte_ms = millis();
        if (call_byte) {
            emitMessage();          // the message that just ended
            g_call = data;
            g_len = 0;
            g_have_call = true;
        } else if (g_have_call && g_len < MAX_DATA) {
            g_data[g_len++] = data;
        }
    }

    // A message is complete once the next call byte arrives; also flush a long
    // pause so the last message of a burst is not held back.
    unsigned long now = millis();
    if (g_len > 0 && now - g_last_byte_ms > 50) {
        emitMessage();
        g_len = 0;
    }

    if (now - g_last_report_ms >= 30000) {
        g_last_report_ms = now;
        DEBUG_PRINTF("XN stats: %lu bytes, %lu messages\n", (unsigned long)g_bytes, (unsigned long)g_messages);
    }
}

#else

void xnetSnifferBegin() {}
void xnetSnifferUpdate() {}

#endif  // ENABLE_XNET_SNIFFER
