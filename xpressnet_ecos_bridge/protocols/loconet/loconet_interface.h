/*
 * LocoNet Interface
 *
 * Phase 7 item 3, step P2: receive + slot server. Receives on the hardware
 * UART, frames and checksums every message, and acts as the LocoNet master
 * for slot handling (LnSlotServer): it answers a Digitrax throttle's
 * requests so the throttle can select a loco. Nothing is routed to
 * CommandRouter yet - that is step P3. See config.h (LOCONET CONFIGURATION)
 * for the pin/UART choices and their consequences.
 *
 * Transmit path: replies are sent on the bus once it has been idle for the
 * spec's carrier-detect backoff. Each byte is sent with SoftwareSerial
 * (inverted, through the BC547 on D3) and read back from the hardware UART
 * RX, which sees the bus; a mismatch is a collision, answered with a break
 * and a retry.
 */

#ifndef LOCONET_INTERFACE_H
#define LOCONET_INTERFACE_H

#include <cstdint>
#include <SoftwareSerial.h>
#include "../../interfaces/interface_base.h"
#include "loconet_protocol.h"
#include "loconet_slots.h"

class LocoNetInterface : public ProtocolInterface {
public:
    LocoNetInterface();

    // Must be the very first thing setup() does (before anything slow):
    // releases the bus (GPIO0 is held high by the boot strap, which turns
    // the TX transistor on and pulls LocoNet low) and starts UART0 as the
    // LocoNet receiver. Static: needed even in Setup Mode, where the
    // interface itself is never begun.
    static void initHardware();

    bool begin() override;
    void update() override;

    // Nothing is sent to LocoNet from other protocols yet (step P3).
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override;
    void sendFunctionCommand(uint16_t address, uint32_t functions) override;

    ComponentStatus getStatus() const override;
    const char* getName() const override { return "LocoNet"; }

    unsigned long getLastMessageAgeMs() const override;
    uint32_t getRxFrameCount() const override;
    uint32_t getRxErrorCount() const override;

private:
    LnFrameParser parser;
    LnSlotServer slot_server;
    SoftwareSerial ln_tx;

    bool seen_frame;
    unsigned long last_frame_ms;
    uint32_t last_rx_us;            // when a byte was last read from the UART
    unsigned long tx_not_before_ms;  // start-up wait, then retry backoff
    uint8_t tx_attempts;             // attempts so far for the frame at the head of the queue
    uint32_t tx_frames;
    uint32_t tx_collisions;
    uint32_t tx_dropped;

    void logFrame(const LnFrame& frame);
    void serviceTransmit(unsigned long now_ms);
    bool transmitFrame(const LnFrame& frame);
    void sendBreak();
    void drainRx();
};

#endif  // LOCONET_INTERFACE_H
