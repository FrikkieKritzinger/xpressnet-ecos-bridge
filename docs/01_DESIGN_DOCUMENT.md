# XpressNet-Ecos Bridge - Architecture & Design Document

**Project Status:** Pre-Implementation Design Phase  
**Target Platform:** Wemos D1 Mini (ESP8266)  
**Scope:** Learn from Gahtow's Z21 architecture, build fresh modular codebase  
**Version:** 0.1 (Design Phase)

---

## 1. Executive Summary

### Problem Statement
- Current POC can receive XpressNet commands but cannot bridge to Ecos LAN protocol
- Existing Z21 codebase is complex and difficult to extend for Ecos integration
- Need modular, timing-aware architecture that respects XpressNet's strict protocol requirements

### Solution Approach
- Design modular component architecture inspired by Gahtow's patterns
- Implement fresh codebase (not forked from Z21)
- Use proven XpressNet Master Library (Gahtow's)
- Create protocol-agnostic state engine
- Compile-time configuration for modularity
- Local-only, learning-focused implementation

### Key Design Goals
1. **Modularity:** Add/remove protocols via config.h toggles
2. **Timing Integrity:** Respect XpressNet's strict timing requirements
3. **Clarity:** Fresh, readable codebase easier to understand and extend
4. **Flexibility:** Extensible to LocoNet, Z21 LAN in future
5. **Learning:** Educational value - understand protocol implementation

---

## 2. Architectural Overview

### 2.1 Layered Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│  (State Engine, Bridge Logic, Command Routing)              │
└───────────────┬──────────────────┬──────────────────────────┘
                │                  │
      ┌─────────┴────────┐    ┌────┴──────────────┐
      │  Protocol Layer  │    │  Hardware Layer   │
      └─────────┬────────┘    └────┴──────────────┘
                │                  │
    ┌───────────┼───────────┐      │
    │           │           │      │
┌───┴──┐  ┌────┴────┐  ┌───┴──┐   │
│XNet  │  │ Ecos    │  │Z21   │   │
│Master│  │ LAN     │  │ LAN   │   │
└──────┘  └─────────┘  └───────┘   │
    │           │           │      │
    └───────────┼───────────┴──────┘
                │
      ┌─────────┴──────────┐
      │  Interface Layer   │
      │  (Serial, TCP/IP)  │
      └────────────────────┘
                │
      ┌─────────┴──────────┐
      │  Hardware Layer    │
      │  (MAX485, Ethernet)│
      └────────────────────┘
```

### 2.2 Module Organization

```
xpressnet_ecos_bridge/
├── config.h                          # Compile-time configuration
├── main.ino                          # Arduino entry point
├── definitions.h                     # Global types & constants
├── state_engine.h / .cpp             # Locomotive state management
├── command_router.h / .cpp           # Command translation & routing
│
├── interfaces/                       # Hardware abstraction layer
│   ├── interface_base.h              # Abstract interface class
│   └── (protocol implementations)
│
├── protocols/                        # Protocol implementations
│   ├── xpressnet/
│   │   ├── xpressnet_interface.h
│   │   └── xpressnet_interface.cpp
│   ├── ecos/
│   │   ├── ecos_interface.h
│   │   └── ecos_interface.cpp
│   ├── loconet/                      # (Future)
│   │   ├── loconet_interface.h
│   │   └── loconet_interface.cpp
│   └── z21lan/                       # (Future)
│       ├── z21lan_interface.h
│       └── z21lan_interface.cpp
│
├── display/                          # Display drivers
│   ├── display_base.h
│   └── oled_display.h / .cpp
│
├── utils/                            # Utility functions
│   ├── timing.h                      # Non-blocking timing utilities
│   ├── debug.h                       # Debug logging
│   └── memory.h                      # Memory utilities
│
├── libraries/                        # External libraries (git submodules)
│   └── XpressNetMaster/              # Gahtow's XpressNet library
│
└── tests/                            # Unit tests (future)
    └── test_state_engine.cpp
```

---

## 3. Design Patterns & Architecture Decisions

### 3.1 Modular Component System

**Pattern: Compile-Time Feature Toggles via config.h**

```cpp
// config.h - User Configuration
#define ENABLE_XPRESSNET    1
#define ENABLE_ECOS_LAN     1
#define ENABLE_LOCONET      0
#define ENABLE_Z21_LAN      0
#define ENABLE_OLED_DISPLAY 1

#define XPRESSNET_RX_PIN    13  // D7
#define XPRESSNET_TX_PIN    15  // D8
#define XPRESSNET_DE_PIN    14  // D5
#define XPRESSNET_RE_PIN    12  // D6

#define ECOS_IP             "192.168.1.100"
#define ECOS_PORT           15471

#define OLED_SDA_PIN        4   // D2
#define OLED_SCL_PIN        5   // D1
```

**Pattern: Interface-Based Component Architecture**

Each protocol/display implements a common interface:

```cpp
// interfaces/interface_base.h
class ProtocolInterface {
public:
    virtual ~ProtocolInterface() {}
    virtual bool begin() = 0;           // Initialize
    virtual void update() = 0;          // Non-blocking update
    virtual bool handleMessage() = 0;   // Process incoming
    virtual void sendCommand(...) = 0;  // Send outgoing
};

class DisplayInterface {
public:
    virtual ~DisplayInterface() {}
    virtual bool begin() = 0;
    virtual void update(const SystemStatus&) = 0;
};
```

**Pattern: Static Registration vs. Dynamic Allocation**

For ESP8266 with limited RAM, use compile-time static objects:

```cpp
// main.ino
#if ENABLE_XPRESSNET
XpressNetInterface xpressnet_interface;
#endif

#if ENABLE_ECOS_LAN
EcosLanInterface ecos_interface;
#endif

#if ENABLE_OLED_DISPLAY
OledDisplay display;
#endif

// In setup():
#if ENABLE_XPRESSNET
    xpressnet_interface.begin();
#endif
#if ENABLE_ECOS_LAN
    ecos_interface.begin();
#endif
#if ENABLE_OLED_DISPLAY
    display.begin();
#endif
```

### 3.2 Timing Architecture

**Principle: XpressNet Timing is Critical**

XpressNet protocol requirements:
- 9600 baud (9 data bits + 1 parity bit = 10 bits total)
- ~1ms per byte transmission
- Message roundtrip: typically 20-50ms
- Must not miss messages due to blocking operations

**Non-Blocking Pattern: Cooperative Multitasking with millis()**

```cpp
// timing.h
class TimedTask {
private:
    unsigned long last_execute;
    unsigned long interval;
public:
    TimedTask(unsigned long interval_ms) 
        : last_execute(0), interval(interval_ms) {}
    
    bool shouldExecute() {
        unsigned long now = millis();
        if (now - last_execute >= interval) {
            last_execute = now;
            return true;
        }
        return false;
    }
};

// Usage in loop():
TimedTask heartbeat_task(1000);  // Every 1000ms
if (heartbeat_task.shouldExecute()) {
    sendHeartbeat();
}
```

**Main Loop Structure:**

```cpp
void loop() {
    // Order matters: timing-critical first
    
    // 1. XpressNet (highest priority - timing critical)
    #if ENABLE_XPRESSNET
    xpressnet_interface.update();  // Must run frequently
    #endif
    
    // 2. Check for incoming commands (all protocols)
    #if ENABLE_ECOS_LAN
    ecos_interface.update();  // Non-blocking read
    #endif
    
    // 3. Periodic housekeeping (lower priority)
    static TimedTask heartbeat(5000);
    if (heartbeat.shouldExecute()) {
        performHeartbeatCheck();
    }
    
    // 4. Display update (lowest priority)
    #if ENABLE_OLED_DISPLAY
    static TimedTask display_update(500);
    if (display_update.shouldExecute()) {
        display.update(system_status);
    }
    #endif
    
    yield();  // ESP8266 watchdog reset
}
```

### 3.3 State Engine Architecture

**No Persistence Model**

```cpp
// definitions.h
enum class LocoSource {
    UNKNOWN = 0,
    XPRESSNET = 1,
    ECOS = 2,
    LOCONET = 3,
    Z21_LAN = 4
};

struct LocoState {
    uint16_t dcc_address;           // DCC address (0-9999)
    uint8_t speed;                  // 0-126
    uint8_t direction;              // 0=reverse, 1=forward
    uint32_t functions;             // F0-F31
    LocoSource last_source;         // Where command came from
    unsigned long last_update_ms;   // Timestamp
    bool subscribed_to_ecos;        // Currently subscribed?
};

// state_engine.h
class StateEngine {
private:
    static const int MAX_LOCOS = 50;
    LocoState locos[MAX_LOCOS];
    int loco_count;
    
public:
    bool addOrUpdateLoco(uint16_t address, const LocoState& state);
    bool getLoco(uint16_t address, LocoState& state);
    void removeInactiveLoco(uint16_t address);
    void expungeOldestInactive();
    int getLocoCount() { return loco_count; }
};
```

**In-Memory Only, Ecos Provides Persistence**

When device restarts:
1. State engine empty
2. XpressNet/Ecos sends commands for controlled locos
3. State engine rebuilds from live commands
4. No data loss (Ecos retains all state)

### 3.4 Command Routing & Echo Prevention

**Smart Routing Pattern:**

```cpp
// command_router.h
class CommandRouter {
private:
    struct LastCommand {
        uint16_t loco_address;
        uint8_t command_type;       // SPEED, DIRECTION, FUNCTION
        unsigned long timestamp_ms;
    };
    
    LastCommand last_ecos_command;
    
public:
    void handleXNetCommand(const XNetMessage& msg);
    void handleEcosCommand(const EcosMessage& msg);
    
private:
    bool isEchoCommand(uint16_t address, uint8_t type);
    void broadcastToOtherProtocols(
        uint16_t address, 
        const LocoState& state,
        LocoSource source);
};
```

**Echo Prevention:**
- When receiving update from Ecos, mark source
- When routing to XpressNet, check if this came from XpressNet recently
- If so, suppress (it's an echo)
- Timeout: 500ms window

---

## 4. Protocol-Specific Design

### 4.1 XpressNet Interface

**Uses: Gahtow's XpressNetMaster Library**

Already solves:
- 9+1 bit protocol handling
- Hardware UART with software fallback
- Master/Slave mode auto-detection
- Device enumeration
- Message checksum/validation

**Our XpressNet Wrapper:**

```cpp
// protocols/xpressnet/xpressnet_interface.h
class XpressNetInterface : public ProtocolInterface {
private:
    XpressNetMaster xnet;
    TimedTask poll_task;
    
    struct XNetDevice {
        uint8_t device_id;
        uint8_t status;
        unsigned long last_seen;
    };
    
    XNetDevice devices[31];  // Max 31 XNet slaves
    int device_count;
    
public:
    bool begin() override;
    void update() override;
    bool handleMessage() override;
    
    void handleLocoCommand(uint16_t address, uint8_t speed, uint8_t direction);
    void handleFunctionCommand(uint16_t address, uint32_t functions);
    
private:
    void processIncomingMessage();
    void updateDeviceStatus();
};
```

**Timing Guarantees:**
- `update()` called every loop iteration
- Non-blocking serial read from XNet bus
- Message processing immediate (not queued)
- No delays in loop

### 4.2 Ecos LAN Interface

**Protocol: XML-based TCP/IP**

Examples from Ecos documentation:
```xml
<!-- Subscribe to locomotive -->
<SUBSCRIBE id="1" TYPE="LOCO" OBJECT="" ADDRESS="100" BASE_VALUE="1" VERSION="1" />

<!-- Receive locomotive update -->
<LOCO id="4" ADDRESS="100" SPEED="50" DIRECTION="1" F0="1" F1="0" />

<!-- Send speed/direction command -->
<SET id="2" OBJECT="4" ADDRESS="100" SPEED="75" DIRECTION="1" />

<!-- Heartbeat -->
<REQUEST id="3" TYPE="STATUS" />
```

**Our Ecos Wrapper:**

```cpp
// protocols/ecos/ecos_interface.h
class EcosLanInterface : public ProtocolInterface {
private:
    WiFiClient tcp_client;
    TimedTask connect_task;
    TimedTask heartbeat_task;
    
    IPAddress ecos_ip;
    uint16_t ecos_port;
    
    String rx_buffer;
    
public:
    bool begin() override;
    void update() override;
    bool handleMessage() override;
    
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction);
    void sendFunctionCommand(uint16_t address, uint32_t functions);
    void subscribeToLoco(uint16_t address);
    
private:
    bool parseXmlMessage(const String& xml);
    void handleEcosLocoUpdate(const String& xml);
    void ensureConnected();
};
```

**Non-Blocking TCP:**
- Connect in `update()`, don't block on connection timeout
- Read available bytes only
- Accumulate in buffer until complete XML element
- Parse on demand

### 4.3 Display Interface (OLED)

```cpp
// display/oled_display.h
class OledDisplay : public DisplayInterface {
private:
    Adafruit_SSD1306 oled;
    TimedTask update_task;
    uint8_t display_mode;  // Cycle through different info
    
public:
    bool begin() override;
    void update(const SystemStatus& status) override;
    
private:
    void drawStatusScreen(const SystemStatus& status);
    void drawLocoListScreen();
    void drawDebugScreen();
};

// definitions.h - Status structure
struct SystemStatus {
    bool xnet_active;
    bool ecos_connected;
    int active_locos;
    uint8_t xnet_device_count;
    int wifi_rssi;
};
```

---

## 5. Configuration System (config.h)

```cpp
// config.h - Master configuration file

// ===== HARDWARE SELECTION =====
#define ENABLE_XPRESSNET    1
#define ENABLE_ECOS_LAN     1
#define ENABLE_LOCONET      0      // Future
#define ENABLE_Z21_LAN      0      // Future
#define ENABLE_OLED_DISPLAY 1

// ===== XPRESSNET CONFIGURATION =====
#if ENABLE_XPRESSNET
    #define XPRESSNET_RX_PIN    13   // D7
    #define XPRESSNET_TX_PIN    15   // D8
    #define XPRESSNET_DE_PIN    14   // D5 (Driver Enable)
    #define XPRESSNET_RE_PIN    12   // D6 (Receiver Enable)
    #define XPRESSNET_BAUD      9600
    #define XPRESSNET_TIMEOUT   300000  // 5 min inactivity timeout
#endif

// ===== ECOS LAN CONFIGURATION =====
#if ENABLE_ECOS_LAN
    #define ECOS_IP             "192.168.1.100"
    #define ECOS_PORT           15471
    #define ECOS_TIMEOUT        5000   // TCP timeout
    #define ECOS_HEARTBEAT_INTERVAL  30000  // 30 sec heartbeat
#endif

// ===== OLED DISPLAY CONFIGURATION =====
#if ENABLE_OLED_DISPLAY
    #define OLED_SDA_PIN        4     // D2
    #define OLED_SCL_PIN        5     // D1
    #define OLED_ADDRESS        0x3C
    #define OLED_UPDATE_INTERVAL 500  // ms
#endif

// ===== STATE ENGINE =====
#define MAX_LOCOS           50
#define LOCO_INACTIVITY_TIMEOUT  300000  // 5 minutes

// ===== ECHO PREVENTION =====
#define ECHO_PREVENTION_WINDOW   500  // ms

// ===== DEBUG OPTIONS =====
#define ENABLE_DEBUG        1
#define DEBUG_BAUD          115200
#define DEBUG_XNET          1
#define DEBUG_ECOS          1
```

---

## 6. State Management Flow

### 6.1 Locomotive Lifecycle

```
User sends XpressNet command for unknown loco
    ↓
State Engine: Loco not found
    ↓
Create new LocoState with address
    ↓
Mark source: XPRESSNET
    ↓
Subscribe to Ecos (send SUBSCRIBE message)
    ↓
Store in state engine
    ↓
Router: Broadcast to Ecos interface
    ↓
Ecos receives update
    ↓
[During session: receive updates from both sources]
    ↓
5 minutes no update from any source
    ↓
State Engine: Expunge old loco
    ↓
Device power off: All state cleared (expected)
```

### 6.2 Update Flow (XpressNet → Ecos)

```
XpressNet device sends: Speed command for loco 100

XpressNetInterface.update()
    ├─ Receive message from bus
    ├─ Parse: Loco 100, Speed 75, Direction forward
    └─ Call router.handleXNetCommand()
        ├─ State Engine: Find or create loco 100
        ├─ Update speed, direction, timestamp
        ├─ Mark source: XPRESSNET
        ├─ Check: Should echo? (not our command)
        ├─ Call ecos_interface.sendSpeedCommand(100, 75, 1)
        └─ EcosInterface: Queue TCP message

EcosLanInterface.update()
    ├─ Check TCP connection
    └─ Send: <SET id="X" ADDRESS="100" SPEED="75" DIRECTION="1" />

Ecos replies: <LOCO ADDRESS="100" SPEED="75" DIRECTION="1" />

EcosInterface receives reply
    ├─ Parse XML
    ├─ Call router.handleEcosCommand()
    └─ Router checks: Is echo? (YES - same loco, within 500ms)
        └─ Suppress send to XpressNet (prevent loop)
```

### 6.3 Update Flow (Ecos → XpressNet)

```
Ecos sends: Loco 205 speed changed on another app

EcosLanInterface.update()
    ├─ Read from TCP: <LOCO ADDRESS="205" SPEED="40" ... />
    ├─ Parse message
    └─ Call router.handleEcosCommand()
        ├─ State Engine: Find or create loco 205
        ├─ Update speed, direction, timestamp
        ├─ Mark source: ECOS
        ├─ Check: Should echo? (not from XpressNet)
        ├─ Call xpressnet_interface.handleLocoCommand(205, 40, dir)
        └─ Record as last Ecos command for echo prevention

XpressNetInterface.update()
    ├─ Send to XNet bus: Speed command for loco 205
    └─ XpressNet devices receive update
```

---

## 7. External Dependencies

### 7.1 Required Libraries

From Gahtow's Project (via Git Submodule):
- **XpressNetMaster** - XpressNet protocol handling
  - Location: `libraries/XpressNetMaster/`
  - Solves: 9+1 bit serial, checksums, message framing
  - Already proven working

Arduino Built-in/Standard:
- **WiFi** (ESP8266 built-in)
- **Wire** (I2C, ESP8266 built-in)
- **Adafruit_SSD1306** (OLED display)
- **Adafruit_GFX** (Graphics library, dependency of SSD1306)

### 7.2 Library Acquisition Strategy

```bash
# Clone Gahtow's XpressNet library as submodule
git submodule add \
  https://sourceforge.net/projects/f944.pgahtow.p/files/Arduino\ \(v1.0\)\ libaries/ \
  libraries/

# Arduino IDE will auto-find libraries in:
# - Arduino libraries folder
# - Sketch/libraries folder
```

---

## 8. Compilation & Build Strategy

### 8.1 ESP8266 Arduino IDE Configuration

```
Tools → Board → LOLIN(WEMOS) D1 mini (ESP8266)
Tools → Upload Speed → 115200
Tools → CPU Frequency → 80 MHz
Tools → Flash Size → 4M (3M SPIFFS)
```

### 8.2 Memory Optimization

ESP8266 memory profile:
- **Flash:** 4MB (plenty for sketch)
- **RAM:** 160KB total
  - OS reserved: ~60KB
  - Heap available: ~100KB
  - Stack: ~5KB

Optimal state engine size:
- 50 locomotives × ~30 bytes each = 1.5KB
- Room for: TCP buffers, message queues, stack

### 8.3 Build Process

```cpp
// main.ino
#include "config.h"           // Must be first
#include "definitions.h"
#include "state_engine.h"
#include "command_router.h"

#if ENABLE_XPRESSNET
    #include "protocols/xpressnet/xpressnet_interface.h"
#endif

#if ENABLE_ECOS_LAN
    #include "protocols/ecos/ecos_interface.h"
#endif

// Conditional compilation via config.h preprocessor directives
// Only enabled modules are compiled and linked
```

---

## 9. Testing Strategy

### 9.1 Unit Testing (Offline)

Test state engine logic without hardware:

```cpp
// tests/test_state_engine.cpp
#include <gtest/gtest.h>
#include "state_engine.h"

TEST(StateEngine, AddLoco) {
    StateEngine engine;
    LocoState state;
    state.dcc_address = 100;
    
    ASSERT_TRUE(engine.addOrUpdateLoco(100, state));
    ASSERT_EQ(engine.getLocoCount(), 1);
}

TEST(StateEngine, ExpungeInactive) {
    // Mock time, verify expiry logic
}
```

### 9.2 Integration Testing (Hardware)

```cpp
// tests/integration_test.ino
// Upload to device, verify:
// 1. XpressNet bus communication
// 2. State engine updates
// 3. Ecos TCP connection
// 4. Message routing
// 5. Echo prevention
```

### 9.3 Protocol Testing

Each protocol interface has test mode:

```cpp
// In config.h
#define ENABLE_XNET_TEST_MODE  1   // Synthetic test messages
#define ENABLE_ECOS_TEST_MODE  1   // Simulated Ecos
```

---

## 10. Extensibility Points

### 10.1 Adding LocoNet Support (Future)

1. Create: `protocols/loconet/loconet_interface.h`
2. Implement: `ProtocolInterface` interface
3. Add: `#define ENABLE_LOCONET 1` to config.h
4. Register in main.ino: `LocoNetInterface loconet_interface;`
5. Add to main loop
6. Router automatically bridges commands

### 10.2 Adding Z21 LAN Support (Future)

Same pattern - Z21 has different protocol than Ecos but same bridge architecture.

### 10.3 Adding New Display Type

1. Create: `display/new_display.h`
2. Extend: `DisplayInterface`
3. Register in main.ino
4. Toggle via config.h

---

## 11. Success Criteria

### Phase 1: Foundation (This Phase)
- [ ] Architecture document approved
- [ ] Config system designed
- [ ] State engine pattern defined
- [ ] XpressNet interface skeleton created
- [ ] Ecos interface skeleton created
- [ ] Basic main loop structure

### Phase 2: XpressNet Implementation
- [ ] XpressNetMaster library integrated
- [ ] Receive and parse XNet messages
- [ ] State engine updates from XNet
- [ ] OLED displays status
- [ ] Test with actual XpressNet device

### Phase 3: Ecos Integration
- [ ] TCP connection to Ecos
- [ ] XML parsing
- [ ] Subscribe to locos
- [ ] Send speed/direction commands
- [ ] Receive updates from Ecos

### Phase 4: Bridging & Testing
- [ ] Command routing between protocols
- [ ] Echo prevention working
- [ ] State engine lifecycle management
- [ ] Full integration testing
- [ ] Documentation complete

---

## 12. Known Constraints & Assumptions

### Constraints
- **Single-core only:** No FreeRTOS (ESP8266 limitation)
- **Limited RAM:** ~100KB usable, must keep structures lean
- **No persistence:** EEPROM not used (Ecos handles it)
- **Local network only:** No internet, no MQTT
- **XpressNet timing critical:** Must not miss messages

### Assumptions
- Ecos available on same LAN
- XpressNet bus properly terminated (120Ω)
- WiFi reasonably stable (reconnection handled)
- Devices used during railway session only (state engine resets on power)

---

## 13. File Structure Summary

```
xpressnet_ecos_bridge/
├── config.h                          ← USER CONFIGURATION
├── main.ino                          ← ENTRY POINT
├── definitions.h                     ← GLOBAL TYPES
├── state_engine.h/cpp                ← LOCOMOTIVE STATE
├── command_router.h/cpp              ← COMMAND BRIDGING
│
├── interfaces/
│   └── interface_base.h              ← ABSTRACT INTERFACES
│
├── protocols/
│   ├── xpressnet/
│   │   └── xpressnet_interface.h/cpp
│   ├── ecos/
│   │   └── ecos_interface.h/cpp
│   ├── loconet/                      ← (FUTURE)
│   └── z21lan/                       ← (FUTURE)
│
├── display/
│   └── oled_display.h/cpp
│
├── utils/
│   ├── timing.h
│   └── debug.h
│
└── README.md                         ← THIS DOCUMENT

```

---

## 14. Next Steps

### Immediate (Today)
1. ✅ Approve this architecture document
2. ⏳ Identify any changes needed
3. ⏳ Confirm file/module structure

### Short Term (This Week)
1. Create project folder structure
2. Implement `config.h` template
3. Implement `definitions.h`
4. Implement `state_engine.h/cpp`
5. Implement `command_router.h/cpp`
6. Create protocol interface base

### Implementation (Following Weeks)
1. Integrate XpressNetMaster library
2. Implement XpressNet wrapper interface
3. Implement Ecos TCP + XML parsing
4. Implement OLED display
5. Implement main.ino loop structure
6. Testing & refinement

---

## 15. Questions & Decisions for Review

Before proceeding, please confirm:

1. **Modular Architecture:** Is the compile-time toggle approach via config.h ideal?
2. **State Engine Size:** 50 locomotives sufficient, or should it be configurable?
3. **Echo Prevention Window:** 500ms window appropriate?
4. **External Libraries:** OK to use Gahtow's XpressNetMaster as-is, or should we understand it first?
5. **Ecos Protocol:** Should we document Ecos XML protocol in separate file?
6. **LocoNet/Z21 Future:** Should we design interfaces now, or add later?
7. **Error Handling:** Strategy for device disconnections, timeouts, bus errors?
8. **Testing:** Priority - unit tests or hardware integration first?

---

**Document Status:** Ready for Review & Discussion

