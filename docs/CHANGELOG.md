# XpressNet-Ecos Bridge — Development Changelog

Detailed, dated session history for this project. This is the full "what happened
and why" record — root causes, investigation steps, exact numbers. `CLAUDE.md`
stays focused on current architecture/state/conventions; anything historical lives
here. Newest entries first.

---

**2026-07-31 — Ecos-side validation: two real connection bugs found and fixed against a live Ecos**
- **Trigger**: reverted the two temporary diagnostic aids left active from Phase 4.6's
  XpressNet-only testing (`TEMP_SKIP_ECOS_CONNECT` in `ecos_interface.cpp`,
  `XNetDEBUG`/`XNetSerial` in `libraries/XpressNetMaster/XpressNetMaster.h`, both
  restored to the vendored/pre-diagnostic state) and flashed against a real,
  reachable Ecos (`192.168.0.50:15471`) for the first time - Ecos had never
  actually been connected to during any earlier session.
- **Bug 1 - connection dropped in a ~10-second loop**: first flash showed
  `Ecos TCP connected!` and a correct address-map reply (`DCC 24 → Ecos ID 1004`),
  but then `Ecos heartbeat timeout` and a reconnect within seconds, repeating
  indefinitely. Root cause: `updateConnectionStatus()`
  (`ecos_interface.cpp` ~line 160) had a hardcoded "no data for 10 sec = dead
  connection" watchdog, but `sendHeartbeat()` is only scheduled every
  `ECOS_HEARTBEAT_INTERVAL` (30 sec, `config.h`) - the watchdog always fired
  first, so the heartbeat mechanism could never keep a connection alive in
  practice. This could never have surfaced before now since Ecos was never
  actually connected during any prior test (Phase 3.2 was mock-only; Phase 4.6
  deliberately disconnected it via `TEMP_SKIP_ECOS_CONNECT`).
  **Fixed**: added `ECOS_MESSAGE_TIMEOUT` to `config.h`, defined as
  `ECOS_HEARTBEAT_INTERVAL + 15000` (a comment documents why it must exceed the
  heartbeat interval with margin) instead of a bare magic number, and switched
  the watchdog check to use it.
- **Bug 2 - the heartbeat command itself was rejected by Ecos**: with bug 1 fixed,
  the connection survived past 10 seconds but the first heartbeat produced
  `Ecos error response: 18 (internal error at 9)`. Root cause: `sendHeartbeat()`
  sent `get(10, name)` - `ECOS_OBJECT_LOCOMOTIVE_MANAGER` (10) is a query
  *category* used with `queryObjects(10, addr, name)` for enumeration, not an
  addressable object with its own `name` property, so Ecos correctly rejected it.
  **Fixed**: `sendHeartbeat()` now just calls the already-proven-working
  `queryAddressMap()` (the same `queryObjects(10, addr, name)` call used at
  connect time) instead of building a separate, invalid `get()` command - this
  also means the address map now effectively refreshes every 30 sec instead of
  only every 10 min, a harmless side benefit.
- **Result, confirmed on real hardware**: reflashed and monitored ~90 seconds
  (three full heartbeat cycles at 15:22:18/15:22:48/15:23:18, each exactly 30s
  apart) - `Ecos=Connected` held continuously the whole window, each heartbeat
  got a clean address-map reply, zero disconnects or error responses. Native
  suite still 91/91 passing; `env:debug` rebuilds clean (44.7% RAM / 31.2% Flash,
  unchanged).
- **Not yet done**: end-to-end command propagation (XpressNet throttle command
  reaching Ecos and back, Ecos-side changes reaching XpressNet) and real-timing
  subscription lifecycle (5-min inactivity expiry) are still unverified - next
  steps for closing out Phase 4.6.

---

**2026-07-31 — First real end-to-end XpressNet→Ecos test: physical RX fault recurrence, then two more real bugs found and fixed**
- **Trigger**: with the address map now correctly listing loco 5452 (the
  MultiMaus's loco), attempted the first real end-to-end test - move the
  throttle, confirm the command reaches Ecos.
- **False start - physical RX path fault recurred**: two capture attempts
  (60s and 90s, with the user deliberately waiting ~15s after each flash for
  the DTR-triggered reboot to settle before touching the throttle) both
  showed `XNet=Disconnected` for the *entire* window and `Locos=0` never
  changed - meaning zero valid XpressNet messages were received, not just a
  display glitch. Cross-checking against every capture from earlier today
  confirmed this predated all of today's Ecos work (even the very first
  capture right after reverting `TEMP_SKIP_ECOS_CONNECT`/`XNetDEBUG` never
  showed "Bus connected!") - and confirmed `XNetDEBUG` only gates print
  statements in the vendored library, no functional code path, ruling out
  today's revert as the cause. The user confirmed the MultiMaus's own display
  showed normal/active (not `Err 13`), meaning master→throttle (TX) was
  still fine - and separately mentioned an already-known symptom: unplugging
  the MAX485 causes `Err 13` that doesn't recover on its own and needs
  "wiggling" the module or a power cycle to clear. That's a textbook loose
  physical connection, and matches the exact TX-works/RX-fragile asymmetry
  already root-caused on 2026-07-29 (MAX485 undervoltage, a resistor wired
  in parallel instead of series, bad modules) - very plausibly re-jostled
  loose by today's dozen-plus reflashes. **Fix**: physical only - wiggling
  the MAX485's connections (no code change). Retested and got full real
  traffic immediately.
- **Full real end-to-end propagation confirmed**: with the connection
  physically restored, a MultiMaus speed sweep (dial swept both directions)
  produced a clean pipeline for every command:
  `XpressNet RX: Speed - Addr=5452 Speed=20 Dir=0` -> loco created in
  `StateEngine` -> `Subscribed to loco 5452 (Ecos ID 1009)` ->
  `Ecos TX: Speed loco 5452 = 20` - repeated correctly across a full range
  including a direction reversal (`Dir=1`). This is the core Phase 4.6 goal:
  a real XpressNet command reaching a real, connected Ecos.
- **Bug found while reviewing that same log - `subscribed_to_ecos` never set
  true by the XpressNet-initiated path**: the log showed "New loco from
  XpressNet: requesting Ecos subscription" on every single speed update
  instead of just the first - a "known side-effect, not yet investigated"
  item flagged back on 2026-07-30. Root cause: `subscribed_to_ecos` is set
  `true` in 4 places in `command_router.cpp`, but all 4 are on the
  *Ecos-initiated* path (`handleEcosCommand`/`handleEcosFunctionCommand`) -
  the *XpressNet-initiated* path (`handleXpressNetCommand`/
  `handleXpressNetFunctionCommand`) calls `requestEcosSubscription()` but
  never marks the loco as subscribed afterward, so every subsequent command
  for that loco looked like "first time" again, forever.
  **Fixed**: both XpressNet-initiated handlers now set
  `subscribed_to_ecos = true` as soon as the subscription is requested
  (not only once Ecos confirms it) - `subscribeToLoco()` is a fire-and-forget
  TCP query with no clean way to correlate Ecos's async reply back to a
  specific request, and the flag's existing semantics elsewhere in the file
  are already "do we know about this relationship," not "has Ecos
  specifically confirmed this one" - so this is consistent, not a
  weakening. A local `already_subscribed` bool captures the pre-update value
  so the actual `requestEcosSubscription()` call still only fires once.
- **Second bug found while fixing the first - XpressNet status display stuck
  on "Disconnected" forever once it timed out once**: while confirming the
  end-to-end log, noticed `Status: XNet=Disconnected` was still being
  printed even in the middle of clean, continuous real traffic flowing
  through (the RX-path fault above already proved the *status field* isn't
  what gates real command processing, but it's still a real reporting bug
  worth fixing). Root cause: `XpressNetInterface::markBusActivity()`
  (`xpressnet_interface.cpp`) only handled the `CONNECTING -> CONNECTED`
  transition; once `updateBusStatus()`'s 3-second "no initial activity"
  grace period had already timed out to `DISCONNECTED` (which it always
  does if the throttle doesn't speak within the first 3 seconds after boot -
  a near-certainty in practice, since the human needs time to notice and
  touch the throttle), no amount of subsequent real traffic could ever move
  it back to `CONNECTED` - the guard's condition was simply never satisfied
  again. **Fixed**: broadened the guard to `if (current_status !=
  ComponentStatus::CONNECTED)`, so activity from any prior state
  (`CONNECTING` or `DISCONNECTED`) correctly restores `CONNECTED`.
- **Result, confirmed on real hardware**: reflashed once more and reran the
  speed-sweep test - `XpressNet: Bus connected! First message from device`
  now fires correctly, `Status: XNet=Connected Ecos=Connected Locos=1`
  displays correctly, and "requesting Ecos subscription" appears exactly
  once across a sequence of 8+ speed commands (99/53/27/28/9/29/66/112),
  every one of which still correctly reached Ecos. Added
  `test_router_repeat_xpressnet_commands_do_not_resubscribe` (regression
  test for the subscription bug, driven directly off the real log's speed
  sequence). Native suite: 93/93 passing. `env:debug` rebuilds clean, memory
  unchanged (44.7% RAM / 31.2% Flash).
- **Noted, not investigated**: one `XpressNet: Invalid speed byte 0xff for
  addr 5452` appeared once near the end of the retest, likely a reserved/
  edge-case byte from a dial extreme or fast direction reversal - the parser
  correctly rejected it rather than mishandling it (consistent with reserved
  speed codes being already-documented as unmodeled), so not treated as a
  bug unless it recurs as an actual problem.
- **Remaining Phase 4.6 items**: Ecos-side changes propagating back to
  XpressNet, and the 5-minute subscription-lifecycle timeout under real
  (not mocked) timing.

---

**2026-07-31 — Address map only ever kept the last of many locomotives (two more real bugs, found by inspecting the log)**
- **Trigger**: user asked what the "Address map: DCC 24" log line meant, and
  pointed out they have loco 5452 selected on the MultiMaus while "definitely
  many more locos defined in Ecos" - correctly suspecting the single entry
  didn't match reality.
- **Bug 1 - `EcosMessageParser` collapsed multi-line blocks down to one
  entry**: added a temporary raw-line dump (`ecos_message_parser.cpp`, since
  removed) right before block parsing, which proved Ecos's real
  `queryObjects(10, addr, name)` reply carries **15 content lines, one per
  locomotive** - including `1009 name["BN U28B #5452"] addr[5452]`, the exact
  loco on the MultiMaus. Root cause: `EcosReply` models "one block = one
  object's state" (correct for `request(id,view)`/`<EVENT id>`, which really
  are single-line), but `parseBlock()` looped over every content line writing
  into the *same shared* `EcosReply`, so each line overwrote the last -
  only whichever locomotive Ecos listed last (`DCC 24`) survived into the
  address map. All 14 others, including 5452, were parsed correctly and then
  silently discarded.
- **User pushback on the fix's first draft**: the initial plan sized the new
  per-line buffer at the old arbitrary `MAX_BLOCK_LINES` (20). User pointed
  out their own test setup already has 14-15 locos and real layouts can have
  many more - correctly flagging that 20 would just move the silent-data-loss
  bug to a slightly higher threshold instead of fixing it properly.
- **Fixed**: reworked `EcosMessageParser` to parse each content line
  immediately into its own `EcosReply` entry (instead of buffering raw text
  for later batch-parsing), sized to `MAX_ECOS_OBJECTS` (config.h, 100) -
  the same authoritative cap the address map itself already uses, rather than
  inventing a second, independently-drifting constant. `processByte()` keeps
  its existing two-arg signature and still writes the first entry into its
  output param (so all ~14 pre-existing tests needed zero changes); a new
  `getNextQueuedReply()` method lets the caller drain any additional entries
  a multi-line block produced. `EcosInterface::update()`'s read loop now
  drains the queue after every `handleReply()` call. Block-level fields
  (kind/end_code/end_text) are stamped onto every queued entry at `<END>`
  time; a block with zero content lines (bare acks/errors) still emits
  exactly one reply, preserving the last-message-time watchdog reset from the
  2026-07-31 heartbeat fix above.
- **Bug 2 - address map only ever appended, never updated**: fixing bug 1
  surfaced a second, latent bug it would have made visible almost
  immediately: since the heartbeat now re-runs the same `queryObjects` query
  every 30 seconds (per the fix above), `addAddressMapEntry()` being
  append-only meant the map would gain 15 duplicate rows every cycle and hit
  `MAX_ECOS_OBJECTS` (100) within about 3 minutes, silently refusing all
  further entries ("Map full") from then on. This bug already existed before
  today (the old 10-minute refresh timer would have hit it too, just over
  ~67 minutes instead of ~3) but was never observed since Ecos had never
  actually been connected long enough. **Fixed**: `addAddressMapEntry()` now
  checks for an existing `dcc_address` and updates its `ecos_id` in place
  before falling back to appending a genuinely new entry.
- **Result, confirmed on real hardware**: all 15 real locomotives now appear
  in the address map on connect (`DCC 17, 2655, 41, 72, 20, 3, 34, 1702, 5592,
  5452, 1445, 9524, 1841, 1818, 24`), and two subsequent heartbeat cycles
  (30s apart) reproduce the exact same 15 entries with no growth or
  duplication. Added `test_ecos_parse_query_objects_multiple_locos` (drains
  a 3-loco fixture and checks every entry) and reworked
  `test_ecos_block_with_too_many_lines_discards_oldest` into
  `test_ecos_block_with_too_many_lines_drops_excess` (now actually exercises
  the real `MAX_ECOS_OBJECTS`-sized cap instead of a no-longer-existent
  20-line one). Native suite: 92/92 passing. `env:debug` rebuilds clean,
  memory essentially unchanged (44.7% RAM / 31.2% Flash - parsed-struct
  storage turned out no more expensive than the raw-text buffer it replaced).
- **Not yet done**: end-to-end command propagation (XpressNet throttle
  command reaching Ecos and back) and real-timing subscription lifecycle are
  still the remaining Phase 4.6 items.

---

**2026-07-31 — Ecos-side validation: two real connection bugs found and fixed against a live Ecos**
- **Trigger**: reverted the two temporary diagnostic aids left active from Phase 4.6's
  XpressNet-only testing (`TEMP_SKIP_ECOS_CONNECT` in `ecos_interface.cpp`,
  `XNetDEBUG`/`XNetSerial` in `libraries/XpressNetMaster/XpressNetMaster.h`, both
  restored to the vendored/pre-diagnostic state) and flashed against a real,
  reachable Ecos (`192.168.0.50:15471`) for the first time - Ecos had never
  actually been connected to during any earlier session.
- **Bug 1 - connection dropped in a ~10-second loop**: first flash showed
  `Ecos TCP connected!` and a correct address-map reply (`DCC 24 → Ecos ID 1004`),
  but then `Ecos heartbeat timeout` and a reconnect within seconds, repeating
  indefinitely. Root cause: `updateConnectionStatus()`
  (`ecos_interface.cpp` ~line 160) had a hardcoded "no data for 10 sec = dead
  connection" watchdog, but `sendHeartbeat()` is only scheduled every
  `ECOS_HEARTBEAT_INTERVAL` (30 sec, `config.h`) - the watchdog always fired
  first, so the heartbeat mechanism could never keep a connection alive in
  practice. This could never have surfaced before now since Ecos was never
  actually connected during any prior test (Phase 3.2 was mock-only; Phase 4.6
  deliberately disconnected it via `TEMP_SKIP_ECOS_CONNECT`).
  **Fixed**: added `ECOS_MESSAGE_TIMEOUT` to `config.h`, defined as
  `ECOS_HEARTBEAT_INTERVAL + 15000` (a comment documents why it must exceed the
  heartbeat interval with margin) instead of a bare magic number, and switched
  the watchdog check to use it.
- **Bug 2 - the heartbeat command itself was rejected by Ecos**: with bug 1 fixed,
  the connection survived past 10 seconds but the first heartbeat produced
  `Ecos error response: 18 (internal error at 9)`. Root cause: `sendHeartbeat()`
  sent `get(10, name)` - `ECOS_OBJECT_LOCOMOTIVE_MANAGER` (10) is a query
  *category* used with `queryObjects(10, addr, name)` for enumeration, not an
  addressable object with its own `name` property, so Ecos correctly rejected it.
  **Fixed**: `sendHeartbeat()` now just calls the already-proven-working
  `queryAddressMap()` (the same `queryObjects(10, addr, name)` call used at
  connect time) instead of building a separate, invalid `get()` command - this
  also means the address map now effectively refreshes every 30 sec instead of
  only every 10 min, a harmless side benefit.
- **Result, confirmed on real hardware**: reflashed and monitored ~90 seconds
  (three full heartbeat cycles at 15:22:18/15:22:48/15:23:18, each exactly 30s
  apart) - `Ecos=Connected` held continuously the whole window, each heartbeat
  got a clean address-map reply, zero disconnects or error responses. Native
  suite still 91/91 passing; `env:debug` rebuilds clean (44.7% RAM / 31.2% Flash,
  unchanged).
- **Not yet done**: end-to-end command propagation (XpressNet throttle command
  reaching Ecos and back, Ecos-side changes reaching XpressNet) and real-timing
  subscription lifecycle (5-min inactivity expiry) are still unverified - next
  steps for closing out Phase 4.6.

---

**2026-07-30 — XpressNet drive commands confirmed working end-to-end (root cause: missing 14/27/28-speed-step handlers)**
- **Trigger**: continuing the 2026-07-29 RX-path investigation, enabled the
  XpressNetMaster library's built-in `XNetDEBUG` raw-byte logging
  (`libraries/XpressNetMaster/XpressNetMaster.h`) to get firmware-level ground
  truth instead of continuing to interpret analog oscilloscope traces. First
  capture immediately showed real, checksum-valid XpressNet traffic arriving
  every time the MultiMaus throttle was touched - proving the RX electrical
  path (MAX485 receiver, D6, resistor) was never actually broken. The multi-day
  hardware debugging (diode/resistor/bad-modules root causes, TX/RX signal
  tracing) was real and necessary work, but this final layer of silence was a
  software gap, not a hardware one.
- **Root cause**: decoded the raw capture (`1-E4 2-12 3-D5 4-4C 5-xx 6-xx MRX:
  0x1C3 0xE4 0x12 0xD5 0x4C 0x8x 0xEx`) against
  `XpressNetMaster.cpp`'s dispatch table (`case 0xE4`, ~line 454): `data1=0x12`
  selects **28-speed-step mode**, which the library routes to
  `notifyXNetLocoDrive28` - a different callback from the `notifyXNetLocoDrive128`
  that `xpressnet_interface.cpp` implemented. Since `notifyXNetLocoDrive14/27/28`
  are weak, undefined externs with no implementation anywhere in this project,
  their linked address resolves to null, so the library's
  `if (notifyXNetLocoDrive28) notifyXNetLocoDrive28(...)` guard silently dropped
  every single drive command from the throttle. The MultiMaus's loco (DCC
  address 5452, decoded from the constant `0xD5 0x4C` address bytes) was
  configured for 28 steps, not 128 - confirmed by testing: changing the
  MultiMaus's per-loco step setting to 128 did *not* change the wire traffic
  (still `data1=0x12` after reselecting the loco), so the fix had to be on the
  firmware side regardless of throttle configuration.
- **Fixed**: added `XpressNetInterface::onLocoDriveStepped()`
  (`xpressnet_interface.h/.cpp`) plus `notifyXNetLocoDrive14/27/28` free-function
  callbacks, mirroring the existing `onLocoDrive128` pattern. 14-step uses the
  same plain `RVVVVVVV` byte layout as 128-step (just a smaller meaningful
  range); 27/28-step reverses the bit-interleaved 5-bit speed encoding that
  `XpressNetMasterClass::setSpeed()` uses on the *send* side
  (`v = ((s&0x0F)<<1) | ((s>>4)&0x01) | dir`). All three rescale their raw
  step-count-relative speed linearly into this bridge's internal 128-step-based
  0-126 range (`speed = raw_speed * DCC_MAX_SPEED / max_steps`), so
  `CommandRouter`/`StateEngine` never need to know which step mode the throttle
  used. Reserved/emergency-stop codes for each mode are not specially modeled -
  same simplification already accepted for 128-step's `V=127` case.
- **Result**: real hardware confirmed - example from serial log:
  `XpressNet RX: Speed (28-step) - Addr=5452 RawSpeed=18 Speed=81 Dir=0` followed
  by `CommandRouter` creating the loco, requesting Ecos subscription, and
  broadcasting the command - the full intended pipeline, for the first time,
  driven by a real physical throttle. `env:debug` rebuilt clean (44.5% RAM /
  31.2% Flash, up slightly from the new handlers) and reflashed successfully.
- **Known side-effect, not yet investigated**: "New loco from XpressNet:
  requesting Ecos subscription" logs on *every* speed update instead of once -
  very likely because Ecos is still disconnected
  (`TEMP_SKIP_ECOS_CONNECT` from the 2026-07-29 entry is still active), so the
  subscription never completes and the loco never leaves pending state. Needs
  re-checking once real Ecos connectivity is restored, before treating it as a
  bug.
- **Not yet reverted**: `XNetDEBUG`/`XNetSerial` in
  `libraries/XpressNetMaster/XpressNetMaster.h` and `TEMP_SKIP_ECOS_CONNECT` in
  `ecos_interface.cpp` are both still active for continued hardware testing
  (function commands, other speed/direction values, then Ecos-side validation).
  Revert both once XpressNet is fully validated and Ecos testing resumes, per
  the 2026-07-29 entries.
- **Follow-up same day - MultiMaus stuck at 28 steps, resolved by implementing
  the master's LocoInfo reply (no Ecos dependency needed)**: user reported the
  MultiMaus's own menu doesn't let them change a loco's speed-step setting - it
  stays at 28 regardless. Real XpressNet's actual contract is the reverse of
  what a throttle-side menu implies: **the master declares the step count per
  loco, and slave throttles obey** - not something the throttle unilaterally
  decides. Reasoned through why this doesn't require the "wait for Ecos before
  answering XpressNet" contract change the user raised: the step-count
  declaration is a pure master<->throttle wire contract, decoupled from Ecos -
  Ecos only ever sees this bridge's internal 0-126 speed value and never
  needs to agree on what step count we told a throttle, so the master can
  answer immediately from local `StateEngine` state (defaulting to
  stopped/no-functions if the loco is unknown) with no Ecos dependency at all.
  Implemented `onGiveLocoInfo()` (`notifyXNetgiveLocoInfo`, XpressNet header
  0xE3/data1=0x00, the generic Lenz "request loco info" message) replying via
  `xnet.SetLocoInfo()`, always declaring `Loco128`. First flash of this alone
  didn't change the real MultiMaus's behavior - live serial monitoring caught
  why: **the MultiMaus doesn't send the generic 0x00 request at all - it sends
  a proprietary MultiMaus-specific request (header 0xE3/data1=0xF0, "Lok und
  Funktionszustand MultiMaus anfordern")**, routed by the library to a
  completely different callback (`notifyXNetgiveLocoMM`) expecting a different
  reply format (`SetLocoInfoMM`, header 0xE7, covers F0-F20 in one message
  instead of the generic reply's F0-F12). This lines up with a comment already
  present in the vendored library (`SetLocoInfoMM`, ~line 939) about step
  counts not sticking for addresses >99 - that comment turned out to describe
  a real MultiMaus behavior, but specifically for the *unimplemented* MM reply
  path, not an unfixable hardware limitation. Implemented `onGiveLocoMM()`
  answering the real request the MultiMaus actually sends, also always
  declaring `Loco128`, sharing a new `resolveLocoStateForReply()` helper with
  `onGiveLocoInfo()` for the common StateEngine lookup + RVVVVVVV speed-byte
  encoding.
  **Result, confirmed on real hardware**: after reselecting loco 5452, all
  subsequent drive commands arrived as genuine 128-step (`data1=0x13`,
  `XpressNet RX: Speed - Addr=5452 Speed=...`, no longer the 28-step path) -
  91 speed messages across both directions (40x Dir=1, 51x Dir=0), only 2
  stray 28-step messages in the brief window before the first `LocoInfoMM`
  reply landed. Function group 1 (F0/F3) also confirmed correct
  (`Fn=0x00→0x01→0x09→0x01`). The bridge no longer needs its 14/27/28-step
  fallback handlers for this throttle in practice, though they remain in place
  as a safety net for any throttle/loco that doesn't obey the master's
  declaration.
- **Follow-up same day - MultiMaus STOP button froze the display, requiring a
  physical unplug/replug to recover**: this was a previously-deprioritized
  finding from earlier in Phase 4.6, resurfaced by the user now that XpressNet
  RX is otherwise fully working. Root cause: the library's three separate
  incoming track-power/emergency-stop request paths (the MultiMaus's physical
  STOP button most likely sends the simple `0x80`/data1=`0x80` "EmStop"
  broadcast) all only update the library's own internal `Railpower` state and
  fire `notifyXNetPower(state)` **if implemented** - none of them
  automatically echo any acknowledgment back onto the bus. This callback was
  never implemented in `xpressnet_interface.cpp`, so the master silently
  absorbed every STOP/resume request with zero response - the MultiMaus was
  very likely just waiting indefinitely for a confirmation broadcast that
  never came, exactly matching the freeze/unplug-to-recover symptom. Matches
  the already-documented "Future Improvements" backlog item (bus-wide
  emergency stop "nothing consumes it yet").
  **Fixed** (deliberately minimal scope, chosen over the fuller feature):
  added `onPowerStateChange()` / `notifyXNetPower` calling
  `xnet.setPower(state)`, which broadcasts the state back to all XpressNet
  devices (`GENERAL_BROADCAST`, no per-device targeting needed) - purely a
  wire-protocol acknowledgment fix. It does **not** force any locomotives to
  actually stop moving - that remains the deferred "Future Improvements" item,
  now with a concrete implementation path (iterate `StateEngine`, force
  speed=0, propagate to other XpressNet devices and Ecos) whenever it's
  wanted.
  **Confirmed on real hardware**: pressing STOP now displays immediately
  (`state=0x01`/`csEmergencyStop` logged) and un-stop resets cleanly
  (`state=0x00`/`csNormal` logged) - no more unplug required.
- **Earlier same-day follow-up - function commands confirmed too**: with the fix flashed,
  ran a live serial monitor while the user manually toggled F0, F1, and F3 on the
  real MultiMaus and varied speed/direction. Confirmed correct end-to-end: speed
  spanned the full range with both directions (e.g. `RawSpeed=26 Speed=117 Dir=1`,
  `RawSpeed=2 Speed=9 Dir=0`), and function bitmap decode was exactly right
  (`Fn=0x01`=F0 on, `Fn=0x03`=F0+F1, `Fn=0x0b`=F0+F1+F3, back down to `Fn=0x00`).
  **XpressNet RX (speed, direction, all tested function groups) is now fully
  validated on real hardware** - the "XpressNet working 100% before Ecos testing"
  bar the user set is met. Also newly observed: `Ecos: Outgoing queue full,
  dropping oldest` logs on every command now, alongside the already-known
  repeating "New loco... requesting Ecos subscription" - both are expected
  consequences of Ecos being deliberately disconnected (`TEMP_SKIP_ECOS_CONNECT`),
  not new bugs; re-check once real Ecos connectivity is restored.

**2026-07-29 — XpressNet TX path confirmed working, RX path (MultiMaus→bridge) now suspect**
- **Trigger**: after the bus-connection fix (see entry below), unplugging the MultiMaus
  live (while powered) caused it to show `Err 13` again and required a manual reset of
  the Wemos to recover - itself a real finding worth revisiting later (a master
  shouldn't need resetting just because a slave disconnects; possibly a hot-plug
  transient this simple setup has no protection against). After reboot + reconnecting
  the MultiMaus, a deeper issue surfaced.
- **Key finding**: the MultiMaus shows no `Err 13` and appears fully active (loco
  selected, speed set, functions toggled) - but the bridge's debug log has *never*
  printed `XpressNet: Bus connected!` or any `XpressNet RX:` message since that reboot,
  staying permanently at `XNet=Disconnected`. Confirmed this isn't just a logging gap:
  deliberately changing speed/direction/toggling a function live, while watching the
  serial monitor, produces zero output.
- **Diagnosis so far**: the MultiMaus clearing `Err 13` only proves it's receiving the
  master's call-byte traffic (the TX path already validated via oscilloscope - see
  entry below). It does NOT prove the master is receiving anything back. This points
  to the **RX path** (MultiMaus → bus → MAX485 A/B input → MAX485 receiver → RO →
  through the 1kΩ resistor → D6 → ESP8266) being broken, separate from the TX path
  (ESP8266 → D6 → resistor → MAX485 DI → driver → A/B output) confirmed working
  earlier with the same chip.
- **Oscilloscope evidence**: widening the timebase to ~2ms after the master's own
  DE-triggered transmit pulse (per the XpressNet spec, a slave must respond within
  100us) shows only small, rounded, barely-above-baseline bumps - not the clean
  square digital edges seen on the TX side. This looks like a heavily attenuated or
  capacitively-coupled signal, not a properly driven response.
- **Not yet determined - next step to resume with**: whether this weak signal is
  already present at the MAX485's A/B pins (bus side, meaning the incoming signal
  from the MultiMaus/cable is weak before reaching our chip) or only appears at
  RO/D6 (meaning this specific chip's receiver stage is degraded, separate from its
  driver stage which is confirmed fine - a chip can have one working and not the
  other). Resume by checking A/B directly during that same widened window, then
  compare to RO/D6.

**2026-07-29 — XpressNet bus connection established: real MultiMaus talking to the bridge**
- **Trigger**: first live test with a physical throttle (Roco MultiMaus) connected to the
  RS485 bus - MultiMaus displayed `Err 13 - no XpressNet master`, and the bridge's own
  debug log showed `XpressNet: Bus disconnected (no initial activity)` with zero
  received bytes in either direction.
- **Root cause was three stacked hardware faults**, found via systematic oscilloscope
  probing (DE/RE control signal → DI data signal → A/B differential output, at each
  stage moving the probe progressively closer to the physical MAX485 chip pins to
  localize the break):
  1. **MAX485 undervoltage**: a silicon 1N4004 diode was deliberately placed in the
     5V rail feeding the MAX485, to protect the ESP8266's 3.3V-only GPIOs from the
     module's 5V logic levels (specifically RO, which shares the same D6 half-duplex
     pin as DI). That diode's ~0.7V drop left the MAX485 running at 4.28V VCC -
     enough for its logic-level DE/DI inputs to still work, but below the ~4.75V
     minimum needed for its RS485 driver output stage to produce a valid differential
     signal. Fixed by removing the diode and instead adding a 1kΩ series resistor
     between D6 and the MAX485's DI/RO pin, relying on the ESP8266 GPIO's internal
     clamp diodes (current-limited by the resistor to ~1mA) for the same protection
     without starving the MAX485's own supply.
  2. **Resistor initially added in parallel, not series**: the original direct D6-to-DI/RO
     trace wasn't severed when the protective resistor was added, so the resistor had
     no effect and the ESP8266 GPIO remained exposed to unprotected 5V. Fixed by
     cutting the direct trace so the resistor is genuinely in series.
  3. **Two separate faulty MAX485 modules** discovered in the parts bin during
     substitution testing (both now clearly marked and set aside) - these made the
     wiring fixes above look ineffective until a third, genuinely good module was
     tried and finally showed correct differential A/B output.
- **Diagnostic technique worth remembering**: when a signal looks wrong, move the
  probe progressively closer to the actual component pins rather than trusting nearby
  test points/headers - this caught both a bad ground reference (giving false flat
  -4V readings across unrelated pins) and confirmed the DE/DI signals were correct
  at the chip before concluding the fault was in the A/B driver stage specifically.
- **Result**: `XpressNet: Bus connected!` - real MultiMaus now recognizes the bridge
  as XpressNet master (Err 13 cleared), throttle in active mode. This is the first
  confirmed real-hardware validation of the XpressNetMaster-library-based interface
  from the 2026-07-27 rewrite. Next: verify actual speed/direction/function commands
  from the throttle are parsed and routed correctly (watch for
  `XpressNet RX: Speed - Addr=... Speed=... Dir=...` in the debug log).

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
  #error ...` ([config.h](../xpressnet_ecos_bridge/config.h)). Real values live in
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
  failing. Cosmetic only, not fixed - see entry above).
- **Discovery**: that wiring didn't match anything in the repo, and digging into why
  surfaced that the existing XpressNet code had never been validated against real
  hardware or the real protocol, despite its own header comment claiming otherwise:
  * `xpressnet_interface.h` said *"Uses Gahtow's XpressNetMaster library for
    hardware abstraction"* - but the `.cpp` never used it. It hand-rolled everything
    on `Serial1` with separate RX(D7)/TX(D8)/DE(D5) pins, and the library wasn't
    present anywhere in the repo.
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

---

**How this file works**: newest entries go at the top, directly below the header note.
Full narrative detail (root causes, exact numbers, investigation steps) belongs here.
`CLAUDE.md` only carries a short current-state summary and links back to the relevant
dated entry when detail is needed.
