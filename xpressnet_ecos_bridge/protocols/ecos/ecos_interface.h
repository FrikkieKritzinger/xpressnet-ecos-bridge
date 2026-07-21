/*
 * Ecos LAN Interface - Stub for Phase 3 Implementation
 * 
 * This is a skeleton that will be fully implemented in Phase 3.
 * For now, it provides the minimal interface to allow compilation.
 */

#ifndef ECOS_INTERFACE_H
#define ECOS_INTERFACE_H

#include "../interfaces/interface_base.h"

class EcosInterface : public ProtocolInterface {
public:
    EcosInterface() {}
    
    bool begin() override {
        // TODO: Phase 3 - Initialize WiFi and TCP connection to Ecos
        return false;
    }
    
    void update() override {
        // TODO: Phase 3 - Non-blocking TCP read from Ecos, parse XML
    }
    
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override {
        // TODO: Phase 3 - Send XML speed/direction command to Ecos
        (void)address; (void)speed; (void)direction;
    }
    
    void sendFunctionCommand(uint16_t address, uint32_t functions) override {
        // TODO: Phase 3 - Send XML function states to Ecos
        (void)address; (void)functions;
    }
    
    ComponentStatus getStatus() const override {
        // TODO: Phase 3 - Return actual status
        return ComponentStatus::DISCONNECTED;
    }
    
    const char* getName() const override {
        return "Ecos";
    }
    
    /**
     * Subscribe to locomotive on Ecos
     * Called when XpressNet sends command for unknown loco
     * TODO: Phase 3 - Send subscription XML to Ecos
     */
    void subscribeToLoco(uint16_t address) {
        (void)address;
    }
};

#endif  // ECOS_INTERFACE_H
