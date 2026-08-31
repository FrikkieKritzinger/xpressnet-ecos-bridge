/*
 * Z21 LAN Interface Implementation - UDP Command Station Emulation
 */

#include "z21lan_interface.h"
#include "z21_protocol.h"
#include "../../command_router.h"
#include "../../definitions.h"
#include "../../utils/debug.h"
#include <Arduino.h>
#include <cstring>

Z21LanInterface::Z21LanInterface()
    : router(nullptr),
      current_status(ComponentStatus::DISCONNECTED),
      emergency_stop_active(false) {
}

bool Z21LanInterface::begin() {
    #if ENABLE_Z21_LAN
        if (!udp.begin(Z21_PORT)) {
            DEBUG_PRINTF("Z21: ERROR - failed to bind UDP port %u\n", (unsigned)Z21_PORT);
            current_status = ComponentStatus::ERROR;
            return false;
        }
        current_status = ComponentStatus::CONNECTED;  // "connected" = socket bound and listening, matches this interface's connectionless nature - there's no persistent link to be up/down the way Ecos's TCP socket has
        DEBUG_PRINTF("Z21 LAN listening on UDP port %u\n", (unsigned)Z21_PORT);
        return true;
    #else
        return false;
    #endif
}

void Z21LanInterface::update() {
    #if ENABLE_Z21_LAN
        int packet_size = udp.parsePacket();
        if (packet_size > 0) {
            uint8_t buffer[64];
            int len = udp.read(buffer, sizeof(buffer));
            if (len > 0) {
                handlePacket(buffer, (size_t)len, udp.remoteIP(), udp.remotePort());
            }
        }

        static unsigned long last_expiry_check_ms = 0;
        unsigned long now = millis();
        if (now - last_expiry_check_ms >= 10000) {
            last_expiry_check_ms = now;
            expireStaleClients();
        }
    #endif
}

// ============================================================================
// CLIENT SESSION TABLE
// ============================================================================

int Z21LanInterface::findOrAddClient(const IPAddress& ip, uint16_t port) {
    int free_slot = -1;
    for (int i = 0; i < MAX_Z21_CLIENTS; i++) {
        if (clients[i].active && clients[i].ip == ip && clients[i].port == port) {
            return i;
        }
        if (!clients[i].active && free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        DEBUG_PRINTF("Z21: client table full, dropping new client %s:%u\n",
                     ip.toString().c_str(), (unsigned)port);
        return -1;
    }

    clients[free_slot].active = true;
    clients[free_slot].ip = ip;
    clients[free_slot].port = port;
    // Default to driving/switching broadcasts already enabled (spec's
    // LAN_SET_BROADCASTFLAGS bit 0x00000001). Real-hardware testing
    // 2026-08-28 confirmed a physical WLANmaus never sends
    // LAN_SET_BROADCASTFLAGS at all (across a fresh connect and a full
    // WLANmaus reboot/reconnect) despite clearly expecting broadcasts to
    // work - it apparently treats "subscribed via LAN_X_GET_LOCO_INFO" as
    // sufficient opt-in on its own. Defaulting the flag on means such
    // clients still receive updates; a client that explicitly calls
    // LAN_SET_BROADCASTFLAGS (including to disable) still overrides this.
    clients[free_slot].broadcast_flags = 0x00000001;
    clients[free_slot].subscribed_count = 0;
    DEBUG_PRINTF("Z21: new client %s:%u (slot %d)\n", ip.toString().c_str(), (unsigned)port, free_slot);
    return free_slot;
}

void Z21LanInterface::subscribeClientToLoco(int client_index, uint16_t address) {
    if (client_index < 0 || client_index >= MAX_Z21_CLIENTS) return;
    Z21Client& client = clients[client_index];

    if (isClientSubscribed(client_index, address)) {
        return;
    }

    if (client.subscribed_count >= MAX_Z21_SUBSCRIBED_PER_CLIENT) {
        // FIFO eviction, per spec's own wording ("max 16 ... FIFO").
        for (uint8_t i = 1; i < client.subscribed_count; i++) {
            client.subscribed_addresses[i - 1] = client.subscribed_addresses[i];
        }
        client.subscribed_count--;
    }

    client.subscribed_addresses[client.subscribed_count++] = address;
}

bool Z21LanInterface::isClientSubscribed(int client_index, uint16_t address) const {
    if (client_index < 0 || client_index >= MAX_Z21_CLIENTS) return false;
    const Z21Client& client = clients[client_index];
    for (uint8_t i = 0; i < client.subscribed_count; i++) {
        if (client.subscribed_addresses[i] == address) return true;
    }
    return false;
}

void Z21LanInterface::expireStaleClients() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_Z21_CLIENTS; i++) {
        if (clients[i].active && (now - clients[i].last_seen_ms > Z21_CLIENT_TIMEOUT_MS)) {
            DEBUG_PRINTF("Z21: client %s:%u timed out\n",
                         clients[i].ip.toString().c_str(), (unsigned)clients[i].port);
            clients[i].active = false;
        }
    }
}

uint8_t Z21LanInterface::getActiveClientCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_Z21_CLIENTS; i++) {
        if (clients[i].active) count++;
    }
    return count;
}

ComponentStatus Z21LanInterface::getStatus() const {
    if (current_status == ComponentStatus::ERROR) {
        return ComponentStatus::ERROR;  // Genuine UDP bind failure - always surface this
    }
    return (getActiveClientCount() > 0) ? ComponentStatus::CONNECTED : ComponentStatus::DISCONNECTED;
}

unsigned long Z21LanInterface::getLastMessageAgeMs() const {
    if (!has_last_action) {
        return NO_TIMESTAMP;
    }
    return millis() - last_action_time;
}

void Z21LanInterface::getLastMessageSourceIp(char* buf, size_t buf_size) const {
    if (buf == nullptr || buf_size == 0) {
        return;
    }
    if (!has_last_action) {
        buf[0] = '\0';
        return;
    }
    String ip_str = last_action_ip.toString();
    strncpy(buf, ip_str.c_str(), buf_size - 1);
    buf[buf_size - 1] = '\0';
}

void Z21LanInterface::recordAction(int client_index) {
    if (client_index < 0 || client_index >= MAX_Z21_CLIENTS) {
        return;
    }
    last_action_ip = clients[client_index].ip;
    last_action_time = millis();
    has_last_action = true;
}

// ============================================================================
// INCOMING PACKET HANDLING
// ============================================================================

void Z21LanInterface::handlePacket(const uint8_t* buffer, size_t len, const IPAddress& remote_ip, uint16_t remote_port) {
    int client_index = findOrAddClient(remote_ip, remote_port);
    if (client_index >= 0) {
        clients[client_index].last_seen_ms = millis();
    }

    // A single UDP packet can carry several combined Z21 datasets back to
    // back (spec section 1.3) - loop until the buffer is exhausted.
    size_t offset = 0;
    while (offset + 4 <= len) {
        uint16_t data_len = (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
        if (data_len < 4 || offset + data_len > len) {
            break;  // malformed/truncated - stop, don't misinterpret trailing bytes
        }
        uint16_t header = (uint16_t)buffer[offset + 2] | ((uint16_t)buffer[offset + 3] << 8);
        const uint8_t* data = buffer + offset + 4;
        size_t data_bytes = data_len - 4;

        handleDataset(header, data, data_bytes, client_index);

        offset += data_len;
    }
}

void Z21LanInterface::handleDataset(uint16_t header, const uint8_t* data, size_t data_len, int client_index) {
    uint8_t reply[16];
    size_t reply_len;

    if (header == Z21_HEADER_GET_SERIAL_NUMBER) {
        reply_len = z21BuildSerialNumberReply(reply, sizeof(reply));
        sendToClient(client_index, reply, reply_len);
        return;
    }

    if (header == Z21_HEADER_LOGOFF) {
        if (client_index >= 0) {
            clients[client_index].active = false;
        }
        return;
    }

    if (header == Z21_HEADER_SET_BROADCASTFLAGS) {
        if (client_index >= 0 && data_len >= 4) {
            uint32_t flags = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                             ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
            clients[client_index].broadcast_flags = flags;
            DEBUG_PRINTF("Z21: client slot %d broadcast flags = 0x%08lX\n", client_index, (unsigned long)flags);
        }
        return;
    }

    if (header != Z21_HEADER_LAN_X || data_len == 0) {
        return;  // not an X-Bus tunneled command, or empty - nothing more to do
    }

    uint8_t x_header = data[0];

    if (x_header == Z21_X_GET_LOCO_INFO && data_len >= 4 && data[1] == 0xF0) {
        // data[0]=X-Header(0xE3), data[1]=DB0(0xF0), data[2]=DB1=Adr_MSB,
        // data[3]=DB2=Adr_LSB, data[4]=DB3=XOR. Real bug found live
        // 2026-08-27: this used data[1]/data[2] (DB0/Adr_MSB) instead of
        // data[2]/data[3] (Adr_MSB/Adr_LSB), decoding a garbage address
        // for every subscription request - the client would then never
        // receive any LAN_X_LOCO_INFO update for the loco it actually
        // asked about, matching the live symptom (WLANmaus sends commands
        // fine, but its own display never reflects the resulting state).
        uint16_t address = z21DecodeAddress(data[2], data[3]);
        DEBUG_PRINTF("Z21 RX: GetLocoInfo Addr=%u (subscribing client %d)\n", address, client_index);
        if (client_index >= 0) {
            subscribeClientToLoco(client_index, address);
        }
        LocoState loco;
        bool known = router && router->getStateEngine().getLoco(address, loco);
        reply_len = z21BuildLocoInfo(reply, sizeof(reply), address,
                                     known ? loco.direction : 1,
                                     known ? loco.speed : 0,
                                     known ? loco.functions : 0);
        sendToClient(client_index, reply, reply_len);
        return;
    }

    if (x_header == Z21_X_SET_LOCO_DRIVE && data_len >= 5 && (data[1] & 0xF0) == 0x10) {
        // DB0=0x1S (upper nibble 0x1 distinguishes this from
        // SET_LOCO_FUNCTION below, which shares this same X-header 0xE4
        // but has DB0=0xF8), DB1=Adr_MSB, DB2=Adr_LSB, DB3=RVVVVVVV, DB4=XOR
        uint8_t step_mode = data[1] & 0x03;
        uint16_t address = z21DecodeAddress(data[2], data[3]);
        uint8_t direction, speed;
        if (z21DecodeSpeed(step_mode, data[4], direction, speed) && router) {
            DEBUG_PRINTF("Z21 RX: Drive Addr=%u Speed=%u Dir=%u\n", address, speed, direction);
            recordAction(client_index);
            router->handleZ21Command(address, speed, direction);
            broadcastConfirmedState(address);
        }
        return;
    }

    if (x_header == Z21_X_SET_LOCO_FUNCTION && data_len >= 5 && data[1] == 0xF8) {
        // DB0=0xF8, DB1=Adr_MSB, DB2=Adr_LSB, DB3=TTNNNNNN, DB4=XOR
        uint16_t address = z21DecodeAddress(data[2], data[3]);
        uint8_t function_index, switch_type;
        if (z21DecodeFunctionCommand(data[4], function_index, switch_type) && router && function_index <= 31) {
            LocoState loco;
            uint32_t functions = 0;
            if (router->getStateEngine().getLoco(address, loco)) {
                functions = loco.functions;
            }
            DEBUG_PRINTF("Z21 RX: Function raw DB3=0x%02X TT=%u F%u current-bridge-state=%d\n",
                         data[4], switch_type, function_index, (functions & (1UL << function_index)) ? 1 : 0);
            uint32_t bit = (1UL << function_index);
            bool new_state;
            if (switch_type == Z21_FUNCTION_SWITCH_ON) {
                new_state = true;
            } else if (switch_type == Z21_FUNCTION_SWITCH_OFF) {
                new_state = false;
            } else {  // TOGGLE
                new_state = !(functions & bit);
            }
            functions = new_state ? (functions | bit) : (functions & ~bit);
            DEBUG_PRINTF("Z21 RX: Function Addr=%u F%u=%d\n", address, function_index, new_state ? 1 : 0);
            recordAction(client_index);
            router->handleZ21FunctionCommand(address, functions);
            broadcastConfirmedState(address);
        }
        return;
    }

    if (x_header == Z21_X_SET_STOP) {
        DEBUG_PRINTF("Z21 RX: Emergency stop\n");
        recordAction(client_index);
        if (router) router->emergencyStopAll(LocoSource::Z21_LAN);
        return;
    }

    if (x_header == Z21_X_HEADER_SYSTEM && data_len >= 2 && data[1] == Z21_X_DB0_GET_STATUS) {
        reply_len = z21BuildStatusChangedReply(reply, sizeof(reply), emergency_stop_active);
        sendToClient(client_index, reply, reply_len);
        return;
    }

    if (x_header == Z21_X_HEADER_SYSTEM && data_len >= 2 && data[1] == Z21_X_DB0_TRACK_POWER_OFF) {
        DEBUG_PRINTF("Z21 RX: Track power OFF\n");
        recordAction(client_index);
        if (router) router->emergencyStopAll(LocoSource::Z21_LAN);
        return;
    }

    if (x_header == Z21_X_HEADER_SYSTEM && data_len >= 2 && data[1] == Z21_X_DB0_TRACK_POWER_ON) {
        DEBUG_PRINTF("Z21 RX: Track power ON\n");
        recordAction(client_index);
        if (router) router->resumeOperation(LocoSource::Z21_LAN);
        return;
    }

    if (x_header == Z21_X_GET_FIRMWARE_VERSION) {
        reply_len = z21BuildFirmwareVersionReply(reply, sizeof(reply));
        sendToClient(client_index, reply, reply_len);
        return;
    }

    // Unrecognized X-Bus command - let the client know it was received but
    // not understood, rather than silently dropping it.
    reply_len = z21BuildUnknownCommandReply(reply, sizeof(reply));
    sendToClient(client_index, reply, reply_len);
}

void Z21LanInterface::sendToClient(int client_index, const uint8_t* buffer, size_t len) {
    if (client_index < 0 || client_index >= MAX_Z21_CLIENTS || len == 0) return;
    Z21Client& client = clients[client_index];
    if (!client.active) return;

    udp.beginPacket(client.ip, client.port);
    udp.write(buffer, len);
    udp.endPacket();
}

// ============================================================================
// OUTGOING (Ecos-sourced updates -> Z21 clients)
// ============================================================================

void Z21LanInterface::broadcastConfirmedState(uint16_t address) {
    // Real Z21 hardware confirms a SET_LOCO_DRIVE/SET_LOCO_FUNCTION request
    // by broadcasting LAN_X_LOCO_INFO to subscribed clients immediately,
    // synchronously with processing the request - it has no DCC ACK to wait
    // for. This bridge instead normally waits for Ecos's own echo to come
    // back through CommandRouter::broadcastCommand()'s ECOS branch - but
    // that echo is deliberately suppressed by the router's echo-prevention
    // logic for the very protocol that originated the request (the same
    // mechanism that already relies on XpressNet throttles optimistically
    // updating their own display locally). WLANmaus does NOT do that for
    // functions (confirmed live 2026-08-28: speed/direction appeared to
    // work only because of its own local slider echo, but function button
    // state silently never updated) - so mirror real Z21 hardware and
    // confirm the request ourselves immediately, independent of the Ecos
    // round trip.
    if (!router) return;
    LocoState loco;
    if (router->getStateEngine().getLoco(address, loco)) {
        broadcastLocoInfo(address, loco.direction, loco.speed, loco.functions);
    }
}

void Z21LanInterface::broadcastLocoInfo(uint16_t address, uint8_t direction, uint8_t speed, uint32_t functions) {
    uint8_t packet[16];
    size_t packet_len = z21BuildLocoInfo(packet, sizeof(packet), address, direction, speed, functions);
    DEBUG_PRINTF("Z21 TX: LocoInfo Addr=%u Dir=%u Speed=%u Fn=0x%08lX len=%u\n",
                 address, direction, speed, (unsigned long)functions, (unsigned)packet_len);
    if (packet_len == 0) return;
    DEBUG_PRINTF("Z21 TX: bytes = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                 packet[0], packet[1], packet[2], packet[3], packet[4], packet[5], packet[6],
                 packet[7], packet[8], packet[9], packet[10], packet[11], packet[12], packet[13]);

    int sent_count = 0;
    for (int i = 0; i < MAX_Z21_CLIENTS; i++) {
        if (!clients[i].active) continue;
        if (!(clients[i].broadcast_flags & 0x00000001)) {
            DEBUG_PRINTF("Z21 TX: client %d active but broadcast flags 0x%08lX don't include 0x1 - skipping\n",
                         i, (unsigned long)clients[i].broadcast_flags);
            continue;
        }
        if (!isClientSubscribed(i, address)) {
            DEBUG_PRINTF("Z21 TX: client %d not subscribed to addr %u - skipping\n", i, address);
            continue;
        }
        sendToClient(i, packet, packet_len);
        sent_count++;
    }
    DEBUG_PRINTF("Z21 TX: sent to %d client(s)\n", sent_count);
}

void Z21LanInterface::sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) {
    // A LAN_X_LOCO_INFO packet always reports the loco's full state -
    // fill in the current functions from StateEngine, same pattern
    // XpressNetInterface::resolveLocoStateForReply() already uses.
    uint32_t functions = 0;
    LocoState loco;
    if (router && router->getStateEngine().getLoco(address, loco)) {
        functions = loco.functions;
    }
    broadcastLocoInfo(address, direction, speed, functions);
}

void Z21LanInterface::sendFunctionCommand(uint16_t address, uint32_t functions) {
    uint8_t speed = 0, direction = 1;
    LocoState loco;
    if (router && router->getStateEngine().getLoco(address, loco)) {
        speed = loco.speed;
        direction = loco.direction;
    }
    broadcastLocoInfo(address, direction, speed, functions);
}

void Z21LanInterface::sendEmergencyStop() {
    emergency_stop_active = true;
    uint8_t packet[16];
    size_t packet_len = z21BuildStoppedBroadcast(packet, sizeof(packet));
    for (int i = 0; i < MAX_Z21_CLIENTS; i++) {
        if (clients[i].active && (clients[i].broadcast_flags & 0x00000001)) {
            sendToClient(i, packet, packet_len);
        }
    }
}

void Z21LanInterface::sendResumeOperation() {
    emergency_stop_active = false;
    uint8_t packet[16];
    size_t packet_len = z21BuildTrackPowerBroadcast(packet, sizeof(packet), true);
    for (int i = 0; i < MAX_Z21_CLIENTS; i++) {
        if (clients[i].active && (clients[i].broadcast_flags & 0x00000001)) {
            sendToClient(i, packet, packet_len);
        }
    }
}
