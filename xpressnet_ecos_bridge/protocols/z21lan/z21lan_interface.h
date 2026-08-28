/*
 * Z21 LAN Interface - UDP Command Station Emulation for WLANmaus
 *
 * Phase 6 step 4. Arduino-only (WiFiUDP) - the actual packet encode/
 * decode logic lives in z21_protocol.h/.cpp instead, which is
 * native-testable. Confirmed against the official Z21 LAN Protocol
 * Specification v1.13 (docs/z21-lan-protokoll-en.pdf, gitignored).
 *
 * Unlike XpressNet (a shared RS485 bus where multiple physical throttles
 * already see each other's traffic for free), Z21 is UDP client-server -
 * this interface tracks each connected client (WLANmaus, phone app, etc.)
 * itself and broadcasts state to all of them individually. CommandRouter
 * still only ever sees "one Z21 interface", same shape as xpressnet/ecos.
 *
 * v1 scope (agreed with the user before implementation): loco
 * speed/direction/function control, track power on/off, emergency stop -
 * all mapped onto this project's existing CommandRouter mechanisms.
 * Deliberately deferred: Z21 turnout/accessory commands (XpressNet->Ecos
 * accessory support already exists, a second input path is separate
 * work), CV programming (out of scope project-wide).
 */

#ifndef Z21LAN_INTERFACE_H
#define Z21LAN_INTERFACE_H

#include <cstdint>
#include <WiFiUdp.h>
#include <IPAddress.h>
#include "../../interfaces/interface_base.h"
#include "../../config.h"

// Forward declaration
class CommandRouter;

class Z21LanInterface : public ProtocolInterface {
public:
    Z21LanInterface();

    bool begin() override;
    void update() override;

    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override;
    void sendFunctionCommand(uint16_t address, uint32_t functions) override;
    void sendEmergencyStop() override;
    void sendResumeOperation() override;

    ComponentStatus getStatus() const override { return current_status; }
    const char* getName() const override { return "Z21 LAN"; }

    /**
     * Set reference to command router (called after construction) -
     * same pattern as every other protocol interface.
     */
    void setCommandRouter(CommandRouter* router) { this->router = router; }

    /**
     * Number of currently-active client sessions, for OLED display.
     */
    uint8_t getActiveClientCount() const;

private:
    WiFiUDP udp;
    CommandRouter* router;
    ComponentStatus current_status;
    bool emergency_stop_active;

    struct Z21Client {
        bool active = false;
        IPAddress ip;
        uint16_t port = 0;
        uint32_t broadcast_flags = 0;
        unsigned long last_seen_ms = 0;
        uint16_t subscribed_addresses[MAX_Z21_SUBSCRIBED_PER_CLIENT] = {0};
        uint8_t subscribed_count = 0;
    };

    Z21Client clients[MAX_Z21_CLIENTS];

    /**
     * Find an existing client session by IP+port, or create one in the
     * first free/expired slot if not found (this is the implicit "login"
     * the spec describes - any command from a new IP+port registers it).
     * @return client index, or -1 if the table is full
     */
    int findOrAddClient(const IPAddress& ip, uint16_t port);

    /**
     * Subscribe a client to broadcast updates for a loco address -
     * upserts by address (FIFO eviction of the oldest if the client's
     * subscription list is already full, per spec's own FIFO wording).
     */
    void subscribeClientToLoco(int client_index, uint16_t address);

    bool isClientSubscribed(int client_index, uint16_t address) const;

    /**
     * Drop any client not heard from in Z21_CLIENT_TIMEOUT_MS - spec:
     * "Each client is expected to communicate with the Z21 once per
     * minute, otherwise it will be removed from the list."
     */
    void expireStaleClients();

    /**
     * Parse one UDP payload, which may contain multiple combined Z21
     * datasets back-to-back (spec section 1.3) - loops until the buffer
     * is exhausted.
     */
    void handlePacket(const uint8_t* buffer, size_t len, const IPAddress& remote_ip, uint16_t remote_port);

    /**
     * Handle a single decoded dataset (header + data, DataLen framing
     * already stripped).
     */
    void handleDataset(uint16_t header, const uint8_t* data, size_t data_len,
                       int client_index);

    void sendToClient(int client_index, const uint8_t* buffer, size_t len);

    /**
     * Build and send a LAN_X_LOCO_INFO packet for `address` to every
     * client subscribed to it (and with broadcast flag 0x00000001 set).
     */
    void broadcastLocoInfo(uint16_t address, uint8_t direction, uint8_t speed, uint32_t functions);

    /**
     * Immediately confirms a just-processed SET_LOCO_DRIVE/SET_LOCO_FUNCTION
     * request by broadcasting the loco's current StateEngine state, rather
     * than waiting on Ecos's echo (which the router's echo-prevention
     * logic deliberately suppresses for the originating protocol).
     */
    void broadcastConfirmedState(uint16_t address);
};

#endif  // Z21LAN_INTERFACE_H
