/*
 * XpressNet Interface - Stub for Phase 3 Implementation
 * 
 * This is a skeleton that will be fully implemented in Phase 3.
 * For now, it provides the minimal interface to allow compilation.
 */

#ifndef XPRESSNET_INTERFACE_H
#define XPRESSNET_INTERFACE_H

#include "../interfaces/interface_base.h"

class XpressNetInterface : public ProtocolInterface {
public:
    XpressNetInterface() {}
    
    bool begin() override {
        // TODO: Phase 3 - Initialize XpressNet hardware
        return false;
    }
    
    void update() override {
        // TODO: Phase 3 - Non-blocking serial read from XpressNet
    }
    
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override {
        // TODO: Phase 3 - Send speed/direction to XpressNet bus
        (void)address; (void)speed; (void)direction;
    }
    
    void sendFunctionCommand(uint16_t address, uint32_t functions) override {
        // TODO: Phase 3 - Send function states to XpressNet bus
        (void)address; (void)functions;
    }
    
    ComponentStatus getStatus() const override {
        // TODO: Phase 3 - Return actual status
        return ComponentStatus::DISCONNECTED;
    }
    
    const char* getName() const override {
        return "XpressNet";
    }
};

#endif  // XPRESSNET_INTERFACE_H
