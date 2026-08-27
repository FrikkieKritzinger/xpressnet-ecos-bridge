/*
 * EEPROM Store - Hardware-Backed Read/Write for EepromConfig
 *
 * Thin wrapper around the ESP8266 EEPROM.h library (flash-emulated).
 * Arduino-only - excluded from env:native, same hardware-coupled boundary
 * as ecos_interface.cpp/xpressnet_interface.cpp. The struct layout,
 * default-seeding, and validation logic it depends on lives in
 * eeprom_config.h/.cpp instead, which IS natively testable.
 */

#ifndef EEPROM_STORE_H
#define EEPROM_STORE_H

#include "eeprom_config.h"

/**
 * Load config from EEPROM. If the stored data is missing, corrupt, or
 * from an incompatible firmware version (eepromConfigIsValid() fails),
 * falls back to compile-time defaults from config.h and writes them to
 * EEPROM once, so subsequent boots read a valid struct without needing
 * to reseed every time.
 */
void eepromStoreLoad(EepromConfig& config);

/**
 * Persist `config` to EEPROM (recomputes the checksum first, so callers
 * never need to maintain it themselves after changing a field).
 */
void eepromStoreSave(EepromConfig& config);

#endif  // EEPROM_STORE_H
