/*
 * State Engine Implementation - Locomotive State Management
 * 
 * Manages in-memory state of all controlled locomotives.
 * No persistence - Ecos handles that.
 * 
 * Key features:
 * - Fast lookup by DCC address
 * - Periodic expiry of inactive locos (5 min timeout)
 * - Non-blocking expiry check
 * - Subscribed status tracking
 */

#include <cstdint>
#include <cstring>
#include <Arduino.h>
#include "state_engine.h"
#include "utils/debug.h"
#include "utils/memory.h"
#include "utils/now_ms.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

StateEngine::StateEngine() : loco_count(0) {
    // Initialize all locomotive states to zero
    memset(locos, 0, sizeof(locos));
    
    DEBUG_STATE_PRINTF("StateEngine created - max capacity: %d locos\n", MAX_LOCOS);
}

// ============================================================================
// FIND LOCOMOTIVE
// ============================================================================

int StateEngine::findLoco(uint16_t address) const {
    /*
     * Linear search through active locos
     * O(n) but n is typically < 20, so fast enough
     * 
     * Returns: index if found, -1 if not found
     */
    for (int i = 0; i < loco_count; i++) {
        if (locos[i].dcc_address == address) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// ADD OR UPDATE LOCOMOTIVE
// ============================================================================

bool StateEngine::addOrUpdateLoco(uint16_t address, const LocoState& state) {
    /*
     * Add new loco or update existing one
     * 
     * Returns: true if successful, false if state engine full
     */
    
    // Validate DCC address
    if (!isValidDccAddress(address)) {
        DEBUG_STATE_PRINTF("ERROR: Invalid DCC address: %u\n", address);
        return false;
    }
    
    // Try to find existing loco
    int index = findLoco(address);
    
    if (index >= 0) {
        // Update existing locomotive
        LocoState& existing = locos[index];
        
        DEBUG_STATE_PRINTF("Updating loco %u: speed=%u dir=%u fn=0x%x\n",
                          address, state.speed, state.direction, state.functions);
        
        existing.speed = state.speed;
        existing.direction = state.direction;
        existing.functions = state.functions;
        existing.last_source = state.last_source;
        existing.last_update_ms = now_ms();
        existing.subscribed_to_ecos = state.subscribed_to_ecos;
        
        return true;
    }
    
    // Add new locomotive if space available
    if (loco_count >= MAX_LOCOS) {
        DEBUG_STATE_PRINTF("ERROR: State engine full! Max locos (%d) reached\n", MAX_LOCOS);
        return false;
    }
    
    // Create new entry
    LocoState& new_loco = locos[loco_count];
    
    new_loco.dcc_address = address;
    new_loco.speed = state.speed;
    new_loco.direction = state.direction;
    new_loco.functions = state.functions;
    new_loco.last_source = state.last_source;
    new_loco.last_update_ms = now_ms();
    new_loco.subscribed_to_ecos = state.subscribed_to_ecos;
    new_loco.unknown = false;
    
    loco_count++;
    
    DEBUG_STATE_PRINTF("Added new loco %u (now %d total). Source: %s\n",
                      address, loco_count, locoSourceToString(state.last_source));
    
    return true;
}

// ============================================================================
// GET LOCOMOTIVE STATE
// ============================================================================

bool StateEngine::getLoco(uint16_t address, LocoState& state) const {
    /*
     * Retrieve current state of a locomotive
     * 
     * Returns: true if found (state is valid), false if not found
     */
    
    int index = findLoco(address);
    if (index < 0) {
        return false;
    }
    
    // Copy state
    state = locos[index];
    return true;
}

// ============================================================================
// REMOVE LOCOMOTIVE BY INDEX
// ============================================================================

void StateEngine::removeLoco(int index) {
    /*
     * Remove locomotive at specified index
     * Shifts remaining locos down in array
     * 
     * This preserves order and keeps array compact
     */
    
    if (index < 0 || index >= loco_count) {
        DEBUG_STATE_PRINTF("ERROR: Invalid loco index: %d\n", index);
        return;
    }
    
    uint16_t removed_address = locos[index].dcc_address;
    
    // Shift remaining locos down
    for (int i = index; i < loco_count - 1; i++) {
        locos[i] = locos[i + 1];
    }
    
    loco_count--;
    
    DEBUG_STATE_PRINTF("Removed loco %u (now %d total)\n", removed_address, loco_count);
}

// ============================================================================
// REMOVE LOCOMOTIVE BY ADDRESS
// ============================================================================

bool StateEngine::removeLocoByAddress(uint16_t address) {
    /*
     * Remove locomotive by DCC address
     * 
     * Returns: true if removed, false if not found
     */
    
    int index = findLoco(address);
    if (index < 0) {
        return false;
    }
    
    removeLoco(index);
    return true;
}

// ============================================================================
// EXPUNGE INACTIVE LOCOMOTIVES
// ============================================================================

int StateEngine::expungeInactiveLocos(uint16_t* removed_out, int max_out) {
    /*
     * Remove all locos not updated in LOCO_INACTIVITY_TIMEOUT
     * Called periodically (every ~30 seconds from main loop)
     * 
     * Returns: number of locos removed
     * 
     * Strategy: Iterate backwards through array so index doesn't shift
     * when we remove items
     */
    
    unsigned long now = now_ms();
    int removed_count = 0;

    // Iterate backwards to safely remove while iterating
    for (int i = loco_count - 1; i >= 0; i--) {
        unsigned long age = now - locos[i].last_update_ms;

        if (age > LOCO_INACTIVITY_TIMEOUT) {
            DEBUG_STATE_PRINTF("Expiring inactive loco %u (age: %lu ms)\n",
                              locos[i].dcc_address, age);

            // Capture address if output array provided
            if (removed_out && removed_count < max_out) {
                removed_out[removed_count] = locos[i].dcc_address;
            }

            removeLoco(i);
            removed_count++;
        }
    }

    if (removed_count > 0) {
        DEBUG_STATE_PRINTF("Expunge complete: removed %d locos\n", removed_count);
    }

    return removed_count;
}

// ============================================================================
// GET LOCOMOTIVE BY INDEX
// ============================================================================

LocoState* StateEngine::getLocoByIndex(int index) {
    /*
     * Get locomotive by array index
     * Useful for iteration
     * 
     * Returns: pointer to LocoState, or nullptr if invalid index
     */
    
    if (index < 0 || index >= loco_count) {
        return nullptr;
    }
    
    return &locos[index];
}

const LocoState* StateEngine::getLocoByIndex(int index) const {
    /*
     * Const version of getLocoByIndex
     */
    
    if (index < 0 || index >= loco_count) {
        return nullptr;
    }
    
    return &locos[index];
}

// ============================================================================
// CLEAR ALL LOCOMOTIVES
// ============================================================================

void StateEngine::clear() {
    /*
     * Clear all locomotives from state engine
     * Used on shutdown or factory reset
     */
    
    DEBUG_STATE_PRINTF("Clearing all %d locomotives from state engine\n", loco_count);
    
    memset(locos, 0, sizeof(locos));
    loco_count = 0;
}

// ============================================================================
// DEBUG FUNCTIONS
// ============================================================================

#if ENABLE_DEBUG && DEBUG_STATE_ENGINE

void StateEngine::debugPrintAllLocos() const {
    /*
     * Print all locos for debugging
     * Shows address, speed, direction, functions, age, and source
     */
    
    DEBUG_STATE_PRINT("\n=== Locomotive State Engine ===\n");
    DEBUG_STATE_PRINTF("Total locos: %d/%d\n", loco_count, MAX_LOCOS);
    
    if (loco_count == 0) {
        DEBUG_STATE_PRINT("No locomotives in state engine\n");
        DEBUG_STATE_PRINT("==============================\n\n");
        return;
    }
    
    unsigned long now = now_ms();
    
    DEBUG_STATE_PRINT("Addr  Spd Dir Fn        Age(ms) Source    Ecos\n");
    DEBUG_STATE_PRINT("---- ---- --- -------- ------- --------- ----\n");
    
    for (int i = 0; i < loco_count; i++) {
        const LocoState& loco = locos[i];
        unsigned long age = now - loco.last_update_ms;
        
        DEBUG_STATE_PRINTF("%4u %3u %s   0x%06x %7lu %-9s %s\n",
                          loco.dcc_address,
                          loco.speed,
                          (loco.direction ? "F" : "R"),
                          loco.functions,
                          age,
                          locoSourceToString(loco.last_source),
                          (loco.subscribed_to_ecos ? "Yes" : "No"));
    }
    
    DEBUG_STATE_PRINT("==============================\n\n");
}

void StateEngine::debugPrintLoco(uint16_t address) const {
    /*
     * Print detailed info for specific locomotive
     */
    
    int index = findLoco(address);
    if (index < 0) {
        DEBUG_STATE_PRINTF("Loco %u not found\n", address);
        return;
    }
    
    const LocoState& loco = locos[index];
    unsigned long age = now_ms() - loco.last_update_ms;
    
    DEBUG_STATE_PRINTF("\n=== Locomotive %u ===\n", address);
    DEBUG_STATE_PRINTF("Speed:              %u\n", loco.speed);
    DEBUG_STATE_PRINTF("Direction:         %s\n", loco.direction ? "Forward" : "Reverse");
    DEBUG_STATE_PRINTF("Functions:         0x%08x\n", loco.functions);
    
    // Print individual functions
    DEBUG_STATE_PRINT("  Functions: ");
    for (int f = 0; f < 32; f++) {
        if ((loco.functions >> f) & 1) {
            DEBUG_STATE_PRINTF("F%d ", f);
        }
    }
    DEBUG_STATE_PRINT("\n");
    
    DEBUG_STATE_PRINTF("Last Update:       %lu ms ago\n", age);
    DEBUG_STATE_PRINTF("Last Source:       %s\n", locoSourceToString(loco.last_source));
    DEBUG_STATE_PRINTF("Subscribed to Ecos: %s\n", loco.subscribed_to_ecos ? "Yes" : "No");
    DEBUG_STATE_PRINTF("Unknown:           %s\n", loco.unknown ? "Yes" : "No");
    DEBUG_STATE_PRINT("====================\n\n");
}

#endif  // ENABLE_DEBUG && DEBUG_STATE_ENGINE
