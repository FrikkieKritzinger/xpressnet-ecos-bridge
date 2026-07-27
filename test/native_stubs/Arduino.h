/*
 * Native Arduino Compatibility Stub - Testing support
 *
 * Production code (definitions.h, interface_base.h, utils/memory.h,
 * utils/timing.h, and a few .cpp files) unconditionally includes
 * <Arduino.h>. On native (host) builds there is no real Arduino core,
 * so this stub stands in for it - only for env:native, via -I on the
 * include path (see platformio.ini).
 *
 * Deliberately does NOT define ARDUINO: utils/now_ms.h branches on that
 * macro to pick mock_now_ms() instead of millis() for deterministic
 * test time, and that branching must keep working here.
 *
 * millis() itself is declared (not defined) here - the real definition
 * lives in tests/mocks/mock_now_ms.cpp so both direct millis() callers
 * (utils/timing.h) and now_ms() callers share one mock clock.
 */

#ifndef NATIVE_STUB_ARDUINO_H
#define NATIVE_STUB_ARDUINO_H

#include <cstdint>
#include <cstdio>
#include <cstdarg>

unsigned long millis(void);

struct NativeSerialStub {
    void begin(unsigned long) {}
    void println(const char* s) { std::fprintf(stdout, "%s\n", s); }
    void print(const char* s) { std::fprintf(stdout, "%s", s); }
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::vfprintf(stdout, fmt, args);
        va_end(args);
    }
};
extern NativeSerialStub Serial;

struct NativeEspStub {
    uint32_t getFreeHeap() { return 100000; }
    uint32_t getMaxFreeBlockSize() { return 50000; }
    uint32_t getCpuFreqMHz() { return 160; }
    uint32_t getSketchSize() { return 300000; }
    uint32_t getFreeSketchSpace() { return 700000; }
};
extern NativeEspStub ESP;

#endif  // NATIVE_STUB_ARDUINO_H
