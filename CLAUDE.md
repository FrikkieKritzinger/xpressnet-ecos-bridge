# XpressNet-Ecos Bridge - Development Guide

**Project**: Model railway protocol bridge for ESP8266 (Wemos D1 Mini)  
**Purpose**: Translate commands between XpressNet (hardwired RS485), Z21 LAN (UDP), LocoNet and ESU Ecos (WiFi TCP)  
**Language**: C++ (PlatformIO / Arduino framework)  
**Target**: ESP8266 microcontroller (160MHz, 4MB Flash, 160KB RAM)

> ℹ️ **How this file is maintained**: `CLAUDE.md` carries only what's pertinent to
> the current and next session - architecture, conventions, config reference, and
> current status. Dated session narratives (root causes, debugging steps, exact
> numbers) go to [docs/CHANGELOG.md](docs/CHANGELOG.md) instead. When updating
> after a session, add detail to the changelog and only touch this file's status
> sections if the current state actually changed.

> 🔗 **Shared cross-project notes** (also used by other projects under
> `E:\Claude\`) - record reusable LocoNet/ESP8266 learnings there, not only here:
> @E:/Claude/shared/esp8266-platform.md
> @E:/Claude/shared/loconet.md

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
- Design premise (confirmed by the user 2026-10-26): a change on any interface is
  mirrored to Ecos AND every other interface (XpressNet, Z21, LocoNet); an Ecos
  update for a subscribed loco (or E-stop) goes to all of them. Ecos is the DCC
  interface and the source of truth. Only Ecos is a required, known device.

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
   - **Limitation noted here at the time, since resolved (confirmed
     2026-08-31)**: Ecos's dedicated hardware direction switch used to not
     generate any network event on its own - only combined with a
     following speed change (e.g. crossing the zero-speed detent) did a
     real event reach the bridge. The detent-crossing method was a full
     working substitute at the time, so this was flagged and parked
     rather than chased. Confirmed fixed as an incidental side effect of
     Phase 6 step 4's echo-handling fixes (2026-08-28) - the user
     directly tested both direction-change mechanisms afterward and
     confirmed both now work correctly on XpressNet and Z21.
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
Protocol Layer     → XpressNet, Ecos, Z21 LAN, LocoNet (all live)
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
- Echo prevention: `wasRecentSource()` (4 s window) says which throttle protocol last commanded a loco; `isEchoOfRecentCommand()` then drops an Ecos update for THAT protocol only if it carries a value the throttle itself just set (function bits it turned on/off are remembered per loco in `noteThrottleFunctionChange()`); a different change from Ecos inside the window is forwarded
- Handles unknown loco subscription requests to Ecos
- Fans every throttle command out immediately to Ecos and all other throttle-facing protocols; Ecos-originated changes fan out to all throttle protocols
- Caches best-known accessory positions to answer throttle state queries (XpressNet poll, Z21 `GET_TURNOUT_INFO`, LocoNet `OPC_SW_STATE`); a throttle's turnout change is forwarded to Ecos and every other interface directly
- `trackLocoForThrottle()`: a throttle that merely SELECTS a loco subscribes the bridge to it on Ecos, so the first Ecos changes are not lost

**Protocol Interfaces** (inherit from `ProtocolInterface`)
- **XpressNet** (`protocols/xpressnet/`): Master device on RS485, accepts throttle commands
- **Ecos** (`protocols/ecos/`): TCP client over WiFi, text-based object protocol
- **Z21 LAN** (`protocols/z21lan/`): UDP server on port 21105 emulating a Z21 (WLANmaus); packet codec in `z21_protocol.*`, socket/session table in `z21lan_interface.*`
- **LocoNet** (`protocols/loconet/`): slot server for Digitrax throttles (UT6), receive on UART0 + SoftwareSerial TX, `ENABLE_LOCONET` = 1 (Phase 7 item 3; slot ownership stays internal, never gates the router)

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
- **Phase 3.3 (LocoNet)**: done as Phase 7 item 3 (released `v1.5.0`, refined through `v1.5.4`, 2026-10-26).
- **Phase 3.4 (Z21 LAN)**: done as Phase 6 step 4 (confirmed live 2026-08-28).
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
- **Phase 6 (Future Improvements)**: ✅ Complete - steps 1-4 done and
  confirmed live (EEPROM storage, web config UI/Setup Mode, OTA updates,
  Z21 LAN for WLANmaus), tagged `v1.1.1`. LocoNet (originally step 5) is
  blocked on hardware the user doesn't have yet and moves to Phase 7
  instead of holding this phase open; the open/TBD slot (step 6) carries
  forward as Phase 7's own open slot. See the dedicated section below.
- **Phase 7 (TBD)**: 🔧 In progress - items 1-4 and 7 done (Z21 turnouts, turnout v2, LocoNet, "stolen" refresh, function display sync), 5-6 open/low priority; see the dedicated section below.

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
- Free heap on the `wemos_dev` build is ~13 KB (the OLED warns below 10 KB); the 30 s `Heap:` log line tracks it. Static RAM matters: the debug log ring is 2 KB (`NET_LOG_BUFFER_SIZE`), and Setup Mode builds its pages in a per-request buffer instead of a permanent array

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

## MCU Platform

**Current and only supported target: ESP8266 (Wemos D1 Mini).** Discussed
2026-09-23 whether to branch onto an ESP32-based "D1 Mini" board (dual-core,
which the user has since ordered a few of) instead of or alongside the
ESP8266 build. Decided explicitly: **not now, and not as a permanently
co-maintained fork** - finish the remaining ESP8266 roadmap (LocoNet is now done, 2026-10-26;
only deferred items remain) and get it stable first; treat ESP32 as this project's "v3",
a deliberate future migration to evaluate once the ESP8266 version is done,
not a parallel branch to maintain indefinitely alongside it.

Investigated Philipp Gahtow's own ESP32 support before that decision (same
author as the vendored `XpressNetMaster` library and the Z21 reference
already used repeatedly this project): the **library itself already has
real ESP32 support built in** - every platform branch in the vendored
`.cpp`, including the tricky 9th-bit call-byte parity handling on the
software-serial write, is unified `#if defined(ESP8266) || defined(ESP32)`,
no separate ESP32 code path needed there. But his own **published ESP32
command-station firmware is explicitly marked "IN DER ENTWICKLUNG - STILL
UNDER DEVELOPMENT!"**, with a documented requirement to patch the ESP32
Arduino core itself to fix timer-interrupt crashes, plus known gaps (EEPROM
write issues, S88N untested, no RailCom/DS18B20 support) - genuinely less
mature than the ESP8266/D1 Mini design this project is built on. Also
notable: his ESP32 design does **not** use dual-core task pinning for
XpressNet timing - the "dedicate a core to bus polling, stop fighting the
WiFi stack for timing" benefit that's the main draw of ESP32 here would be
this project's own work to add, not something inherited for free.
Reference: [Z21 Arduino Zentrale (ESP32)](https://pgahtow.de/w/Z21_Arduino_Zentrale_(ESP32)).

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

### Setup Mode button
```
Pin: D7 (GPIO13) to GND, INPUT_PULLUP - hold 3s while running to enter Setup Mode
```

### LocoNet (live since 2026-10-23, motherboard PCB v2.1)
```
TX: D3 (GPIO0) through a BC547, INVERTED (GPIO high = bus low)
RX: GPIO3 (hardware UART0 RX) - 16660 baud 8N1, not inverted
```
Per Gahtow's D1 Mini design (internal pull-ups). UART0 belongs to LocoNet, so USB-serial
upload and serial debug do not work while the D1 is seated: flash with the D1 pulled, or
over the network (`wemos_dev` + `tools/devflash.py`, below), and read debug text from the
TCP log on port 23 (`tools/netlog.py`). The board's LocoNet master jumper (12 V) must be
fitted or a throttle never transmits. The UT6 powers from the LocoNet socket.

### Serial Monitor (Debug)
```
Baud: 115200 (DEBUG_BAUD in config.h)
Use `python -m platformio device monitor` (DTR/RTS are disabled in platformio.ini so opening it does not reset the board) or any serial terminal
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
  MultiMaus STOP-button display freeze. Since Phase 5 step 2 it also forces every
  known loco to speed 0 and tells Ecos (single system-wide `stop`), in both
  directions; resuming never restores previous speeds.

### Ecos (Phase 3.2 - Complete)

**Protocol**: real ESU Ecos object protocol, text-based over TCP/15471 (NOT XML -
an earlier assumption in this doc was wrong; corrected 2026-07-22).
**Framing**: line-based - `request(id, view)`, `set(id, speed[64])` commands out;
`<EVENT>`/`<REPLY>`/`<END>` block framing in.
**Addressing**: DCC address ↔ Ecos object ID mapping via `queryObjects(10, addr, name)`.
**Lifecycle**: subscribe on first XpressNet command for an address, unsubscribe after
5 minutes of inactivity, auto-resubscribe on next update. Heartbeat (address-map
query) every 5s, dead-link watchdog 10s, reconnect backoff 5/10/20/60s.
**Echo prevention**: a single last-command attribution record in `CommandRouter`
(address, type, source, timestamp); `wasRecentSource()` skips only the destination
protocol that originated the command, within `ECOS_ECHO_ATTRIBUTION_WINDOW_MS`
(4s - Ecos's real confirmation latency under load). The original circular-queue
design was replaced in Phase 6 step 4.
**Scope**: locomotives and accessories (Phase 5 step 10 / Phase 7 steps 1-2).
Full design: `docs/02_PHASE_3_2_ECOS_DESIGN.md`.
**Function inversion**: Ecos handles F8-F11 inverted (the real loco follows Ecos's screen,
while the wire value is the opposite of what the throttles show), so
`ECOS_INVERTED_FUNCTIONS` (config.h, default `0x0F00`) flips those bits on the way to and
from Ecos, inside `EcosInterface` only.
**Accessories**: `set(11, switch[...])` to throw; each accessory object is subscribed
with `request(view)` plus `get(id, state)` so the bridge knows every position from the
start (a throttle's state query can be answered for a turnout nobody has moved).

### XpressNet reliability (2026-10-26, the lessons behind v1.5.x)

Several overlapping causes made XpressNet frames get lost or corrupted; each is fixed:
- **CPU clock**: the board silently ran at 80 MHz; `board_build.f_cpu = 160000000L`
  (in `env:wemos`) gives the software-serial receive (16 us per bit) enough margin.
- **Wi-Fi modem sleep** adds interrupt-latency spikes that corrupt one-shot frames (a
  function press); `WiFi.setSleepMode(WIFI_NONE_SLEEP)` is set before `WiFi.begin()`.
  The unit is mains powered and the user prefers responsiveness over power saving.
- **The vendored library's TX queue** held 5 messages with no overflow check, so any burst
  of more than five overwrote unsent ones (scrambling the bus). Now `XNetBufferSize` 12,
  and a call byte plus its message are sent as ONE queue entry. Never queue a call byte
  and a message separately.
- **Refreshing a MultiMaus display** for a change made elsewhere: a MultiMaus applies
  another throttle's message it OVERHEARS, plus a directed `E3 40` "loco busy" notice
  (learned by sniffing a real Z21 with `env:wemos_sniff`). `QueueLocoRefresh()` /
  `flushPendingRefresh()` in the library send, at the start of a polling window and at
  most once per 100 ms per loco: the changed group's `E4 20/21/22` message (and speed)
  after an inquiry call byte of the unused slot 30, then `E3 40` to each slot holding
  the loco (or up to 3 known devices if none is registered). Switches:
  `XPRESSNET_REFRESH_IN_POLL_WINDOW 1`, `XPRESSNET_REFRESH_MODE 2`. The old out-of-window
  injection (`XPRESSNET_PUSH_OWNING_SLOT`) stays OFF: it corrupted frames.
- **Confirmation reply**: a function frame from a throttle is answered with the loco's
  info in that throttle's own window (`XPRESSNET_CONFIRM_FUNCTION_REPLY 1`).
- A MultiMaus has no speed display or motorised knob, so speed never mirrors onto it.
  The master declares 128 speed steps (a real Z21 declares 28, hence `E4 12` vs our `E4 13`).
- Diagnostics (never print inside the bus code): the 30 s log line reports `XNet bus: N
  frames ok, M dropped (bad checksum), K refreshes sent` and the main-loop rate;
  `XPRESSNET_TRACE_TX 1` prints the bytes actually transmitted for each refresh.

### Build environments & tools

- `env:wemos` - production firmware (no dev updater). Flash over USB with the D1 pulled
  out, or through Setup Mode's `/update` page. Never publish a .bin from `wemos_dev`.
- `env:wemos_dev` - the same plus a network updater (HTTP on port 8266, NO authentication)
  and the TCP debug log; flash with `python tools/devflash.py` while the D1 stays seated.
  Always develop with this environment (flashing plain `wemos` over the network removes
  the updater).
- `env:wemos_sniff` - receive-only XpressNet sniffer (`python tools/devflash.py --env
  wemos_sniff`): XpressNet itself is NOT run; it logs every message of a bus something
  else runs. Wire ONLY the data pair and ground to that bus, never the 12 V pin.
- `env:native` - host unit tests (`python -m platformio test -e native`, 408 tests).
- `tools/netlog.py [--seconds N --out FILE]` reads the TCP debug log. **Run exactly one
  client**: a second client replays the buffered history (the log now skips the replay
  for a client that reconnects within 10 s) and the extra load disturbs XpressNet.

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

XpressNet framing and parsing belong to the vendored XpressNetMaster library, so there is no byte-level mock to write: the bridge's own logic is tested natively through `CommandRouter` and `MockProtocolInterface`, and the wire side is verified on the real bus with a MultiMaus. Never add `Serial.printf` inside the library's bus-dispatch path - it breaks XpressNet timing (confirmed live, err13). Log from the interface callbacks in `xpressnet_interface.cpp` instead, e.g. via `DEBUG_XPRESSNET`-guarded prints.

---

## Common Workflows

### Compiling & Uploading

1. Build: `python -m platformio run -e wemos`
2. Upload over USB: `python -m platformio run -e wemos -t upload` (board `d1_mini`, 921600 baud), or over the air via Setup Mode's `/update` page
3. Real Wi-Fi credentials must exist in `xpressnet_ecos_bridge/wifi_credentials.local.h` (gitignored) or the build stops with an `#error`

### Debugging

1. Read the log: `python tools/netlog.py` (TCP log, port 23) on the network builds - the
   serial port is unavailable while LocoNet owns UART0 - or the Serial Monitor (115200
   baud) when LocoNet is off
2. Watch startup messages and protocol activity; the 30 s lines show heap, loop rate and
   XpressNet frame counters
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
├── version.h                           FIRMWARE_VERSION (single source of truth)
├── state_engine.h/.cpp                 Loco state management (50 locos max)
├── command_router.h/.cpp               Protocol bridging & echo attribution
├── eeprom_config.h/.cpp                Persisted settings (pure logic, native-tested)
├── eeprom_store.h/.cpp                 EEPROM.h load/save (Arduino only)
├── setup_web_form.h/.cpp               Setup/update page HTML + validation (native-tested)
├── setup_mode.h/.cpp                   Setup Mode AP, web server, OTA, button, Wi-Fi fallback
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
│   │   ├── ecos_interface.h/.cpp       WiFi TCP client, address maps, queues
│   │   ├── ecos_message_parser.h/.cpp  <REPLY>/<EVENT> framing + property parsing
│   │   └── ecos_protocol.h/.cpp        Outgoing command builders
│   │
│   ├── loconet/
│   │   ├── loconet_interface.h/.cpp    UART0 RX + SoftwareSerial TX, collision handling, routing, resync, slot expiry
│   │   ├── loconet_protocol.h/.cpp     Framing, checksum, opcode names, plain-text frame descriptions (native-tested)
│   │   └── loconet_slots.h/.cpp        Slot server, turnout reports, expiry (native-tested)
│   │
│   └── z21lan/
│       ├── z21lan_interface.h/.cpp     WiFiUDP socket + client session table
│       └── z21_protocol.h/.cpp         Packet encode/decode (native-tested)
│
├── display/
│   ├── oled_display.h                  I2C OLED driver
│   └── boot_logo.h                     Boot splash bitmap
│
├── oled_display.cpp                    OLED implementation
│
├── utils/
│   ├── timing.h                        Non-blocking TimedTask class
│   ├── now_ms.h                        Time source (mockable in tests)
│   ├── debug.h                         DEBUG_PRINTF macro
│   ├── memory.h                        ESP8266 memory utilities
│   ├── log_ring.h/.cpp                 Byte ring behind the TCP debug log (native-tested)
│   ├── net_log.h/.cpp                  TCP debug log on port 23 (Arduino only)
│   ├── dev_ota.h/.cpp                  Network updater, `wemos_dev` only (NO authentication)
│   └── xnet_sniffer.h/.cpp             Receive-only XpressNet sniffer, `wemos_sniff` only
│
├── test_support/                       Native-build stubs only (excluded from firmware)
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
├── test/                               Native unit tests (Unity), mocks, fixtures - see test/README.md
├── libraries/                          External libs (PlatformIO lib_extra_dirs)
│   └── XpressNetMaster/                 Gahtow's XpressNet master library, PATCHED here (TX queue 12,
│                                        refresh queue, bus counters) - keep our changes when updating it
├── tools/
│   ├── devflash.py                      Build + flash over the network (--env wemos_dev|wemos_sniff)
│   └── netlog.py                        Read the TCP debug log (one client at a time)
└── README.md                           User-facing overview
```

---

## Key Design Decisions & Rationale

### Why No Dynamic Memory?
Embedded systems with limited RAM need predictable memory usage. Static allocation prevents heap fragmentation and out-of-memory crashes. 50 locos = ~1.5KB fixed footprint.

### Why Compile-Time Toggles in config.h?
Features not enabled = zero compiled code overhead. A disabled protocol costs nothing. Supports multiple hardware configurations with same codebase.

### Why Echo Prevention at Router Level?
All protocols route through the router, so one attribution record (not per-protocol logic) decides who must not be sent a confirmation of their own command. The window was 500ms originally and had to grow to 4s (`ECOS_ECHO_ATTRIBUTION_WINDOW_MS`) once real Ecos confirmation latency under load was measured.

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
- **Respect the echo attribution window** (`ECOS_ECHO_ATTRIBUTION_WINDOW_MS`, 4s): router tests are timing-sensitive to it
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
- **Echo Prevention**: single last-command attribution record, 4s window (`wasRecentSource()`)
- **State Engine**: Truth table; partial Ecos updates merge only the fields reported

---

## 🎯 Phase 6: Future Improvements ✅ COMPLETE (steps 1-4 of original 6 done, 5-6 moved to Phase 7)

Ordered and agreed 2026-08-14. CV/programming-track support was audited
during Phase 5 planning and placed **out of scope entirely, not deferred** -
the user's stated intent (confirmed 2026-08-31) is that this bridge is
envisaged as an *operational* interface, not a programming one; programming
on the main or on a programming track is not planned to be supported at all,
regardless of what the user's Ecos can already do on its own program track.

1. ✅ **EEPROM storage - implemented and confirmed live (2026-08-27)**.
   Final scope, six real fields: WiFi SSID, WiFi password, Ecos IP,
   `XPRESSNET_BUS_TIMEOUT`, `LOCO_INACTIVITY_TIMEOUT` (added during
   implementation - same "judgment call that varies by usage pattern"
   category as the bus timeout), and the bridge's own static IP
   (`bridge_ip`/`bridge_gateway`/`bridge_subnet`). **Revised during step 2
   design (2026-08-27) from optional/DHCP-by-default to mandatory, no
   DHCP** - Z21 LAN (step 4, a future WLANmaus client) will need to
   depend on the bridge's address reliably, and a fixed IP is simpler
   than chasing a DHCP lease; this bumped `EEPROM_CONFIG_VERSION` to 2.
   - **New module**: `eeprom_config.h/.cpp` (pure struct/defaults/checksum/
     validation logic, no `EEPROM.h` dependency - fully native-testable,
     11 new tests) + `eeprom_store.h/.cpp` (the actual `EEPROM.h` read/
     write calls, Arduino-only, excluded from `env:native` like
     `ecos_interface.cpp`). A magic number + version + checksum
     distinguishes real saved data from blank/erased flash or an
     incompatible schema version; invalid data seeds from `config.h`
     defaults and saves once, so later boots read a valid struct without
     re-seeding every time. `config.h`'s existing constants are now
     documented as EEPROM *defaults* (seed values), not the live values -
     `XpressNetInterface::setBusTimeoutMs()`, `StateEngine::
     setInactivityTimeoutMs()`, and `EcosInterface::setConfig()` carry the
     real runtime values in, called from the `.ino`'s `setup()` right
     after `eepromStoreLoad()`, before any interface `begin()`.
   - **Side cleanup in the same pass**: removed the dead `XPRESSNET_TIMEOUT`
     constant (config.h:47) - confirmed unused anywhere in the codebase;
     `LOCO_INACTIVITY_TIMEOUT` is the real constant `state_engine.cpp`
     reads. Also removed the now-redundant `ENABLE_EEPROM_CONFIG` feature
     flag (EEPROM support is unconditional, small core infrastructure, not
     an optional protocol behind a toggle).
   - **Real bug found via live hardware testing**: `eepromStoreSave()`
     never checked `EEPROM.commit()`'s return value. Found while stress-
     testing the corruption/reseed path with two `EEPROM.put()`+`commit()`
     calls back-to-back in the same boot (a test-only pattern, not a real
     production code path - `eepromStoreLoad()` only ever calls save once
     per boot) - the second commit was silently lost. Fixed by logging a
     clear error if `commit()` ever returns false, so a real failure would
     at least be visible instead of silently dropped.
   - **Confirmed live via direct flash inspection** (`esptool.py
     read_flash` at the EEPROM's linked address, `0x3FB000` on this
     board/partition layout - found via `nm` on the linked ELF's
     `_EEPROM_start` symbol), bypassing serial-log timing ambiguity
     entirely: blank/erased flash (0xFF) correctly triggers a reseed with
     real defaults (confirmed via raw bytes: magic, SSID, Ecos IP all
     correct after boot, no serial connection involved); a plain reflash
     with unchanged code leaves the EEPROM sector byte-for-byte untouched;
     and - the key persistence proof - changing `config.h`'s compile-time
     `ECOS_IP` default and reflashing (no erase) still loads and connects
     to the *old*, previously-persisted value, not the new default,
     confirming EEPROM genuinely wins over compile-time defaults rather
     than incidentally matching them.
   - **Test-harness footnote, not a firmware issue**: earlier attempts to
     verify this via live serial-log capture (a Python/pyserial script)
     gave inconsistent results - almost certainly the script's own port-open
     triggering an extra board reset via the same DTR/RTS auto-reset
     circuit `platformio.ini` already documents (`monitor_rts=0`/
     `monitor_dtr=0` exists for exactly this reason). Resolved by switching
     to direct flash-content verification instead of trusting serial-log
     sequencing.
   - 11 new tests (`test_eeprom_config`); native suite 149/149 passing;
     `env:wemos` builds clean (RAM 45.4%, Flash 32.1%).
2. ✅ **Web-based configuration UI / Setup Mode - implemented and
   confirmed live (2026-08-27)**. All 6 EEPROM fields are editable only
   in a dedicated "Setup Mode" (AP + web form), never live during normal
   bridging - reboot required to apply, matching the user's explicit
   preference over an always-on config server.
   - **Entry, 4 ways, all confirmed live**: (1) blank/invalid EEPROM -
     first boot, no known WiFi/bridge IP to even attempt; (2) EEPROM
     technically valid (correct checksum) but *incomplete* - a real gap
     found live: a freshly-reseeded config has blank bridge IP fields
     with a valid checksum, which would otherwise silently start normal
     operation with DHCP fallback instead of forcing setup - fixed with
     `eepromConfigIsComplete()`, a second check beyond raw checksum
     validity; (3) holding a dedicated pushbutton on D7/GPIO13 for 3s
     while running normally; (4) 5 minutes of continuous WiFi
     disconnection (e.g. router password changed) - defense-in-depth so
     a bad WiFi credential never requires a USB reflash to recover.
   - **Button pin correction, found live**: originally planned to reuse
     the Wemos D1 Mini's onboard button, assumed wired to GPIO0 (a
     "FLASH" button, common on other ESP8266 boards) - confirmed on the
     user's actual hardware that this board's only onboard button is
     wired directly to RST/EN instead, silkscreened "RESET", a genuine
     hardware reset that bypasses all running code and can't be
     "held" in software at all (confirmed via live serial capture:
     zero button-hold debug output before an instant reboot). Moved to a
     new dedicated pushbutton wired to D7/GPIO13 - chosen specifically to
     avoid GPIO0/GPIO2/GPIO15's boot-strapping significance, so (unlike
     the original GPIO0 plan) there's no "don't hold this during
     power-up" caveat to document or get wrong. Requires real new wiring,
     which the user did on the workbench to confirm the fix.
   - **Module split**: `setup_web_form.h/.cpp` (pure HTML generation +
     field validation/parsing, no Arduino dependency - native-testable,
     20 new tests) + `setup_mode.h/.cpp` (real `ESP8266WebServer`/
     `WiFi.softAP()`/RTC-memory/GPIO calls, Arduino-only, excluded from
     `env:native` like `eeprom_store.cpp`). Timeout fields are shown/
     submitted in whole seconds, converted to/from the stored
     milliseconds internally. Bridge IP/gateway/subnet are `required`
     HTML fields (client- and server-side) - the subnet specifically
     shows a `255.255.255.0` suggestion when blank (added after live
     testing: the user's first real submission used `255.255.0.0`
     instead of the intended `/24`, and asked for a nudge toward the
     standard value) but never overrides an already-saved value, even a
     non-standard one - EEPROM always wins over any suggestion once set.
   - **Reboot-to-apply signal**: `requestSetupModeOnNextBoot()`/
     `consumeSetupModeRequest()` use ESP8266 RTC user memory (a
     magic+flag struct at offset 0, which the core maps to physical
     block 64 - confirmed via the actual core source, safely past the
     SDK/eboot-reserved region) rather than EEPROM, since this is a
     one-shot "next boot only" signal that doesn't need to survive a
     power cycle and shouldn't cost a real flash write cycle.
   - **Confirmed live for all 4 entry paths** via direct serial capture
     (this feature's own debug output isn't timing-sensitive the way
     step 1's persistence proof was, so serial logs were reliable here)
     and cross-checked against direct flash reads for the actual saved
     values: blank-EEPROM entry, incomplete-config entry, button-hold
     entry (after the D7 rewiring), and WiFi-fallback entry (tested with
     a temporary shortened timeout + fake SSID override, both fully
     reverted afterward) all fire correctly, and a real save/reboot
     cycle correctly persists and applies the new static IP (`WiFi
     connected! IP: 192.168.0.51` - the configured static address, not a
     DHCP-assigned one).
   - 20 new tests (`test_setup_web_form`) + 4 more in `test_eeprom_config`
     for `eepromConfigIsComplete()`; native suite 191/191 passing;
     `env:wemos` builds clean (RAM 53.9%, Flash 34.7%).
3. ✅ **OTA (over-the-air) firmware updates - implemented and confirmed
   live (2026-08-27)**. A web page within Setup Mode (not a separate
   mode/trigger - reuses the same button-hold/AP entry already built for
   step 2) at `/update`, showing the current version and a single
   firmware-upload form. No auth (matches this project's posture
   elsewhere) - the user judged malicious-firmware risk not worth
   defending against for a single-user device, while an accidentally
   failed/interrupted update is an accepted risk with USB reflash as an
   acceptable rare fallback (framed as "a standard service call," not
   something requiring elaborate safety nets).
   - **Uses the official `ESP8266HTTPUpdateServer` library** for the
     actual upload/flash handling at a background path (`/doupdate`),
     rather than hand-rolling it - safety-relevant code where reusing
     well-tested standard library logic beats a custom implementation.
     This project's own `/update` GET page (styled to match the rest of
     Setup Mode, and without the library's own default page's unneeded
     "FileSystem" upload option - no SPIFFS/LittleFS use here) just
     POSTs to that path.
   - **Verified the actual safety mechanism against this board's real
     linker script** (`eagle.flash.4m1m.ld`) rather than assuming: it
     reserves a ~2MB "empty" region between the running sketch (~1019KB)
     and the filesystem area - the real OTA staging area the `Update`
     library writes the new image into. A failed/interrupted upload
     only affects that staging region; the bootloader (`eboot`) only
     swaps the new image in after a complete, verified copy exists, so
     the currently-running firmware is never touched by a failed
     upload - confirmed not a bricking risk, matching what the user was
     designing around.
   - **New version scheme**: `version.h`, a single dedicated file
     (`FIRMWARE_VERSION`, starting at `1.0.0`) - added mid-implementation
     after a live test surfaced a real problem with the original
     date/time-only approach: the OLED's compact "FW:" line only showed
     the *date*, and two test builds compiled the same day were
     indistinguishable there. The user asked for a conventional
     version/fix-number scheme instead (more recognizable to end users,
     easier to track across releases - they plan to publish built `.bin`
     files directly to users, without requiring a PlatformIO
     environment). `FIRMWARE_BUILD_INFO` (date+time) still exists
     alongside it as a secondary, more precise detail shown on the
     update page for disambiguating same-version development builds.
   - **Confirmed live end-to-end**: flashed a `1.0.0` baseline over USB,
     built a second `.bin` with a temporary `1.0.1-test` version bump,
     uploaded it through the real `/update` page from the user's own
     phone/PC (this dev machine has no WiFi adapter to test it directly
     with), and confirmed the version visibly changed to `1.0.1-test` on
     both the OLED and the setup page after the automatic reboot -
     genuine proof the uploaded image was what actually started running,
     not just that the upload succeeded. Reverted to `1.0.0` afterward
     (the real release value) and reflashed over USB.
   - 5 new tests (`test_setup_web_form`, covering `buildUpdatePageHtml()`
     and the config page's new nav link); native suite 198/198 passing;
     `env:wemos` builds clean (RAM 55.1%, Flash 35.9% - `ESP8266HTTPUpdateServer`'s
     modest overhead, on top of step 2's `ESP8266WebServer`).
4. ✅ **Z21 LAN protocol support - COMPLETE, confirmed live 2026-08-28.**
   A marathon session (WLANmaus + tethered MultiMaus, simultaneously
   connected throughout) found and fixed roughly seventeen real bugs
   across two halves of the same day - full dated diagnosis for every one
   in `docs/CHANGELOG.md`. By the end: drive/direction/function control
   confirmed correct in all six pairings (XpressNet↔Ecos, Z21↔Ecos,
   XpressNet↔Z21 - each verified with the OTHER two protocols fully
   powered off, to rule out any cross-contamination in the test itself);
   no XpressNet bus errors (err13) under real simultaneous multi-protocol
   load; noticeably more responsive than earlier in the session once the
   real bottlenecks were found.
   - **The headline architectural change, prompted directly by the user
     partway through testing**: earlier in the session, a throttle-facing
     protocol's command only reached the OTHER throttle-facing protocol
     indirectly, by waiting for Ecos's own confirmation to complete the
     loop - which turned out to be fragile in multiple ways (see the bug
     list below). The user pushed back on this design directly ("why do
     we rely on ECOS response to broadcast to other protocols?... we will
     broadcast any change from any protocol, to all OTHER protocols").
     Redesigned `CommandRouter::broadcastCommand()` so a throttle-facing
     protocol's command now fans out immediately and directly to Ecos
     *and* every other throttle-facing protocol in the same call, with no
     dependency on Ecos's confirmation timing for that leg at all. Ecos's
     own echo still separately reaches every throttle-facing protocol for
     genuine Ecos-originated changes (someone touching Ecos directly) -
     that path is the only one still echo-timing-dependent, and only
     needs to be, since there's no other way to learn about a change Ecos
     didn't get from the bridge.
   - **Real bugs found and fixed, roughly in the order they surfaced**
     (full diagnosis of each in CHANGELOG): wrong byte offsets decoding
     `LAN_X_GET_LOCO_INFO` (garbage-address subscriptions); Z21 function
     confirmation depending on an Ecos round trip that echo-suppression
     drops for the sender - fixed with an immediate self-confirm matching
     real Z21 hardware; real WLANmaus never sending `LAN_SET_BROADCASTFLAGS`
     despite expecting broadcasts - default new clients to broadcasts-on;
     Z21's direction bit needing inversion from the spec-literal reading
     to match XpressNet's already-proven convention against this Ecos
     setup; plain drive commands unconditionally re-sending the whole
     function bitmap (capable of wiping real Ecos function state with a
     stale zero after reboot); no baseline `get()` query ever existed for
     a freshly-subscribed loco (`request(view)` only reports *future*
     changes; the `get()` builder had been deleted as "dead code" in an
     earlier audit - it was a missing feature, not dead); that 34-query
     baseline burst, and separately `sendFunctionCommand()`'s 32-command
     burst, both blocking long enough in one synchronous call to cause
     real XpressNet err13 - paced, then the function send redesigned to
     diff against what was last actually sent so only the changed bit(s)
     go out immediately, no pacing needed for the common case at all
     (a burst of rapid real button presses had been causing most paced
     sweeps to get abandoned mid-way before this); the router's echo
     gate being all-or-nothing (dropped an Ecos-echoed update entirely
     for ANY recently-active protocol, not just the true originator) -
     replaced with `wasRecentSource()`, a per-destination check; that
     same gate also existed, redundantly and more narrowly, on the
     *incoming* side of the XpressNet/Z21 handlers, intermittently
     dropping genuine new commands - removed outright (XpressNet's
     master-polled bus and Z21's client-server UDP both have no real
     physical echo path this was ever protecting against); the 500ms
     echo-attribution window being far shorter than Ecos's real
     confirmation latency under load, so a protocol's own late
     confirmation got misattributed as an external change and pushed
     back as "stolen" - widened to `ECOS_ECHO_ATTRIBUTION_WINDOW_MS`
     (4s); and finally the true root cause of persistent spontaneous
     "stolen" flashes even after all of the above: `handleEcosCommand()`/
     `handleEcosFunctionCommand()` unconditionally stamped the shared
     echo-attribution state to `ECOS` on every call, silently clobbering
     a still-relevant XpressNet/Z21 attribution whenever two of that
     protocol's commands were in flight close together - confirmed with
     a fully isolated test (Z21 completely powered off) and fixed by
     simply no longer touching that state from Ecos-sourced handling
     (nothing ever actually needed to read "ECOS" out of it).
   - **F8-F11 appearing inverted between Ecos and the bridge, on multiple
     locomotives** - investigated at length (byte-exact wire verification
     of both the Z21 encoder and the Ecos property parser, both confirmed
     correct and generic with no special-casing for any function index)
     before the user found the actual cause directly in Ecos: those four
     functions are genuinely inverted at the Ecos/decoder level for the
     locos in question, unrelated to the bridge. Not a bridge bug.
   - 4 new native tests (`get()` command builders); native suite
     254/254 passing throughout; every fix confirmed against real
     hardware via targeted serial captures and raw wire-byte inspection,
     not guessed. Tagged `v1.1.0`.
   - **Follow-up housekeeping, tagged `v1.1.1`**: Z21 had never gotten an
     OLED page (unlike XpressNet/Ecos, which each have their own). Added
     a fifth page (`PAGE_Z21`) showing client count, time since the last
     genuine action, and that action's source IP - deliberately NOT a
     full connected-client list (could be any number on a layout with
     more throttles than this project's own test setup) and NOT padded
     with the same `active_locos`/`total_commands` figures already shown
     on the XpressNet page, since neither is actually Z21-specific.
     `Z21LanInterface::getStatus()` also changed to mean "at least one
     client active" instead of "socket bound" - the previous meaning was
     true almost the entire time the bridge is up, regardless of whether
     any WLANmaus was actually connected, so it carried no real signal.
     A real bug surfaced immediately in live testing: "Last Msg" stayed
     stuck near 0 even after 20+ seconds of the throttle sitting
     completely idle. Traced with added debug logging to real WLANmaus
     hardware polling `LAN_X_GET_STATUS` roughly twice a second even at
     rest - a genuine, spec-documented request type our dispatch code
     already handled correctly, just without a debug print, so it was
     invisible in earlier wire captures while still counting as "a
     message" for the age calculation. Not a bug in the arithmetic, but
     not a useful signal either - the user asked for "last genuine
     action" instead of "last packet of any kind" once this was
     understood. Moved the tracked timestamp from `handlePacket()`
     (fires for every packet) to individual calls at the drive/function/
     emergency-stop/track-power branches only. 4 new native tests
     covering the new `SystemStatus` fields; native suite 258/258.

   The paragraph below is the original pre-live-test status from the
   2026-08-27 checkpoint, kept for history:
   - **Scope agreed with the user before implementation**: loco speed/
     direction/function control, track power on/off, and emergency stop -
     all mapped onto this project's existing `CommandRouter` mechanisms.
     Deliberately deferred: Z21 turnout/accessory commands (XpressNet->
     Ecos accessory support already exists; a second input path for the
     same feature is separate work) and CV programming (out of scope
     project-wide).
   - **Verified against the actual official spec**, not general
     recollection: fetched and saved the real Z21 LAN Protocol
     Specification v1.13 (docs/z21-lan-protokoll-en.pdf, gitignored -
     same redistribution restriction as the Ecos PDF). Confirmed UDP
     port 21105, and confirmed the base Z21 protocol has **no**
     documented broadcast auto-discovery (only the separate "Z21 pro
     LINK" accessory does) - directly validating the earlier decision to
     make the bridge's static IP (step 1/2) mandatory rather than
     optional, not just a good guess.
   - **Architecture impact analysis, done before writing code**: verified
     against the real `command_router.cpp` (not assumed) that Ecos
     itself needs zero changes - `EcosInterface` has no idea which
     throttle-facing protocol originated a call, by design. CommandRouter
     needed only the same small, mechanical extension already used for
     XpressNet: a `z21` member/setter, a `handleZ21Command()`/
     `handleZ21FunctionCommand()` pair mirroring the XpressNet handlers,
     and one new branch in `broadcastCommand()`'s existing
     `LocoSource::ECOS` case (fanning Ecos-sourced updates out to Z21
     too) plus a new `LocoSource::Z21_LAN` case (forwarding to Ecos only -
     Ecos's own echo-back is what completes the loop to other throttle
     protocols, same as it already does for XpressNet).
   - **Module split**: `z21_protocol.h/.cpp` (pure packet encode/decode -
     checksum, address/speed/function encode-decode, packet builders - no
     Arduino dependency, native-testable, 42 new tests including several
     that assert byte-for-byte against the spec's own worked examples) +
     `z21lan_interface.h/.cpp` (real `WiFiUDP` socket + a fixed-size
     client session table, Arduino-only, excluded from `env:native`).
     Unlike XpressNet (a shared bus where multiple physical throttles
     see each other's traffic for free), Z21 is UDP client-server - this
     interface tracks each connected client itself and broadcasts state
     to all of them, entirely internal to `Z21LanInterface` - CommandRouter
     still only ever sees "one Z21 interface".
   - **Two real bugs caught before ever reaching hardware**, via careful
     review while writing the dispatch logic (not live-testing this
     time - the spec's own quirk of reusing X-Header values across
     multiple commands, disambiguated only by a DB0 sub-byte, is an easy
     trap): `LAN_X_SET_LOCO_DRIVE` and `LAN_X_SET_LOCO_FUNCTION` share
     X-Header `0xE4` - the first implementation would have silently
     misinterpreted every function-toggle packet as a garbled drive
     command; fixed by requiring DB0's upper nibble to be `0x1` before
     treating it as a drive command. Renamed a pair of confusingly-aliased
     constants (`Z21_X_GET_VERSION`/`Z21_X_SET_TRACK_POWER`, both
     accidentally defined as the same numeric value `0x21`) to a single
     clearly-named `Z21_X_HEADER_SYSTEM` shared header, since X-Header
     `0x21` is genuinely shared by four different requests
     (GET_VERSION/GET_STATUS/TRACK_POWER_OFF/TRACK_POWER_ON) disambiguated
     by DB0, not something worth four separate same-valued aliases.
   - 42 new tests (`test_z21_protocol`) + 9 new `CommandRouter` integration
     tests (routing, echo prevention, emergency stop/resume, all mirroring
     existing XpressNet test coverage); native suite 249/249 passing;
     `env:wemos` builds clean (RAM 56.2%, Flash 36.6%). Confirmed on real
     hardware only that the interface itself boots correctly and binds
     UDP port 21105 (serial log: "Z21 LAN listening on UDP port 21105") -
     **no real WLANmaus session has happened yet**. Session paused here
     at the user's request before that test could run; picking back up
     needs a live WLANmaus connected to the bridge's real IP
     (`192.168.0.51`) to actually exercise speed/function/power control
     end-to-end before this step can be marked done, version-bumped, or
     tagged.
**Phase 6 closed out 2026-08-31** at the user's request, with steps 1-4
done and confirmed live (tagged `v1.1.1`). The original steps 5 (LocoNet)
and 6 (open/TBD) move to Phase 7 below rather than holding this phase
open - LocoNet specifically is blocked on hardware the user doesn't have
yet, not on anything code-side, so there was no reason to keep Phase 6
"in progress" waiting on it.

---

## 🎯 Phase 7: TBD 🔧 IN PROGRESS

Ordered and agreed 2026-08-31, picking up the two items left over from
Phase 6 (LocoNet, open/TBD) plus real gaps identified from a status
review of everything deliberately deferred or parked across the whole
project's history. Named "TBD" rather than a themed name like Phase 6's
"Future Improvements" - deliberately open-ended, add to this list as
real needs come up rather than pre-committing to a fixed scope.

1. ✅ **Z21 turnout/accessory command support** (2026-08-31, confirmed live
   2026-09-01) - mirrors the existing XpressNet→Ecos v1 accessory path
   (Phase 5 step 10): a WLANmaus throws a turnout, Ecos receives it. No new
   Ecos-side machinery needed - Ecos's `set(11, switch[...])` already
   addresses by protocol+address+port directly, no per-accessory object ID
   lookup or subscription required for this direction.
   - **The initial implementation (2026-08-31) had `LAN_X_SET_TURNOUT`'s DB
     fields in the wrong order** - it assumed the flags byte (`10Q0A00P`)
     came first, copying `LAN_X_SET_LOCO_DRIVE`'s field order, when the
     real spec (and address-first commands generally) put the address
     bytes first and the flags byte third. This meant the decoded "address"
     was actually built from the flags byte - a function of which button
     was pressed, not which turnout was selected - live-observed as
     pressing either button on either selected turnout always affecting
     only one of two fixed addresses, and release re-triggering a second,
     differently-addressed command. A follow-up debugging attempt (in a
     separate session) treated this as "WLANmaus sends weird toggle
     pulses" and patched around it with a release-pulse debounce heuristic
     instead of finding the byte-order bug - the underlying decode function
     was rewritten to match the (mis-assigned) bytes it was actually
     receiving rather than the call site being corrected.
   - **Root-caused 2026-09-01** by cross-checking the actual byte order
     against two independent sources: the official Z21 LAN spec text
     (section 5.2) and a verbatim download of Philipp Gahtow's own
     reference Z21 library (`kind3r/esp8266-z21-lib`, a port of Gahtow's
     original - the same author as this project's own vendored
     XpressNetMaster library) - `notifyz21Accessory((packet[5]<<8)+
     packet[6], bitRead(packet[7],0), bitRead(packet[7],3))`: address
     bytes first, flags byte third. Fixed the call site and removed the
     debounce hack (unnecessary once the deactivate/release edge is
     correctly filtered by the decode function itself, exactly like
     XpressNet's own turnout handling already does).
   - **A second, independent bug surfaced once addressing was fixed**: the
     "is this a valid turnout flags byte" nibble check didn't correctly
     ignore the Q (queue) bit, so any packet with `Q=1` (a real mode from
     Z21 firmware ≥1.24) was silently rejected outright regardless of
     address or button.
   - **A third bug, found via a real raw wire-byte capture** (this
     project's own proven method for exactly this kind of ambiguity) after
     the Q-bit fix still didn't restore function: this specific real
     WLANmaus does not set the documented `bit7` "1" prefix on the flags
     byte at all - real captured values were `0x00/0x01/0x08/0x09`, not the
     spec's `0x80/0x81/0x88/0x89`. Confirmed coherent against the actual
     button-press sequence (activate/deactivate pairs for straight/diverging
     alternating correctly when reading bits `[3]`/`[0]` directly). Removed
     the nibble/format gate entirely - `X-Header 0x53` alone already
     disambiguates this as a turnout command, no format check needed -
     trusting the real capture over the literal spec text, the same call
     already made for this project's Z21 direction-bit and XpressNet r/g
     port-letter precedents.
   - **Confirmed live 2026-09-01**: T0001 and T0002, straight and diverging,
     both correctly reaching Ecos, no cross-addressing, no reverting on
     release. Native suite 272/272 passing. `env:wemos` flashed at
     `1.2.0`. See `docs/CHANGELOG.md` for full diagnosis detail.
2. ✅ **Accessory/Turnout v2** (2026-09-01) - the real Ecos→throttle
   direction: an accessory thrown directly on Ecos now reaches both
   XpressNet and Z21. Backend mirrors the loco address-map/subscribe
   pattern, confirmed real against actual Ecos hardware first (Phase 7
   step 2 investigation, before writing any code): `queryObjects(11, addr)`
   for discovery and `request(id, view)` per accessory both work, despite
   the official spec marking the whole individual-accessory-object section
   "(in Planung)"/not-yet-implemented - real Ecos firmware had shipped
   ahead of that spec revision. State-change events (`state[val]`,
   `switching[val]` ignored as a transient in-motion flag) route through a
   new `CommandRouter::handleEcosAccessoryCommand()`, forwarding to both
   throttle protocols unconditionally (no originating protocol to skip,
   unlike the v1 direction). Deliberately does NOT touch `last_accessory`
   (the OLED "last accessory" line) - explicit design call: that field
   shows what a handheld last *commanded*, not Ecos's truth, since a
   dropped bridge command is worth surfacing but a redundant re-confirm
   isn't. Dedicated OLED accessory page skipped, per the same v1 scoping
   logic (Phase 5 step 10) - not revisited.
   - **Real bugs found via live testing, in order**: (1) `state[val]`'s
     polarity was backwards from the initial guess - confirmed by
     correlating the bridge's own outgoing commands against the resulting
     events across 8 consecutive cycles, zero exceptions - fixed
     (`state=0` is diverging, not 1). (2) A plain, unsolicited
     `SetTrntPos()` broadcast to XpressNet did nothing on real hardware -
     confirmed via a live capture that our own code was executing
     correctly, so the gap was client trust, not encoding.
   - **The actual root cause, found via a real Roco Z21 command station +
     two MultiMaus reference test** (the user's own idea, run after
     several dead-end fix attempts): a genuine command station propagates
     turnout changes between throttles with a 1-2s delay, and correctly
     restores state when reselecting a turnout - both signs of a
     **poll/reply** mechanism, not a push/broadcast one. Confirmed live:
     a real MultiMaus polls a completely separate, previously-unimplemented
     XpressNet message (`notifyXNetTrntInfo`, the "Accessory Decoder
     Information Request", header 0x42/0x43 - distinct from the `0x52` SET
     command) at ~2Hz while sitting on a turnout screen, and getting no
     reply at all is exactly why it fell back to "unknown" after a few
     seconds. Implemented `XpressNetInterface::onTurnoutInfoRequest()` →
     `XpressNetMasterClass::SetTrntStatus()`, with the request/reply byte
     layout (turnout *group* addressing - 4 turnouts per group, 2 per
     nibble, `TT`/`N`/`Z3Z2Z1Z0` fields) confirmed against the official
     Lenz XpressNet Specification (v2, 6/2003) cross-referenced with the
     already-vendored library's own comments. **Confirmed live: MultiMaus
     no longer goes "unknown", and now correctly reflects updates coming
     from Z21 too** (via the same new state cache below, served on its
     next poll).
   - Added `CommandRouter`'s new best-known-accessory-state cache
     (`known_accessories[]`, `getKnownAccessoryState()`), updated by both
     the v1 (handheld-commanded) and v2 (Ecos-confirmed) directions -
     needed to answer on-demand queries (this XpressNet request, and Z21's
     `GET_TURNOUT_INFO`, now wired to real state too instead of a
     hardcoded "always straight" guess).
   - Added `XpressNetMasterClass::PushExternalTurnoutUpdate()` to the
     vendored library (mirrors `PushExternalLocoUpdate()`'s call-byte-then-
     reply bus injection, Phase 5 step 9) for the instant-push leg - kept
     in place as a reasonable belt-and-suspenders addition even though the
     poll/reply fix above turned out to be the actual missing piece.
   - **Deferred, not fully understood**: a real WLANmaus never sends
     `GET_TURNOUT_INFO` at all while idle on the turnout screen (confirmed
     live: zero requests in 60s, contradicting an earlier session's
     assumption that it polls at 2+Hz) - it relies purely on receiving the
     `LAN_X_TURNOUT_INFO` broadcast, which is confirmed byte-perfect and
     delivered to the connected client every time. Its display almost
     never refreshes from this - except once, a few seconds after an
     Ecos-direct change, not reproduced since. That one success rules out
     "WLANmaus categorically ignores this broadcast" as the explanation,
     but a generic UDP-packet-loss theory doesn't fully fit either, since
     loco broadcasts use the identical fire-and-forget send and have been
     reliable throughout this whole project - something turnout-specific
     is still going on, cause unknown. XpressNet works reliably both
     directions, which the user is satisfied with for now - this is
     deferred as its own low-priority follow-up (Phase 7, new item 6
     below), not blocking anything else.
3. ✅ **LocoNet support - released (`v1.5.0`, 2026-10-26) and refined through `v1.5.4`**,
   confirmed live with a Digitrax UT6 alongside XpressNet, Z21 and Ecos. Current state:
   - **Modules** (`protocols/loconet/`): `loconet_protocol.*` (framing, checksum, opcode
     names, plain-text frame descriptions, resync decision - pure, native-tested),
     `loconet_slots.*` (slot server: 20 slots, select/steal/dispatch, replies, turnout
     reports, expiry - pure, native-tested) and `loconet_interface.*` (UART0 receive at
     16660 baud, SoftwareSerial transmit through the BC547, collision detection,
     routing). Slot ownership stays INTERNAL to the interface and never gates
     `CommandRouter` (stealing is free): design decision of 2026-09-23, the user's
     strongly held preference over Digitrax's exclusive-ownership model - on a home
     layout seamless cross-protocol control matters more. XpressNet's own
     `SlotLokUse[]`/`AddBusySlot()` is the same kind of internal bookkeeping.
   - **What works**: speed, direction, F0-F28, long addresses, track power/E-stop (a stop
     is `OPC_GPOFF`, what the UT6 power button sends; the bridge learns Ecos's run
     state on connect with `get(1,status)`), no "steal this loco?" prompt (`LOCO_ADR`
     replies never report IN_USE), and a bridge restart under a live UT6 is handled (a
     select of a vanished slot answers with the empty slot read).
   - **UT6 display refresh**: a UT6 ignores unsolicited slot changes and gives no "stolen"
     notice. What works is a forced bus disconnect: the TX line is held low ~250 ms (150
     ms does nothing, >=200 ms works); the UT6 re-reads its slot, adopts the real
     values, keeps the loco and flashes once. Automatic and rate-limited
     (`LOCONET_RESYNC_INTERVAL_MS` 5 s, `LOCONET_RESYNC_QUIET_MS` 2 s,
     `LOCONET_RESYNC_HOLD_MS` 250 ms; only when a held slot changed elsewhere). The UT6
     shows speed as 0-99 (percent of 126 steps), not the Ecos step number.
   - **Slot expiry**: an IN_USE slot a throttle has not spoken about for
     `LOCONET_SLOT_IDLE_EXPIRY_MS` (10 min, 0 disables) drops to COMMON and returns to
     IN_USE when the throttle speaks again; changes pushed from other protocols do not
     keep it alive (`last_heard_ms` vs `last_use_ms`).
   - **Turnouts**: a UT6 `OPC_SW_REQ` (the ON edge is the command; wire address = number-1;
     DIR set = closed/straight) reaches Ecos and every other interface. A change made
     anywhere else goes out as `OPC_SW_REP` (`B1`, SN2 `0x20` closed line / `0x10` thrown
     line) and `OPC_SW_STATE` (`BC`) queries are answered from the router's known-turnout
     cache, which the bridge seeds from Ecos (`get(id,state)` when it subscribes to an
     accessory). **Parked/known**: a UT6 only redraws a turnout's position after its own
     F key, whatever the bus carries (sending the `B0` pair as well, switch
     `LOCONET_TURNOUT_SEND_SW_REQ`, is off). It resends its command regardless and Ecos
     reapplies it; nothing breaks.
   - **OLED LocoNet page**: connected icon = at least one slot IN_USE; `Frames` (plus `Err`
     only when non-zero), `Last Msg`, `Slots: n/20`, and the last two frames in plain text.
   - **Tooling**: debug text is a TCP log on port 23 (UART0 belongs to LocoNet) read with
     `tools/netlog.py`; spec `docs/loconet-personal-use-edition-1.0.pdf` (gitignored);
     wire-verified notes in `E:\Claude\shared\loconet.md`. Never log from a global
     constructor with non-constant state (caused a boot loop once).
   - **Open**: F13+ from the UT6 not exercised; no dedicated OLED turnout page.

4. ✅ **Expand "stolen" icon refresh from F0-F4 to F0-F12** (2026-09-02) -
   `PushExternalLocoUpdate()` (Phase 5 step 9) now also injects F5-F8/F9-F12
   call-byte-then-reply messages alongside the original F0-F4 one, using
   the same `buildFunctionGroupByte()` groups already used elsewhere.
   **Confirmed live**: "stolen" now correctly triggers for functions
   tested up to F9, same 100%-reliable call-byte injection as before.
   - **A separate, deeper issue was found investigating this, NOT caused
     by this change**: while confirming the pushed function *value*
     (not just the "stolen" flash) actually displays correctly, live
     testing found function state doesn't reliably sync between XpressNet
     and Z21 in either direction - confirmed via live capture that the
     bridge's own routing is 100% correct every time (right bitmap
     computed, `sendFunctionCommand()`/`pushLocoStateToOwningSlot()` both
     called with the right value, every single toggle) - the gap is
     downstream, in how MultiMaus/WLANmaus actually apply what's sent to
     their own display, not yet root-caused. Delivery to Ecos/DCC itself
     is unaffected and 100% reliable both ways - only the throttle-side
     *display* is sometimes stale. The user's call: log as a known,
     deferred, non-critical issue (an annoyance, not a functional flaw)
     rather than continue chasing it - see Phase 7's new item 7.
   - **A red herring investigated and resolved along the way**: an
     apparent "activity LED stopped pulsing at ~1Hz" regression turned
     out to be expected behavior, not a bug - the steady pulse was the
     new `onTurnoutInfoRequest()` handler's real ~2Hz Accessory Info
     Request traffic (only sent while MultiMaus is on the turnout
     screen), not a separate always-on heartbeat. Confirmed live:
     switching back to the turnout screen brings the pulse back.
   - **A second red herring along the way**: an F1 toggle that appeared
     to do nothing turned out to be MultiMaus's own WLANmaus button
     actually sending "toggle F9" on the wire (`DB3=0x89`, confirmed via
     raw capture) - not a decode bug, likely a WLANmaus-side function
     remap/label mismatch or a mis-press, not investigated further.
5. ⬜ **Open/TBD** - candidates as of 2026-11-23: free-heap measurement on the plain
   `wemos` build, a dedicated OLED turnout page, F13+ from the UT6, a long soak test
   with all four interfaces (see "Open / candidates" at the end of this file). The
   UT6 turnout display is parked on purpose.

6. ⬜ **Investigate WLANmaus turnout broadcast reliability** - added
   2026-09-01, deferred/low-priority. A real WLANmaus's display almost
   never refreshes from a confirmed byte-perfect, confirmed-delivered
   `LAN_X_TURNOUT_INFO` broadcast - except once, not reproduced since (see
   Phase 7 step 2's write-up above for the full story and what's already
   been ruled out). XpressNet already works reliably both directions,
   which the user is satisfied with for now - this isn't blocking
   anything, fold in opportunistically if it's ever worth another look
   (e.g. if a wired packet capture on the WiFi side becomes practical, to
   rule packet loss in or out directly instead of inferring from symptoms).
7. ✅ **Loco function display sync - solved 2026-10-26** (was a deferred annoyance).
   Root cause: the vendored XpressNetMaster library's transmit queue held only 5
   messages with no overflow check, so any burst of more than five (the old "stolen"
   push, every refresh variant) was overwritten on the wire. With a 12-entry queue, the
   call byte and message sent as ONE entry, and a refresh that copies what a real Z21
   puts on the bus (the changed group's `E4 2x` message, then a directed `E3 40`
   notice), a MultiMaus display now follows function changes from Ecos and the other
   throttles in any order. Details: "XpressNet reliability" under Protocol Details and
   docs/CHANGELOG.md 2026-10-26.

---

**Last Updated**: 2026-11-23 (documentation refresh - the firmware itself is `v1.5.4`,
last code change 2026-10-26). Full dated narrative for everything below is in
[docs/CHANGELOG.md](docs/CHANGELOG.md).

**Current state**
- Released through `v1.5.4` (tags `v1.5.0`-`v1.5.4`, 2026-10-26): XpressNet (MultiMaus),
  Z21 LAN (WLANmaus), LocoNet (Digitrax UT6) and Ecos all bridged. Operating model: a
  change on any interface is mirrored to Ecos AND every other interface; Ecos is the
  DCC interface and the source of truth.
- Verified live on real hardware: speed, direction, F0-F28, track power/E-stop, and
  turnouts across all four interfaces; function and turnout displays mirror on a
  MultiMaus in any timing; LocoNet slot expiry; 160 MHz with Wi-Fi sleep off; ~13 KB free
  heap on the dev build.
- Native suite: 408 tests passing. `env:wemos` and `env:wemos_dev` build clean.
- The D1 on the bench runs the matching `wemos_dev` build (network flashing with
  `python tools/devflash.py`; never publish a .bin from that environment).

**Open / candidates** (nothing blocking)
1. Measure free heap on the plain `wemos` build (needs a USB flash with the D1 out of
   its socket) and confirm the production build behaves like the dev one.
2. **Parked by the user**: a UT6 only redraws a turnout's position after its own F key.
3. WLANmaus turnout display rarely refreshes from the (confirmed-delivered) Z21 turnout
   broadcast (Phase 7 item 6) - low priority.
4. Not yet exercised: F13+ from the UT6; no dedicated OLED turnout page.
5. A long soak test with all four interfaces active.
