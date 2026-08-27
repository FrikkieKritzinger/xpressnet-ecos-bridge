/*
 * EEPROM Store Implementation - ESP8266 EEPROM.h Read/Write
 */

#include "eeprom_store.h"
#include <EEPROM.h>
#include "utils/debug.h"

bool eepromStoreLoad(EepromConfig& config) {
    EEPROM.begin(sizeof(EepromConfig));
    EEPROM.get(0, config);

    if (eepromConfigIsValid(config)) {
        DEBUG_PRINTF("EEPROM: loaded valid config (version %u)\n", config.version);
        return true;
    }

    DEBUG_PRINTF("EEPROM: no valid config found - seeding defaults from config.h\n");
    eepromConfigLoadDefaults(config);
    eepromStoreSave(config);
    return false;
}

void eepromStoreSave(EepromConfig& config) {
    config.checksum = eepromConfigChecksum(config);
    EEPROM.put(0, config);
    if (EEPROM.commit()) {
        DEBUG_PRINTF("EEPROM: config saved (%u bytes)\n", (unsigned)sizeof(EepromConfig));
    } else {
        DEBUG_PRINTF("EEPROM: ERROR - commit() failed, config NOT persisted\n");
    }
}
