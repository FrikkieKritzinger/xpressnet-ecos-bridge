# Unit Test Suite - XpressNet-Ecos Bridge

## Overview

Comprehensive unit test suite for the XpressNet-Ecos Bridge project. Tests are designed to run on the host platform (Linux/macOS/Windows) using PlatformIO's native environment, not on the actual ESP8266 hardware.

## Directory Structure

```
test/
├── README.md                              This file
├── test_state_engine/                     Loco state management tests
├── test_command_router/                   Protocol bridging, echo attribution, accessories, E-stop
├── test_ecos_parser/                      Ecos text protocol parsing tests
├── test_ecos_command_builder/             Ecos command generation tests
├── test_z21_protocol/                     Z21 packet encode/decode (incl. spec worked examples)
├── test_eeprom_config/                    Persisted-settings struct, defaults, checksum, validation
├── test_setup_web_form/                   Setup/update page HTML and field validation
├── mocks/
│   ├── mock_protocol_interface.h          Mock protocol interface for testing the router
│   └── mock_now_ms.h                      Mock time control (implementation: xpressnet_ecos_bridge/test_support/mock_now_ms.cpp)
├── native_stubs/
│   ├── Arduino.h                          Stand-in <Arduino.h> (Serial/ESP/millis stubs)
│   └── arduino_stub.cpp
└── fixtures/
    └── ecos_responses.h                   Valid/invalid Ecos text protocol responses
```

PlatformIO 6 hard-codes the test directory to `test/` and expects one directory per suite (`test_*`); there is no `test_dir` option.

XpressNet parsing is no longer unit-tested here - it's owned by the real
XpressNetMaster library (`libraries/XpressNetMaster`), which `xpressnet_interface.cpp`
wraps. That interface talks to real hardware (SoftwareSerial) so, like
`ecos_interface.cpp`, it's excluded from the native test build; it can only be
validated on the physical bus (Phase 4.6).

## Test Framework

Tests use the **Unity** testing framework (lightweight, designed for embedded systems).

### Dependencies
- PlatformIO (for compilation and running)
- C++11 compiler (g++ on Linux/macOS, MinGW on Windows)
- Unity test framework (included via platformio.ini)

## Building & Running Tests

### Compile native test build
```bash
# Compile all tests for host platform (not ESP8266)
platformio run -e native

# Verbose output
platformio run -e native -v

# Clean and rebuild
platformio run -e native --target clean
```

### Run tests
```bash
# Run compiled tests
platformio test -e native

# Specific test file
platformio test -e native --filter test_command_router
```

## Test Structure

Each test file follows this pattern:

```cpp
// setUp() - Run before each test
void setUp(void) {
    // Initialize state, mocks, fixtures
}

// Each test function
void test_<feature>_<scenario>(void) {
    // Arrange: Set up test data
    // Act: Call code under test
    // Assert: Verify results
}

// tearDown() - Run after each test
void tearDown(void) {
    // Clean up
}

// main() - Entry point
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_...);
    // ... more tests
    return UNITY_END();
}
```

## Test Categories

### 1. Ecos Parser Tests (`test_ecos_parser`)
- Text protocol parsing (key[value] extraction)
- Line accumulation (byte-by-byte input)
- Reply/Event framing
- Multi-line message handling
- Malformed message handling

**Key Fixtures**: `ecos_responses.h`

### 2. Ecos Command Builder Tests (`test_ecos_command_builder`)
- Speed command generation
- Function command generation
- Query command generation
- Subscribe/unsubscribe commands
- Input validation and bounds checking

### 3. State Engine Tests (`test_state_engine`)
- Loco addition and updates
- Loco expiry (5-minute timeout)
- Address lookup
- Capacity limits (50 locos max)
- Time-dependent behavior (uses `mock_now_ms`)

**Key Mock**: `mock_now_ms.h/cpp`

### 4. Command Router Tests (`test_command_router`)
- Protocol bridging (XpressNet ↔ Ecos ↔ Z21)
- Echo attribution (`wasRecentSource()`, 4 s window), per-destination fan-out
- Accessory/turnout routing and known-state cache, emergency stop/resume
- Subscription lifecycle
- Multi-throttle consistency
- Multi-interface routing

**Key Mock**: `mock_protocol_interface.h`, `mock_now_ms.h`

### 5. Z21 Protocol Tests (`test_z21_protocol`)
- Checksum, address/speed/function encode-decode, packet builders
- Byte-for-byte checks against the worked examples in the official Z21 LAN spec v1.13
- Turnout command decode (field order, Q bit)

### 6. EEPROM Config Tests (`test_eeprom_config`)
- Defaults seeding, checksum validation, version mismatch, completeness check (`eepromConfigIsComplete()`)

### 7. Setup Page Tests (`test_setup_web_form`)
- Config and update page HTML generation, field parsing and validation (IPs, timeouts in seconds -> ms)

## Mocks & Fixtures

### MockProtocolInterface
Implements `ProtocolInterface` for testing the router.

```cpp
MockProtocolInterface mock;
mock.setStatus(ComponentStatus::CONNECTED);
mock.sendSpeedCommand(100, 64, 1);

// Verify calls
assert(mock.getSpeedCommandCount() == 1);
assert(mock.getLastSpeedCommand().address == 100);
```

### Mock Time (mock_now_ms)
Provides deterministic time control for testing timeouts and expiry.

```cpp
resetMockNowMs();        // Start at t=0
setMockNowMs(100);       // Jump to t=100ms
advanceMockNowMs(1000);  // Advance to t=1100ms

// Loco 100 expires after 5 minutes of inactivity
setMockNowMs(0);
engine.addLoco(100);
setMockNowMs(300001);    // 5 min + 1ms
engine.expungeInactiveLocos();  // Loco 100 should be removed
```

### Fixtures
Pre-defined test data:

- **Ecos Responses** (`ecos_responses.h`)
  - Speed queries and events
  - Address map discovery
  - Multi-line messages
  - Malformed responses

## Running Individual Tests

```cpp
// In test file, only run certain tests
void test_important_case(void) {
    // This runs
}

void test_edge_case(void) {
    // This is skipped with TEST_IGNORE_MESSAGE
    TEST_IGNORE_MESSAGE("TODO: Implement");
}

// Use Unity's filtering
RUN_TEST(test_important_case);
// TEST(test_edge_case);  // Commented out = skipped
```

## Common Test Patterns

### Verify time-dependent behavior
```cpp
void test_loco_expiry(void) {
    resetMockNowMs();
    engine.addLoco(100);
    
    advanceMockNowMs(300000 + 1);  // 5 min + 1ms
    int removed = engine.expungeInactiveLocos();
    
    TEST_ASSERT_EQUAL_INT(removed, 1);
}
```

### Verify echo prevention
```cpp
void test_echo_suppression(void) {
    MockProtocolInterface ecos;
    router.setEcosInterface(&ecos);
    
    router.handleXpressNetSpeedCommand(100, 64, 1);
    TEST_ASSERT_EQUAL_INT(ecos.getSpeedCommandCount(), 1);
    
    // Simulate Ecos's confirmation inside the attribution window (ECOS_ECHO_ATTRIBUTION_WINDOW_MS)
    router.handleEcosSpeedCommand(100, 64, 1);
    TEST_ASSERT_EQUAL_INT(ecos.getSpeedCommandCount(), 1);  // Not incremented
}
```

## Debugging Tests

### Print debug output
```cpp
// In any test
TEST_MESSAGE("Debug output here");
TEST_PRINTF("Loco address: %u\n", address);
```

### Check memory during tests
```cpp
// Unity can track memory allocations (if using heap)
TEST_ASSERT_EQUAL_INT(expected, actual);  // Will show memory delta
```

### Run with verbose output
```bash
platformio test -e native --verbose
```

## Status

All suites are implemented and passing: **287 tests** in 7 suites (`python -m platformio test -e native`). Hardware-coupled files (`ecos_interface.cpp`, `xpressnet_interface.cpp`, `z21lan_interface.cpp`, `eeprom_store.cpp`, `setup_mode.cpp`, the OLED code) are excluded from the native build - see `platformio.ini` - and are verified on the real board instead. Release history and what each live test found: [../docs/CHANGELOG.md](../docs/CHANGELOG.md).

## References

- [Unity Test Framework Docs](http://www.throwtheswitch.org/unity)
- [PlatformIO Testing](https://docs.platformio.org/en/latest/frameworks/arduino.html#testing)
- Original test plan (with a status note): `docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md`
