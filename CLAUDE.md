# XpressNet-Ecos Bridge - Development Guide

**Project**: Model railway protocol bridge for ESP8266 (Wemos D1 Mini)  
**Purpose**: Translate commands between XpressNet (hardwired RS485) and ESU Ecos (WiFi TCP/XML)  
**Language**: C++ (Arduino IDE 1.8+)  
**Target**: ESP8266 microcontroller (160MHz, 4MB Flash, 160KB RAM)

---

## 📝 Recent Updates (This Session)

**2026-07-22 — Phase 3.2 Implementation Complete**
- ✅ Implemented full Ecos LAN WiFi TCP protocol (1700+ lines)
- ✅ Corrected protocol assumption: XML → real ESU text-based object protocol
- ✅ Built message parser (line/block accumulation, key[value] extraction)
- ✅ Implemented command builders (request, set, get, release, queryObjects)
- ✅ Integrated with CommandRouter (echo prevention, subscriptions, loco expiry)
- ✅ Updated state_engine.h/cpp (capture removed locos on expiry)
- ✅ Updated definitions.h (added ecos_object_id to LocoState)
- ✅ Corrected design doc (protocol section: XML → text-based)
- **Commit 4c3e0d6**: Phase 3.2 implementation
- **Commit e405a04**: CLAUDE.md status update

**2026-07-22 — Phase 4 Testing Infrastructure Planned**
- ✅ Designed comprehensive testing strategy (PlatformIO + Unity)
- ✅ Discovered Python 3.10 + pip available in this environment
- ✅ Updated approach: Install PlatformIO here, compile & run native tests for real
- ✅ Identified blocker issues (debug.h Arduino guard, vestigial includes)
- ✅ Designed time abstraction seam (utils/now_ms.h) for deterministic testing
- ✅ Planned 5 core unit test files + mock Ecos server + hardware procedures doc
- **Key Decision**: Native test suite will actually pass in this environment (not just verified by reading)
- **Next**: Implement Phase 4 (start with PlatformIO installation, then write/run tests)

**2026-07-22 — Phase 4.1: Foundation & Testing Infrastructure Setup ✅ COMPLETE**
- ✅ **Step 1**: Installed PlatformIO 6.1.19 via `pip install platformio`
- ✅ **Step 2**: Fixed blocker issues
  * Guarded Arduino-only code in `utils/debug.h` (#ifdef ARDUINO)
  * Removed Arduino.h from `state_engine.h` and `command_router.h` (pure logic files)
  * Added native/test fallback: `fprintf(stdout)` for debug output when not Arduino
- ✅ **Step 3**: Created time seam abstraction (`utils/now_ms.h`)
  * Arduino: wraps `millis()` for real hardware
  * Tests: uses injected `mock_now_ms` global for deterministic time control
  * Updated `state_engine.cpp`: 5 millis() → now_ms() calls
  * Updated `command_router.cpp`: 10 millis() → now_ms() calls
- ✅ **Step 4**: Created `platformio.ini` with three environments
  * `env:wemos` — ESP8266 production build
  * `env:native` — Host CPU compilation for testing (requires g++/MinGW)
  * `env:debug` — ESP8266 with debug output enabled
- **Note**: Native compilation deferred (g++ not in PATH on Windows)
- **Commits**: 521ae62 (design doc), 2d89d64 (foundation), ae6b990 (CLAUDE.md update)
- **Status**: Phase 4.1 foundation complete, paused for later resumption

**2026-07-24 — Phase 4.1 Build Verification & Compilation Fixes ✅ VERIFIED**
- ✅ **First build test** revealed 8 compilation issues that needed fixing:
  1. Macro redefinition conflicts (MAX_ECOS_OBJECTS, MAX_PENDING_QUERIES, MAX_OUTGOING_QUEUE)
     - Removed static const redeclarations in ecos_interface.h that conflicted with config.h macros
  2. Missing include: Added `#include "../../utils/timing.h"` to ecos_interface.h
  3. TimedTask: Added default constructor for member variable initialization
  4. Serial1.begin(): Fixed parameter order (use SERIAL_FULL instead of GPIO pin)
  5. Format specifiers: Fixed %lx → %x and %lu → %u for uint32_t throughout codebase
  6. Member initialization order: Reordered EcosInterface constructor to match declaration order
  7. Function signature: Fixed expungeInactiveLocos() to include optional parameters
- ✅ **Clean build achieved**:
  * ESP8266 (wemos) target: **PASS** (no errors, no warnings)
  * Memory usage: 43.4% RAM (35588 / 81920 bytes), 30.0% Flash (313215 / 1044464 bytes)
  * Excellent headroom for testing framework additions
- **Commit 7211782**: Fix compilation issues
- **Tag v0.4.1-clean-build**: Versioned clean build pushed to GitHub
- **Status**: Foundation code now verified to compile cleanly, ready for Phase 4.2

**2026-07-24 — Phase 4.2: Test Scaffold ✅ COMPLETE**
- ✅ **Directory structure created**: `tests/` with `mocks/` and `fixtures/` subdirectories
- ✅ **MockProtocolInterface** implemented
  * Derives from ProtocolInterface (sendSpeedCommand, sendFunctionCommand)
  * Tracks all method calls and parameters
  * Query API for test assertions (getSpeedCommandCount, getLastSpeedCommand, etc)
  * Manual status control for testing connected/disconnected states
- ✅ **Mock Time (mock_now_ms)** implemented
  * Deterministic time control: getMockNowMs, setMockNowMs, advanceMockNowMs
  * Replaces Arduino millis() during testing
  * Pre-defined constants for common durations (500ms echo window, 5min expiry, etc)
  * Enables testing of time-dependent behavior (timeouts, expiry, heartbeats)
- ✅ **Test fixtures** created
  * `xpressnet_messages.h`: Valid/invalid binary messages (speed, function, e-stop, checksums)
  * `ecos_responses.h`: Valid/invalid text protocol responses (queries, events, malformed)
- ✅ **Test template files** created (5 core tests)
  * `test_xpressnet_parser.cpp`: Message parsing, checksum validation
  * `test_ecos_parser.cpp`: Text protocol parsing, line accumulation, framing
  * `test_ecos_command_builder.cpp`: Command generation, validation
  * `test_state_engine.cpp`: State management, expiry, capacity limits
  * `test_command_router.cpp`: Bridging, echo prevention, subscriptions, routing
- ✅ **Documentation**: `tests/README.md` (comprehensive guide to test framework)
- **Commit 9543dd1**: Phase 4.2 test scaffold complete
- **Status**: Test scaffold complete with all templates and mocks ready. 11 new files (~1500 lines).

---

## 🎯 Phase 4.3: Unit Test Implementation (READY TO START)

**Test scaffold complete. Ready to implement actual test cases.**

**Next Steps for Phase 4.3-4.5**:

1. Implement actual test cases in each template file:
   - Fill in TODO sections with assertions and test logic
   - Use Unity testing macros (TEST_ASSERT_EQUAL, etc)
   - Follow Arrange-Act-Assert pattern

2. Configure Unity framework in `platformio.ini`
   - Add test dependencies
   - Configure test build environment

3. Compile and run tests:
   - `platformio run -e native` (compile)
   - `platformio test -e native` (run)

4. Iterate: Fix failures, add more test cases, achieve >90% coverage

**Files to reference**:
- Design: `docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md`
- Test guide: `tests/README.md` (patterns, examples, debugging)
- Fixtures: `tests/fixtures/` (valid/invalid test data)
- Mocks: `tests/mocks/` (mock interfaces for testing)

---

---

## Architecture Overview

### Layered Design
```
Application Layer  → State Engine (loco state), Command Router (bridging)
Protocol Layer     → XpressNet, Ecos, LocoNet (future), Z21 (future)
Display Layer      → OLED driver (128x64 SSD1306 I2C)
Utilities          → Timing (non-blocking timers), Debug, Memory helpers
```

### Key Components

**State Engine** (`state_engine.h/cpp`)
- In-memory tracking of up to 50 locomotives
- Stores: DCC address, speed (0-126), direction, functions (F0-F31), metadata
- Auto-expiry after 5 minutes of inactivity
- Fast O(n) lookup by address

**Command Router** (`command_router.h/cpp`)
- Central hub routing commands between protocols
- Manages echo prevention (500ms window)
- Handles unknown loco subscription requests to Ecos
- Auto-broadcasts state changes to all active protocols

**Protocol Interfaces** (inherit from `ProtocolInterface`)
- **XpressNet** (`protocols/xpressnet/`): Master device on RS485, accepts throttle commands
- **Ecos** (`protocols/ecos/`): TCP client over WiFi, XML protocol (Phase 3 Part 2)
- **LocoNet** (`protocols/loconet/`): Future
- **Z21** (`protocols/z21lan/`): Future

### Configuration
Single source of truth: **`config.h`**
- Feature enable/disable (compile-time toggles)
- Pin assignments (RX, TX, DE, SCL, SDA, etc.)
- Timing constants (timeouts, poll intervals, heartbeats)
- WiFi credentials (SSID, password)
- Ecos IP/port
- Debug flags per component

All disabled features = zero compiled code overhead.

---

## Development Status

### Phase 1: Architecture ✅ COMPLETE
- Design document (`docs/01_DESIGN_DOCUMENT.md`)
- Interface specifications
- Module structure

### Phase 2: Skeleton & Display ✅ COMPLETE
- Base classes and stubs for all protocols
- State engine fully implemented
- Command router fully implemented
- OLED display driver fully implemented (4-page rotating status, popup messages)

### Phase 3: Protocol Implementations ⏳ IN PROGRESS

**Phase 3.1: XpressNet Master** ✅ COMPLETE
- Binary message parsing (9600 baud RS485)
- Speed/direction command reception (0-126 speed)
- Emergency stop (speed 127)
- Function toggles F0-F31
- Echo prevention integration (queue-based, 500ms window)
- Bus timeout detection (5 sec)
- Non-blocking serial I/O (message accumulation state machine)
- Status tracking (throttle count, device detection)
- **Files**: `xpressnet_interface.h/cpp`, `xpressnet_message_parser.h/cpp`
- **Commit**: 5205b94

**Phase 3.2: Ecos LAN Protocol** ✅ COMPLETE
- **Implementation**: Commit 4c3e0d6 (1703 insertions)
- **Files**: ecos_protocol.h/cpp, ecos_message_parser.h/cpp, ecos_interface.cpp (full header)
- **Design Document**: `docs/02_PHASE_3_2_ECOS_DESIGN.md` (corrected from XML assumption)
- TCP connection over WiFi with exponential backoff resilience (5s → 60s)
- Text-based protocol parsing (NOT XML) — corrected; real ESU Ecos object protocol
  * Line-based: `request(id, view)`, `set(id, speed[64])`, `<EVENT>/<REPLY>/<END>` framing
  * DCC address ↔ Ecos object ID mapping via `queryObjects(10, addr, name)`
- Loco status queries on-demand (queryObjects at startup, refresh every 10 min)
- Subscribe-only-known strategy: subscribe on first XpressNet cmd, unsubscribe on expiry
- Function state synchronization (F0-F31 bitmap, `func[n,0|1]` wire format)
- Echo prevention: Circular queue (10 entries, 2-sec window accounts for TCP latency)
- Pending query buffer (5 entries): Commands while object ID resolution in flight
- Activity-based lifecycle: 5-minute inactivity expiry, auto-resubscribe on next update
- Multi-throttle consistency: Broadcasts Ecos updates back to XpressNet (via CommandRouter)
- Heartbeat/keep-alive (30 sec query), non-blocking polling with yield()
- Outgoing queue (10 entries) for buffering during WiFi/TCP disconnect
- OLED display: Connection status, IP:port, uptime (diagnostics only, no per-loco sync)
- **Architecture**: State engine is truth (second only to Ecos). XpressNet priority, Ecos backfill.
- **Scope**: Locomotives only (accessories deferred until XpressNet supports them)
- **Commit Date**: 2026-07-22

**Phase 3.3: LocoNet** (Future)

**Phase 3.4: Z21 LAN** (Future)

### Phase 4: Testing Infrastructure ⏳ IN PROGRESS

**Phase 4.1: Foundation & Setup** ✅ COMPLETE
- **Step 1**: Installed PlatformIO 6.1.19
- **Step 2**: Fixed blocker issues
  * Guarded Arduino-only code in `utils/debug.h`
  * Removed unnecessary `#include <Arduino.h>` from `state_engine.h`, `command_router.h`
- **Step 3**: Created time seam (`utils/now_ms.h`)
  * Arduino: wraps `millis()` for real hardware
  * Tests: uses injected `mock_now_ms` for deterministic control
  * Updated state_engine.cpp and command_router.cpp to use `now_ms()`
- **Step 4**: Created `platformio.ini` with three environments
  * `env:wemos` — ESP8266 production
  * `env:native` — Host compilation for testing (requires g++/MinGW)
  * `env:debug` — ESP8266 with debug enabled
- **Build Verification** (2026-07-24): ✅ Clean build achieved
  * Fixed 8 compilation issues (macro conflicts, missing includes, format specifiers)
  * ESP8266 (wemos) target: PASS (no errors, no warnings)
  * Memory: 43.4% RAM, 30.0% Flash (excellent headroom)
  * Tagged v0.4.1-clean-build, pushed to GitHub
- **Design Document**: `docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md`

**Phase 4.2: Test Scaffold** ✅ COMPLETE
- **Step 1**: Created tests/ directory structure
  * mocks/ (MockProtocolInterface, mock_now_ms)
  * fixtures/ (xpressnet_messages, ecos_responses)
- **Step 2**: Implemented MockProtocolInterface
  * Tracks sendSpeedCommand and sendFunctionCommand calls
  * Query API for test verification
  * Manual status control
- **Step 3**: Implemented mock time (mock_now_ms)
  * Deterministic time control for testing timeouts and expiry
  * Replaces Arduino millis() during testing
- **Step 4**: Created test fixtures
  * xpressnet_messages.h: Valid/invalid binary messages
  * ecos_responses.h: Valid/invalid text protocol responses
- **Step 5**: Created test template files (5 core tests)
  * test_xpressnet_parser.cpp, test_ecos_parser.cpp, test_ecos_command_builder.cpp
  * test_state_engine.cpp, test_command_router.cpp
  * All include TODO comments and test structure
- **Documentation**: tests/README.md (comprehensive test guide)
- **Status**: All scaffolding complete, ready for test implementation

**Phase 4.3-4.5: Unit Test Implementation** 🚀 READY TO START
- 5 core unit test files (parsers, builders, state engine, router)
- Deterministic expiry/echo testing via time mock
- Mock Ecos server (Python) for integration

**Phase 4.6: Hardware Procedures** (Pending)
- Manual test checklist for real ESP8266 + XpressNet bus

---

## Code Style & Conventions

### Naming
- **Files**: snake_case (e.g., `xpressnet_interface.cpp`)
- **Classes**: PascalCase (e.g., `XpressNetInterface`, `StateEngine`)
- **Methods**: camelCase (e.g., `processIncomingMessage()`)
- **Constants**: SCREAMING_SNAKE_CASE (e.g., `MAX_LOCOS`, `XPRESSNET_TIMEOUT`)
- **Local variables**: snake_case (e.g., `msg_buffer`, `last_message_time`)

### Comments
- No comments for obvious code (well-named identifiers are self-documenting)
- Add comments ONLY for non-obvious logic:
  - Hidden constraints or invariants
  - Workarounds for specific bugs
  - Subtle protocol details (e.g., "XOR checksum includes all bytes except checksum itself")
  - Performance-critical sections
- Single-line comments max (// style, not /* */ blocks)
- Multi-paragraph comments do NOT exist in this codebase

### Structure
- Public methods first, private methods/members last
- Virtual methods in public section
- Initialization in constructor, cleanup in destructor
- Static methods for utility functions (no instances needed)

### Memory Management
- No dynamic allocation (no `new`/`delete`)
- Fixed-size arrays (MAX_LOCOS = 50, MAX_XNET_MESSAGE_LENGTH = 32)
- Stack-allocated objects (structs like `LocoState`, `XNetCommand`)
- Total heap usage: ~2KB (mostly WiFi stack on ESP8266)

### Error Handling
- Validate inputs at system boundaries (user config, serial data, network packets)
- Trust internal code (state engine guarantees, interface contracts)
- Return bool for success/failure, NO exceptions
- Use DEBUG_PRINTF for diagnostic output (compiled out if ENABLE_DEBUG=0)

### Non-Blocking Design
- All `update()` methods MUST be non-blocking
- No `delay()` in main loop (only in initialization)
- XpressNet update() is HIGHEST priority (timing-critical ~20-50ms windows)
- Ecos/LocoNet update() next (network, ~100ms tolerance)
- Display update() lowest priority (~500ms cycle)
- Use cooperative multitasking: `yield()` between protocol updates

---

## Hardware Configuration (config.h)

### XpressNet (RS485 half-duplex)
```
RX: D7 (pin 13, Serial1 RX)
TX: D8 (pin 15, Serial1 TX)
DE: D5 (pin 14) - Driver Enable (HIGH=transmit, LOW=receive)
RE: D6 (pin 12) - Receiver Enable (tied to ground = always enabled)
Baud: 9600 (DO NOT CHANGE - XpressNet standard)
```

### Ecos (WiFi TCP)
```
SSID: "HOMER" (config.h, currently hardcoded)
Password: "REDACTED-WIFI-PASSWORD" (config.h, currently hardcoded)
Ecos IP: 192.168.1.100 (config.h)
Ecos Port: 15471 (standard, do not change)
```

### OLED Display (I2C)
```
SDA: D2 (pin 4)
SCL: D1 (pin 5)
Address: 0x3C (or 0x3D, try both if display not detected)
Size: 128x64 pixels
```

### Serial Monitor (Debug)
```
Baud: 115200 (DEBUG_BAUD in config.h)
Use Arduino IDE Serial Monitor or compatible
```

---

## Protocol Details

### XpressNet (Master Device, Receiver)

**Message Format** (4 bytes minimum):
```
[Address High] [Address Low] [Data] [Checksum]
```

**Speed Command**:
- Bit 7 of Data byte: Direction (0=forward, 1=reverse) [inverted from our convention]
- Bits 6-0: Speed (0-126, 127=E-stop)

**Function Command**:
- Separate packet per 8 functions (F0-F7, F8-F15, F16-F23, F24-F31)
- Each bit = one function state (1=on, 0=off)

**Checksum**:
- XOR of all bytes except checksum
- Must validate before accepting message

**Throttle Addressing**:
- Short address (1-99): bit 6 of high byte set
- Long address (100-9999): 14-bit field

**Emergency Stop**:
- Speed value = 127 (parsed as EMERGENCY_STOP command type)
- Router converts to speed=0

### Ecos (Phase 3.2 - To be implemented)

**Protocol**: XML over TCP/15471  
**Connection**: Establish, send heartbeat every 30 sec, handle subscriptions  
**Message types**: status queries, loco control, function updates, device list  

---

## Working with the Code

### Adding a New Feature

1. **Understand the layer**: Is this display? Protocol? State? Routing?
2. **Check existing patterns**: Similar features already implemented?
3. **Update config.h first**: Add constants, enable flags
4. **Implement in appropriate file**: Don't cross layer boundaries
5. **Test locally**: Compile, run on hardware or simulator
6. **Update OLED display** if it affects user-visible state
7. **Add debug output**: DEBUG_PRINTF for development tracking
8. **No comments for obvious code**

### Modifying a Protocol Handler

1. Inherit from `ProtocolInterface` (required methods: `begin()`, `update()`, `sendSpeedCommand()`, `sendFunctionCommand()`, `getStatus()`)
2. Create static objects in main .ino, NOT dynamically
3. Register with router in setup(): `router.setXxxInterface(&xxx_interface)`
4. Call `router.handleXxxCommand()` for incoming commands
5. Router calls your `send*Command()` methods for outgoing broadcasts
6. Echo prevention is automatic (don't check it yourself)

### Testing XpressNet Messages

Mock throttle messages for testing:
```cpp
// Simulate throttle sending speed 64 forward to loco 100
uint8_t msg[4] = {0x00, 100, 0x40, 0x40 ^ 0x00 ^ 100};  // checksum
Serial1.write(msg, 4);
```

Verify with debug output:
```
DEBUG_XNET_PRINTF("XpressNet RX: Speed - Addr=%u Speed=%u Dir=%u\n", ...)
```

---

## Common Workflows

### Compiling & Uploading

1. Open `xpressnet_ecos_bridge.ino` in Arduino IDE
2. Board: "LOLIN(WEMOS) D1 R2 & mini" (ESP8266)
3. Port: COM port of Wemos D1 Mini (USB)
4. Verify build: Sketch → Verify/Compile
5. Upload: Sketch → Upload

### Debugging

1. Open Serial Monitor (115200 baud)
2. Watch startup messages and protocol activity
3. Adjust `config.h` debug flags:
   - `DEBUG_XPRESSNET=1`: XpressNet messages
   - `DEBUG_ECOS=1`: Ecos messages
   - `DEBUG_STATE_ENGINE=1`: Loco state changes
   - `DEBUG_ECHO_PREVENTION=1`: Echo suppression

### Adding a New DCC Address

1. **Automatic**: First throttle command for unknown address auto-creates loco in StateEngine
2. **Router**: Calls `requestEcosSubscription(address)` to query Ecos
3. **Ecos**: Responds with current state (Phase 3.2)
4. **Display**: Shows loco count increasing

### Checking Memory Usage

```cpp
// In debug output:
Heap: 156000 bytes available

// Peak memory for 50 locos + state:
State engine: ~1.5KB (50 × 30 bytes)
Display: ~1KB
Router: <1KB
Total: ~3KB used, 157KB free
```

---

## Files & Organization

```
xpressnet_ecos_bridge/
├── xpressnet_ecos_bridge.ino          Main sketch entry point
├── config.h                            User-facing configuration (ONLY file users edit)
├── definitions.h                       Global types, enums, constants
├── state_engine.h/.cpp                 Loco state management (50 locos max)
├── command_router.h/.cpp               Protocol bridging & echo prevention
│
├── interfaces/
│   └── interface_base.h                Abstract base classes for protocols
│
├── protocols/
│   ├── xpressnet/
│   │   ├── xpressnet_interface.h/.cpp  Master device implementation
│   │   ├── xpressnet_message_parser.h/.cpp  Binary protocol parsing
│   │   └── [future: Gahtow XpressNetMaster lib integration]
│   │
│   ├── ecos/
│   │   └── ecos_interface.h            TCP/XML protocol (Phase 3.2 stub)
│   │
│   ├── loconet/
│   │   └── loconet_interface.h         Future
│   │
│   └── z21lan/
│       └── z21lan_interface.h          Future
│
├── display/
│   └── oled_display.h                  I2C OLED driver
│
├── oled_display.cpp                    OLED implementation
│
├── utils/
│   ├── timing.h                        Non-blocking TimedTask class
│   ├── debug.h                         DEBUG_PRINTF macro
│   └── memory.h                        ESP8266 memory utilities
│
├── docs/
│   ├── 01_DESIGN_DOCUMENT.md          Full architecture specification
│   ├── 02_GITHUB_SETUP.md             Repository setup guide
│   ├── 03_FILE_MANIFEST.md            Detailed file descriptions
│   ├── 04_COMPILATION_FIX.md          Include path fixes (reference)
│   └── 05_COMPILATION_FIX_COMPLETE.md Final verification (reference)
│
├── tests/                              [Placeholder for Phase 4 unit tests]
├── libraries/                          [External libs via Arduino IDE]
└── README.md                           User-facing overview
```

---

## Key Design Decisions & Rationale

### Why No Dynamic Memory?
Embedded systems with limited RAM need predictable memory usage. Static allocation prevents heap fragmentation and out-of-memory crashes. 50 locos = ~1.5KB fixed footprint.

### Why Compile-Time Toggles in config.h?
Features not enabled = zero compiled code overhead. A disabled protocol costs nothing. Supports multiple hardware configurations with same codebase.

### Why Echo Prevention at Router Level?
All protocols route through router, so a single check prevents loops. Simpler than implementing per-protocol. 500ms window works for all tested scenarios.

### Why Fixed 9600 Baud XpressNet?
XpressNet standard, hardware-level limitation. Cannot be changed without breaking protocol compatibility.

### Why Non-Blocking Everything?
XpressNet message windows are ~20-50ms. Blocking for 100ms on Ecos TCP would miss commands. Cooperative multitasking with yield() balances responsiveness.

### Why 5-Minute Loco Timeout?
XpressNet throttles are session-based. After 5 min inactivity, assume loco disconnected. Frees up state engine slots for new locos. Matches typical human usage patterns (user puts throttle down).

---

## When Working with Claude Code

- **Always read the current code first**: Don't assume Phase 1 design doc matches Phase 3 reality
- **Respect the 500ms echo window**: Tests timing-sensitive to this value
- **Non-blocking is non-negotiable**: Main loop MUST yield to ESP8266 WiFi stack
- **Test on hardware**: Simulator can't verify serial timing or WiFi connectivity
- **One protocol per PR**: Don't mix XpressNet + Ecos in single change
- **Config.h is the API**: Users modify only that file, nothing else
- **Memory is precious**: 160KB RAM total, 50 locos already use 3KB

---

## Design Documents & Implementation Blueprints

- **`docs/01_DESIGN_DOCUMENT.md`**: Overall architecture and Phase 1-2 specification
- **`docs/02_PHASE_3_2_ECOS_DESIGN.md`**: Ecos LAN protocol (TCP/text, echo prevention, subscription model)
- **`docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md`**: Complete Phase 4 testing plan (12 steps, frameworks, success criteria)
- **Implementation Status**: See "Development Status" section (Phase 3.1-3.2 done, Phase 4.1 foundation complete)

## Useful References

- **XpressNet Protocol**: Lenz standard, Gahtow's library docs
- **DCC Standard**: NMRA S-9.1 (loco addressing, speed steps)
- **Ecos Protocol**: ESU documentation (XML commands, subscription model)
- **ESP8266 Constraints**: 160MHz single-core, WiFi pre-empts timing
- **Echo Prevention**: Queue-based (10-item buffer, 2-sec window for TCP latency)
- **State Engine**: Truth table (XpressNet priority, Ecos backfill)

---

## Future Improvements (Post-Phase 4)

- EEPROM storage of WiFi credentials and Ecos IP
- Web-based configuration UI
- OTA (over-the-air) firmware updates
- LocoNet support (parallel to XpressNet)
- Z21 LAN protocol support
- Accessory decoder (turnout) control
- Speed step 128 (currently 14-step, DCC standard)
- Advanced function mapping (e.g., F5=headlight, but only on steam engines)

---

**Last Updated**: 2026-07-24 (Phase 4.1-4.2 complete: clean build verified, test scaffold implemented)
