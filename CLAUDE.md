# XpressNet-Ecos Bridge - Development Guide

**Project**: Model railway protocol bridge for ESP8266 (Wemos D1 Mini)  
**Purpose**: Translate commands between XpressNet (hardwired RS485) and ESU Ecos (WiFi TCP/XML)  
**Language**: C++ (Arduino IDE 1.8+)  
**Target**: ESP8266 microcontroller (160MHz, 4MB Flash, 160KB RAM)

> 🔧 **Phase 4.6 IN PROGRESS (2026-07-27) — hardware has arrived.** The board is
> assembled (Wemos D1 Mini + MAX485, half-duplex single-wire per Philipp Gahtow's Z21
> wiring pattern: D6=data, D0=DE/RE) and the firmware now integrates Gahtow's real
> XpressNetMaster library instead of the old hand-rolled parser — see the "Design
> correction" entry below for why. No OLED is wired yet (non-fatal, firmware runs
> without it). Native tests and the ESP8266 build have been re-verified; real hardware
> validation against a physical throttle + Ecos is the next manual step. See Phase 4.6
> below for the checklist.

---

## 📝 Recent Updates (This Session)

**2026-07-29 — First real hardware flash: XpressNet interface up, Ecos/OLED as expected**
- Flashed `env:debug` (env:wemos + debug output) to the physical Wemos D1 Mini via
  esptool over COM4 (921600 baud). Chip confirmed genuine ESP8266EX (MAC
  ec:fa:bc:b3:14:00), not just an assumed serial port.
- **Boot log confirmed**: XpressNet interface initializes on D12/D16 at 62500 baud as
  configured; WiFi connects to `HOMER`, gets `192.168.0.113`; Ecos TCP connect to
  `192.168.0.50:15471` fails and retries with backoff - **expected**, Ecos wasn't
  powered on for this test. `XpressNet: Bus disconnected (no initial activity)` is
  also expected - no throttle plugged in yet, D1 completely isolated for this boot.
- **Investigated one surprising log line**: `OLED display initialized successfully`
  despite no OLED being wired (user confirmed board is fully isolated right now).
  Root cause identified in the vendored `Adafruit_SSD1306` library, not this
  project's code: `Adafruit_SSD1306::begin()` only returns `false` on its internal
  framebuffer `malloc()` failure - it never checks for an I2C ACK from the display,
  so with nothing connected it just silently no-ops rather than failing. Confirmed
  harmless (no crash, no side effects - draws to a buffer nobody reads). Corrected
  the slightly-inaccurate "fails gracefully" phrasing in Hardware Configuration
  above; left the code as-is since an actual I2C presence probe isn't worth adding
  for a cosmetic log line.
- **Next**: real XpressNet throttle test (speed/direction/function commands) per the
  Phase 4.6 checklist - Ecos side deferred until Ecos is powered on.

**2026-07-29 — WiFi credentials removed from git (were already public on GitHub)**
- **Trigger**: user asked to stop committing the real WiFi SSID/password to git and
  keep them local-only. Checking history before acting revealed this wasn't a
  hypothetical risk: `WIFI_SSID`/`WIFI_PASSWORD` were hardcoded in `config.h` since
  commit `4c3e0d6` (Phase 3.2, 2026-07-22), which was already merged into
  `origin/main` - and `gh repo view` confirmed this GitHub repo is **public**. The
  real WiFi password had been publicly visible for about a week before this fix.
- **Fixed going forward**: `config.h` no longer hardcodes `WIFI_SSID`/`WIFI_PASSWORD`
  - it now does `#if __has_include("wifi_credentials.local.h") #include ... #else
  #error ...` ([config.h](xpressnet_ecos_bridge/config.h)). Real values live in
  `xpressnet_ecos_bridge/wifi_credentials.local.h`, added to `.gitignore` (fixing a
  stale, wrong-path `src/config.local.h` entry left over from an earlier unrealized
  plan for this same idea). A committed `wifi_credentials.local.h.example` template
  tells future checkouts what to create. Verified the `#error` guard actually fires
  with a clear message when the local file is absent (temporarily removed it to
  confirm, then restored).
- **Not done (flagged to user, needs explicit decision)**: this only stops *future*
  commits from containing the secret. The password is still present in
  `origin/main`'s history at `4c3e0d6` right now. Actually removing it requires
  rewriting git history (e.g. `git filter-repo`) and force-pushing over
  `origin/main` - destructive to shared history, and even then doesn't guarantee
  removal from any existing forks/clones/caches. Recommended the user also treat
  the real WiFi password as compromised and rotate it on the router regardless of
  what happens with git history (not something achievable from this repo).
- **Result**: `env:wemos` rebuilds identically (44.3% RAM / 31.1% Flash - same
  string values, just relocated). Native suite unaffected: 91/91 passing (config.h's
  WiFi defines aren't exercised by the native build).
- **Scope note**: only `WIFI_SSID`/`WIFI_PASSWORD` were moved - `ECOS_IP`/`ECOS_PORT`
  remain in `config.h` (not secrets, just internal network topology).

**2026-07-29 — Real WiFi/Ecos credentials confirmed before first flash**
- **Trigger**: about to attempt the first real hardware flash for Phase 4.6; `config.h`
  had never been validated against the user's actual network (flagged as placeholders
  since the "Hardware arrived" entry).
- **Confirmed with user**: `WIFI_SSID`/`WIFI_PASSWORD` (`HOMER`/existing password) were
  already correct - no change needed. `ECOS_IP` was wrong: the real Ecos (hostname
  `ECOS`) is at `192.168.0.50`, not the placeholder `192.168.1.100` (which happens to be
  ESU's factory-default fixed IP - this Ecos is evidently not at its factory default).
  `ECOS_PORT` (15471) confirmed correct as the fixed ESU protocol port.
- **Changed**: `config.h`'s `ECOS_IP` updated to `"192.168.0.50"`.
- **Result**: `env:wemos` rebuilt clean: 44.3% RAM (36296/81920 bytes), 31.1% Flash
  (324339/1044464 bytes) - unchanged from the master-mode-tripwire build modulo the
  shorter IP string literal. Native tests not affected (config.h's ECOS_IP isn't
  exercised by the native suite). Ready for first real flash attempt.
- **Also decided this session (not yet started)**: WiFi/Ecos web configuration
  (EEPROM-backed, WiFiManager captive portal) is deliberately deferred until after
  Phase 4.6 hardware validation completes - see Future Improvements.

**2026-07-29 — Master-mode tripwire added to XpressNetInterface**
- **Trigger**: before starting real hardware validation, user asked for explicit
  confirmation that this bridge will always run as XpressNet MASTER, since the
  design assumes it's the sole command-station-role device on the bus (throttles
  are slaves, Ecos is off-bus over WiFi).
- **Investigation**: traced the actual library behavior rather than trusting the
  header comment. `xnet.setup(Loco128, XPRESSNET_DATA_PIN, XPRESSNET_CONTROL_PIN)`
  in `xpressnet_interface.cpp` uses the 3-arg overload, defaulting `XnModeAuto` to
  `true` - the only correct choice for a master-role device (`false` would force
  permanent SLAVE mode, i.e. a throttle, not a command station). The library starts
  `XNetSlaveMode = 0` (master) and only demotes to slave if it receives a call byte
  addressed to the legacy fixed slave address `0x5F`/`0x5A` (`MY_ADDRESS` in
  XpressNetMaster.h) - real other-master traffic that can't occur in this topology.
  Also checked a specific self-echo hazard: the master's own address-31 poll byte is
  numerically `0x5F`, same as `MY_ADDRESS`, on a single half-duplex wire where TX
  loops back to RX - but `XNetSwSerial.enableIntTx(false)` disables RX interrupt
  handling during transmission, so the device never hears its own call bytes. No
  live code path exists that would demote this bridge from master.
- **Added**: a cheap, edge-triggered tripwire in `updateBusStatus()` (already called
  every `update()` cycle, non-blocking) - compares `xnet.getOperationModeMaster()`
  against a new `was_master_mode` member and logs via `DEBUG_XNET_PRINTF` only on
  transition, so an unexpected demotion (e.g. a wiring mistake introducing a second
  master) becomes visible instead of silently breaking throttle response.
- **Result**: native suite still 91/91 passing (this file isn't part of the native
  build - Arduino-only). `env:wemos` firmware re-verified: 44.3% RAM (36292/81920
  bytes), 31.1% Flash (324335/1044464 bytes) - negligible increase from one bool
  member and a few lines of comparison/logging.

**2026-07-27 — Hardware arrived; XpressNet interface rewritten around Gahtow's real library**
- **Trigger**: user assembled the physical board (Wemos D1 Mini + MAX485) per Philipp
  Gahtow's Z21-command-station wiring: a single half-duplex data pin (**D6**, MAX485
  DI+RO tied together) and a single DE/RE control pin (**D0**, tied together). No OLED
  wired yet (non-fatal - confirmed on first real flash 2026-07-29: with nothing on
  D1/D2, `Adafruit_SSD1306::begin()` still logs "initialized successfully" and
  returns true, since that library only ever returns false on its internal malloc
  failure, never on missing-device I2C NACK - so it silently no-ops rather than
  failing. Cosmetic only, not fixed - see Recent Updates).
- **Discovery**: that wiring didn't match anything in the repo, and digging into why
  surfaced that the existing XpressNet code had never been validated against real
  hardware or the real protocol, despite its own header comment claiming otherwise:
  * [xpressnet_interface.h](xpressnet_ecos_bridge/protocols/xpressnet/xpressnet_interface.h)
    said *"Uses Gahtow's XpressNetMaster library for hardware abstraction"* - but the
    `.cpp` never used it. It hand-rolled everything on `Serial1` with separate
    RX(D7)/TX(D8)/DE(D5) pins, and the library wasn't present anywhere in the repo.
  * Real XpressNet's physical layer is **62500 baud, 8 data bits + a parity bit used
    as a 9th "call byte" marker, single half-duplex wire** - not the 9600 baud
    full-duplex scheme `config.h` had (previously commented "DO NOT CHANGE...
    hardware-level limitation", which was simply wrong). Verified directly against
    Gahtow's library source (`XNetSwSerial.begin(62500, SWSERIAL_8S1, ...)`) and its
    ESP8266 example, which calls `XpressNet.setup(Loco128, D6, D0)` - an exact match
    for this board's wiring.
  * The hand-rolled parser modeled "emergency stop" as speed=127 on a per-locomotive
    basis. Real XpressNet has no such thing: per-loco stop is just speed=0, and the
    only emergency stop the reference library implements is a **bus-wide track-power
    broadcast** (`notifyXNetPower(csEmergencyStop)`) - deliberately **not wired up
    yet** (see Future Improvements), to keep this change focused on validating the
    new hardware.
- **Fixed**: integrated the user's local copy of Gahtow's XpressNetMaster library
  (v3.1.1) into `libraries/XpressNetMaster/`. Rewrote
  `xpressnet_interface.h/cpp` as a thin wrapper: `begin()`/`update()` call the
  library's `setup()`/`update()`; incoming commands arrive via the library's
  weak-symbol `notifyXNetLocoDrive128`/`notifyXNetLocoFunc1/2/3/X` callbacks (which
  can't be class methods, so they're file-scope free functions dispatching through a
  single active-instance pointer); outgoing commands call `xnet.setSpeed()`/
  `setFuncNtoM()`. Function-state updates required extra care: XpressNet fragments
  F0-F31 across up to 5 separate messages with a real Lenz bit-ordering quirk (F0
  lives at bit4 of the first group, not bit0), and `CommandRouter::handleXpressNetFunctionCommand`
  does a full bitmap overwrite rather than a merge - so the new interface reads the
  loco's current bitmap via the already-public `router->getStateEngine().getLoco()`,
  merges in just the arrived group's bits, then forwards the full bitmap.
- **Removed**: `xpressnet_message_parser.h/cpp` and `test/test_xpressnet_parser/`
  (16 tests) - fully superseded now that the real library owns parsing, and confirmed
  to have zero other consumers. Consistent with this project's established practice
  of deleting confirmed-dead scaffolding (see the device-count-tracking removal
  below) rather than leaving it in place.
- **`config.h`**: `XPRESSNET_RX_PIN`/`TX_PIN`/`DE_PIN`/`RE_PIN` replaced with
  `XPRESSNET_DATA_PIN` (12/D6) and `XPRESSNET_CONTROL_PIN` (16/D0);
  `XPRESSNET_BAUD` corrected from 9600 to 62500.
- **Result**: native test suite now 91/91 passing (down from 111 - `test_xpressnet_parser`
  had grown from 16 to ~20 tests by the end of Phase 4.5's coverage push, which
  accounts for the removed 20; the other 4 suites - state_engine, command_router,
  ecos_parser, ecos_command_builder - are unaffected, verified by rerunning them
  directly). `env:wemos` firmware build re-verified clean against the new library
  dependency: 44.1% RAM (36144/81920 bytes), 31.0% Flash (324115/1044464 bytes) -
  up slightly from 43.4%/30.0% pre-integration, as expected from pulling in a real
  third-party library instead of hand-rolled code. Also fixed a `platformio.ini` bug
  surfaced by this build: `lib_extra_dirs` was set under `[platformio]`, which
  PlatformIO 6.1.19 silently ignores - moved to `env:wemos` (the only environment
  that needs it; `env:native` never touches `libraries/XpressNetMaster`). Real
  hardware validation (throttle -> bridge -> Ecos over the physical bus) is the next
  manual step, not yet done in this session.
- **Known gap, deliberately deferred**: the bus-wide emergency-stop/track-power
  broadcast (`notifyXNetPower`) is not wired to anything yet - see Future
  Improvements.

**2026-07-27 — Design decision: XpressNet device-count/status tracking removed**
- **Decision**: The bridge's design premise is deliberately simple - forward any
  XpressNet command to Ecos (and LocoNet/Z21 once live); forward any Ecos update for a
  subscribed loco (or an E-stop) to all XpressNet/LocoNet/Z21 devices. The bridge never
  needs to know *how many* XpressNet throttles exist or track their presence - it just
  assumes any could be there. Only Ecos is a required, known device (the command
  station; boosters connect to it, not to this bridge). Device-count tracking was
  therefore decided to serve no purpose and was removed rather than fixed.
- **Investigation before removing**: confirmed `XNetCommand::STATUS`'s only consumer,
  `XpressNetInterface::updateDeviceCount()`, was a no-op placeholder
  (`(void)cmd; // Placeholder`) that never populated `device_count`, and
  `getDeviceCount()` had zero callers anywhere (not even the OLED display). This
  wasn't a working feature broken by the Phase 4.5-flagged dead-code bug - it was
  unfinished scaffolding end-to-end, on both the producer and consumer sides.
- **Removed**: `XNetCommand::STATUS` enum value; the dead STATUS branch in
  `determineCommandType()` and the STATUS case in `parse()`
  (`xpressnet_message_parser.h/cpp`); `XpressNetInterface::getDeviceCount()`,
  `updateDeviceCount()`, and the `device_count` member (`xpressnet_interface.h/cpp`);
  `SystemStatus::xnet_device_count` (`definitions.h`) and its assignment in
  `CommandRouter::getSystemStatus()`.
- **Result**: 111/111 tests still passing (no test referenced any of this). Coverage
  improved further as a side effect: 88.3%→**89.3%** line, 58.1%→**59.0%** branch
  (removing unreachable code raises the percentage of what remains). ESP8266 firmware
  build confirmed unaffected, with a small binary-size reduction (35588→35580 bytes RAM,
  313215→313183 bytes Flash) from the deleted dead code.

**2026-07-27 — Phase 4.5: Test Coverage Analysis ✅ COMPLETE**
- ✅ **Coverage measurement set up**: `--coverage` + `-lgcov` added to `env:native` build
  flags (gcov instrumentation), `gcovr` installed via pip for reporting (lcov isn't
  available on Windows/MinGW). `-O0` added for accurate line mapping.
- ✅ **Baseline measured**: 76.1% line / 88.1% function / 47.8% branch coverage across
  the 5 core modules (state_engine.cpp, command_router.cpp, xpressnet_message_parser.cpp,
  ecos_message_parser.cpp, ecos_protocol.cpp)
- ✅ **27 new tests added** closing real gaps (not just line-count filler):
  * **state_engine**: MAX_LOCOS capacity-full rejection, invalid-address rejection,
    `getLocoByIndex()` (const + non-const, valid + invalid index - previously 0% covered),
    `clear()`, removing a non-existent address
  * **xpressnet_message_parser**: the short-address branch in `extractAddress()`
    (bit 0x40 set - never exercised by any fixture, since they all use the long-address
    path), `parse()`'s own checksum-failure branch (previously only `isValidMessage()`
    was tested directly), `isValidMessage()`/`determineCommandType()` short-buffer guards
  * **ecos_message_parser**: stray `<END>` without an open block, stray content line
    before any block starts, bare empty line, line-too-long discard (>256 bytes),
    block-too-many-lines oldest-discard (>20 lines), `addr[...]` key extraction
  * **command_router**: invalid-address/speed rejection for the 3 handler methods that
    only had it tested on the 4th (`handleXpressNetFunctionCommand`, `handleEcosCommand`,
    `handleEcosFunctionCommand`), the "update existing loco" branch on the two function
    handlers, reverse-direction echo suppression (only one direction was tested before),
    `xpressnet->getStatus()` branch in `getSystemStatus()`, `debugPrintEchoState()` smoke test
- ✅ **Result**: 88.3% line / 96.6% function / 58.1% branch coverage (up from the
  baseline), **111/111 tests passing**
- 📝 **Known limitation flagged (resolved later this session by removal, not a fix -
  see the entry above)**: in `xpressnet_message_parser.cpp`, `determineCommandType()`'s
  STATUS message classification was unreachable dead code - the SPEED check
  (`data_byte & 0x7F <= 126`) and E-stop check together partition the *entire* possible
  byte range, so no data_byte value could ever fall through to the STATUS/INVALID checks
  below them for length>=4 messages.
- Remaining uncovered lines are narrow boundary conditions (e.g. a line landing exactly
  at MAX_LINE_LENGTH-1 when '\n' arrives) or genuinely unreachable defensive code
  (e.g. `processLine()`'s own empty-line guard, which `processByte()` already filters
  before ever calling it) - diminishing returns past this point.
- ESP8266 (wemos) firmware build re-verified unaffected (same 43.4% RAM / 30.0% Flash)

**2026-07-27 — Phase 4.4: Framework Integration & Test Execution ✅ COMPLETE**
- ✅ **Native build environment established** (previously never verified - g++ wasn't installed)
  * Installed MinGW-w64 (GCC 16.1.0) via winget
  * Created `test/native_stubs/Arduino.h` stub (Serial/ESP/millis) for the handful of
    production files that unconditionally include `<Arduino.h>` - no real Arduino core
    exists for native builds
  * Fixed real pre-existing bugs the native build immediately surfaced:
    - `utils/debug.h`: per-component debug macros (`DEBUG_XNET_PRINTF` etc.) were only
      ever defined inside `#ifdef ARDUINO`, with no native counterpart - removed the guard
    - `utils/memory.h`: missing `<cstdlib>` include for `malloc`; ESP8266-only stack
      intrinsics (inline asm) now guarded behind `#ifdef ARDUINO`
    - `platformio.ini`: updated for PlatformIO 6.1.19's current API (`test_dir` removed,
      hardcoded to `./test`; `src_filter`→`build_src_filter`; `test_build_project_src`→
      `test_build_src`); restructured `tests/`→`test/` with one subdirectory per suite
      (this PlatformIO version bundles flat `test_*.cpp` files into one binary otherwise)
- ✅ **CommandRouter refactored for testability**: `setEcosInterface`/`setXpressNetInterface`
  now accept the abstract `ProtocolInterface*` instead of concrete `EcosInterface*`/
  `XpressNetInterface*`, so `MockProtocolInterface` can be injected directly. Added
  `subscribeToLoco()`/`unsubscribeFromLoco()` as virtual no-op methods on `ProtocolInterface`.
- ✅ **test_command_router.cpp rewritten**: fixed method-name mismatches
  (`handleXpressNetCommand`/`handleEcosCommand`, not the `*SpeedCommand` variants used
  by the Phase 4.3 draft), fixed `isEchoCommand()` test assumptions (it compares
  address+source+time window only, not command values)
- ✅ **test_ecos_command_builder.cpp implemented for real** (Phase 4.3 left it as 21 empty
  stubs) against the actual `ecosBuild*Cmd` free-function API (object ID, not DCC address;
  trailing `\n`)
- ✅ **Unity framework integrated**: all 5 files converted from inline early-return
  assertions to real `TEST_ASSERT_*` macros
- ✅ **Fixed real bugs the tests caught** (not just test-code bugs):
  * `xpressnet_message_parser.cpp`: `determineCommandType()` could never classify a
    message as FUNCTION (its SPEED check matched everything first) - real XpressNet
    function commands were being misinterpreted as speed commands. Fixed by checking
    a function-marker bit (0x20) first; `extractAddress()` updated to strip marker bits
    before reconstructing the address
  * `xpressnet_message_parser.cpp`: E-stop (speed=127) was being clamped to 126,
    contradicting `XNetCommand::speed`'s own documented contract ("127=E-stop") and
    CLAUDE.md's documented design ("Router converts to speed=0", implying the parser
    should preserve 127)
  * `test/fixtures/xpressnet_messages.h`: 5 of 9 fixtures had hand-computed checksums
    that were simply arithmetic errors; 2 fixtures set the wrong address-encoding bit
    (0x40 means SHORT address per CLAUDE.md's own spec, not "long" as the fixture
    comments claimed)
  * `test/fixtures/ecos_responses.h` + `test_ecos_parser.cpp`: fixtures crammed
    everything into one line (`"<REPLY id 100 speed[64]>"`), but the real parser
    requires a 3-line block (marker, then a property line with the object ID as
    its leading token, then `<END 0 (OK)>`) - REPLY doesn't carry an id on its
    marker line the way EVENT does. Also fixed a key-name mismatch: fixtures used
    `dir[...]` but the parser matches the property key `direction`.
- **Result**: **84/84 tests passing (100%)**, well above the 90% target
  * test_xpressnet_parser: 16/16, test_ecos_parser: 14/14, test_state_engine: 15/15,
    test_ecos_command_builder: 25/25 (expanded from 21), test_command_router: 14/14
- **Status**: Phase 4.4 complete. Native test suite (`platformio test -e native`) is a
  real, working regression harness for the first time in this project's history.

**2026-07-24 — Phase 4.3: Unit Test Implementation ✅ COMPLETE**
- ✅ Implemented 80+ test cases across 5 core test files
- ✅ **test_xpressnet_parser.cpp** (16 tests): Binary protocol parsing, checksums, errors
- ✅ **test_ecos_parser.cpp** (14 tests): Text protocol parsing, line accumulation
- ✅ **test_state_engine.cpp** (15 tests): Loco management, time-dependent expiry (using mock_now_ms)
- ✅ **test_ecos_command_builder.cpp** (21 tests): Command generation (speed, function, query)
- ✅ **test_command_router.cpp** (12 tests): Protocol bridging, echo prevention, subscriptions
- ✅ All tests follow Arrange-Act-Assert pattern
- ✅ Inline assertions ready for Unity TEST_ASSERT macro replacement
- ✅ Mock time enables deterministic testing (no real delays)
- ✅ Mock interfaces track calls for verification
- **Commits**: b990560, 73b6c60, 2dbcdad
- **Status**: Phase 4.3 complete, Phase 4.4 ready to start
- **Next**: Integrate Unity framework, run tests, achieve >90% pass rate

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

## 🎯 Phase 4.3 & 4.4: Test Implementation + Unity Integration ✅ COMPLETE (historical)

Both phases are done. The detailed step-by-step plan that used to live here has been
superseded by what actually happened - see the **2026-07-27** entries at the top of
"Recent Updates" for the real implementation notes (native build environment, Unity
integration, the FUNCTION/SPEED and E-stop bugs found and fixed, etc.). Commits:
`73b6c60` (Phase 4.3), `37c17d2` (Phase 4.4).

---

## 🎯 Phase 4.6: Hardware Procedures 🔧 IN PROGRESS

**Hardware arrived 2026-07-27**: board assembled (Wemos D1 Mini + MAX485, D6 data / D0
control per Gahtow's Z21 wiring pattern, no OLED yet). The XpressNet interface has
been rewritten around Gahtow's real XpressNetMaster library to match - see the
"Hardware arrived" entry in Recent Updates for the full story. Native tests and the
`wemos` build have been re-verified since that change (see "Verification" below for
the actual numbers). Real hardware validation (flash + test against a live throttle
and Ecos) has not happened yet - that's the remaining work in this phase.

### Current State

- Phases 1-4.5 complete, plus the XpressNetMaster library integration above.
- Native unit test suite and coverage numbers: see the "Hardware arrived" entry in
  Recent Updates for the latest counts (parser-specific tests were removed since that
  logic now lives in the real library).
- ESP8266 (`env:wemos`) firmware build re-verified after the library integration.
- Device-count/status tracking removed per design decision (bridge doesn't need to
  know if/how many XpressNet devices exist - see Recent Updates)
- Design premise confirmed by user: forward any XpressNet command to Ecos (and
  LocoNet/Z21 once live); forward any Ecos update for a subscribed loco (or E-stop) to
  all XpressNet/LocoNet/Z21 devices. Only Ecos is a required, known device.

### Environment Setup Reference (for a fresh session/machine)

- **Compiler**: MinGW-w64 (GCC 16.1.0), installed via winget as
  `BrechtSanders.WinLibs.POSIX.UCRT`. Add to PATH:
  `C:\Users\Francois\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`
- **PlatformIO**: `pip install platformio` (already installed)
- **Coverage tooling**: `pip install gcovr` (lcov isn't available on Windows/MinGW)

**Run native tests**: `python -m platformio test -e native` (run from
`E:\Claude\Bridge\files`, with MinGW on PATH). Note: PlatformIO's own pass/fail summary
line has occasionally shown a spurious SIGFPE/miscount artifact on this Windows setup -
if that happens, run `.pio/build/native/program.exe` directly for the ground truth
(it prints a clean Unity `X Tests Y Failures` summary and exits with the failure count).

**Generate coverage report**:
```bash
"C:\Users\Francois\AppData\Local\Packages\PythonSoftwareFoundation.Python.3.10_qbz5n2kfra8p0\LocalCache\local-packages\Python310\Scripts\gcovr.exe" \
  --root xpressnet_ecos_bridge \
  --filter "xpressnet_ecos_bridge/state_engine.*" \
  --filter "xpressnet_ecos_bridge/command_router.*" \
  --filter "xpressnet_ecos_bridge/protocols/ecos/ecos_message_parser.*" \
  --filter "xpressnet_ecos_bridge/protocols/ecos/ecos_protocol.*" \
  --object-directory .pio/build/native --print-summary -r .
```
(requires a fresh `pio test -e native` run first, so `.gcda` files exist)

**Build real firmware**: `python -m platformio run -e wemos`

### What Phase 4.6 Actually Needs

1. **A manual test checklist** for verifying the bridge against a real ESP8266 +
   physical XpressNet bus + real ESU Ecos (or Ecos-compatible command station). This
   validates things native unit tests structurally cannot: actual RS485/half-duplex
   serial timing, real WiFi/TCP behavior against a live Ecos, and real XpressNet
   throttle behavior. Highest priority: this is the first time the new
   XpressNetMaster-based interface (and its function-fragment merge logic in
   `xpressnet_interface.cpp`) will see real hardware bytes at all - verify basic
   speed/direction/function commands from a real throttle before anything else.
2. Flash the firmware (`env:wemos` target) to the physical Wemos D1 Mini.
3. Confirm `config.h`'s WiFi credentials and Ecos IP match the real network before
   testing (currently hardcoded placeholders - see "Hardware Configuration" below).
4. Work through the checklist: XpressNet speed/direction/function commands from a
   real throttle, verify they reach Ecos correctly; Ecos-side changes verify they
   propagate back to XpressNet; subscription lifecycle (new loco -> subscribe, 5-minute
   inactivity -> unsubscribe) against real timing, not mocked time. (Bus-wide
   emergency stop is not implemented yet - see Future Improvements - so it's not
   part of this checklist.)

### After Phase 4.6

See "Future Improvements (Post-Phase 4)" near the end of this file for the backlog
(LocoNet, Z21, EEPROM config storage, OTA updates, etc.) - nothing there is started.

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

**Phase 4.3: Unit Test Implementation** ✅ COMPLETE
- **80+ test cases** implemented across 5 core test files
- **16 tests**: XpressNet message parsing (binary protocol)
- **14 tests**: Ecos text protocol parsing (line accumulation, key-value)
- **15 tests**: State engine (loco management, time-dependent expiry with mock_now_ms)
- **21 tests**: Ecos command builder (speed, function, query, subscribe)
- **12 tests**: Command router (bridging, echo prevention, subscriptions)
- **Status**: All tests written with inline assertions, ready for Unity integration

**Phase 4.4: Framework Integration & Test Execution** ✅ COMPLETE
- Unity framework integrated, native build environment established (MinGW-w64)
- All inline assertions replaced with real TEST_ASSERT macros
- 84/84 tests passing (grew from 80+ as test_ecos_command_builder went from 21 stubs to 25 real tests)
- Fixed 2 real production bugs and several fixture bugs the tests caught (see 2026-07-27 entry above)
- ESP8266 (wemos) firmware build re-verified clean after all changes
- **Commit**: 37c17d2

**Phase 4.5: Test Coverage Analysis** ✅ COMPLETE
- Coverage measured via gcov + gcovr (lcov unavailable on Windows/MinGW): 76.1%→88.3%
  line, 88.1%→96.6% function, 47.8%→58.1% branch across the 5 core modules
- 27 new tests added (111 total) closing real gaps: MAX_LOCOS capacity limit,
  `getLocoByIndex()` (was 0% covered), the true short-address branch in
  `extractAddress()`, buffer-overflow/line-too-long/block-too-many-lines paths in
  `EcosMessageParser`, invalid-input validation on 3 of 4 CommandRouter handlers that
  only had it tested on the 4th, reverse-direction echo suppression
- Flagged `determineCommandType()`'s unreachable STATUS dead code - resolved by
  removal (see "Design decision" entry above), not by fixing it
- Integration tests (multi-component round trips) not yet added - deferred, current
  unit-level coverage was the higher-value target
- **Status**: 111/111 tests passing, wemos firmware build re-verified unaffected

**Phase 4.6: Hardware Procedures** 🔧 IN PROGRESS
- Hardware assembled; XpressNet interface rewritten around Gahtow's real
  XpressNetMaster library to match the physical wiring (see Recent Updates)
- Remaining: manual test checklist against a real ESP8266 + XpressNet bus + Ecos

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
Data:    D6 (pin 12, GPIO12) - half-duplex data line (MAX485 DI+RO tied together)
Control: D0 (pin 16, GPIO16) - MAX485 DE+RE tied together (HIGH=transmit, LOW=receive)
Baud:    62500, 8 data bits + parity-as-9th-bit, 1 stop (SWSERIAL_8S1) - real Lenz
         XpressNet wire rate, DO NOT CHANGE (protocol standard, not arbitrary)
```
Wiring follows Philipp Gahtow's Z21-command-station pattern exactly (single half-duplex
data pin + single combined DE/RE control pin) - the same pattern his XpressNetMaster
library (`libraries/XpressNetMaster/`) is built for. `xpressnet_interface.cpp` wraps
that library rather than talking to the pins directly.

### Ecos (WiFi TCP)
```
SSID/Password: in xpressnet_ecos_bridge/wifi_credentials.local.h (gitignored, NOT in
  git - see wifi_credentials.local.h.example for the template). Confirmed correct
  against the real network 2026-07-29.
Ecos IP: 192.168.0.50 (config.h, hostname ECOS, confirmed against real hardware 2026-07-29)
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

Real message framing, checksums, and call-byte arbitration are owned entirely by
Gahtow's XpressNetMaster library (`libraries/XpressNetMaster/`) - `xpressnet_interface.cpp`
only translates its callback API to/from `CommandRouter`. The physical layer is
62500 baud, 8N1 + a parity bit used as a 9th "call byte" marker, over a single
half-duplex wire (see Hardware Configuration above).

**Speed Command** (128-speed-step mode, `notifyXNetLocoDrive128`):
- Raw byte is `RVVVVVVV`: bit 7 = direction (0=forward, 1=reverse on the wire,
  inverted from our internal convention), bits 6-0 = speed (0-126)

**Function Commands**:
- Fragmented across up to 5 messages: F0-F4 (F0 sits at bit4, not bit0 - a real Lenz
  quirk), F5-F8, F9-F12, F13-F20, F21-F28. Each fragment only carries its own bits;
  `xpressnet_interface.cpp` merges each arriving fragment into the loco's full
  F0-F31 bitmap (read via `router->getStateEngine().getLoco()`) before forwarding to
  `CommandRouter`, since `handleXpressNetFunctionCommand` expects a full snapshot,
  not a partial update.

**Throttle Addressing**:
- Short address (1-99): bit 6 of high byte set
- Long address (100-9999): 14-bit field

**Emergency Stop** (not yet wired up - see Future Improvements):
- Per-locomotive stop is just an ordinary speed=0 command, nothing special.
- Real bus-wide emergency stop (Notaus/track power) is a separate broadcast message
  the library surfaces via a `notifyXNetPower(csEmergencyStop)` callback, not a
  per-loco speed value. (The previous "speed=127 means E-stop" model documented here
  was incorrect - discovered while integrating the real library; see the 2026-07-27
  "Hardware arrived" entry in Recent Updates.)

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
│   │   └── xpressnet_interface.h/.cpp  Wraps libraries/XpressNetMaster (real
│   │                                    parsing/framing lives in the library)
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
├── libraries/                          External libs (PlatformIO lib_extra_dirs)
│   └── XpressNetMaster/                 Gahtow's real XpressNet master library
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

### Why Fixed 62500 Baud XpressNet?
This is the real Lenz XpressNet wire rate (8 data bits + a parity bit used as the 9th
"call byte" marker bit, half-duplex), confirmed against Gahtow's reference
XpressNetMaster library. Cannot be changed without breaking protocol compatibility.
(A previous version of this doc said 9600 baud - that was never validated against
real hardware and was wrong; corrected 2026-07-27 when integrating the real library.)

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
- Advanced function mapping (e.g., F5=headlight, but only on steam engines)
- **Bus-wide emergency stop / track power** (Notaus): XpressNetMaster surfaces this
  via a `notifyXNetPower(csEmergencyStop)` callback, but nothing consumes it yet.
  Likely needs a new CommandRouter path (e.g. iterate all known locos in StateEngine
  and force speed=0) rather than the single-address `handleXpressNetCommand` path.
  Deliberately deferred out of the 2026-07-27 XpressNetMaster integration to keep
  that change focused on validating the new hardware.

---

**Last Updated**: 2026-07-27 (Hardware arrived; XpressNet interface rewritten around
Gahtow's real XpressNetMaster library to match the physical D6/D0 half-duplex wiring
and the real 62500-baud protocol - see the top-of-file banner and the "Hardware
arrived" entry in Recent Updates for full details. Native test suite and ESP8266
build re-verified after the change; real hardware validation is the next step.)
