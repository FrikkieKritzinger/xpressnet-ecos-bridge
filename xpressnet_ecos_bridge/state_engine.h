/*
 * State Engine - Locomotive State Management
 * 
 * Maintains in-memory state of all controlled locomotives.
 * No persistence (Ecos handles that).
 * 
 * Lifecycle:
 * 1. XpressNet/Ecos sends command for unknown loco
 * 2. State engine creates new LocoState
 * 3. Stores in array
 * 4. Later, periodically removes inactive locos (5 min timeout)
 * 5. On device power off, state cleared (expected behavior)
 */

#ifndef STATE_ENGINE_H
#define STATE_ENGINE_H

#include <cstdint>
#include <Arduino.h>
#include "config.h"
#include "definitions.h"

class StateEngine {
public:
    /**
     * Initialize state engine (empty)
     */
    StateEngine();
    
    /**
     * Find locomotive by DCC address
     * @param address DCC address to search for
     * @return index if found (0-49), -1 if not found
     */
    int findLoco(uint16_t address) const;
    
    /**
     * Add or update locomotive in state engine
     * If loco exists, update its state
     * If loco doesn't exist and space available, create new entry
     * 
     * @param address DCC address
     * @param state New state to store
     * @return true if successful, false if state engine full
     */
    bool addOrUpdateLoco(uint16_t address, const LocoState& state);
    
    /**
     * Get current state of a locomotive
     * @param address DCC address
     * @param state Output: locomotive state (only valid if returns true)
     * @return true if found, false if not in state engine
     */
    bool getLoco(uint16_t address, LocoState& state) const;
    
    /**
     * Remove specific locomotive from state engine
     * @param index Index from findLoco() or getLoco()
     */
    void removeLoco(int index);
    
    /**
     * Remove locomotive by address
     * @param address DCC address
     * @return true if removed, false if not found
     */
    bool removeLocoByAddress(uint16_t address);
    
    /**
     * Expire all locos that haven't been updated in LOCO_INACTIVITY_TIMEOUT
     * Called periodically (every ~30 seconds)
     * @return number of locos removed
     */
    int expungeInactiveLocos();
    
    /**
     * Get total number of locos currently in state engine
     * @return count (0 to MAX_LOCOS)
     */
    int getLocoCount() const { return loco_count; }
    
    /**
     * Get locomotive by index (for iteration)
     * @param index 0 to getLocoCount()-1
     * @return pointer to LocoState, or nullptr if invalid index
     */
    LocoState* getLocoByIndex(int index);
    const LocoState* getLocoByIndex(int index) const;
    
    /**
     * Clear all locomotives (reset state engine)
     * Called on shutdown or factory reset
     */
    void clear();
    
    /**
     * Get statistics (for debugging/display)
     * @return total locos in engine
     */
    int getStats() const { return loco_count; }
    
    /**
     * Debug: Print all locomotives
     * Only compiled if ENABLE_DEBUG && DEBUG_STATE_ENGINE
     */
    #if ENABLE_DEBUG && DEBUG_STATE_ENGINE
    void debugPrintAllLocos() const;
    
    /**
     * Debug: Print specific locomotive details
     * @param address DCC address to show
     */
    void debugPrintLoco(uint16_t address) const;
    #endif

private:
    // Internal storage
    LocoState locos[MAX_LOCOS];  // Array of locomotive states
    int loco_count;              // Current number of locos
    
    // Helper methods
    void shiftLocoArray(int start_index);  // Remove by shifting remaining
};

#endif  // STATE_ENGINE_H
