# OmniConnect (XpressNet-Ecos Bridge)

A protocol bridge on a Wemos D1 Mini (ESP8266) that lets XpressNet throttles (Roco MultiMaus), Z21 LAN throttles (Roco WLANmaus / Z21 apps), LocoNet throttles (Digitrax UT6) and an ESU Ecos command station control the same locomotives and accessories, with every change propagated to all of them.

**Status:** Working on real hardware (Phases 1-6 complete, Phase 7 in progress; LocoNet released).
**Target Platform:** ESP8266 (Wemos D1 Mini)
**Firmware version:** see `xpressnet_ecos_bridge/version.h` (currently 1.5.4)

See [CLAUDE.md](CLAUDE.md) for current architecture and status, and [docs/CHANGELOG.md](docs/CHANGELOG.md) for the dated development history.

## What it does

- **XpressNet** (hardwired RS485, timing-critical) - acts as the bus *master* for MultiMaus-style throttles, built on Philipp Gahtow's XpressNetMaster library. Speed, direction, F0-F28, track power / emergency stop and turnouts.
- **Ecos LAN** (WiFi, TCP port 15471) - ESU's plain-text object protocol (not XML). Speed, direction, functions, emergency stop and accessories, in both directions.
- **Z21 LAN** (WiFi, UDP port 21105) - emulates a Z21 so a WLANmaus or Z21 app can connect straight to the bridge.
- **LocoNet** (hardware on the motherboard PCB, receive on UART0, transmit via SoftwareSerial) - acts as a slot server for Digitrax throttles such as the UT6: speed, direction, F0-F28, track power / stop and turnouts. Slot ownership is internal and never blocks other protocols.
- **State engine** - in-memory tracking of up to 50 active locomotives; a loco is dropped (and unsubscribed from Ecos) after 5 minutes of inactivity.
- **OLED display** (128x64 SSD1306) - six status pages (main, device, XpressNet, Ecos, Z21, LocoNet) plus a boot splash.
- **Setup Mode** - a Wi-Fi access point and web page to edit the persisted settings and to upload new firmware over the air.

Design premise: any command from any throttle protocol is forwarded to Ecos *and* to every other throttle-facing protocol; Ecos is the only required, known device. There is no exclusive ownership of a locomotive between protocols.

## Hardware

- **MCU:** Wemos D1 Mini (ESP8266, 80/160 MHz single core, 4 MB flash, 160 KB RAM). ESP32 is a possible future "v3", not a supported target (see CLAUDE.md, "MCU Platform").
- **XpressNet:** MAX485 RS485 module, wired the way Gahtow's Z21 command station does it - a *single* half-duplex data pin (DI and RO tied together) and a *single* direction pin (DE and RE tied together).
- **Display:** SSD1306 128x64 I2C OLED.
- **Setup button:** momentary pushbutton to GND.

### Pin map (as implemented in `config.h`)

| Function | D1 Mini pin | GPIO | Notes |
|----------|-------------|------|-------|
| XpressNet data (SoftwareSerial RX+TX) | D6 | 12 | 62500 baud, 8 data bits + parity used as the 9th (call-byte) bit |
| XpressNet MAX485 DE+RE | D0 | 16 | HIGH = transmit, LOW = receive |
| OLED SDA | D2 | 4 | I2C, address 0x3C |
| OLED SCL | D1 | 5 | I2C |
| Setup Mode button | D7 | 13 | to GND, internal pull-up; hold 3 s |
| LocoNet TX / RX | D3 / RX | 0 / 3 | TX through a BC547 (inverted), RX on hardware UART0 16660 baud, per Gahtow's D1 Mini design |

The LocoNet pins share boot-strap and USB-serial lines: UART0 belongs to LocoNet, so USB flashing and serial debug are unavailable while the D1 is seated. Flash with the D1 removed, or over the network with the `wemos_dev` environment and `tools/devflash.py`; read debug text from the TCP log on port 23 (`tools/netlog.py`).

## Getting started

### Prerequisites

- [PlatformIO](https://platformio.org/) (`pip install platformio`)
- Libraries are resolved by PlatformIO (`Adafruit SSD1306`); Gahtow's XpressNetMaster is vendored in `libraries/XpressNetMaster/` (with local modifications - see CLAUDE.md).

### Build and flash

1. Copy `xpressnet_ecos_bridge/wifi_credentials.local.h.example` to `xpressnet_ecos_bridge/wifi_credentials.local.h` and fill in your Wi-Fi SSID and password. This file is gitignored; never commit it.
2. Review `xpressnet_ecos_bridge/config.h` (the Ecos IP default and feature toggles live there).
3. Build and flash:
   ```bash
   python -m platformio run -e wemos
   python -m platformio run -e wemos -t upload
   ```
4. Serial monitor at 115200 baud (`monitor_rts`/`monitor_dtr` are disabled so opening the monitor does not reset the board).

### First boot and configuration

The settings that vary per installation live in EEPROM, not in the firmware: Wi-Fi SSID/password, Ecos IP, the bridge's own **static** IP / gateway / subnet (mandatory - there is no DHCP), the XpressNet bus timeout and the loco inactivity timeout. `config.h` only supplies defaults used to seed the EEPROM.

On first boot (or whenever the config is incomplete) the bridge starts **Setup Mode**: join the open `OmniConnect-Setup` Wi-Fi network and browse to the page it shows on the OLED. You can also enter Setup Mode by holding the pushbutton for 3 seconds, or automatically after 5 minutes of continuous Wi-Fi loss. The same Setup Mode serves `/update`, a firmware upload page (OTA).

### Tests

Unit tests run natively on the host (no hardware). From the repository root, with a MinGW/GCC toolchain on PATH:

```bash
python -m platformio test -e native
```

408 tests currently pass. See [test/README.md](test/README.md).

## Repository structure

```
.
├── README.md, CLAUDE.md, platformio.ini
├── docs/                       Design docs, protocol specs, CHANGELOG.md
├── libraries/XpressNetMaster/  Vendored Gahtow XpressNet master library
├── test/                       Native unit tests (Unity) + mocks/fixtures
└── xpressnet_ecos_bridge/      Firmware sources (PlatformIO src_dir)
    ├── xpressnet_ecos_bridge.ino   setup()/loop()
    ├── config.h, version.h, definitions.h
    ├── state_engine.*, command_router.*
    ├── eeprom_config.*, eeprom_store.*     Persisted settings
    ├── setup_mode.*, setup_web_form.*      Setup Mode AP/web UI/OTA
    ├── oled_display.cpp, display/
    ├── interfaces/interface_base.h         ProtocolInterface contract
    ├── protocols/{xpressnet,ecos,z21lan,loconet}/
    └── utils/
```

A file-by-file list is in [docs/03_FILE_MANIFEST.md](docs/03_FILE_MANIFEST.md).

## Documentation

- [CLAUDE.md](CLAUDE.md) - current architecture, conventions, configuration and status
- [docs/CHANGELOG.md](docs/CHANGELOG.md) - dated history and diagnoses
- [docs/user/](docs/user/) - user documentation sources (Quick Start Guide, User Manual); build the Word files with `python tools/build_docs.py`
- [docs/01_DESIGN_DOCUMENT.md](docs/01_DESIGN_DOCUMENT.md) - architecture and design
- [docs/02_PHASE_3_2_ECOS_DESIGN.md](docs/02_PHASE_3_2_ECOS_DESIGN.md) - Ecos interface design
- [docs/03_FILE_MANIFEST.md](docs/03_FILE_MANIFEST.md) - source file inventory
- [docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md](docs/04_PHASE_4_TESTING_INFRASTRUCTURE.md) - test infrastructure plan and outcome

## Attribution

Inspired by and built on the work of Philipp Gahtow:
- [Z21 Command Station](https://pgahtow.de/w/Zentrale_Z21PG/en) - architecture patterns and wiring
- [XpressNet](https://pgahtow.de/w/XpressNet) and the XpressNetMaster library
- [LocoNet](https://pgahtow.de/w/Loconet) write-up and hardware designs

## License

See [LICENSE](LICENSE).
