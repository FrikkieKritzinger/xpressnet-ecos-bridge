/*
 * EEPROM Config - Persisted Settings Layout and Validation
 *
 * Defines the small set of settings that survive reboots/reflashes
 * (Phase 6 step 1, scope finalized 2026-08-27): WiFi SSID/password, Ecos
 * IP, XNet bus timeout, loco inactivity timeout, and an optional static
 * IP for the bridge itself. Everything else (pins, baud rate, buffer
 * sizes, protocol/debug enable flags) stays config.h compile-time only -
 * see CLAUDE.md Phase 6 step 1 for the full reasoning.
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
#define EEPROM_CONFIG_VERSION 1

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

    // Bridge's own network identity. Off (DHCP, current/default behavior)
    // unless explicitly enabled - nothing reads bridge_ip/gateway/subnet
    // when use_static_ip is false.
    bool use_static_ip;
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
 * erased flash or a struct from an incompatible version.
 */
bool eepromConfigIsValid(const EepromConfig& config);

#endif  // EEPROM_CONFIG_H
