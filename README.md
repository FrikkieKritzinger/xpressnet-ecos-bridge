# XpressNet-Ecos Bridge

A modular protocol bridge connecting XpressNet model railway control systems to ESU Ecos via a Wemos D1 Mini (ESP8266), with extensibility for LocoNet and Z21 LAN protocols.

**Status:** Pre-Implementation (Architecture Phase Complete)  
**Target Platform:** ESP8266 (Wemos D1 Mini)  
**Version:** 0.1-dev

## Quick Overview

This project bridges model railway protocols:
- **XpressNet** (hardwired, timing-critical) → Receives throttle commands from XpressNet devices
- **Ecos LAN** (WiFi, XML-based) → Sends/receives commands from ESU Ecos control system
- **State Engine** (in-memory) → Tracks controlled locomotives and their state
- **Display** (OLED 128x64) → Shows real-time status

**Future:** LocoNet and Z21 LAN protocol support (modular add-ons)

## Philosophy

Inspired by Philipp Gahtow's Z21 command station architecture but with a focused, learning-oriented approach:
- ✅ Modular (compile-time toggles via config.h)
- ✅ Clear separation of concerns
- ✅ Readable code (learning > optimization)
- ✅ Ecos-specific (not trying to be everything)
- ✅ Timing-aware (XpressNet is priority)

## Repository Structure

```
xpressnet-ecos-bridge/
├── README.md                          # This file
├── docs/
│   ├── 01_DESIGN_DOCUMENT.md         # Complete architecture design
│   ├── 02_PROTOCOL_XPRESSNET.md      # XpressNet protocol reference
│   ├── 02_PROTOCOL_ECOS.md           # Ecos LAN protocol reference
│   ├── 03_HARDWARE_WIRING.md         # Hardware connections
│   └── GLOSSARY.md                   # Terminology
│
├── src/
│   ├── config.h                      # ← User configuration (God config)
│   ├── main.ino                      # Arduino entry point
│   ├── definitions.h                 # Global types & constants
│   ├── state_engine.h
│   ├── state_engine.cpp
│   ├── command_router.h
│   ├── command_router.cpp
│   │
│   ├── interfaces/
│   │   └── interface_base.h          # Abstract base classes
│   │
│   ├── protocols/
│   │   ├── xpressnet/
│   │   │   ├── xpressnet_interface.h
│   │   │   └── xpressnet_interface.cpp
│   │   ├── ecos/
│   │   │   ├── ecos_interface.h
│   │   │   └── ecos_interface.cpp
│   │   ├── loconet/                  # (Future - skeleton only)
│   │   │   ├── loconet_interface.h
│   │   │   └── loconet_interface.cpp
│   │   └── z21lan/                   # (Future - skeleton only)
│   │       ├── z21lan_interface.h
│   │       └── z21lan_interface.cpp
│   │
│   ├── display/
│   │   ├── display_base.h
│   │   ├── oled_display.h
│   │   └── oled_display.cpp
│   │
│   └── utils/
│       ├── timing.h                  # Non-blocking timing utilities
│       ├── debug.h                   # Debug logging
│       └── memory.h                  # Memory utilities (ESP8266 specific)
│
├── libraries/
│   └── README.md                     # External library instructions
│
├── tests/
│   ├── test_state_engine.cpp         # Unit tests (future)
│   └── test_echo_prevention.cpp      # Functional tests (future)
│
└── .gitignore                        # Git ignore rules
```

## Hardware Requirements

### Microcontroller
- **Wemos D1 Mini** (ESP8266)
  - 160 MHz single-core
  - 4MB Flash / 160KB RAM
  - WiFi 802.11 b/g/n

### Interfaces
- **XpressNet Interface:** MAX485 RS485 module (TTL conversion)
- **Ecos Connection:** WiFi (built-in to ESP8266)
- **Display:** SSD1306 I2C OLED (0.96", 128x64)

### Pin Configuration
See `docs/03_HARDWARE_WIRING.md` for detailed connections.

Quick reference:
```
XpressNet (RS485):
  RX → GPIO13 (D7)
  TX → GPIO15 (D8)
  DE → GPIO14 (D5)
  RE → GPIO12 (D6)

I2C OLED Display:
  SDA → GPIO4 (D2)
  SCL → GPIO5 (D1)
```

## Getting Started

### Prerequisites
- Arduino IDE 1.8.0 or later
- ESP8266 board support for Arduino
- Required libraries:
  - Adafruit_SSD1306
  - Adafruit_GFX
  - XpressNetMaster (Gahtow's library)

### Installation Steps

1. **Clone or download this repository**
   ```bash
   git clone https://github.com/yourname/xpressnet-ecos-bridge.git
   cd xpressnet-ecos-bridge
   ```

2. **Set up Arduino IDE for ESP8266**
   - Add board URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Install ESP8266 boards
   - Select: Tools → Board → LOLIN(WEMOS) D1 mini (ESP8266)

3. **Install required libraries**
   - Sketch → Include Library → Manage Libraries
   - Search and install:
     - Adafruit_SSD1306
     - Adafruit_GFX
   - Download XpressNetMaster from Gahtow's sourceforge (see docs/)

4. **Configure your hardware**
   - Edit `src/config.h`
   - Set your Ecos IP address
   - Adjust pin assignments if different

5. **Upload to Wemos D1 Mini**
   - Plug in via USB
   - Select Tools → Port → (your device)
   - Click Upload

6. **Test and verify**
   - Open Serial Monitor (115200 baud)
   - Watch startup messages
   - Connect XpressNet device
   - Verify Ecos connection

## Configuration

All configuration is in **`src/config.h`** (the "God" config file):

```cpp
// Enable/disable protocols
#define ENABLE_XPRESSNET    1
#define ENABLE_ECOS_LAN     1
#define ENABLE_LOCONET      0
#define ENABLE_Z21_LAN      0
#define ENABLE_OLED_DISPLAY 1

// Hardware pins
#define XPRESSNET_RX_PIN    13
#define XPRESSNET_TX_PIN    15
#define XPRESSNET_DE_PIN    14
#define XPRESSNET_RE_PIN    12

// Ecos configuration
#define ECOS_IP             "192.168.1.100"
#define ECOS_PORT           15471

// Display configuration
#define OLED_SDA_PIN        4
#define OLED_SCL_PIN        5
#define OLED_WIDTH          128
#define OLED_HEIGHT         64
```

## Development Status

### Phase 1: Architecture ✅ COMPLETE
- Design document created
- Architecture approved
- Hardware specifications finalized

### Phase 2: Skeleton Code (IN PROGRESS)
- File structure creation
- Base class definitions
- Skeleton implementations

### Phase 3: Core Implementation (PLANNED)
- XpressNet interface integration
- Ecos TCP/XML implementation
- State engine full implementation
- Command routing logic

### Phase 4: Testing (PLANNED)
- Hardware integration testing
- Functional testing
- Unit testing
- Edge case handling

## Documentation

- **`docs/01_DESIGN_DOCUMENT.md`** - Complete architecture and design decisions
- **`docs/02_PROTOCOL_XPRESSNET.md`** - XpressNet protocol details
- **`docs/02_PROTOCOL_ECOS.md`** - Ecos LAN protocol reference
- **`docs/03_HARDWARE_WIRING.md`** - Pin diagrams and connections

## Contributing

This is a learning project. Contributions welcome:
- Bug reports and fixes
- Protocol implementations (LocoNet, Z21)
- Documentation improvements
- Test cases

## License

Open source for personal/educational use. See LICENSE file for details.

## Attribution

This project is inspired by and learns from:
- **Philipp Gahtow's Z21 Command Station** - Architecture patterns, protocol libraries, and modular design philosophy
- **Gahtow's XpressNet Master Library** - Production-proven XpressNet protocol implementation
- **Arduino and ESP8266 communities** - Educational resources and examples

## References

- [Gahtow's Z21 Project](https://pgahtow.de/w/Zentrale_Z21PG/en)
- [XpressNet Protocol](https://pgahtow.de/w/XpressNet)
- [Arduino IDE](https://www.arduino.cc/)
- [ESP8266 Arduino Support](https://arduino.esp8266.com/)
- [Adafruit Libraries](https://adafruit.com/)

## Contact & Support

- Issues: Use GitHub Issues
- Questions: See documentation first, then GitHub Discussions
- Contact: (your contact info)

---

**Last Updated:** 2024
**Version:** 0.1-dev (Pre-Implementation)
