/*
 * Ecos Protocol Test Response Fixtures
 *
 * Hardcoded valid and invalid Ecos text-based object protocol responses.
 * Format: Text-based key[value] protocol with <REPLY>, <EVENT>, <END> framing
 */

#ifndef ECOS_RESPONSES_H
#define ECOS_RESPONSES_H

// ============================================================================
// VALID ECOS RESPONSES
// ============================================================================

// Simple get request reply (e.g., query loco status)
// Real block shape: start marker (no id here - REPLY doesn't carry one),
// then a property line with the object ID as its leading token, then <END>.
// Property is "dir", not "direction" - "direction" is rejected by real
// Ecos as "unknown option" (confirmed against real hardware 2026-07-31).
static const char* ECOS_REPLY_SPEED_QUERY =
    "<REPLY get(100, view)>\n"
    "100 speed[64] dir[1] func[0,0]\n"
    "<END 0 (OK)>\n";

// Event: Loco speed change (unsolicited update from Ecos)
// EVENT lines DO carry the object ID directly (see ecosParseEventObjectId),
// format "<EVENT 1000>" - no "id" keyword.
static const char* ECOS_EVENT_SPEED_CHANGE =
    "<EVENT 101>\n"
    "101 speed[90] dir[1] func[1,0]\n"
    "<END 0 (OK)>\n";

// Multiple lines (typical: request then reply)
static const char* ECOS_MULTILINE_RESPONSE =
    "request(100, view)\n"
    "<REPLY id 100 speed[64] dir[1] func[0,0]>\n"
    "<END>";

// Query objects response (address map discovery) - real wire shape: one
// <REPLY> marker, then one content line per locomotive Ecos knows about,
// then a single <END>. Confirmed against real hardware 2026-07-31: a block
// like this can legitimately carry a dozen-plus entries.
static const char* ECOS_REPLY_QUERY_OBJECTS =
    "<REPLY queryObjects(10, addr, name)>\n"
    "1000 addr[100] name[Loco_100]\n"
    "1001 addr[50] name[Loco_50]\n"
    "1002 addr[5452] name[Loco_5452]\n"
    "<END 0 (OK)>\n";

// Set speed response (acknowledge)
static const char* ECOS_REPLY_SET_SPEED = "<REPLY id 100 speed[80]>";

// Set function response (acknowledge)
static const char* ECOS_REPLY_SET_FUNCTION = "<REPLY id 100 func[1,1]>";

// Heartbeat response (keep-alive query)
static const char* ECOS_REPLY_HEARTBEAT = "<REPLY id 1>";

// Subscribe response
static const char* ECOS_REPLY_SUBSCRIBE = "<REPLY id 100>";

// Unsubscribe response
static const char* ECOS_REPLY_UNSUBSCRIBE = "<REPLY>";

// ============================================================================
// EDGE CASES
// ============================================================================

// Empty response
static const char* ECOS_EMPTY = "";

// Only newlines
static const char* ECOS_ONLY_NEWLINES = "\n\n\n";

// Partial message (incomplete)
static const char* ECOS_PARTIAL = "<REPLY id 100 speed";

// Malformed (missing closing bracket)
static const char* ECOS_MALFORMED_NOBRACKET = "<REPLY id 100 speed[64]";

// Invalid ID format
static const char* ECOS_INVALID_ID = "<REPLY id abc speed[64]>";

// Multiple closing tags (protocol violation)
static const char* ECOS_MULTIPLE_ENDS =
    "<REPLY id 100 speed[64]>\n"
    "<END>\n"
    "<END>";

// ============================================================================
// RESPONSE LENGTHS
// ============================================================================

static const int ECOS_PARTIAL_LEN = 22;

// ============================================================================
// MESSAGE PATTERNS (for line accumulation testing)
// ============================================================================

// Single message broken into multiple writes (e.g., one byte at a time)
static const char ECOS_BYTE_BY_BYTE[] =
    "<REPLY get(100, view)>\n"
    "100 speed[64]\n"
    "<END 0 (OK)>\n";

// Message with various line endings
static const char* ECOS_WITH_CR = "<REPLY id 100 speed[64]>\r";
static const char* ECOS_WITH_LF = "<REPLY id 100 speed[64]>\n";
static const char* ECOS_WITH_CRLF = "<REPLY id 100 speed[64]>\r\n";

// Message with embedded spaces/formatting
static const char* ECOS_FORMATTED =
    "<REPLY\n"
    "  id 100\n"
    "  speed[64]\n"
    "  dir[1]\n"
    ">";

#endif  // ECOS_RESPONSES_H
