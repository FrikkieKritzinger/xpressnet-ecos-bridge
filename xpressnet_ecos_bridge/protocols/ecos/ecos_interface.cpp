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
      config(nullptr),
      address_map_count(0),
      address_map_last_refresh(0),
      awaiting_query_reply(false),
      last_heartbeat_latency_ms(NO_TIMESTAMP),
      baseline_query_object_id(0),
      baseline_query_step(0),
      pending_function_object_id(0),
      pending_function_bitmap(0),
      pending_function_step(0),
      pending_query_count(0),
      heartbeat_timer(ECOS_HEARTBEAT_INTERVAL),
      address_map_refresh_timer(ECOS_ADDRESS_MAP_REFRESH_INTERVAL),
      reconnect_attempt(0),
      last_reconnect_attempt(0) {
    memset(address_map, 0, sizeof(address_map));
    memset(pending_queries, 0, sizeof(pending_queries));
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

        // Falls back to the compile-time defaults if setConfig() was never
        // called (shouldn't happen in real firmware - the .ino always calls
        // it before begin() - but keeps this safe standalone, e.g. in a test).
        const char* ssid = config ? config->wifi_ssid : WIFI_SSID;
        const char* password = config ? config->wifi_password : WIFI_PASSWORD;

        // Bridge's own static IP (Phase 6 step 1/2) - mandatory, no DHCP
        // option (decided during step 2 design: Z21 LAN, Phase 6 step 4,
        // will need to know the bridge's own address reliably). Must be
        // called before WiFi.begin(). In real operation this is always
        // present by the time begin() runs - blank/invalid EEPROM forces
        // Setup Mode before normal operation is ever reached - but falls
        // back to DHCP defensively rather than refusing to start at all
        // if it's somehow missing (e.g. a direct/standalone begin() call).
        if (config && config->bridge_ip[0] != '\0') {
            IPAddress ip, gateway, subnet;
            if (ip.fromString(config->bridge_ip) &&
                gateway.fromString(config->bridge_gateway) &&
                subnet.fromString(config->bridge_subnet)) {
                WiFi.config(ip, gateway, subnet);
                DEBUG_ECOS_PRINTF("Using static IP %s\n", config->bridge_ip);
            } else {
                DEBUG_ECOS_PRINTF("WARNING: static IP fields invalid - falling back to DHCP\n");
            }
        } else {
            DEBUG_ECOS_PRINTF("WARNING: no static IP configured - falling back to DHCP (run Setup Mode)\n");
        }

        // Note: WiFi.begin() is fire-and-forget; actual connection happens asynchronously
        // The setup() banner in the .ino documents this, and the update() loop polls status
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        DEBUG_ECOS_PRINTF("Ecos LAN initialized (WiFi SSID=%s)\n", ssid);
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

    // Phase 5: Advance any in-progress baseline state query, one get() per
    // call - see sendNextBaselineQueryStep()'s header comment for why this
    // must stay paced rather than sent as one synchronous burst.
    sendNextBaselineQueryStep();

    // Phase 6: Advance any in-progress outgoing function bitmap send, one
    // set() per call - see sendNextFunctionStep()'s header comment.
    sendNextFunctionStep();
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
    const char* ecos_ip = config ? config->ecos_ip : ECOS_IP;
    DEBUG_ECOS_PRINTF("Attempting TCP connect to %s:%u...\n", ecos_ip, ECOS_PORT);

    // WiFiClient::connect() blocks the whole loop() until it succeeds, fails,
    // or this timeout elapses - see ECOS_TIMEOUT in config.h for the hardware
    // bug this fixes. Must be set before connect(), not after.
    wifi_client.setTimeout(ECOS_TIMEOUT);

    if (wifi_client.connect(ecos_ip, ECOS_PORT)) {
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
        awaiting_query_reply = true;
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

EcosInterface::AddressMapEntry* EcosInterface::findAddressMapEntry(uint16_t dcc_address) {
    for (uint16_t i = 0; i < address_map_count; i++) {
        if (address_map[i].dcc_address == dcc_address) {
            return &address_map[i];
        }
    }
    return nullptr;
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
    address_map[address_map_count].last_sent_functions = 0;
    address_map[address_map_count].has_last_sent_functions = false;
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
            pending_queries[i].has_speed_direction = true;
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
    pending_queries[pending_query_count].functions = 0;
    pending_queries[pending_query_count].has_speed_direction = true;
    pending_queries[pending_query_count].has_functions = false;
    pending_query_count++;
}

void EcosInterface::queuePendingFunctionQuery(uint16_t address, uint32_t functions) {
    // Mirrors queuePendingQuery() - see its comments for why upserting by
    // address matters here too.
    for (uint16_t i = 0; i < pending_query_count; i++) {
        if (pending_queries[i].address == address) {
            pending_queries[i].functions = functions;
            pending_queries[i].has_functions = true;
            return;
        }
    }

    if (pending_query_count >= MAX_PENDING_QUERIES) {
        DEBUG_ECOS_PRINTF("Pending query queue full\n");
        return;
    }

    pending_queries[pending_query_count].address = address;
    pending_queries[pending_query_count].speed = 0;
    pending_queries[pending_query_count].direction = 1;
    pending_queries[pending_query_count].functions = functions;
    pending_queries[pending_query_count].has_speed_direction = false;
    pending_queries[pending_query_count].has_functions = true;
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
            // We now have the object ID, send the queued command(s) - only
            // the field(s) this entry actually has queued, not both
            // unconditionally (an entry queued from a pure function change
            // must not also send a placeholder speed=0/dir=1, and vice
            // versa).
            char cmd_buffer[80];

            if (pending_queries[i].has_speed_direction) {
                // Direction before speed - see sendSpeedCommand() for why:
                // real hardware testing 2026-08-03 showed sending speed
                // first meant a genuine direction change landed correctly
                // but speed read back as stale/0, consistent with Ecos
                // building the real combined DCC packet from a
                // not-yet-updated speed cache at the moment it processes a
                // direction change.
                uint16_t len = ecosBuildSetDirectionCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, pending_queries[i].direction);
                if (len > 0) {
                    wifi_client.write((uint8_t*)cmd_buffer, len);
                }

                len = ecosBuildSetSpeedCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, pending_queries[i].speed);
                if (len > 0) {
                    wifi_client.write((uint8_t*)cmd_buffer, len);
                }

                DEBUG_ECOS_PRINTF("Ecos TX: Flushed queued speed/direction for loco %u (speed=%u dir=%u)\n",
                                  pending_queries[i].address, pending_queries[i].speed, pending_queries[i].direction);
            }

            if (pending_queries[i].has_functions) {
                // Reuse sendFunctionCommand()'s own diff-against-last-sent
                // logic rather than duplicating it - obj_id is already
                // resolved so this re-lookup is cheap.
                sendFunctionCommand(pending_queries[i].address, pending_queries[i].functions);

                DEBUG_ECOS_PRINTF("Ecos TX: Flushed queued functions for loco %u = 0x%08x\n",
                                  pending_queries[i].address, pending_queries[i].functions);
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
        if (awaiting_query_reply) {
            // First entry back since we last sent a query - see
            // getLastHeartbeatLatencyMs()'s comment for why this is an
            // honest round-trip measurement despite not correlating by
            // request ID.
            last_heartbeat_latency_ms = millis() - address_map_last_refresh;
            awaiting_query_reply = false;
        }
        addAddressMapEntry(reply.dcc_address, reply.object_id);
        DEBUG_ECOS_PRINTF("Address map: DCC %u → Ecos ID %u\n", reply.dcc_address, reply.object_id);
        return;
    }

    // Handle locomotive state update (from subscription or get query)
    if (reply.object_id > 0 && (reply.has_speed || reply.has_direction || reply.has_functions)) {
        DEBUG_ECOS_PRINTF("Ecos RX: obj=%u has_speed=%d speed=%u has_dir=%d dir=%u has_func=%d func=0x%08lX mask=0x%08lX\n",
                          reply.object_id, reply.has_speed, reply.speed, reply.has_direction, reply.direction,
                          reply.has_functions, (unsigned long)reply.functions, (unsigned long)reply.functions_mask);
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

        if (dcc_address > 0 && router) {
            // Real bug found live 2026-08-28, fixed in two stages: this
            // block used to gate itself (and, transitively, function data
            // riding along with a speed/direction reply) behind a local
            // isEchoCommand(ECHO_TYPE_SPEED) match, AND CommandRouter's own
            // handleEcosCommand()/handleEcosFunctionCommand() had a SECOND,
            // separate echo gate that dropped the entire update (speed,
            // direction, and functions together) whenever it matched ANY
            // recently-active protocol - correct for not echoing a
            // protocol's own command back to itself, but with three
            // protocols that also meant the OTHER throttle-facing protocol
            // never learned about a genuine cross-protocol change either
            // (confirmed live: XpressNet<->Z21 changes never propagated to
            // each other through Ecos, only Ecos itself ever saw them).
            // Both gates removed - CommandRouter::broadcastCommand() now
            // does correct PER-DESTINATION skipping via wasRecentSource()
            // instead of an all-or-nothing drop.
            if (reply.has_speed || reply.has_direction) {
                router->handleEcosCommand(dcc_address, reply.speed, reply.direction,
                                           reply.has_speed, reply.has_direction);
            }
            if (reply.has_functions) {
                router->handleEcosFunctionCommand(dcc_address, reply.functions, reply.functions_mask);
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
        DEBUG_ECOS_PRINTF("Ecos TX: Speed loco %u = %u\n", address, speed);
    }
}

void EcosInterface::sendFunctionCommand(uint16_t address, uint32_t functions) {
    // See sendSpeedCommand() - a master must keep transmitting regardless
    // of connection state; no special "not connected" case is needed since
    // address_map_count is always 0 while disconnected, so obj_id naturally
    // comes back 0 below and routes into the same queue used for "loco not
    // resolved yet" while connected. Real gap found in codebase audit
    // 2026-08-05: this used to return early here, silently dropping any
    // function command issued before the address map was populated or
    // while Ecos was disconnected, unlike sendSpeedCommand() which queues.
    uint16_t obj_id = findEcosObjectId(address);

    if (obj_id == 0) {
        queuePendingFunctionQuery(address, functions);
        return;
    }

    // Real bug found live 2026-08-28: resending all 32 bits every time,
    // even paced, meant a *second* function change arriving before a
    // still-in-progress 32-step sweep finished just restarted it - with
    // several rapid real button presses (e.g. toggling F8 through F11 in
    // quick succession), most of them never got far enough to complete at
    // all before being superseded, so only roughly the last one in a burst
    // ever actually reached Ecos. Fixed by tracking what was last sent per
    // loco and diffing: once a baseline is known, only the bit(s) that
    // actually changed get sent - typically 1, cheap enough to send
    // synchronously with no pacing needed at all. Pacing is now only used
    // for the genuine first-ever send (nothing to diff against yet, so all
    // 32 bits are meaningful) via sendNextFunctionStep().
    AddressMapEntry* entry = findAddressMapEntry(address);
    if (entry != nullptr && entry->has_last_sent_functions) {
        uint32_t changed = functions ^ entry->last_sent_functions;
        if (changed == 0) {
            return;  // Nothing actually changed - don't send anything
        }
        char cmd_buffer[80];
        for (uint8_t fn = 0; fn < 32; fn++) {
            if (!(changed & (1UL << fn))) continue;
            uint8_t state = (functions >> fn) & 1;
            uint16_t len = ecosBuildSetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), obj_id, fn, state);
            if (len > 0) {
                wifi_client.write((uint8_t*)cmd_buffer, len);
            }
        }
        entry->last_sent_functions = functions;
        DEBUG_ECOS_PRINTF("Ecos TX: Functions loco %u = 0x%08x (diff 0x%08x, immediate)\n",
                          address, functions, changed);
        return;
    }

    // First-ever send for this loco - nothing to diff against, so all 32
    // bits are meaningful. Paced to one per update() tick - see the header
    // comment on pending_function_object_id.
    pending_function_object_id = obj_id;
    pending_function_bitmap = functions;
    pending_function_step = 0;
    if (entry != nullptr) {
        entry->last_sent_functions = functions;
        entry->has_last_sent_functions = true;
    }

    DEBUG_ECOS_PRINTF("Ecos TX: Functions loco %u = 0x%08x (first send, queued, paced)\n", address, functions);
}

void EcosInterface::sendNextFunctionStep() {
    if (pending_function_object_id == 0) {
        return;  // nothing pending
    }

    uint8_t fn = pending_function_step;
    uint8_t state = (pending_function_bitmap >> fn) & 1;
    char cmd_buffer[80];
    uint16_t len = ecosBuildSetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), pending_function_object_id, fn, state);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    pending_function_step++;
    if (pending_function_step >= 32) {
        pending_function_object_id = 0;  // done
    }
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

    // request(id, view/control) only subscribes to FUTURE change events - it
    // does not report the loco's current state. Query the real baseline
    // explicitly so anything Ecos already had (e.g. a function left on from
    // before this bridge session) is actually picked up, instead of the
    // bridge silently assuming speed=0/dir=1/functions=0 until something
    // happens to change again. No bulk "all functions" query exists in the
    // real protocol - each of the 32 has to be asked for individually.
    // Paced one per update() call rather than sent here as one 34-command
    // burst - see sendNextBaselineQueryStep()'s header comment for why.
    baseline_query_object_id = obj_id;
    baseline_query_step = 0;

    DEBUG_ECOS_PRINTF("Subscribed to loco %u (Ecos ID %u), querying baseline state\n", address, obj_id);
}

void EcosInterface::sendNextBaselineQueryStep() {
    if (baseline_query_object_id == 0) {
        return;  // nothing pending
    }

    char cmd_buffer[80];
    uint16_t len = 0;

    if (baseline_query_step == 0) {
        len = ecosBuildGetPropertyCmd(cmd_buffer, sizeof(cmd_buffer), baseline_query_object_id, "speed");
    } else if (baseline_query_step == 1) {
        len = ecosBuildGetPropertyCmd(cmd_buffer, sizeof(cmd_buffer), baseline_query_object_id, "dir");
    } else {
        uint8_t fn = baseline_query_step - 2;
        len = ecosBuildGetFunctionCmd(cmd_buffer, sizeof(cmd_buffer), baseline_query_object_id, fn);
    }

    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
    }

    baseline_query_step++;
    if (baseline_query_step >= 34) {  // 1 speed + 1 dir + 32 functions
        baseline_query_object_id = 0;  // done
    }
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

void EcosInterface::sendAccessoryCommand(uint16_t address, bool diverging) {
    if (current_status != ComponentStatus::CONNECTED) {
        return;
    }

    char cmd_buffer[40];
    uint16_t len = ecosBuildSetAccessoryCmd(cmd_buffer, sizeof(cmd_buffer), address, diverging);
    if (len > 0) {
        wifi_client.write((uint8_t*)cmd_buffer, len);
        DEBUG_ECOS_PRINTF("Ecos TX: Accessory %u -> %s\n", address, diverging ? "diverging" : "straight");
    }
}

// ============================================================================
// COMMAND ROUTER INTEGRATION
// ============================================================================

void EcosInterface::setCommandRouter(CommandRouter* r) {
    router = r;
}
