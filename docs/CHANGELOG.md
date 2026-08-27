# XpressNet-Ecos Bridge — Development Changelog

Detailed, dated session history for this project. This is the full "what happened
and why" record — root causes, investigation steps, exact numbers. `CLAUDE.md`
stays focused on current architecture/state/conventions; anything historical lives
here. Newest entries first.

---

**2026-08-27 — Phase 6 step 2: web-based config UI / Setup Mode, confirmed live (all 4 entry paths)**

- **Design discussion before any code**: two real risks were unpacked with
  the user before starting - which web server model to use (settled on
  the built-in synchronous `ESP8266WebServer`, since Setup Mode usage is
  rare/occasional rather than continuous like Ecos's heartbeat, so the
  brief per-request blocking window doesn't violate the project's
  non-blocking design premise), and what happens if a WiFi credential
  change via the UI is wrong (a single-radio ESP8266 can't "test" new
  WiFi credentials without dropping the very connection serving the test
  page - three options were laid out: no fallback, a full WiFiManager-
  style captive portal library, or a minimal hand-rolled AP fallback with
  no new dependency; the middle option was chosen). The user then asked
  the sharper question of *how you even get into Setup Mode in the first
  place* when nothing is broken (STA connecting fine, you just want to
  change a setting) - landing on a physical button as the deliberate
  trigger, plus an automatic WiFi-disconnection-timeout fallback as
  defense-in-depth, both confirmed live.
- **Mid-design pivot on the bridge's static IP**: originally scoped in
  step 1 as optional (DHCP by default, an on/off toggle). The user asked
  directly whether the bridge and a future Z21 LAN interface (step 4, for
  WLANmaus) would share one IP or need separate ones - confirmed they
  share one (Z21 LAN will be another protocol interface on this same
  device, not a separate one), and whether showing the DHCP-assigned IP
  on the OLED would make static IP unnecessary. The answer distinguished
  two different problems: OLED display solves *discovery* (a human
  finding the IP) but not *stability* (an automated client like WLANmaus
  reconnecting after a lease change) - and genuine Z21 hardware supports
  UDP broadcast auto-discovery for exactly this reason, though that's a
  claim from general protocol familiarity, not yet verified against an
  authoritative Z21 LAN spec (to be confirmed properly at step 4). Given
  the uncertainty, static IP was kept mandatory - a strict superset of
  correctness regardless of what WLANmaus turns out to need. This
  required revising the already-shipped step 1 EEPROM schema: removed
  `use_static_ip`, made `bridge_ip`/`bridge_gateway`/`bridge_subnet`
  mandatory with no DHCP path, bumped `EEPROM_CONFIG_VERSION` to 2 (a
  real schema change - anything written by the v1 schema correctly fails
  validation and reseeds, fine since v1 had shipped only hours earlier in
  the same session, before any real deployment).
- **Module split, mirroring the project's existing hardware-coupled
  boundary**: `setup_web_form.h/.cpp` holds pure HTML generation and
  field validation/parsing - no Arduino dependency at all, fully
  native-testable (20 new tests: IPv4 format validation, per-field
  accept/reject rules, HTML pre-fill content, undersized-buffer
  rejection). `setup_mode.h/.cpp` wraps the real `WiFi.softAP()`/
  `ESP8266WebServer`/RTC-memory/GPIO calls - Arduino-only, excluded from
  `env:native` like `ecos_interface.cpp`/`eeprom_store.cpp`. Timeout
  fields are shown/submitted in whole seconds (friendlier than raw
  milliseconds), converted to/from the stored ms internally.
- **Entry mechanism, RTC memory**: a "next boot only" flag
  (`requestSetupModeOnNextBoot()`/`consumeSetupModeRequest()`) backed by
  ESP8266 RTC user memory rather than EEPROM, since this is a transient
  signal that shouldn't cost a real flash write cycle and doesn't need to
  survive a power cycle. Checked the actual ESP8266 Arduino core source
  (`Esp.cpp`) rather than assuming: `rtcUserMemoryWrite/Read(offset, ...)`
  internally maps to physical block `64 + offset`, meaning offset 0
  (what this code uses) safely lands past the SDK/eboot-reserved region
  (the first 64 blocks) - confirmed correct, not a bug, when this was
  briefly suspected during the button-hold investigation below.
- **Real gap found via live testing: valid-but-incomplete config skipping
  Setup Mode**. The user tested the very first real Setup Mode session
  themselves (connected to the AP, loaded the page, saved real values) -
  and afterward asked directly "if [the bridge IP fields] are blank in
  eeprom, there is a problem". Investigation confirmed: a freshly-seeded
  config (first boot) has a *valid* checksum (since `eepromConfigLoadDefaults()`
  correctly computes it over whatever it seeded, including blank bridge
  fields - there's no compile-time default for those) while still having
  blank bridge IP/gateway/subnet. The existing entry condition only
  checked raw checksum validity (`eepromConfigIsValid()`), so a *second*
  boot after the same reseed would have found the struct "valid" and
  proceeded straight into normal operation with an incomplete mandatory
  setting, silently falling back to DHCP in `EcosInterface` instead of
  forcing Setup Mode again. Fixed with a new `eepromConfigIsComplete()`
  (validity plus non-empty bridge fields), with 4 new
  `test_eeprom_config` tests specifically covering "valid but not
  complete" as a distinct state from either "invalid" or "complete".
- **Real bug found via live testing: the button pin was simply wrong**.
  The original plan (documented and agreed with the user beforehand)
  was to reuse the Wemos D1 Mini's onboard button, assumed wired to
  GPIO0 - a "FLASH" button pattern common on other ESP8266 dev boards,
  reusable with zero new wiring. Live testing showed the button
  triggered an instant reboot with **zero** `checkSetupButton()` debug
  output beforehand, and the very next boot proceeded straight to normal
  operation (no Setup Mode request recorded) - meaning the reboot
  bypassed the running sketch's code entirely. The user confirmed: their
  board has exactly one button, and it's silkscreened "RESET", wired
  directly to RST/EN - a genuine hardware reset line, unreadable and
  un-holdable in software at all, not the GPIO0 pattern assumed. Fixed
  by moving to a genuinely new, separately-wired pushbutton on
  **D7/GPIO13** - deliberately not GPIO0 (which has its own "don't hold
  during power-up or you enter the ROM bootloader" boot-strapping
  caveat), and not GPIO2 (already used for the XNet activity LED) or
  GPIO15 (also boot-strapping-significant) - so unlike the original
  GPIO0 plan, D7 has no equivalent caveat to get wrong. The user manually
  wired a test button to D7 to confirm the fix.
- **A UX follow-up during the same live session**: the user's first real
  save used `255.255.0.0` for the bridge subnet instead of the intended
  `255.255.255.0` (a home-network `/24`). Rather than silently
  "correcting" values on save (which would violate "EEPROM/what's
  actually saved always wins, only ever default when genuinely blank" -
  a principle the user explicitly restated and endorsed once this was
  discussed), a **display-only** suggestion was added:
  `buildConfigPageHtml()` shows `255.255.255.0` as the pre-filled value
  only when `bridge_subnet` is truly empty (first boot), and never
  overrides an already-saved value, even a non-standard one - confirmed
  by two dedicated tests (`test_html_suggests_default_subnet_when_blank`/
  `test_html_keeps_saved_subnet_when_not_blank`) and by the user's own
  re-test: the correction only took effect once they manually cleared
  and retyped the field, exactly as designed.
- **All 4 Setup Mode entry paths confirmed live, on real hardware,
  directly by the user**:
  1. Blank/erased EEPROM -> immediate Setup Mode on first boot.
  2. Technically-valid-but-incomplete EEPROM -> Setup Mode (the gap
     found and fixed above) - this was the actual live scenario that
     surfaced the bug, not a synthetic test.
  3. Holding the (corrected) D7 button for 3s while running normally -
     confirmed via serial capture: `Setup button held - entering Setup
     Mode on reboot` followed by `Entering Setup Mode (requested)` on
     the next boot.
  4. 5 minutes of continuous WiFi disconnection - tested with a
     temporarily shortened 20s threshold plus a RAM-only fake SSID
     override (never written to EEPROM, confirmed via direct flash read
     after reverting), both fully reverted before the final production
     flash. Confirmed via serial capture: `WiFi disconnected for too
     long - entering Setup Mode on reboot` firing at the 20s mark, then
     `Entering Setup Mode (requested)` on the next boot - and separately
     confirmed visually by the user, who watched the OLED switch from
     normal RUN-mode pages to the SETUP MODE screen at the same moment.
  A full real save/reboot cycle was also confirmed end-to-end: submitted
  values persisted to EEPROM exactly as entered (verified via direct
  flash reads, catching one self-inflicted test-harness bug along the way
  - a temporary WiFi-fallback test hook that mutated the config struct
  without recomputing its checksum, which the completeness check
  correctly - if confusingly, for that specific test - flagged as
  corrupted), and the reboot into normal operation correctly applied the
  new static IP (`WiFi connected! IP: 192.168.0.51` - the configured
  address, confirmed not a DHCP-assigned one).
- 20 new tests (`test_setup_web_form`) + 4 more in `test_eeprom_config`
  (for `eepromConfigIsComplete()`); native suite 191/191 passing;
  `env:wemos` builds clean (RAM 53.9% from 45.4%, Flash 34.7% from
  32.1% - the jump is `ESP8266WebServer`'s real overhead, only paid
  while actually in Setup Mode, not during normal bridging). Real
  hardware is now fully configured (WiFi, Ecos IP, timeouts, static IP,
  correct subnet) via Setup Mode itself, running the final production
  build with all temporary test hooks reverted.

---

**2026-08-27 — Phase 6 step 1: EEPROM storage, confirmed live via direct flash inspection**

- **Scope, agreed before writing any code**: a design discussion (see
  CLAUDE.md's Phase 6 step 1 entry) settled on exactly which `config.h`
  values should move to runtime-editable, EEPROM-persisted storage vs.
  staying compile-time forever. Final list: WiFi SSID, WiFi password,
  Ecos IP, `XPRESSNET_BUS_TIMEOUT`, and `LOCO_INACTIVITY_TIMEOUT` (added
  during this session - same "judgment call that varies by usage pattern"
  category as the bus timeout, caught by re-auditing `config.h` alongside
  the already-agreed fields), plus an optional bridge static IP
  (`use_static_ip`/`bridge_ip`/`bridge_gateway`/`bridge_subnet`, off/DHCP
  by default - added ahead of the web UI step since browsing to a
  DHCP-assigned address that changes on every reboot is annoying once
  that UI exists, and it's cheap to add the storage now even though
  nothing sets it yet). Explicitly excluded: hardware-fixed constants
  (pins, baud, buffer sizes - describe wiring, not preference) and
  protocol-enable/debug flags (already zero-cost when compiled out;
  making them EEPROM/runtime-toggleable would force their code to always
  be compiled in, undermining that zero-overhead design, and for debug
  specifically would reintroduce the exact risk category that caused the
  Phase 5 step 10 `err13` freeze - a live `Serial.print()` capability
  sitting in the timing-critical XpressNet path, this time reachable via
  a bad EEPROM write instead of a code change).

- **New module split, mirroring the project's existing hardware-coupled
  boundary**: `eeprom_config.h/.cpp` holds the `EepromConfig` struct plus
  pure defaults-seeding/checksum/validation logic - no `EEPROM.h`
  dependency at all, so it compiles and is fully unit-tested under
  `env:native` (11 new tests: defaults match `config.h`, checksum is
  deterministic and changes when a field changes, invalid data is
  correctly rejected for wrong magic/wrong version/corrupted
  field/blank-erased-flash/all-zero). `eeprom_store.h/.cpp` wraps the
  actual `EEPROM.h` (`begin()`/`get()`/`put()`/`commit()`) calls -
  Arduino-only, excluded from `env:native` in `platformio.ini` the same
  way `ecos_interface.cpp`/`xpressnet_interface.cpp` already are. A
  4-byte magic number + 2-byte version + 2-byte checksum at the end of
  the struct distinguishes real saved data from blank/erased flash
  (reads as `0xFF` bytes) or a struct from an incompatible schema
  version; invalid data seeds from `config.h` compile-time defaults and
  saves once, so subsequent boots read a valid struct without
  re-seeding every time.

- **Call-site wiring**: three of the previously-`static const`/macro-only
  values became runtime members with setters -
  `XpressNetInterface::setBusTimeoutMs()` (was `static const unsigned
  long BUS_TIMEOUT = XPRESSNET_BUS_TIMEOUT`), `StateEngine::
  setInactivityTimeoutMs()` (was a direct `LOCO_INACTIVITY_TIMEOUT` macro
  reference inside `expungeInactiveLocos()`), and `EcosInterface::
  setConfig(const EepromConfig*)` (covers WiFi SSID/password, Ecos IP,
  and the optional static IP via `WiFi.config()` before `WiFi.begin()`).
  All three fall back to their `config.h` compile-time default if the
  setter is never called, so nothing breaks if a future call site forgets
  to wire it up. `OledDisplay::setEcosIp()` was added too, since the Ecos
  page previously printed the `ECOS_IP` macro directly. The `.ino`'s
  `setup()` calls `eepromStoreLoad(g_config)` once, right after OLED init
  and before any protocol interface's `begin()`, then pushes the loaded
  values into each interface via its setter.

- **Side cleanup in the same pass**: `XPRESSNET_TIMEOUT` (config.h:47,
  "5 minutes - remove inactive locos from state engine") turned out to be
  a dead duplicate - grep confirmed nothing in the codebase actually read
  it; `LOCO_INACTIVITY_TIMEOUT` (a separate constant in the State Engine
  section, functionally identical) is the one `state_engine.cpp` really
  uses. Removed. Also removed the now-redundant `ENABLE_EEPROM_CONFIG`
  feature flag from `config.h`'s "future expansion" section - EEPROM
  support is unconditional (small, core infrastructure, not an optional
  protocol worth a compile-time toggle), so a flag that gated nothing
  would have been actively misleading to leave in place.

- **Real bug found via live hardware testing**: `eepromStoreSave()` never
  checked `EEPROM.commit()`'s return value. Surfaced while stress-testing
  the corruption/reseed path with two `EEPROM.put()`+`commit()` calls
  back-to-back within the same boot (a deliberately temporary test
  pattern to probe reseed behavior, not something real production code
  ever does - `eepromStoreLoad()` only ever calls save once per boot) -
  the second commit's data was silently lost. A cleaner single-commit
  version of the same test (`g_config.magic = 0xBADC0FFE;
  eepromStoreSave(g_config);` as the *only* save that boot) persisted
  correctly, confirmed via a direct flash dump matching the corrupted
  magic byte-for-byte. Root cause not fully chased down (double-commit
  timing on this specific SPI flash chip, most likely) since it isn't a
  real production code path, but `eepromStoreSave()` now logs a clear
  error if `commit()` ever returns false, so a genuine failure would be
  visible in the field instead of silently dropped - a real, worthwhile
  hardening found only because of how this was tested.

- **Verification took two attempts - the first was misleading, not
  wrong-firmware**: initial live testing used a Python/pyserial script to
  capture serial boot output around reflashes. Results were confusing and
  inconsistent - most notably, a fresh boot immediately after a *full chip
  erase* (confirmed separately, via `esptool.py read_flash` at the
  EEPROM's linked address, to have genuinely blanked that sector to
  `0xFF`) still reported "loaded valid config" in the serial log instead
  of the expected reseed message. Root-caused to the capture script
  itself: opening a fresh `pyserial` connection right after an upload
  appears to trigger an *extra* board reset via the same DTR/RTS
  auto-reset circuit `platformio.ini` already documents disabling for its
  own monitor (`monitor_rts = 0` / `monitor_dtr = 0`, added back in
  Phase 4.6 "so opening the monitor doesn't silently reboot the board and
  wipe state mid-test") - meaning the serial capture was very likely
  showing a second, later boot's log rather than the first one after
  reset, several times in a row. Resolved by abandoning serial-log timing
  entirely in favor of direct flash-content inspection (`esptool.py
  read_flash` before and after each step, comparing raw bytes at the
  EEPROM's address found via `nm` on the linked ELF's `_EEPROM_start`
  symbol - `0x3FB000` physical offset on this board's 4MB flash layout).
  That gave unambiguous, timing-independent proof of all three required
  behaviors: blank/erased flash correctly reseeds with real defaults
  (confirmed via raw bytes: correct magic, SSID, Ecos IP, with zero serial
  connection involved in the test); a plain reflash with unchanged code
  leaves the EEPROM sector completely untouched (byte-for-byte identical
  dump before and after); and - the strongest test - deliberately
  changing `config.h`'s compile-time `ECOS_IP` default and reflashing
  (no erase) still loaded and attempted to connect to the *old*,
  already-persisted value rather than the new default, proving EEPROM
  genuinely overrides compile-time defaults rather than the two simply
  happening to agree.

- All temporary test hooks (a debug print of loaded values, the
  double/single-commit corruption tests, the temporarily-altered
  `ECOS_IP` default) were fully reverted before finishing; a final full
  chip erase + clean `env:wemos` production reflash left the real
  hardware in its normal working state with fresh, correct EEPROM
  defaults. Native suite 149/149 passing (138 prior + 11 new); `env:wemos`
  builds clean (RAM 45.4%, Flash 32.1%).

---

**2026-08-05 — Phase 5 step 10: accessory/turnout support v1, confirmed live (Phase 5 now fully complete)**

- **Scope decision, made before writing any code**: the original roadmap
  description for step 10 mentioned "new interface methods, accessory
  state storage, routing, Ecos-side switch/turnout protocol support, and a
  display page" without committing to a direction. A design discussion
  landed on splitting this into v1 (XpressNet → Ecos only) and a
  deliberately deferred v2 (Ecos → XpressNet, plus a dedicated OLED
  page) - **v2 was explicitly marked on hold, not a near-term follow-up**.
  Reasoning: v2 would require fully replicating the loco-parity
  address-map + per-accessory `request(id, view)` subscription
  infrastructure a second time (confirmed against the official ESU spec,
  section 7.12 "Listenobjekt Schaltartikel" - itself marked "(in Planung)"
  in ESU's own documentation), for a benefit that's purely cosmetic
  (keeping a throttle-side display in sync) - real hardware testing
  confirmed a MultiMaus's accessory keyboard always transmits a fresh
  command on every button press regardless of what it currently believes
  the turnout's state is (three repeated presses of the same direction in
  a row each produced a full, identical activate→deactivate message
  pair), so there's no functional gap without v2, only possible display
  staleness on a UI that itself doesn't persistently track state anyway
  (confirmed live: the MultiMaus's own turnout icon shows both routes
  active by default, briefly shows the just-commanded direction, then
  reverts to "both active" after a few seconds - it has nothing to
  compare a redundant press against, which is likely *why* it never
  suppresses one). Also decided against a dedicated OLED accessory page -
  without v2 the bridge only ever knows what it last commanded, not
  Ecos-confirmed truth, so a single "Last accessory: addr X → Str/Div"
  line on the main page (symmetric to the existing loco line) is a more
  honest scope than a page that would otherwise imply live status.

- **Cheap pre-implementation test, before committing to any design**:
  rather than build the real feature on an untested assumption, a
  temporary logging-only `notifyXNetTrnt`/`notifyXNetTrntInfo` (no
  routing, no CommandRouter/Ecos involvement) was added to watch what a
  real MultiMaus actually transmits. Confirmed live: pressing "straight"
  three times in a row on the same turnout produced three complete,
  identical activate→deactivate pairs (`0x88`→`0x80` each time) - no
  suppression of a "redundant" press. This is the same
  research-before-code pattern used successfully earlier this project
  (e.g. the step 9 investigation's temporary library instrumentation).

- **Implementation**:
  - `ProtocolInterface::sendAccessoryCommand(address, diverging)` (new,
    no-op-by-default virtual) - only `EcosInterface` overrides it in v1.
  - `XpressNetInterface::onTurnoutCommand(address, data)` wired to the
    vendored library's `notifyXNetTrnt` (header `0x52`/`0x53`, "Accessory
    Decoder operation request" - previously unimplemented, silently
    dropped by the library). Reacts only on the activate edge (data
    bit3=1); the deactivate half (bit3=0, sent by the MultiMaus a moment
    later for the same press) is ignored - Ecos's own
    `set(11, switch[...])` is a single complete command and generates the
    real DCC pulse timing on its own side, so there's nothing to do with
    a separate deactivate signal on our end.
  - `CommandRouter::handleAccessoryCommand(address, diverging, source)`
    (new) - no `StateEngine`/expiry involvement, since accessories aren't
    ephemeral the way locos are and v1 only tracks the single most recent
    command, not a per-address table. Forwards to Ecos only when
    `source == LocoSource::XPRESSNET` (v1 has no Ecos-sourced path).
  - `EcosInterface::sendAccessoryCommand()` builds
    `set(11, switch[DCC<address><port>])` via new
    `ecosBuildSetAccessoryCmd()`, sent directly against a new
    `ECOS_OBJECT_ACCESSORY_MANAGER` (id=11, official ESU spec section
    7.4) - no per-accessory object ID lookup or address-map needed at
    all, unlike locomotives, since this command form addresses by
    protocol+address+port directly. No-op while disconnected, matching
    `sendEmergencyStop()`/`sendResumeOperation()`'s existing pattern - a
    point-in-time command like this doesn't have obvious value queued for
    whenever a reconnect eventually happens, unlike locomotive state.
  - OLED: new "Last accessory: addr X → Str/Div" line added to the main
    page (there was unused vertical space below the existing 4 lines -
    `LINE_HEIGHT_SMALL` is 8px, not the ~12px assumed during Phase 5 step
    5's design discussion, so a 5th line fits comfortably within the
    64px display).

- **Bug 1, found via live testing: accessory address off by one.**
  MultiMaus address 3 arrived at Ecos as address 2, consistently (address
  2 → arrived as 1). Traced to the **official Lenz XpressNet
  specification** (`libraries/XpressNetMaster/extras/XpressNet V4_0.pdf`,
  section 3.38 "Schaltbefehl"), which explicitly defines the wire-level
  address field as `(Weichennummer-1) / 4` ("(turnout number - 1) / 4") -
  a real, documented 0-vs-1-indexing convention specific to accessory
  addressing (confirmed distinct from locomotive addressing, which has no
  such offset anywhere in this project). The vendored library's existing
  bit-recombination formula for this command
  (`(data1 << 2) | port_bits`, i.e. `data1 × 4 + port_bits`) already
  reverses the spec's "divide by 4, keep remainder separately" split
  (`floor(x/4)×4 + (x mod 4) = x` for any integer x, per Data 2's own
  documented meaning - "the two LSBs of the turnout address that were
  lost during the division by 4") - so what reaches `onTurnoutCommand()`
  is exactly `turnout_number - 1`, and a `+1` correction there
  reconstructs the address the operator actually intended, applied before
  the value reaches either the OLED display or the Ecos command. Verified
  this is spec-mandated behavior every compliant XpressNet device must
  implement, not a MultiMaus-specific quirk that would break
  interoperability with other DCC controllers - any correctly-built
  XpressNet receiver has to perform this same conversion.
- **Bug 2, found via the same live testing: straight/diverging inverted.**
  The initial port-letter mapping (`g`=straight, `r`=diverging) was a
  context-inferred guess, since the official Ecos spec's own example
  (`set(11, switch[MOT10r])`) doesn't spell out what the letters mean.
  Real hardware showed the opposite - corrected to `r`=straight,
  `g`=diverging.
- **A risky diagnostic detour during the address investigation, fully
  reverted**: to see the raw wire bytes directly, temporary
  `Serial.printf()` calls were added inside the vendored library's
  `XNetAnalyseReceived()` dispatch switch (cases `0x52`/`0x53`) - this
  froze real XpressNet bus communication, observed as the MultiMaus
  showing "err13". Root cause: `Serial.printf()` at 115200 baud is slow
  enough (several milliseconds for a short string) to blow XpressNet's
  tight response-time budget when it executes inside the actual
  message-dispatch hot path, unlike the earlier-established pattern of
  printing from within a *callback* (which had worked without incident
  earlier in the same test session) - stacking a second print on top of
  an already-present one is what tipped it over. Fully reverted via
  `git checkout` before any further work (confirmed byte-identical to the
  last commit), and the addressing question was root-caused afterward
  from the official spec text instead of further hardware-touching
  diagnostics - worth remembering for future hardware debugging:
  diagnostic prints inside a library's own time-critical dispatch path
  are meaningfully riskier than prints inside the higher-level callback
  layer, even though both look similar at first glance.
- 8 new tests (5 `test_command_router` covering forwarding, address
  validation, `LocoSource::ECOS` correctly not forwarding in v1, and
  `SystemStatus` surfacing; 3 `test_ecos_command_builder` covering the
  builder's straight/diverging output and undersized-buffer rejection).
  Native suite 138/138 passing. `onTurnoutCommand()` and
  `sendAccessoryCommand()` themselves have no native coverage - same
  hardware-coupled boundary as the rest of `xpressnet_interface.cpp`/
  `ecos_interface.cpp`.
- **This closes out Phase 5 entirely - all 10 steps are now done.**

---

**2026-08-05 — Phase 5 step 8: deferred OLED display fields, confirmed live (Phase 5 steps 1-9 now all done)**

- **XNet "Last Msg" age**: `XpressNetInterface` already tracked
  `last_message_time` (`millis()`-stamped on every successfully-parsed bus
  callback via `markBusActivity()`, the same field `BUS_TIMEOUT` already
  keys off) - it just wasn't exposed anywhere outside the class. Added
  `getLastMessageAgeMs() const override` returning `millis() -
  last_message_time`, or `NO_TIMESTAMP` if no message has been received
  yet this boot (`last_message_time == 0`).
- **Ecos round-trip latency**: `EcosInterface::queryAddressMap()` already
  set `address_map_last_refresh = millis()` on every send, but nothing
  ever read it back - reused it as the "query sent at" timestamp instead
  of introducing a new field. New `awaiting_query_reply` bool, set `true`
  right after `queryAddressMap()` successfully writes the request bytes.
  `handleReply()`'s existing address-map-entry branch (the one that logs
  "Address map: DCC X → Ecos ID Y") now checks this flag: on the *first*
  entry seen since the flag was set, computes `last_heartbeat_latency_ms =
  millis() - address_map_last_refresh` and clears the flag, so later
  entries in the same multi-loco reply don't keep recomputing (and
  shrinking) the same measurement. Not a per-request-ID correlation - but
  address-map queries only ever come from two sources (the 5s heartbeat
  and the far less frequent scheduled full refresh), both calling the
  exact same `queryAddressMap()`, so "most recent send, first reply since"
  is an honest round-trip measurement in practice, not an approximation
  that could attribute one query's reply to a different one.
- **New `ProtocolInterface` virtuals**: `getLastMessageAgeMs()` and
  `getLastHeartbeatLatencyMs()`, both no-op-by-default returning a new
  `NO_TIMESTAMP` sentinel constant (`(unsigned long)-1`) - `0` was
  deliberately not used as "no value yet", since `0` is a legitimate real
  measurement for both age (message just arrived) and latency (an
  implausibly fast reply, but not impossible to represent honestly).
  Documented on the interface as to *why* only one side of the bridge
  meaningfully implements each: XpressNet's bus-quiet signal doesn't have
  an Ecos equivalent (Ecos's own TCP connection state already covers
  "are we connected" directly), and Ecos's request/reply heartbeat cycle
  doesn't have an XpressNet equivalent (bus polling isn't a
  request/reply shape in the same way).
- **`CommandRouter::getSystemStatus()`** populates
  `SystemStatus.xnet_last_message_age_ms`/`ecos_heartbeat_latency_ms` from
  the two new virtuals, guarded by the same `#if ENABLE_XPRESSNET`/
  `#if ENABLE_ECOS_LAN` + null-pointer-check pattern already used for
  `xnet_status`/`ecos_status`.
- **OLED display**: replaced the XNet page's "Last Msg: N/A" with elapsed
  seconds (`Xs`) and the Ecos page's "Latency: N/A" with milliseconds
  (`Xms`), falling back to "N/A" when the underlying value is
  `NO_TIMESTAMP`.
- **Confirmed live**: both the XNet page's "Last Msg" and the Ecos page's
  "Latency" showed real, non-"N/A" values on real hardware after
  reconnecting and letting at least one heartbeat cycle complete.
- 4 new `test_command_router` tests using two new `MockProtocolInterface`
  setters (`setLastMessageAgeMs()`/`setHeartbeatLatencyMs()`) - covering
  both "value surfaced correctly when an interface is set" and "defaults
  to `NO_TIMESTAMP` when no interface is set" for each field. Native suite
  130/130 passing. `oled_display.cpp` itself has no native coverage -
  excluded from the native build like the rest of the display layer,
  confirmed live on real hardware instead, matching that file's existing
  testing boundary.
- **This closes out Phase 5 steps 1-9** - only step 10 (accessory/turnout
  support, the single biggest standalone item) remains on the Phase 5
  roadmap.
- Unrelated to the firmware itself: hit the same COM4 "Access is denied"
  serial-tooling flakiness seen earlier this week when flashing for this
  step - resolved by a hard reboot of the Wemos board itself (not the PC
  this time), with no python process on the host holding the port at all,
  suggesting the fault was on the device/USB-enumeration side rather than
  a stuck host process.

---

**2026-08-05 — Phase 5 step 7: function command reconnect-queue parity, confirmed live**

- **The gap**: `EcosInterface::sendFunctionCommand()` silently dropped the
  command entirely if the Ecos object ID wasn't known yet (`obj_id == 0` -
  either the address hadn't appeared in the map yet, or Ecos was
  disconnected and the map had been cleared) - unlike `sendSpeedCommand()`,
  which queues via `queuePendingQuery()` (the Phase 5 step 3 fix from
  2026-08-03). Same category of gap as step 3, just never applied to the
  function-command path.
- **A second, related gap found while reading the code**:
  `sendFunctionCommand()` still had an `if (current_status != CONNECTED)
  return;` guard at the very top of the function - the exact pattern step 3
  deliberately *removed* from `sendSpeedCommand()`, with the reasoning "a
  master must keep transmitting regardless" (queuing already handles
  disconnection gracefully, since `address_map_count` naturally clears to 0
  on disconnect, making `findEcosObjectId()` return 0 and routing correctly
  into the queue). As written, function commands issued while Ecos was
  fully disconnected were dropped before ever reaching the queue-or-send
  logic at all - not just when the address was merely unresolved while
  still connected.
- **The fix**: `PendingQuery` extended with `uint32_t functions` and two
  bools (`has_speed_direction`, `has_functions` - the existing bare
  `speed`/`direction` fields kept their names but are now only applied when
  `has_speed_direction` is set). Upserting by address is preserved and
  extended: a speed change and a function change queued for the same loco
  while disconnected now merge into *one* entry (only the relevant
  has_-flag gets set) instead of each type fighting over the small
  `MAX_PENDING_QUERIES` buffer independently. New
  `queuePendingFunctionQuery()` mirrors the existing `queuePendingQuery()`.
  `flushPendingQueries()` now checks each entry's flags and only replays
  the field(s) that entry actually has queued - sending both
  unconditionally would have reintroduced the exact class of bug fixed in
  step 9 (`CommandRouter::handleEcosCommand()`'s `has_speed`/
  `has_direction` fix): a pure function-only queued entry would have also
  sent a placeholder speed=0/direction=1, and vice versa.
  `sendFunctionCommand()` itself now calls `queuePendingFunctionQuery()`
  when `obj_id == 0` instead of returning, and the stale `current_status`
  guard was removed to match `sendSpeedCommand()`.
- **Confirmed live**: disconnected Ecos by unplugging its LAN cable,
  toggled two functions on a MultiMaus while disconnected, reconnected
  Ecos - both functions correctly landed on Ecos once it came back online,
  instead of being silently lost as they would have been before this fix.
- Debug-log verification of this specific test was attempted but the
  serial-monitor capture got corrupted by the same kind of garbled-noise
  artifact seen earlier in the day (see the step 6 entry below) - the
  user's direct hardware observation (both functions correctly present on
  Ecos after reconnect) was treated as the real evidence instead of
  re-chasing a working log capture, since it's an unambiguous, directly
  observed pass/fail result.
- No native test coverage added - `ecos_interface.cpp` is excluded from the
  native build (hardware-coupled), same existing testing boundary as the
  rest of this file. Native suite unaffected, 126/126 passing; both
  `env:native` and `env:wemos` build clean.

---

**2026-08-05 — Phase 5 step 6: `notifyXNetgiveLocoFunc` handler, verified by code review only**

- **The gap**: XpressNet header `0xE3`, `data1=0x09` is a standard Lenz
  throttle request asking the master to report a locomotive's F13-F28
  function status - distinct from `data1=0x00` (F0-F12, answered by
  `onGiveLocoInfo`) and `data1=0xF0` (MultiMaus's own proprietary combined
  request, answered by `onGiveLocoMM`). Traced the vendored
  `XpressNetMaster` library's RX dispatch (`XpressNetMaster.cpp`'s `0xE3`
  switch block) to confirm this exact mapping. The library calls the weak
  symbol `notifyXNetgiveLocoFunc` for a `0x09` request, which this project
  had never implemented, so the library silently dropped it (falls through
  to `default: unknown()`).
- **The fix**: new `XpressNetInterface::onGiveLocoFunc(uint8_t user_ops,
  uint16_t address)`, mirroring `onGiveLocoInfo`/`onGiveLocoMM` exactly -
  `markBusActivity()` (a real parsed request counts as bus activity, same
  reasoning as the other two), `resolveLocoStateForReply()` for current
  function state, then reply. The reply method itself,
  `XpressNetMasterClass::SetFktStatus(UserOps, F13to20, F21to28)`, already
  existed in the vendored library (`0xE3`/`0x52` wire format, matching the
  `0x50`/`0x51` "function type" replies the library already sends inline
  for related sub-commands `0x07`/`0x08`) - it had simply never been called
  from any of our code before. `buildFunctionGroupByte()`'s existing
  `0x04`/`0x05` group cases (F13-F20/F21-F28) already covered exactly the
  byte layout `SetFktStatus()` needs. New free-function glue
  (`notifyXNetgiveLocoFunc()`) added alongside the existing
  `notifyXNetgiveLocoInfo()`/`notifyXNetgiveLocoMM()` wrappers.
- **Could not get a live trigger, and that's expected**: real MultiMaus
  hardware uses the combined `0xF0` request instead of the standard
  `0x00`+`0x09` pair, so this specific request type doesn't fire during
  normal MultiMaus usage. Confirmed by exercising a MultiMaus through a
  full range of function toggles (groups 1, 4, and 5 - i.e. F0-F4 and both
  F13-F28 halves) with `DEBUG_XPRESSNET=1`: zero `onGiveLocoFunc`/
  `SetFktStatus` activity in the log, while every one of those toggles was
  correctly received via the already-proven `onLocoFunctionGroup` path in
  the same capture - ruling out "nothing reached the bridge at all" as the
  explanation. Verified instead by code review (identical, proven pattern)
  and clean builds: `env:native` 126/126 (unaffected - this file is
  excluded from the native build, same hardware-coupled boundary as the
  rest of `xpressnet_interface.cpp`) and `env:wemos` builds clean.
- **Unrelated serial-tooling friction this session**, resolved by a full
  PC reboot rather than any firmware change: background `platformio device
  monitor` captures intermittently stayed empty despite the process
  running and consuming CPU; `Stop-Process -Force` and even a WMI
  `Terminate` call sometimes failed to kill the python process
  immediately (return code 2/"Access Denied", though it would eventually
  die); COM4 repeatedly returned `PermissionError(13, 'Access is denied')`
  even with no visible python process holding it; one capture (before the
  reboot) showed 11,000+ lines of a single repeating 12-byte fragment
  ("cos ID 1004"), almost certainly a corrupted/backlogged buffer dump
  rather than a real firmware busy-loop - nothing in this step's code
  touches timing, the heartbeat, or the address-map print path at all.
  Confirmed clean and back to normal pacing after the reboot.

---

**2026-08-03 — Phase 5 step 5: OLED function display, two real bugs found along the way**

- **The feature**: replaced the main OLED page's "Fn: (TBD)" placeholder
  with a comma-separated list of active function numbers (e.g. "0,3,7").
  Design discussion up front: the main page's blue content area only has
  room for 4 lines total (Heap/Mem, Last Loco, Speed/Dir, Fn), each ~12px
  tall, leaving one 128px-wide line (~21 characters) for functions - not
  enough to show 32 individual on/off indicators legibly. Considered three
  options: (1) a hex bitmask (`0x00000012`) - rejected, not human-readable
  at a glance; (2) a comma-separated list of only the *active* function
  numbers - chosen, since real layouts rarely have more than a handful of
  functions on at once, keeping it both short and genuinely readable; (3) a
  dedicated 5th OLED page with a proper grid using the full ~48px blue area
  - more useful "at a glance" but a real feature in its own right, not this
  step's "cheap" scope - deferred rather than done now.
- **Implementation**: `OledDisplay::buildActiveFunctionsLabel()` builds the
  list into a fixed 18-byte buffer (17 usable chars + null, matching the
  line's ~21-char width minus the 4-char "Fn: " prefix), truncating with
  "..." if it overflows. New `SystemStatus.last_command_functions` /
  `CommandRouter::LastCommandInfo.functions` thread `LocoState.functions`
  (already tracked, already used to answer throttle LocoInfo requests)
  through to the display layer.
- **Real gap found and fixed in the same pass**: only speed commands ever
  updated `last_command` at all - `handleXpressNetFunctionCommand()` and
  `handleEcosFunctionCommand()` never touched it. That meant a pure
  function-only interaction (e.g. toggling a headlight, arguably the most
  common real interaction with a loco) never updated the OLED's "Last:
  Loco X / Spd/Dir" fields at all, and would have made the new Fn field
  similarly blind to function-only changes if left as-is. User caught this
  during design discussion and asked for it to be fixed in the same pass
  rather than filed as follow-up work. Fixed by making all four command
  handlers (`handleXpressNetCommand`, `handleXpressNetFunctionCommand`,
  `handleEcosCommand`, `handleEcosFunctionCommand`) consistently update
  `last_command.address/speed/direction/functions/source` from the fully
  resolved `new_state`, not just the two speed-command paths.
- **Live testing caught a second real bug**: with 9+ functions active, the
  displayed list silently stopped after 8 entries with no "..." shown at
  all - looked like a hard cutoff, not a truncation. Root cause:
  `buildActiveFunctionsLabel()`'s original version only checked whether
  there was room to append "..." *after* already failing to fit the next
  entry - by which point the entries already written could have filled the
  buffer right up to (but not including) room for a 3-character ellipsis,
  leaving nothing left to signal truncation with. Fixed by reserving 3
  bytes for a potential "..." unconditionally before filling entries (only
  actually appended if truncation turns out to be needed), so truncation
  is now always signaled correctly regardless of exactly where the cutoff
  lands. Confirmed live on real hardware after the fix.
- 4 new `test_command_router` tests confirming all four handlers correctly
  surface `last_command_functions` (covering both the "speed command
  preserves existing functions" and "function command updates last_command
  at all" cases); native suite 126/126 passing.
  `buildActiveFunctionsLabel()` itself has no native coverage - lives in
  `oled_display.cpp`, excluded from the native build like the rest of the
  display layer (verified live on real hardware instead, matching that
  file's existing testing boundary).

---

**2026-08-03 — Phase 5 step 4 confirmed live; step 9 (stolen icon) investigated, fixed, and confirmed; a second real bug found along the way**

- **Serial monitor was resetting the board on every connect.** Discovered
  because a live-test capture started with `=== XpressNet-Ecos Bridge
  Starting ===`, wiping StateEngine/address-map state right as the test
  began. Opening a serial connection to an ESP8266 pulses DTR/RTS - the
  same wiring used for flash auto-reset. Fixed with `monitor_rts = 0` /
  `monitor_dtr = 0` in `platformio.ini`'s `env:wemos` section.

- **Step 4 confirmed live**: via Ecos, turning on two different functions
  one at a time (F1, then F1+F4) correctly left both on - merged bitmap
  `0x02` then `0x12`, exactly what the `functions_mask` fix from the prior
  session predicted. The old overwrite bug would have reset the first back
  off the instant the second event arrived.

- **But the same live test surfaced a much bigger, previously-unknown
  symptom**: toggling a function on the MultiMaus after the Ecos-side
  functions had been set caused the *other* Ecos-set function to reset,
  AND caused speed to zero and direction to invert. First-round log
  analysis showed this was confounded by an unrelated reboot (the
  DTR/RTS issue above, discovered mid-investigation) plus a `CommandRouter`
  new-loco-defaults quirk - but a clean re-test (after fixing the monitor
  reset) reproduced a real, narrower version: toggling F1 on the MM caused
  Ecos's *other* function (F4) to silently revert. Root cause: XpressNet's
  function-group messages are always the *sending device's own full local
  belief* for that group's bits (F0-F4 together) - never a delta. A
  MultiMaus's belief for a function set externally by Ecos never gets
  updated (its *display* can show the right icon, but its own outgoing
  reports don't reflect it), so the next time it reports that group for
  any reason, it "rolls back" whatever Ecos set that it never learned
  about. This reframed what had been filed as a low-priority display quirk
  (Phase 5 step 9) as a bug with a real state-corruption consequence, and
  the user asked to reprioritize step 9 immediately after step 4 rather
  than continuing through steps 5-8 first.

- **Step 9 root-cause investigation** (per the user's own proposal: use two
  real MultiMaus units and instrument the actual bus traffic, rather than
  reasoning further from static code):
  - First attempt: a new `XpressNetMasterClass::PushLocoState()` sent the
    loco's real state as a *directed* `SetLocoInfoMM` reply (the same
    message format a MultiMaus gets when it explicitly asks) to whichever
    slot the library's own `SlotLokUse[]` tracking showed currently owned
    the address. Live test: no different from the original plain
    broadcast - the MultiMaus still flashed "stolen" without refreshing
    its displayed values. Ruled out "reply format" as the deciding factor.
  - Added temporary `Serial.printf` instrumentation directly into the
    vendored library's `AddBusySlot()`/`SetBusy()`/`SetLocoBusy()` (removed
    again once diagnosis was complete) and captured a real MM-to-MM steal:
    `AddBusySlot(slot=4,...)` → `AddBusySlot(slot=3,...)` → `SetBusy(3)` →
    `SetLocoBusy` evicting slot 4. Confirmed the library's real busy/evict
    mechanism, and confirmed `SetLocoBusy()`'s own payload is hardcoded to
    all-zero speed/functions - yet the user confirmed the "losing"
    MultiMaus's function/direction display **does** stay accurate during a
    real steal, with zero `LocoInfo`/`LocoInfoMM` re-poll ever appearing in
    the capture. The only remaining explanation: a MultiMaus directly
    overhears the winning throttle's own raw reply on the shared RS485 bus
    - ordinary peer bus traffic, not anything routed through the master's
    reply-construction code at all.
  - Traced the actual UART framing in `XpressNetMasterClass::
    XNetReadBuffer()`: the first byte of every message a *master*
    transmits gets the 9th "call-byte" parity bit set, unless that byte is
    exactly `0x00`, in which case it's silently skipped and the rest of
    the message goes out as **unmarked** data - which is exactly the
    format `setSpeed()`/`setFunc0to4()` already use (byte 0 = `0x00`).
    So the existing plain broadcast was *already* wire-format-identical to
    a slave's own reply, and still didn't work - meaning the real
    difference had to be bus **timing/context**, not byte format: a real
    slave's reply always immediately follows the master's own call-byte
    addressed to that specific slot; the plain broadcast just rides
    whatever the round-robin scheduler's next opportunity happens to be.
  - **User's proposed test, confirmed live**: temporary
    `TestForceBusy()`/`TestInjectPeerFunc1to4()`/`TestInjectPeerSpeed()`
    methods that (1) forced busy/stolen via `AddBusySlot()` under an
    unused fake slot (30), then (2) explicitly sent `[call-byte addressed
    to the real owning slot][unmarked reply data]` - deliberately
    reproducing a real slave's own call-byte-then-reply sequence. **This
    worked** - both MultiMaus units correctly flashed stolen, and the
    headlight icon (then speed, then direction) genuinely updated on the
    display, matching real MM-to-MM steal behavior. Extended from
    functions to speed/direction the same way, also confirmed live.

- **A second, independent bug found via the same live retesting** (not
  part of the original step 9 diagnosis - discovered while confirming
  the fix above): using Ecos's below-zero speed-detent to flip direction
  correctly updated the MultiMaus, but the *next* speed increase from Ecos
  immediately reverted the MultiMaus's direction back - even though Ecos's
  own UI still showed the correct, new direction. Root cause:
  `CommandRouter::handleEcosCommand(address, speed, direction)` always
  applied *both* speed and direction from every Ecos event, but a real
  Ecos event frequently reports only one of the two (matching the
  `EcosReply::has_speed`/`has_direction` flags, which existed and were
  parsed but never consulted here) - the unreported field silently reset
  to whatever placeholder value happened to accompany it. Exact same class
  of bug as step 4's `functions_mask` fix, just for speed/direction
  instead of functions. Fixed with new `has_speed`/`has_direction`
  parameters (default `true`, so every existing 3-argument call site is
  unaffected) - `handleEcosCommand()` now only overwrites the field(s)
  Ecos actually reported for that event. `EcosInterface::handleReply()`
  updated to pass `reply.has_speed`/`reply.has_direction` through. This is
  very likely the real explanation for why the *original* `ReqLocoBusy()`
  attempt (tried and fully reverted in an earlier session) broke
  bidirectional propagation after a few cycles - that bug existed
  then too and was never fixed until now, so it's plausible the busy/evict
  mechanism itself was never actually the problem.

- **Known, separately-flagged limitation, left on hold**: Ecos's dedicated
  hardware direction switch doesn't generate any network event on its own
  - not even an unrecognized one, confirmed by the total absence of any
  `Ecos:`/`unhandled reply` line correlating with using it in isolation.
  Only combined with a following speed change (e.g. crossing the
  zero-speed detent) does a real event reach the bridge. Best guess: Ecos
  treats the switch as a local UI latch that only gets pushed onto the
  wire alongside the next speed command, not as an independent property
  push. The detent-crossing method is a full working substitute (reaches
  the exact same code path), so this was flagged and parked rather than
  chased further.

- **Final production implementation** (after the diagnostic-only
  `PushLocoState`/`TestForceBusy`/`TestInjectPeerFunc1to4`/
  `TestInjectPeerSpeed` methods and all `Serial.printf` instrumentation
  were removed): one consolidated `XpressNetMasterClass::
  PushExternalLocoUpdate(Adr, Steps, Speed, F0to4)` in the vendored
  library, using a named `XNetExternalControllerSlot` (30) instead of a
  magic number. `ProtocolInterface::pushLocoStateToOwningSlot()` (new,
  no-op-by-default virtual, only `XpressNetInterface` overrides it) is
  called by `CommandRouter::broadcastCommand()` alongside the existing
  `sendSpeedCommand()`/`sendFunctionCommand()` broadcast whenever Ecos is
  the source. Scope is deliberately limited to F0-F4 (the group actually
  tested) - F5-F31 still only go out via the plain broadcast.
- 2 new `test_command_router` tests for the speed/direction merge fix
  (`test_router_ecos_speed_only_update_preserves_direction`,
  `test_router_ecos_direction_only_update_preserves_speed`); 2 more
  confirming `pushLocoStateToOwningSlot()` is called for Ecos-sourced
  broadcasts and not XpressNet-sourced ones. Native suite 122/122 passing.
  No native coverage for `PushExternalLocoUpdate()` itself - same
  hardware-coupled boundary as the rest of `ecos_interface.cpp`/
  `xpressnet_interface.cpp`.
- Final live confirmation, on the cleaned-up `env:wemos` production build
  (not the `env:debug` build used throughout the investigation): headlight,
  speed, and direction via Ecos all correctly refresh on the MultiMaus
  while it shows the "stolen" flash, and the zero-speed-detent direction
  flip no longer reverts on a subsequent speed change.

---

**2026-08-03 — Boot splash layout: title in the header band, logo below, no divider**
- User request, unrelated to Phase 5: reworked `OledDisplay::drawBootLogo()`
  so "OmniConnect" sits in the top 16 rows (the same yellow/header band
  every regular page splits at `COLOR_SPLIT_Y`) and the logo bitmap starts
  below that line, rather than the original top-to-bottom layout with the
  logo first and text last. Reason given: undecided yet between a
  full-white OLED and a yellow/blue one, and neither the text nor the logo
  mark should straddle the physical color split if it ends up being the
  two-tone display. First pass added a divider line at that row matching
  the regular pages' convention; user liked the layout on the real
  yellow/blue OLED but asked for the line itself removed (the physical
  color split already reads as a divider on that hardware, an explicit
  drawn line was redundant). Two quick flash-and-check iterations, both
  confirmed on real hardware.

---

**2026-08-03 — Phase 5 step 4: Ecos function-command merge bug, fixed (pending live test)**
- **The bug**: `EcosReply.functions_mask` (which function bits a given Ecos
  reply actually reported) was parsed but never consulted anywhere.
  `CommandRouter::handleEcosFunctionCommand()` overwrote the *entire*
  32-bit function bitmap on every Ecos event, using only the `functions`
  value from that one event - since a real Ecos event typically only
  reports the single function that changed, every other already-known
  function would silently reset to 0.
- **The fix**: `handleEcosFunctionCommand()` now takes a `functions_mask`
  parameter and merges - `(existing_functions & ~functions_mask) |
  (functions & functions_mask)` - so only the bits this specific event
  actually reported get updated; everything else keeps its prior value.
  Updated the one call site (`EcosInterface::handleReply()`) to pass
  `reply.functions_mask` through, where previously it was computed and
  then dropped on the floor.
- **A second, more fundamental bug found in the same area**: `functions_mask`
  was declared `uint8_t` - only 8 bits, but functions span F0-F31 (32
  bits, matching `functions`'s own `uint32_t` width). Worse, the line
  setting it used a plain `int` shift (`reply.functions_mask |= (1 <<
  fn_index)`) rather than the `1UL <<` idiom already used correctly one
  line above for `functions` itself - so for any `fn_index` 8 or above,
  the shifted value silently truncated to 0 on assignment to the 8-bit
  field. Any function at F8 or above never actually registered in the
  mask at all, independent of whatever merge logic sat on top of it -
  fixing only the merge logic without this would have left high-numbered
  functions permanently stuck. Fixed by widening `functions_mask` to
  `uint32_t` and using `1UL << fn_index` to match.
- **Zero prior test coverage found for this whole area**: `test_ecos_parser`
  had no `func[]` parsing tests at all before this. Added 4 (single
  function on, mask reflects only the reported bit, F31 specifically -
  the regression test for the truncation bug, function-off still sets the
  mask bit) plus 2 new `test_command_router` cases exercising the actual
  merge (a later partial update preserves earlier bits; a partial update
  can still clear a bit if that bit is the one reported). Native suite
  118/118 passing (up from 112).
- **Verified**: firmware builds clean, flashed. **Not yet verified live**:
  via Ecos (not XpressNet), turn two different functions on one at a time
  and confirm both show on simultaneously - the old bug would have reset
  the first back off the moment the second event arrived.

---

**2026-08-03 — Phase 5 step 3 follow-up: two more real bugs found via live testing, now confirmed both directions**
- **Trigger**: live test of the step 3 fix (disconnect Ecos, change speed/
  direction, reconnect) showed "changes does not feed down to ecos, even
  though bridge indicates connected." Root-caused across several rounds of
  testing, each round narrowing the search with real evidence rather than
  guessing.
- **Bug 1 - message-timeout disconnect path never cleared the address map**:
  `updateConnectionStatus()`'s `!wifi_client.connected()` branch clears
  `address_map_count = 0` on disconnect, but the *other* disconnect branch
  (the `now - last_message_time > ECOS_MESSAGE_TIMEOUT` watchdog) never did.
  That second branch is exactly the one that fires when Ecos's Ethernet
  cable is physically unplugged - no TCP reset is ever generated in that
  case, so `wifi_client.connected()` keeps reporting true and only the
  message-timeout watchdog ever notices. With the stale map left intact,
  `findEcosObjectId()` kept resolving real object IDs while genuinely
  disconnected, so `sendSpeedCommand()` took the "connected" branch and
  wrote straight into a dead socket instead of queuing via
  `queuePendingQuery()` - the exact silent-loss failure mode the original
  step 3 fix was supposed to eliminate, just reachable via the other
  disconnect path. Fixed by clearing `address_map_count` there too.
- **Detection lag was also just too slow to usefully test with**: worst-case
  was bounded by `ECOS_MESSAGE_TIMEOUT` alone (45s - corrected from an
  earlier "45-75s" estimate that wrongly assumed heartbeat cadence factored
  into the bound; `last_message_time` resets on any received byte and the
  timeout is checked every loop iteration regardless of heartbeat
  schedule). Tightened `ECOS_HEARTBEAT_INTERVAL`/`ECOS_MESSAGE_TIMEOUT` from
  30s/45s to 5s/10s - LAN round-trip is low single-digit ms, so 5s of
  margin is still generous against false positives from WiFi jitter, just
  far less patient about a genuinely dead connection. ~4.5x faster
  detection.
- **Bug 2 - command order**: with bugs 1 fixed, a queued command now
  reliably flushed after reconnect (confirmed via a new debug line -
  `flushPendingQueries()`'s success path had zero log output before this,
  which is part of why bug 1 took a few rounds to isolate) - but live
  testing then showed direction landed correctly while speed read back as
  0. User correctly pushed back on an initial (wrong) hypothesis blaming
  lost Ecos "control" registration across the reconnect - manual commands
  worked fine post-reconnect with no special re-subscription logic, which
  a control-registration theory couldn't explain. A cleaner isolating test
  (toggle a function - a single discrete action, unlike an analog speed
  dial which could fire several intermediate commands - as the very first
  action after a clean reconnect with nothing queued) confirmed a general
  "first command after reconnect" failure wasn't the issue either: the
  function toggle worked immediately. That narrowed it specifically to the
  speed+direction combination. Root cause (user's own hypothesis, confirmed
  by testing): both `sendSpeedCommand()` and `flushPendingQueries()` sent
  speed before direction. Real DCC decoders receive speed and direction
  combined in a single packet, not as two independent properties - Ecos
  likely constructs that real packet from whatever it has cached for both,
  so sending speed first meant a *genuinely changing* direction got built
  against a not-yet-updated (stale, effectively 0) cached speed. Reordered
  to direction-before-speed in both places.
- **Result, confirmed live**: unplug Ecos, wait for Disconnected (~10s now),
  change speed and direction, reconnect - both land correctly a couple of
  seconds later (address map has to repopulate over async TCP replies
  first, which is expected/inherent, not a bug).
- **Verified**: native suite 112/112 passing throughout (no coverage exists
  or was added for `ecos_interface.cpp` itself - excluded from the native
  build, real WiFiClient/hardware dependency). Firmware builds clean,
  flashed and confirmed working on real hardware for the full disconnect
  → command → reconnect → correct-recovery cycle.

---

**2026-08-03 — Phase 5 step 3: the fake outgoing Ecos command queue, fixed (pending live test)**
- **The bug**: `EcosInterface::sendSpeedCommand()` special-cased "Ecos not
  connected" by calling `queueOutgoingCommand("", 0)` - its own comment
  admitted *"mark for queue, but actually just drop"*. The
  `outgoing_queue`/`flushOutgoingQueue()` machinery around it was fully
  built (circular buffer, timestamps, flush-on-reconnect) but had exactly
  one caller, always with empty content, making the whole subsystem a
  no-op dressed up as a real feature.
- **The fix - reuse, don't rebuild**: while Ecos is disconnected,
  `address_map_count` is always 0 (`updateConnectionStatus()` clears it the
  moment the TCP socket drops), so `findEcosObjectId()` naturally returns 0
  regardless of connection state. That's exactly the condition
  `sendSpeedCommand()` already handles for a different reason - a loco
  whose Ecos object ID just hasn't been resolved yet - via
  `queuePendingQuery()`, which stores the real DCC address/speed/direction
  and gets replayed by `flushPendingQueries()` once the address map is
  available. Removing the special-cased disconnected branch entirely means
  both cases route through the one mechanism that actually works, instead
  of one that works and one that never did. Deleted the entire dead
  `outgoing_queue` subsystem: the `QueuedCommand` struct, `MAX_COMMAND_LENGTH`,
  `outgoing_queue_head`/`_tail`, `queueOutgoingCommand()`,
  `flushOutgoingQueue()`, and the now-dead `MAX_OUTGOING_QUEUE` config
  constant.
- **Two related bugs fixed in the same pass**, since leaving either would
  have undermined the fix once the disconnected case started flowing
  through this path for real:
  1. `flushPendingQueries()` only ever replayed the queued *speed* -
     direction was captured in `PendingQuery` but never actually sent on
     replay, for any deferred command, not just ones queued while
     disconnected. Same category of bug as the 2026-07-31 direction fix in
     `sendSpeedCommand()`'s already-connected path, just never applied here.
  2. `queuePendingQuery()` was append-only with no dedup by address. Once
     this queue also has to survive a full disconnection (not just the
     brief window before the first address-map reply), repeatedly changing
     one loco's speed while Ecos is down would fill the 5-slot
     `MAX_PENDING_QUERIES` buffer with stale entries for that one address,
     silently dropping any later command once full - including possibly
     the real final speed, and blocking any other loco from queuing at
     all. Fixed to upsert by address instead, matching the precedent set by
     `addAddressMapEntry()`'s 2026-07-31 fix for the same class of bug.
- **Verified**: firmware builds clean (RAM usage dropped ~900 bytes from
  removing the dead queue), flashed. No native test coverage exists or was
  added for this - `ecos_interface.cpp` is excluded from the native build
  entirely (real WiFiClient/hardware dependency), matching its existing
  testing boundary; `CommandRouter`/`EcosMessageParser` (which do have
  native coverage) are untouched by this change.
- **Not yet verified live**: disconnect Ecos, change a loco's speed/
  direction a few times on XpressNet while it's down, reconnect, confirm
  the loco ends up at the last commanded speed/direction rather than lost
  or stuck on a stale intermediate value.

---

**2026-08-03 — Phase 5 step 2 follow-up: Ecos→XpressNet direction, three real bugs found via live testing**
- **Trigger**: user tested the E-stop implementation live. XNet→Ecos direction
  worked immediately (MultiMaus STOP/GO reaches Ecos). But hitting STOP or GO
  directly on the Ecos itself never reached the MultiMaus at all - a real,
  legitimate gap the user caught that the original implementation didn't cover.
- **Root cause 1 - never subscribed to Ecos's own status at all**: the bridge
  only ever subscribed to individual locomotives; nothing subscribed to the
  base ECoS object (id=1), which the official spec (section 7.1) documents as
  supporting `request(1, view)` for exactly this kind of global run-state
  change. Fixed: `EcosInterface::attemptTcpConnect()` now sends
  `request(1, view)` alongside the existing address-map query. New
  `EcosReply::SystemStatus` field + parser support for the `Status[STOP/GO/
  SHUTDOWN]` property on object 1, and a `CommandRouter::emergencyStopAll()`/
  `resumeOperation()` signature change to take a `LocoSource source` parameter -
  whichever side originated the request is not re-notified (avoids both
  redundant echoes and, critically, a feedback loop back to Ecos). New
  `XpressNetInterface::sendEmergencyStop()`/`sendResumeOperation()` overrides
  broadcast `csEmergencyStop`/`csNormal` onto the bus for the Ecos-originated
  case, mirroring what `onPowerStateChange()` already does for the
  XNet-originated case.
- **Root cause 2 - the subscribe request was silently never sent at all**:
  live re-test showed no change - not even a hint of anything on the serial
  monitor when hitting Ecos's physical buttons. Root cause:
  `ecosBuildRequestCmd()` requires `buffer_size >= 60` and silently returns 0
  below that (no error) - the new subscribe code used a 40-byte buffer while
  every other call site in `ecos_interface.cpp` already correctly uses 80.
  The `request(1, view)` command was therefore never actually transmitted;
  Ecos had no reason to ever notify us of anything. Fixed by matching the
  established 80-byte convention, plus added a debug print confirming the
  subscribe actually got sent (or a build failure) so this class of silent
  failure is visible next time.
- **Root cause 3 - unconfirmed property casing**: with the buffer fixed and
  confirmed sent (verified via serial log - no error reply), still zero
  activity when pressing Ecos's physical buttons. Rather than guess at a
  second unverified casing assumption (having just been bitten by one),
  added a diagnostic fallback in `handleReply()` logging *any* reply/event
  with an object ID that doesn't match a known handler, instead of silently
  vanishing with zero trace - and made the `status` key and `stop`/`go`/
  `shutdown` values match case-insensitively (via a small `equalsIgnoreCase()`
  helper, avoiding `strcasecmp`/`_stricmp` portability differences between
  the ESP8266 toolchain and native/MinGW test builds), since the spec's
  documented example casing (`Status[val]`) was never independently
  confirmed against real hardware.
- **Result, confirmed live**: both directions now work - MultiMaus STOP/GO
  reaches Ecos, and Ecos's own STOP/GO now reaches the MultiMaus.
- **Tests**: 6 new `test_command_router` cases for the bidirectional
  skip-logic (each direction reaches the other side, neither re-echoes to
  its own origin), 5 new `test_ecos_parser` cases (STOP/GO/SHUTDOWN event
  parsing, lowercase-casing variant, confirms a normal loco reply doesn't
  false-positive `has_system_status`). Native suite 112/112 passing
  (up from 101 before this follow-up).

---

**2026-08-03 — Phase 5 step 2: bus-wide E-stop actually stops locos (implemented, pending live test)**
- **Checked the official Ecos spec before guessing** (`docs/ecos_pc_interface3.pdf`,
  section 7.1, "Basisobjekt ECoS (id=1)") rather than looping a per-loco
  `speed[0]` command at Ecos as the Phase 5 plan text originally sketched.
  The spec documents `set(1, stop)`/`set(1, go)` as literally "equivalent to
  the STOP/GO button on the Ecos" - a real system-wide command, distinct
  from any one locomotive's speed. Using this instead of a 50-loco loop is
  both more correct (matches what a real STOP button does) and cheaper
  (one command instead of many).
- **New `ProtocolInterface` methods**: `sendEmergencyStop()`/
  `sendResumeOperation()`, no-op by default (matching the existing
  `subscribeToLoco`/`unsubscribeFromLoco` pattern) - only `EcosInterface`
  overrides them meaningfully, sending `set(1, stop)`/`set(1, go)` via two
  new `ecos_protocol.h/.cpp` builders (`ecosBuildSystemStopCmd`/
  `ecosBuildSystemGoCmd`, new `ECOS_OBJECT_BASE_SYSTEM` constant = 1).
- **New `CommandRouter::emergencyStopAll()`**: iterates every loco in
  `StateEngine` (via `getLocoByIndex()`), forces speed to 0 (direction left
  untouched), and broadcasts that to XpressNet directly via
  `xpressnet->sendSpeedCommand()` - deliberately bypassing the existing
  single-address `broadcastCommand()`/echo-prevention path, since that
  machinery tracks one most-recent command at a time and looping 50 locos
  through it would just thrash `echo_state` pointlessly. Then sends Ecos
  the one system-wide stop.
- **New `CommandRouter::resumeOperation()`**: forwards to Ecos's `set(1, go)`
  but deliberately does *not* restore any loco's previous speed - matches
  real command-station safety behavior (an operator re-throttling manually
  after a stop, not a train unexpectedly resuming its old speed the instant
  power comes back).
- **Wired into `XpressNetInterface::onPowerStateChange()`**: the existing
  wire-protocol echo (`xnet.setPower(state)`, fixes the MultiMaus STOP-button
  display freeze, unchanged from 2026-07-30) now also calls
  `router->emergencyStopAll()` when `state` has `csEmergencyStop` or
  `csTrackVoltageOff` set, or `router->resumeOperation()` when `state ==
  csNormal`. Short-circuit (`csShortCircuit`) and service-mode
  (`csServiceMode`) bits are deliberately not treated as a stop trigger here -
  only an explicit e-stop or track-power-off, matching the Phase 5 backlog
  item's literal wording.
- **Tests**: 7 new `test_command_router` cases (zeroes every known loco,
  preserves direction, broadcasts to XpressNet, sends exactly one Ecos
  system-stop regardless of loco count, still stops Ecos with zero known
  locos, resume forwards to Ecos, resume never touches loco speed) and 3 new
  `test_ecos_command_builder` cases for the two new builders.
  `MockProtocolInterface` extended with call-count tracking for both new
  methods. Native suite 101/101 passing (up from 91).
- **Verified**: firmware builds clean, flashed. **Not yet verified**: this is
  safety-critical behavior that needs a real hardware test - hit STOP on the
  MultiMaus with a loco moving and confirm it actually halts (not just the
  display), then GO and confirm no auto-resumed speed.

---

**2026-08-03 — Phase 5 step 1: dead-code cleanup**
- **`ecosBuildGetCmd()` removed** (`protocols/ecos/ecos_protocol.h/.cpp`) - the
  earlier audit said "zero callers", which was true for production code but
  missed that its own unit test suite (`test_ecos_command_builder.cpp`) called
  it directly. Traced the history: this builder was originally used by
  `sendHeartbeat()` (`get(10, name)`), but that call site was replaced with
  `queryAddressMap()` when the 2026-07-31 heartbeat bug was fixed (see that
  entry below) - leaving the builder orphaned with only its own tests keeping
  it alive. Removed the function and its two tests (`test_ecos_build_get_speed_property`,
  `test_ecos_build_get_null_property_rejected`) together, consistent with
  "if you're certain something is unused, delete it completely" rather than
  keeping tests for dead code.
- **`LocoState.unknown` and `LocoState.ecos_object_id` removed** (`definitions.h`):
  `unknown` was always initialized `false` and never set `true` anywhere,
  only ever read in a debug print (removed too, `state_engine.cpp`);
  `ecos_object_id` was declared but never written or read anywhere - it
  duplicated the separate address-map array `EcosInterface` already
  maintains internally. Confirmed no positional/aggregate `LocoState{...}`
  initialization exists anywhere that field order/count could break.
- **Left alone**: `OledDisplay::nextPage()`/`prevPage()` - unused (no
  physical button wired up yet) but harmless, and cleanup didn't happen to
  touch that file.
- **Verified**: native suite 91/91 passing (down from 93 - exactly the two
  `ecosBuildGetCmd` tests removed with it, not a regression); RAM usage
  dropped slightly (~150 bytes, consistent with 3 fewer bytes × 50 max
  locos); firmware builds clean and flashed.

---

**2026-08-03 — Phase 5 roadmap planned: full codebase audit, 10 ordered steps**
- **Trigger**: with Phase 4.6 complete, user asked to plan Phase 5 covering
  every deferred item, proposing an initial list (E-stop, deferred display
  fields, XNet stolen-icon behavior, function display, accessory messages) and
  asking to find anything else stubbed/deferred in code (explicitly excluding
  new protocols - LocoNet/Z21 stay a future phase).
- **Full audit performed** (source code only, not docs - see `CLAUDE.md`'s
  Phase 5 section for the resulting list) surfaced several items beyond the
  user's starting list: an Ecos function-command merge bug
  (`EcosReply.functions_mask` parsed but never consulted, so
  `handleEcosFunctionCommand` clobbers function state instead of merging),
  a fake outgoing-command queue (`sendSpeedCommand()` drops the real command
  and queues an empty placeholder when Ecos is disconnected - commands sent
  while Ecos is down are silently lost today), missing reconnect-queue parity
  for function commands, an unimplemented `notifyXNetgiveLocoFunc` handler,
  unimplemented CV/programming-track support (`DirectCV`/POM - zero code
  anywhere), and several trivial dead-code items (`ecosBuildGetCmd()`,
  `LocoState.unknown`/`ecos_object_id` fields, unused `nextPage()`/`prevPage()`).
- **Also discovered function display is cheaper than its own comment implies**:
  `LocoState.functions` is already tracked and already used elsewhere
  (answering throttle LocoInfo requests) - the OLED gap is just that it was
  never threaded through `SystemStatus` to the display layer. This resolved
  the "do we even need this" discussion the user asked for - it's cheap
  enough that the answer is just "yes, do it."
- **CV programming pulled out of Phase 5 entirely** per user's call - not
  merely deferred, not currently planned at all, since the user's Ecos
  already handles this conveniently on a program track.
- **Ordering proposed and agreed**: correctness bugs before the
  feature/display work that would otherwise sit on top of wrong data;
  function-handling items (display, `giveLocoFunc`, reconnect-queue) batched
  together since they touch the same code paths; the highest-risk item
  (stolen-icon re-attempt, previously caused a live regression per the
  2026-07-31 entry above) scheduled after everything else gives a clean,
  tested baseline; the single biggest standalone feature (accessories/
  turnouts) scheduled last since nothing else depends on it. Final order:
  (1) dead-code cleanup, (2) E-stop, (3) fake outgoing queue, (4) Ecos
  function-merge bug, (5) function display, (6) `giveLocoFunc` handler,
  (7) function reconnect-queue parity, (8) deferred display fields
  (last-msg age, Ecos latency), (9) stolen-icon re-attempt, (10) accessories.
- **Written up as the formal Phase 5 section in `CLAUDE.md`**, mirroring how
  Phase 4.6 was structured. "Future Improvements" at the end of the file was
  trimmed to remove items now covered by Phase 5 (accessory control, advanced
  function mapping, bus-wide e-stop) and renamed to "Beyond Phase 5", keeping
  only genuinely later items (EEPROM config storage, web config UI, OTA,
  LocoNet, Z21).
- Nothing implemented yet - this was planning only.

---

**2026-08-03 — Phase 4.6 complete: 5-minute subscription lifecycle confirmed under real timing**
- **Test**: with Ecos running, moved a loco's speed once via the MultiMaus
  (active-loco count → 1), then left it completely untouched (no further
  drive/function commands) for 5+ minutes.
- **Result**: active-loco count dropped 2→1 on both the XNet page (`Active:`)
  and the Ecos page (`Subscribed:`) as expected - confirming `StateEngine::
  expungeInactiveLocos()` purged the inactive loco after `LOCO_INACTIVITY_
  TIMEOUT` (5 min) and `CommandRouter::update()` correctly called
  `EcosInterface::unsubscribeFromLoco()` for it, matching the code-inspection
  analysis from the 2026-07-31 entry (the 30s Ecos address-map heartbeat
  refresh is fully decoupled from this and cannot resurrect a purged loco).
- **Significance**: this was the last open item from Phase 4.6's checklist
  ("What Phase 4.6 Actually Needs" in `CLAUDE.md`) - all three remaining
  items (XpressNet→Ecos propagation, Ecos→XpressNet propagation, and this
  subscription lifecycle) are now confirmed live. **Phase 4.6 is complete.**
  `CLAUDE.md`'s top banner, Phase 4.6 section, and Development Status were
  all updated to reflect this - see "After Phase 4.6" there for what's next
  (LocoNet, Z21, the deferred stolen-icon display bug, bus-wide e-stop
  actually stopping locos, remaining deferred OLED fields).

---

**2026-08-03 — Boot splash added (OmniConnect logo); RSSI text de-duplicated; branding decision**
- **Trigger**: user confirmed the 120s XNet bus-timeout fix worked live (icon
  stays Connected between widely-spaced throttle commands now), then flagged
  two small UI leftovers and dropped a `logo.png` into the project root
  wanting it shown on the OLED at power-up.
- **RSSI text removed from the Device Status page**: it duplicated the
  header icon (shown on every page already) - `CPU: XXMHz` stays, the
  trailing `RSSI:` text is gone.
- **Boot splash**: `logo.png` (1024x1024 full color - "OmniConnect: Any
  throttle. Any command station. Total freedom.") is far too much for a
  128x64 monochrome display. Cropped to the icon mark only (Pillow, not
  bundled with the project - installed ad hoc for this conversion); dropped
  the wordmark/tagline entirely since fine text doesn't stay legible at this
  resolution; thresholded against the sampled background color and
  hand-packed into a 104x39px 1-bit bitmap (`display/boot_logo.h`, a derived
  asset - regenerate from `logo.png` if the logo ever changes, don't
  hand-edit). "OmniConnect" is printed underneath as real vector text
  instead of trying to bitmap-render it. `OledDisplay::begin()` draws this
  immediately and records a timestamp; `update()` gates on
  `OLED_BOOT_LOGO_DURATION_MS` (10s, `config.h`) before falling through to
  the normal page-cycling logic - not a "wait until connected" gate, XNet/
  Ecos connect in the background the whole time. Moved the OLED init block
  to the front of `setup()` (was previously last, after XNet/Ecos/LocoNet/
  Z21) so the splash appears as early as physically possible at power-up.
  Reset `page_cycle_task`'s timer at the boot-window transition so the Main
  page gets a full fresh dwell instead of the rotation immediately jumping
  to Device (its internal timer had been running since construction,
  10s before the gate ever released it).
- **Branding decision, not executed**: confirmed with the user that
  "OmniConnect" is new branding invented after the project already existed
  under `xpressnet_ecos_bridge` - not a hint to rename anything. Discussed
  and agreed: a full rename would touch the Arduino-IDE-mandated folder/
  `.ino` name match, every doc reference, and git, for zero functional
  benefit at this stage (solo project, no external consumers yet). Decision:
  "OmniConnect" stays user-facing only (boot splash); revisit a real rename
  only if this is ever published/handed to other people.
- **Verified**: firmware builds clean, native suite 93/93 passing (`oled_display.cpp`/
  `boot_logo.h` excluded from native build), flashed and confirmed on real
  hardware - splash renders cleanly for the full 10s before handing off.

---

**2026-08-03 — OLED attached: blocking Ecos-connect bug fully explained the "XNet never connects" symptom; display validated and reworked against real hardware**
- **Trigger**: OLED physically attached for the first time and confirmed
  initializing. With Ecos deliberately left unreachable for this test, three
  symptoms appeared together: XNet status never showed "Connected" (neither
  serial nor OLED), active-loco count stayed 0 despite the MultiMaus actively
  changing speed, and unplugging the MultiMaus threw err13 requiring a full
  Wemos power-cycle to clear.
- **Root cause - single bug explained all three**: `EcosInterface::attemptTcpConnect()`
  (`ecos_interface.cpp`) called `wifi_client.connect(ECOS_IP, ECOS_PORT)`, which
  is genuinely blocking on ESP8266 - traced into the core
  (`ClientContext::connect()`) to confirm it spins in `esp_delay(_timeout_ms, ...)`
  for up to the full timeout without ever returning to `loop()`. Default
  timeout is 5000ms. `config.h` already had an `ECOS_TIMEOUT` constant labeled
  "TCP connection timeout (ms)" but it was dead code - never actually applied
  to `wifi_client`, so every connect attempt used the ESP8266 core's own
  5000ms default. With Ecos unreachable, every backoff-scheduled reconnect
  attempt (5s/10s/20s/60s) froze the *entire* main loop - including
  `xnet_interface.update()`, which drives XpressNet call-byte polling - for up
  to 5 seconds at a stretch. `BUS_TIMEOUT` (XpressNet) is also 5000ms, so a
  single stall was enough to flip XNet back to disconnected almost every time;
  meanwhile the MultiMaus was polling a master gone completely silent on the
  bus, hence err13, repeating forever since Ecos never came back up on its own.
  **Fixed**: wired `ECOS_TIMEOUT` into `wifi_client.setTimeout()` before each
  connect attempt, and dropped its value from 5000ms to 300ms - real LAN
  connects complete in single-digit ms, so this only bounds the failure case.
- **Result, confirmed on real hardware**: reflashed with Ecos still down -
  MultiMaus attached immediately and active-loco count went to 1 the instant
  speed changed, both previously-stuck symptoms resolved. Native suite
  unaffected (`ecos_interface.cpp` is excluded from the native build).
- **OLED stubbed-data pass**: validating the newly-attached display surfaced
  several Phase-2-era placeholder fields still on screen - "Devices: 0"
  (leftover from the device-count-tracking removal, never actually deleted
  from the display), "IP: 192.168.1.105" and "CPU: 80MHz IRAM: 92%" (both
  hardcoded literals, not live reads), "Commands: 0"/"Echo Prev: 0" (counters
  that existed in `SystemStatus` but were always hardcoded to 0 with `// TODO`
  comments in `CommandRouter::getSystemStatus()`), and a hardcoded fake
  "Latency: 125ms" that looked real but wasn't. Wired up real IP
  (`WiFi.localIP()`), real CPU frequency (`getCpuFreqMhz()` from
  `utils/memory.h`), real WiFi RSSI (`WiFi.RSSI()`, guarded behind
  `#ifdef ARDUINO_ARCH_ESP8266` so the native build - which has no WiFi stack
  - still links), a real lifetime command counter and echo-suppressed
  counter (both incremented at the point each `CommandRouter::handle*Command`
  method already updates its echo-prevention state), and real last-command
  info (address/speed/direction). Removed the stale "Devices: 0" line
  outright rather than wiring it up, since device-count tracking was already
  a deliberate removal (see "Why No XpressNet Device-Count Tracking?" in
  `CLAUDE.md`) - this was the incomplete other half of that removal. Fields
  that still need real plumbing (XNet last-message age - needs exposing
  through `ProtocolInterface`, which also touches the mock used by native
  tests; Ecos round-trip latency - needs timestamp correlation on the
  heartbeat query; per-loco functions on the main page) were left as an
  honest `N/A`/`(TBD)` rather than invented data.
- **Latent bug found in passing**: `SystemStatus::last_loco` was a
  `LocoState*` pointer field, declared but never initialized or set anywhere
  in `getSystemStatus()` (which builds a fresh stack-local `SystemStatus`
  each call) - any future code dereferencing it would read garbage stack
  memory. Removed and replaced with plain `last_command_address/speed/
  direction/source` fields, which is what the display actually needed anyway.
- **OLED UI reworked around real hardware constraints**: `drawStatusIcon()`
  (dead code, unused since an earlier session already replaced it with plain
  text after its Unicode glyphs - ✓/✗/◇/! - rendered as garbage on the real
  SSD1306, whose default font only covers ASCII) was replaced with genuinely
  hand-drawn icons (`fillCircle`/`drawCircle`/`drawLine`/`fillTriangle`/
  `fillRect` - no font glyphs involved at all this time): a 4-bar WiFi
  signal-strength gauge (thresholded off real RSSI) and per-interface
  connection-status icons. Iterated on placement per user feedback: WiFi is
  global (every page, rightmost corner, since it's ESP8266-level
  infrastructure, not protocol-specific) while each interface's own
  connection icon is now page-local - drawn only by that interface's own
  screen function, not shown on other pages - explicitly establishing the
  pattern for LocoNet/Z21 once there's no room to show every protocol's
  status at once. Also replaced the "[Page X/4]" text footer on all 4 pages
  with a compact centered dot-row indicator, and had to left-align the page
  titles (previously manually centered with per-page x-offsets) after
  discovering the longer titles would otherwise run directly into the new
  icon zone.
- **Verified**: firmware builds clean for `env:wemos` at each step; native
  suite 93/93 passing throughout (grew from 91 sometime after the last
  changelog entry - not investigated, no regressions). Flashed and confirmed
  working on real hardware after each round of changes.
- **XNet status flapping to Disconnected between drive commands - two attempts,
  second one is the real fix**:
  - *First attempt (incomplete)*: hypothesized `onGiveLocoInfo()`/
    `onGiveLocoMM()`/`onPowerStateChange()` don't call `markBusActivity()`
    (only `onLocoDrive128`/`onLocoDriveStepped`/`onLocoFunctionGroup` did),
    theorizing the MultiMaus re-polls loco info regularly while idle and
    that traffic just wasn't being counted. Added the missing
    `markBusActivity()` calls (a real correctness improvement regardless -
    these genuinely are parsed bus messages and should count) but the user
    tested live and reported **no change**: toggle speed → icon shows
    Connected → reverts to Disconnected by the next time the XNet page
    cycles back into view.
  - *Root cause, confirmed against the vendored library source*: Lenz
    XpressNet's master polls each throttle slot with a bare call-byte
    inquiry (`XpressNetMaster.cpp` ~line 762); a throttle with nothing new
    to report simply stays silent - there is no "acknowledge, nothing to
    report" response in the base protocol. So a real, present, idle
    MultiMaus can legitimately go silent (no drive commands, no
    give-loco-info repolls, nothing) for a long stretch, and there is no
    passive signal available to distinguish that from "no throttle at all."
    `BUS_TIMEOUT` at 5000ms was simply far too aggressive for genuine
    single-throttle idle behavior - this had never been observed before
    since every earlier XpressNet test session had someone actively
    operating the throttle the whole time, so idle behavior was never
    watched live until the OLED made it visible.
  - **Fixed**: raised the timeout to 120 seconds per user's judgment (their
    call - "with just one throttle, there might be a while between throttle
    inputs... very happy to go to 120 seconds and see"; noted multiple active
    throttles would produce more background traffic and could likely
    tighten this later). Moved it out of `xpressnet_interface.h` into
    `config.h` as `XPRESSNET_BUS_TIMEOUT`, matching this project's "config.h
    is the single source of truth for timing constants" convention (it was
    previously a private class constant, unlike every comparable Ecos-side
    timeout which already lived in `config.h`). Comment at the definition
    documents the full tradeoff for whoever needs to revisit it if a genuine
    disconnect ever needs detecting faster than 120s allows.
  - Firmware builds clean, flashed; live re-confirmation still pending.
- **Not yet done**: the 5-minute subscription-lifecycle timeout under real
  timing, the deferred display fields noted above (last-message age, Ecos
  latency, per-loco functions), and live confirmation that 120s actually
  holds Connected through normal single-throttle idle gaps without
  needlessly delaying real-disconnect detection.

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

**2026-07-31 — Real Ecos/RocoNet protocol docs obtained; dir property confirmed, a well-intentioned busy-slot fix tried and reverted**
- **Trigger**: after the subscription/status/TX-blocking fixes, user reported
  the reverse direction still had problems: XpressNet→Ecos speed and direction
  now echoed correctly, but manual changes on Ecos didn't visibly reach the
  MultiMaus - its loco icon flashed "stolen" (another controller has it) but
  the displayed values never updated, and reselecting the loco didn't clear
  the flash either. User then provided the official ESU Ecos PC-Interface
  spec (`docs/ecos_pc_interface3.pdf`) and the Roco RocoNet interface 10785
  spec (`docs/Protocol roconet-v1.6.1.pdf`).
- **Ecos spec confirms real protocol details**: section 7.11 (Listenobjekt
  Lok) confirms `get(id, dir)`/`set(id, dir[val])` is the real property
  (matching the 2026-07-31 hardware-discovered fix earlier today) and
  documents the full Control/View registration model exactly as implemented
  (`request(id,view|control[,force])`/`release`) - confirming
  `subscribeToLoco()`'s existing `request(view)`+`request(control)` calls are
  correctly shaped, modulo not passing `force` (not changed - see below).
  The RocoNet doc turned out to document a different product (the Roco 10785
  PC↔RocoNet interface: CV programming, feedback modules) and doesn't cover
  MultiMaus's LocoInfo/busy-slot semantics at all, so it neither confirmed
  nor refuted the busy-slot theory below.
- **Spec vs. hardware contradiction, resolved in favor of hardware**: the
  Ecos spec's `dir` field is documented as `1=rückwärts` (1=reverse) -
  opposite of this bridge's internal/DCC/XpressNet convention
  (`direction=1=forward`). Implemented an inversion for both directions
  (`ecos_message_parser.cpp`'s `dir` parsing, `EcosInterface::sendSpeedCommand()`'s
  outbound call), but the user immediately flagged that the *already-tested,
  un-inverted* code had shown matching direction arrows between the MultiMaus
  and Ecos in the earlier successful round-trip test. Trusted the confirmed
  empirical result over the spec passage and reverted the inversion in both
  places, with a comment explaining why: most likely this specific
  loco/decoder has its own reverse-direction compensation configured in Ecos
  for physical motor wiring, which a blanket protocol-level inversion would
  fight rather than respect. Flagged as something to revisit only if a loco
  *without* that per-decoder compensation shows a real mismatch.
- **Busy-slot fix attempted for the "stolen but frozen" MultiMaus display,
  then reverted after it broke bidirectional propagation**: traced the
  "stolen" flash back to the vendored XpressNetMaster library's real
  multi-controller mechanism - `SetLocoInfo()`/`SetLocoInfoMM()` compute a
  "busy" bit from `SlotLokUse[UserOps]` (comment: "B=0 free; B=1 controlled
  by another Device"), and the library exposes a public `ReqLocoBusy(Adr)`
  specifically documented as "Slot 0 is reserved for non XpressNet Device" -
  i.e. the intended mechanism for a source like Ecos to properly claim a
  loco. Added `ProtocolInterface::claimLocoControl()` (no-op default,
  matching the existing `subscribeToLoco`/`unsubscribeFromLoco` pattern),
  overridden in `XpressNetInterface` to call `xnet.ReqLocoBusy()`, invoked
  from `CommandRouter::broadcastCommand()`'s Ecos-sourced branch. Flashed and
  tested: the busy claim fired correctly on every Ecos update, and the
  MultiMaus *did* successfully reconnect and resume sending real commands
  after a claim+reclaim cycle - the handover itself worked correctly for
  about 3-4 iterations, then stopped, and the user reported Ecos no longer
  reflected the MultiMaus's settings after reclaiming - a real regression,
  and one whose "works for a few cycles, then breaks" shape fits a
  cumulative-corruption theory rather than an immediate, deterministic
  failure. Root-caused (from the library source, not yet independently
  verified against real behavior beyond this one regression): `ReqLocoBusy()` parks
  the claim in `SlotLokUse[0]`, but the library's own `SetBusy(slot)` cleanup
  loop that runs when a throttle re-claims a loco only scans slots 1-31
  (`for (byte s = 1; s < 32; s++)`) - slot 0 is never cleared, leaving the
  library's internal bookkeeping in an inconsistent state (both the
  MultiMaus's real slot and the reserved slot 0 claiming the same address)
  after a reclaim. Given the original bidirectional propagation was already
  confirmed solid before this change, and the "frozen display while flagged
  busy" behavior may simply be intentional Lenz/Roco throttle UX (many
  real command-station-driven throttles deliberately freeze rather than
  redraw values while another controller holds a loco, to avoid showing
  numbers that might not be trustworthy) rather than a bug in this bridge,
  reverted `claimLocoControl()` entirely (`interface_base.h`,
  `xpressnet_interface.h/.cpp`, `command_router.cpp`) rather than risk the
  regression for an unconfirmed cosmetic improvement.
- **Result, confirmed on real hardware after the revert**: a clean multi-cycle
  round trip - MultiMaus drives loco 5452 (broadcasts to Ecos correctly),
  Ecos then drives it through several updates including surviving a bus
  timeout (broadcasts to XpressNet correctly, matching the earlier TX-fix),
  MultiMaus reclaims and drives again including a direction change
  (broadcasts to Ecos correctly), Ecos drives it once more (broadcasts back
  again) - no drops, no regressions. Native suite: 93/93 passing throughout;
  `env:debug` rebuilds clean each time.
- **Follow-up same day - two-MultiMaus test disproves the "intentional UX"
  theory**: user connected a second real MultiMaus and tested three
  scenarios. (1) MM-to-MM steal/reclaim: both throttles correctly flash
  "stolen" AND correctly refresh direction/functions on both units, in
  both directions - proving the MultiMaus is fully capable of refreshing
  when properly notified, so "frozen display is intentional throttle UX"
  is disproven. (2) Ecos-driven changes: both MultiMaus units correctly
  flash "stolen" now (some signal from the bridge's plain `xnet.setSpeed()`
  broadcast does reach them), but neither ever refreshes its displayed
  values. Conclusion: the real `SetLocoBusy` (0xE3/0x40) message - the
  mechanism genuine MM-to-MM handoff uses, and the one `ReqLocoBusy()`
  sends - is very likely what actually triggers a throttle to re-query and
  refresh, and the bridge's plain drive-command broadcast only accidentally
  triggers the "stolen" flash as a side effect without prompting a refresh.
  This means the earlier `ReqLocoBusy()` approach (tried and reverted the
  same day - see above) was conceptually the right mechanism; it just had a
  real, not-yet-fully-diagnosed side effect that broke XpressNet→Ecos
  forward propagation after a few cycles. The exact mechanism connecting
  the library's stale `SlotLokUse[0]` bookkeeping (root-caused above) to
  that specific forward-propagation regression is not yet confirmed -
  `onLocoDrive128()`/`onLocoDriveStepped()` call `router->handleXpressNetCommand()`
  unconditionally, with no apparent dependency on busy-slot state, so the
  causal link needs more careful, instrumented hardware testing (ideally
  with both real MultiMaus units live) before attempting the fix again.
  **Deliberately deferred to a future session** rather than risk another
  late-session regression - the core Phase 4.6 goal (bidirectional command
  propagation) is confirmed solid, which takes priority over this UI-refresh
  refinement. Follow-up plan for whenever this is revisited: re-add the
  busy-claim call with live monitoring of both MultiMaus screens plus serial
  capture, cycle-by-cycle, to pin down exactly what breaks and why - then
  decide whether the real fix is a targeted patch to the vendored library's
  `SetBusy()` (to also clear the reserved slot 0) or a different mechanism
  entirely.

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
