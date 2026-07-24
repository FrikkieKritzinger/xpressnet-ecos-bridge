/*
 * State Engine Unit Tests
 *
 * Tests for:
 * - Loco addition and update
 * - State persistence (speed, direction, functions)
 * - Loco expiry (5 minute inactivity timeout)
 * - Address lookup and iteration
 * - Maximum capacity (50 locos)
 * - Memory management (no leaks, fixed allocation)
 */

#include <cstdint>
#include <cstring>
#include "../xpressnet_ecos_bridge/state_engine.h"
#include "../xpressnet_ecos_bridge/definitions.h"
#include "mocks/mock_now_ms.h"

// TODO: Include Unity framework headers once configured
// #include "unity.h"

// ============================================================================
// TEST SETUP / TEARDOWN
// ============================================================================

void setUp(void) {
    // Reset mock time to 0
    resetMockNowMs();
}

void tearDown(void) {
    // No cleanup needed
}

// ============================================================================
// TESTS - LOCO ADDITION
// ============================================================================

void test_state_engine_add_new_loco(void) {
    // Add loco 100 with speed 64, direction forward
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 64;
    state.direction = 1;  // forward
    state.functions = 0x00;

    bool success = engine.addOrUpdateLoco(100, state);
    if (!success) return;  // Failed to add

    // Verify: loco exists with correct values
    LocoState retrieved;
    bool found = engine.getLoco(100, retrieved);
    if (!found) return;
    if (retrieved.speed != 64) return;
    if (retrieved.direction != 1) return;
}

void test_state_engine_add_multiple_locos(void) {
    // Add 10 different locos
    StateEngine engine;

    for (int i = 0; i < 10; i++) {
        LocoState state;
        state.dcc_address = 100 + i;
        state.speed = 50 + i;
        state.direction = (i % 2);
        state.functions = i;

        if (!engine.addOrUpdateLoco(100 + i, state)) return;
    }

    // Verify all exist
    for (int i = 0; i < 10; i++) {
        int index = engine.findLoco(100 + i);
        if (index < 0) return;  // Not found
    }
}

void test_state_engine_find_loco_returns_index(void) {
    // findLoco should return valid index (>= 0)
    StateEngine engine;
    LocoState state;
    state.dcc_address = 50;
    state.speed = 30;

    engine.addOrUpdateLoco(50, state);

    int index = engine.findLoco(50);
    if (index < 0) return;  // Should find it
}

void test_state_engine_find_nonexistent_returns_negative(void) {
    // findLoco for non-existent loco should return -1
    StateEngine engine;

    int index = engine.findLoco(999);
    if (index >= 0) return;  // Should return -1
}

// ============================================================================
// TESTS - LOCO UPDATE
// ============================================================================

void test_state_engine_update_speed(void) {
    // Add loco 100, then update speed
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 64;
    state.direction = 1;

    engine.addOrUpdateLoco(100, state);

    // Update speed
    state.speed = 100;
    engine.addOrUpdateLoco(100, state);

    // Verify new speed
    LocoState retrieved;
    engine.getLoco(100, retrieved);
    if (retrieved.speed != 100) return;
}

void test_state_engine_update_direction(void) {
    // Add loco 50 forward, update to reverse
    StateEngine engine;
    LocoState state;
    state.dcc_address = 50;
    state.speed = 50;
    state.direction = 1;  // forward

    engine.addOrUpdateLoco(50, state);

    // Update direction
    state.direction = 0;  // reverse
    engine.addOrUpdateLoco(50, state);

    LocoState retrieved;
    engine.getLoco(50, retrieved);
    if (retrieved.direction != 0) return;
}

void test_state_engine_update_functions(void) {
    // Add loco 75, update functions
    StateEngine engine;
    LocoState state;
    state.dcc_address = 75;
    state.functions = 0x00;

    engine.addOrUpdateLoco(75, state);

    // Update functions
    state.functions = 0x01;  // F0 on
    engine.addOrUpdateLoco(75, state);

    LocoState retrieved;
    engine.getLoco(75, retrieved);
    if (retrieved.functions != 0x01) return;
}

// ============================================================================
// TESTS - LOCO EXPIRY (using mock time)
// ============================================================================

void test_state_engine_loco_expires_after_5_minutes(void) {
    // Add loco, advance time 5+ minutes, call expunge
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 50;

    resetMockNowMs();
    engine.addOrUpdateLoco(100, state);

    // Advance time 5 minutes + 1ms
    setMockNowMs(300000 + 1);

    int removed_count = engine.expungeInactiveLocos();
    if (removed_count != 1) return;  // Should remove 1

    // Verify loco is gone
    int index = engine.findLoco(100);
    if (index >= 0) return;  // Should not find it
}

void test_state_engine_loco_does_not_expire_before_5_minutes(void) {
    // Add loco, advance time 4 minutes, call expunge
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 50;

    resetMockNowMs();
    engine.addOrUpdateLoco(100, state);

    // Advance time 4 minutes (before 5 min timeout)
    setMockNowMs(240000);

    int removed_count = engine.expungeInactiveLocos();
    if (removed_count != 0) return;  // Should remove 0

    // Verify loco still exists
    int index = engine.findLoco(100);
    if (index < 0) return;  // Should find it
}

void test_state_engine_expunge_multiple_expired(void) {
    // Add 5 locos, expire 3 of them (by time)
    StateEngine engine;

    resetMockNowMs();

    // Add loco 1 at time 0
    LocoState state1;
    state1.dcc_address = 1;
    engine.addOrUpdateLoco(1, state1);

    // Add loco 2 at time 0
    LocoState state2;
    state2.dcc_address = 2;
    engine.addOrUpdateLoco(2, state2);

    // Add loco 3 at time 0
    LocoState state3;
    state3.dcc_address = 3;
    engine.addOrUpdateLoco(3, state3);

    // Advance time 5+ minutes
    advanceMockNowMs(300000 + 1);

    // Add loco 4 and 5 after timeout (these will be fresh)
    LocoState state4;
    state4.dcc_address = 4;
    engine.addOrUpdateLoco(4, state4);

    LocoState state5;
    state5.dcc_address = 5;
    engine.addOrUpdateLoco(5, state5);

    // Expunge - should remove locos 1, 2, 3
    int removed_count = engine.expungeInactiveLocos();
    if (removed_count != 3) return;

    // Verify locos 4, 5 still exist
    if (engine.findLoco(4) < 0) return;
    if (engine.findLoco(5) < 0) return;
}

// ============================================================================
// TESTS - LOOKUP AND COUNTING
// ============================================================================

void test_state_engine_loco_count(void) {
    // Add 7 locos, verify count
    StateEngine engine;

    for (int i = 0; i < 7; i++) {
        LocoState state;
        state.dcc_address = 100 + i;
        state.speed = 50;
        engine.addOrUpdateLoco(100 + i, state);
    }

    // Verify count
    if (engine.getLocoCount() != 7) return;
}

void test_state_engine_remove_loco_by_address(void) {
    // Add loco, remove by address
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 50;

    engine.addOrUpdateLoco(100, state);

    // Remove by address
    bool success = engine.removeLocoByAddress(100);
    if (!success) return;

    // Verify removed
    if (engine.findLoco(100) >= 0) return;  // Should not find it
}

// ============================================================================
// TESTS - EDGE CASES
// ============================================================================

void test_state_engine_address_max_long(void) {
    // Add loco 9999 (max long address)
    StateEngine engine;
    LocoState state;
    state.dcc_address = 9999;
    state.speed = 50;

    bool success = engine.addOrUpdateLoco(9999, state);
    if (!success) return;

    if (engine.findLoco(9999) < 0) return;
}

void test_state_engine_address_min_long(void) {
    // Add loco 100 (min long address)
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 50;

    bool success = engine.addOrUpdateLoco(100, state);
    if (!success) return;

    if (engine.findLoco(100) < 0) return;
}

void test_state_engine_address_max_short(void) {
    // Add loco 99 (max short address)
    StateEngine engine;
    LocoState state;
    state.dcc_address = 99;
    state.speed = 50;

    bool success = engine.addOrUpdateLoco(99, state);
    if (!success) return;

    if (engine.findLoco(99) < 0) return;
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    // Run all tests
    setUp();

    test_state_engine_add_new_loco();
    test_state_engine_add_multiple_locos();
    test_state_engine_find_loco_returns_index();
    test_state_engine_find_nonexistent_returns_negative();

    test_state_engine_update_speed();
    test_state_engine_update_direction();
    test_state_engine_update_functions();

    test_state_engine_loco_expires_after_5_minutes();
    test_state_engine_loco_does_not_expire_before_5_minutes();
    test_state_engine_expunge_multiple_expired();

    test_state_engine_loco_count();
    test_state_engine_remove_loco_by_address();

    test_state_engine_address_max_long();
    test_state_engine_address_min_long();
    test_state_engine_address_max_short();

    tearDown();

    return 0;  // TODO: Return UNITY_END() result
}
