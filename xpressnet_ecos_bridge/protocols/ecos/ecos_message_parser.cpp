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
    : line_buffer_index(0), block_reply_count(0), block_reply_read_index(0),
      block_in_progress(false), block_header_object_id(0) {
    memset(line_buffer, 0, sizeof(line_buffer));
}

void EcosMessageParser::reset() {
    line_buffer_index = 0;
    block_reply_count = 0;
    block_reply_read_index = 0;
    block_in_progress = false;
    block_header_object_id = 0;
    memset(line_buffer, 0, sizeof(line_buffer));
}

bool EcosMessageParser::getNextQueuedReply(EcosReply& reply) {
    if (block_reply_read_index >= block_reply_count) {
        return false;
    }
    reply = block_replies[block_reply_read_index++];
    return true;
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
        reply.kind = EcosReply::REPLY;
        return false;
    }

    if (ecosIsEventStart(line)) {
        // Starting a new <EVENT> block
        resetBlock();
        block_in_progress = true;
        reply.kind = EcosReply::EVENT;
        block_header_object_id = ecosParseEventObjectId(line);
        return false;
    }

    // Check for block end marker
    if (ecosIsBlockEnd(line)) {
        if (!block_in_progress) {
            // Stray <END> without a block open — ignore
            DEBUG_ECOS_PRINTF("Ecos: Stray <END> line\n");
            return false;
        }

        int16_t end_code = ecosParseEndCode(line);
        char end_text[32] = {0};

        // Extract end text (e.g., "OK" or "ERR")
        const char* p = line;
        while (*p && *p != '(') p++;  // Find opening paren
        if (*p == '(') {
            p++;
            int i = 0;
            while (*p && *p != ')' && i < (int)sizeof(end_text) - 1) {
                end_text[i++] = *p++;
            }
            end_text[i] = '\0';
        }

        // Stamp the shared block-level fields (kind/end_code/end_text) onto
        // every entry parsed so far, and fill in the <EVENT NNN> header's
        // object ID as a fallback for any entry whose own content line
        // didn't carry one.
        for (uint16_t i = 0; i < block_reply_count; i++) {
            block_replies[i].kind = reply.kind;
            block_replies[i].end_code = end_code;
            strncpy(block_replies[i].end_text, end_text, sizeof(block_replies[i].end_text) - 1);
            if (block_replies[i].object_id == 0) {
                block_replies[i].object_id = block_header_object_id;
            }
        }

        if (block_reply_count == 0) {
            // No content lines (e.g. a bare set() acknowledgment, or an
            // error with no data) - still surface something so the caller
            // sees the error/ack and its last-received-data watchdog resets.
            reply.end_code = end_code;
            strncpy(reply.end_text, end_text, sizeof(reply.end_text) - 1);
            reply.object_id = block_header_object_id;
        } else {
            reply = block_replies[0];
            block_reply_read_index = 1;  // remaining entries wait for getNextQueuedReply()
        }

        block_in_progress = false;
        return true;  // Block complete!
    }

    // Regular content line — parse immediately and queue if block in progress
    if (block_in_progress) {
        if (block_reply_count < MAX_BLOCK_REPLIES) {
            EcosReply entry;
            parsePropertyLine(line, entry);
            block_replies[block_reply_count++] = entry;
            return false;
        } else {
            // Defensive backstop only - MAX_BLOCK_REPLIES matches
            // MAX_ECOS_OBJECTS, comfortably above any real layout's
            // locomotive count. Drop the line rather than losing an
            // already-queued entry to a memmove.
            DEBUG_ECOS_PRINTF("Ecos: Block buffer full (> %d entries), dropping line\n", MAX_BLOCK_REPLIES);
            return false;
        }
    }

    // No block in progress and not a marker — this is stray data, ignore
    return false;
}

// ============================================================================
// BLOCK PARSING
// ============================================================================

void EcosMessageParser::parsePropertyLine(const char* line, EcosReply& reply) {
    if (!line || strlen(line) == 0) {
        return;
    }

    // Format: "1000 speed[64] dir[1] func[0,1] func[1,0] ..."
    // First token is object ID, rest are key[value] pairs. Real property is
    // "dir", not "direction" - confirmed against real hardware 2026-07-31
    // (get(id) dumps every real property name Ecos actually uses).

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
        else if (strcmp(key, "dir") == 0) {
            // NOTE: the official ESU PC-Interface spec (section 7.11) states
            // dir=1 means reverse, opposite of our internal convention
            // (direction=1=forward) - but real-hardware testing 2026-07-31
            // showed the MultiMaus and Ecos direction arrows matching with
            // this value passed through UN-inverted. Not applying the spec's
            // stated inversion here since it would contradict a confirmed
            // empirical result - likely this particular loco/decoder profile
            // has its own reverse-direction setting in Ecos compensating for
            // physical motor wiring, which a blanket protocol-level inversion
            // would fight rather than respect. Revisit if a loco without that
            // per-decoder compensation shows a real mismatch.
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
    block_reply_count = 0;
    block_reply_read_index = 0;
    block_in_progress = false;
    block_header_object_id = 0;
}
