/*
 * Ecos Protocol Implementation - Command Builders & Parsers
 */

#include "ecos_protocol.h"
#include <Arduino.h>
#include <cstdio>

// ============================================================================
// COMMAND BUILDERS
// ============================================================================

uint16_t ecosBuildQueryObjectsCmd(char* buffer, uint16_t buffer_size) {
    if (!buffer || buffer_size < 50) return 0;
    int len = snprintf(buffer, buffer_size, "queryObjects(10, addr, name)\n");
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildRequestCmd(char* buffer, uint16_t buffer_size,
                            uint16_t object_id, const char* mode, bool force_control) {
    if (!buffer || !mode || buffer_size < 60) return 0;

    int len;
    if (force_control && strcmp(mode, ECOS_MODE_CONTROL) == 0) {
        len = snprintf(buffer, buffer_size, "request(%u, %s, force)\n", object_id, mode);
    } else {
        len = snprintf(buffer, buffer_size, "request(%u, %s)\n", object_id, mode);
    }
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildReleaseCmd(char* buffer, uint16_t buffer_size,
                            uint16_t object_id, const char* mode) {
    if (!buffer || !mode || buffer_size < 60) return 0;
    int len = snprintf(buffer, buffer_size, "release(%u, %s)\n", object_id, mode);
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildSetSpeedCmd(char* buffer, uint16_t buffer_size,
                             uint16_t object_id, uint8_t speed) {
    if (!buffer || buffer_size < 60) return 0;
    int len = snprintf(buffer, buffer_size, "set(%u, speed[%u])\n", object_id, (uint16_t)speed);
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildSetDirectionCmd(char* buffer, uint16_t buffer_size,
                                 uint16_t object_id, uint8_t direction) {
    if (!buffer || buffer_size < 60) return 0;
    // Real Ecos property is "dir", not "direction" - "direction" is
    // rejected as "unknown option" (confirmed against real hardware
    // 2026-07-31).
    int len = snprintf(buffer, buffer_size, "set(%u, dir[%u])\n", object_id, (uint16_t)direction);
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildSetFunctionCmd(char* buffer, uint16_t buffer_size,
                                uint16_t object_id, uint8_t function_index, uint8_t state) {
    if (!buffer || buffer_size < 60 || function_index > 31) return 0;
    int len = snprintf(buffer, buffer_size, "set(%u, func[%u,%u])\n",
                      object_id, (uint16_t)function_index, (uint16_t)state);
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildSystemStopCmd(char* buffer, uint16_t buffer_size) {
    if (!buffer || buffer_size < 20) return 0;
    int len = snprintf(buffer, buffer_size, "set(%u, stop)\n", ECOS_OBJECT_BASE_SYSTEM);
    return (len > 0) ? len : 0;
}

uint16_t ecosBuildSystemGoCmd(char* buffer, uint16_t buffer_size) {
    if (!buffer || buffer_size < 20) return 0;
    int len = snprintf(buffer, buffer_size, "set(%u, go)\n", ECOS_OBJECT_BASE_SYSTEM);
    return (len > 0) ? len : 0;
}

uint16_t
ecosBuildSetAccessoryCmd(char* buffer, uint16_t buffer_size, uint16_t address, bool diverging) {
    if (!buffer || buffer_size < 30) return 0;
    // Port letter confirmed live 2026-08-05 against real hardware - the
    // spec's own example doesn't spell out g/r's meaning, and the initial
    // context-inferred guess (g=straight, r=diverging) turned out to be
    // backwards: a real MultiMaus's "straight"/"diverging" showed up
    // inverted on the Ecos side until swapped to this mapping.
    int len = snprintf(buffer, buffer_size, "set(%u, switch[DCC%u%c])\n",
                        ECOS_OBJECT_ACCESSORY_MANAGER, address, diverging ? 'g' : 'r');
    return (len > 0) ? len : 0;
}

// ============================================================================
// PROTOCOL PARSING HELPERS
// ============================================================================

uint16_t ecosParseEventObjectId(const char* line) {
    if (!line || !ecosIsEventStart(line)) return 0;

    // Format: "<EVENT 1000>"
    // Skip "<EVENT "
    const char* p = line + strlen(ECOS_EVENT_START);

    // Parse numeric object ID
    uint16_t id = 0;
    while (*p && *p >= '0' && *p <= '9') {
        id = id * 10 + (*p - '0');
        p++;
    }

    return (id > 0) ? id : 0;
}

int16_t ecosParseEndCode(const char* line) {
    if (!line || !ecosIsBlockEnd(line)) return -1;

    // Format: "<END 0 (OK)>" or "<END 1 (Error)>"
    // Skip "<END "
    const char* p = line + strlen(ECOS_REPLY_END);

    // Parse numeric code
    int16_t code = 0;
    if (*p >= '0' && *p <= '9') {
        code = *p - '0';
        p++;
        // Check for multi-digit codes (though Ecos typically uses 0 or 1)
        while (*p >= '0' && *p <= '9') {
            code = code * 10 + (*p - '0');
            p++;
        }
        return code;
    }

    return -1;
}
