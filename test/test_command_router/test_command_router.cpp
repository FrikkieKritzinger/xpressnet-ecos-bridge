/*
 * Command Router Unit Tests
 *
 * Tests for:
 * - Protocol bridging (XpressNet <-> Ecos <-> Z21)
 * - Echo attribution (wasRecentSource(), ECOS_ECHO_ATTRIBUTION_WINDOW_MS
 *   window, per-destination - see command_router.h's doc comment)
 * - Command routing to correct protocol interface
 * - Multi-throttle consistency (broadcast updates)
 * - Subscription lifecycle (request -> subscribe -> expiry -> unsubscribe)
 * - Input validation (invalid address/speed rejected before state engine)
 *
 * Note: wasRecentSource() only compares (address, source, time window) - it
 * does NOT compare command values, and it governs OUTGOING forwarding from
 * broadcastCommand()'s ECOS branch only (skip re-echoing to whichever
 * protocol was the recent source). Incoming commands from XpressNet/Z21 are
 * no longer gated by any echo check at all (removed 2026-08-28 - see
 * command_router.cpp for why). handleEcosCommand()/handleEcosFunctionCommand()
 * also skip broadcasting when the reported value doesn't actually differ
 * from what's already known, regardless of timing.
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

void test_router_xpressnet_function_command_updates_existing_loco(void) {
    // handleXpressNetFunctionCommand's "existing loco" branch (as opposed
    // to the "brand new loco" branch exercised above)
    CommandRouter router;

    router.handleXpressNetCommand(50, 64, 1);            // creates loco 50
    router.handleXpressNetFunctionCommand(50, 0x03);      // updates it

    LocoState state;
    router.getStateEngine().getLoco(50, state);
    TEST_ASSERT_EQUAL_UINT32(0x03, state.functions);
    TEST_ASSERT_EQUAL_UINT8(64, state.speed);  // Speed preserved
}

// ============================================================================
// TESTS - VALIDATION (all four handlers)
// ============================================================================

void test_router_xpressnet_function_command_rejects_invalid_address(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetFunctionCommand(0, 0x01);

    TEST_ASSERT_EQUAL_INT(0, ecos_mock.getFunctionCommandCount());
    TEST_ASSERT_TRUE(router.getStateEngine().findLoco(0) < 0);
}

void test_router_ecos_command_rejects_invalid_address(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleEcosCommand(0, 64, 1);

    TEST_ASSERT_EQUAL_INT(0, xnet_mock.getSpeedCommandCount());
    TEST_ASSERT_TRUE(router.getStateEngine().findLoco(0) < 0);
}

void test_router_ecos_command_rejects_invalid_speed(void) {
    CommandRouter router;

    router.handleEcosCommand(100, 127, 1);  // 127 is E-stop, not a valid Ecos speed here

    LocoState state;
    TEST_ASSERT_FALSE(router.getStateEngine().getLoco(100, state));
}

void test_router_ecos_function_command_rejects_invalid_address(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleEcosFunctionCommand(0, 0x01, 0xFFFFFFFF);

    TEST_ASSERT_EQUAL_INT(0, xnet_mock.getFunctionCommandCount());
    TEST_ASSERT_TRUE(router.getStateEngine().findLoco(0) < 0);
}

void test_router_ecos_function_command_updates_existing_loco(void) {
    // handleEcosFunctionCommand's "existing loco" branch
    CommandRouter router;

    router.handleEcosCommand(75, 50, 1);              // creates loco 75
    router.handleEcosFunctionCommand(75, 0x07, 0xFFFFFFFF);  // updates it (fully-specified)

    LocoState state;
    router.getStateEngine().getLoco(75, state);
    TEST_ASSERT_EQUAL_UINT32(0x07, state.functions);
    TEST_ASSERT_EQUAL_UINT8(50, state.speed);  // Speed preserved
}

void test_router_ecos_function_command_merges_partial_update(void) {
    // Phase 5 step 4: a single Ecos event usually only reports the one
    // function that actually changed (functions_mask marks which bit),
    // not the full F0-F31 state. This used to overwrite the entire bitmap
    // unconditionally, silently clobbering every other already-known
    // function back to 0.
    CommandRouter router;
    router.handleEcosFunctionCommand(75, 0x03, 0x03);  // F0 and F1 on, fully-specified

    // Only F2 is reported this time (mask=0x04) - F0/F1 must survive
    router.handleEcosFunctionCommand(75, 0x04, 0x04);

    LocoState state;
    router.getStateEngine().getLoco(75, state);
    TEST_ASSERT_EQUAL_UINT32(0x07, state.functions);  // F0, F1, F2 all on
}

void test_router_ecos_function_command_partial_update_can_clear_a_bit(void) {
    // The merge must also be able to turn a previously-on function off,
    // not just add new ones - as long as that bit is in the mask.
    CommandRouter router;
    router.handleEcosFunctionCommand(75, 0x07, 0x07);  // F0, F1, F2 all on

    // F2 reported off this time; F0/F1 not mentioned (mask only covers F2)
    router.handleEcosFunctionCommand(75, 0x00, 0x04);

    LocoState state;
    router.getStateEngine().getLoco(75, state);
    TEST_ASSERT_EQUAL_UINT32(0x03, state.functions);  // F0, F1 survive; F2 cleared
}

// ============================================================================
// TESTS - ACCESSORY / TURNOUT COMMANDS (Phase 5 step 10, v1)
// ============================================================================

void test_router_accessory_command_forwards_to_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleAccessoryCommand(5, true, LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getAccessoryCommandCount());
    TEST_ASSERT_EQUAL_UINT16(5, ecos_mock.getLastAccessoryCommand().address);
    TEST_ASSERT_TRUE(ecos_mock.getLastAccessoryCommand().diverging);
}

void test_router_accessory_command_straight_forwards_correctly(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleAccessoryCommand(5, false, LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getAccessoryCommandCount());
    TEST_ASSERT_FALSE(ecos_mock.getLastAccessoryCommand().diverging);
}

void test_router_accessory_command_from_ecos_does_not_forward_anywhere(void) {
    // v1 is XpressNet -> Ecos only - no Ecos-sourced accessory path exists
    // yet. Included as a regression guard for when that's added later.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleAccessoryCommand(5, true, LocoSource::ECOS);

    TEST_ASSERT_EQUAL_INT(0, xnet_mock.getAccessoryCommandCount());
}

void test_router_accessory_command_rejects_invalid_address(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleAccessoryCommand(0, true, LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(0, ecos_mock.getAccessoryCommandCount());
}

void test_router_accessory_command_surfaces_in_system_status(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleAccessoryCommand(5, true, LocoSource::XPRESSNET);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT16(5, status.last_accessory_address);
    TEST_ASSERT_TRUE(status.last_accessory_diverging);
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
    // Opposite-source update AFTER the attribution window is not suppressed.
    // Uses a genuinely different speed for the second call - handleEcosCommand()
    // now skips re-broadcasting a value that didn't actually change (2026-08-28
    // fix), so reusing the same speed here would pass for the wrong reason.
    // Window widened to ECOS_ECHO_ATTRIBUTION_WINDOW_MS (4000ms) the same day -
    // 500ms proved too short for Ecos's real confirmation round trip under load.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleXpressNetCommand(100, 64, 1);   // t=0, from XPRESSNET
    advanceMockNowMs(ECOS_ECHO_ATTRIBUTION_WINDOW_MS + 100);  // past the window
    router.handleEcosCommand(100, 90, 1);         // from ECOS, genuinely different speed

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getSpeedCommandCount());  // Should pass through
}

void test_router_xpressnet_command_not_suppressed_right_after_ecos_command(void) {
    // Design change 2026-08-28: handleXpressNetCommand() no longer has its
    // own incoming-echo gate. That gate used to suppress a genuine new
    // XpressNet command whenever echo_state had been touched recently by
    // ANY other source for the same address - increasingly common once
    // Ecos's own confirmations reliably reach XpressNet (a real, confirmed
    // live bug: legitimate function toggles intermittently just not
    // registering). XpressNet's master-polled bus has no real pathway for
    // the bridge's own outgoing broadcast to loop back in as a fake
    // incoming throttle request, so removing it doesn't reopen a real echo
    // risk - wasRecentSource() still protects the outgoing direction.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleEcosCommand(100, 64, 1);         // t=0, from ECOS (broadcasts to xpressnet, not ecos)
    router.handleXpressNetCommand(100, 90, 1);     // t=0, from XPRESSNET - a genuine new command, must go through

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getSpeedCommandCount());
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

void test_router_ecos_speed_only_update_preserves_direction(void) {
    // A real Ecos event reporting only a new speed (e.g. the throttle knob
    // moved, direction switch untouched) must not silently reset direction
    // back to whatever placeholder byte accompanied it - the same class of
    // bug already fixed for function merging (has_speed/has_direction mirror
    // functions_mask).
    CommandRouter router;

    router.handleEcosCommand(100, 0, 0, true, true);    // establish speed=0, dir=0
    router.handleEcosCommand(100, 50, 0, true, false);  // speed-only: has_direction=false

    LocoState state;
    router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_EQUAL_UINT8(50, state.speed);
    TEST_ASSERT_EQUAL_UINT8(0, state.direction);  // preserved, not reset
}

void test_router_ecos_direction_only_update_preserves_speed(void) {
    // Mirror of the above: a direction-only Ecos event must not silently
    // zero out speed.
    CommandRouter router;

    router.handleEcosCommand(100, 75, 1, true, true);   // establish speed=75, dir=1
    router.handleEcosCommand(100, 0, 0, false, true);   // direction-only: has_speed=false

    LocoState state;
    router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_EQUAL_UINT8(75, state.speed);  // preserved, not reset
    TEST_ASSERT_EQUAL_UINT8(0, state.direction);
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

    router.handleEcosFunctionCommand(75, 0x03, 0xFFFFFFFF);  // F0 and F1, fully-specified

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getFunctionCommandCount());
    TEST_ASSERT_EQUAL_UINT32(0x03, xnet_mock.getLastFunctionCommand().functions);
}

void test_router_ecos_broadcast_also_pushes_to_owning_xpressnet_slot(void) {
    // A MultiMaus that already has a loco selected doesn't reliably apply a
    // plain broadcast to its own display/button-latch model - only a
    // directed reply it recognizes as authoritative (Phase 5 step 9).
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleEcosCommand(100, 90, 1);

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getPushLocoStateCallCount());
    TEST_ASSERT_EQUAL_UINT16(100, xnet_mock.getLastPushedAddress());
}

void test_router_xpressnet_broadcast_does_not_push_to_ecos(void) {
    // Ecos has no "slot" concept - the targeted push is XpressNet-only.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 90, 1);

    TEST_ASSERT_EQUAL_INT(0, ecos_mock.getPushLocoStateCallCount());
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

void test_router_repeat_xpressnet_commands_do_not_resubscribe(void) {
    // Regression test for a real bug found against hardware (2026-07-31): a
    // real MultiMaus speed sweep showed "requesting Ecos subscription" on
    // every single command instead of just the first. Root cause was that
    // subscribed_to_ecos was only ever set true by the Ecos-initiated path,
    // never by the XpressNet-initiated one that actually requests it.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 20, 0);
    router.handleXpressNetCommand(100, 29, 0);
    router.handleXpressNetCommand(100, 27, 0);
    router.handleXpressNetCommand(100, 0, 1);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getSubscribeCallCount());
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
// TESTS - EMERGENCY STOP / RESUME (Phase 5 step 2)
// ============================================================================

void test_router_emergency_stop_zeroes_every_known_loco(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleXpressNetCommand(100, 64, 1);
    router.handleXpressNetCommand(200, 90, 0);
    router.handleXpressNetCommand(300, 30, 1);

    router.emergencyStopAll(LocoSource::XPRESSNET);

    LocoState state;
    router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_EQUAL_UINT8(0, state.speed);
    router.getStateEngine().getLoco(200, state);
    TEST_ASSERT_EQUAL_UINT8(0, state.speed);
    router.getStateEngine().getLoco(300, state);
    TEST_ASSERT_EQUAL_UINT8(0, state.speed);
}

void test_router_emergency_stop_preserves_direction(void) {
    // Speed goes to 0, but direction is not a safety concern here and
    // shouldn't be silently changed.
    CommandRouter router;
    router.handleXpressNetCommand(100, 64, 0);  // reverse

    router.emergencyStopAll(LocoSource::XPRESSNET);

    LocoState state;
    router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_EQUAL_UINT8(0, state.direction);
}

void test_router_emergency_stop_broadcasts_zero_speed_to_xpressnet(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.handleXpressNetCommand(100, 64, 1);
    xnet_mock.reset();  // clear the count from the setup command above

    router.emergencyStopAll(LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getSpeedCommandCount());
    TEST_ASSERT_EQUAL_UINT16(100, xnet_mock.getLastSpeedCommand().address);
    TEST_ASSERT_EQUAL_UINT8(0, xnet_mock.getLastSpeedCommand().speed);
}

void test_router_emergency_stop_sends_single_ecos_system_stop(void) {
    // Ecos gets one real system-wide stop command, not a per-loco loop.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleXpressNetCommand(100, 64, 1);
    router.handleXpressNetCommand(200, 90, 0);
    router.handleXpressNetCommand(300, 30, 1);

    router.emergencyStopAll(LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getEmergencyStopCallCount());
}

void test_router_emergency_stop_with_no_known_locos_still_stops_ecos(void) {
    // A system-wide stop should reach Ecos even if the bridge doesn't
    // currently know about any locomotive.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.emergencyStopAll(LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getEmergencyStopCallCount());
}

void test_router_resume_operation_forwards_to_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.resumeOperation(LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getResumeOperationCallCount());
}

void test_router_resume_operation_does_not_touch_loco_speed(void) {
    // Resuming must never resurrect a previous speed - the operator has to
    // re-throttle manually. A loco that was never stopped should be
    // completely unaffected by resumeOperation().
    CommandRouter router;
    router.handleXpressNetCommand(100, 50, 1);

    router.resumeOperation(LocoSource::XPRESSNET);

    LocoState state;
    router.getStateEngine().getLoco(100, state);
    TEST_ASSERT_EQUAL_UINT8(50, state.speed);
}

// ============================================================================
// TESTS - EMERGENCY STOP / RESUME BIDIRECTIONALITY (real gap found live:
// XNet-triggered stop/go reached Ecos correctly, but Ecos-triggered stop/go
// never reached XNet/the MultiMaus at all - Ecos was never subscribed to its
// own base object's status events)
// ============================================================================

void test_router_emergency_stop_from_xpressnet_does_not_reecho_to_xpressnet(void) {
    // XpressNet already knows about its own stop request (onPowerStateChange
    // echoes it directly) - CommandRouter shouldn't tell it again.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.emergencyStopAll(LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(0, xnet_mock.getEmergencyStopCallCount());
}

void test_router_emergency_stop_from_ecos_reaches_xpressnet(void) {
    // The real gap: an operator hitting STOP directly on the Ecos must
    // still propagate to XpressNet throttles.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.emergencyStopAll(LocoSource::ECOS);

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getEmergencyStopCallCount());
}

void test_router_emergency_stop_from_ecos_does_not_reecho_to_ecos(void) {
    // Ecos already knows about its own stop (it's the one that sent the
    // event) - re-sending set(1, stop) back to it would be redundant and,
    // if Ecos re-emitted the event each time, a real feedback loop.
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.emergencyStopAll(LocoSource::ECOS);

    TEST_ASSERT_EQUAL_INT(0, ecos_mock.getEmergencyStopCallCount());
}

void test_router_resume_from_ecos_reaches_xpressnet(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.resumeOperation(LocoSource::ECOS);

    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getResumeOperationCallCount());
}

void test_router_resume_from_ecos_does_not_reecho_to_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.resumeOperation(LocoSource::ECOS);

    TEST_ASSERT_EQUAL_INT(0, ecos_mock.getResumeOperationCallCount());
}

void test_router_resume_from_xpressnet_does_not_reecho_to_xpressnet(void) {
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    router.setXpressNetInterface(&xnet_mock);

    router.resumeOperation(LocoSource::XPRESSNET);

    TEST_ASSERT_EQUAL_INT(0, xnet_mock.getResumeOperationCallCount());
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

void test_router_surfaces_xnet_status_in_system_status(void) {
    // getSystemStatus() reads xpressnet->getStatus() only when an
    // XpressNet interface is actually set - covers that branch, distinct
    // from the "xpressnet == nullptr -> DISCONNECTED" default path.
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    xnet_mock.setStatus(ComponentStatus::CONNECTED);
    router.setXpressNetInterface(&xnet_mock);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_INT((int)ComponentStatus::CONNECTED, (int)status.xnet_status);
}

void test_router_surfaces_xnet_last_message_age(void) {
    // Phase 5 step 8: getSystemStatus() reads xpressnet->getLastMessageAgeMs()
    CommandRouter router;
    MockProtocolInterface xnet_mock;
    xnet_mock.setLastMessageAgeMs(4200);
    router.setXpressNetInterface(&xnet_mock);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT32(4200, status.xnet_last_message_age_ms);
}

void test_router_xnet_last_message_age_defaults_to_no_timestamp(void) {
    // No XpressNet interface set at all - distinct from "interface set but
    // never received a message yet" (which the mock/real interface itself
    // already defaults to NO_TIMESTAMP for).
    CommandRouter router;

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT32(ProtocolInterface::NO_TIMESTAMP, status.xnet_last_message_age_ms);
}

void test_router_surfaces_ecos_heartbeat_latency(void) {
    // Phase 5 step 8: getSystemStatus() reads ecos->getLastHeartbeatLatencyMs()
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    ecos_mock.setHeartbeatLatencyMs(37);
    router.setEcosInterface(&ecos_mock);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT32(37, status.ecos_heartbeat_latency_ms);
}

void test_router_ecos_heartbeat_latency_defaults_to_no_timestamp(void) {
    CommandRouter router;

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT32(ProtocolInterface::NO_TIMESTAMP, status.ecos_heartbeat_latency_ms);
}

void test_router_xpressnet_speed_command_surfaces_existing_functions(void) {
    // A speed-only command must not blank out functions already known for
    // that loco - last_command_functions should reflect current state.
    CommandRouter router;
    router.handleXpressNetFunctionCommand(100, 0x05);  // F0, F2 on
    router.handleXpressNetCommand(100, 64, 1);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT16(100, status.last_command_address);
    TEST_ASSERT_EQUAL_UINT32(0x05, status.last_command_functions);
}

void test_router_xpressnet_function_command_updates_last_command(void) {
    // Real gap found 2026-08-03: pure function commands never updated
    // last_command at all (only speed commands did), so a function-only
    // interaction (e.g. toggling a headlight) wouldn't show up as "last
    // command" on the OLED at all.
    CommandRouter router;
    router.handleXpressNetCommand(100, 50, 1);
    router.handleXpressNetFunctionCommand(100, 0x01);  // F0 on

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT16(100, status.last_command_address);
    TEST_ASSERT_EQUAL_UINT8(50, status.last_command_speed);
    TEST_ASSERT_EQUAL_UINT32(0x01, status.last_command_functions);
}

void test_router_ecos_speed_command_surfaces_existing_functions(void) {
    CommandRouter router;
    router.handleEcosFunctionCommand(100, 0x02, 0x02);  // F1 on
    router.handleEcosCommand(100, 64, 1);

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT16(100, status.last_command_address);
    TEST_ASSERT_EQUAL_UINT32(0x02, status.last_command_functions);
}

void test_router_ecos_function_command_updates_last_command(void) {
    // Same gap as the XpressNet side, Ecos direction.
    CommandRouter router;
    router.handleEcosCommand(100, 50, 1);
    router.handleEcosFunctionCommand(100, 0x01, 0x01);  // F0 on

    SystemStatus status = router.getSystemStatus();
    TEST_ASSERT_EQUAL_UINT16(100, status.last_command_address);
    TEST_ASSERT_EQUAL_UINT8(50, status.last_command_speed);
    TEST_ASSERT_EQUAL_UINT32(0x01, status.last_command_functions);
}

#if ENABLE_DEBUG
void test_router_debug_print_echo_state_does_not_crash(void) {
    // Smoke test for debugPrintEchoState(), covering both its
    // "still within window" and "window expired" branches.
    CommandRouter router;
    router.handleXpressNetCommand(100, 64, 1);

    router.debugPrintEchoState();       // age < ECOS_ECHO_ATTRIBUTION_WINDOW_MS
    advanceMockNowMs(ECOS_ECHO_ATTRIBUTION_WINDOW_MS + 100);
    router.debugPrintEchoState();       // age >= ECOS_ECHO_ATTRIBUTION_WINDOW_MS

    TEST_PASS();  // Reaching here without crashing is the assertion
}
#endif

// ============================================================================
// Z21 LAN ROUTING (Phase 6 step 4)
// ============================================================================
// Mirrors the existing XpressNet routing tests above - handleZ21Command()/
// handleZ21FunctionCommand() are structurally identical, and
// broadcastCommand()'s Ecos branch now fans out to Z21 too.

void test_router_route_z21_speed_to_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleZ21Command(100, 64, 1);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getSpeedCommandCount());
    TEST_ASSERT_EQUAL_UINT16(100, ecos_mock.getLastSpeedCommand().address);
    TEST_ASSERT_EQUAL_UINT8(64, ecos_mock.getLastSpeedCommand().speed);
}

void test_router_route_z21_function_to_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.handleZ21FunctionCommand(50, 0x01);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getFunctionCommandCount());
    TEST_ASSERT_EQUAL_UINT16(50, ecos_mock.getLastFunctionCommand().address);
}

void test_router_route_ecos_to_z21(void) {
    // The other direction: an Ecos-sourced update must reach Z21 clients,
    // same as it already reaches XpressNet.
    CommandRouter router;
    MockProtocolInterface z21_mock;
    router.setZ21Interface(&z21_mock);

    router.handleEcosCommand(100, 64, 1);

    TEST_ASSERT_EQUAL_INT(1, z21_mock.getSpeedCommandCount());
    TEST_ASSERT_EQUAL_UINT16(100, z21_mock.getLastSpeedCommand().address);
}

void test_router_z21_receives_direct_xpressnet_fanout(void) {
    // Design change 2026-08-28: a throttle-facing protocol now fans out
    // directly to every OTHER throttle-facing protocol immediately, not
    // just to Ecos - waiting on Ecos's own echo to complete the loop to
    // the other protocol turned out to be fragile in practice (see
    // CHANGELOG for the specific bugs this caused). See the
    // broadcastCommand() comment this mirrors.
    CommandRouter router;
    MockProtocolInterface z21_mock;
    router.setZ21Interface(&z21_mock);

    router.handleXpressNetCommand(100, 64, 1);

    TEST_ASSERT_EQUAL_INT(1, z21_mock.getSpeedCommandCount());
}

void test_router_echo_prevention_z21_vs_ecos(void) {
    CommandRouter router;
    MockProtocolInterface z21_mock;
    MockProtocolInterface ecos_mock;
    router.setZ21Interface(&z21_mock);
    router.setEcosInterface(&ecos_mock);

    router.handleZ21Command(100, 64, 1);   // t=0, from Z21_LAN
    router.handleEcosCommand(100, 64, 1);  // t=0, from ECOS (opposite source)

    TEST_ASSERT_EQUAL_INT(0, z21_mock.getSpeedCommandCount());  // suppressed
}

void test_router_emergency_stop_from_z21_reaches_ecos(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    router.setEcosInterface(&ecos_mock);

    router.emergencyStopAll(LocoSource::Z21_LAN);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getEmergencyStopCallCount());
}

void test_router_emergency_stop_from_ecos_reaches_z21(void) {
    CommandRouter router;
    MockProtocolInterface z21_mock;
    router.setZ21Interface(&z21_mock);

    router.emergencyStopAll(LocoSource::ECOS);

    TEST_ASSERT_EQUAL_INT(1, z21_mock.getEmergencyStopCallCount());
}

void test_router_emergency_stop_from_z21_does_not_reecho_to_z21(void) {
    CommandRouter router;
    MockProtocolInterface z21_mock;
    router.setZ21Interface(&z21_mock);

    router.emergencyStopAll(LocoSource::Z21_LAN);

    TEST_ASSERT_EQUAL_INT(0, z21_mock.getEmergencyStopCallCount());
}

void test_router_resume_operation_from_z21_reaches_ecos_and_xpressnet(void) {
    CommandRouter router;
    MockProtocolInterface ecos_mock;
    MockProtocolInterface xnet_mock;
    router.setEcosInterface(&ecos_mock);
    router.setXpressNetInterface(&xnet_mock);

    router.resumeOperation(LocoSource::Z21_LAN);

    TEST_ASSERT_EQUAL_INT(1, ecos_mock.getResumeOperationCallCount());
    TEST_ASSERT_EQUAL_INT(1, xnet_mock.getResumeOperationCallCount());
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_router_route_xpressnet_speed_to_ecos);
    RUN_TEST(test_router_route_function_command);
    RUN_TEST(test_router_xpressnet_function_command_updates_existing_loco);

    RUN_TEST(test_router_xpressnet_function_command_rejects_invalid_address);
    RUN_TEST(test_router_ecos_command_rejects_invalid_address);
    RUN_TEST(test_router_ecos_command_rejects_invalid_speed);
    RUN_TEST(test_router_ecos_function_command_rejects_invalid_address);
    RUN_TEST(test_router_ecos_function_command_updates_existing_loco);
    RUN_TEST(test_router_ecos_function_command_merges_partial_update);
    RUN_TEST(test_router_ecos_function_command_partial_update_can_clear_a_bit);

    RUN_TEST(test_router_accessory_command_forwards_to_ecos);
    RUN_TEST(test_router_accessory_command_straight_forwards_correctly);
    RUN_TEST(test_router_accessory_command_from_ecos_does_not_forward_anywhere);
    RUN_TEST(test_router_accessory_command_rejects_invalid_address);
    RUN_TEST(test_router_accessory_command_surfaces_in_system_status);

    RUN_TEST(test_router_echo_prevention_opposite_source_suppressed);
    RUN_TEST(test_router_xpressnet_command_not_suppressed_right_after_ecos_command);
    RUN_TEST(test_router_same_source_repeat_is_not_echo);
    RUN_TEST(test_router_echo_prevention_window_expires);

    RUN_TEST(test_router_adds_loco_to_state_engine);
    RUN_TEST(test_router_updates_loco_speed);
    RUN_TEST(test_router_ecos_speed_only_update_preserves_direction);
    RUN_TEST(test_router_ecos_direction_only_update_preserves_speed);

    RUN_TEST(test_router_broadcast_ecos_update_to_xpressnet);
    RUN_TEST(test_router_broadcast_ecos_function_to_xpressnet);
    RUN_TEST(test_router_ecos_broadcast_also_pushes_to_owning_xpressnet_slot);
    RUN_TEST(test_router_xpressnet_broadcast_does_not_push_to_ecos);

    RUN_TEST(test_router_new_loco_triggers_ecos_subscription);
    RUN_TEST(test_router_repeat_xpressnet_commands_do_not_resubscribe);
    RUN_TEST(test_router_expiry_triggers_ecos_unsubscribe);

    RUN_TEST(test_router_emergency_stop_zeroes_every_known_loco);
    RUN_TEST(test_router_emergency_stop_preserves_direction);
    RUN_TEST(test_router_emergency_stop_broadcasts_zero_speed_to_xpressnet);
    RUN_TEST(test_router_emergency_stop_sends_single_ecos_system_stop);
    RUN_TEST(test_router_emergency_stop_with_no_known_locos_still_stops_ecos);
    RUN_TEST(test_router_resume_operation_forwards_to_ecos);
    RUN_TEST(test_router_resume_operation_does_not_touch_loco_speed);

    RUN_TEST(test_router_emergency_stop_from_xpressnet_does_not_reecho_to_xpressnet);
    RUN_TEST(test_router_emergency_stop_from_ecos_reaches_xpressnet);
    RUN_TEST(test_router_emergency_stop_from_ecos_does_not_reecho_to_ecos);
    RUN_TEST(test_router_resume_from_ecos_reaches_xpressnet);
    RUN_TEST(test_router_resume_from_ecos_does_not_reecho_to_ecos);
    RUN_TEST(test_router_resume_from_xpressnet_does_not_reecho_to_xpressnet);

    RUN_TEST(test_router_reject_invalid_address_zero);
    RUN_TEST(test_router_reject_invalid_speed);

    RUN_TEST(test_router_surfaces_ecos_status_in_system_status);
    RUN_TEST(test_router_surfaces_xnet_status_in_system_status);
    RUN_TEST(test_router_surfaces_xnet_last_message_age);
    RUN_TEST(test_router_xnet_last_message_age_defaults_to_no_timestamp);
    RUN_TEST(test_router_surfaces_ecos_heartbeat_latency);
    RUN_TEST(test_router_ecos_heartbeat_latency_defaults_to_no_timestamp);

    RUN_TEST(test_router_xpressnet_speed_command_surfaces_existing_functions);
    RUN_TEST(test_router_xpressnet_function_command_updates_last_command);
    RUN_TEST(test_router_ecos_speed_command_surfaces_existing_functions);
    RUN_TEST(test_router_ecos_function_command_updates_last_command);

    RUN_TEST(test_router_route_z21_speed_to_ecos);
    RUN_TEST(test_router_route_z21_function_to_ecos);
    RUN_TEST(test_router_route_ecos_to_z21);
    RUN_TEST(test_router_z21_receives_direct_xpressnet_fanout);
    RUN_TEST(test_router_echo_prevention_z21_vs_ecos);
    RUN_TEST(test_router_emergency_stop_from_z21_reaches_ecos);
    RUN_TEST(test_router_emergency_stop_from_ecos_reaches_z21);
    RUN_TEST(test_router_emergency_stop_from_z21_does_not_reecho_to_z21);
    RUN_TEST(test_router_resume_operation_from_z21_reaches_ecos_and_xpressnet);

    #if ENABLE_DEBUG
    RUN_TEST(test_router_debug_print_echo_state_does_not_crash);
    #endif

    return UNITY_END();
}
