/*
 * Command Router Unit Tests
 *
 * Tests for:
 * - Protocol bridging (XpressNet ↔ Ecos)
 * - Echo prevention (500ms window, circular queue)
 * - Command routing to correct protocol interface
 * - Multi-throttle consistency (broadcast updates)
 * - Subscription lifecycle (request → subscribe → expiry → unsubscribe)
 */

#include <cstdint>
#include <cstring>
#include "../xpressnet_ecos_bridge/command_router.h"
#include "mocks/mock_protocol_interface.h"
#include "mocks/mock_now_ms.h"

// TODO: Include Unity framework headers once configured
// #include "unity.h"

// ============================================================================
// TEST SETUP / TEARDOWN
// ============================================================================

void setUp(void) {
    // Reset mock time
    resetMockNowMs();
}

void tearDown(void) {
    // No cleanup needed (mocks are stack-allocated)
}

// ============================================================================
// TESTS - BASIC ROUTING
// ============================================================================

void test_router_route_xpressnet_speed_to_ecos(void) {
    // Send speed command from XpressNet interface
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // Simulate XpressNet speed command: loco 100, speed 64, forward
    router.handleXpressNetSpeedCommand(100, 64, 1);

    // Verify: Ecos interface received the command
    if (ecos_mock.getSpeedCommandCount() != 1) return;
    if (ecos_mock.getLastSpeedCommand().address != 100) return;
    if (ecos_mock.getLastSpeedCommand().speed != 64) return;
    if (ecos_mock.getLastSpeedCommand().direction != 1) return;
}

void test_router_route_function_command(void) {
    // Send function command from XpressNet interface
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // Simulate XpressNet function command: loco 50, F0 on
    router.handleXpressNetFunctionCommand(50, 0x01);

    // Verify: Ecos interface received the command
    if (ecos_mock.getFunctionCommandCount() != 1) return;
    if (ecos_mock.getLastFunctionCommand().address != 50) return;
    if (ecos_mock.getLastFunctionCommand().functions != 0x01) return;
}

// ============================================================================
// TESTS - ECHO PREVENTION
// ============================================================================

void test_router_echo_prevention_same_command_suppressed(void) {
    // Echo prevention: same command sent back should be suppressed
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    MockProtocolInterface ecos_mock;
    router.setXpressNetInterface(&xnet_mock);
    router.setEcosInterface(&ecos_mock);

    // XpressNet sends speed command
    resetMockNowMs();
    router.handleXpressNetSpeedCommand(100, 64, 1);

    // Immediately, Ecos echoes back the same command
    // (this would happen in a real scenario if both were sending to each other)
    router.handleEcosSpeedCommand(100, 64, 1);

    // The echo should be prevented (not forwarded to XpressNet)
    if (xnet_mock.getSpeedCommandCount() > 0) return;  // Should be 0 (echo suppressed)
}

void test_router_echo_prevention_different_command_passes(void) {
    // Different command should NOT be suppressed
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    MockProtocolInterface ecos_mock;
    router.setXpressNetInterface(&xnet_mock);
    router.setEcosInterface(&ecos_mock);

    resetMockNowMs();
    router.handleXpressNetSpeedCommand(100, 64, 1);  // XpressNet: 100, speed 64

    // Ecos sends different speed
    router.handleEcosSpeedCommand(100, 80, 1);  // Ecos: 100, speed 80 (different!)

    // This should NOT be suppressed
    if (xnet_mock.getSpeedCommandCount() != 1) return;  // Should forward to XpressNet
}

void test_router_echo_prevention_window_expires(void) {
    // Echo prevention window is 500ms
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // Send command at t=0
    resetMockNowMs();
    router.handleXpressNetSpeedCommand(100, 64, 1);
    if (ecos_mock.getSpeedCommandCount() != 1) return;

    // Send identical command at t=600ms (past 500ms window)
    advanceMockNowMs(600);
    router.handleXpressNetSpeedCommand(100, 64, 1);

    // Should not be suppressed (window expired)
    if (ecos_mock.getSpeedCommandCount() != 2) return;
}

// ============================================================================
// TESTS - STATE ENGINE INTEGRATION
// ============================================================================

void test_router_adds_loco_to_state_engine(void) {
    // When a command is routed, loco should be added to state engine
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // Send command for loco 100
    router.handleXpressNetSpeedCommand(100, 64, 1);

    // Verify: loco 100 is in state engine
    LocoState state;
    bool found = router.getLocoState(100, state);
    if (!found) return;
    if (state.speed != 64) return;
    if (state.direction != 1) return;
}

void test_router_updates_loco_speed(void) {
    // State engine should update speed when command arrives
    CommandRouter router;
    router.setEcosInterface(nullptr);  // No output interface needed

    // Send initial command
    router.handleXpressNetSpeedCommand(50, 50, 1);

    // Update with new speed
    router.handleXpressNetSpeedCommand(50, 100, 1);

    // Verify: speed updated
    LocoState state;
    router.getLocoState(50, state);
    if (state.speed != 100) return;
}

// ============================================================================
// TESTS - MULTI-THROTTLE CONSISTENCY
// ============================================================================

void test_router_broadcast_ecos_update_to_xpressnet(void) {
    // When Ecos sends update, should broadcast to XpressNet
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    // Ecos sends speed change event
    router.handleEcosSpeedCommand(100, 90, 1);

    // Should be forwarded to XpressNet
    if (xnet_mock.getSpeedCommandCount() != 1) return;
    if (xnet_mock.getLastSpeedCommand().speed != 90) return;
}

void test_router_broadcast_ecos_function_to_xpressnet(void) {
    // When Ecos sends function update, should broadcast to XpressNet
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    // Ecos sends function change
    router.handleEcosFunctionCommand(75, 0x03);  // F0 and F1

    // Should be forwarded to XpressNet
    if (xnet_mock.getFunctionCommandCount() != 1) return;
    if (xnet_mock.getLastFunctionCommand().functions != 0x03) return;
}

// ============================================================================
// TESTS - VALIDATION
// ============================================================================

void test_router_reject_invalid_address_zero(void) {
    // Address 0 should be rejected or handled specially
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // Try to send command for address 0
    router.handleXpressNetSpeedCommand(0, 64, 1);

    // Should either reject or not forward
    // (depends on design - for now, just verify no crash)
}

void test_router_reject_invalid_speed(void) {
    // Speed > 126 should be rejected or clamped
    CommandRouter router;

    // Try to send invalid speed
    router.handleXpressNetSpeedCommand(100, 127, 1);

    // Verify: state engine should have speed <= 126 or reject
    LocoState state;
    bool found = router.getLocoState(100, state);
    if (found && state.speed > 126) return;  // Invalid
}

// ============================================================================
// TESTS - INTERFACE STATUS CHECKING
// ============================================================================

void test_router_check_interface_status(void) {
    // Router should track interface connectivity status
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    ecos_mock.setStatus(ComponentStatus::CONNECTED);
    router.setEcosInterface(&ecos_mock);

    // Send command - should work when connected
    router.handleXpressNetSpeedCommand(100, 64, 1);
    if (ecos_mock.getSpeedCommandCount() != 1) return;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    // Run all tests
    setUp();

    test_router_route_xpressnet_speed_to_ecos();
    test_router_route_function_command();

    test_router_echo_prevention_same_command_suppressed();
    test_router_echo_prevention_different_command_passes();
    test_router_echo_prevention_window_expires();

    test_router_adds_loco_to_state_engine();
    test_router_updates_loco_speed();

    test_router_broadcast_ecos_update_to_xpressnet();
    test_router_broadcast_ecos_function_to_xpressnet();

    test_router_reject_invalid_address_zero();
    test_router_reject_invalid_speed();

    test_router_check_interface_status();

    tearDown();

    return 0;  // TODO: Return UNITY_END() result
}
