/*
 * XpressNet Interface - Master Device Implementation
 *
 * Implements XpressNet master device functionality on ESP8266.
 * - Receives commands from slave devices (throttles) via RS485
 * - Routes commands to command router for state update and broadcast to Ecos
 * - Sends speed/direction and function commands back to bus
 *
 * Wraps Philipp Gahtow's XpressNetMaster library (libraries/XpressNetMaster), which
 * owns the real half-duplex single-wire serial framing (62500 baud, 8N1+parity) and
 * XpressNet call-byte/checksum handling. This class only translates between that
 * library's callback API and CommandRouter.
 */

#ifndef XPRESSNET_INTERFACE_H
#define XPRESSNET_INTERFACE_H

#include <cstdint>
#include <XpressNetMaster.h>
#include "../../interfaces/interface_base.h"
#include "../../definitions.h"

// Forward declaration
class CommandRouter;

class XpressNetInterface : public ProtocolInterface {
public:
    XpressNetInterface();
    ~XpressNetInterface();

    /**
     * Initialize XpressNet hardware via XpressNetMasterClass::setup()
     * Note: the library itself hangs (infinite loop, blinks nothing) if the
     * data/control pin configuration is invalid - this is a hard fault baked
     * into the library, not something this wrapper can recover from.
     * @return true if initialization successful
     */
    bool begin() override;

    /**
     * Non-blocking update - pumps XpressNetMasterClass::update()
     * Called every main loop iteration (HIGHEST priority)
     * Must NEVER block
     */
    void update() override;

    /**
     * Send speed/direction command to XpressNet bus
     * @param address DCC locomotive address
     * @param speed 0-126 (0=stop)
     * @param direction 0=reverse, 1=forward
     */
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override;

    /**
     * Send function states to XpressNet bus
     * Sends one packet per group that has changed (F0-F4, F5-F8, F9-F12, F13-F20, F21-F28)
     * @param address DCC locomotive address
     * @param functions 32-bit bitmap (bit 0=F0, bit 1=F1, ..., bit 31=F31)
     */
    void sendFunctionCommand(uint16_t address, uint32_t functions) override;

    /**
     * Get current connection status
     * @return CONNECTED, DISCONNECTED, ERROR
     */
    ComponentStatus getStatus() const override {
        return current_status;
    }

    /**
     * Get protocol name for display
     * @return "XpressNet"
     */
    const char* getName() const override {
        return "XpressNet";
    }

    /**
     * Set reference to command router (called after construction)
     * Router is used to handle incoming commands and update state
     * @param router Pointer to CommandRouter instance
     */
    void setCommandRouter(CommandRouter* router) {
        this->router = router;
    }

    // ------------------------------------------------------------------
    // Called only by the file-scope notifyXNet* callbacks (weak symbols
    // required by XpressNetMasterClass - they can't be member functions).
    // Public only for that purpose, not part of the ProtocolInterface API.
    // ------------------------------------------------------------------

    /**
     * Handle a 128-speed-step drive notification
     * @param address DCC address
     * @param speed_byte Raw RVVVVVVV byte: bit7=direction, bits6-0=speed 0-126
     */
    void onLocoDrive128(uint16_t address, uint8_t speed_byte);

    /**
     * Handle one fragment of a function-state notification and merge it
     * into the loco's full F0-F31 bitmap before forwarding to the router.
     * @param address DCC address
     * @param group Which fragment: 1=F0-F4, 2=F5-F8, 3=F9-F12, 4=F13-F20, 5=F21-F28
     * @param bits Raw group byte from the library callback
     */
    void onLocoFunctionGroup(uint16_t address, uint8_t group, uint8_t bits);

private:
    // Hardware state
    ComponentStatus current_status;         // CONNECTED, DISCONNECTED, ERROR
    unsigned long last_message_time;        // Timestamp of last received message
    unsigned long bus_connect_time;         // When we first detected bus activity
    static const unsigned long BUS_TIMEOUT = 5000;  // 5 seconds = no bus

    // Library instance - owns the half-duplex SoftwareSerial and XpressNet framing
    XpressNetMasterClass xnet;

    // Command routing
    CommandRouter* router;                  // Reference to main router

    // Helper methods

    /**
     * Update connection status based on activity
     * Tracks timeouts and status changes
     */
    void updateBusStatus();

    /**
     * Record that a message was received from the bus (called from the
     * notify callbacks) - flips CONNECTING -> CONNECTED on first activity
     * and resets the timeout clock.
     */
    void markBusActivity();

    /**
     * Build the outbound byte for one function group from the full 32-bit
     * bitmap, applying the group-1 bit remap (F0 lives at bit4, not bit0 -
     * a real Lenz XpressNet quirk, not a bug).
     * @param functions Full F0-F31 bitmap
     * @param group 1=F0-F4, 2=F5-F8, 3=F9-F12, 4=F13-F20, 5=F21-F28
     * @return Raw group byte ready for xnet.setFuncNtoM()
     */
    static uint8_t buildFunctionGroupByte(uint32_t functions, uint8_t group);
};

#endif  // XPRESSNET_INTERFACE_H
