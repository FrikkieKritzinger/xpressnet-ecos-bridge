# Phase 3.2: Ecos LAN Protocol Implementation

**Status**: Design Document (Pre-Implementation)  
**Target**: ESP8266 Wemos D1 Mini  
**Scope**: TCP/XML interface to ESU Ecos command station  
**Priority**: After Phase 3.1 XpressNet completion  

---

## Overview

The Ecos LAN interface connects the bridge to an ESU Ecos command station via WiFi TCP/XML protocol. Ecos serves as the **master persistence layer** and **source of truth** for all DCC device state (locomotives and accessories).

**Key Principle**: State Engine is truth table (second only to Ecos itself). Apply changes immediately, validate/sync with Ecos asynchronously.

---

## Architecture

### Data Flow

```
XpressNet (RS485)           Ecos (WiFi TCP/XML)
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
- **Lifecycle**: Entries expire after 5 min of inactivity (no XpressNet updates)
- **Persistence**: None (Ecos handles persistence)
- **Priority**: XpressNet changes applied immediately, Ecos updates backfill/validate

### Subscription Model

- **Strategy**: Subscribe ONLY to locos in state engine (known locos)
- **Trigger**: When XpressNet sends command for unknown loco:
  1. Apply change immediately to state engine
  2. Send change to Ecos (optimistic update)
  3. Start Ecos query in background for this loco
  4. Add subscription when query completes
- **Cleanup**: When loco expires from state engine (5 min timeout), unsubscribe from Ecos
- **Resubscribe**: Automatic when same address appears again (next XpressNet command)

### Activity Tracking

- **Per-address**: Track `last_update_ms` by DCC address
- **Source-independent**: Doesn't matter if XpressNet or other throttle controlled it
- **5-minute expiry**: No updates in 5 min → purge from state engine + unsubscribe Ecos
- **Accessories**: Same lifecycle as locos (expire after inactivity, resubscribe on demand)

---

## Echo Prevention (Enhanced)

### Problem
TCP round-trip latency (100-500ms) means Ecos echo arrives well after 500ms window.

### Solution
**Outgoing Command Queue**: Track recent commands sent to Ecos to suppress echoes.

```cpp
struct OutgoingCommand {
    uint16_t address;           // DCC address
    uint8_t command_type;       // SPEED, FUNCTION, ACCESSORY
    uint8_t speed;              // For SPEED commands
    uint32_t functions;         // For FUNCTION commands
    unsigned long timestamp_ms; // When sent
};

// Maintain circular buffer of last 10 outgoing commands
// When Ecos sends update, check if it matches any recent outgoing cmd
// If match and timestamp < 2 seconds: suppress (it's our echo)
// If match and timestamp > 2 seconds: process (different device sent it)
```

### Logic
1. XpressNet sends speed change → immediately to Ecos
2. Record in outgoing queue with timestamp
3. Ecos processes change, sends update back via subscription
4. Bridge receives Ecos update → check outgoing queue
5. If matches recent outgoing (< 2 sec) → suppress, don't re-broadcast to XpressNet
6. If no match or old (> 2 sec) → process normally (another device changed it)

**Why 2 seconds?** Ecos typically acknowledges within 200-500ms. 2 sec provides safety margin for network jitter.

---

## Pending Query Buffer

### Problem
Multiple XpressNet commands may arrive while Ecos query for same address is in flight.

### Solution
**Pending Query Buffer**: Queue commands for addresses being queried.

```cpp
struct PendingCommand {
    uint16_t address;
    uint8_t speed;
    uint8_t direction;
    uint32_t functions;
};

// Maintain small queue (5-10 addresses)
// When XpressNet command arrives for unknown address:
//   1. Apply to state engine immediately
//   2. Send to Ecos immediately
//   3. If query already pending: add to queue
//   4. When query completes: process any pending commands
```

### Flow
```
Time  Event
────  ──────────────────────────────────────────────
0ms   XpressNet: Loco 100 speed 64 (unknown)
      → Apply to state, send to Ecos, start query
      → Pending queue: empty

50ms  XpressNet: Loco 100 speed 80
      → Query still pending, add to pending queue
      → Pending queue: [Loco 100 speed 80]

200ms Ecos query completes for Loco 100
      → Merge Ecos status with state
      → Process pending queue: apply speed 80
      → Subscribe to Loco 100 updates
      → Pending queue: empty
```

---

## Update Strategy: XpressNet Priority

### Principle
State Engine is truth. XpressNet commands apply immediately. Ecos updates backfill missing data.

### Scenarios

**Scenario 1: Unknown Loco from XpressNet**
```
Event: XpressNet sends speed 64 for unknown Loco 100
Action:
  1. Create new LocoState (speed=64, dir=1, functions=0)
  2. Send to Ecos (optimistic update)
  3. Start Ecos query for Loco 100 status
  4. (State: speed=64, ready for next XpressNet update)
  
Ecos responds (after 200ms):
  5. Merge: functions=0x0F (from Ecos), keep speed=64 (from XpressNet)
  6. Add subscription for future updates
```

**Scenario 2: Ecos Update for Known Loco**
```
Event: Ecos sends speed 32 for Loco 100 (we have speed 64)
Action:
  1. Check: Is this in outgoing queue (< 2 sec)?
     - YES: Suppress (our echo)
     - NO: Continue
  2. Compare state vs update:
     - If same: Ignore (no change)
     - If different: Broadcast to XpressNet (another throttle changed it)
  3. Merge into state engine (preserve local priority)
```

**Scenario 3: Multi-Throttle Control**
```
Throttle A (XpressNet): Controls Loco 50
  → State: speed 50, direction 1
  
Throttle B (Ecos console): Changes Loco 50 to speed 25
  → Ecos sends update: speed 25
  → Not in outgoing queue (didn't come from us)
  → BROADCAST to XpressNet (router.sendToXpressNet())
  → Throttle A's display shows speed 25 (consistent with Ecos)
```

---

## Accessory (Turnout) Management

### Tracking
- **Separate from locos** OR mixed in StateEngine (implementation detail)
- **State**: Address, current position (0/1 for turnout), last activity time
- **Lifecycle**: Same as locos (5 min timeout, expire + unsubscribe)

### Control Flow
```
XpressNet: Accessory command (address 1, position 0)
  → Check state engine
    - Unknown: Add, send to Ecos, start query
    - Known: Apply, send to Ecos
  → Ecos handles pulse/latch logic (we just command)
  
Ecos: Responds with status
  → Update state engine
  → Broadcast to XpressNet (if different)
```

### Query Support
- Bridge can query Ecos for accessory status at any time
- Used when responding to throttle queries or diagnostics
- Not time-critical (low priority operation)

### Why Track All?
Without tracking all accessories, throttle displays and Ecos get out of sync:
- User can't see actual turnout state on throttle
- If we purge an accessory from state, next command requires re-query (slow)
- Memory cost is negligible (1 Kbyte for 100 accessories)

---

## Connection Management

### Initialization
```
1. WiFi connect (SSID, password from config.h)
2. Establish TCP connection to Ecos (IP:PORT)
3. On success: Status = CONNECTED
4. On failure: Status = ERROR (retry with backoff)
```

### Heartbeat & Keep-Alive
- **Heartbeat interval**: 30 seconds (configurable, from config.h)
- **Type**: Periodic empty query or "are you alive" message
- **Purpose**: Detect dead connections early
- **Failure**: Connection drops, status = DISCONNECTED, attempt reconnect

### Reconnect Strategy
```
Initial attempt: immediate
Backoff 1: 5 sec
Backoff 2: 10 sec
Backoff 3: 20 sec
Max: 60 sec (retry every 60 sec forever)

Success: Reset to immediate for next reconnect
```

### Command Queueing During Disconnect
```
If Ecos disconnected:
  - XpressNet still works (command applied to state)
  - Ecos command: Queue it (small circular buffer, 10 items)
  - On reconnect: Flush queue to Ecos
  
If queue fills: Drop oldest items (prioritize recent)
```

---

## TCP Protocol Details

### Message Format (TBD - Ecos XML)
**Assumption**: Ecos uses XML over TCP/15471

Examples (hypothetical):
```xml
<!-- Query loco status -->
<query type="loco" address="100"/>

<!-- Subscribe to loco updates -->
<subscribe type="loco" address="100"/>

<!-- Update loco -->
<command type="loco" address="100">
  <speed>64</speed>
  <direction>1</direction>
  <functions value="0x0F"/>
</command>

<!-- Accessory command -->
<command type="accessory" address="1">
  <position>0</position>
</command>
```

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
- **Line-based** (if XML with newlines): Parse until `\n`
- **Length-prefixed** (if binary): Read length, then payload
- **Complete messages only**: Parse full XML, not fragments

---

## State Updates & Broadcasting

### When XpressNet Sends Command
```
CommandRouter::handleXpressNetCommand(address, speed, dir)
  1. Create/get LocoState
  2. Update: speed, direction, last_source=XPRESSNET, last_update_ms=now
  3. Send to Ecos immediately (via ecos_interface.sendSpeedCommand)
  4. Record in outgoing queue
  5. If unknown loco: start Ecos query
  6. Return (don't broadcast to XpressNet—already applied)
```

### When Ecos Sends Update
```
EcosInterface::handleEcosUpdate(address, speed, dir, functions)
  1. Check outgoing queue: Is this our recent command?
     - YES (< 2 sec): Suppress (echo), return
     - NO: Continue
  2. Get LocoState from state engine
  3. Compare:
     - If matches state: Ignore (no change)
     - If differs: Broadcast to XpressNet (router.sendToXpressNet)
  4. Merge into state (preserve speed from XpressNet, backfill missing data)
```

### Broadcast to XpressNet
```
If Ecos state differs from our state:
  Send update to all connected throttles (via XpressNetInterface)
  This keeps throttle displays in sync with Ecos
  
Example: Another throttle changed speed on Ecos console
  → Ecos sends update
  → We send speed change to all throttles via XpressNet
  → All throttles now show consistent speed
```

---

## OLED Display Integration

### Status Page
Show:
```
Ecos: <status>     (CONNECTED / DISCONNECTED / ERROR)
IP: <ip>:<port>    (e.g., "192.168.1.100:15471")
Uptime: <minutes>  (time since bridge boot)
Active locos: <count>
```

### Error Display
- "Ecos connection failed" (red background if supported)
- "WiFi disconnected"
- "TCP timeout"

### Do NOT Show
- Individual loco sync status (too verbose, low priority)
- Per-protocol state details (keep simple)

### Refresh
- Update on status changes (connection, disconnect)
- Update every 5 seconds if connected (refresh IP/uptime)

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
    
    // Phase 4: Process any pending commands
    if (query_timer.shouldExecute()) {
        processPendingQueries();
    }
}
// Total time: < 50ms (doesn't block XpressNet)
```

### No Blocking Operations
- No `delay()`
- No synchronous TCP waits
- No parsing entire buffer at once (parse incrementally)
- No disk/EEPROM I/O

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
          → CommandRouter.expungeInactiveLocos() runs
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
Detect: No heartbeat response for 10 seconds
  → Close connection
  → Status = DISCONNECTED
  → Start WiFi reconnect sequence
  
On reconnect:
  → Reestablish TCP to Ecos
  → Flush any queued commands
  → Resubscribe to all locos in state engine
```

### Ecos Unresponsive
```
Query sent, no response for 5 seconds
  → Treat as connection loss
  → Close socket
  → Attempt reconnect
  
Pending commands:
  → Stay queued
  → Retry when Ecos comes back
```

---

## Implementation Files

### New Files
- `protocols/ecos/ecos_interface.cpp` (~600 lines)
  - TCP connection management
  - XML message parsing
  - Command queueing
  - Heartbeat/keep-alive
  
- `protocols/ecos/ecos_message_parser.h/cpp` (~300 lines)
  - XML parsing (simple, no external library)
  - Extract address, speed, direction, functions
  - Build outgoing commands

### Modified Files
- `command_router.cpp`: Add `handleEcosUpdate()`, `broadcastToXpressNet()`
- `state_engine.h/.cpp`: Add accessory tracking (if separate from locos)
- `xpressnet_ecos_bridge.ino`: Initialize Ecos interface, set router reference
- `config.h`: Add Ecos timeouts, heartbeat interval, backoff params
- `display/oled_display.cpp`: Add Ecos status page

---

## Testing Strategy

### Mock Ecos Server
Create simple TCP server that:
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
8. **Buffer overflow**: Large XML response, handle correctly without crash

---

## Performance Targets

- **Query latency**: 200-500ms (acceptable, not time-critical)
- **Update broadcast**: < 100ms (should see Ecos changes on XpressNet throttles quickly)
- **Memory footprint**: < 5KB (TCP buffer + queue + state tracking)
- **CPU load**: < 5% of main loop time (XpressNet is still highest priority)
- **Throughput**: 10+ commands/second to/from Ecos (should handle typical throttle usage)

---

## Future Enhancements (Post-Phase 4)

- Block occupancy/feedback from Ecos
- Consist (double-header) support
- Advanced function mapping
- Speed step 128 (currently DCC 14-step)
- Accessory feedback/sensing
- WiFi credential storage in EEPROM
- Web-based configuration UI

---

**Document Version**: 1.0  
**Date**: 2026-07-22  
**Author**: Claude + Frikkie  
**Status**: Ready for implementation review
