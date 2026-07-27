/*
 * Mock Time Header - Testing support
 *
 * Public interface for controlling virtual time in unit tests.
 */

#ifndef MOCK_NOW_MS_H
#define MOCK_NOW_MS_H

#include <cstdint>

// ============================================================================
// PUBLIC API - Test control
// ============================================================================

/**
 * Get current mock time
 * @return Current virtual time in milliseconds
 */
unsigned long getMockNowMs(void);

/**
 * Set mock time to specific value
 * @param ms New virtual time in milliseconds
 */
void setMockNowMs(unsigned long ms);

/**
 * Advance mock time by delta
 * @param delta_ms Milliseconds to advance
 */
void advanceMockNowMs(unsigned long delta_ms);

/**
 * Reset mock time to 0
 */
void resetMockNowMs(void);

/**
 * Mock millis() - Replaces Arduino millis() during testing
 * @return Current mock time in milliseconds
 */
unsigned long millis(void);

// ============================================================================
// TIME CONSTANTS FOR TESTING
// ============================================================================

// Identifiers for getTestTimeConstant()
#define TEST_TIME_0_MS          0
#define TEST_TIME_100_MS        1
#define TEST_TIME_500_MS        2   // Echo prevention window
#define TEST_TIME_1_SEC         3
#define TEST_TIME_5_SEC         4
#define TEST_TIME_10_SEC        5
#define TEST_TIME_30_SEC        6   // Heartbeat interval
#define TEST_TIME_1_MIN         7
#define TEST_TIME_5_MIN         8   // Loco expiry timeout
#define TEST_TIME_10_MIN        9   // Address map refresh
#define TEST_TIME_1_HOUR        10

/**
 * Get predefined test time constants
 * @param constant_id One of TEST_TIME_* constants above
 * @return Time in milliseconds
 */
unsigned long getTestTimeConstant(int constant_id);

#endif  // MOCK_NOW_MS_H
