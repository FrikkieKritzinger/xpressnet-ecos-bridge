/*
 * Z21 LAN Interface - Stub for Future Implementation
 * 
 * This is a skeleton for future Z21 LAN support.
 * Enables compilation even when ENABLE_Z21_LAN is set.
 */

#ifndef Z21LAN_INTERFACE_H
#define Z21LAN_INTERFACE_H

#include "../interfaces/interface_base.h"

class Z21LanInterface : public ProtocolInterface {
public:
    Z21LanInterface() {}
    
    bool begin() override {
        // TODO: Future - Initialize Z21 LAN interface
        return false;
    }
    
    void update() override {
        // TODO: Future - Non-blocking Z21 LAN communication
    }
    
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override {
        // TODO: Future - Send speed/direction via Z21 protocol
        (void)address; (void)speed; (void)direction;
    }
    
    void sendFunctionCommand(uint16_t address, uint32_t functions) override {
        // TODO: Future - Send functions via Z21 protocol
        (void)address; (void)functions;
    }
    
    ComponentStatus getStatus() const override {
        return ComponentStatus::DISCONNECTED;
    }
    
    const char* getName() const override {
        return "Z21 LAN";
    }
};

#endif  // Z21LAN_INTERFACE_H
