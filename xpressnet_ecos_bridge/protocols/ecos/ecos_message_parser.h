/*
 * Ecos Message Parser - Plain-Text Line/Block Protocol Handler
 *
 * Parses ESU Ecos LAN protocol replies and events:
 * - Accumulates lines (terminated by \n or \r\n)
 * - Groups lines into blocks (delimited by <REPLY>/<EVENT> start and <END> marker)
 * - Extracts object ID and property key/value pairs from block content
 * - Builds a structured reply object for routing to CommandRouter
 *
 * NOT XML — plain text, key[value] pairs, CRLF line endings, <END>-delimited blocks.
 */

#ifndef ECOS_MESSAGE_PARSER_H
#define ECOS_MESSAGE_PARSER_H

#include <cstdint>
#include "../../definitions.h"
#include "../../config.h"

// ============================================================================
// MESSAGE STRUCTURES
// ============================================================================

/**
 * Parsed Ecos reply or event
 * Represents one <REPLY>/<EVENT> block after parsing
 */
struct EcosReply {
    enum Kind {
        UNKNOWN = 0,
        REPLY = 1,      // Response to a command we sent
        EVENT = 2       // Unsolicited notification from Ecos
    };

    // Ecos's global run state - property "Status" on the base ECoS object
    // (id=1), e.g. "1 Status[STOP]". Distinct from any one locomotive's
    // speed - this is the system-wide equivalent of the Ecos's own STOP/GO
    // buttons (official spec, section 7.1).
    enum SystemStatus {
        SYSTEM_STATUS_UNKNOWN = 0,
        SYSTEM_STATUS_STOP = 1,
        SYSTEM_STATUS_GO = 2,
        SYSTEM_STATUS_SHUTDOWN = 3
    };

    Kind kind;                      // REPLY, EVENT, or UNKNOWN
    uint16_t object_id;             // Object ID from <EVENT> or body lines
    uint16_t dcc_address;           // Locomotive DCC address (0 if not present)

    uint8_t speed;                  // 0-126 (0 if not present)
    uint8_t direction;              // 0=reverse, 1=forward (invalid if not set)
    uint32_t functions;             // Bitmap F0-F31

    // Which function bits were actually reported in this reply (so a caller
    // can merge instead of overwriting unset functions when backfilling).
    // Real bug found in codebase audit 2026-08-03: this was uint8_t, which
    // can only represent F0-F7 - `1 << fn_index` for fn_index 8-31 silently
    // truncated to 0 on assignment, so any reported bit at F8 or above
    // never actually registered in the mask. Must match `functions`'s own
    // 32-bit width.
    uint32_t functions_mask;

    SystemStatus system_status;     // Valid only if has_system_status

    // Accessory/turnout "state" property (Phase 7 step 2) - the resting
    // position of an individual Schaltartikel object, e.g. "20000 state[1]".
    // Deliberately does NOT parse the accompanying "switching" property
    // (a transient in-motion flag bracketing the transition, confirmed via
    // real capture 2026-09-01: switching[1] before, switching[0] after -
    // only the settled state matters to this bridge).
    bool accessory_diverging;

    int16_t end_code;               // Code from <END> marker (0=OK, 1=error, -1=not parsed)
    char end_text[32];              // Text from <END>, e.g., "OK" or "ERR"

    // Flags indicating which fields are valid
    bool has_speed;
    bool has_direction;
    bool has_dcc_address;
    bool has_functions;
    bool has_system_status;
    bool has_accessory_state;

    EcosReply() : kind(UNKNOWN), object_id(0), dcc_address(0), speed(0),
                 direction(0), functions(0), functions_mask(0),
                 system_status(SYSTEM_STATUS_UNKNOWN), accessory_diverging(false),
                 end_code(-1),
                 has_speed(false), has_direction(false), has_dcc_address(false),
                 has_functions(false), has_system_status(false),
                 has_accessory_state(false) {
        end_text[0] = '\0';
    }
};

// ============================================================================
// MESSAGE PARSER CLASS
// ============================================================================

class EcosMessageParser {
public:
    /**
     * Constructor - initialize parser state
     */
    EcosMessageParser();

    /**
     * Process one byte from TCP stream, accumulate into message
     * Returns true when a complete <REPLY>/<EVENT> block is parsed
     * (and reply output is populated)
     *
     * @param byte One byte from TCP socket
     * @param[out] reply Populated if method returns true
     * @return true if a complete message was parsed and reply is valid
     */
    bool processByte(uint8_t byte, EcosReply& reply);

    /**
     * After processByte() returns true, a block with more than one content
     * line (e.g. a queryObjects reply enumerating several locomotives) has
     * additional parsed entries queued beyond the one already written into
     * processByte()'s `reply` output. Call this in a loop right after a true
     * return to drain them - each call yields the next queued entry.
     * @return true if an entry was written into `reply`, false once drained
     */
    bool getNextQueuedReply(EcosReply& reply);

    /**
     * Reset the parser to initial state (useful after error or reconnect)
     */
    void reset();

    /**
     * Get current buffer state (for debugging)
     */
    int getLineBufferLength() const { return line_buffer_index; }
    int getBlockBufferLength() const { return block_reply_count; }

private:
    // ========================================================================
    // LINE-LEVEL ACCUMULATION
    // ========================================================================

    static const int MAX_LINE_LENGTH = 256;  // Max bytes per line

    // One parsed entry per content line in a block. Sized to MAX_ECOS_OBJECTS
    // (config.h) - the same cap the address map itself uses - since a
    // queryObjects reply enumerates every locomotive Ecos knows about in one
    // block, and an arbitrary smaller cap would silently drop real locos.
    static const int MAX_BLOCK_REPLIES = MAX_ECOS_OBJECTS;

    char line_buffer[MAX_LINE_LENGTH];
    uint16_t line_buffer_index;

    // ========================================================================
    // BLOCK-LEVEL ACCUMULATION
    // ========================================================================

    // Content lines are parsed immediately as they arrive (not stored as raw
    // text) - each becomes its own EcosReply entry. At <END>, entry [0] is
    // handed back via processByte()'s output param and any remaining entries
    // stay here for getNextQueuedReply() to drain.
    EcosReply block_replies[MAX_BLOCK_REPLIES];
    uint16_t block_reply_count;        // Entries parsed so far in this block
    uint16_t block_reply_read_index;   // Next entry getNextQueuedReply() will hand out
    bool block_in_progress;            // True if <REPLY> or <EVENT> seen
    uint16_t block_header_object_id;   // From "<EVENT NNN>" marker; fallback object_id
                                        // for entries whose content line had none

    // ========================================================================
    // HELPER METHODS
    // ========================================================================

    /**
     * Process a complete line (accumulate into block or detect start/end markers)
     * @return true if block is complete and ready to parse
     */
    bool processLine(const char* line, EcosReply& reply);

    /**
     * Parse a single object property line like "1000 speed[64] dir[1]"
     * into the reply structure
     */
    void parsePropertyLine(const char* line, EcosReply& reply);

    /**
     * Extract key[value] from a string, return pointer past the value
     * Modifies *out_value to point to the value (inside brackets)
     * Returns pointer to character after the closing bracket
     */
    const char* extractKeyValue(const char* line, char* key, int key_max,
                               char* value, int value_max);

    /**
     * Resynchronize after a line overflow or invalid block
     * (keeps first valid-looking line if possible, discards the rest)
     */
    void discardLine();

    /**
     * Clear block and reset for next <REPLY>/<EVENT>
     */
    void resetBlock();
};

#endif  // ECOS_MESSAGE_PARSER_H
