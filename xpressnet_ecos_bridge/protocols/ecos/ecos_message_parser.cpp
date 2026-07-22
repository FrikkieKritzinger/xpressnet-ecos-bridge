/*
 * Ecos Message Parser Implementation
 *
 * Line/block accumulation and key[value] pair extraction for ESU Ecos LAN protocol
 */

#include "ecos_message_parser.h"
#include "ecos_protocol.h"
#include "../../utils/debug.h"
#include <cstring>
#include <cstdlib>
#include <Arduino.h>

// ============================================================================
// CONSTRUCTOR / RESET
// ============================================================================

EcosMessageParser::EcosMessageParser()
    : line_buffer_index(0), block_line_count(0), block_in_progress(false),
      block_type(BLOCK_NONE) {
    memset(line_buffer, 0, sizeof(line_buffer));
    memset(block_lines, 0, sizeof(block_lines));
}

void EcosMessageParser::reset() {
    line_buffer_index = 0;
    block_line_count = 0;
    block_in_progress = false;
    block_type = BLOCK_NONE;
    memset(line_buffer, 0, sizeof(line_buffer));
}

// ============================================================================
// MAIN BYTE PROCESSOR
// ============================================================================

bool EcosMessageParser::processByte(uint8_t byte, EcosReply& reply) {
    // Accumulate bytes into line_buffer until we see \n (line ending)

    if (byte == '\r') {
        // Skip carriage return (Windows \r\n or old Mac \r)
        return false;
    }

    if (byte == '\n') {
        // End of line — process it
        if (line_buffer_index == 0) {
            // Empty line, ignore
            return false;
        }

        // Null-terminate the line
        if (line_buffer_index < MAX_LINE_LENGTH - 1) {
            line_buffer[line_buffer_index] = '\0';
        } else {
            line_buffer[MAX_LINE_LENGTH - 1] = '\0';
        }

        // Process the complete line
        bool block_complete = processLine(line_buffer, reply);

        // Reset line buffer for next line
        line_buffer_index = 0;
        memset(line_buffer, 0, MAX_LINE_LENGTH);

        return block_complete;
    }

    // Regular character — add to line buffer
    if (line_buffer_index < MAX_LINE_LENGTH - 1) {
        line_buffer[line_buffer_index++] = byte;
        return false;
    } else {
        // Line too long — discard and reset
        DEBUG_ECOS_PRINTF("Ecos: Line buffer overflow (> %d bytes)\n", MAX_LINE_LENGTH);
        discardLine();
        return false;
    }
}

// ============================================================================
// LINE PROCESSING
// ============================================================================

bool EcosMessageParser::processLine(const char* line, EcosReply& reply) {
    if (!line || strlen(line) == 0) {
        return false;
    }

    // Check for block start markers
    if (ecosIsReplyStart(line)) {
        // Starting a new <REPLY> block
        resetBlock();
        block_in_progress = true;
        block_type = BLOCK_REPLY;
        reply.kind = EcosReply::REPLY;
        return false;
    }

    if (ecosIsEventStart(line)) {
        // Starting a new <EVENT> block
        resetBlock();
        block_in_progress = true;
        block_type = BLOCK_EVENT;
        reply.kind = EcosReply::EVENT;
        reply.object_id = ecosParseEventObjectId(line);
        return false;
    }

    // Check for block end marker
    if (ecosIsBlockEnd(line)) {
        if (!block_in_progress) {
            // Stray <END> without a block open — ignore
            DEBUG_ECOS_PRINTF("Ecos: Stray <END> line\n");
            return false;
        }

        // End of block — parse accumulated lines and return reply
        reply.end_code = ecosParseEndCode(line);

        // Extract end text (e.g., "OK" or "ERR")
        const char* p = line;
        while (*p && *p != '(') p++;  // Find opening paren
        if (*p == '(') {
            p++;
            int i = 0;
            while (*p && *p != ')' && i < (int)sizeof(reply.end_text) - 1) {
                reply.end_text[i++] = *p++;
            }
            reply.end_text[i] = '\0';
        }

        // Parse all accumulated block lines
        parseBlock(reply);

        // Reset for next block
        resetBlock();
        return true;  // Block complete!
    }

    // Regular content line — accumulate if block in progress
    if (block_in_progress) {
        if (block_line_count < MAX_BLOCK_LINES) {
            strncpy(block_lines[block_line_count].data, line, MAX_LINE_LENGTH - 1);
            block_lines[block_line_count].data[MAX_LINE_LENGTH - 1] = '\0';
            block_line_count++;
            return false;
        } else {
            // Block too many lines — discard oldest and add new
            DEBUG_ECOS_PRINTF("Ecos: Block buffer full (> %d lines)\n", MAX_BLOCK_LINES);
            memmove(&block_lines[0], &block_lines[1],
                   sizeof(BlockLine) * (MAX_BLOCK_LINES - 1));
            strncpy(block_lines[MAX_BLOCK_LINES - 1].data, line, MAX_LINE_LENGTH - 1);
            return false;
        }
    }

    // No block in progress and not a marker — this is stray data, ignore
    return false;
}

// ============================================================================
// BLOCK PARSING
// ============================================================================

void EcosMessageParser::parseBlock(EcosReply& reply) {
    // Parse all accumulated lines in the block
    for (uint16_t i = 0; i < block_line_count; i++) {
        parsePropertyLine(block_lines[i].data, reply);
    }
}

void EcosMessageParser::parsePropertyLine(const char* line, EcosReply& reply) {
    if (!line || strlen(line) == 0) {
        return;
    }

    // Format: "1000 speed[64] direction[1] func[0,1] func[1,0] ..."
    // First token is object ID, rest are key[value] pairs

    const char* p = line;

    // Skip leading whitespace
    while (*p && (*p == ' ' || *p == '\t')) p++;

    // Parse object ID
    uint16_t obj_id = 0;
    while (*p && *p >= '0' && *p <= '9') {
        obj_id = obj_id * 10 + (*p - '0');
        p++;
    }

    if (obj_id > 0) {
        reply.object_id = obj_id;
    }

    // Parse key[value] pairs
    char key[32];
    char value[32];

    while (*p) {
        // Skip whitespace between properties
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        // Extract next key[value]
        p = extractKeyValue(p, key, sizeof(key), value, sizeof(value));
        if (!key[0]) continue;  // Failed to extract

        // Process the key/value pair
        if (strcmp(key, "speed") == 0) {
            reply.speed = atoi(value);
            reply.has_speed = true;
        }
        else if (strcmp(key, "direction") == 0) {
            reply.direction = atoi(value);
            reply.has_direction = true;
        }
        else if (strcmp(key, "addr") == 0) {
            reply.dcc_address = atoi(value);
            reply.has_dcc_address = true;
        }
        else if (strcmp(key, "func") == 0) {
            // Format: "func[0,1]" means function 0 is ON (1)
            // Parse as "n,state"
            int fn_index = 0;
            int fn_state = 0;
            if (sscanf(value, "%d,%d", &fn_index, &fn_state) == 2) {
                if (fn_index >= 0 && fn_index < 32) {
                    if (fn_state) {
                        reply.functions |= (1UL << fn_index);
                    } else {
                        reply.functions &= ~(1UL << fn_index);
                    }
                    reply.functions_mask |= (1 << fn_index);
                    reply.has_functions = true;
                }
            }
        }
        // Ignore unknown keys (e.g., "name", "protocol", etc.)
    }
}

const char* EcosMessageParser::extractKeyValue(const char* line, char* key, int key_max,
                                              char* value, int value_max) {
    if (!line || !key || !value) return nullptr;

    key[0] = '\0';
    value[0] = '\0';

    const char* p = line;

    // Extract key (up to '[')
    int i = 0;
    while (*p && *p != '[' && i < key_max - 1) {
        key[i++] = *p++;
    }
    key[i] = '\0';

    // Skip the '['
    if (*p != '[') {
        return p;  // No opening bracket, malformed
    }
    p++;

    // Extract value (up to ']')
    i = 0;
    while (*p && *p != ']' && i < value_max - 1) {
        value[i++] = *p++;
    }
    value[i] = '\0';

    // Skip the ']'
    if (*p == ']') {
        p++;
    }

    return p;
}

// ============================================================================
// CLEANUP HELPERS
// ============================================================================

void EcosMessageParser::discardLine() {
    line_buffer_index = 0;
    memset(line_buffer, 0, MAX_LINE_LENGTH);
    if (block_in_progress) {
        resetBlock();
    }
}

void EcosMessageParser::resetBlock() {
    block_line_count = 0;
    block_in_progress = false;
    block_type = BLOCK_NONE;
    memset(block_lines, 0, sizeof(block_lines));
}
