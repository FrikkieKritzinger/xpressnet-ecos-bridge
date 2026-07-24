/*
 * Mock Time Implementation - Testing support
 *
 * Provides deterministic time control for unit tests.
 * Allows advancing virtual time to test timeouts and expiry.
 *
 * During testing:
 * - now_ms() returns mock_now_ms value instead of millis()
 * - Tests can advance time with setMockNowMs()
 * - Deterministic: same input → same output (no real clock)
 */

#include <cstdint>

// ============================================================================
// MOCK TIME STATE
// ============================================================================

// Global mock time value (in milliseconds)
// Updated by test harness to control virtual time progression
static unsigned long mock_now_ms_value = 0;

// ============================================================================
// PUBLIC API - Tests use these functions
// ============================================================================

/**
 * Get current mock time
 * @return Current virtual time in milliseconds
 *
 * Usage in tests:
 *   unsigned long now = getMockNowMs();
 */
unsigned long getMockNowMs(void) {
    return mock_now_ms_value;
}

/**
 * Set mock time to specific value
 * @param ms New virtual time in milliseconds
 *
 * Usage in tests:
 *   setMockNowMs(0);           // Start at time 0
 *   setMockNowMs(1000);        // Jump to 1 second
 *   setMockNowMs(300000);      // Jump to 5 minutes
 */
void setMockNowMs(unsigned long ms) {
    mock_now_ms_value = ms;
}

/**
 * Advance mock time by delta
 * @param delta_ms Milliseconds to advance
 *
 * Usage in tests:
 *   advanceMockNowMs(100);     // Advance 100ms
 *   advanceMockNowMs(300000);  // Advance 5 minutes
 */
void advanceMockNowMs(unsigned long delta_ms) {
    mock_now_ms_value += delta_ms;
}

/**
 * Reset mock time to 0
 *
 * Usage in tests (setUp):
 *   resetMockNowMs();
 */
void resetMockNowMs(void) {
    mock_now_ms_value = 0;
}

// ============================================================================
// WRAPPER - Replaces real millis() during testing
// ============================================================================

/**
 * Mock millis() - Used by production code during testing
 * When testing, this replaces the real millis() by linking to this implementation
 * instead of the Arduino library version.
 *
 * @return Current mock time in milliseconds
 */
unsigned long millis(void) {
    return mock_now_ms_value;
}

// ============================================================================
// CONSTANTS FOR TESTING
// ============================================================================

// Common test time values
static const unsigned long TIME_0_MS = 0;
static const unsigned long TIME_100_MS = 100;
static const unsigned long TIME_500_MS = 500;           // Echo prevention window
static const unsigned long TIME_1_SEC = 1000;
static const unsigned long TIME_5_SEC = 5000;
static const unsigned long TIME_10_SEC = 10000;
static const unsigned long TIME_30_SEC = 30000;         // Heartbeat interval
static const unsigned long TIME_1_MIN = 60000;
static const unsigned long TIME_5_MIN = 300000;         // Loco expiry timeout
static const unsigned long TIME_10_MIN = 600000;        // Address map refresh
static const unsigned long TIME_1_HOUR = 3600000;

/**
 * Get predefined test time constants
 * Usage:
 *   setMockNowMs(getTestTimeConstant(TEST_TIME_5_MIN));
 *
 * Note: Could be used for static initialization if needed
 */
unsigned long getTestTimeConstant(int constant_id) {
    switch (constant_id) {
        case 0: return TIME_0_MS;
        case 1: return TIME_100_MS;
        case 2: return TIME_500_MS;
        case 3: return TIME_1_SEC;
        case 4: return TIME_5_SEC;
        case 5: return TIME_10_SEC;
        case 6: return TIME_30_SEC;
        case 7: return TIME_1_MIN;
        case 8: return TIME_5_MIN;
        case 9: return TIME_10_MIN;
        case 10: return TIME_1_HOUR;
        default: return 0;
    }
}
