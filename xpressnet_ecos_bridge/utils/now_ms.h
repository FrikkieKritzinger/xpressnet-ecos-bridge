/*
 * Time Abstraction Seam - now_ms()
 *
 * Provides a single point of abstraction for time queries.
 *
 * In production (Arduino): calls millis()
 * In testing (native): uses injected mock time
 *
 * This allows deterministic testing of timing-dependent logic:
 * - Echo prevention (500ms window)
 * - Loco expiry (5 minute timeout)
 *
 * Usage:
 *   uint32_t current_time = now_ms();
 *   if (current_time - last_timestamp > TIMEOUT_MS) { ... }
 */

#ifndef UTILS_NOW_MS_H
#define UTILS_NOW_MS_H

#include <cstdint>

#ifdef ARDUINO

/**
 * Get current time in milliseconds (Arduino)
 * Calls the built-in millis() function.
 *
 * @return Milliseconds since device startup
 */
inline uint32_t now_ms() {
    return millis();
}

#else  // Native/Test Environment

/**
 * Get current time in milliseconds (Native/Test)
 *
 * In production code, this is never called.
 * In tests, this uses the injected mock_now_ms value,
 * allowing deterministic control of time.
 */
extern uint32_t mock_now_ms;

inline uint32_t now_ms() {
    return mock_now_ms;
}

#endif  // ARDUINO

#endif  // UTILS_NOW_MS_H
