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

    Kind kind;                      // REPLY, EVENT, or UNKNOWN
    uint16_t object_id;             // Object ID from <EVENT> or body lines
    uint16_t dcc_address;           // Locomotive DCC address (0 if not present)

    uint8_t speed;                  // 0-126 (0 if not present)
    uint8_t direction;              // 0=reverse, 1=forward (invalid if not set)
    uint32_t functions;             // Bitmap F0-F31

    uint8_t functions_mask;         // Which function bits were actually set in this reply
                                    // (so we don't overwrite unset functions when backfilling)

    int16_t end_code;               // Code from <END> marker (0=OK, 1=error, -1=not parsed)
    char end_text[32];              // Text from <END>, e.g., "OK" or "ERR"

    // Flags indicating which fields are valid
    bool has_speed;
    bool has_direction;
    bool has_dcc_address;
    bool has_functions;

    EcosReply() : kind(UNKNOWN), object_id(0), dcc_address(0), speed(0),
                 direction(0), functions(0), functions_mask(0), end_code(-1),
                 has_speed(false), has_direction(false), has_dcc_address(false),
                 has_functions(false) {
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
     * Reset the parser to initial state (useful after error or reconnect)
     */
    void reset();

    /**
     * Get current buffer state (for debugging)
     */
    int getLineBufferLength() const { return line_buffer_index; }
    int getBlockBufferLength() const { return block_line_count; }

private:
    // ========================================================================
    // LINE-LEVEL ACCUMULATION
    // ========================================================================

    static const int MAX_LINE_LENGTH = 256;  // Max bytes per line
    static const int MAX_BLOCK_LINES = 20;   // Max lines per <REPLY>/<EVENT> block

    char line_buffer[MAX_LINE_LENGTH];
    uint16_t line_buffer_index;

    // ========================================================================
    // BLOCK-LEVEL ACCUMULATION
    // ========================================================================

    struct BlockLine {
        char data[MAX_LINE_LENGTH];
    };

    BlockLine block_lines[MAX_BLOCK_LINES];  // Lines in current block
    uint16_t block_line_count;               // Number of lines accumulated
    bool block_in_progress;                  // True if <REPLY> or <EVENT> seen

    enum BlockType {
        BLOCK_NONE = 0,
        BLOCK_REPLY = 1,
        BLOCK_EVENT = 2
    } block_type;

    // ========================================================================
    // HELPER METHODS
    // ========================================================================

    /**
     * Process a complete line (accumulate into block or detect start/end markers)
     * @return true if block is complete and ready to parse
     */
    bool processLine(const char* line, EcosReply& reply);

    /**
     * Parse all lines in the accumulated block into reply structure
     */
    void parseBlock(EcosReply& reply);

    /**
     * Parse a single object property line like "1000 speed[64] direction[1]"
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
