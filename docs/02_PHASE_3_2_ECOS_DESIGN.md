# Phase 3.2: Ecos LAN Protocol Implementation

**Status**: Implemented and verified on real hardware. Originally written 2026-07-22 as a pre-implementation design; revised 2026-10-11 so the sections below describe what the code actually does. Items that changed from the original plan are called out inline. Dated history: [CHANGELOG.md](CHANGELOG.md); current summary: [../CLAUDE.md](../CLAUDE.md).  
**Target**: ESP8266 Wemos D1 Mini  
**Scope**: TCP interface (ESU text object protocol - not XML) to an ESU Ecos command station  

---

## Overview

The Ecos LAN interface connects the bridge to an ESU Ecos command station via WiFi TCP, using ESU's plain-text object protocol (not XML). Ecos serves as the **master persistence layer** and **source of truth** for all DCC device state (locomotives and accessories).

**Key Principle**: State Engine is truth table (second only to Ecos itself). Apply changes immediately, validate/sync with Ecos asynchronously.

---

## Architecture

### Data Flow

```
XpressNet (RS485)           Ecos (WiFi TCP, text protocol)
      ↓                              ↑
  XpressNetInterface         EcosInterface
      ↓                              ↓
      └─→ CommandRouter ←───────────┘
          ├→ StateEngine (truth table)
          ├→ OLED Display
          └→ [back to XpressNet if state differs]
```

### State Engine Role

- **Tracks**: All locos (50 max) and accessories in use
- **Ownership**: Owned by CommandRouter, read/write by all protocols
- **Lifecycle**: Entries expire after 5 min of inactivity (no updates from any protocol; the timeout is EEPROM-configurable)
- **Persistence**: None (Ecos handles persistence)
- **Priority**: XpressNet changes applied immediately, Ecos updates backfill/validate

### Subscription Model

- **Strategy**: Subscribe ONLY to locos in state engine (known locos)
- **Trigger**: When a throttle protocol (XpressNet or Z21) sends a command for an unknown loco:
  1. Apply change immediately to state engine
  2. Send change to Ecos (queued by address if the Ecos object ID is not resolved yet or Ecos is down)
  3. Subscribe with `request(id, view)` and run paced baseline `get()` queries for the loco's current speed/direction/functions
- **Cleanup**: When loco expires from state engine (5 min timeout), unsubscribe from Ecos
- **Resubscribe**: Automatic when same address appears again (next XpressNet command)

### Activity Tracking

- **Per-address**: Track `last_update_ms` by DCC address
- **Source-independent**: Doesn't matter if XpressNet or other throttle controlled it
- **5-minute expiry**: No updates in 5 min → purge from state engine + unsubscribe Ecos
- **Accessories**: Same lifecycle as locos (expire after inactivity, resubscribe on demand)

---

## Echo Prevention (as implemented)

> **Revised**: the original design here was a circular buffer of the last 10 outgoing commands with a 2-second match window. That was replaced during Phase 6 (2026-08-28) with the scheme below; see the CHANGELOG for why.

### Problem
When a throttle command is forwarded to Ecos and Ecos confirms it, that confirmation must not be pushed back to the throttle that sent it (it would flash "stolen" on its own display), but a genuinely independent change made on Ecos itself must reach every throttle.

### Solution
`CommandRouter` keeps **one** attribution record: the address, command type, source protocol and timestamp of the most recent throttle-originated command. `wasRecentSource(address, source)` is true when the record matches that address and protocol and is younger than `ECOS_ECHO_ATTRIBUTION_WINDOW_MS` (**4000 ms** - Ecos's real confirmation latency under load exceeded the earlier 500 ms / 2 s windows). When an Ecos event fans out, each destination protocol is checked individually, so only the true originator is skipped; the others still receive the update.

Other throttle-facing protocols do **not** wait for the Ecos confirmation: a throttle command fans out to Ecos and all other throttle protocols immediately. Ecos-originated handling never writes to the attribution record. Incoming XpressNet/Z21 commands are never dropped as echoes.

Trade-off accepted: an independent Ecos-direct change to the *same address* within the window after a throttle command may be attributed to the throttle.

---

## Pending Query Buffer (as implemented)

### Problem
Commands can arrive while the Ecos object ID for an address is not known yet (loco not yet in the address map) or while Ecos is disconnected. While disconnected the address map is empty, so both cases look the same to the sender.

### Solution
`EcosInterface::queuePendingQuery()` / `queuePendingFunctionQuery()` keep a small table (`MAX_PENDING_QUERIES` = 5) **upserted by DCC address**: a later speed/direction or function change for the same address updates the existing entry instead of consuming another slot. Each entry records separately whether it holds speed+direction and/or functions, so a replay only sends what was actually queued. `flushPendingQueries()` runs once the address map contains the loco (after (re)connect) and sends **direction before speed** (DCC decoders receive both in one packet; the reverse order built the packet from a stale speed).

---

## Update Strategy: XpressNet Priority

### Principle
State Engine is truth. XpressNet commands apply immediately. Ecos updates backfill missing data.

### Scenarios

**Scenario 1: Unknown Loco from a throttle**
```
Event: XpressNet sends speed 64 for unknown Loco 100
Action:
  1. Create new LocoState (speed=64, dir=1, functions=0)
  2. Send to Ecos (queued by address if the object ID is not resolved yet)
  3. Subscribe (request view) and run paced baseline get() queries

Ecos responds (after 200ms):
  4. Merge: functions from Ecos; speed/direction only if Ecos reported them
```

**Scenario 2: Ecos Update for Known Loco**
```
Event: Ecos sends speed 32 for Loco 100 (we have speed 64)
Action:
  1. Merge into state engine - only the fields Ecos actually reported
     (has_speed / has_direction / functions_mask)
  2. Fan out to XpressNet and Z21, skipping only a protocol that was the
     recent source of a command for this address (wasRecentSource())
```

**Scenario 3: Multi-Throttle Control**
```
Throttle A (XpressNet): Controls Loco 50
  → State: speed 50, direction 1

Throttle B (Ecos console): Changes Loco 50 to speed 25
  → Ecos sends update: speed 25
  → Not attributed to any throttle protocol
  → Fanned out to XpressNet and Z21 (CommandRouter::broadcastCommand())
  → Throttle A's display shows speed 25 (consistent with Ecos)
```

---

## Accessory (Turnout) Management (as implemented)

Accessories are **not** tracked in `StateEngine`; they were scoped in two steps (Phase 5 step 10, Phase 7 steps 1-2).

- **Throttle -> Ecos**: XpressNet (`onTurnoutCommand`, activate edge only) and Z21 (`LAN_X_SET_TURNOUT`) call `CommandRouter`, which sends `set(11, switch[DCC<addr><r|g>])` to the fixed accessory manager (`r` = straight, `g` = diverging). Ecos generates the pulse itself, so no object-ID lookup is needed in this direction.
- **Ecos -> throttles**: accessory objects are discovered with `queryObjects(11, addr)` and subscribed individually (`MAX_ECOS_ACCESSORIES`); `state[val]` events (`0` = diverging, `1` = straight; `switching[val]` is ignored) fan out to XpressNet and Z21. Accessory object IDs are told apart from loco IDs by `ECOS_ACCESSORY_ID_MIN` (10000).
- **Known-state cache**: `CommandRouter` keeps the best-known position per address (`MAX_KNOWN_ACCESSORIES`) from either direction, to answer a MultiMaus's accessory-info poll and Z21's `GET_TURNOUT_INFO`.
- Real Ecos firmware implements the individual accessory-object commands even though the ESU spec marks that section "in planning".
- There is no accessory expiry; entries persist for the session.

---

## Connection Management

### Initialization
```
1. WiFi connect (SSID/password and the bridge's static IP from EEPROM)
2. Establish TCP connection to Ecos (IP:PORT)
3. On success: Status = CONNECTED
4. On failure: Status = ERROR (retry with backoff)
```

### Heartbeat & Keep-Alive
- **Heartbeat interval**: 5 seconds (`ECOS_HEARTBEAT_INTERVAL`; originally 30 s, tightened after a hardware test on 2026-08-03)
- **Type**: the `queryObjects(10, addr, name)` address-map query (a plain `get(10, ...)` is rejected by real Ecos), which also refreshes the map
- **Watchdog**: no data received for `ECOS_MESSAGE_TIMEOUT` (10 s) closes the socket - the only way to notice an unplugged Ethernet cable, which sends no TCP reset
- **Failure**: status = DISCONNECTED, address map cleared, reconnect with backoff

### Reconnect Strategy
```
Initial attempt: immediate
Backoff 1: 5 sec
Backoff 2: 10 sec
Backoff 3: 20 sec
Max: 60 sec (retry every 60 sec forever)

Success: attempt counter reset
```

### Command Queueing During Disconnect
```
If Ecos disconnected:
  - XpressNet and Z21 still work (command applied to state)
  - Ecos command: queued per address via the pending-query table
    (see "Pending Query Buffer" - upserted, MAX_PENDING_QUERIES = 5)
  - On reconnect: address map rebuilt, then queue flushed to Ecos
```

---

## TCP Protocol Details - ESU Ecos LAN Protocol (Text-Based, NOT XML)

### Message Format (CORRECTED)
**Protocol**: Plain-text, line-based, object/property format over TCP/15471

All commands end with `\n` (LF). All replies/events are framed by `<REPLY>`/`<EVENT>` start and `<END code>` marker.

**Wire Format Examples**:
```
→ queryObjects(10, addr, name)
← <REPLY queryObjects(10, addr, name)>
← 1000 addr[100] name[My Locomotive]
← 1001 addr[101] name[Other Loco]
← <END 0 (OK)>

→ request(1000, view)
← <REPLY request(1000,view)>
← 1000 speed[0] dir[1]
← <END 0 (OK)>

← <EVENT 1000>
← 1000 speed[64] dir[1]
← <END 0 (OK)>

→ set(1000, speed[80])
← <REPLY set(1000, speed[80])>
← <END 0 (OK)>
```

> **Corrected 2026-07-31**: the direction property is `dir`, not `direction` -
> `direction` is rejected by real Ecos as "unknown option". This example
> originally used the wrong (unverified) name; confirmed against real
> hardware via `get(id)`, which dumps every real property name for a
> locomotive object. See `docs/CHANGELOG.md`.

**Key Points**:
- Object IDs (e.g., 1000) are Ecos-internal, independent of DCC address
- DCC address is queried via `queryObjects(10, ...)` — returns addr[n] properties
- Commands: `queryObjects(10,...)`, `request(id,view|control)`, `set(id,prop[val])`, `get(id,prop)`, `release(id,mode)`
- Functions encoded as separate properties: `func[0,1]` (F0 on), `func[1,0]` (F1 off)
- `<END n (text)>` closes block (n=0 success, n≥1 error)

### Buffer Management
```cpp
static const int MAX_TCP_BUFFER = 1024;  // From config.h
uint8_t tcp_buffer[MAX_TCP_BUFFER];
int tcp_buffer_index = 0;

// Non-blocking read and parse:
while (available()) {
    uint8_t byte = read();
    tcp_buffer[tcp_buffer_index++] = byte;

    if (complete_message(tcp_buffer, tcp_buffer_index)) {
        parse_and_route(tcp_buffer, tcp_buffer_index);
        tcp_buffer_index = 0;
    }

    if (tcp_buffer_index >= MAX_TCP_BUFFER) {
        // Overflow - reset and log error
        tcp_buffer_index = 0;
    }
}
```

### Parsing
- **Line-based**: bytes are accumulated until `\n`; lines are grouped into `<REPLY>`/`<EVENT>` blocks that close with `<END code (text)>`
- **Complete blocks only**: a block is parsed only once its `<END>` line has arrived (`EcosMessageParser`)

---

## State Updates & Broadcasting

### When XpressNet Sends Command
```
CommandRouter::handleXpressNetCommand(address, speed, dir)
  1. Create/get LocoState
  2. Update: speed, direction, last_source=XPRESSNET, last_update_ms=now
  3. Fan out immediately: Ecos (sendSpeedCommand) and every other
     throttle protocol (Z21)
  4. Record the attribution (address, source, timestamp)
  5. If unknown loco: subscribe + paced baseline queries
```

### When Ecos Sends Update
```
EcosInterface::handleEcosUpdate(address, speed, dir, functions)
  1. Merge into the LocoState (only the fields Ecos reported)
  2. CommandRouter::broadcastCommand(ECOS): for each of XpressNet and Z21,
     skip it only if wasRecentSource(address, thatProtocol)
```

### Broadcast to throttles
```
If the update was not attributed to that protocol:
  Send update to all connected throttles (XpressNetInterface, Z21LanInterface)
  This keeps throttle displays in sync with Ecos

Example: Another throttle changed speed on Ecos console
  → Ecos sends update
  → We send speed change to all throttles via XpressNet
  → All throttles now show consistent speed
```

---

## OLED Display Integration

Ecos has its own OLED page (see [01_DESIGN_DOCUMENT.md](01_DESIGN_DOCUMENT.md) section 4): connection status, the configured Ecos IP, and the round-trip latency of the last heartbeat query/reply. Loco counts and uptime are on the main/device pages. Connection state is shown with hand-drawn icons (no Unicode glyphs). The OLED refreshes every 500 ms.

---

## Non-Blocking Design Constraints

### update() Method
```cpp
void EcosInterface::update() {
    // Phase 1: Check connection status
    updateConnectionStatus();

    // Phase 2: Non-blocking TCP read
    while (wifi_client.available()) {
        if (processIncomingByte()) {
            // Parsed complete message
            handleMessage(...);
        }
        yield();  // Let WiFi stack run
    }

    // Phase 3: Send heartbeat if needed
    if (heartbeat_timer.shouldExecute()) {
        sendHeartbeat();
    }

    // Phase 4: Replay any queued commands once the object ID is resolved
    flushPendingQueries();
}
// Total time: < 50ms (doesn't block XpressNet)
```

### No Blocking Operations
- No `delay()`
- No synchronous TCP waits
- No parsing entire buffer at once (parse incrementally)
- No flash/EEPROM writes in the update path (settings are only saved from Setup Mode)

---

## Lifecycle: Loco Expiry & Resubscription

### Example Timeline
```
12:00:00  XpressNet: Loco 100 speed 50
          → Add to state engine
          → Subscribe to Ecos updates
          → last_update_ms = 12:00:00

12:04:00  No updates from XpressNet for Loco 100
          → Still in state engine

12:05:00  Loco 100 hasn't been updated in 5 minutes
          → StateEngine::expungeInactiveLocos() runs (via CommandRouter::update())
          → Remove Loco 100 from state
          → Unsubscribe from Ecos

12:05:30  XpressNet: Loco 100 speed 60 (new throttle picks it up)
          → Not in state engine
          → Create new entry, send to Ecos
          → Start Ecos query
          → Subscribe again when query completes
```

---

## Error Recovery

### WiFi Connection Lost
```
Attempt 1: Immediate reconnect
  → If success: Continue
  → If fail: Wait 5 sec

Attempt 2: After 5 sec
  → If success: Continue
  → If fail: Wait 10 sec

[Backoff continues up to 60 sec]

XpressNet behavior: UNAFFECTED
  - Throttles still work
  - State engine still active
  - Commands queue for later Ecos sync
```

### TCP Connection Lost
```
Detect: No data received for ECOS_MESSAGE_TIMEOUT (10 seconds)
  → Close connection
  → Status = DISCONNECTED
  → Start WiFi reconnect sequence

On reconnect:
  → Reestablish TCP to Ecos
  → Rebuild the address map, then flush queued commands
  → Resubscribe to locos as they next appear
```

### Ecos Unresponsive
```
No data for ECOS_MESSAGE_TIMEOUT
  → Treat as connection loss
  → Close socket
  → Attempt reconnect

Pending commands:
  → Stay queued
  → Retry when Ecos comes back
```

---

## Implementation Files

- `protocols/ecos/ecos_interface.h/.cpp` - TCP connection, address maps (loco and accessory), subscriptions, pending queues, heartbeat (hardware-coupled, not in the native build)
- `protocols/ecos/ecos_message_parser.h/.cpp` - `<REPLY>`/`<EVENT>` framing and `key[value]` property extraction (native-tested)
- `protocols/ecos/ecos_protocol.h/.cpp` - outgoing command builders: `request`, `set`, `get`, `queryObjects`, `release`, accessory `switch` (native-tested)
- `command_router.cpp`, `state_engine.*`, `oled_display.cpp`, `config.h` carry the router, state and display sides

---

## Testing Strategy

### Testing as built
The native suite covers the parser, the command builders and the router (via a mock interface). The Ecos TCP behaviour was verified against a real Ecos; the mock Ecos server described below was not built. The cases listed are covered by the unit tests or by live hardware tests.

### Original mock-server plan
A simple TCP server that:
- Accepts connection on port 15471
- Responds to queries with canned data
- Sends subscription updates for test addresses
- Simulates latency (100-500ms responses)

### Test Cases
1. **Initial connection**: WiFi connects, TCP establishes, status=CONNECTED
2. **Unknown loco**: XpressNet sends speed, Ecos query completes, state merges
3. **Echo prevention**: Send speed, Ecos echoes back, bridge suppresses re-broadcast
4. **Multi-throttle**: Ecos update differs from state, broadcast to XpressNet
5. **Accessory control**: Send accessory command, receive status confirmation
6. **Loco expiry**: No XpressNet updates for 5 min, state removed, unsubscribe
7. **Reconnect**: Lose connection, reconnect, queue flushed
8. **Buffer overflow**: Large reply, handle correctly without crash

---

## Performance Targets

- **Query latency**: 200-500ms (acceptable, not time-critical)
- **Update broadcast**: < 100ms (should see Ecos changes on XpressNet throttles quickly)
- **Memory footprint**: < 5KB (TCP buffer + queue + state tracking)
- **CPU load**: < 5% of main loop time (XpressNet is still highest priority)
- **Throughput**: 10+ commands/second to/from Ecos (should handle typical throttle usage)

---

## Future Enhancements

- Block occupancy/feedback from Ecos
- Consist (double-header) support
- Advanced function mapping
- Accessory feedback/sensing

(128-step speed, WiFi credentials in EEPROM and the web configuration UI, listed here originally, are implemented.)

---

**Document Version**: 1.1 (revised to match the implementation)
**Date**: 2026-07-22 (revised 2026-10-11)
**Author**: Claude + Frikkie
**Status**: Implemented
