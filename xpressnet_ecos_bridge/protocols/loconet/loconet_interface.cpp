#include "loconet_interface.h"

#include <Arduino.h>
#include "../../utils/debug.h"

#if ENABLE_LOCONET

// Bytes drained from the UART per update() call, so a flood on the bus can
// never starve XpressNet polling in the same loop.
static const int MAX_BYTES_PER_UPDATE = 128;

LocoNetInterface::LocoNetInterface()
    : seen_frame(false), last_frame_ms(0) {}

void LocoNetInterface::initHardware() {
    // Inverted TX: GPIO low = transistor off = bus released.
    pinMode(LOCONET_TX_PIN, OUTPUT);
    digitalWrite(LOCONET_TX_PIN, LOW);

    Serial.setRxBufferSize(LOCONET_RX_BUFFER_BYTES);
    Serial.begin(LOCONET_BAUD);
}

bool LocoNetInterface::begin() {
    parser.reset();
    seen_frame = false;
    DEBUG_LOCONET_PRINTF("LocoNet: passive listen on UART0 RX (GPIO%d) at %d baud, TX released on GPIO%d\n",
                         LOCONET_RX_PIN, LOCONET_BAUD, LOCONET_TX_PIN);
    return true;
}

void LocoNetInterface::update() {
    unsigned long now = millis();

#if LOCONET_RAW_TRACE
    uint8_t raw[24];
    uint8_t raw_count = 0;
#endif

    for (int i = 0; i < MAX_BYTES_PER_UPDATE && Serial.available() > 0; i++) {
        int value = Serial.read();
        if (value < 0) {
            break;
        }
        uint8_t byte = (uint8_t)value;

#if LOCONET_RAW_TRACE
        if (raw_count < sizeof(raw)) {
            raw[raw_count++] = byte;
        }
#endif

        switch (parser.feed(byte, now)) {
            case LnFrameParser::FRAME:
                seen_frame = true;
                last_frame_ms = now;
                logFrame(parser.frame());
                break;
            case LnFrameParser::BAD_CHECKSUM: {
                char text[LN_MAX_FRAME_BYTES * 3 + 8];
                lnFormatFrame(parser.rejected(), text, sizeof(text));
                DEBUG_LOCONET_PRINTF("LN RX bad checksum: %s\n", text);
                break;
            }
            case LnFrameParser::FRAMING_ERROR:
                DEBUG_LOCONET_PRINTF("LN RX framing error (total %lu)\n",
                                     (unsigned long)parser.stats().framing_errors);
                break;
            case LnFrameParser::NONE:
                break;
        }
    }

    parser.tick(now);

#if LOCONET_RAW_TRACE
    if (raw_count > 0) {
        char text[sizeof(raw) * 3 + 1];
        size_t pos = 0;
        for (uint8_t i = 0; i < raw_count; i++) {
            pos += snprintf(text + pos, sizeof(text) - pos, "%02X ", raw[i]);
        }
        DEBUG_LOCONET_PRINTF("LN raw: %s\n", text);
    }
#endif
}

void LocoNetInterface::logFrame(const LnFrame& frame) {
    char text[LN_MAX_FRAME_BYTES * 3 + 24];
    lnFormatFrame(frame, text, sizeof(text));
    DEBUG_LOCONET_PRINTF_TS("LN RX: %s\n", text);
}

void LocoNetInterface::sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    (void)address; (void)speed; (void)direction;
}

void LocoNetInterface::sendFunctionCommand(uint16_t address, uint32_t functions) {
    (void)address; (void)functions;
}

ComponentStatus LocoNetInterface::getStatus() const {
    if (seen_frame && (millis() - last_frame_ms) < LOCONET_BUS_TIMEOUT_MS) {
        return ComponentStatus::CONNECTED;
    }
    return ComponentStatus::DISCONNECTED;
}

unsigned long LocoNetInterface::getLastMessageAgeMs() const {
    if (!seen_frame) {
        return NO_TIMESTAMP;
    }
    return millis() - last_frame_ms;
}

uint32_t LocoNetInterface::getRxFrameCount() const {
    return parser.stats().frames_ok;
}

uint32_t LocoNetInterface::getRxErrorCount() const {
    const LnParserStats& s = parser.stats();
    return s.bad_checksum + s.framing_errors + s.timeouts;
}

#endif  // ENABLE_LOCONET
