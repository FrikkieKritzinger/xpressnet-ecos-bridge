/*
 * Ecos LAN Interface Implementation - WiFi TCP Client
 *
 * Manages TCP connection to ESU Ecos command station, subscribes to locomotives,
 * parses replies/events, synchronizes state with state engine, and broadcasts
 * updates to XpressNet via CommandRouter.
 *
 * Non-blocking polling design: update() processes available TCP data, connection
 * state machine, heartbeat, and the pending-query queue — all without blocking.
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
      echo_queue_head(0),
      echo_queue_tail(0),
      heartbeat_timer(ECOS_HEARTBEAT_INTERVAL),
      address_map_refresh_timer(ECOS_ADDRESS_MAP_REFRESH_INTERVAL),
      reconnect_attempt(0),
      last_reconnect_attempt(0) {
    memset(address_map, 0, sizeof(address_map));
    memset(pending_queries, 0, sizeof(pending_queries));
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

    // Phase 4: Flush pending queries (addresses not yet resolved to an Ecos
    // object ID - including everything queued while disconnected, since the
    // address map is empty then too - see sendSpeedCommand())
    flushPendingQueries();
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
                    // Real bug found on hardware 2026-08-03: this branch
                    // never cleared address_map_count, unlike the
                    // !wifi_client.connected() branch above - but this is
                    // exactly the branch that fires when Ecos's Ethernet
                    // cable is physically unplugged (no TCP reset is ever
                    // generated, so wifi_client.connected() keeps reporting
                    // true and only this timeout ever notices). With the
                    // stale map left intact, findEcosObjectId() kept
                    // resolving real object IDs while genuinely
                    // disconnected, so sendSpeedCommand() took the
                    // "connected" branch and wrote into a dead socket
                    // instead of queuing via queuePendingQuery() - silently
                    // losing commands, the exact failure mode the Phase 5
                    // step 3 fix was supposed to eliminate.
                    address_map_count = 0;
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

        // Subscribe to the base ECoS object's own global run state (STOP/GO/
        // SHUTDOWN), so an operator hitting STOP/GO directly on the Ecos
        // itself propagates back to XpressNet - not just the other
        // direction. See handleReply()'s ECOS_OBJECT_BASE_SYSTEM branch.
        // Real bug found on hardware 2026-08-03: this used a 40-byte buffer,
        // but ecosBuildRequestCmd() requires >= 60 bytes and silently
        // returns 0 (no error) below that - the request was never actually
        // sent, so Ecos had no reason to ever notify us of anything. Every
        // other ecosBuildRequestCmd()/ecosBuildReleaseCmd() call site in
        // this file already uses 80 bytes; this one just didn't match.
        char cmd_buffer[80];
        uint16_t len = ecosBuildRequestCmd(cmd_buffer, sizeof(cmd_buffer),
                                          ECOS_OBJECT_BASE_SYSTEM, ECOS_MODE_VIEW);
        if (len > 0) {
            wifi_client.write((uint8_t*)cmd_buffer, len);
            DEBUG_ECOS_PRINTF("Ecos TX: Subscribed to system status (object 1)\n");
        } else {
            DEBUG_ECOS_PRINTF("Ecos: ERROR - failed to build system status subscribe request\n");
        }

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
    // Upsert by address - real gap found while fixing the fake outgoing
    // queue (2026-08-03): this queue now also has to survive a full Ecos
    // disconnection, not just the brief window before the first address-map
    // reply arrives. Without upserting, repeatedly changing one loco's
    // speed while disconnected would fill the small MAX_PENDING_QUERIES
    // buffer with stale entries for that same address, silently dropping
    // any later command (including the actual final speed) once full -
    // and blocking any other loco from queuing at all.
    for (uint16_t i = 0; i < pending_query_count; i++) {
        if (pending_queries[i].address == address) {
            pending_queries[i].speed = speed;
            pending_queries[i].direction = direction;
            return;
        }
    }

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
            // We now have the object ID, send the queued command. Direction
            // before speed - see sendSpeedCommand() for why: real hardware
            // testing 2026-08-03 showed sending speed first meant a
            // genuine direction change landed correctly but speed read back
            // as stale/0, consistent with Ecos building the real combined
            // DCC packet from a not-yet-updated speed cache at the moment
            // it processes a direction change. Direction was also captured
            // in PendingQuery but never actually sent here at all until
            // this same investigation - only speed was ever replayed.
            char cmd_buffer[80];
            uint16_t len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, pending_queries[i].direction);
            if (len > 0) {
                wifi_client.write((uint8_t*)cmd_buffer, len);
            }

            len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, pending_queries[i].speed);
            if (len > 0) {
                wifi_client.write((uint8_t*)cmd_buffer, len);
                addToEchoQueue(pending_queries[i].address, ECHO_TYPE_SPEED, pending_queries[i].speed);
            }

            DEBUG_ECOS_PRINTF("Ecos TX: Flushed queued command for loco %u (speed=%u dir=%u)\n",
                              pending_queries[i].address, pending_queries[i].speed, pending_queries[i].direction);
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

    // Handle the base ECoS object's own global run state (STOP/GO/SHUTDOWN) -
    // an operator hitting STOP/GO directly on the Ecos itself, not via a
    // throttle. Routes into the same CommandRouter path XpressNet-triggered
    // stops use, tagged with LocoSource::ECOS so it doesn't get echoed back
    // to Ecos as a redundant set(1, stop)/set(1, go).
    if (reply.object_id == ECOS_OBJECT_BASE_SYSTEM && reply.has_system_status) {
        if (router) {
            if (reply.system_status == EcosReply::SYSTEM_STATUS_GO) {
                DEBUG_ECOS_PRINTF("Ecos: system status GO\n");
                router->resumeOperation(LocoSource::ECOS);
            } else {
                // STOP and SHUTDOWN both mean "not running" - treat SHUTDOWN
                // as a stop too, safer to halt locos than to ignore it.
                DEBUG_ECOS_PRINTF("Ecos: system status STOP/SHUTDOWN\n");
                router->emergencyStopAll(LocoSource::ECOS);
            }
        }
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
                    router->handleEcosCommand(dcc_address, reply.speed, reply.direction,
                                               reply.has_speed, reply.has_direction);
                }
                if (reply.has_functions) {
                    router->handleEcosFunctionCommand(dcc_address, reply.functions, reply.functions_mask);
                }
            }
        }
        return;
    }

    // Diagnostic fallback: something arrived with a real object ID but
    // didn't match any handler above (address map, loco state, system
    // status). Previously this just silently vanished with zero trace -
    // added after the 2026-08-03 investigation into Ecos-triggered STOP/GO
    // not reaching XpressNet, to make any future silently-dropped
    // reply/event visible instead of indistinguishable from "nothing arrived
    // at all".
    if (reply.object_id > 0) {
        DEBUG_ECOS_PRINTF("Ecos: unhandled reply/event for object %u (end_code=%d)\n",
                          reply.object_id, reply.end_code);
    }
}

// ============================================================================
// OUTGOING COMMANDS - BROADCAST FROM XPRESSNET
// ============================================================================

void EcosInterface::sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    // Real bug found on hardware 2026-08-03: this used to special-case
    // "not connected" by calling queueOutgoingCommand("", 0) - its own
    // comment admitted "mark for queue, but actually just drop" - so any
    // command issued while Ecos was disconnected was silently lost, not
    // queued as the surrounding machinery implied.
    //
    // No special case is needed: while disconnected, address_map_count is
    // always 0 (cleared in updateConnectionStatus() on disconnect), so
    // findEcosObjectId() naturally returns 0 below, which already routes
    // into queuePendingQuery() - the exact same real address+speed+direction
    // queue used for "loco not resolved yet" while connected. One mechanism
    // instead of two, and this one actually works.
    uint16_t obj_id = findEcosObjectId(address);

    if (obj_id == 0) {
        // Unknown address yet, or Ecos currently disconnected/reconnecting -
        // queue the command for when the address map is available.
        queuePendingQuery(address, speed, direction);
        return;
    }

    char cmd_buffer[80];

    // Direction sent BEFORE speed - real DCC decoders receive speed and
    // direction combined in one packet, not as two independent properties,
    // so Ecos likely has to construct that real packet from whatever it has
    // cached for both. Real bug found on hardware 2026-08-03: sending speed
    // first meant that if direction was also genuinely changing, Ecos
    // appeared to build the resulting packet using a not-yet-updated
    // (stale, often 0) cached speed at the moment it processed the
    // direction change - direction landed correctly, speed didn't. Sending
    // direction first means speed is the last property changed, so it's
    // the value in Ecos's cache by the time any packet gets constructed.
    //
    // Real bug found on hardware 2026-07-31: direction was accepted as a
    // parameter here but never actually sent at all - ecosBuildSetDirectionCmd()
    // existed with zero callers, so Ecos never heard about direction
    // changes from XpressNet at all.
    // NOTE: the ESU spec says Ecos's dir=1 means reverse (opposite of our
    // direction=1=forward convention), but passing the value through
    // UN-inverted is what real-hardware testing confirmed matches - see the
    // matching note in ecos_message_parser.cpp's "dir" key handling.
    uint16_t len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, direction);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        DEBUG_ECOS_PRINTF("Ecos TX: Direction loco %u = %u\n", address, direction);
    }

    len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, speed);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        addToEchoQueue(address, ECHO_TYPE_SPEED, speed);
        DEBUG_ECOS_PRINTF("Ecos TX: Speed loco %u = %u\n", address, speed);
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

void EcosInterface::sendEmergencyStop() {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    char cmd_buffer[24];
    uint16_t len = ecosBuildSystemStopCmd(cmd_buffer, sizeof(cmd_buffer));
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        DEBUG_ECOS_PRINTF("Ecos TX: System stop\n");
    }
}

void EcosInterface::sendResumeOperation() {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    char cmd_buffer[24];
    uint16_t len = ecosBuildSystemGoCmd(cmd_buffer, sizeof(cmd_buffer));
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        DEBUG_ECOS_PRINTF("Ecos TX: System go\n");
    }
}

// ============================================================================
// COMMAND ROUTER INTEGRATION
// ============================================================================

void EcosInterface::setCommandRouter(CommandRouter* r) {
    router = r;
}
