# XpressNet-Ecos Bridge - Architecture & Design Document

**Target Platform:** Wemos D1 Mini (ESP8266)
**Status:** Implemented and running on real hardware (firmware version in `xpressnet_ecos_bridge/version.h`)
**Document revision:** 2026-10-11 - rewritten from the original Phase 1 design to match the code as built and tested. Where the original design and reality diverged (pins, baud rate, Ecos wire format, persistence, Z21), this document follows the code. Dated history of how it got here is in [CHANGELOG.md](CHANGELOG.md); the day-to-day working summary is [../CLAUDE.md](../CLAUDE.md).

---

## 1. Purpose

Several model-railway control protocols have to control the same locomotives and accessories at once, with every device seeing every change:

- **XpressNet** (hardwired RS485) - Roco MultiMaus and other Lenz-style throttles
- **Z21 LAN** (UDP over WiFi) - Roco WLANmaus and Z21 apps
- **ESU Ecos** (TCP over WiFi) - the command station that actually drives the track
- **LocoNet** - planned (Phase 7); hardware is built, firmware not started

The bridge is the XpressNet *master*, a Z21 *server*, and an Ecos *client*. Ecos is the only required device and the source of truth; the bridge keeps no persistent loco state of its own.

### Design goals

1. **Modularity** - protocols are compile-time toggles in `config.h`; a disabled protocol compiles to nothing.
2. **Timing integrity** - XpressNet polling is time-critical and always runs first; nothing in the main loop blocks.
3. **One path for every command** - any throttle command goes to Ecos *and* every other throttle-facing protocol. No protocol owns a locomotive exclusively.
4. **Testable core** - routing, state and wire-protocol encoding are hardware-independent and unit-tested natively; only thin `*_interface.cpp` glue touches hardware.
5. **Readable** - fixed-size static allocation, small files, explicit comments for non-obvious protocol facts.

---

## 2. Architecture

### 2.1 Layers

```
 Application   StateEngine (loco state)     CommandRouter (routing, fan-out, echo attribution,
                                            accessory-state cache)
                       │                              │
 Protocol      XpressNetInterface   EcosInterface   Z21LanInterface   (LocoNetInterface: stub)
                       │                 │                │
 Transport     Gahtow XpressNetMaster   WiFiClient (TCP)   WiFiUDP
               SoftwareSerial 62500 baud   :15471             :21105
                       │
 Hardware      MAX485 (RS485)          ESP8266 WiFi

 Display       OledDisplay (SSD1306 128x64, I2C)      Persistence   EEPROM (eeprom_config/store)
 Setup Mode    AP + web form + OTA (setup_mode, setup_web_form)
```

### 2.2 Source layout

See [03_FILE_MANIFEST.md](03_FILE_MANIFEST.md) for the file-by-file list. In outline:

```
xpressnet_ecos_bridge/
├── xpressnet_ecos_bridge.ino      setup()/loop()
├── config.h  version.h  definitions.h
├── state_engine.*  command_router.*
├── eeprom_config.*  eeprom_store.*  setup_mode.*  setup_web_form.*
├── oled_display.cpp  display/
├── interfaces/interface_base.h
├── protocols/{xpressnet,ecos,z21lan,loconet}/
└── utils/
```

### 2.3 The `ProtocolInterface` contract

Every protocol implements `ProtocolInterface` (`interfaces/interface_base.h`):

| Method | Meaning |
|--------|---------|
| `begin()` / `update()` | Initialise; non-blocking per-loop work |
| `sendSpeedCommand(addr, speed, dir)` | Push a speed/direction change out to this protocol's devices |
| `sendFunctionCommand(addr, functions)` | Push a full F0-F31 bitmap |
| `sendAccessoryCommand(addr, diverging)` | Accessory/turnout command (default no-op) |
| `getStatus()` / `getName()` | `ComponentStatus` and display name |
| `subscribeToLoco()` / `unsubscribeFromLoco()` | Ecos-only; no-op elsewhere |
| `sendEmergencyStop()` / `sendResumeOperation()` | System-wide stop/go where the protocol has one |
| `pushLocoStateToOwningSlot()` | XpressNet-only: refresh a throttle's "stolen" display |
| `getLastMessageAgeMs()` / `getLastHeartbeatLatencyMs()` | Optional OLED values; `NO_TIMESTAMP` when unknown |

Interfaces call *into* the router (`router->handleXpressNetCommand()`, `handleEcosCommand()`, `handleZ21Command()` and their function/accessory siblings); the router calls *out* through the `send*` methods above. Interfaces are static objects created in the `.ino`; there is no dynamic allocation anywhere.

### 2.4 Main loop and timing

`loop()` order, highest priority first:

1. `xnet_interface.update()` - call-byte polling has a ~20-50 ms window
2. `ecos_interface.update()`, `z21_interface.update()` (and LocoNet when it exists)
3. Setup-button and Wi-Fi-fallback checks; periodic loco expiry (`LOCO_EXPIRY_CHECK_INTERVAL`)
4. OLED refresh (`OLED_UPDATE_INTERVAL`, 500 ms)
5. `yield()` for the ESP8266 Wi-Fi stack

If the device booted into **Setup Mode** (see 2.7) `loop()` only services the setup server; normal bridging and Setup Mode are mutually exclusive within one boot.

Rules that follow from this: no `delay()` in `update()` paths; no slow debug output (`Serial.printf`) in bus hot paths (it demonstrably breaks XpressNet timing); blocking network calls are bounded (the Ecos TCP connect timeout is 300 ms because the default 5 s froze XpressNet polling).

### 2.5 State engine

`StateEngine` holds up to `MAX_LOCOS` (50) `LocoState` entries: DCC address, speed 0-126, direction, F0-F31 bitmap, last source, last-update time, and whether the loco is subscribed on Ecos. Entries expire after `loco_inactivity_timeout` (default 5 minutes, EEPROM-configurable) with no activity from any source; expiry also unsubscribes from Ecos. Nothing is persisted - after a reboot the table rebuilds from live traffic and Ecos retains the real state.

### 2.6 Command routing and echo attribution

`CommandRouter` is the single hub.

- **Throttle-facing protocol → everyone else.** A command from XpressNet or Z21 updates `StateEngine` and fans out immediately to Ecos *and* to every other throttle-facing protocol in the same call. The other-throttle leg does not wait for Ecos to confirm (an earlier design that did was fragile and was replaced).
- **Ecos → throttles.** Ecos confirmations/events for subscribed locos fan out to XpressNet and Z21, except back to the protocol that most recently sent that same address (`wasRecentSource()`, window `ECOS_ECHO_ATTRIBUTION_WINDOW_MS` = 4 s). The router keeps a single "last command" attribution record (address, command type, source, timestamp) - not a command queue.
- **Partial updates are merged**, not overwritten: an Ecos event reporting only speed does not reset direction, and one reporting one function changes only that bit (`has_speed`/`has_direction`, `functions_mask`).
- **No incoming-side echo gate.** XpressNet's polled bus and Z21's UDP client/server have no physical echo path, so incoming commands are never dropped as "echoes".
- **Emergency stop / track power** is bidirectional: from a throttle, all known locos are zeroed and Ecos receives its single system-wide `stop`; from Ecos, throttles are told to stop. Resuming never restores previous speeds.
- **Accessories**: a throttle's turnout command goes to Ecos (`set(11, switch[...])`); Ecos accessory events go to XpressNet and Z21. The router caches the best-known position per address (`MAX_KNOWN_ACCESSORIES`) to answer on-demand state queries from throttles.
- **Stolen-icon refresh (XpressNet).** A plain broadcast makes a MultiMaus flash "stolen" without refreshing its display. For Ecos-sourced changes the router also calls `pushLocoStateToOwningSlot()`, which reproduces the call-byte-then-reply sequence a real throttle's own transmission has (F0-F12 and speed/direction; turnouts have the analogous `PushExternalTurnoutUpdate()`).

### 2.7 Persistence, Setup Mode and OTA

Settings that vary per installation live in EEPROM (`eeprom_config`/`eeprom_store`, magic + version + checksum): Wi-Fi SSID/password, Ecos IP, the bridge's own **static** IP/gateway/subnet (mandatory, no DHCP - Z21 clients cannot discover the bridge, so it must be at a known address), the XpressNet bus timeout and the loco inactivity timeout. `config.h` supplies only the seed defaults.

**Setup Mode** is a separate operating mode (open AP `XNetBridge-Setup`, web form on port 80). It is entered when the EEPROM is blank/invalid/incomplete, when the pushbutton on D7/GPIO13 is held 3 s, or after 5 minutes of continuous Wi-Fi loss. Reboot-to-apply is signalled through ESP8266 RTC user memory (one-shot, no flash write). The same server exposes `/update`, an OTA firmware upload using the standard `ESP8266HTTPUpdateServer`; a failed upload only affects the staging region and cannot damage the running image.

---

## 3. Protocol specifics

### 3.1 XpressNet (master)

Framing, checksums, call-byte arbitration and the 9th-bit handling are owned by Philipp Gahtow's **XpressNetMaster** library (vendored in `libraries/XpressNetMaster/`, with local additions for pushing external loco/turnout updates). `xpressnet_interface.cpp` only translates its callbacks to and from `CommandRouter`.

- **Wire**: 62500 baud, 8 data bits + parity bit used as the 9th "call byte" marker, 1 stop bit (`SWSERIAL_8S1`), half-duplex RS485 through a MAX485. This is the real Lenz XpressNet rate - not changeable.
- **Pins**: data on **D6/GPIO12** (SoftwareSerial RX and TX share the one pin; the MAX485's DI and RO are tied together), direction control on **D0/GPIO16** (DE and RE tied together, HIGH = transmit). This is the wiring of Gahtow's Z21 command station, and the library's ESP8266 example calls `setup(Loco128, D6, D0)`.
- **128 speed steps** are declared to throttles; 14/27/28-step inputs are rescaled to the internal 0-126 range.
- **Functions** arrive fragmented (F0-F4, F5-F8, F9-F12, F13-F20, F21-F28); each fragment is merged into the loco's full bitmap before routing.
- **Addresses**: short 1-99, long 100-9999. Accessory addresses on the wire are `turnout_number - 1`; the interface corrects this by +1.
- **Bus timeout**: with no received message for `xnet_bus_timeout` (default 120 s) status drops to DISCONNECTED. Lenz polling has no "nothing to report" reply, so an idle throttle is legitimately silent for long periods.
- A MultiMaus uses its own combined loco-info request (`0xF0`) rather than the standard `0x00`+`0x09` pair; both are answered.

### 3.2 Ecos LAN

Plain-text, line-based object protocol over TCP port **15471** (not XML - an early design assumption that was wrong). Replies/events are framed `<REPLY …>` / `<EVENT id>` … `<END code (text)>`. Authoritative reference: `docs/ecos_pc_interface3.pdf` (gitignored). Detailed design: [02_PHASE_3_2_ECOS_DESIGN.md](02_PHASE_3_2_ECOS_DESIGN.md).

- Locomotives: `queryObjects(10, addr, name)` maps DCC address ↔ Ecos object ID; `request(id, view)` subscribes; `set(id, speed[n])`, `set(id, dir[n])`, `set(id, func[n,v])` command. The direction property is `dir`. Direction is sent before speed (DCC decoders receive both in one packet).
- Accessories: `set(11, switch[DCC<addr><r|g>])` addressed directly at the accessory manager (r = straight, g = diverging); Ecos accessory objects are discovered with `queryObjects(11, addr)` and subscribed per object.
- System: object 1 is subscribed for stop/go; `set(1, stop)` / `set(1, go)`.
- Heartbeat: the address-map query (`queryObjects(10, addr, name)`) doubles as the keep-alive, sent every `ECOS_HEARTBEAT_INTERVAL` (5 s) on top of the full refresh every 10 minutes; no data for `ECOS_MESSAGE_TIMEOUT` (10 s) is treated as a dead connection (an unplugged cable generates no TCP reset).
- Reconnect backoff: 5 s, 10 s, 20 s, then 60 s.
- Commands issued while Ecos is down or a loco is not yet resolved are queued by address (upserted, `MAX_PENDING_QUERIES`) and replayed on reconnect.
- Function changes are diffed against what was last sent so only changed bits go out; baseline `get()` queries after a fresh subscription are paced to avoid starving the XpressNet loop.

### 3.3 Z21 LAN

UDP server on port **21105** emulating a Z21, implemented against the official *Z21 LAN Protocol Specification v1.13*. Pure encode/decode lives in `z21_protocol.*` (native-tested, including the spec's worked examples); `z21lan_interface.*` owns the socket and a fixed client table (`MAX_Z21_CLIENTS`, `MAX_Z21_SUBSCRIBED_PER_CLIENT`, 60 s client timeout). New clients default to broadcasts-on (a real WLANmaus never sends `SET_BROADCASTFLAGS`). Supported: drive, functions, track power, emergency stop, turnouts. CV programming is out of scope for the whole project (this is an operational interface). Real-hardware deviations from the spec text (direction bit, the turnout flags byte) are recorded in the CHANGELOG and resolved in favour of verified captures.

### 3.4 LocoNet (planned)

Hardware is on the motherboard PCB: TX on **D3/GPIO0**, RX on **GPIO3**, per Gahtow's D1 Mini design (internal pull-ups; USB serial and flashing are unavailable while LocoNet is active, so flash with the D1 removed or by OTA). Agreed design: real LocoNet slot select/steal/dispatch semantics are implemented entirely *inside* the LocoNet interface so Digitrax throttles behave normally, but never gate `CommandRouter` - every LocoNet command flows through like any other protocol, and changes from other protocols push a slot refresh to the LocoNet throttle holding that loco (the same pattern as XpressNet's slot bookkeeping). Not started; blocked on a test throttle.

---

## 4. Display

SSD1306 128x64 on I2C (**SDA D2/GPIO4, SCL D1/GPIO5**, address 0x3C). A 10 s boot splash shows while protocols connect in the background, then five pages: Main (status icons, last command incl. active functions, last accessory), Device (heap, uptime, IP), XpressNet (bus status, last-message age), Ecos (connection, heartbeat latency) and Z21 (client count, last action age and source IP). Setup Mode has its own screen showing the AP name and address. Status icons are hand-drawn bitmaps; Unicode glyphs render as garbage on this font.

---

## 5. Configuration (`config.h`)

`config.h` is the single compile-time file. Highlights (see the file itself for the authoritative values):

| Constant | Value | Meaning |
|----------|-------|---------|
| `ENABLE_XPRESSNET` / `ECOS_LAN` / `Z21_LAN` / `OLED_DISPLAY` | 1 | Feature toggles |
| `ENABLE_LOCONET` | 0 | Stub only |
| `XPRESSNET_DATA_PIN` / `XPRESSNET_CONTROL_PIN` | 12 / 16 | D6 / D0 |
| `XPRESSNET_BAUD` | 62500 | Lenz wire rate |
| `OLED_SDA_PIN` / `OLED_SCL_PIN` | 4 / 5 | D2 / D1 |
| `SETUP_BUTTON_PIN` | 13 | D7, to GND |
| `ECOS_PORT` / `Z21_PORT` | 15471 / 21105 | |
| `ECOS_TIMEOUT` | 300 ms | TCP connect bound |
| `ECOS_HEARTBEAT_INTERVAL` / `ECOS_MESSAGE_TIMEOUT` | 5 s / 10 s | Keep-alive and dead-link watchdog |
| `ECOS_ECHO_ATTRIBUTION_WINDOW_MS` | 4000 | Echo attribution |
| `MAX_LOCOS` | 50 | |
| EEPROM defaults | `ECOS_IP`, `XPRESSNET_BUS_TIMEOUT` (120 s), `LOCO_INACTIVITY_TIMEOUT` (5 min) | Seeded once; live values come from EEPROM |

---

## 6. Memory and dependencies

- No dynamic allocation; all tables are fixed-size statics. Current firmware: RAM ~59%, flash ~37% of the ESP8266 (v1.4.0 build, 2026-10-11).
- Dependencies: `Adafruit SSD1306` (+ `Adafruit GFX`), the ESP8266 core (`ESP8266WiFi`, `ESP8266WebServer`, `ESP8266HTTPUpdateServer`, `EEPROM`, built-in `SoftwareSerial`), and the vendored `XpressNetMaster`. Pinned in `platformio.ini` (`espressif8266@4.2.1`).
- Secrets (Wi-Fi credentials) live only in the gitignored `wifi_credentials.local.h`.

---

## 7. Testing

Native unit tests (Unity, `env:native`, 287 tests) cover `StateEngine`, `CommandRouter` (via a mock `ProtocolInterface`), the Ecos parser and command builders, Z21 packet encode/decode, EEPROM config logic and the setup-page generators. Time is injected via `now_ms()`/`mock_now_ms` so expiry and echo windows are deterministic. Hardware-coupled files (`*_interface.cpp` for XpressNet/Ecos/Z21, `eeprom_store.cpp`, `setup_mode.cpp`, the display) are excluded from the native build and verified on the real board - with raw serial/wire captures when behaviour is in doubt, and the spec or reference library as ground truth. See [04_PHASE_4_TESTING_INFRASTRUCTURE.md](04_PHASE_4_TESTING_INFRASTRUCTURE.md) and [../test/README.md](../test/README.md).

---

## 8. Extending

**Adding a protocol (e.g. LocoNet):**
1. Add `protocols/<name>/<name>_interface.h/.cpp` implementing `ProtocolInterface`.
2. Add its `ENABLE_*` toggle and constants to `config.h`.
3. Add a member/setter and `handle<Name>Command()` / `handle<Name>FunctionCommand()` to `CommandRouter`, plus its branches in `broadcastCommand()`.
4. Instantiate and register it in the `.ino` (`router.set<Name>Interface()`, `<name>.setCommandRouter(&router)`), call `update()` in `loop()`.
5. Keep pure encode/decode in a separate hardware-independent file so it can be unit-tested natively.
6. Add an OLED page and update the native build's `build_src_filter` for any hardware-only file.

**Known constraints:** single-core ESP8266 (Wi-Fi can pre-empt timing - the reason an ESP32 "v3" is a future option, see CLAUDE.md "MCU Platform"); ~100 KB usable heap; local network only; the XpressNet bus must be properly terminated.
