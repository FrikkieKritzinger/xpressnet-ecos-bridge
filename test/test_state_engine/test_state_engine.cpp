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
#include <unity.h>
#include "state_engine.h"
#include "definitions.h"
#include "mocks/mock_now_ms.h"

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
    TEST_ASSERT_TRUE(success);

    // Verify: loco exists with correct values
    LocoState retrieved;
    bool found = engine.getLoco(100, retrieved);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_UINT8(64, retrieved.speed);
    TEST_ASSERT_EQUAL_UINT8(1, retrieved.direction);
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

        TEST_ASSERT_TRUE(engine.addOrUpdateLoco(100 + i, state));
    }

    // Verify all exist
    for (int i = 0; i < 10; i++) {
        int index = engine.findLoco(100 + i);
        TEST_ASSERT_TRUE(index >= 0);
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
    TEST_ASSERT_TRUE(index >= 0);
}

void test_state_engine_find_nonexistent_returns_negative(void) {
    // findLoco for non-existent loco should return -1
    StateEngine engine;

    int index = engine.findLoco(999);
    TEST_ASSERT_TRUE(index < 0);
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
    TEST_ASSERT_EQUAL_UINT8(100, retrieved.speed);
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
    TEST_ASSERT_EQUAL_UINT8(0, retrieved.direction);
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
    TEST_ASSERT_EQUAL_UINT32(0x01, retrieved.functions);
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
    TEST_ASSERT_EQUAL_INT(1, removed_count);

    // Verify loco is gone
    int index = engine.findLoco(100);
    TEST_ASSERT_TRUE(index < 0);
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
    TEST_ASSERT_EQUAL_INT(0, removed_count);

    // Verify loco still exists
    int index = engine.findLoco(100);
    TEST_ASSERT_TRUE(index >= 0);
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
    TEST_ASSERT_EQUAL_INT(3, removed_count);

    // Verify locos 4, 5 still exist
    TEST_ASSERT_TRUE(engine.findLoco(4) >= 0);
    TEST_ASSERT_TRUE(engine.findLoco(5) >= 0);
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
    TEST_ASSERT_EQUAL_INT(7, engine.getLocoCount());
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
    TEST_ASSERT_TRUE(success);

    // Verify removed
    TEST_ASSERT_TRUE(engine.findLoco(100) < 0);
}

void test_state_engine_remove_nonexistent_loco_by_address(void) {
    // removeLocoByAddress for an address that was never added should fail cleanly
    StateEngine engine;

    bool success = engine.removeLocoByAddress(999);
    TEST_ASSERT_FALSE(success);
}

// ============================================================================
// TESTS - VALIDATION & CAPACITY
// ============================================================================

void test_state_engine_reject_invalid_address(void) {
    // addOrUpdateLoco should reject addresses outside 1-9999
    StateEngine engine;
    LocoState state;
    state.dcc_address = 0;
    state.speed = 50;

    bool success = engine.addOrUpdateLoco(0, state);
    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_TRUE(engine.findLoco(0) < 0);
}

void test_state_engine_full_capacity_rejects_new_loco(void) {
    // Fill the engine to MAX_LOCOS, then verify the next add is rejected
    StateEngine engine;

    for (int i = 0; i < MAX_LOCOS; i++) {
        LocoState state;
        state.dcc_address = 1 + i;
        state.speed = 0;
        TEST_ASSERT_TRUE(engine.addOrUpdateLoco(1 + i, state));
    }
    TEST_ASSERT_EQUAL_INT(MAX_LOCOS, engine.getLocoCount());

    // One more (a brand new address) should be rejected - engine is full
    LocoState overflow_state;
    overflow_state.dcc_address = 9999;
    overflow_state.speed = 0;
    bool success = engine.addOrUpdateLoco(9999, overflow_state);
    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_EQUAL_INT(MAX_LOCOS, engine.getLocoCount());  // Unchanged

    // Updating an EXISTING loco should still work even when full
    LocoState update_state;
    update_state.dcc_address = 1;
    update_state.speed = 42;
    TEST_ASSERT_TRUE(engine.addOrUpdateLoco(1, update_state));
}

// ============================================================================
// TESTS - INDEX-BASED ITERATION
// ============================================================================

void test_state_engine_get_loco_by_index_valid(void) {
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 64;
    engine.addOrUpdateLoco(100, state);

    LocoState* loco = engine.getLocoByIndex(0);
    TEST_ASSERT_NOT_NULL(loco);
    TEST_ASSERT_EQUAL_UINT16(100, loco->dcc_address);
    TEST_ASSERT_EQUAL_UINT8(64, loco->speed);
}

void test_state_engine_get_loco_by_index_invalid(void) {
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    engine.addOrUpdateLoco(100, state);

    TEST_ASSERT_NULL(engine.getLocoByIndex(-1));
    TEST_ASSERT_NULL(engine.getLocoByIndex(1));  // Only index 0 exists
}

void test_state_engine_get_loco_by_index_const(void) {
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 64;
    engine.addOrUpdateLoco(100, state);

    const StateEngine& const_engine = engine;
    const LocoState* loco = const_engine.getLocoByIndex(0);
    TEST_ASSERT_NOT_NULL(loco);
    TEST_ASSERT_EQUAL_UINT16(100, loco->dcc_address);
    TEST_ASSERT_NULL(const_engine.getLocoByIndex(-1));
}

// ============================================================================
// TESTS - CLEAR
// ============================================================================

void test_state_engine_clear_removes_all_locos(void) {
    StateEngine engine;

    for (int i = 0; i < 5; i++) {
        LocoState state;
        state.dcc_address = 100 + i;
        engine.addOrUpdateLoco(100 + i, state);
    }
    TEST_ASSERT_EQUAL_INT(5, engine.getLocoCount());

    engine.clear();

    TEST_ASSERT_EQUAL_INT(0, engine.getLocoCount());
    TEST_ASSERT_TRUE(engine.findLoco(100) < 0);
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
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_TRUE(engine.findLoco(9999) >= 0);
}

void test_state_engine_address_min_long(void) {
    // Add loco 100 (min long address)
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    state.speed = 50;

    bool success = engine.addOrUpdateLoco(100, state);
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_TRUE(engine.findLoco(100) >= 0);
}

void test_state_engine_address_max_short(void) {
    // Add loco 99 (max short address)
    StateEngine engine;
    LocoState state;
    state.dcc_address = 99;
    state.speed = 50;

    bool success = engine.addOrUpdateLoco(99, state);
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_TRUE(engine.findLoco(99) >= 0);
}

// ============================================================================
// TEST RUNNER
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_state_engine_add_new_loco);
    RUN_TEST(test_state_engine_add_multiple_locos);
    RUN_TEST(test_state_engine_find_loco_returns_index);
    RUN_TEST(test_state_engine_find_nonexistent_returns_negative);

    RUN_TEST(test_state_engine_update_speed);
    RUN_TEST(test_state_engine_update_direction);
    RUN_TEST(test_state_engine_update_functions);

    RUN_TEST(test_state_engine_loco_expires_after_5_minutes);
    RUN_TEST(test_state_engine_loco_does_not_expire_before_5_minutes);
    RUN_TEST(test_state_engine_expunge_multiple_expired);

    RUN_TEST(test_state_engine_loco_count);
    RUN_TEST(test_state_engine_remove_loco_by_address);
    RUN_TEST(test_state_engine_remove_nonexistent_loco_by_address);

    RUN_TEST(test_state_engine_reject_invalid_address);
    RUN_TEST(test_state_engine_full_capacity_rejects_new_loco);

    RUN_TEST(test_state_engine_get_loco_by_index_valid);
    RUN_TEST(test_state_engine_get_loco_by_index_invalid);
    RUN_TEST(test_state_engine_get_loco_by_index_const);

    RUN_TEST(test_state_engine_clear_removes_all_locos);

    RUN_TEST(test_state_engine_address_max_long);
    RUN_TEST(test_state_engine_address_min_long);
    RUN_TEST(test_state_engine_address_max_short);

    return UNITY_END();
}
