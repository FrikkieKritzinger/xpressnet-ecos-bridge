# Source File Manifest

Inventory of the files in the repository as they exist now. (This document used to be the hand-off list for the design-phase skeleton; it was rewritten on 2026-10-11 to match the real tree.)

Firmware sources live in `xpressnet_ecos_bridge/` (PlatformIO `src_dir`). "Native-tested" means the file is compiled into `env:native` and covered by the unit tests; the others are hardware-coupled and verified on the real board.

## Top level

| File | Purpose |
|------|---------|
| `README.md` | User-facing overview, pin map, build/flash steps |
| `CLAUDE.md` | Current architecture, conventions, config reference and status |
| `platformio.ini` | `env:wemos` (firmware), `env:native` (host unit tests), `env:debug` |
| `docs/` | Design docs, CHANGELOG, protocol specs (the two PDFs are gitignored - licensing) |
| `libraries/XpressNetMaster/` | Vendored Gahtow XpressNet master library, with local additions (`PushExternalLocoUpdate()`, `PushExternalTurnoutUpdate()`) |
| `test/` | Unity tests, mocks, fixtures, native stubs |

## `xpressnet_ecos_bridge/`

### Core

| File | Purpose | Native-tested |
|------|---------|:---:|
| `xpressnet_ecos_bridge.ino` | `setup()`/`loop()`, global objects, Setup Mode branch | no |
| `config.h` | Compile-time toggles, pins, timing constants (EEPROM defaults) | - |
| `version.h` | `FIRMWARE_VERSION` - single source of truth | - |
| `definitions.h` | `LocoSource`, `ComponentStatus`, `LocoState`, `CommandMessage`, `SystemStatus` | - |
| `state_engine.h/.cpp` | In-memory loco state (50 max), inactivity expiry | yes |
| `command_router.h/.cpp` | Bridging, fan-out, echo attribution, accessory-state cache | yes |
| `interfaces/interface_base.h` | `ProtocolInterface` / `DisplayInterface` abstract contracts | - |

### Persistence and Setup Mode

| File | Purpose | Native-tested |
|------|---------|:---:|
| `eeprom_config.h/.cpp` | Settings struct, defaults, checksum, validation (no `EEPROM.h`) | yes |
| `eeprom_store.h/.cpp` | The actual `EEPROM.h` load/save | no |
| `setup_web_form.h/.cpp` | Setup/update page HTML, field parsing and validation | yes |
| `setup_mode.h/.cpp` | AP + `ESP8266WebServer`, OTA (`/update`), setup button, Wi-Fi fallback, RTC-memory reboot flag | no |
| `wifi_credentials.local.h` | Real Wi-Fi SSID/password defaults - **gitignored**; template is `wifi_credentials.local.h.example` | - |

### Protocols

| File | Purpose | Native-tested |
|------|---------|:---:|
| `protocols/xpressnet/xpressnet_interface.h/.cpp` | Wraps `XpressNetMaster`; callbacks to/from `CommandRouter` | no |
| `protocols/ecos/ecos_interface.h/.cpp` | WiFi TCP client, address maps, subscriptions, queues | no |
| `protocols/ecos/ecos_message_parser.h/.cpp` | `<REPLY>`/`<EVENT>` framing and property parsing | yes |
| `protocols/ecos/ecos_protocol.h/.cpp` | Outgoing Ecos command builders | yes |
| `protocols/z21lan/z21lan_interface.h/.cpp` | UDP socket, client session table | no |
| `protocols/z21lan/z21_protocol.h/.cpp` | Z21 packet encode/decode | yes |
| `protocols/loconet/loconet_interface.h` | Stub only (`ENABLE_LOCONET` is 0) | - |

### Display and utilities

| File | Purpose |
|------|---------|
| `oled_display.cpp`, `display/oled_display.h` | SSD1306 driver, five status pages, boot splash, Setup Mode screen |
| `display/boot_logo.h` | Boot splash bitmap |
| `utils/timing.h` | Non-blocking `TimedTask` |
| `utils/now_ms.h` | Time source, replaceable in tests |
| `utils/debug.h` | `DEBUG_*_PRINTF` macros (compiled out when disabled) |
| `utils/memory.h` | ESP8266 heap helpers |
| `test_support/` | `arduino_stub.cpp`, `mock_now_ms.cpp` - native builds only, excluded from firmware |

## `test/`

| Path | Purpose |
|------|---------|
| `test_state_engine/`, `test_command_router/` | Core logic (router tests use mock interfaces) |
| `test_ecos_parser/`, `test_ecos_command_builder/` | Ecos wire protocol |
| `test_z21_protocol/` | Z21 packet encode/decode, byte-for-byte against the spec |
| `test_eeprom_config/`, `test_setup_web_form/` | Persisted config and setup pages |
| `mocks/`, `fixtures/`, `native_stubs/` | Shared test support (`<Arduino.h>` stand-in, mock protocol interface, Ecos replies) |

Running them: `python -m platformio test -e native` (287 tests).

## Historical documents in `docs/`

| File | Status |
|------|--------|
| `01_DESIGN_DOCUMENT.md` | Living architecture/design document |
| `02_PHASE_3_2_ECOS_DESIGN.md` | Ecos interface design, with a current-state banner |
| `04_PHASE_4_TESTING_INFRASTRUCTURE.md` | Testing plan; outcome noted at the top |
| `02_GITHUB_SETUP.md`, `04_COMPILATION_FIX.md`, `05_COMPILATION_FIX_COMPLETE.md` | Historical, from the original skeleton phase; layout and paths no longer apply |
| `CHANGELOG.md` | Dated history, newest first |
