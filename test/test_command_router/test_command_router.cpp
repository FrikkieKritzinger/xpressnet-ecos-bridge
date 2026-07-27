/*
 * Command Router Unit Tests
 *
 * Tests for:
 * - Protocol bridging (XpressNet <-> Ecos)
 * - Echo prevention (500ms window, opposite-source-only)
 * - Command routing to correct protocol interface
 * - Multi-throttle consistency (broadcast updates)
 * - Subscription lifecycle (request -> subscribe -> expiry -> unsubscribe)
 * - Input validation (invalid address/speed rejected before state engine)
 *
 * Note: isEchoCommand() only compares (address, opposite source, time window) -
 * it does NOT compare command values. Two different-source commands for the
 * same loco within 500ms are suppressed regardless of whether speed/direction
 * differ; two same-source commands are never treated as echoes, regardless of
 * timing. Tests below reflect that actual behavior.
 */

#include <cstdint>
#include <cstring>
#include <unity.h>
#include "command_router.h"
#include "mocks/mock_protocol_interface.h"
#include "mocks/mock_now_ms.h"

// ============================================================================
// TEST SETUP / TEARDOWN
// ============================================================================

void setUp(void) {
    resetMockNowMs();
}

void tearDown(void) {
    // No cleanup needed (mocks are stack-allocated)
}

// ============================================================================
// TESTS - BASIC ROUTING
// ============================================================================

void test_router_route_xpressnet_speed_to_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // XpressNet speed command: loco 100, speed 64, forward
    router.handleXpressNetCommand(100, 64, 1);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getSpeedCommandCount());
    TEST_ASSERT_EQUAL_UINT16(100, ecos_mock.getLastSpeedCommand().address);
    TEST_ASSERT_EQUAL_UINT8(64, ecos_mock.getLastSpeedCommand().speed);
    TEST_ASSERT_EQUAL_UINT8(1, ecos_mock.getLastSpeedCommand().direction);
}

void test_router_route_function_command(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    // XpressNet function command: loco 50, F0 on
    router.handleXpressNetFunctionCommand(50, 0x01);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getFunctionCommandCount());
    TEST_ASSERT_EQUAL_UINT16(50, ecos_mock.getLastFunctionCommand().address);
    TEST_ASSERT_EQUAL_UINT32(0x01, ecos_mock.getLastFunctionCommand().functions);
}

// ============================================================================
// TESTS - ECHO PREVENTION
// ============================================================================

void test_router_echo_prevention_opposite_source_suppressed(void) {
    // Opposite-source update for the same loco within the 500ms window
    // is suppressed, regardless of whether the values match.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    MockProtocolInterface ecos_mock;
    router.setXpressNetInterface(&xnet_mock);
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 64, 1);   // t=0, from XPRESSNET
    router.handleEcosCommand(100, 64, 1);         // t=0, from ECOS (opposite source)

    TEST_ASSERT_EQUAL_INT(0, xnet_mock.getSpeedCommandCount());  // Should be suppressed
}

void test_router_same_source_repeat_is_not_echo(void) {
    // Two commands from the SAME source are never treated as an echo,
    // even back-to-back for the same loco.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 64, 1);
    router.handleXpressNetCommand(100, 80, 1);

    TEST_ASSERT_EQUAL_INT(2, ecos_mock.getSpeedCommandCount());  // Both should broadcast
}

void test_router_echo_prevention_window_expires(void) {
    // Opposite-source update AFTER the 500ms window is not suppressed.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleXpressNetCommand(100, 64, 1);   // t=0, from XPRESSNET
    advanceMockNowMs(600);                        // past the 500ms window
    router.handleEcosCommand(100, 64, 1);         // t=600, from ECOS

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getSpeedCommandCount());  // Should pass through
}

// ============================================================================
// TESTS - STATE ENGINE INTEGRATION
// ============================================================================

void test_router_adds_loco_to_state_engine(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 64, 1);

    LocoState state;
    bool found = router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_UINT8(64, state.speed);
    TEST_ASSERT_EQUAL_UINT8(1, state.direction);
}

void test_router_updates_loco_speed(void) {
    CommandRouter router;

    router.handleXpressNetCommand(50, 50, 1);
    router.handleXpressNetCommand(50, 100, 1);

    LocoState state;
    router.getStateEngine().getLoco(50, state);
    TEST_ASSERT_EQUAL_UINT8(100, state.speed);
}

// ============================================================================
// TESTS - MULTI-THROTTLE CONSISTENCY
// ============================================================================

void test_router_broadcast_ecos_update_to_xpressnet(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleEcosCommand(100, 90, 1);

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getSpeedCommandCount());
    TEST_ASSERT_EQUAL_UINT8(90, xnet_mock.getLastSpeedCommand().speed);
}

void test_router_broadcast_ecos_function_to_xpressnet(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleEcosFunctionCommand(75, 0x03);  // F0 and F1

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getFunctionCommandCount());
    TEST_ASSERT_EQUAL_UINT32(0x03, xnet_mock.getLastFunctionCommand().functions);
}

// ============================================================================
// TESTS - SUBSCRIPTION LIFECYCLE
// ============================================================================

void test_router_new_loco_triggers_ecos_subscription(void) {
    // First XpressNet command for an unknown loco should request Ecos subscription
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 64, 1);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getSubscribeCallCount());
    TEST_ASSERT_EQUAL_UINT16(100, ecos_mock.getLastSubscribedAddress());
}

void test_router_expiry_triggers_ecos_unsubscribe(void) {
    // Loco expiring after 5 minutes of inactivity should unsubscribe from Ecos
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 64, 1);
    advanceMockNowMs(300000 + 1);  // 5 minutes + 1ms
    router.update();

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getUnsubscribeCallCount());
    TEST_ASSERT_EQUAL_UINT16(100, ecos_mock.getLastUnsubscribedAddress());
}

// ============================================================================
// TESTS - VALIDATION
// ============================================================================

void test_router_reject_invalid_address_zero(void) {
    // Address 0 fails isValidDccAddress() - command must be dropped entirely
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(0, 64, 1);

    TEST_ASSERT_TRUE(router.getStateEngine().findLoco(0) < 0);  // Must not be added
    TEST_ASSERT_EQUAL_INT(0, ecos_mock.getSpeedCommandCount());  // Must not broadcast
}

void test_router_reject_invalid_speed(void) {
    // Speed 127 fails isValidSpeed() (DCC_MAX_SPEED=126, 127 is E-stop) -
    // command must be dropped entirely, not clamped.
    CommandRouter router;

    router.handleXpressNetCommand(100, 127, 1);

    LocoState state;
    bool found = router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_FALSE(found);  // Should never have been added
}

// ============================================================================
// TESTS - INTERFACE STATUS
// ============================================================================

void test_router_surfaces_ecos_status_in_system_status(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    ecos_mock.setStatus(ComponentStatus::CONNECTED);
    router.setEcosInterface(&ecos_mock);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_INT((int)ComponentStatus::CONNECTED, (int)status.ecos_status);
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_router_route_xpressnet_speed_to_ecos);
    RUN_TEST(test_router_route_function_command);

    RUN_TEST(test_router_echo_prevention_opposite_source_suppressed);
    RUN_TEST(test_router_same_source_repeat_is_not_echo);
    RUN_TEST(test_router_echo_prevention_window_expires);

    RUN_TEST(test_router_adds_loco_to_state_engine);
    RUN_TEST(test_router_updates_loco_speed);

    RUN_TEST(test_router_broadcast_ecos_update_to_xpressnet);
    RUN_TEST(test_router_broadcast_ecos_function_to_xpressnet);

    RUN_TEST(test_router_new_loco_triggers_ecos_subscription);
    RUN_TEST(test_router_expiry_triggers_ecos_unsubscribe);

    RUN_TEST(test_router_reject_invalid_address_zero);
    RUN_TEST(test_router_reject_invalid_speed);

    RUN_TEST(test_router_surfaces_ecos_status_in_system_status);

    return UNITY_END();
}
