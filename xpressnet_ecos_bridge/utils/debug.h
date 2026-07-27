/*
 * Debug Utilities - Conditional Debug Output
 * 
 * Provides printf-style debug logging that can be compiled out entirely.
 * All debug output is conditional on ENABLE_DEBUG setting in config.h
 * 
 * When ENABLE_DEBUG = 0, debug statements compile to nothing (zero overhead).
 * When ENABLE_DEBUG = 1, full debug output is available.
 * 
 * Usage:
 *   DEBUG_PRINT("Simple message\n");
 *   DEBUG_PRINTF("Value: %d\n", value);
 *   DEBUG_XNET("XNet message\n");  // Only if DEBUG_XNET enabled
 *   DEBUG_ECOS("Ecos: %s\n", xml_string);
 */

#ifndef UTILS_DEBUG_H
#define UTILS_DEBUG_H

#include <cstdio>
#include <cstdarg>
#include "../config.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

// ============================================================================
// DEBUG MACROS - Main debug output (controlled by ENABLE_DEBUG)
// ============================================================================

#if ENABLE_DEBUG

#ifdef ARDUINO
    /**
     * Debug print with newline
     * Usage: DEBUG_PRINT("Message\n");
     */
    #define DEBUG_PRINT(msg) \
        do { Serial.print(msg); } while(0)

    /**
     * Debug printf (formatted output)
     * Usage: DEBUG_PRINTF("Value: %d\n", value);
     */
    #define DEBUG_PRINTF(fmt, ...) \
        do { \
            char _debug_buf[256]; \
            snprintf(_debug_buf, sizeof(_debug_buf), fmt, ##__VA_ARGS__); \
            Serial.print(_debug_buf); \
        } while(0)

    /**
     * Debug print with timestamp
     * Useful to see timing of events
     * Usage: DEBUG_PRINT_TS("Event occurred\n");
     */
    #define DEBUG_PRINT_TS(msg) \
        do { \
            Serial.print("["); \
            Serial.print(millis()); \
            Serial.print("ms] "); \
            Serial.print(msg); \
        } while(0)

    /**
     * Debug printf with timestamp
     * Usage: DEBUG_PRINTF_TS("Value: %d\n", value);
     */
    #define DEBUG_PRINTF_TS(fmt, ...) \
        do { \
            char _debug_buf[256]; \
            snprintf(_debug_buf, sizeof(_debug_buf), fmt, ##__VA_ARGS__); \
            Serial.print("["); \
            Serial.print(millis()); \
            Serial.print("ms] "); \
            Serial.print(_debug_buf); \
        } while(0)
#else  // !ARDUINO (native/test environment)
    // When compiling natively (not Arduino), debug output goes to stdout
    #define DEBUG_PRINT(msg) \
        do { fprintf(stdout, "%s", msg); fflush(stdout); } while(0)

    #define DEBUG_PRINTF(fmt, ...) \
        do { fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout); } while(0)

    #define DEBUG_PRINT_TS(msg) \
        do { fprintf(stdout, "[ts] %s", msg); fflush(stdout); } while(0)

    #define DEBUG_PRINTF_TS(fmt, ...) \
        do { fprintf(stdout, "[ts] " fmt, ##__VA_ARGS__); fflush(stdout); } while(0)
#endif

#else  // !ENABLE_DEBUG

    // When debug disabled, macros expand to nothing (no code generated)
    #define DEBUG_PRINT(msg) do {} while(0)
    #define DEBUG_PRINTF(fmt, ...) do {} while(0)
    #define DEBUG_PRINT_TS(msg) do {} while(0)
    #define DEBUG_PRINTF_TS(fmt, ...) do {} while(0)

#endif  // ENABLE_DEBUG

// ============================================================================
// SELECTIVE DEBUG CATEGORIES
//
// These only expand to DEBUG_PRINT/DEBUG_PRINTF, which are already defined
// per-platform above (Serial on Arduino, stdout natively) - so this section
// is NOT guarded by #ifdef ARDUINO and applies to both.
// ============================================================================

#if ENABLE_DEBUG && DEBUG_STARTUP
    #define DEBUG_STARTUP_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_STARTUP_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_STARTUP_PRINT(msg) do {} while(0)
    #define DEBUG_STARTUP_PRINTF(fmt, ...) do {} while(0)
#endif

#if ENABLE_DEBUG && DEBUG_XPRESSNET
    #define DEBUG_XNET_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_XNET_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
    #define DEBUG_XNET_PRINT_TS(msg) DEBUG_PRINT_TS(msg)
#else
    #define DEBUG_XNET_PRINT(msg) do {} while(0)
    #define DEBUG_XNET_PRINTF(fmt, ...) do {} while(0)
    #define DEBUG_XNET_PRINT_TS(msg) do {} while(0)
#endif

#if ENABLE_DEBUG && DEBUG_ECOS
    #define DEBUG_ECOS_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_ECOS_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
    #define DEBUG_ECOS_PRINT_TS(msg) DEBUG_PRINT_TS(msg)
#else
    #define DEBUG_ECOS_PRINT(msg) do {} while(0)
    #define DEBUG_ECOS_PRINTF(fmt, ...) do {} while(0)
    #define DEBUG_ECOS_PRINT_TS(msg) do {} while(0)
#endif

#if ENABLE_DEBUG && DEBUG_STATE_ENGINE
    #define DEBUG_STATE_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_STATE_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_STATE_PRINT(msg) do {} while(0)
    #define DEBUG_STATE_PRINTF(fmt, ...) do {} while(0)
#endif

#if ENABLE_DEBUG && DEBUG_ECHO_PREVENTION
    #define DEBUG_ECHO_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_ECHO_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_ECHO_PRINT(msg) do {} while(0)
    #define DEBUG_ECHO_PRINTF(fmt, ...) do {} while(0)
#endif

#if ENABLE_DEBUG && DEBUG_TIMING
    #define DEBUG_TIMING_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_TIMING_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_TIMING_PRINT(msg) do {} while(0)
    #define DEBUG_TIMING_PRINTF(fmt, ...) do {} while(0)
#endif

#if ENABLE_DEBUG && DEBUG_MEMORY
    #define DEBUG_MEMORY_PRINT(msg) DEBUG_PRINT(msg)
    #define DEBUG_MEMORY_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_MEMORY_PRINT(msg) do {} while(0)
    #define DEBUG_MEMORY_PRINTF(fmt, ...) do {} while(0)
#endif

// ============================================================================
// ASSERTION MACROS (Arduino only, Debugging aid)
// ============================================================================

#ifdef ARDUINO
#if ENABLE_DEBUG

    /**
     * Assert that condition is true
     * If false, prints error and location
     * Usage: ASSERT(value > 0, "Value must be positive");
     */
    #define ASSERT(condition, message) \
        do { \
            if (!(condition)) { \
                Serial.print("ASSERT FAILED: "); \
                Serial.print(message); \
                Serial.print(" at "); \
                Serial.print(__FILE__); \
                Serial.print(":"); \
                Serial.println(__LINE__); \
            } \
        } while(0)

    /**
     * Assert that condition is true (no message)
     * Usage: ASSERT_TRUE(value == expected);
     */
    #define ASSERT_TRUE(condition) \
        do { \
            if (!(condition)) { \
                Serial.print("ASSERT FAILED at "); \
                Serial.print(__FILE__); \
                Serial.print(":"); \
                Serial.println(__LINE__); \
            } \
        } while(0)

    /**
     * Assert that condition is false
     * Usage: ASSERT_FALSE(error_flag);
     */
    #define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#else
    #define ASSERT(condition, message) do {} while(0)
    #define ASSERT_TRUE(condition) do {} while(0)
    #define ASSERT_FALSE(condition) do {} while(0)
#endif
#else  // !ARDUINO
    #define ASSERT(condition, message) do {} while(0)
    #define ASSERT_TRUE(condition) do {} while(0)
    #define ASSERT_FALSE(condition) do {} while(0)
#endif

// ============================================================================
// HELPER FUNCTIONS (Arduino only)
// ============================================================================

#ifdef ARDUINO
#if ENABLE_DEBUG

/**
 * Print a buffer as hex bytes (Arduino only)
 * Useful for debugging binary data
 */
inline void debugPrintHex(const uint8_t* buffer, size_t length, const char* label = nullptr) {
    if (label) {
        Serial.print(label);
        Serial.print(": ");
    }
    for (size_t i = 0; i < length; i++) {
        if (buffer[i] < 0x10) Serial.print("0");
        Serial.print(buffer[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

/**
 * Print free heap memory (Arduino only)
 * Useful to detect memory leaks
 */
inline void debugPrintHeap(const char* label = nullptr) {
    if (label) {
        Serial.print(label);
        Serial.print(": ");
    }
    Serial.print("Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
}

/**
 * Print a formatted debug section header (Arduino only)
 * Makes debug output easier to read
 */
#define DEBUG_SECTION_START(name) \
    do { \
        Serial.println(); \
        Serial.println("=== " name " ==="); \
    } while(0)

#define DEBUG_SECTION_END(name) \
    do { \
        Serial.println("=== " name " DONE ==="); \
    } while(0)

#else  // !ENABLE_DEBUG
    inline void debugPrintHex(const uint8_t* buffer, size_t length, const char* label = nullptr) {
        (void)buffer; (void)length; (void)label;
    }
    inline void debugPrintHeap(const char* label = nullptr) {
        (void)label;
    }
    #define DEBUG_SECTION_START(name) do {} while(0)
    #define DEBUG_SECTION_END(name) do {} while(0)
#endif

#else  // !ARDUINO (native/test environment)
    // Stub implementations for native compilation
    inline void debugPrintHex(const uint8_t* buffer, size_t length, const char* label = nullptr) {
        (void)buffer; (void)length; (void)label;
    }
    inline void debugPrintHeap(const char* label = nullptr) {
        (void)label;
    }
    #define DEBUG_SECTION_START(name) do {} while(0)
    #define DEBUG_SECTION_END(name) do {} while(0)
#endif

#endif  // UTILS_DEBUG_H
