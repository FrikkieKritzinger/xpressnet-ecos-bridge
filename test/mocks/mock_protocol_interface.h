/*
 * Mock Protocol Interface - Testing support
 *
 * Implements ProtocolInterface for testing command routing and echo prevention.
 * Tracks all method calls for verification in unit tests.
 */

#ifndef MOCK_PROTOCOL_INTERFACE_H
#define MOCK_PROTOCOL_INTERFACE_H

#include <cstdint>
#include "../../xpressnet_ecos_bridge/interfaces/interface_base.h"

class MockProtocolInterface : public ProtocolInterface {
public:
    MockProtocolInterface() : current_status(ComponentStatus::DISCONNECTED) {
        reset();
    }

    bool begin() override {
        begin_called = true;
        current_status = ComponentStatus::CONNECTED;
        return true;
    }

    void update() override {
        update_call_count++;
    }

    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override {
        last_speed_command.address = address;
        last_speed_command.speed = speed;
        last_speed_command.direction = direction;
        last_speed_command.timestamp_ms = 0;  // Would use mock_now_ms if available
        speed_command_count++;
    }

    void sendFunctionCommand(uint16_t address, uint32_t functions) override {
        last_function_command.address = address;
        last_function_command.functions = functions;
        last_function_command.timestamp_ms = 0;
        function_command_count++;
    }

    ComponentStatus getStatus() const override {
        return current_status;
    }

    const char* getName() const override {
        return "MockProtocol";
    }

    void subscribeToLoco(uint16_t address) override {
        last_subscribed_address = address;
        subscribe_call_count++;
    }

    void unsubscribeFromLoco(uint16_t address) override {
        last_unsubscribed_address = address;
        unsubscribe_call_count++;
    }

    void sendEmergencyStop() override {
        emergency_stop_call_count++;
    }

    void sendResumeOperation() override {
        resume_operation_call_count++;
    }

    // ========================================================================
    // TESTING INTERFACE - Verification and control
    // ========================================================================

    struct SpeedCommand {
        uint16_t address;
        uint8_t speed;
        uint8_t direction;
        unsigned long timestamp_ms;
    };

    struct FunctionCommand {
        uint16_t address;
        uint32_t functions;
        unsigned long timestamp_ms;
    };

    // Query call history
    bool wasBeginCalled() const { return begin_called; }
    int getUpdateCallCount() const { return update_call_count; }
    int getSpeedCommandCount() const { return speed_command_count; }
    int getFunctionCommandCount() const { return function_command_count; }
    int getSubscribeCallCount() const { return subscribe_call_count; }
    int getUnsubscribeCallCount() const { return unsubscribe_call_count; }
    int getEmergencyStopCallCount() const { return emergency_stop_call_count; }
    int getResumeOperationCallCount() const { return resume_operation_call_count; }
    uint16_t getLastSubscribedAddress() const { return last_subscribed_address; }
    uint16_t getLastUnsubscribedAddress() const { return last_unsubscribed_address; }

    // Get last commands sent
    const SpeedCommand& getLastSpeedCommand() const { return last_speed_command; }
    const FunctionCommand& getLastFunctionCommand() const { return last_function_command; }

    // Manual status control for testing
    void setStatus(ComponentStatus status) { current_status = status; }

    // Reset all tracking
    void reset() {
        begin_called = false;
        update_call_count = 0;
        speed_command_count = 0;
        function_command_count = 0;
        subscribe_call_count = 0;
        unsubscribe_call_count = 0;
        emergency_stop_call_count = 0;
        resume_operation_call_count = 0;
        last_subscribed_address = 0;
        last_unsubscribed_address = 0;
        last_speed_command = {0, 0, 0, 0};
        last_function_command = {0, 0, 0};
    }

private:
    // State
    ComponentStatus current_status;

    // Call tracking
    bool begin_called;
    int update_call_count;
    int speed_command_count;
    int function_command_count;
    int subscribe_call_count;
    int unsubscribe_call_count;
    int emergency_stop_call_count;
    int resume_operation_call_count;
    uint16_t last_subscribed_address;
    uint16_t last_unsubscribed_address;

    // Command history
    SpeedCommand last_speed_command;
    FunctionCommand last_function_command;
};

#endif  // MOCK_PROTOCOL_INTERFACE_H
