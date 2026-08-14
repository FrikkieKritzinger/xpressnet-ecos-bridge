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
> **Deliberately out of scope for Phase 4.6** (tracked separately as Phase 5
> steps - see below): bus-wide emergency stop only acknowledging on the wire
> without forcing locos to stop, and the MultiMaus "stolen icon" not
> refreshing displayed values - both since fixed in Phase 5 (steps 2 and 9); a
> few deferred OLED fields (XNet last-message age, Ecos round-trip latency,
> per-loco functions on the main page) remain open as step 5/8.
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

## 🎯 Phase 5: Feature Completion ✅ COMPLETE (all 10 steps done)

Goal: finish and harden the existing XpressNet+Ecos feature set - real bugs
found during a full codebase audit (2026-08-03) plus every item deliberately
deferred during Phase 4.6 - before considering new protocols (LocoNet, Z21;
see Future Improvements). CV/programming-track support (`DirectCV`/POM) was
audited too but explicitly pulled out of this phase: the user's Ecos already
handles this conveniently on a program track, so it's not currently planned
at all, not merely deferred.

Ordered by dependency and risk, not just by value - correctness bugs are fixed
before the display/feature work that would otherwise sit on top of wrong data.
Step 9 (the riskiest item, previously caused a live regression) was originally
scheduled last, but was deliberately moved up and re-attempted right after
step 4 (2026-08-03) once live testing of step 4 surfaced a second, more severe
symptom from the same underlying gap - see step 9 below for the full story and
why this reordering turned out to be the right call.

1. ✅ **Dead-code cleanup** (2026-08-03) - removed unused `ecosBuildGetCmd()`
   (`protocols/ecos/ecos_protocol.h/.cpp` - its only callers turned out to be
   its own now-deleted unit tests, not production code) and the unused
   `LocoState.unknown`/`ecos_object_id` fields (`definitions.h`). Left
   `nextPage()`/`prevPage()` on `OledDisplay` alone (no button wired up yet,
   but harmless and didn't touch the same file). Native suite 91/91 passing
   (down from 93 - the two `ecosBuildGetCmd` tests were removed with it, not
   a regression); firmware builds clean and flashed.
2. ✅ **Bus-wide E-stop actually stops locos, both directions confirmed on
   real hardware (2026-08-03)**. `onPowerStateChange()`'s existing
   wire-protocol echo is untouched; it now also calls
   `CommandRouter::emergencyStopAll(LocoSource)`/`resumeOperation(LocoSource)`.
   `emergencyStopAll()` zeroes every known loco's speed in `StateEngine` and
   broadcasts it to XpressNet (direction untouched), plus tells whichever
   side didn't originate the request - Ecos gets one real system-wide stop
   (`set(1, stop)`, confirmed against the official ESU PC-Interface spec
   section 7.1 as "equivalent to the STOP button on the Ecos", not a
   per-loco loop), XpressNet gets a bus broadcast via new
   `XpressNetInterface::sendEmergencyStop()`/`sendResumeOperation()`
   overrides. `resumeOperation()` deliberately never restores any loco's
   previous speed - the operator must re-throttle manually, matching real
   command-station safety behavior.
   - **Confirmed live, XNet→Ecos direction**: MultiMaus STOP/GO reaches Ecos
     immediately.
   - **Ecos→XNet direction needed a second round of fixes** after initial
     live testing showed nothing propagated back: (1) we had never
     subscribed to Ecos's own base system object (id=1) as a View at all -
     added `request(1, view)` on connect; (2) that subscribe call used a
     40-byte buffer while `ecosBuildRequestCmd()` requires ≥60 and silently
     returns 0 below that, so the subscription was never actually sent in
     the first place - fixed to match every other call site's 80-byte
     buffer; (3) added case-insensitive matching for the `status`
     property/`stop`/`go`/`shutdown` values (real hardware's exact casing
     vs. the spec's documented example was unconfirmed) plus a diagnostic
     fallback logging any unrecognized reply/event instead of silently
     dropping it. **Now confirmed live, both directions.**
   - 21 new unit tests total across this feature (`test_command_router`,
     `test_ecos_command_builder`, `test_ecos_parser`); native suite
     112/112 passing.
3. ✅ **Outgoing Ecos command queue is fake - fixed and confirmed live
   (2026-08-03)**. The old `EcosInterface::sendSpeedCommand()` special-cased
   "not connected" by calling `queueOutgoingCommand("", 0)` - its own
   comment admitted *"mark for queue, but actually just drop"* - so commands
   issued while Ecos was down were silently lost, not queued as the
   surrounding machinery implied. Fixed by removing that special case
   entirely and the whole dead `outgoing_queue`/`flushOutgoingQueue()`
   subsystem it lived in: while disconnected, `address_map_count` is always
   0 (cleared on disconnect), so `findEcosObjectId()` naturally returns 0,
   which already routes into `queuePendingQuery()` - the real
   address+speed+direction queue used for "loco not resolved yet" while
   connected. One correct mechanism instead of two, one of which never
   worked. Two related bugs fixed in the same pass since they'd have
   undermined the fix otherwise: `flushPendingQueries()` only ever replayed
   speed, silently dropping direction on every deferred command; and
   `queuePendingQuery()` was append-only with no dedup by address, so
   repeatedly changing one loco's speed while disconnected would have
   filled the small `MAX_PENDING_QUERIES` buffer with stale entries and
   silently dropped later commands - now upserts by address instead.
   - **Live testing surfaced two more real bugs beyond the original fix**:
     (a) the message-timeout disconnect path (`now - last_message_time >
     ECOS_MESSAGE_TIMEOUT`) never cleared `address_map_count`, unlike the
     `!wifi_client.connected()` path - but that's exactly the path that
     fires when Ecos's Ethernet cable is physically unplugged (no TCP reset
     is ever generated), so the stale map kept resolving real object IDs
     and commands went out via direct write into a dead socket instead of
     queuing. Fixed by clearing it there too. Also tightened
     `ECOS_HEARTBEAT_INTERVAL`/`ECOS_MESSAGE_TIMEOUT` from 30s/45s to
     5s/10s, cutting worst-case detection lag ~4.5x.
     (b) Direction landed correctly after a reconnect-triggered flush, but
     speed read back as 0 - traced to command order: `sendSpeedCommand()`
     and `flushPendingQueries()` both sent speed before direction. Real DCC
     decoders receive speed+direction combined in one packet, not as two
     independent properties, so Ecos likely constructs that packet from
     whatever it has cached for both - sending speed first meant a
     genuinely-changing direction got built against a not-yet-updated
     (stale) speed. Reordered to direction-before-speed in both places;
     confirmed live - both now land correctly a couple seconds after
     reconnecting.
   - Added a debug line confirming when `flushPendingQueries()` actually
     sends a queued command - that path had zero visibility before, which
     is part of why this took several rounds to isolate.
   - No native test coverage for this file (`ecos_interface.cpp` is
     excluded from the native build, real-hardware/WiFi-coupled - matches
     its existing testing boundary); native suite 112/112 passing
     throughout (config/logic changes elsewhere unaffected).
4. ✅ **Ecos function-command merge bug - fixed and confirmed live (2026-08-03)**.
   `EcosReply.functions_mask` was parsed but never consulted anywhere -
   `CommandRouter::handleEcosFunctionCommand` overwrote the *entire*
   function bitmap on every Ecos event instead of merging only the reported
   bits, so a single Ecos event reporting just one changed function (e.g.
   only F3) would silently clobber every other already-known function back
   to 0. Fixed: `handleEcosFunctionCommand()` now takes a `functions_mask`
   parameter and merges - `(existing & ~mask) | (functions & mask)` -
   preserving every bit not covered by this specific event. Found a second,
   more fundamental bug in the same area while wiring this up:
   `functions_mask` was declared `uint8_t` (only 8 bits) with a plain `int`
   shift, so `1 << fn_index` for fn_index 8-31 silently truncated to 0 on
   assignment - any function at F8 or above never actually registered in
   the mask at all, regardless of merge logic built on top of it. Widened
   to `uint32_t` and fixed the shift to `1UL <<`, matching the pattern
   already used correctly for `functions` itself one line above. This area
   had zero existing test coverage (no `func[]` parsing tests existed in
   `test_ecos_parser` at all) - added 4 parser tests plus 2
   `test_command_router` merge tests; native suite 118/118 passing.
   **Confirmed live**: via Ecos, turning two different functions on one at a
   time correctly left both on simultaneously (F1 then F1+F4, merged
   bitmap `0x02` then `0x12`) - the old bug would have reset the first back
   off the instant the second event arrived. The live test also surfaced a
   much bigger, previously-unknown symptom (functions/speed/direction
   appearing to cross-contaminate on the MultiMaus's display) - see step 9,
   which turned out to be a separate, deeper issue this step's live test
   exposed rather than a flaw in the merge fix itself.
5. ✅ **OLED function display - implemented and confirmed live (2026-08-03)**.
   Replaced the main page's "Fn: (TBD)" placeholder with a comma-separated
   list of active function numbers (e.g. "0,3,7"), truncated with "..." if
   it overflows the line's ~17 usable characters
   (`OledDisplay::buildActiveFunctionsLabel()`) - a hex bitmask was
   considered and rejected (not human-readable at a glance); a full
   dedicated grid page was considered too (would use the ~48px blue area
   properly) but deferred as a separate, bigger feature rather than this
   "cheap" step's scope. `LocoState.functions` was already tracked; new
   `SystemStatus.last_command_functions` threads it through
   `CommandRouter::getSystemStatus()`.
   - **Real gap found and fixed in the same pass**: only speed commands
     ever updated `last_command` at all - `handleXpressNetFunctionCommand()`
     and `handleEcosFunctionCommand()` never touched it, so a pure function
     toggle (e.g. flipping a headlight, the most common real interaction)
     wouldn't have updated the OLED's "Last:" fields at all. Fixed by
     making all four command handlers consistently update
     `last_command.address/speed/direction/functions/source`.
   - **First live test caught a real truncation bug**: with 9+ functions
     active, the list silently stopped after 8 with no "..." at all -
     `buildActiveFunctionsLabel()` only checked for room to append "..."
     *after* failing to fit the next entry, by which point the buffer
     could already be completely full. Fixed by reserving 3 bytes for a
     potential "..." upfront (before filling entries), so truncation is
     always signaled correctly. Confirmed live.
   - 4 new `test_command_router` tests (all four handlers correctly
     surface `last_command_functions`); native suite 126/126 passing.
     `buildActiveFunctionsLabel()` itself has no native coverage - lives in
     `oled_display.cpp`, excluded from the native build like the rest of
     the display layer (verified live on real hardware instead, matching
     that file's existing testing boundary).
6. ✅ **`notifyXNetgiveLocoFunc` handler - implemented, verified by code
   review + clean builds only (2026-08-05)**. XpressNet header `0xE3`,
   `data1=0x09` is a standard Lenz throttle request for a loco's F13-F28
   status, distinct from `onGiveLocoInfo` (F0-F12, `0x00`) and
   `onGiveLocoMM` (MultiMaus's own combined request, `0xF0`) - previously
   unimplemented, so the library silently dropped it. New
   `XpressNetInterface::onGiveLocoFunc()` mirrors the existing two handlers
   exactly (`markBusActivity()`, `resolveLocoStateForReply()`) and replies
   via the vendored library's `SetFktStatus()` - a method that already
   existed for exactly this reply but had never been called from our code.
   - **Could not get a live trigger**: real MultiMaus hardware uses the
     combined `0xF0` request instead of the standard `0x00`+`0x09` pair, so
     with only MultiMaus units to test against, this specific request type
     never fires in practice - confirmed by exercising a MultiMaus through
     a full range of function toggles (groups 1/4/5, i.e. F0-F4 and
     F13-F28) and finding zero `onGiveLocoFunc` activity in the debug log,
     while the already-working `onLocoFunctionGroup` path correctly
     received every one of those toggles. Not a sign the fix is wrong -
     just an untriggerable path with this hardware. Verified instead by
     code review (identical pattern to the two already-proven handlers)
     and clean `env:native` (126/126) + `env:wemos` builds.
   - This session's serial-monitor tooling was also unusually unreliable
     (stuck/zombie python processes not responding to normal or
     WMI-forced termination, intermittent "Access is denied" on the COM
     port, one capture showing a garbled repeating-fragment artifact) -
     resolved by a full PC reboot, unrelated to any firmware change here.
7. ✅ **Function command reconnect-queue parity - fixed and confirmed live
   (2026-08-05)**. `EcosInterface::sendFunctionCommand()` used to silently
   drop the command entirely if the Ecos object ID wasn't known yet
   (disconnected, or address not yet in the map) - unlike
   `sendSpeedCommand()`, which queues via `queuePendingQuery()` (the step 3
   fix). A second, related gap found in the same pass:
   `sendFunctionCommand()` still had an `if (current_status != CONNECTED)
   return;` guard at the top - the exact pattern step 3 deliberately
   removed from `sendSpeedCommand()`, since a master should keep
   transmitting regardless and `findEcosObjectId()` already naturally
   returns 0 while disconnected (address map clears on disconnect),
   routing correctly into the queue.
   - **Fix**: `PendingQuery` extended with `functions`/`has_functions`
     fields alongside the existing `speed`/`direction` (now
     `has_speed_direction`), so a speed change and a function change
     queued for the same loco while disconnected merge into one upserted
     entry instead of competing for the small `MAX_PENDING_QUERIES`
     buffer - same upsert-by-address pattern already used for speed.
     New `queuePendingFunctionQuery()` mirrors `queuePendingQuery()`.
     `flushPendingQueries()` now replays only the field(s) each entry
     actually has queued (not both unconditionally, which would send a
     placeholder speed=0/dir=1 alongside a pure function-only queued
     entry, or vice versa - same class of bug as step 9's
     `has_speed`/`has_direction` fix). `sendFunctionCommand()` itself
     queues via `queuePendingFunctionQuery()` on `obj_id == 0` instead of
     returning.
   - **Confirmed live**: disconnected Ecos (unplugged its LAN cable),
     toggled two functions on the MultiMaus while disconnected, reconnected
     Ecos - both functions correctly landed on Ecos once it came back
     online, instead of being silently lost as before.
   - No native test coverage added - `ecos_interface.cpp` is excluded from
     the native build (hardware-coupled), same existing testing boundary
     as the rest of this file; native suite unaffected, 126/126 passing.
8. ✅ **Deferred OLED display fields - implemented and confirmed live
   (2026-08-05)**. Both `ProtocolInterface::getLastMessageAgeMs()` and
   `getLastHeartbeatLatencyMs()` (new virtuals, `NO_TIMESTAMP` sentinel
   default) thread real values into `SystemStatus`/
   `CommandRouter::getSystemStatus()`, replacing the XNet page's
   "Last Msg: N/A" and the Ecos page's "Latency: N/A" placeholders.
   - **XNet "Last Msg" age**: `XpressNetInterface` already tracked
     `last_message_time` internally (for `BUS_TIMEOUT`) - just needed
     exposing. Displayed as elapsed seconds.
   - **Ecos round-trip latency**: `EcosInterface` already set
     `address_map_last_refresh = millis()` when sending a query, but never
     read it back anywhere - reused it as the "sent at" timestamp instead
     of adding a new field. New `awaiting_query_reply` flag, set in
     `queryAddressMap()` right after writing the request, cleared in
     `handleReply()`'s address-map-entry branch on the first reply entry
     seen since - not a per-request-ID correlation, but address-map
     queries only ever overlap with themselves (both the 5s heartbeat and
     the less-frequent scheduled refresh call the same
     `queryAddressMap()`), so "most recent send, first reply since" is an
     honest measurement in practice. Displayed as milliseconds.
   - Both fields default to a `NO_TIMESTAMP` sentinel
     (`(unsigned long)-1`) rather than `0`, since `0` is a legitimate real
     value for both age and latency.
   - 4 new `test_command_router` tests (each field surfaced correctly when
     present, and correctly defaults to `NO_TIMESTAMP` when no interface is
     set) using new `MockProtocolInterface` setters; native suite 130/130
     passing. `oled_display.cpp` itself has no native coverage - excluded
     from the native build like the rest of the display layer, confirmed
     live on real hardware instead.
9. ✅ **XNet "stolen icon" display refresh - fixed and confirmed live
   (2026-08-03)**. Moved up and re-attempted right after step 4 (see above)
   once live testing there showed an Ecos-driven function change getting
   silently reverted by the MultiMaus's own next report for that same
   function group - a real, live-observed consequence of this gap, not
   just a cosmetic one.
   - **Root cause, found via a real two-MultiMaus bus-instrumented
     investigation** (temporary `Serial.printf`s added directly to the
     vendored `XpressNetMaster` library's `AddBusySlot`/`SetBusy`/
     `SetLocoBusy`, removed again once diagnosed): a genuine MM-to-MM steal
     keeps both displays in sync because the "losing" MultiMaus directly
     overhears the "winning" one's own raw reply on the shared RS485 bus -
     a real slave's reply is unmarked data (no 9th-bit call-byte) sent
     immediately after the master's call-byte addressed to that slot's own
     number. Our own `sendSpeedCommand()`/`sendFunctionCommand()`
     broadcasts (and an addressed `SetLocoInfoMM` reply, tried first and
     confirmed to make no difference) don't reproduce that same timing, so
     a MultiMaus flashing "stolen" doesn't trust them enough to refresh its
     displayed values - it does flash the icon, just without updating what
     it shows underneath.
   - **Fix**: new `XpressNetMasterClass::PushExternalLocoUpdate()`
     (`libraries/XpressNetMaster`) marks the address busy under a
     dedicated fake slot (`XNetExternalControllerSlot` = 30, evicting
     whichever real slot currently owns it, exactly as a genuine steal
     would) and then sends `[call-byte addressed to that slot][unmarked
     reply data]` for both the F0-F4 function group and speed/direction -
     i.e. reproducing the exact call-byte-then-reply sequence a real
     throttle's own transmission has. `CommandRouter::broadcastCommand()`
     calls this (via `ProtocolInterface::pushLocoStateToOwningSlot()`, a
     new no-op-by-default virtual, XpressNet-only) alongside the existing
     broadcast whenever Ecos is the source. **Confirmed live across many
     cycles**: both MultiMaus units correctly flash stolen, and the
     headlight/speed/direction values now genuinely refresh on the
     display, matching a real steal's behavior.
   - Only F0-F4 are covered (the group this was tested against) - F5-F31
     still only go out via the plain broadcast and may not refresh a
     "stolen" MultiMaus's display for those functions. Not yet needed in
     practice; revisit if a higher function ever shows the same gap.
   - **A second, independent bug found and fixed during the same live
     testing**: `CommandRouter::handleEcosCommand()` used to always apply
     both `speed` and `direction` from every Ecos event, even when Ecos
     had only reported one of the two (e.g. a pure speed change) - the
     unreported field silently reset to whatever placeholder value
     accompanied it, observed live as direction reverting to its old value
     the instant speed next changed after a direction-only update. Fixed
     with `has_speed`/`has_direction` parameters (default `true`, so
     existing 3-argument call sites are unaffected) that make
     `handleEcosCommand()` only overwrite the field(s) Ecos actually
     reported - the same class of fix as step 4's `functions_mask`, just
     for speed/direction. `EcosInterface::handleReply()` now passes
     `reply.has_speed`/`reply.has_direction` through. 2 new
     `test_command_router` regression tests. This is very likely also what
     actually broke the earlier, fully-reverted `ReqLocoBusy()` attempt
     (tracked in the changelog at the time as "not fully root-caused") -
     that bug existed then too and was never fixed until now.
   - **Known, deliberately-parked limitation, unrelated to the above**:
     Ecos's dedicated hardware direction switch doesn't generate any
     network event on its own (not even an unrecognized one) - only
     combined with a following speed change (e.g. crossing the zero-speed
     detent) does a real event reach the bridge. Best guess: Ecos treats
     the switch as a local UI latch that only gets pushed onto the wire
     alongside the next speed command. The detent-crossing method works
     as a full substitute and reaches every code path the dedicated switch
     would, so this is flagged and put on hold rather than chased further.
   - Native suite 122/122 passing (2 new tests from the speed/direction
     merge fix; the push-mechanism itself has no native coverage, same
     `ecos_interface.cpp`/hardware-coupled boundary as the rest of that
     file - `pushLocoStateToOwningSlot()`'s *call site* in
     `CommandRouter::broadcastCommand()` is covered by mock-based tests).
10. ✅ **Accessory/turnout support v1 - implemented and confirmed live
    (2026-08-05)**. Scoped down from the original full-parity plan after a
    design discussion: v1 is **XpressNet → Ecos only** (a throttle throws a
    turnout, Ecos receives it) - the Ecos → XpressNet direction and a
    dedicated OLED accessory page are **deliberately deferred/on hold**,
    not planned as a near-term follow-up. Reasoning: v2 would mean fully
    replicating the loco-parity address-map + per-accessory subscription
    machinery a second time, for a benefit that's purely cosmetic (keeping
    a throttle-side display in sync) - a real MultiMaus's own accessory
    control was confirmed live to always resend a fresh command on every
    button press regardless of what it currently believes the state is, so
    there's no functional gap without v2, only a possible display
    staleness. A "Last accessory: addr X → Str/Div" line was added to the
    main OLED page instead of a dedicated page, symmetric to the existing
    loco line - the bridge only ever knows what it last commanded, not
    Ecos-confirmed truth, so that's an honest scope for what a full page
    would otherwise imply.
    - **Wire protocol**: `XpressNetInterface::onTurnoutCommand()` (new)
      wired to the vendored library's `notifyXNetTrnt`/`0x52`/`0x53`
      (previously unimplemented). Reacts only on the activate edge (data
      bit3=1) - real hardware testing confirmed a MultiMaus sends both an
      activate and, a moment later, a deactivate for the same press;
      that's needed for real DCC decoders' physical pulse timing, but
      Ecos's own `set(11, switch[...])` is a single complete command and
      handles the actual pulse generation on its own side, so the
      deactivate half is simply ignored.
    - **Ecos side**: `set(11, switch[DCC<address><port>])` sent directly
      against the fixed `ECOS_OBJECT_ACCESSORY_MANAGER` (id=11, official
      ESU spec section 7.4) - no per-accessory object ID lookup or
      address-map needed at all, unlike locomotives, since this command
      addresses by protocol+address+port directly.
    - **Two real bugs found via live testing, both fixed and confirmed**:
      (1) the accessory address arriving at Ecos was consistently one
      lower than the address entered on the MultiMaus (e.g. MultiMaus
      address 3 reached Ecos as address 2) - tracked down to the *official
      Lenz XpressNet specification itself* (section 3.38): the wire-level
      address field is explicitly defined as `(turnout_number - 1) / 4`,
      a real, documented 0-vs-1-indexing convention specific to accessory
      addressing (locomotive addressing has no such offset). The vendored
      library's existing bit-recombination formula (`data1<<2 |
      port_bits`) already reverses the "divide by 4" part, so what reaches
      our code is exactly `turnout_number - 1`; a `+1` correction in
      `onTurnoutCommand()` reconstructs the address the operator actually
      intended, applied before the value reaches either the OLED display
      or the Ecos command. Confirmed this is spec-mandated behavior every
      compliant XpressNet device must handle, not a MultiMaus quirk that
      would break interop with other DCC controllers. (2) the straight/
      diverging port letters (`r`/`g`) were inverted from an initial
      context-inferred guess (the official spec's own example doesn't
      spell out their meaning) - corrected to r=straight, g=diverging
      after live testing showed the opposite of what was expected.
    - **A risky diagnostic detour, reverted**: while investigating the
      addressing question, temporary `Serial.printf()` calls were added
      directly inside the vendored library's time-critical bus-dispatch
      switch statement to inspect raw wire bytes - this froze real
      XpressNet communication (MultiMaus showed "err13") because
      `Serial.printf()` at 115200 baud is slow enough to blow XpressNet's
      tight response-time budget when it runs inside the message-dispatch
      hot path. Fully reverted (confirmed via `git diff`/`git checkout`)
      before any further work; the addressing question was root-caused
      afterward from the official spec text instead, with no further
      hardware-touching diagnostics needed.
    - 8 new tests (5 `test_command_router`, 3 `test_ecos_command_builder`)
      covering the builder's straight/diverging output and the router's
      forwarding/validation/`SystemStatus` surfacing; native suite
      138/138 passing. `onTurnoutCommand()` itself has no native
      coverage - same hardware-coupled boundary as the rest of
      `xpressnet_interface.cpp`.
    - **This closes out all of Phase 5** - steps 1-10 are now done (step 6
      verified by code review only, everything else confirmed live).

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
- **Phase 5 (Feature Completion)**: ✅ Complete - all 10 steps done (step 6
  verified by code review only; step 10 is v1-scoped, Ecos→XpressNet and a
  dedicated display page deliberately deferred) - see the dedicated section
  below for the full roadmap.

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
- **Tag every major milestone**: when a phase (or an equally large, independently
  shippable unit of work) completes, commit the final state, then create an
  annotated git tag on that commit (`vMAJOR.MINOR.0-<slug>`, e.g.
  `v0.5.0-phase5-complete`) summarizing what the milestone covered, and push
  both the commit and the tag to origin - unprompted, as part of closing out
  the milestone, the same standing authorization that already covers
  CLAUDE.md commits/pushes at checkpoints. Don't tag individual steps within a
  phase, only the phase (or equivalent) completing.

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

**Last Updated**: 2026-08-05. **Phase 4.6 is complete** - every checklist item
confirmed on real hardware. **Phase 5 (Feature Completion) is now complete -
all 10 steps done**, a 10-step ordered roadmap from a full codebase audit of
everything deferred or stubbed in the existing XpressNet+Ecos feature set -
see the dedicated section above. Full narrative detail for every step lives
in `docs/CHANGELOG.md` (dated entries); this section is intentionally just
current status.

Most recent: **step 10** (accessory/turnout support) implemented and
confirmed live, scoped to v1 (XpressNet → Ecos only; the reverse direction
and a dedicated display page deliberately deferred - see the step 10 entry
above for why). Two real bugs found and fixed via live testing: an
addressing offset traced to the official Lenz XpressNet spec's own
`(turnout_number-1)/4` formula (a documented protocol convention, not a
MultiMaus quirk), and inverted straight/diverging port letters. A risky
diagnostic detour (temporary `Serial.printf()`s inside the vendored
library's time-critical bus dispatch, which froze real XpressNet
communication) was fully reverted before the actual root cause was found
from spec text instead. Native suite 138/138 passing; `env:native` and
`env:wemos` both build clean.

With Phase 5 complete, there's no active roadmap item right now - see
"Future Improvements" above for longer-term, not-yet-scheduled ideas
(LocoNet, Z21, accessory v2, etc.) if picking up new work.
