/*
 * Ecos Protocol Definitions & Command Builders
 *
 * ESU Ecos LAN protocol over TCP port 15471 — plain-text, line-based, object/property format
 * Not XML. Commands like request(id, control), set(id, speed[64]), replies like <REPLY ...>
 *
 * This file isolates protocol constants and command-building utilities so that
 * protocol details (object IDs, command syntax) can be refined later without
 * touching interface or parser logic.
 */

#ifndef ECOS_PROTOCOL_H
#define ECOS_PROTOCOL_H

#include <cstdint>
#include <cstring>

// ============================================================================
// ECOS OBJECT IDs - FIXED BY ECOS SYSTEM
// ============================================================================

// Locomotive manager: querying this object lists all locomotives with their addresses
#define ECOS_OBJECT_LOCOMOTIVE_MANAGER  10

// Base ECoS object - set(1, stop)/set(1, go) are "equivalent to the STOP/GO
// button on the Ecos" (official ESU PC-Interface spec, section 7.1), a real
// system-wide command distinct from setting any one locomotive's speed.
#define ECOS_OBJECT_BASE_SYSTEM         1

// ============================================================================
// ECOS COMMAND FORMAT CONSTANTS
// ============================================================================

// Command modes for request() / release()
#define ECOS_MODE_VIEW      "view"       // Subscribe to unsolicited events
#define ECOS_MODE_CONTROL   "control"    // Request exclusive control (required for set)

// ============================================================================
// ECOS REPLY BLOCK MARKERS
// ============================================================================

// Block delimiters (set by Ecos, not built by bridge)
#define ECOS_REPLY_START    "<REPLY "
#define ECOS_EVENT_START    "<EVENT "
#define ECOS_REPLY_END      "<END "

// ============================================================================
// COMMAND BUILDERS
// ============================================================================

/**
 * Build a "queryObjects" command to list all locos on the system
 * Returns: "queryObjects(10, addr, name)\n"
 * Used once at startup and periodically to refresh the DCC address → Ecos object ID map
 *
 * @param buffer Destination for the command string (must be >= 50 bytes)
 * @param buffer_size Size of the destination buffer
 * @return Number of bytes written (excluding null terminator), or 0 if buffer too small
 */
uint16_t ecosBuildQueryObjectsCmd(char* buffer, uint16_t buffer_size);

/**
 * Build a "request" command to subscribe to an object or request control
 * Returns: "request(1000, view)\n" or "request(1000, control)\n"
 * Subscribes to unsolicited <EVENT> pushes for that object (view mode)
 * or requests exclusive throttle control (control mode)
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @param object_id Ecos object ID (e.g., 1000)
 * @param mode Either ECOS_MODE_VIEW or ECOS_MODE_CONTROL
 * @param force_control If true and mode is "control", adds "force" flag (optional, rarely needed)
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildRequestCmd(char* buffer, uint16_t buffer_size,
                            uint16_t object_id, const char* mode, bool force_control = false);

/**
 * Build a "release" command to unsubscribe or release control
 * Returns: "release(1000, view)\n" or "release(1000, control)\n"
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @param object_id Ecos object ID
 * @param mode Either ECOS_MODE_VIEW or ECOS_MODE_CONTROL
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildReleaseCmd(char* buffer, uint16_t buffer_size,
                            uint16_t object_id, const char* mode);

/**
 * Build a "set" command to change locomotive speed
 * Returns: "set(1000, speed[64])\n"
 * Requires prior "request(id, control)" to be in effect
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @param object_id Ecos object ID
 * @param speed 0-126 (DCC standard, 127 is reserved for E-stop)
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildSetSpeedCmd(char* buffer, uint16_t buffer_size,
                             uint16_t object_id, uint8_t speed);

/**
 * Build a "set" command to change locomotive direction
 * Returns: "set(1000, dir[1])\n" - the real Ecos property is "dir", not
 * "direction" ("direction" is rejected as "unknown option"; confirmed
 * against real hardware 2026-07-31 via get(id) dumping every real property
 * name for a locomotive object).
 * Requires prior "request(id, control)"
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @param object_id Ecos object ID
 * @param direction 0=reverse, 1=forward (DCC standard)
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildSetDirectionCmd(char* buffer, uint16_t buffer_size,
                                 uint16_t object_id, uint8_t direction);

/**
 * Build a "set" command to toggle a function
 * Returns: "set(1000, func[0,1])\n" (function 0, turn on)
 * Requires prior "request(id, control)"
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @param object_id Ecos object ID
 * @param function_index 0-31 (F0=headlight, F1-F4=direction-dependent headlights, etc.)
 * @param state 0=off, 1=on
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildSetFunctionCmd(char* buffer, uint16_t buffer_size,
                                uint16_t object_id, uint8_t function_index, uint8_t state);

/**
 * Build the system-wide stop command - "equivalent to the STOP button on
 * the Ecos" (official spec). Stops the whole layout, not just one loco.
 * Returns: "set(1, stop)\n"
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildSystemStopCmd(char* buffer, uint16_t buffer_size);

/**
 * Build the system-wide resume command - "equivalent to the GO button on
 * the Ecos" (official spec).
 * Returns: "set(1, go)\n"
 *
 * @param buffer Destination for the command string
 * @param buffer_size Size of the destination buffer
 * @return Number of bytes written, or 0 if buffer too small
 */
uint16_t ecosBuildSystemGoCmd(char* buffer, uint16_t buffer_size);

// ============================================================================
// PROTOCOL PARSING HELPERS
// ============================================================================

/**
 * Check if a line is the start of a <REPLY> block
 * Returns true for lines like: "<REPLY request(100, control)>"
 */
inline bool ecosIsReplyStart(const char* line) {
    return (strncmp(line, ECOS_REPLY_START, strlen(ECOS_REPLY_START)) == 0);
}

/**
 * Check if a line is the start of an <EVENT> block
 * Returns true for lines like: "<EVENT 1000>"
 */
inline bool ecosIsEventStart(const char* line) {
    return (strncmp(line, ECOS_EVENT_START, strlen(ECOS_EVENT_START)) == 0);
}

/**
 * Check if a line is the end marker for a block
 * Returns true for lines like: "<END 0 (OK)>" or "<END 1 (Err)>"
 */
inline bool ecosIsBlockEnd(const char* line) {
    return (strncmp(line, ECOS_REPLY_END, strlen(ECOS_REPLY_END)) == 0);
}

/**
 * Extract the object ID from an <EVENT n> line
 * Returns the object ID (e.g., 1000 from "<EVENT 1000>"), or 0 on parse error
 */
uint16_t ecosParseEventObjectId(const char* line);

/**
 * Extract the end code from a <END n (text)> line
 * Returns the numeric code (0=OK, 1=error, etc.), or -1 on parse error
 */
int16_t ecosParseEndCode(const char* line);

#endif  // ECOS_PROTOCOL_H
