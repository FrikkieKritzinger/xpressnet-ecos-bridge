# Phase 4: Testing Infrastructure

**Objective**: Establish comprehensive unit and integration testing framework using PlatformIO + Unity.

**Scope**: Test core logic (parsers, command builders, state engine, router) in native/host environment without hardware.

**Timeline**: Build order follows dependency chain; tests runnable immediately after each module.

---

## Overview

### Why Phase 4?

Phases 1-3 implemented two complete protocol handlers (XpressNet, Ecos) with a central state engine and router. Before adding LocoNet/Z21, we need:

1. **Automated regression suite** — catches breaks when refactoring
2. **Deterministic timing tests** — echo prevention (500ms), loco expiry (5 min) require time control
3. **Network-free testing** — protocol logic without real hardware/WiFi
4. **CI/CD foundation** — infrastructure to validate future changes

### Architecture

```
Native (Host) Environment              Hardware (ESP8266)
─────────────────────────────────────────────────────────
tests/ (no Arduino libs)                xpressnet_ecos_bridge.ino
  ├── unit_*.cpp (PlatformIO native)      ├── Arduino framework
  ├── mock_*.cpp (fakes)                  ├── Real Serial1, WiFi
  └── test_*.cpp (runners)                └── Hardware timing
```

**Test Environment**:
- PlatformIO `native` environment (host CPU, not ESP8266)
- Unity test framework (ThrowTheSwitch)
- Custom mocks: `now_ms.h` (time seam), `MockProtocolInterface`
- No Arduino libs; code under test is protocol-agnostic logic only

**Integration Tests**:
- Mock Ecos server (Python) simulating TCP/XML protocol
- XpressNet simulator (bash/Python) for bus message injection
- Hardware checklist (manual procedures, cannot be automated)

---

## Implementation Order

### Phase 4.1: Foundation (Steps 1-3)

**Step 1: Install PlatformIO**
```bash
pip install platformio
pio --version  # Verify
```

**Step 2: Fix Blocker Issues**
- **`utils/debug.h`**: Wrap Arduino-only macros in `#ifdef ARDUINO`
  - `millis()`, `Serial.print()` guarded
  - Native tests use `#include <chrono>` instead
- **Remove vestigial includes**: Scan .cpp files for unused `#include <Arduino.h>`, `#include <ESP8266WiFi.h>` in logic-only files (parsers, state engine, router)

**Step 3: Add Time Seam**
- Create `utils/now_ms.h`
  - Function: `uint32_t now_ms()`
  - Production: calls `millis()`
  - Tests: injectible via test harness (see Step 4)
- Update `state_engine.cpp` to call `now_ms()` instead of `millis()` for expiry checks
- Update `command_router.cpp` to use `now_ms()` for echo-prevention window (500ms)

---

### Phase 4.2: Test Scaffold (Steps 4-5)

**Step 4: Create `platformio.ini` with Native Environment**

```ini
[platformio]
src_dir = xpressnet_ecos_bridge
include_dir = xpressnet_ecos_bridge

[env:native]
platform = native
test_framework = unity
test_dir = tests
build_flags = -DENABLE_TESTING=1
lib_deps =
    ThrowTheSwitch/Unity

[env:wemos]
platform = espressif8266
board = d1_mini
framework = arduino
; ... existing config ...
```

**Step 5: Create Test Directory Structure**

```
tests/
├── test_xpressnet_parser.cpp          XpressNet binary message parsing
├── test_ecos_parser.cpp                Ecos text protocol parsing
├── test_ecos_command_builder.cpp       Ecos command generation
├── test_state_engine.cpp               Loco state lifecycle (with time mock)
├── test_command_router.cpp             Echo prevention, routing (with time mock)
├── mocks/
│   ├── mock_protocol_interface.h       Fake ProtocolInterface for router tests
│   └── mock_now_ms.cpp                 Time injection for deterministic tests
└── fixtures/
    ├── xpressnet_messages.h            Hardcoded valid/invalid binary frames
    └── ecos_responses.h                Real Ecos TCP response samples
```

---

### Phase 4.3: Unit Tests (Steps 6-10)

**Step 6: XpressNet Message Parser Tests** (`test_xpressnet_parser.cpp`)

**Scope**: `xpressnet_message_parser.h/cpp`

**Test cases**:
- Valid speed command (forward, reverse, E-stop)
- Valid function command (F0-F7, F8-F15, F16-F23, F24-F31)
- Invalid checksum (should reject)
- Incomplete message (accumulator state machine)
- Throttle address parsing (short vs. long address)
- Boundary conditions (speed 0, speed 126, speed 127 E-stop)

**Example**:
```cpp
void test_xpressnet_speed_command_forward(void) {
  XpressNetMessageParser parser;
  uint8_t msg[] = {0x00, 100, 0x40, 0x40 ^ 0x00 ^ 100};
  XNetCommand cmd = parser.parse(msg, 4);
  
  TEST_ASSERT_EQUAL(COMMAND_SPEED, cmd.type);
  TEST_ASSERT_EQUAL(100, cmd.address);
  TEST_ASSERT_EQUAL(64, cmd.speed);
  TEST_ASSERT_EQUAL(DIRECTION_FORWARD, cmd.direction);
}
```

---

**Step 7: Ecos Message Parser Tests** (`test_ecos_parser.cpp`)

**Scope**: `ecos_message_parser.h/cpp`

**Test cases**:
- Valid reply frame (`<REPLY id="1" .../>`)
- Valid event frame (`<EVENT id="1" .../>`)
- Line accumulation (partial frames across calls)
- Key-value extraction (`speed[64]`, `func[0,1]`, `func[5,0]`)
- Malformed frames (missing tags, invalid XML)
- Empty/whitespace-only lines (should skip)

**Example**:
```cpp
void test_ecos_parse_speed_reply(void) {
  EcosMessageParser parser;
  const char* frame = "<REPLY id=\"1\"><GET id=\"10\" speed[64]><END />";
  parser.accumulate(frame, strlen(frame));
  
  EcosMessage msg = parser.parse();
  TEST_ASSERT_EQUAL(MESSAGE_REPLY, msg.type);
  TEST_ASSERT_EQUAL(64, msg.fields["speed"]);
}
```

---

**Step 8: Ecos Command Builder Tests** (`test_ecos_command_builder.cpp`)

**Scope**: `ecos_interface.h` (protocol builder methods)

**Test cases**:
- `buildRequest()` format: `request(id, view)` line
- `buildGet()` format: `get(id, addr, name)` line
- `buildSet()` format: `set(id, speed[n])` for speed
- `buildFunctionCommand()` format: `set(id, func[n,0|1])` per function
- `buildRelease()` format: `release(id, addr)` line
- Multi-line accumulation (commands queued until send)

**Example**:
```cpp
void test_ecos_build_speed_command(void) {
  EcosInterface ecos;
  ecos.buildSet(/* id= */ 1, /* speed= */ 64);
  
  const char* cmd = ecos.getQueuedCommand(0);
  TEST_ASSERT_EQUAL_STRING("set(1, speed[64])\n", cmd);
}
```

---

**Step 9: State Engine Tests** (`test_state_engine.cpp`)

**Scope**: `state_engine.h/cpp` + time seam (`now_ms.h`)

**Test cases**:
- Add loco (create new state)
- Update speed/direction/functions
- Retrieve loco by address
- Loco expiry (set time forward 5 min, verify removed)
- Max locos (50 limit, next add fails gracefully)
- All functions F0-F31 bitmap storage/retrieval

**Key**: Time mock enables deterministic expiry testing without waiting 5 minutes.

**Mock approach**:
```cpp
// In test harness:
extern uint32_t mock_now_ms;  // Global override
uint32_t now_ms() { return mock_now_ms; }

void test_loco_expiry(void) {
  StateEngine engine;
  engine.addLoco(100);  // mock_now_ms = 0
  
  mock_now_ms = 5 * 60 * 1000;  // Advance 5 minutes
  engine.update();
  
  TEST_ASSERT_FALSE(engine.hasLoco(100));  // Should be expired
}
```

---

**Step 10: Command Router Tests** (`test_command_router.cpp`)

**Scope**: `command_router.h/cpp` + mocks + time seam

**Test cases**:
- Route speed command: XpressNet → broadcast to all protocols
- Route function command: XpressNet → Ecos
- Echo prevention: Send XpressNet cmd, block duplicate within 500ms window
- Echo prevention: Echo older than 500ms allowed
- Unknown loco: Subscribe request to Ecos on first command
- Multiple throttles: Commands from different throttles don't interfere
- State consistency: Ecos update reflected back to XpressNet

**Mock**: `MockProtocolInterface` (stub that records calls):
```cpp
class MockProtocolInterface : public ProtocolInterface {
  public:
    void sendSpeedCommand(uint16_t addr, uint8_t speed, uint8_t dir) override {
      speed_sent_count++;
      last_speed_sent = speed;
    }
    int speed_sent_count = 0;
    uint8_t last_speed_sent = 0;
};

void test_echo_prevention_blocks_duplicate(void) {
  CommandRouter router;
  MockProtocolInterface mock_xnet, mock_ecos;
  
  router.setXnetInterface(&mock_xnet);
  router.setEcosInterface(&mock_ecos);
  
  // Send speed cmd from XNet
  router.handleXnetSpeedCommand(100, 64, DIRECTION_FORWARD);
  TEST_ASSERT_EQUAL(1, mock_ecos.speed_sent_count);
  
  // Duplicate within 500ms → blocked
  mock_now_ms += 250;
  router.handleXnetSpeedCommand(100, 64, DIRECTION_FORWARD);
  TEST_ASSERT_EQUAL(1, mock_ecos.speed_sent_count);  // Still 1, not 2
  
  // After 500ms → allowed
  mock_now_ms += 300;
  router.handleXnetSpeedCommand(100, 64, DIRECTION_FORWARD);
  TEST_ASSERT_EQUAL(2, mock_ecos.speed_sent_count);  // Now 2
}
```

---

### Phase 4.4: Integration Tests (Steps 11-12)

**Step 11: Mock Ecos Server** (`tests/mock_ecos_server.py`)

**Purpose**: Simulate ESU Ecos TCP endpoint without real hardware.

**Capabilities**:
- Listen on `localhost:15471`
- Accept `request(id, view)` → respond with object list
- Accept `queryObjects(10, addr, name)` → return object ID mapping
- Accept `set(id, speed[n])` → store state, echo via event
- Accept `get(id, ...)` → return current state
- Send periodic keep-alives

**Usage**:
```bash
python tests/mock_ecos_server.py &
# Run tests with real TCP traffic to mock server
pio test -e native --test-port=15471
```

---

**Step 12: Hardware Test Procedures** (`docs/06_HARDWARE_TEST_PROCEDURES.md`)

**Scope**: Manual checklist for real ESP8266 + XpressNet bus + real Ecos.

**Procedure 1: XpressNet Reception**
- Connect XpressNet master (e.g., Roco Multimaus)
- Send speed command to loco 100
- Verify: OLED displays speed update, logs show parsed message
- Repeat with function toggles F0-F31

**Procedure 2: Ecos Integration**
- Configure `config.h` with real Ecos IP
- Recompile, upload to Wemos D1 Mini
- Send XpressNet command to unknown loco (e.g., 50)
- Verify: Bridge queries Ecos for loco 50 object ID within 2 sec
- Verify: Speed sync from Ecos reflected to XpressNet

**Procedure 3: Multi-Protocol Consistency**
- Operate same loco from Ecos web UI + XpressNet throttle simultaneously
- Verify: State updates flow in both directions, no echo loops

**Procedure 4: Timeout/Recovery**
- Power-cycle Ecos
- Send XpressNet command
- Verify: Bridge detects disconnection, queues command, resumes after Ecos restarts

---

## File Manifest (Phase 4 Deliverables)

```
docs/
├── 04_PHASE_4_TESTING_INFRASTRUCTURE.md    (this file)
└── 06_HARDWARE_TEST_PROCEDURES.md          (Step 12 output)

platformio.ini                              (Step 4)

utils/
├── now_ms.h                                (Step 3)
└── debug.h                                 (Step 2, modified)

tests/
├── test_xpressnet_parser.cpp               (Step 6)
├── test_ecos_parser.cpp                    (Step 7)
├── test_ecos_command_builder.cpp           (Step 8)
├── test_state_engine.cpp                   (Step 9)
├── test_command_router.cpp                 (Step 10)
├── mocks/
│   ├── mock_protocol_interface.h           (Step 10)
│   └── mock_now_ms.cpp                     (Step 9)
├── fixtures/
│   ├── xpressnet_messages.h                (Step 6)
│   └── ecos_responses.h                    (Step 7)
└── mock_ecos_server.py                     (Step 11)
```

---

## Success Criteria

- [ ] **Step 1**: `pio --version` outputs 6.1.0+
- [ ] **Step 2**: Zero compiler warnings in native build
- [ ] **Step 3**: `now_ms()` called by state engine/router (verified via grep)
- [ ] **Step 4**: `pio run -e native` builds without errors
- [ ] **Step 5**: Test directory structure exists, empty test files compile
- [ ] **Step 6**: `test_xpressnet_parser` runs, 8+ tests pass
- [ ] **Step 7**: `test_ecos_parser` runs, 6+ tests pass
- [ ] **Step 8**: `test_ecos_command_builder` runs, 5+ tests pass
- [ ] **Step 9**: `test_state_engine` runs, expiry test deterministically passes (no 5-min wait)
- [ ] **Step 10**: `test_command_router` runs, echo prevention verified in 500ms window
- [ ] **Step 11**: Mock server starts, accepts TCP connections on 15471
- [ ] **Step 12**: Hardware procedures doc written, manual tests executed on real hardware
- [ ] **Final**: `pio test -e native` runs all 30+ tests, all pass

---

## Timeline & Dependencies

```
Step 1 (PlatformIO)
  ↓ (required for Steps 4-10)
Step 2 (Blockers)
  ↓ (enables native compilation)
Step 3 (Time Seam)
  ↓ (enables deterministic tests)
Steps 4-5 (Scaffold)
  ↓ (enables test authoring)
Steps 6-10 (Unit Tests) — can run in parallel
  ↓ (cumulative, each depends on previous modules)
Step 11 (Mock Ecos)
  ↓ (optional, used after all unit tests pass)
Step 12 (Hardware Procedures)
  ↓ (final validation, uses real hardware)

Estimated effort per step: 30 min (scaffold) → 2 hours (parser tests) → 1 hour (router tests)
Total: ~10-12 hours to completion
```

---

## Known Blockers & Mitigations

| Blocker | Root Cause | Mitigation |
|---------|-----------|-----------|
| Arduino.h guards | Undefined `millis()` in native build | Wrap in `#ifdef ARDUINO` (Step 2) |
| ESP8266WiFi.h includes | No WiFi stack in native env | Remove from parser/state_engine files (Step 2) |
| Time-dependent logic | Expiry tests would wait 5 min | Inject via `now_ms()` seam (Step 3) |
| No hardware for integration | Real Ecos unavailable in dev env | Mock server script (Step 11) |

---

## Next Steps (Phase 5+)

- **Phase 5**: LocoNet support (parallel to XpressNet, same architecture)
- **Phase 6**: Z21 LAN support
- **Phase 7**: Accessory decoder (turnout) support
- **Phase 8**: EEPROM storage, web config UI
- **Phase 9**: OTA firmware updates

---

**Last Updated**: 2026-07-22  
**Document Status**: APPROVED, ready for implementation
