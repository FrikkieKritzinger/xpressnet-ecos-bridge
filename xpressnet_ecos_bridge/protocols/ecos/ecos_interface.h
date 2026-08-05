/*
 * Ecos LAN Interface - WiFi TCP Client Master Implementation
 *
 * Manages connection to ESU Ecos command station over WiFi/TCP.
 * Parses text-based object protocol (not XML), tracks locomotive state,
 * subscribes/unsubscribes from locos, prevents echo loops, and routes
 * updates through CommandRouter for multi-throttle consistency.
 *
 * Non-blocking design: all operations in update() are non-blocking poll loops.
 */

#ifndef ECOS_INTERFACE_H
#define ECOS_INTERFACE_H

#include <cstdint>
#include <ESP8266WiFi.h>
#include "../../interfaces/interface_base.h"
#include "../../utils/timing.h"
#include "ecos_message_parser.h"

// Forward declaration
class CommandRouter;

class EcosInterface : public ProtocolInterface {
public:
    EcosInterface();
    ~EcosInterface();

    /**
     * Initialize WiFi and Ecos TCP connection
     * Starts WiFi connection (non-blocking); actual TCP connection happens in update()
     * @return true if initialization started successfully
     */
    bool begin() override;

    /**
     * Non-blocking update - process TCP data, connection state, heartbeat
     * Called every main loop iteration (lower priority than XpressNet)
     * - Reads available TCP bytes into message parser
     * - Updates connection status with backoff retry
     * - Sends periodic heartbeat to keep connection alive
     * - Flushes the pending-query buffer (unresolved addresses, including
     *   anything queued while disconnected)
     * Must NEVER block
     */
    void update() override;

    /**
     * Send speed/direction command to Ecos for a locomotive
     * @param address DCC locomotive address
     * @param speed 0-126
     * @param direction 0=reverse, 1=forward
     */
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override;

    /**
     * Send function states to Ecos for a locomotive
     * @param address DCC locomotive address
     * @param functions Bitmap F0-F31
     */
    void sendFunctionCommand(uint16_t address, uint32_t functions) override;

    /**
     * Get current Ecos connection status
     * @return CONNECTED, DISCONNECTED, CONNECTING, ERROR
     */
    ComponentStatus getStatus() const override {
        return current_status;
    }

    /**
     * Get protocol name for display
     * @return "Ecos"
     */
    const char* getName() const override {
        return "Ecos";
    }

    /**
     * Subscribe to locomotive updates on Ecos
     * Called by CommandRouter when a new loco is first seen from XpressNet
     * @param address DCC address
     */
    void subscribeToLoco(uint16_t address) override;

    /**
     * Unsubscribe from locomotive on Ecos
     * Called by CommandRouter when a loco expires from state engine
     * @param address DCC address
     */
    void unsubscribeFromLoco(uint16_t address) override;

    /**
     * Send the real Ecos system-wide stop command ("set(1, stop)" - the
     * same command its own STOP button sends). Called by CommandRouter on
     * a bus-wide XpressNet emergency-stop/track-power-off request.
     */
    void sendEmergencyStop() override;

    /**
     * Send the real Ecos system-wide resume command ("set(1, go)").
     * Called by CommandRouter when XpressNet signals normal operation
     * resumed.
     */
    void sendResumeOperation() override;

    /**
     * Set reference to command router (called after construction)
     * @param router Pointer to CommandRouter instance
     */
    void setCommandRouter(CommandRouter* router);

private:
    // ========================================================================
    // CONNECTION STATE
    // ========================================================================

    ComponentStatus current_status;
    unsigned long connected_time_ms;          // When current TCP connection established
    unsigned long last_message_time;          // Last byte received from Ecos
    WiFiClient wifi_client;                   // TCP socket to Ecos
    EcosMessageParser* parser;                // Message parser (allocated in begin())
    CommandRouter* router;                    // Pointer to router for callbacks

    // ========================================================================
    // ADDRESS MAPPING (DCC address ↔ Ecos object ID)
    // ========================================================================

    struct AddressMapEntry {
        uint16_t dcc_address;
        uint16_t ecos_id;
    };

    AddressMapEntry address_map[MAX_ECOS_OBJECTS];
    uint16_t address_map_count;
    unsigned long address_map_last_refresh;

    /**
     * Find Ecos object ID for a DCC address
     * Returns 0 if not found
     */
    uint16_t findEcosObjectId(uint16_t dcc_address) const;

    /**
     * Add entry to address map
     * Returns false if map is full
     */
    bool addAddressMapEntry(uint16_t dcc_address, uint16_t ecos_id);

    /**
     * Query Ecos for list of all locomotives and their addresses
     * Results are accumulated in handleReply() into address_map
     */
    void queryAddressMap();

    // ========================================================================
    // PENDING QUERY BUFFER
    // ========================================================================

    struct PendingQuery {
        uint16_t address;
        uint8_t speed;
        uint8_t direction;
        uint32_t functions;
        bool has_speed_direction;  // whether speed/direction should be flushed
        bool has_functions;        // whether functions should be flushed
    };

    PendingQuery pending_queries[MAX_PENDING_QUERIES];
    uint16_t pending_query_count;

    /**
     * Queue a speed/direction command for an address whose Ecos object ID
     * isn't known yet. Upserts by address - if a function command for the
     * same address is already queued, this only updates the speed/
     * direction fields, leaving the queued functions untouched. When the
     * address map is updated, these are processed by flushPendingQueries().
     */
    void queuePendingQuery(uint16_t address, uint8_t speed, uint8_t direction);

    /**
     * Queue a function command for an address whose Ecos object ID isn't
     * known yet. Upserts by address - mirrors queuePendingQuery(), just for
     * functions instead of speed/direction, so a speed change and a
     * function change queued for the same loco while disconnected merge
     * into one entry instead of competing for the small pending-query
     * buffer.
     */
    void queuePendingFunctionQuery(uint16_t address, uint32_t functions);

    /**
     * Process all pending queries whose addresses are now in the map -
     * replays only the field(s) each entry actually has queued
     * (has_speed_direction/has_functions), not both unconditionally.
     */
    void flushPendingQueries();

    // ========================================================================
    // ECHO PREVENTION QUEUE
    // ========================================================================

    static const int MAX_ECHO_QUEUE = 10;
    static const int ECHO_TYPE_SPEED = 0;
    static const int ECHO_TYPE_FUNCTION = 1;

    struct EchoEntry {
        uint16_t address;
        uint8_t cmd_type;
        uint8_t value;
        unsigned long timestamp;
    };

    EchoEntry echo_queue[MAX_ECHO_QUEUE];
    uint16_t echo_queue_head;
    uint16_t echo_queue_tail;

    /**
     * Add outgoing command to echo queue (so we can suppress our own echoes)
     */
    void addToEchoQueue(uint16_t address, uint8_t cmd_type, uint8_t value);

    /**
     * Check if a received command is an echo of our own recent outgoing command
     * Window: ECOS_ECHO_WINDOW_MS from config.h (2 seconds, accounts for TCP latency)
     */
    bool isEchoCommand(uint16_t address, uint8_t cmd_type, uint8_t value) const;

    // ========================================================================
    // CONNECTION/BACKOFF MANAGEMENT
    // ========================================================================

    TimedTask heartbeat_timer;
    TimedTask address_map_refresh_timer;

    uint16_t reconnect_attempt;              // Current backoff level (0-3)
    unsigned long last_reconnect_attempt;

    /**
     * Update connection state machine
     * Polls WiFi status, TCP socket, timeouts, triggers reconnect
     */
    void updateConnectionStatus();

    /**
     * Attempt to establish TCP connection to Ecos
     * Assumes WiFi is already connected
     */
    void attemptTcpConnect();

    /**
     * Attempt to reconnect with exponential backoff
     */
    void attemptReconnect();

    /**
     * Get current backoff delay (5s, 10s, 20s, 60s max)
     */
    unsigned long getBackoffDelay();

    /**
     * Send heartbeat query to keep connection alive
     * Sends a cheap get() query every 30 seconds
     */
    void sendHeartbeat();

    // ========================================================================
    // REPLY HANDLING
    // ========================================================================

    /**
     * Process a parsed Ecos reply/event
     * Routes to address map update, echo prevention check, or CommandRouter
     */
    void handleReply(const EcosReply& reply);
};

#endif  // ECOS_INTERFACE_H
