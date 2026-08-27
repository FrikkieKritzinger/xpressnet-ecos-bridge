/*
 * EEPROM Config Implementation - Defaults, Checksum, Validation
 *
 * Pure logic, no EEPROM.h/Arduino dependency - see eeprom_config.h.
 */

#include "eeprom_config.h"
#include <cstring>
#include "config.h"

void eepromConfigLoadDefaults(EepromConfig& config) {
    memset(&config, 0, sizeof(config));

    config.magic = EEPROM_CONFIG_MAGIC;
    config.version = EEPROM_CONFIG_VERSION;

    strncpy(config.wifi_ssid, WIFI_SSID, sizeof(config.wifi_ssid) - 1);
    strncpy(config.wifi_password, WIFI_PASSWORD, sizeof(config.wifi_password) - 1);
    strncpy(config.ecos_ip, ECOS_IP, sizeof(config.ecos_ip) - 1);

    config.xnet_bus_timeout_ms = XPRESSNET_BUS_TIMEOUT;
    config.loco_inactivity_timeout_ms = LOCO_INACTIVITY_TIMEOUT;

    // Bridge's own static IP has no compile-time equivalent (inherently
    // network-specific) - left blank here. Blank/invalid EEPROM already
    // forces Setup Mode on the very next boot, so normal operation never
    // actually runs with these empty.
    config.bridge_ip[0] = '\0';
    config.bridge_gateway[0] = '\0';
    config.bridge_subnet[0] = '\0';

    config.checksum = eepromConfigChecksum(config);
}

uint16_t eepromConfigChecksum(const EepromConfig& config) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&config);
    size_t len = offsetof(EepromConfig, checksum);

    // Rotate-xor accumulator - cheap, deterministic, sensitive to both
    // byte values and their position (so e.g. a swapped pair of bytes
    // doesn't checksum the same as the original).
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = static_cast<uint16_t>((sum << 1) | (sum >> 15));
        sum = static_cast<uint16_t>(sum ^ bytes[i]);
    }
    return sum;
}

bool eepromConfigIsValid(const EepromConfig& config) {
    if (config.magic != EEPROM_CONFIG_MAGIC) return false;
    if (config.version != EEPROM_CONFIG_VERSION) return false;
    if (config.checksum != eepromConfigChecksum(config)) return false;
    return true;
}

bool eepromConfigIsComplete(const EepromConfig& config) {
    if (!eepromConfigIsValid(config)) return false;
    if (config.bridge_ip[0] == '\0') return false;
    if (config.bridge_gateway[0] == '\0') return false;
    if (config.bridge_subnet[0] == '\0') return false;
    return true;
}
