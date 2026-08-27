/*
 * EEPROM Config - Persisted Settings Layout and Validation
 *
 * Defines the small set of settings that survive reboots/reflashes
 * (Phase 6 step 1, scope finalized 2026-08-27, revised during step 2's
 * design 2026-08-27): WiFi SSID/password, Ecos IP, XNet bus timeout,
 * loco inactivity timeout, and the bridge's own static IP. Everything
 * else (pins, baud rate, buffer sizes, protocol/debug enable flags)
 * stays config.h compile-time only - see CLAUDE.md Phase 6 step 1 for
 * the full reasoning.
 *
 * Revision: the bridge's static IP was originally optional (DHCP by
 * default, an off/on toggle) but is now mandatory - decided during
 * Phase 6 step 2 design once it was clear Z21 LAN (a future WLANmaus
 * client, Phase 6 step 4) will need to know the bridge's own address
 * reliably, and a fixed static IP is simpler to depend on than chasing
 * a DHCP lease. This bumped EEPROM_CONFIG_VERSION to 2 (a real schema
 * change, not just a new field) - any EEPROM written by the v1 schema
 * fails validation and reseeds, which is correct/expected here since v1
 * shipped only hours earlier in the same work session, before any real
 * deployment.
 *
 * This header has no EEPROM.h/Arduino dependency - the struct and its
 * validation/default-seeding logic are plain, natively testable code.
 * Only eeprom_store.h/.cpp (the actual EEPROM.get()/put() calls) is
 * Arduino-only, same hardware-coupled boundary as ecos_interface.cpp.
 */

#ifndef EEPROM_CONFIG_H
#define EEPROM_CONFIG_H

#include <cstdint>
#include <cstddef>

// Bumped whenever the struct layout changes below, so a struct written by
// an older/incompatible firmware version is treated as invalid (reseeded
// from config.h defaults) instead of being misinterpreted byte-for-byte.
#define EEPROM_CONFIG_VERSION 2

// Arbitrary sentinel distinguishing a real saved config from blank/erased
// flash (which reads back as 0xFF bytes) or a version-0/garbage struct.
#define EEPROM_CONFIG_MAGIC 0xEC05B01DUL

struct EepromConfig {
    uint32_t magic;
    uint16_t version;

    char wifi_ssid[33];      // 32 chars + null (802.11 max SSID length)
    char wifi_password[65];  // 64 chars + null (WPA2 max PSK length)
    char ecos_ip[16];        // "255.255.255.255" + null

    uint32_t xnet_bus_timeout_ms;
    uint32_t loco_inactivity_timeout_ms;

    // Bridge's own network identity - mandatory, no DHCP fallback (see
    // revision note above). No compile-time default exists (it's
    // inherently network-specific), so these stay blank until Setup
    // Mode is used to fill them in - which blank/invalid EEPROM already
    // forces on first boot, so normal operation never actually sees
    // these empty in practice.
    char bridge_ip[16];
    char bridge_gateway[16];
    char bridge_subnet[16];

    // Computed over every field above (not itself) - must stay last.
    uint16_t checksum;
};

/**
 * Populate `config` with the compile-time defaults from config.h /
 * wifi_credentials.local.h - used to seed EEPROM on first boot, and as
 * the fallback whenever stored EEPROM contents fail validation.
 */
void eepromConfigLoadDefaults(EepromConfig& config);

/**
 * Checksum over every field of `config` except the checksum field itself.
 * Deliberately simple (catches blank/torn/corrupted writes, not a
 * cryptographic integrity guarantee) - EEPROM.get() either reads back
 * exactly what was written or reads erased-flash 0xFF bytes, there's no
 * adversarial tampering to defend against here.
 */
uint16_t eepromConfigChecksum(const EepromConfig& config);

/**
 * True if magic/version/checksum all check out - i.e. this looks like a
 * real, uncorrupted config written by this firmware version, not blank/
 * erased flash or a struct from an incompatible version. Does NOT check
 * whether every field has a real value - see eepromConfigIsComplete() for
 * that.
 */
bool eepromConfigIsValid(const EepromConfig& config);

/**
 * True if `config` is not just structurally valid but also has real
 * values in every field required for normal operation - specifically the
 * bridge's own static IP (bridge_ip/gateway/subnet), which has no
 * compile-time default and so can be validly checksummed while still
 * blank right after a first-boot reseed (confirmed live 2026-08-27: a
 * freshly-seeded config passes eepromConfigIsValid() with blank bridge
 * fields, which would otherwise let normal operation start with an
 * incomplete mandatory setting instead of forcing Setup Mode). Used to
 * decide whether to force Setup Mode even when the stored struct itself
 * is uncorrupted.
 */
bool eepromConfigIsComplete(const EepromConfig& config);

#endif  // EEPROM_CONFIG_H
