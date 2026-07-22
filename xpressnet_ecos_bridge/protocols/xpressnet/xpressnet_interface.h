/*
 * XpressNet Interface - Master Device Implementation
 *
 * Implements XpressNet master device functionality on ESP8266.
 * - Receives commands from slave devices (throttles) via RS485
 * - Parses binary XpressNet protocol messages
 * - Routes commands to command router for state update and broadcast to Ecos
 * - Sends speed/direction and function commands back to bus
 *
 * Non-blocking serial I/O for cooperative multitasking.
 * Uses Gahtow's XpressNetMaster library for hardware abstraction.
 */

#ifndef XPRESSNET_INTERFACE_H
#define XPRESSNET_INTERFACE_H

#include <cstdint>
#include "../../interfaces/interface_base.h"
#include "../../definitions.h"
#include "xpressnet_message_parser.h"

// Forward declaration
class CommandRouter;

class XpressNetInterface : public ProtocolInterface {
public:
    XpressNetInterface();
    ~XpressNetInterface();

    /**
     * Initialize XpressNet hardware
     * - Configure RS485 pins (DE, RE)
     * - Initialize Serial1 at 9600 baud
     * - Start message processing
     * @return true if initialization successful
     */
    bool begin() override;

    /**
     * Non-blocking update - process incoming messages
     * Called every main loop iteration (HIGHEST priority)
     * - Reads available serial data
     * - Parses complete messages
     * - Routes commands to command router
     * - Handles bus status updates
     * - Tracks connection status (timeout detection)
     * Must NEVER block
     */
    void update() override;

    /**
     * Send speed/direction command to XpressNet bus
     * Broadcasts to all throttles for this address
     * @param address DCC locomotive address
     * @param speed 0-126 (0=stop, 127=reserved)
     * @param direction 0=reverse, 1=forward
     */
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override;

    /**
     * Send function states to XpressNet bus
     * May send multiple packets (F0-F7, F8-F15, etc.)
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

    /**
     * Get number of throttles currently connected to XpressNet bus
     * Updated from bus status messages
     * @return device count (0-30)
     */
    uint8_t getDeviceCount() const {
        return device_count;
    }

private:
    // Hardware state
    ComponentStatus current_status;         // CONNECTED, DISCONNECTED, ERROR
    uint8_t device_count;                   // Number of throttles on bus (0-30)
    unsigned long last_message_time;        // Timestamp of last received message
    unsigned long bus_connect_time;         // When we first detected bus activity

    // Serial communication
    HardwareSerial* serial;                 // Serial1 for XpressNet
    uint8_t message_buffer[MAX_XNET_MESSAGE_LENGTH];
    uint8_t buffer_index;
    static const unsigned long BUS_TIMEOUT = 5000;  // 5 seconds = no bus

    // Command routing
    CommandRouter* router;                  // Reference to main router

    // Message parsing
    XNetCommand current_command;            // Last parsed command

    // Helper methods

    /**
     * Check for incoming serial data and parse complete messages
     * Non-blocking - process one message per call
     * @return true if a complete message was parsed and routed
     */
    bool processIncomingMessage();

    /**
     * Update connection status based on activity
     * Tracks timeouts and status changes
     */
    void updateBusStatus();

    /**
     * Handle a successfully parsed XpressNet command
     * Routes to command_router for state update and broadcast
     * @param cmd Parsed command
     */
    void handleCommand(const XNetCommand& cmd);

    /**
     * Build XpressNet speed packet for sending
     * Formats binary message for transmission
     * @param buffer Output: constructed packet
     * @param address DCC address
     * @param speed 0-126
     * @param direction 0=reverse, 1=forward
     * @return packet length in bytes
     */
    uint8_t buildSpeedPacket(uint8_t* buffer, uint16_t address,
                            uint8_t speed, uint8_t direction);

    /**
     * Build XpressNet function packet for sending
     * May need multiple packets for F0-F31
     * @param buffer Output: constructed packet
     * @param address DCC address
     * @param functions 32-bit bitmap
     * @param function_range 0=F0-F7, 1=F8-F15, etc.
     * @return packet length in bytes
     */
    uint8_t buildFunctionPacket(uint8_t* buffer, uint16_t address,
                               uint32_t functions, uint8_t function_range);

    /**
     * Send raw packet to XpressNet bus
     * Handles RS485 control (DE/RE pins)
     * @param buffer Packet data
     * @param length Packet length
     */
    void sendPacket(const uint8_t* buffer, uint8_t length);

    /**
     * Extract device count from bus status message (if applicable)
     * @param cmd Parsed command (if it's a status message)
     */
    void updateDeviceCount(const XNetCommand& cmd);
};

#endif  // XPRESSNET_INTERFACE_H
