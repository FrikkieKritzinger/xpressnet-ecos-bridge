/*
 * XpressNet-Ecos Bridge - Master Configuration File
 * 
 * This is the "God" config file - all hardware selection and feature toggles here.
 * This is the ONLY file users should need to modify for their hardware setup.
 * 
 * Compile-time configuration via preprocessor directives.
 * Features not enabled here are compiled out entirely (zero runtime overhead).
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "version.h"

// ============================================================================
// FIRMWARE IDENTITY (Phase 6 step 3)
// ============================================================================
// FIRMWARE_VERSION (version.h) is the primary, user-facing identifier -
// a conventional version number, bumped by hand per release, shown on
// the OLED and Setup Mode pages. FIRMWARE_BUILD_INFO is a secondary,
// more precise compile date/time - useful during development for
// disambiguating builds that share the same not-yet-bumped version
// number, shown alongside the version on the setup page where there's
// room, but not on the space-constrained OLED line. Re-invoking
// __DATE__/__TIME__ in each file that uses this (rather than a single
// shared global) can differ by a second or two across translation units
// compiled at slightly different moments within the same build - a
// cosmetic non-issue, not worth a shared-global workaround for.
#define FIRMWARE_BUILD_INFO (__DATE__ " " __TIME__)

// ============================================================================
// PROTOCOL ENABLEMENT (Compile-time toggles)
// ============================================================================
// Set 1 to enable, 0 to disable. Disabled features = no code, no overhead.

#define ENABLE_XPRESSNET        1    // XpressNet interface (hardwired, timing-critical)
#define ENABLE_ECOS_LAN         1    // Ecos LAN interface (WiFi, text object protocol)
#define ENABLE_LOCONET          1    // LocoNet interface (Phase 7 item 3 - in development)
#define ENABLE_Z21_LAN          1    // Z21 LAN protocol (Phase 6 step 4 - WLANmaus support)
#define ENABLE_OLED_DISPLAY     1    // OLED status display

// ============================================================================
// XPRESSNET CONFIGURATION
// ============================================================================
// XpressNet uses non-standard 9+1 bit serial protocol
// Requires MAX485 RS485 interface module

#if ENABLE_XPRESSNET
    // Pin assignments (Wemos D1 Mini pin names)
    // Hardware: MAX485 module, half-duplex single-wire (DI+RO tied together on the
    // module), DE+RE also tied together. Matches Philipp Gahtow's XpressNetMaster
    // library wiring pattern exactly (its ESP8266 example calls setup(Loco128, D6, D0)).
    #define XPRESSNET_DATA_PIN      12   // D6 - half-duplex data (SoftwareSerial RX+TX, same pin)
    #define XPRESSNET_CONTROL_PIN   16   // D0 - MAX485 DE+RE tied together (HIGH=TX, LOW=RX)

    // Serial configuration
    // 62500 baud, 8 data bits + parity-as-9th-bit (SWSERIAL_8S1), 1 stop bit -
    // this is the real Lenz XpressNet wire rate (confirmed against XpressNetMaster's
    // own SoftwareSerial.begin() call), NOT the 9600 baud previously assumed here.
    // The parity bit distinguishes a "call byte" from a data byte for bus arbitration.
    #define XPRESSNET_BAUD          62500
    
    // Timing and lifecycle
    #define XPRESSNET_POLL_INTERVAL 20     // ms - how often to check for new messages

    // How long with no received XpressNet message before status flips to
    // DISCONNECTED. Real bug found on hardware 2026-08-03: at 5000ms this
    // flapped to DISCONNECTED during completely normal single-throttle
    // idling. Lenz XpressNet's call-byte polling has no "nothing to report"
    // acknowledgment - a throttle with nothing new just stays silent - so
    // there's no passive signal to distinguish "no throttle" from "idle
    // throttle" below some threshold; picking this value is a real judgment
    // call, not a bug to fix in logic. 120s was chosen for a single-throttle
    // layout with long gaps between inputs. With multiple throttles active
    // there's more background traffic and this could likely be tightened.
    // If a genuine disconnect (bus fault, throttle unplugged) ever needs to
    // be detected faster than this, that's the tradeoff to revisit here.
    // Phase 6 step 1 (2026-08-27): this value is now just the EEPROM
    // *default* - eeprom_config.cpp seeds it on first boot, but the live
    // value XpressNetInterface actually uses comes from EEPROM via
    // setBusTimeoutMs(), so it can be retuned per layout without a reflash.
    #define XPRESSNET_BUS_TIMEOUT   120000

    // 1 = after a change from another protocol, also inject the loco's state
    // onto the bus as if the owning MultiMaus had sent it (the "stolen" display
    // refresh, Phase 5 step 9). Experiment 2026-10-26: those extra transmissions
    // coincided with bursts of XpressNet frames dropped for bad checksums.
    #define XPRESSNET_PUSH_OWNING_SLOT   0

    // 1 = answer a throttle's function frame with the loco's current info, in
    // the throttle's own polling window (a command-station confirmation).
    #define XPRESSNET_CONFIRM_FUNCTION_REPLY   1
#endif

// ============================================================================
// ECOS LAN CONFIGURATION
// ============================================================================
// ESU Ecos command system - communicates via plain-text line-based object protocol
// over TCP/WiFi (NOT XML). Protocol: request(id,mode), set(id,prop[val]), <EVENT>/<REPLY>/<END> framing
// Requires WiFi connectivity

#if ENABLE_ECOS_LAN
    // Network configuration - EEPROM default (Phase 6 step 1, 2026-08-27):
    // seeded on first boot, live value comes from EEPROM via
    // EcosInterface::setConfig(). Bridge's own static IP (mandatory, no
    // DHCP - see EepromConfig) has no compile-time equivalent at all.
    #define ECOS_IP                 "192.168.0.50"   // IP address of your Ecos (hostname ECOS)
    #define ECOS_PORT               15471            // Standard Ecos port (do not change)

    // WiFi configuration - real SSID/password live in wifi_credentials.local.h,
    // a gitignored file (never committed - see wifi_credentials.local.h.example
    // for the template). EEPROM default (Phase 6 step 1) - seeded on first
    // boot, live value comes from EEPROM via EcosInterface::setConfig().
    #if __has_include("wifi_credentials.local.h")
        #include "wifi_credentials.local.h"
    #else
        #error "Missing wifi_credentials.local.h - copy wifi_credentials.local.h.example to wifi_credentials.local.h and fill in your real WiFi SSID/password."
    #endif
    
    // Timeouts and intervals
    // WiFiClient::connect() blocks the main loop for up to this long on every
    // attempt (real bug found on hardware 2026-08-03: this constant existed
    // but was never actually applied to wifi_client, so connect() silently
    // used the ESP8266 core's own 5000ms default instead - when Ecos was
    // unreachable, every reconnect attempt froze the whole loop, including
    // XpressNet polling, for up to 5s at a time, matching BUS_TIMEOUT closely
    // enough to keep XpressNet stuck DISCONNECTED and starve the MultiMaus of
    // call bytes long enough to throw err13). Real LAN connects complete in
    // single-digit ms, so this only needs to bound the failure case.
    #define ECOS_TIMEOUT                300             // TCP connection timeout (ms)
    // Real gap found on hardware 2026-08-03: unplugging Ecos's Ethernet
    // cable (as opposed to Ecos rebooting/closing the socket cleanly) never
    // generates a TCP reset - our side has no way to notice except this
    // "no data received" watchdog, which used to allow up to 45s of
    // silently-lost commands (current_status still reported CONNECTED, and
    // findEcosObjectId() still resolved from the stale-but-not-yet-cleared
    // address map, right up until this timeout finally tripped). Tightened
    // from 30000/45000 to close that window to ~10s - LAN round-trip is
    // low single-digit ms, so 5s of heartbeat-cadence margin is still very
    // generous against false-positives from WiFi jitter, just far less
    // patient about a genuinely dead connection.
    #define ECOS_HEARTBEAT_INTERVAL     5000            // Heartbeat query every X ms
    // Must exceed ECOS_HEARTBEAT_INTERVAL with margin for the round trip, or the
    // "no data" watchdog trips before the heartbeat ever gets a reply back.
    #define ECOS_MESSAGE_TIMEOUT        (ECOS_HEARTBEAT_INTERVAL + 5000)   // No-data disconnect threshold (ms)
    #define ECOS_RECONNECT_INTERVAL     10000           // Backoff retry interval (ms)
    #define ECOS_ADDRESS_MAP_REFRESH_INTERVAL 600000    // Refresh loco list every 10 minutes

    // Buffers
    #define MAX_ECOS_OBJECTS            100             // Max locomotives in address map
    // Commands queued while the object ID is unresolved - covers both "loco
    // not yet in the address map" and "Ecos currently disconnected" (the
    // address map is empty in both cases). Upserted by address, so this
    // bounds concurrently-queued distinct locos, not total speed changes.
    #define MAX_PENDING_QUERIES         5

    // Phase 7 step 2: accessory/turnout address map (DCC address <-> Ecos
    // object ID), separate from the locomotive map above since both share
    // the same "address-map-style" reply shape (object ID + addr, no
    // speed/dir/func) and need to land in different arrays.
    #define MAX_ECOS_ACCESSORIES        40

    // Threshold used to tell a locomotive's address-map reply apart from an
    // accessory's - both queryObjects(10, addr) and queryObjects(11, addr)
    // produce identically-shaped replies (object ID + addr, nothing else),
    // and the real protocol doesn't tag which command a reply answers.
    // Confirmed via a real raw capture against this project's own Ecos
    // (2026-09-01, Phase 7 step 2 investigation): locomotive object IDs sat
    // in the 1000s, accessory object IDs at 20000+ - a real, structural
    // separation from Ecos's own per-manager object ID allocation (LokManager
    // id=10 and SchaltartikelManager id=11 are different managers with
    // separate ID pools), not a coincidence of this one layout's creation
    // order. Revisit if a real layout is ever seen with locomotive IDs at or
    // above this threshold.
    #define ECOS_ACCESSORY_ID_MIN       10000
#endif

// ============================================================================
// LOCONET CONFIGURATION (Future)
// ============================================================================

#if ENABLE_LOCONET
    // Wiring follows Philipp Gahtow's D1 Mini LocoNet design (confirmed
    // against his Z21 firmware, LNHwSerial.h): receive on the HARDWARE UART0
    // RX pin (GPIO3 - immune to the interrupt latency that XpressNet's
    // SoftwareSerial causes), transmit on D3/GPIO0 through a BC547 that pulls
    // the bus low, so the TX line is INVERTED (GPIO high = bus low/"space",
    // GPIO low = bus released).
    //
    // Consequences, all inherent to this pin choice:
    // - UART0 is the LocoNet receiver, so Serial runs at LOCONET_BAUD and USB
    //   serial debug is unreliable; debug output goes to the network log
    //   (utils/net_log.h) instead - see DEBUG_TO_SERIAL below.
    // - GPIO0 is a boot-strap pin: at reset the base resistor holds the bus
    //   low until setup() releases it, so release it first thing in setup().
    // - Flashing over USB needs the D1 Mini removed from its socket (or use
    //   OTA from Setup Mode).
    #define LOCONET_RX_PIN          3    // GPIO3 / UART0 RX (hardware serial, not configurable)
    #define LOCONET_TX_PIN          0    // D3 / GPIO0, inverted, through BC547
    #define LOCONET_BAUD            16660   // Spec: 60us bit time = 16.66 kbaud +/-1.5%
    #define LOCONET_RX_BUFFER_BYTES 512     // UART0 software RX buffer (default 256)

    // Status reads CONNECTED while a valid frame was seen this recently.
    // LocoNet has no polling, so an idle layout is legitimately silent;
    // a throttle pings well inside this (spec: about every 100 s).
    #define LOCONET_BUS_TIMEOUT_MS  120000

    // Bring-up aid: also log each batch of raw received bytes (one line per
    // update() pass that received anything). Shows garbage if RX polarity or
    // baud is wrong, which framed-message logging alone would hide. Turn off
    // once LocoNet is proven - it is noisy.
    #define LOCONET_RAW_TRACE       0

    // Transmit timing, from the Digitrax spec ("NETWORK Timing"/"Access"):
    // 1 bit = 60us. A device waits for carrier-detect backoff (20 bit times =
    // 1.2 ms of idle) before seizing the bus; the master needs no extra delay
    // (other devices add 6 bit times). After a collision the transmitter holds
    // the line low for a 15 bit-time break. 25 attempts before giving up, and
    // a device waits 250 ms after start-up before its first transmission.
    #define LOCONET_CD_BACKOFF_US      1200
    #define LOCONET_BREAK_US           900
    #define LOCONET_TX_RETRIES_MAX     25
    #define LOCONET_TX_STARTUP_DELAY_MS 250
    // How long to wait for our own transmitted byte to echo back on RX.
    #define LOCONET_ECHO_TIMEOUT_US    1500

    // How a change made by another protocol reaches a LocoNet throttle. A
    // Digitrax UT6 did NOT update its display from bare OPC_LOCO_SPD/DIRF/SND
    // frames (live test 2026-10-23); the slot read below is what is being
    // tried. Left switchable so each can be tested alone from the network
    // flasher.
    #define LOCONET_PUSH_SLOT_READ     0   // coalesced OPC_SL_RD_DATA per changed slot (UT6 ignores it)

    // What DOES refresh a UT6 (live 2026-10-23): hold the data line low for
    // about 200 ms (150 ms did nothing, 200/250/400 ms all worked). Every
    // device treats that as a bus disconnect and re-reads its slot, so the UT6
    // shows the loco's real state and keeps the loco (one screen flash). It is
    // done only when another protocol has changed a loco a throttle is
    // holding, at most every LOCONET_RESYNC_INTERVAL_MS, and only after the
    // bus has been quiet for LOCONET_RESYNC_QUIET_MS so it never interrupts
    // someone turning the knob. 0 disables it.
    #define LOCONET_RESYNC_INTERVAL_MS 5000
    #define LOCONET_RESYNC_HOLD_MS     250    // margin above the ~200 ms the UT6 needs
    #define LOCONET_RESYNC_QUIET_MS    2000
    #define LOCONET_PUSH_BYTE_FRAMES   0   // also OPC_LOCO_SPD/DIRF/SND for every change
#endif

// ============================================================================
// Z21 LAN CONFIGURATION (Phase 6 step 4)
// ============================================================================
// UDP server emulating a Z21 command station, so a Roco WLANmaus (or any
// Z21-compatible app) can connect directly to this bridge. Confirmed
// against the official Z21 LAN Protocol Specification v1.13 (docs/
// z21-lan-protokoll-en.pdf, gitignored - redistribution-restricted like
// the Ecos PDF): port 21105 is correct, and the base Z21 protocol has no
// documented broadcast auto-discovery (only the separate "Z21 pro LINK"
// accessory does) - clients are expected to already know the server's
// IP, confirming the bridge's own static IP (Phase 6 step 2) as the
// right call, not incidental.

#if ENABLE_Z21_LAN
    #define Z21_PORT                    21105  // Official Z21 UDP port - do not change

    // Fixed-size client session table - no dynamic allocation, matching
    // the rest of this project. Unlike XpressNet (a shared bus where
    // multiple physical throttles already see each other's traffic for
    // free), Z21 is UDP client-server - this bridge itself must track
    // each connected client (WLANmaus, phone app, etc.) and broadcast
    // state to all of them individually.
    #define MAX_Z21_CLIENTS                  4  // Concurrent Z21 clients
    #define MAX_Z21_SUBSCRIBED_PER_CLIENT    8  // Loco addresses per client (spec allows up to 16; capped lower for RAM)

    // Spec: "Each client is expected to communicate with the Z21 once per
    // minute, otherwise it will be removed from the list of active
    // participants."
    #define Z21_CLIENT_TIMEOUT_MS       60000
#endif

// ============================================================================
// OLED DISPLAY CONFIGURATION
// ============================================================================
// SSD1306 I2C OLED display - shows status and information

#if ENABLE_OLED_DISPLAY
    // I2C pins (standard for Wemos D1 Mini)
    #define OLED_SDA_PIN            4     // D2 - I2C Data
    #define OLED_SCL_PIN            5     // D1 - I2C Clock
    
    // Display specifications
    #define OLED_ADDRESS            0x3C  // I2C address (0x3C or 0x3D)
    #define OLED_WIDTH              128   // pixels
    #define OLED_HEIGHT             64    // pixels (0.96" display)
    
    // Timing
    #define OLED_UPDATE_INTERVAL    500   // ms - how often to refresh display

    // Boot splash: shown once from display init, while setup()/loop() carry
    // on connecting XpressNet/Ecos in the background - not a "wait until
    // connected" gate, just a fixed-length logo screen to let power settle
    // before switching to the regular status pages.
    #define OLED_BOOT_LOGO_DURATION_MS  10000
#endif

// ============================================================================
// SETUP MODE CONFIGURATION (Phase 6 step 2)
// ============================================================================
// AP mode + web config page for editing the EEPROM-persisted settings (see
// eeprom_config.h). All 6 fields (WiFi SSID/password, Ecos IP, XNet bus
// timeout, loco inactivity timeout, bridge static IP) are only editable
// here, in this dedicated mode - never live during normal operation.
//
// Entered via: (1) blank/invalid EEPROM (first boot - no known WiFi to even
// attempt), (2) holding a dedicated momentary pushbutton (D7/GPIO13 to
// GND, using INPUT_PULLUP internally - no external resistor needed) for
// SETUP_BUTTON_HOLD_MS while running normally, or (3)
// WIFI_FALLBACK_TIMEOUT_MS of continuous WiFi disconnection (e.g. router
// password changed) - a defense-in-depth auto-recovery so a WiFi credential
// mistake never requires a USB reflash to fix.
//
// Real correction (2026-08-27): originally planned to reuse the Wemos D1
// Mini's onboard button assuming it was wired to GPIO0 (a "FLASH" button,
// common on other ESP8266 dev boards). Confirmed live that this specific
// board has only one onboard button, wired directly to RST/EN - a genuine
// hardware reset that bypasses all running code, silkscreened "RESET",
// not readable/holdable in software at all. A real dedicated button is
// required; D7/GPIO13 was chosen specifically to avoid GPIO0/GPIO2/GPIO15,
// which all have boot-strapping significance (pulling them low/high at
// power-on selects boot mode) - GPIO13 has none, so there's no equivalent
// "don't hold this during power-up" caveat to document or get wrong.

#define SETUP_BUTTON_PIN            13      // D7/GPIO13 - dedicated pushbutton to GND (new wiring required)
#define SETUP_BUTTON_HOLD_MS        3000    // Hold duration to trigger setup mode
#define WIFI_FALLBACK_TIMEOUT_MS    300000  // 5 min continuous WiFi disconnection -> auto setup mode
#define SETUP_MODE_AP_SSID          "XNetBridge-Setup"
#define SETUP_MODE_AP_PASSWORD      ""      // Open network - matches this project's no-auth posture elsewhere; the button/timeout triggers already require physical presence or an already-broken connection
#define SETUP_MODE_WEB_PORT         80

// ============================================================================
// STATE ENGINE CONFIGURATION
// ============================================================================
// In-memory locomotive state tracking (no persistence)

#define MAX_LOCOS                   50    // Maximum locomotives in state engine
// EEPROM default (Phase 6 step 1, 2026-08-27) - seeded on first boot, live
// value comes from EEPROM via StateEngine::setInactivityTimeoutMs().
#define LOCO_INACTIVITY_TIMEOUT     300000  // 5 minutes - remove if no updates
#define LOCO_EXPIRY_CHECK_INTERVAL  30000   // Check for expired locos every X ms

// CommandRouter's known-accessory-state cache (Phase 7 step 2) - the best
// currently-known straight/diverging position per address, from whichever
// source (a handheld's command, or Ecos's own confirmation) touched it most
// recently. Answers on-demand state queries (Z21 GET_TURNOUT_INFO, XpressNet
// Accessory Info Request) - not gated behind ENABLE_ECOS_LAN since it's used
// by the throttle-facing protocols regardless of which backend is active.
#define MAX_KNOWN_ACCESSORIES       40

// ============================================================================
// ECHO PREVENTION CONFIGURATION
// ============================================================================
// How long CommandRouter::wasRecentSource() attributes an Ecos-originated
// confirmation to whichever throttle-facing protocol most recently sent a
// command for that address, so broadcastCommand()'s ECOS branch doesn't
// echo it straight back (e.g. XpressNet visibly flashing "stolen" for its
// own command). Real bug found live 2026-08-28: the original 500ms was too
// short for Ecos's real confirmation round trip under real load (TCP
// latency, paced baseline queries), so late echoes of a protocol's own
// command were misattributed as independent Ecos-side changes. Widened
// with margin - the tradeoff (rarely, a genuine independent Ecos-direct
// change made within this window of recent throttle activity might be
// attributed to the throttle instead) is far less disruptive than the
// false-positive "stolen" flashes the short window caused.
#define ECOS_ECHO_ATTRIBUTION_WINDOW_MS   4000   // ms

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
// Enable/disable serial debug output

#define ENABLE_DEBUG                1     // Master debug enable
#define DEBUG_BAUD                  115200  // Serial monitor speed (used only when LocoNet is disabled)

// Network debug log (utils/net_log.h): all debug output is also streamed
// over TCP to one client on NET_LOG_PORT once WiFi is up, e.g.
// `ncat <bridge-ip> 23`. Read-only, no authentication (matches this
// project's posture elsewhere). Needed because LocoNet takes over UART0.
#define ENABLE_NET_LOG              1

// Development-only network firmware updater (utils/dev_ota.h). OFF here and
// in every release build; only the `wemos_dev` PlatformIO environment turns
// it on (-DENABLE_DEV_OTA=1). It has no authentication.
#ifndef ENABLE_DEV_OTA
#define ENABLE_DEV_OTA              0
#endif
#define DEV_OTA_PORT                8266
#define NET_LOG_PORT                23
#define NET_LOG_BUFFER_SIZE         2048  // bytes of history kept for late-connecting clients

// Mirror debug output to Serial as well. Off while LocoNet owns UART0: at
// LOCONET_BAUD (16660) Serial.print() blocks ~7x longer than at 115200 once
// its FIFO fills, which is exactly the slow-output-in-a-hot-path hazard.
#if ENABLE_LOCONET
    #define DEBUG_TO_SERIAL         0
#else
    #define DEBUG_TO_SERIAL         1
#endif

// Specific debug output (only if ENABLE_DEBUG is 1)
#define DEBUG_STARTUP               1     // Show startup messages
#define DEBUG_XPRESSNET             1     // Show XpressNet messages
#define DEBUG_ECOS                  1     // Show Ecos TCP messages
#define DEBUG_STATE_ENGINE          1     // Show state engine operations
#define DEBUG_ECHO_PREVENTION       1     // Show echo prevention decisions
#define DEBUG_LOCONET               1     // Show LocoNet frames
#define DEBUG_TIMING                0     // Show loop timing (verbose)
#define DEBUG_MEMORY                0     // Show heap usage (verbose)

// ============================================================================
// ADVANCED CONFIGURATION (Usually don't change these)
// ============================================================================

// Timing thresholds
#define MAIN_LOOP_TIMEOUT           500   // ms - prevent watchdog reset
#define WATCHDOG_RESET              yield()  // ESP8266 specific

// Memory settings
#define MAX_TCP_BUFFER_SIZE         1024  // bytes - Ecos message buffer
#define MAX_XNET_MESSAGE_LENGTH     32    // bytes - XpressNet message

// Feature flags for future expansion
// EEPROM config (Phase 6 step 1) and the web config/setup mode (Phase 6
// step 2) are both implemented unconditionally - see eeprom_config.h/
// eeprom_store.h and setup_mode.h. Not behind toggles: EEPROM is small
// core infrastructure, and the web server only runs while actually in
// Setup Mode (not during normal operation), so there's no always-on cost
// to disable.

// ============================================================================
// VALIDATION - Don't modify below this line
// ============================================================================

#if !ENABLE_XPRESSNET && !ENABLE_ECOS_LAN && !ENABLE_LOCONET && !ENABLE_Z21_LAN
    #error "At least one protocol must be enabled!"
#endif

#if ENABLE_XPRESSNET && !ENABLE_OLED_DISPLAY
    #warning "XpressNet without display - consider adding OLED for status feedback"
#endif

#endif  // CONFIG_H
