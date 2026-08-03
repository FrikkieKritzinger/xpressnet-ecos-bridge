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
     * Handle a 14/27/28-speed-step drive notification. Throttles/locos not
     * configured for 128-step mode send a smaller, differently-encoded speed
     * value (27/28-step interleaves the 5-bit value across the wire byte -
     * see XpressNetMasterClass::setSpeed() for the matching encode side);
     * this rescales it into our internal 128-step-based 0-126 range so the
     * rest of the bridge never needs to know which mode the throttle used.
     * @param address DCC address
     * @param speed_byte Raw wire byte (layout depends on max_steps)
     * @param max_steps 14, 27, or 28 - the throttle's configured step count
     */
    void onLocoDriveStepped(uint16_t address, uint8_t speed_byte, uint8_t max_steps);

    /**
     * Handle a throttle's request for a locomotive's current info - fires
     * when a loco is selected on the throttle (XpressNet header 0xE3,
     * data1=0x00). Answers immediately from this bridge's own StateEngine
     * (defaulting to stopped/no-functions if the loco is unknown), always
     * declaring 128-speed-step mode. Step-count is a master->slave contract
     * XpressNet makes on its own wire, independent of Ecos - Ecos never
     * sees or needs to agree on which step count we told the throttle, so
     * this never needs to wait on an Ecos connection/subscription.
     * @param user_ops Requesting device's XpressNet slot - the reply is
     *                 addressed back to this specific device
     * @param address DCC address being queried
     */
    void onGiveLocoInfo(uint8_t user_ops, uint16_t address);

    /**
     * Handle a Roco MultiMaus-specific loco/function status request
     * (XpressNet header 0xE3, data1=0xF0 - not the generic Lenz 0x00
     * request that onGiveLocoInfo answers). Real MultiMaus hardware uses
     * this proprietary variant instead of/alongside the standard one, and
     * expects the "MM" reply format (SetLocoInfoMM, header 0xE7) covering
     * F0-F20 in one message rather than the standard F0-F12 reply.
     * @param user_ops Requesting device's XpressNet slot - the reply is
     *                 addressed back to this specific device
     * @param address DCC address being queried
     */
    void onGiveLocoMM(uint8_t user_ops, uint16_t address);

    /**
     * Handle a track-power/emergency-stop state change request (e.g. the
     * MultiMaus's physical STOP button). The library only updates its own
     * internal state and fires this callback - it never automatically
     * echoes an acknowledgment onto the bus, so without this, throttles
     * sit waiting for a confirmation that never comes (observed as the
     * MultiMaus's display freezing until physically unplugged/replugged).
     * This is the minimal wire-protocol fix only: it echoes the state back
     * via xnet.setPower() so throttles get their acknowledgment. It does
     * NOT force any locomotives to actually stop - see CLAUDE.md's Future
     * Improvements for the fuller safety feature, deliberately deferred.
     * @param state csNormal, csEmergencyStop, or csTrackVoltageOff
     */
    void onPowerStateChange(uint8_t state);

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
    // See XPRESSNET_BUS_TIMEOUT in config.h for why this is 120s, not a
    // shorter "obviously safe" value.
    static const unsigned long BUS_TIMEOUT = XPRESSNET_BUS_TIMEOUT;
    bool was_master_mode;                   // Last known xnet.getOperationModeMaster() - edge-triggered tripwire

    // TEMP: visual health indicator for hardware testing - onboard LED
    // blinks briefly on every real XpressNet message, so bus activity is
    // visible without a serial monitor attached. Remove once Ecos-side
    // round-trip testing is done. 0 = LED currently off/idle.
    static const unsigned long ACTIVITY_LED_MS = 40;
    unsigned long led_off_at_ms;

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
     * Shared lookup for onGiveLocoInfo/onGiveLocoMM: resolves a loco's
     * current speed/direction/functions from the StateEngine (defaulting
     * to stopped/no-functions if unknown) and encodes the RVVVVVVV speed
     * byte both reply formats need.
     */
    void resolveLocoStateForReply(uint16_t address, uint8_t& speed_byte, uint32_t& functions);

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
