/*
 * Ecos LAN Interface Implementation - WiFi TCP Client
 *
 * Manages TCP connection to ESU Ecos command station, subscribes to locomotives,
 * parses replies/events, synchronizes state with state engine, and broadcasts
 * updates to XpressNet via CommandRouter.
 *
 * Non-blocking polling design: update() processes available TCP data, connection
 * state machine, heartbeat, and outgoing queue — all without blocking.
 */

#include "ecos_interface.h"
#include "ecos_message_parser.h"
#include "ecos_protocol.h"
#include "../../command_router.h"
#include "../../config.h"
#include "../../definitions.h"
#include "../../utils/debug.h"
#include "../../utils/timing.h"
#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <cstring>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

EcosInterface::EcosInterface()
    : current_status(ComponentStatus::DISCONNECTED),
      connected_time_ms(0),
      last_message_time(0),
      parser(nullptr),
      router(nullptr),
      address_map_count(0),
      address_map_last_refresh(0),
      pending_query_count(0),
      outgoing_queue_head(0),
      outgoing_queue_tail(0),
      echo_queue_head(0),
      echo_queue_tail(0),
      heartbeat_timer(ECOS_HEARTBEAT_INTERVAL),
      address_map_refresh_timer(ECOS_ADDRESS_MAP_REFRESH_INTERVAL),
      reconnect_attempt(0),
      last_reconnect_attempt(0) {
    memset(address_map, 0, sizeof(address_map));
    memset(pending_queries, 0, sizeof(pending_queries));
    memset(outgoing_queue, 0, sizeof(outgoing_queue));
    memset(echo_queue, 0, sizeof(echo_queue));
}

EcosInterface::~EcosInterface() {
    if (parser) delete parser;
    wifi_client.stop();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool EcosInterface::begin() {
    #if ENABLE_ECOS_LAN
        DEBUG_ECOS_PRINTF("Initializing Ecos LAN interface...\n");

        // Create message parser
        parser = new EcosMessageParser();
        if (!parser) {
            DEBUG_ECOS_PRINTF("ERROR: Failed to allocate message parser\n");
            current_status = ComponentStatus::ERROR;
            return false;
        }

        // Start WiFi connection (non-blocking, will complete in update() loop)
        current_status = ComponentStatus::CONNECTING;
        last_message_time = millis();

        // Note: WiFi.begin() is fire-and-forget; actual connection happens asynchronously
        // The setup() banner in the .ino documents this, and the update() loop polls status
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        DEBUG_ECOS_PRINTF("Ecos LAN initialized (WiFi SSID=%s)\n", WIFI_SSID);
        return true;

    #else
        DEBUG_PRINTF("Ecos LAN disabled in config.h\n");
        return false;
    #endif
}

// ============================================================================
// MAIN UPDATE LOOP - NON-BLOCKING
// ============================================================================

void EcosInterface::update() {
    #if !ENABLE_ECOS_LAN
        return;
    #endif

    // Phase 1: Update connection/WiFi status
    updateConnectionStatus();

    // Phase 2: Non-blocking TCP read and parse
    if (current_status == ComponentStatus::CONNECTED) {
        while (wifi_client.available()) {
            uint8_t byte = wifi_client.read();
            EcosReply reply;

            if (parser && parser->processByte(byte, reply)) {
                // Complete message parsed - a block enumerating multiple
                // objects (e.g. queryObjects listing every locomotive) queues
                // additional entries beyond this first one; drain them all.
                handleReply(reply);
                while (parser->getNextQueuedReply(reply)) {
                    handleReply(reply);
                }
            }

            yield();  // Let ESP8266 WiFi stack run
        }
    }

    // Phase 3: Periodic housekeeping
    if (heartbeat_timer.shouldExecute()) {
        sendHeartbeat();
    }

    if (address_map_refresh_timer.shouldExecute()) {
        queryAddressMap();
    }

    // Phase 4: Flush pending queries and outgoing commands
    flushPendingQueries();
    flushOutgoingQueue();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

void EcosInterface::updateConnectionStatus() {
    unsigned long now = millis();

    switch (current_status) {
        case ComponentStatus::CONNECTING:
            // Waiting for WiFi to connect
            if (WiFi.status() == WL_CONNECTED) {
                DEBUG_ECOS_PRINTF("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
                // WiFi connected, now try TCP
                attemptTcpConnect();
            } else if (now - last_reconnect_attempt > ECOS_RECONNECT_INTERVAL) {
                // Timeout waiting for WiFi
                current_status = ComponentStatus::DISCONNECTED;
                DEBUG_ECOS_PRINTF("WiFi connection timeout\n");
            }
            break;

        case ComponentStatus::CONNECTED:
            // Check if TCP socket is still alive
            if (!wifi_client.connected()) {
                current_status = ComponentStatus::DISCONNECTED;
                address_map_count = 0;  // Address map stale, will refresh on next connect
                DEBUG_ECOS_PRINTF("Ecos TCP disconnected\n");
            } else {
                // Check for heartbeat timeout (no data for > ECOS_MESSAGE_TIMEOUT)
                if (now - last_message_time > ECOS_MESSAGE_TIMEOUT) {
                    DEBUG_ECOS_PRINTF("Ecos heartbeat timeout\n");
                    wifi_client.stop();
                    current_status = ComponentStatus::DISCONNECTED;
                }
            }
            break;

        case ComponentStatus::DISCONNECTED:
            // Attempt reconnect with exponential backoff
            if (now - last_reconnect_attempt >= getBackoffDelay()) {
                attemptReconnect();
            }
            break;

        case ComponentStatus::ERROR:
            // Permanent error state (shouldn't happen yet)
            break;
    }
}

void EcosInterface::attemptTcpConnect() {
    DEBUG_ECOS_PRINTF("Attempting TCP connect to %s:%u...\n", ECOS_IP, ECOS_PORT);

    // WiFiClient::connect() blocks the whole loop() until it succeeds, fails,
    // or this timeout elapses - see ECOS_TIMEOUT in config.h for the hardware
    // bug this fixes. Must be set before connect(), not after.
    wifi_client.setTimeout(ECOS_TIMEOUT);

    if (wifi_client.connect(ECOS_IP, ECOS_PORT)) {
        current_status = ComponentStatus::CONNECTED;
        connected_time_ms = millis();
        last_message_time = millis();
        reconnect_attempt = 0;
        last_reconnect_attempt = 0;

        // Disable Nagle's algorithm for low-latency
        wifi_client.setNoDelay(true);

        // Kick off address map query
        queryAddressMap();

        DEBUG_ECOS_PRINTF("Ecos TCP connected!\n");
    } else {
        current_status = ComponentStatus::DISCONNECTED;
        DEBUG_ECOS_PRINTF("Ecos TCP connect failed\n");
    }
}

void EcosInterface::attemptReconnect() {
    last_reconnect_attempt = millis();

    // Check if WiFi is still connected
    if (WiFi.status() != WL_CONNECTED) {
        current_status = ComponentStatus::CONNECTING;
        DEBUG_ECOS_PRINTF("WiFi disconnected, attempting reconnect...\n");
        return;
    }

    // WiFi OK, try TCP
    attemptTcpConnect();
}

unsigned long EcosInterface::getBackoffDelay() {
    // Exponential backoff: 5s, 10s, 20s, 60s (capped)
    unsigned long delays[] = {5000, 10000, 20000, 60000};
    int max_index = sizeof(delays) / sizeof(delays[0]);
    if (reconnect_attempt >= max_index) reconnect_attempt = max_index - 1;
    return delays[reconnect_attempt];
}

void EcosInterface::sendHeartbeat() {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    // ECOS_OBJECT_LOCOMOTIVE_MANAGER (10) is a query category, not an addressable
    // object - get(10, name) returns a real Ecos error ("internal error"), confirmed
    // against real hardware. queryObjects(10, addr, name) is the already-proven-working
    // command for that category, so reuse it as the keep-alive - it doubles as an
    // incidental address-map refresh between the 10-minute scheduled ones.
    DEBUG_ECOS_PRINTF("Heartbeat: ");
    queryAddressMap();
}

// ============================================================================
// COMMAND QUEUEING (for while disconnected)
// ============================================================================

bool EcosInterface::queueOutgoingCommand(const char* cmd, uint16_t len) {
    if (len >= MAX_COMMAND_LENGTH - 1) {
        return false;  // Command too large
    }

    // Calculate next write position (circular buffer)
    uint16_t next_tail = (outgoing_queue_tail + 1) % MAX_OUTGOING_QUEUE;

    // Check if queue full
    if (next_tail == outgoing_queue_head) {
        DEBUG_ECOS_PRINTF("Ecos: Outgoing queue full, dropping oldest\n");
        // Discard oldest entry (move head forward)
        outgoing_queue_head = (outgoing_queue_head + 1) % MAX_OUTGOING_QUEUE;
    }

    // Store command
    strncpy(outgoing_queue[outgoing_queue_tail].cmd, cmd, MAX_COMMAND_LENGTH - 1);
    outgoing_queue[outgoing_queue_tail].cmd[MAX_COMMAND_LENGTH - 1] = '\0';
    outgoing_queue[outgoing_queue_tail].len = len;
    outgoing_queue[outgoing_queue_tail].timestamp = millis();

    outgoing_queue_tail = next_tail;
    return true;
}

void EcosInterface::flushOutgoingQueue() {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    while (outgoing_queue_head != outgoing_queue_tail) {
        const auto& entry = outgoing_queue[outgoing_queue_head];
        wifi_client.write((uint8_t*)entry.cmd, entry.len);

        outgoing_queue_head = (outgoing_queue_head + 1) % MAX_OUTGOING_QUEUE;
    }
}

// ============================================================================
// ADDRESS MAP MANAGEMENT
// ============================================================================

void EcosInterface::queryAddressMap() {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    DEBUG_ECOS_PRINTF("Querying Ecos address map...\n");

    char cmd_buffer[80];
    uint16_t len = ecosBuildQueryObjectsCmd(cmd_buffer, sizeof(cmd_buffer));

    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    address_map_last_refresh = millis();
}

uint16_t EcosInterface::findEcosObjectId(uint16_t dcc_address) const {
    for (uint16_t i = 0; i < address_map_count; i++) {
        if (address_map[i].dcc_address == dcc_address) {
            return address_map[i].ecos_id;
        }
    }
    return 0;  // Not found
}

bool EcosInterface::addAddressMapEntry(uint16_t dcc_address, uint16_t ecos_id) {
    // Upsert: queryObjects() now re-runs on every heartbeat (30s), so the
    // same locomotive list arrives repeatedly - update in place instead of
    // appending duplicates, which would otherwise fill MAX_ECOS_OBJECTS
    // within a few heartbeat cycles on a layout with many locos.
    for (uint16_t i = 0; i < address_map_count; i++) {
        if (address_map[i].dcc_address == dcc_address) {
            address_map[i].ecos_id = ecos_id;
            return true;
        }
    }

    if (address_map_count >= MAX_ECOS_OBJECTS) {
        return false;  // Map full
    }

    address_map[address_map_count].dcc_address = dcc_address;
    address_map[address_map_count].ecos_id = ecos_id;
    address_map_count++;
    return true;
}

// ============================================================================
// PENDING QUERY BUFFER
// ============================================================================

void EcosInterface::queuePendingQuery(uint16_t address, uint8_t speed, uint8_t direction) {
    if (pending_query_count >= MAX_PENDING_QUERIES) {
        DEBUG_ECOS_PRINTF("Pending query queue full\n");
        return;
    }

    pending_queries[pending_query_count].address = address;
    pending_queries[pending_query_count].speed = speed;
    pending_queries[pending_query_count].direction = direction;
    pending_query_count++;
}

void EcosInterface::flushPendingQueries() {
    if (pending_query_count == 0 || current_status != ComponentStatus::CONNECTED) {
        return;
    }

    // Process all pending queries for addresses that now have object IDs
    uint16_t remaining = 0;
    for (uint16_t i = 0; i < pending_query_count; i++) {
        uint16_t obj_id = findEcosObjectId(pending_queries[i].address);

        if (obj_id > 0) {
            // We now have the object ID, send the queued command
            char cmd_buffer[80];
            uint16_t len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, pending_queries[i].speed);
            if (len > 0) {
                wifi_client.write((uint8_t*)cmd_buffer, len);
                addToEchoQueue(pending_queries[i].address, ECHO_TYPE_SPEED, pending_queries[i].speed);
            }
            // Don't add to remaining (discard this entry)
        } else {
            // Still don't have object ID, keep it in the buffer
            if (remaining != i) {
                pending_queries[remaining] = pending_queries[i];
            }
            remaining++;
        }
    }

    pending_query_count = remaining;
}

// ============================================================================
// ECHO PREVENTION QUEUE
// ============================================================================

void EcosInterface::addToEchoQueue(uint16_t address, uint8_t cmd_type, uint8_t value) {
    uint16_t next_tail = (echo_queue_tail + 1) % MAX_ECHO_QUEUE;

    if (next_tail == echo_queue_head) {
        // Queue full, discard oldest
        echo_queue_head = (echo_queue_head + 1) % MAX_ECHO_QUEUE;
    }

    echo_queue[echo_queue_tail].address = address;
    echo_queue[echo_queue_tail].cmd_type = cmd_type;
    echo_queue[echo_queue_tail].value = value;
    echo_queue[echo_queue_tail].timestamp = millis();

    echo_queue_tail = next_tail;
}

bool EcosInterface::isEchoCommand(uint16_t address, uint8_t cmd_type, uint8_t value) const {
    unsigned long now = millis();
    uint16_t i = echo_queue_head;

    while (i != echo_queue_tail) {
        const auto& entry = echo_queue[i];

        // Check if this command matches and is within echo window
        if (entry.address == address &&
            entry.cmd_type == cmd_type &&
            now - entry.timestamp < ECOS_ECHO_WINDOW_MS) {
            return true;  // This is our own echo
        }

        i = (i + 1) % MAX_ECHO_QUEUE;
    }

    return false;  // Not our echo
}

// ============================================================================
// REPLY HANDLING
// ============================================================================

void EcosInterface::handleReply(const EcosReply& reply) {
    last_message_time = millis();

    if (reply.end_code != 0) {
        // Error response from Ecos
        DEBUG_ECOS_PRINTF("Ecos error response: %d (%s)\n", reply.end_code, reply.end_text);
        return;
    }

    // Handle queryObjects response (address map population)
    if (reply.has_dcc_address && reply.object_id > 0 && !reply.has_speed) {
        // This looks like a queryObjects result (object ID + DCC address, no speed/dir/functions)
        addAddressMapEntry(reply.dcc_address, reply.object_id);
        DEBUG_ECOS_PRINTF("Address map: DCC %u → Ecos ID %u\n", reply.dcc_address, reply.object_id);
        return;
    }

    // Handle locomotive state update (from subscription or get query)
    if (reply.object_id > 0 && (reply.has_speed || reply.has_direction || reply.has_functions)) {
        // This is a loco state update; find the DCC address
        uint16_t dcc_address = 0;

        if (reply.has_dcc_address) {
            dcc_address = reply.dcc_address;
        } else {
            // Search address map for this object ID
            for (uint16_t i = 0; i < address_map_count; i++) {
                if (address_map[i].ecos_id == reply.object_id) {
                    dcc_address = address_map[i].dcc_address;
                    break;
                }
            }
        }

        if (dcc_address > 0) {
            // Check echo prevention
            if (isEchoCommand(dcc_address, ECHO_TYPE_SPEED, reply.speed)) {
                DEBUG_ECOS_PRINTF("Echo suppressed for loco %u\n", dcc_address);
                return;
            }

            // Route to CommandRouter
            if (router) {
                if (reply.has_speed || reply.has_direction) {
                    router->handleEcosCommand(dcc_address, reply.speed, reply.direction);
                }
                if (reply.has_functions) {
                    router->handleEcosFunctionCommand(dcc_address, reply.functions);
                }
            }
        }
    }
}

// ============================================================================
// OUTGOING COMMANDS - BROADCAST FROM XPRESSNET
// ============================================================================

void EcosInterface::sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    if (current_status != ComponentStatus::CONNECTED) {
        queueOutgoingCommand("", 0);  // Mark for queue, but actually just drop
        return;
    }

    // Look up Ecos object ID
    uint16_t obj_id = findEcosObjectId(address);

    if (obj_id == 0) {
        // Unknown address yet, queue the command for when address map is updated
        queuePendingQuery(address, speed, direction);
        return;
    }

    // Build and send speed command
    char cmd_buffer[80];
    uint16_t len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, speed);

    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        addToEchoQueue(address, ECHO_TYPE_SPEED, speed);
        DEBUG_ECOS_PRINTF("Ecos TX: Speed loco %u = %u\n", address, speed);
    }

    // Real bug found on hardware 2026-07-31: direction was accepted as a
    // parameter here but never actually sent - ecosBuildSetDirectionCmd()
    // existed with zero callers, so Ecos never heard about direction
    // changes from XpressNet at all.
    // NOTE: the ESU spec says Ecos's dir=1 means reverse (opposite of our
    // direction=1=forward convention), but passing the value through
    // UN-inverted is what real-hardware testing confirmed matches - see the
    // matching note in ecos_message_parser.cpp's "dir" key handling.
    len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, direction);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        DEBUG_ECOS_PRINTF("Ecos TX: Direction loco %u = %u\n", address, direction);
    }
}

void EcosInterface::sendFunctionCommand(uint16_t address, uint32_t functions) {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    uint16_t obj_id = findEcosObjectId(address);

    if (obj_id == 0) {
        // Unknown address, can't send yet
        return;
    }

    // Send each function bit that differs from current state
    // (For now, just send all 32 as individual commands — not optimal but safe)
    char cmd_buffer[80];

    for (uint8_t fn = 0; fn < 32; fn++) {
        uint8_t state = (functions >> fn) & 1;
        uint16_t len = ecosBuildSetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, fn, state);

        if (len > 0) {
            wifi_client.write((uint8_t*)cmd_buffer, len);
        }
    }

    DEBUG_ECOS_PRINTF("Ecos TX: Functions loco %u = 0x%08x\n", address, functions);
}

// ============================================================================
// SUBSCRIPTION MANAGEMENT
// ============================================================================

void EcosInterface::subscribeToLoco(uint16_t address) {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    uint16_t obj_id = findEcosObjectId(address);

    if (obj_id == 0) {
        // Address map not yet loaded, retry later
        return;
    }

    // Send subscription requests
    char cmd_buffer[80];

    uint16_t len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, ECOS_MODE_VIEW, false);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, ECOS_MODE_CONTROL, false);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    DEBUG_ECOS_PRINTF("Subscribed to loco %u (Ecos ID %u)\n", address, obj_id);
}

void EcosInterface::unsubscribeFromLoco(uint16_t address) {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    uint16_t obj_id = findEcosObjectId(address);

    if (obj_id == 0) {
        return;  // Not in map, no need to unsubscribe
    }

    // Send release requests
    char cmd_buffer[80];

    uint16_t len = ecosBuildReleaseCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, ECOS_MODE_CONTROL);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    len = ecosBuildReleaseCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, ECOS_MODE_VIEW);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    DEBUG_ECOS_PRINTF("Unsubscribed from loco %u\n", address);
}

// ============================================================================
// COMMAND ROUTER INTEGRATION
// ============================================================================

void EcosInterface::setCommandRouter(CommandRouter* r) {
    router = r;
}
