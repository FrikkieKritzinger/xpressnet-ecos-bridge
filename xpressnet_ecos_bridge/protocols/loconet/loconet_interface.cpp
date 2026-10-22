#include "loconet_interface.h"

#include <Arduino.h>
#include "../../utils/debug.h"

#if ENABLE_LOCONET

// Bytes drained from the UART per update() call, so a flood on the bus can
// never starve XpressNet polling in the same loop.
static const int MAX_BYTES_PER_UPDATE = 128;

LocoNetInterface::LocoNetInterface()
    : seen_frame(false), last_frame_ms(0), last_rx_us(0), tx_not_before_ms(0),
      tx_attempts(0), tx_frames(0), tx_collisions(0), tx_dropped(0) {}

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

    // Transmit-only SoftwareSerial, inverted (the BC547 pulls the bus low
    // when the pin is high). The receive side is the hardware UART.
    ln_tx.begin(LOCONET_BAUD, SWSERIAL_8N1, -1, LOCONET_TX_PIN, true);

    tx_not_before_ms = millis() + LOCONET_TX_STARTUP_DELAY_MS;
    last_rx_us = micros();

    DEBUG_LOCONET_PRINTF("LocoNet: slot server on UART0 RX (GPIO%d) at %d baud, TX on GPIO%d\n",
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
        last_rx_us = micros();

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
                slot_server.handleFrame(parser.frame(), now);
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

    serviceTransmit(now);
}

// ============================================================================
// TRANSMIT
// ============================================================================

void LocoNetInterface::serviceTransmit(unsigned long now_ms) {
    if (!slot_server.hasReply()) {
        return;
    }
    if ((long)(now_ms - tx_not_before_ms) < 0) {
        return;
    }
    // The bus must be quiet: nothing waiting to be read, no message half
    // received, and at least the carrier-detect backoff since the last byte.
    // Our idea of "last byte" is when we polled it, which is never earlier
    // than when it arrived, so this can only err on the side of waiting.
    if (Serial.available() > 0 || parser.hasPartial()) {
        return;
    }
    if ((uint32_t)(micros() - last_rx_us) < LOCONET_CD_BACKOFF_US) {
        return;
    }

    LnFrame frame;
    if (!slot_server.peekReply(frame)) {
        return;
    }

    if (transmitFrame(frame)) {
        slot_server.dropReply();
        tx_attempts = 0;
        tx_frames++;
        char text[LN_MAX_FRAME_BYTES * 3 + 24];
        lnFormatFrame(frame, text, sizeof(text));
        DEBUG_LOCONET_PRINTF_TS("LN TX: %s\n", text);
    } else {
        tx_collisions++;
        tx_attempts++;
        if (tx_attempts >= LOCONET_TX_RETRIES_MAX) {
            slot_server.dropReply();
            tx_attempts = 0;
            tx_dropped++;
            DEBUG_LOCONET_PRINTF("LN TX: giving up after %d attempts (dropped %lu)\n",
                                 LOCONET_TX_RETRIES_MAX, (unsigned long)tx_dropped);
        } else {
            // Small spread so two devices that collided do not collide again.
            tx_not_before_ms = now_ms + 2 + (micros() % 5);
            DEBUG_LOCONET_PRINTF("LN TX collision (attempt %d, total %lu)\n",
                                 tx_attempts, (unsigned long)tx_collisions);
        }
    }
    last_rx_us = micros();
}

bool LocoNetInterface::transmitFrame(const LnFrame& frame) {
    bool collision = false;

    for (uint8_t i = 0; i < frame.len && !collision; i++) {
        ln_tx.write(frame.data[i]);

        // The bus is wired-OR and the RX pin sees it, so every byte we send
        // must come straight back; anything else means another device was
        // transmitting at the same time.
        uint32_t t0 = micros();
        while (Serial.available() == 0) {
            if ((uint32_t)(micros() - t0) > LOCONET_ECHO_TIMEOUT_US) {
                collision = true;
                break;
            }
        }
        if (!collision && (uint8_t)Serial.read() != frame.data[i]) {
            collision = true;
        }
    }

    if (collision) {
        sendBreak();
        drainRx();
        return false;
    }
    return true;
}

void LocoNetInterface::sendBreak() {
    // Spec: on a transmit collision, hold the line low (SPACE) for 15 bit
    // times so every receiver discards the damaged message. Inverted TX:
    // GPIO high pulls the bus low.
    digitalWrite(LOCONET_TX_PIN, HIGH);
    delayMicroseconds(LOCONET_BREAK_US);
    digitalWrite(LOCONET_TX_PIN, LOW);
}

void LocoNetInterface::drainRx() {
    // Our own break and the other device's damaged message land in the UART;
    // throw them away and restart framing.
    delayMicroseconds(LOCONET_BREAK_US);
    while (Serial.available() > 0) {
        Serial.read();
    }
    parser.reset();
}

// ============================================================================
// REST OF THE INTERFACE
// ============================================================================

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
