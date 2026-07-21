/*
 * LocoNet Interface - Stub for Future Implementation
 * 
 * This is a skeleton for future LocoNet support.
 * Enables compilation even when ENABLE_LOCONET is set.
 */

#ifndef LOCONET_INTERFACE_H
#define LOCONET_INTERFACE_H

#include <cstdint>
#include "../../interfaces/interface_base.h"

class LocoNetInterface : public ProtocolInterface {
public:
    LocoNetInterface() {}
    
    bool begin() override {
        // TODO: Future - Initialize LocoNet hardware
        return false;
    }
    
    void update() override {
        // TODO: Future - Non-blocking LocoNet communication
    }
    
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override {
        // TODO: Future - Send speed/direction via LocoNet
        (void)address; (void)speed; (void)direction;
    }
    
    void sendFunctionCommand(uint16_t address, uint32_t functions) override {
        // TODO: Future - Send functions via LocoNet
        (void)address; (void)functions;
    }
    
    ComponentStatus getStatus() const override {
        return ComponentStatus::DISCONNECTED;
    }
    
    const char* getName() const override {
        return "LocoNet";
    }
};

#endif  // LOCONET_INTERFACE_H
