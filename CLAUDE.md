# XpressNet-Ecos Bridge - Development Guide

**Project**: Model railway protocol bridge for ESP8266 (Wemos D1 Mini)  
**Purpose**: Translate commands between XpressNet (hardwired RS485) and ESU Ecos (WiFi TCP)  
**Language**: C++ (Arduino IDE 1.8+)  
**Target**: ESP8266 microcontroller (160MHz, 4MB Flash, 160KB RAM)

> ℹ️ **How this file is maintained**: `CLAUDE.md` carries only what's pertinent to
> the current and next session - architecture, conventions, config reference, and
> current status. Dated session narratives (root causes, debugging steps, exact
> numbers) go to [docs/CHANGELOG.md](docs/CHANGELOG.md) instead. When updating
> after a session, add detail to the changelog and only touch this file's status
> sections if the current state actually changed.

> ✅ **Phase 4.6 COMPLETE (2026-08-03): full bridge validated end-to-end on real hardware.**
> Every checklist item now confirmed live, across sessions from 2026-07-30 to
> 2026-08-03: XpressNet command/reply correctness (speed, direction, functions,
> 128-step declaration, STOP-button ack) against a real MultiMaus; a stable Ecos
> TCP connection; bidirectional XpressNet↔Ecos command propagation surviving
> reclaim cycles and bus timeouts; the 5-minute subscription lifecycle (loco
> purged from `StateEngine` and unsubscribed from Ecos after real, unmocked
> inactivity - confirmed today, active-loco count dropped 2→1 on both the XNet
> and Ecos pages exactly as expected); and the OLED display (live status icons,
> 10-second OmniConnect boot splash). Many real hardware bugs were found and
> fixed to get here - full dated diagnosis for every one in
> [docs/CHANGELOG.md](docs/CHANGELOG.md), starting from the 2026-07-30 entry.
>
> **Deliberately out of scope for Phase 4.6** (tracked separately - see Future
> Improvements below): bus-wide emergency stop only acknowledges on the wire,
> doesn't yet force locos to actually stop; the MultiMaus "stolen icon" display
> not refreshing when Ecos drives a loco (one fix attempted and reverted -
> needs careful instrumented retesting, see changelog); a few deferred OLED
> fields (XNet last-message age, Ecos round-trip latency, per-loco functions
> on the main page).
>
> **Next**: nothing required to close out Phase 4.6 - see "After Phase 4.6"
> below for the open backlog.

---

## 🎯 Phase 4.6: Hardware Procedures ✅ COMPLETE

Hardware assembled 2026-07-27 (Wemos D1 Mini + MAX485, D6 data / D0 control per
Gahtow's Z21 wiring pattern, no OLED yet). Full story of the rewrite and the
2026-07-29/30 hardware debugging sessions: [docs/CHANGELOG.md](docs/CHANGELOG.md).

### Current State

- Phases 1-4.5 complete, plus the XpressNetMaster library integration (2026-07-27).
- **XpressNet RX/TX fully validated on real hardware (2026-07-30)**: speed,
  direction, all function groups, 128-step declaration to the throttle, and
  STOP-button track-power acknowledgment all confirmed working against a real
  Roco MultiMaus.
- **Ecos connection confirmed stable (2026-07-31)**: `TEMP_SKIP_ECOS_CONNECT` and
  `XNetDEBUG` reverted; connected to the real Ecos and found/fixed two real bugs
  (heartbeat/watchdog timing mismatch, invalid heartbeat command) - see
  [docs/CHANGELOG.md](docs/CHANGELOG.md). Connection held stable across multiple
  30-second heartbeat cycles with no drops.
- **Address map fixed for multi-locomotive layouts (2026-07-31)**: the real
  Ecos test setup has 15 locomotives, and `queryObjects` was silently
  collapsing all of them down to just the last one (`EcosMessageParser`
  bug), compounded by `addAddressMapEntry()` never updating existing entries
  (would have filled `MAX_ECOS_OBJECTS` within minutes once heartbeat started
  re-querying every 30s). Both fixed and confirmed on hardware - all 15 locos
  now populate the map correctly and stay stable across repeated heartbeats.
- **Bidirectional XpressNet ↔ Ecos propagation confirmed robust (2026-07-31)**:
  a real MultiMaus speed sweep correctly reaches Ecos, and Ecos-side changes
  correctly reach the MultiMaus back, across multiple claim/reclaim cycles
  and surviving bus timeouts. Getting here required fixing a recurring
  physical RX-path fault (same MAX485 fragility as 2026-07-29 - fixed by
  reseating/wiggling the module), plus several real software bugs:
  `subscribed_to_ecos` never set by the XpressNet-initiated path (resubscribe
  spam), `markBusActivity()` never recovering the status display from
  `DISCONNECTED`, `XpressNetInterface::sendSpeedCommand()`/`sendFunctionCommand()`
  silently refusing to transmit once 5s had passed without hearing a
  throttle, and `EcosInterface` never actually sending direction changes at
  all (plus using the wrong property name, `direction` instead of the real
  `dir`, confirmed against the official ESU protocol spec now in
  `docs/ecos_pc_interface3.pdf`). A follow-up attempt to fix the MultiMaus's
  "stolen" conflict icon not refreshing its displayed values (via the
  library's `ReqLocoBusy()`) was tried, found to break bidirectional
  propagation after a few claim/reclaim cycles, and reverted - see
  [docs/CHANGELOG.md](docs/CHANGELOG.md) for full diagnosis of everything
  above.
- **Known, deferred follow-up**: the MultiMaus's own display doesn't visually
  refresh speed/direction while its "stolen" (in-use-elsewhere) icon is
  flashing, when the change originates from Ecos. A two-MultiMaus test
  disproved "intentional throttle UX" as the explanation - genuine MM-to-MM
  steal/reclaim DOES correctly refresh both units' displays, so the gap is
  bridge-side: Ecos-driven changes trigger the "stolen" flash via a side
  effect of the plain drive-command broadcast, not via the real
  `SetLocoBusy` message that MM-to-MM handoff uses to prompt a refresh. The
  earlier `ReqLocoBusy()` attempt (tried and reverted same day) used the
  conceptually-correct mechanism but had a real side effect that broke
  forward propagation after a few cycles, not yet root-caused with
  confidence - see [docs/CHANGELOG.md](docs/CHANGELOG.md) for the follow-up
  plan (re-attempt with live dual-MultiMaus monitoring; possibly a targeted
  patch to the vendored library's `SetBusy()`). Deliberately deferred - core
  bidirectional command propagation (the actual Phase 4.6 goal) is solid.
- **Blocking Ecos-connect bug fixed (2026-08-03)**: `EcosInterface::attemptTcpConnect()`'s
  `wifi_client.connect()` was blocking the entire main loop for up to 5s per
  attempt (ESP8266 core default; a `config.h` `ECOS_TIMEOUT` constant existed
  but was never actually applied). With Ecos unreachable, this froze
  `xnet_interface.update()` too, on every backoff-scheduled reconnect -
  explaining XNet never reaching Connected, active-loco count stuck at 0, and
  MultiMaus err13 as one root cause. Fixed by wiring `ECOS_TIMEOUT` into
  `wifi_client.setTimeout()` and dropping it to 300ms. Confirmed on hardware -
  MultiMaus attached immediately and active-loco count tracked speed changes
  correctly with Ecos still down. See [docs/CHANGELOG.md](docs/CHANGELOG.md).
- **XNet status-flap fixed (2026-08-03, two attempts)**: status was
  reverting to Disconnected during completely normal single-throttle idling.
  First attempt added `markBusActivity()` to `onGiveLocoInfo()`/
  `onGiveLocoMM()`/`onPowerStateChange()` (real parsed bus messages that
  weren't counted before - a genuine correctness fix) but live testing
  showed no change. Real root cause, confirmed against the vendored library
  source: Lenz XpressNet's call-byte polling has no "nothing to report"
  acknowledgment, so an idle throttle can legitimately stay silent
  indefinitely - `BUS_TIMEOUT` at 5000ms was just too aggressive for that.
  Raised to 120s (user's call for a single-throttle layout - more throttles
  active would mean more background traffic and could allow tightening
  this) and moved from a private constant in `xpressnet_interface.h` into
  `config.h` as `XPRESSNET_BUS_TIMEOUT`, matching every comparable Ecos-side
  timeout already living there. **Confirmed live**: status now holds
  Connected correctly through widely-spaced throttle commands.
  See [docs/CHANGELOG.md](docs/CHANGELOG.md).
- **OLED display validated against real hardware for the first time (2026-08-03)**:
  physically attached and confirmed initializing (last touched 2026-07-29
  when confirmed to harmlessly no-op with nothing wired - now genuinely
  tested). Several Phase-2-era placeholder fields (device count, IP, CPU
  freq, command/echo counters, last-command info) wired to live data instead
  of hardcoded stubs; UI reworked with hand-drawn status icons (WiFi signal
  bars global/every page, per-interface connection icons page-local -
  explicitly setting the pattern for LocoNet/Z21) after confirming Unicode
  glyphs render as garbage on the real SSD1306 font. Followed up with a
  10-second boot splash (OmniConnect logo, shown immediately at power-up
  while XNet/Ecos connect in the background - see `display/boot_logo.h`) and
  removed the Device Status page's now-duplicate RSSI text. See
  [docs/CHANGELOG.md](docs/CHANGELOG.md).
- **5-minute subscription lifecycle confirmed under real timing (2026-08-03)**:
  left a loco untouched on XpressNet for 5+ minutes - active-loco count
  dropped 2→1 on both the XNet and Ecos pages, confirming `StateEngine`
  purge and `EcosInterface::unsubscribeFromLoco()` both fired correctly.
  This was the last open item from Phase 4.6's checklist - **Phase 4.6 is
  now complete**.
- **Deferred, not part of Phase 4.6** - now tracked as ordered Phase 5 steps
  (see below): deferred OLED display fields (step 8), bus-wide emergency stop
  actually stopping locos (step 2).
- Native unit test suite: 93/93 passing (grew from 91 sometime after the
  XpressNetMaster rewrite - not independently investigated, no regressions).
- ESP8266 (`env:wemos`) firmware build re-verified after the library integration.
- Design premise: forward any XpressNet command to Ecos (and LocoNet/Z21 once
  live); forward any Ecos update for a subscribed loco (or E-stop) to all
  XpressNet/LocoNet/Z21 devices. Only Ecos is a required, known device.

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

### What Phase 4.6 Needed (all done)

All completed and confirmed on real hardware: manual throttle test checklist
(speed/direction/function/128-step/STOP, 2026-07-30), firmware flashing,
WiFi/Ecos credential confirmation, a stable real Ecos TCP connection
(2026-07-31), bidirectional XpressNet↔Ecos command propagation (2026-07-31),
the 5-minute subscription lifecycle under real timing (2026-08-03), and the
OLED display (2026-08-03). Full detail: [docs/CHANGELOG.md](docs/CHANGELOG.md).

(Bus-wide emergency stop still only acknowledges on the wire, not actually
stopping locos - now Phase 5 step 2 - this was always out of scope for this
checklist.)

### After Phase 4.6

See "🎯 Phase 5: Feature Completion" below - a fully-audited, ordered backlog of
everything left incomplete in the existing XpressNet+Ecos feature set (bugs,
deferred display fields, missing accessory support). New protocols (LocoNet,
Z21) and anything else longer-term live in "Future Improvements" near the end
of this file instead.

---

## 🎯 Phase 5: Feature Completion 🔲 NOT STARTED

Goal: finish and harden the existing XpressNet+Ecos feature set - real bugs
found during a full codebase audit (2026-08-03) plus every item deliberately
deferred during Phase 4.6 - before considering new protocols (LocoNet, Z21;
see Future Improvements). CV/programming-track support (`DirectCV`/POM) was
audited too but explicitly pulled out of this phase: the user's Ecos already
handles this conveniently on a program track, so it's not currently planned
at all, not merely deferred.

Ordered by dependency and risk, not just by value - correctness bugs are fixed
before the display/feature work that would otherwise sit on top of wrong data;
the riskiest item (5.9, previously caused a live regression) is scheduled once
everything else gives a clean, tested baseline to attempt it from again.

1. **Dead-code cleanup** (trivial, do first to clear noise): remove unused
   `ecosBuildGetCmd()` (`protocols/ecos/ecos_protocol.h/.cpp`, zero callers),
   unused `LocoState.unknown` and `LocoState.ecos_object_id` fields
   (`definitions.h` - `ecos_object_id` duplicates `EcosInterface`'s own
   internal address map). `nextPage()`/`prevPage()` on `OledDisplay` are also
   unused (no button wired up yet) but harmless - leave unless it's convenient
   to touch the same file anyway.
2. **Bus-wide E-stop actually stops locos** - operating-safety priority.
   `XpressNetInterface::onPowerStateChange()` currently only echoes the
   acknowledgment back onto the bus (fixes the MultiMaus STOP-button display
   freeze from 2026-07-30) but never forces any locomotive to actually stop.
   Needs a new `CommandRouter` path - iterate all known locos in `StateEngine`
   and force speed=0 out to both XpressNet and Ecos - rather than the
   single-address `handleXpressNetCommand` path, plus a decision on how this
   interacts with Ecos-side state.
3. **Outgoing Ecos command queue is fake** - real correctness bug, not a
   missing feature: `EcosInterface::sendSpeedCommand()`, when disconnected,
   calls `queueOutgoingCommand("", 0)` - its own comment says *"Mark for
   queue, but actually just drop"*. The queue/flush machinery
   (`outgoing_queue`/`flushOutgoingQueue()`) is real and runs on reconnect,
   but the actual command text is never captured, so anything sent while
   Ecos is down is silently lost today, not queued as the code structure
   implies.
4. **Ecos function-command merge bug** - also a real correctness bug:
   `EcosReply.functions_mask` (which function bits a given Ecos reply
   actually reported) is parsed but never consulted anywhere.
   `CommandRouter::handleEcosFunctionCommand` overwrites the *entire*
   function bitmap on every Ecos event instead of merging only the reported
   bits, so a partial Ecos function update can silently clobber other
   already-known function state.
5. **OLED function display** ("Fn: (TBD)" on the main page) - cheaper than
   it looks: `LocoState.functions` is already tracked and already used
   elsewhere (answering throttle LocoInfo requests); it just never got
   threaded through `SystemStatus`/`CommandRouter::getSystemStatus()` to the
   display layer. Doing this right after step 4 means the data it displays
   is already trustworthy.
6. **`notifyXNetgiveLocoFunc` handler** - a MultiMaus function-status
   request, unimplemented; same shape as `onGiveLocoMM` (which is handled) -
   same function-handling code path as steps 4-5, same category of gap as
   the LocoInfo bug fixed 2026-07-30.
7. **Function command reconnect-queue parity** - `sendFunctionCommand()`
   currently just drops the command if the Ecos object ID isn't known yet,
   unlike `sendSpeedCommand()` which queues via `queuePendingQuery()`. Same
   Ecos-side function-command code path as steps 3-4-6.
8. **Deferred OLED display fields** (lower-stakes polish, no dependencies):
   XNet "Last Msg" age (needs exposing through `ProtocolInterface`, touching
   the mock used by native tests) and Ecos round-trip latency (needs
   timestamp correlation on the heartbeat query).
9. **XNet "stolen icon" re-attempt** (highest risk - do last among the
   smaller items, once everything else gives a clean baseline to work from):
   the MultiMaus's "stolen" (in-use-elsewhere) icon doesn't visually refresh
   speed/direction when Ecos drives the loco. A previous attempt using the
   library's `ReqLocoBusy()` broke bidirectional propagation after a few
   claim/reclaim cycles and was fully reverted (zero trace left in code) -
   needs careful, instrumented dual-MultiMaus retesting, not a quick patch.
10. **Accessory/turnout support** - the single biggest, fully standalone
    item, scheduled last for that reason. Confirmed total absence: no
    accessory concept exists anywhere in `ProtocolInterface` or
    `CommandRouter`, not even a stub, despite the vendored XpressNetMaster
    library already exposing everything needed
    (`SetTrntStatus`/`notifyXNetTrntInfo`/`notifyXNetTrnt`). Real new
    feature: new interface methods, accessory state storage, routing,
    Ecos-side switch/turnout protocol support, and a display page.

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
- **Ecos** (`protocols/ecos/`): TCP client over WiFi, text-based object protocol
- **LocoNet** (`protocols/loconet/`): Future
- **Z21** (`protocols/z21lan/`): Future

### Configuration
Single source of truth: **`config.h`**
- Feature enable/disable (compile-time toggles)
- Pin assignments (RX, TX, DE, SCL, SDA, etc.)
- Timing constants (timeouts, poll intervals, heartbeats)
- Ecos IP/port (WiFi credentials live in the gitignored `wifi_credentials.local.h` - see Hardware Configuration below)
- Debug flags per component

All disabled features = zero compiled code overhead.

---

## Development Status

- **Phase 1 (Architecture)**: ✅ Complete. `docs/01_DESIGN_DOCUMENT.md`.
- **Phase 2 (Skeleton & Display)**: ✅ Complete. Base classes, state engine, command
  router, OLED driver all implemented.
- **Phase 3.1 (XpressNet Master)**: ✅ Complete, later rewritten around Gahtow's real
  XpressNetMaster library (2026-07-27, see changelog) once hardware arrived.
- **Phase 3.2 (Ecos LAN Protocol)**: ✅ Complete (commit `4c3e0d6`, 2026-07-22). Full
  TCP text-protocol implementation - see Protocol Details below and
  `docs/02_PHASE_3_2_ECOS_DESIGN.md`.
- **Phase 3.3 (LocoNet)** / **Phase 3.4 (Z21 LAN)**: Future, not started.
- **Phase 4.1-4.5 (Testing Infrastructure)**: ✅ Complete. Native build (MinGW-w64 +
  PlatformIO) established, Unity framework integrated, 111/111 tests passing at
  88.3% line / 96.6% function / 58.1% branch coverage across the 5 core modules
  before the Phase 3.1 rewrite (91/91 after, since the superseded XpressNet parser
  tests were removed). Full step-by-step history in the changelog.
- **Phase 4.6 (Hardware Procedures)**: ✅ Complete - see the dedicated section
  above for what was validated and the remaining backlog.
- **Phase 5 (Feature Completion)**: 🔲 Not started - see the dedicated section
  below for the roadmap.

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
  git - see wifi_credentials.local.h.example for the template).
Ecos IP: 192.168.0.50 (config.h, hostname ECOS, confirmed against real hardware)
Ecos Port: 15471 (standard, do not change)
```

### OLED Display (I2C)
```
SDA: D2 (pin 4)
SCL: D1 (pin 5)
Address: 0x3C (or 0x3D, try both if display not detected)
Size: 128x64 pixels
```
Note: `Adafruit_SSD1306::begin()` only fails on internal malloc failure, never on a
missing I2C device - so it logs "initialized successfully" even with nothing wired.
Cosmetic only, not a bug worth fixing.

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
- 14/27/28-step modes are also handled (`notifyXNetLocoDrive14/27/28`), rescaled
  linearly into the same internal 0-126 range, for throttles/locos that don't obey
  the master's 128-step declaration below.

**LocoInfo replies** (`onGiveLocoInfo`/`onGiveLocoMM`): the master answers both the
generic Lenz loco-info request (header 0xE3/data1=0x00) and the Roco MultiMaus's
proprietary variant (header 0xE3/data1=0xF0, different reply format) from local
`StateEngine` state, always declaring `Loco128` - this is what makes throttles use
128-speed-step instead of defaulting to 28-step. Pure master↔throttle wire contract,
no Ecos dependency.

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

**Track power / emergency stop**:
- Per-locomotive stop is just an ordinary speed=0 command, nothing special.
- Bus-wide emergency stop (Notaus/track power) is a separate broadcast the library
  surfaces via `notifyXNetPower(state)`. The master acknowledges it back onto the
  bus (`onPowerStateChange()` → `xnet.setPower(state)`) - this is what fixes the
  MultiMaus STOP-button display freeze. It does **not** yet force any locomotives
  to actually stop moving (Phase 5 step 2).

### Ecos (Phase 3.2 - Complete)

**Protocol**: real ESU Ecos object protocol, text-based over TCP/15471 (NOT XML -
an earlier assumption in this doc was wrong; corrected 2026-07-22).
**Framing**: line-based - `request(id, view)`, `set(id, speed[64])` commands out;
`<EVENT>`/`<REPLY>`/`<END>` block framing in.
**Addressing**: DCC address ↔ Ecos object ID mapping via `queryObjects(10, addr, name)`.
**Lifecycle**: subscribe on first XpressNet command for an address, unsubscribe after
5 minutes of inactivity, auto-resubscribe on next update. Heartbeat query every 30s.
**Echo prevention**: circular queue, 10 entries, 2-second window (accounts for TCP
latency, longer than XpressNet's 500ms window).
**Scope**: locomotives only (accessories deferred until XpressNet supports them).
Full design: `docs/02_PHASE_3_2_ECOS_DESIGN.md`.

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
3. **Ecos**: Responds with current state
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
│   │   └── ecos_interface.h            TCP text-protocol implementation
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
│   ├── 02_PHASE_3_2_ECOS_DESIGN.md    Ecos LAN protocol design
│   ├── 03_FILE_MANIFEST.md            Detailed file descriptions
│   ├── 04_COMPILATION_FIX.md          Include path fixes (reference)
│   ├── 04_PHASE_4_TESTING_INFRASTRUCTURE.md  Phase 4 testing plan
│   ├── 05_COMPILATION_FIX_COMPLETE.md Final verification (reference)
│   └── CHANGELOG.md                   Dated session history (see note at top of this file)
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

### Why Non-Blocking Everything?
XpressNet message windows are ~20-50ms. Blocking for 100ms on Ecos TCP would miss commands. Cooperative multitasking with yield() balances responsiveness.

### Why 5-Minute Loco Timeout?
XpressNet throttles are session-based. After 5 min inactivity, assume loco disconnected. Frees up state engine slots for new locos. Matches typical human usage patterns (user puts throttle down).

### Why No XpressNet Device-Count Tracking?
The bridge doesn't need to know how many throttles exist - it forwards any command
from any throttle and broadcasts any Ecos update to all of them. Device-count
scaffolding was unfinished on both ends (producer and consumer) and was removed
rather than completed, since it served no design purpose.

---

## When Working with Claude Code

- **Always read the current code first**: Don't assume design docs match current reality
- **Respect the 500ms echo window**: Tests timing-sensitive to this value
- **Non-blocking is non-negotiable**: Main loop MUST yield to ESP8266 WiFi stack
- **Test on hardware**: Simulator can't verify serial timing or WiFi connectivity
- **One protocol per PR**: Don't mix XpressNet + Ecos in single change
- **Config.h is the API**: Users modify only that file, nothing else
- **Memory is precious**: 160KB RAM total, 50 locos already use 3KB
- **History goes to the changelog**: when documenting a session's work, put the
  narrative (root cause, investigation, exact numbers) in
  [docs/CHANGELOG.md](docs/CHANGELOG.md); only update this file's status sections
  if the current state actually changed.

---

## Design Documents & Implementation Blueprints

- **`docs/01_DESIGN_DOCUMENT.md`**: Overall architecture and Phase 1-2 specification
- **`docs/02_PHASE_3_2_ECOS_DESIGN.md`**: Ecos LAN protocol (TCP/text, echo prevention, subscription model)
- **`docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md`**: Complete Phase 4 testing plan (12 steps, frameworks, success criteria)
- **`docs/ecos_pc_interface3.pdf`**: Official ESU Ecos PC-Interface protocol spec (German) - the authoritative
  source for real property names/commands (e.g. confirmed `dir` not `direction`) and the Control/View
  registration model. Consult this before guessing at Ecos wire behavior.
- **`docs/CHANGELOG.md`**: Dated development history - full detail behind every status line in this file

## Useful References

- **XpressNet Protocol**: Lenz standard, Gahtow's library docs
- **DCC Standard**: NMRA S-9.1 (loco addressing, speed steps)
- **Ecos Protocol**: ESU documentation (text-based object protocol, subscription model)
- **ESP8266 Constraints**: 160MHz single-core, WiFi pre-empts timing
- **Echo Prevention**: Queue-based (10-item buffer, 2-sec window for TCP latency)
- **State Engine**: Truth table (XpressNet priority, Ecos backfill)

---

## Future Improvements (Beyond Phase 5)

Bus-wide E-stop, accessory/turnout control, and advanced function mapping are
now tracked as ordered Phase 5 steps (see above), not here. CV/programming-track
support was audited during Phase 5 planning but pulled out entirely - not
currently planned, since the user's Ecos already handles this conveniently on
a program track.

- EEPROM storage of WiFi credentials and Ecos IP
- Web-based configuration UI
- OTA (over-the-air) firmware updates
- LocoNet support (parallel to XpressNet)
- Z21 LAN protocol support

---

**Last Updated**: 2026-08-03. **Phase 4.6 is complete** - every checklist item
confirmed on real hardware. **Phase 5 (Feature Completion) is now planned**,
a 10-step ordered roadmap from a full codebase audit of everything deferred
or stubbed in the existing XpressNet+Ecos feature set - see the dedicated
section above. Not started yet; next session should begin at step 1
(dead-code cleanup) and proceed in order.
