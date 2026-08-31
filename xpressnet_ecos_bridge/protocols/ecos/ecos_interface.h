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
#include "../../eeprom_config.h"
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
     * Round-trip latency (ms) of the most recently completed address-map
     * query, for OLED display. Measured from queryAddressMap() writing the
     * request to the first reply entry handleReply() sees for it - not a
     * per-request-ID correlation, but address-map queries only ever
     * overlap with themselves (heartbeat and the less-frequent scheduled
     * refresh both call the same queryAddressMap()), so "most recent send,
     * first reply seen since" is an honest round-trip measurement in
     * practice. Returns NO_TIMESTAMP if no round-trip has completed yet.
     */
    unsigned long getLastHeartbeatLatencyMs() const override {
        return last_heartbeat_latency_ms;
    }

    /**
     * Send an accessory/turnout command directly to the fixed
     * ECOS_OBJECT_ACCESSORY_MANAGER object ("set(11, switch[...])") - no
     * per-accessory object ID lookup needed, unlike locomotives. No-op
     * while disconnected, same as sendEmergencyStop()/sendResumeOperation()
     * - a point-in-time command like this doesn't have obvious value in
     * being queued for delivery whenever a reconnect eventually happens,
     * unlike locomotive state which stays continuously relevant.
     */
    void sendAccessoryCommand(uint16_t address, bool diverging) override;

    /**
     * Set reference to command router (called after construction)
     * @param router Pointer to CommandRouter instance
     */
    void setCommandRouter(CommandRouter* router);

    /**
     * Set the EEPROM-loaded config (WiFi SSID/password, Ecos IP, optional
     * bridge static IP) to use in begin() - Phase 6 step 1. Stores a
     * pointer (config outlives this object - it's a global in the .ino),
     * not a copy. Must be called before begin().
     */
    void setConfig(const EepromConfig* config) {
        this->config = config;
    }

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
    const EepromConfig* config;               // EEPROM-loaded settings, see setConfig()

    // ========================================================================
    // ADDRESS MAPPING (DCC address ↔ Ecos object ID)
    // ========================================================================

    struct AddressMapEntry {
        uint16_t dcc_address;
        uint16_t ecos_id;
        uint32_t last_sent_functions;    // What we last told Ecos, so sendFunctionCommand() can send only the bits that changed instead of resending all 32 every time
        bool has_last_sent_functions;    // False until the first send - that one still sends all 32 (paced), since there's nothing to diff against yet
    };

    AddressMapEntry address_map[MAX_ECOS_OBJECTS];
    uint16_t address_map_count;
    unsigned long address_map_last_refresh;  // also doubles as "query sent at" for latency below
    bool awaiting_query_reply;
    unsigned long last_heartbeat_latency_ms;

    /**
     * Find Ecos object ID for a DCC address
     * Returns 0 if not found
     */
    uint16_t findEcosObjectId(uint16_t dcc_address) const;

    /**
     * Find the full address map entry for a DCC address (for reading/
     * updating last_sent_functions). Returns nullptr if not found.
     */
    AddressMapEntry* findAddressMapEntry(uint16_t dcc_address);

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
    // BASELINE STATE QUERY (paced across multiple update() calls)
    // ========================================================================
    // request(id, view/control) only subscribes to future change events - it
    // never reports a loco's state as it stood before subscribing, and the
    // real Ecos protocol has no bulk query, so seeding a freshly-subscribed
    // loco's real speed/direction/all-32-functions baseline takes 34
    // separate get() commands. Real bug found live 2026-08-28: sending all
    // 34 synchronously in one call (inside subscribeToLoco() itself)
    // starved XpressNet's time-critical bus polling badly enough to cause
    // real MultiMaus err13 bus timeouts - this project's non-blocking rule
    // exists for exactly this reason. Paced to one get() per update() call
    // instead, same cooperative-multitasking pattern as everything else here.

    uint16_t baseline_query_object_id;  // 0 = no baseline query in progress
    uint8_t baseline_query_step;        // 0=speed, 1=dir, 2..33=func[0..31]

    /**
     * If a baseline query is in progress, send its next single get() step
     * and advance. No-op if none is pending. Called once per update().
     */
    void sendNextBaselineQueryStep();

    // ========================================================================
    // OUTGOING FUNCTION BITMAP (paced across multiple update() calls)
    // ========================================================================
    // sendFunctionCommand() used to write all 32 individual set(func[n,v])
    // commands to the TCP socket synchronously in one call - the exact same
    // blocking-burst anti-pattern as the baseline query above, just never
    // paced. Real bug found live 2026-08-28: with a genuine XpressNet
    // function change forwarded through this path, Ecos responded with
    // "control[none]" (twice) instead of ever sending a real state
    // confirmation event back - most likely Ecos's own request handling
    // getting overwhelmed by 32 rapid commands for one object, which would
    // also explain why no confirmation ever reached the OTHER throttle-
    // facing protocol (there was nothing valid to forward). Paced to one
    // set() per update() call, same pattern as sendNextBaselineQueryStep().

    uint16_t pending_function_object_id;  // 0 = nothing pending
    uint32_t pending_function_bitmap;
    uint8_t pending_function_step;        // 0..31 = func[0..31]

    /**
     * If a paced function send is in progress, send its next single set()
     * step and advance. No-op if none is pending. Called once per update().
     */
    void sendNextFunctionStep();

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
