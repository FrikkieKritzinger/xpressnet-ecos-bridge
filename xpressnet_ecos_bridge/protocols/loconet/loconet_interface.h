/*
 * LocoNet Interface
 *
 * Phase 7 item 3, step P1: PASSIVE LISTEN ONLY. Receives on the hardware
 * UART, frames and checksums every message, counts good and bad frames and
 * logs what it sees to the network log. Nothing is transmitted and nothing
 * is routed to CommandRouter yet - the command-station (slot server) role
 * and routing come in later steps. See config.h (LOCONET CONFIGURATION) for
 * the pin/UART choices and their consequences.
 */

#ifndef LOCONET_INTERFACE_H
#define LOCONET_INTERFACE_H

#include <cstdint>
#include "../../interfaces/interface_base.h"
#include "loconet_protocol.h"

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

    // Passive for now: nothing is transmitted.
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override;
    void sendFunctionCommand(uint16_t address, uint32_t functions) override;

    ComponentStatus getStatus() const override;
    const char* getName() const override { return "LocoNet"; }

    unsigned long getLastMessageAgeMs() const override;
    uint32_t getRxFrameCount() const override;
    uint32_t getRxErrorCount() const override;

private:
    LnFrameParser parser;
    bool seen_frame;
    unsigned long last_frame_ms;

    void logFrame(const LnFrame& frame);
};

#endif  // LOCONET_INTERFACE_H
