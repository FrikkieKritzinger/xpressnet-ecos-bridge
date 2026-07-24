/*
 * Timing Utilities - Non-Blocking Timer Management
 * 
 * Provides helper functions and classes for cooperative multitasking
 * without blocking the main loop.
 * 
 * All timing is based on millis() for compatibility with XpressNet timing.
 * 
 * Usage:
 *   // Option 1: Object-oriented
 *   TimedTask heartbeat(5000);
 *   if (heartbeat.shouldExecute()) {
 *       sendHeartbeat();
 *   }
 *
 *   // Option 2: Function-based
 *   unsigned long last_check = 0;
 *   if (hasTimePassed(last_check, 5000)) {
 *       doSomething();
 *   }
 */

#ifndef UTILS_TIMING_H
#define UTILS_TIMING_H

#include <cstdint>
#include <Arduino.h>

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

#define TIME_MS_PER_SECOND      1000
#define TIME_MS_PER_MINUTE      60000
#define TIME_MS_PER_HOUR        3600000

// Common timeout values (in milliseconds)
#define TIMEOUT_SHORT           100
#define TIMEOUT_NORMAL          1000
#define TIMEOUT_LONG            5000
#define TIMEOUT_VERY_LONG       30000

// ============================================================================
// UTILITY FUNCTIONS - Standalone timing checks
// ============================================================================

/**
 * Check if specified time has passed since timestamp
 * 
 * @param last_time Previous timestamp (from millis())
 * @param interval Time that must pass (milliseconds)
 * @return true if (current_time - last_time) >= interval
 * 
 * Usage:
 *   static unsigned long last_check = 0;
 *   if (hasTimePassed(last_check, 1000)) {
 *       last_check = millis();
 *       doSomething();
 *   }
 */
inline bool hasTimePassed(unsigned long last_time, unsigned long interval) {
    return (millis() - last_time) >= interval;
}

/**
 * Get milliseconds elapsed since timestamp
 * Handles millis() overflow gracefully
 * 
 * @param start_time Starting timestamp (from millis())
 * @return Milliseconds elapsed (safe even if millis() overflowed)
 * 
 * Note: millis() overflows every ~49 days on Arduino
 * This function handles that gracefully
 */
inline unsigned long timeElapsed(unsigned long start_time) {
    return millis() - start_time;
}

/**
 * Convert milliseconds to seconds (integer division)
 * 
 * @param milliseconds Time in milliseconds
 * @return Time in seconds (rounded down)
 * 
 * Usage:
 *   unsigned long uptime_seconds = millisToSeconds(millis());
 */
inline unsigned long millisToSeconds(unsigned long milliseconds) {
    return milliseconds / TIME_MS_PER_SECOND;
}

/**
 * Convert seconds to milliseconds
 * 
 * @param seconds Time in seconds
 * @return Time in milliseconds
 * 
 * Usage:
 *   unsigned long timeout = secondsToMillis(5);  // 5 seconds
 */
inline unsigned long secondsToMillis(unsigned long seconds) {
    return seconds * TIME_MS_PER_SECOND;
}

/**
 * Check if timestamp is within timeout window
 * Useful for timeout detection
 * 
 * @param timestamp When event occurred
 * @param timeout How long event remains "active"
 * @return true if (current_time - timestamp) < timeout
 * 
 * Usage:
 *   if (isWithinTimeout(last_message_time, 5000)) {
 *       // Last message was within 5 seconds
 *   }
 */
inline bool isWithinTimeout(unsigned long timestamp, unsigned long timeout) {
    return (millis() - timestamp) < timeout;
}

// ============================================================================
// TIMED TASK CLASS - Object-oriented periodic execution
// ============================================================================

/**
 * TimedTask - Encapsulates periodic timer logic
 * 
 * Cleaner than managing timestamps scattered through code.
 * Each periodic task gets its own TimedTask object.
 * 
 * Usage:
 *   TimedTask heartbeat(5000);  // Every 5000ms
 *   
 *   void loop() {
 *       if (heartbeat.shouldExecute()) {
 *           sendHeartbeat();
 *       }
 *   }
 * 
 * Memory: ~8 bytes per instance (two unsigned longs)
 */
class TimedTask {
public:
    /**
     * Default constructor - creates task with 0 interval (must be reconfigured)
     */
    TimedTask() : last_execute(millis()), interval(0) {}

    /**
     * Create a task that executes at specified interval
     *
     * @param interval_ms How often to trigger (milliseconds)
     * @param execute_immediately If true, triggers on first call
     */
    explicit TimedTask(unsigned long interval_ms, bool execute_immediately = false)
        : last_execute(execute_immediately ? 0 : millis()),
          interval(interval_ms) {}
    
    /**
     * Check if it's time to execute
     * Automatically updates last_execute on true return
     * 
     * @return true if interval has passed (and task is executed)
     *         false if more time needed
     * 
     * This should be called in every loop iteration for that task
     */
    bool shouldExecute() {
        unsigned long now = millis();
        if (now - last_execute >= interval) {
            last_execute = now;
            return true;
        }
        return false;
    }
    
    /**
     * Force immediate execution (reset timer)
     * Useful for manual triggers or early events
     * 
     * Usage:
     *   if (urgent_event) {
     *       task.reset();  // Will trigger on next check
     *   }
     */
    void reset() {
        last_execute = millis();
    }
    
    /**
     * Change the execution interval
     * Takes effect on next shouldExecute() call
     * 
     * @param new_interval New interval in milliseconds
     * 
     * Usage:
     *   if (performance_mode) {
     *       task.setInterval(100);  // More frequent
     *   } else {
     *       task.setInterval(500);  // Less frequent
     *   }
     */
    void setInterval(unsigned long new_interval) {
        interval = new_interval;
    }
    
    /**
     * Get the current interval
     * @return Interval in milliseconds
     */
    unsigned long getInterval() const {
        return interval;
    }
    
    /**
     * Get time until next execution
     * 
     * @return Milliseconds remaining until shouldExecute() returns true
     *         0 if ready to execute now
     * 
     * Useful for debugging or display
     */
    unsigned long timeUntilNext() const {
        unsigned long now = millis();
        unsigned long elapsed = now - last_execute;
        if (elapsed >= interval) {
            return 0;
        }
        return interval - elapsed;
    }
    
    /**
     * Get time since last execution
     * @return Milliseconds since last shouldExecute() returned true
     */
    unsigned long timeSinceLastExecution() const {
        return millis() - last_execute;
    }

private:
    unsigned long last_execute;  // Timestamp of last execution
    unsigned long interval;      // Target interval between executions
};

// ============================================================================
// RATE LIMITER CLASS - Prevent too-frequent operations
// ============================================================================

/**
 * RateLimiter - Enforces minimum time between operations
 * 
 * Useful for:
 * - Display updates (don't refresh faster than display can handle)
 * - Network requests (respect rate limits)
 * - Sensor reads (debounce)
 * 
 * Similar to TimedTask but with different semantics:
 * - TimedTask: "Do this every X ms"
 * - RateLimiter: "Allow at most N per X ms"
 * 
 * Usage:
 *   RateLimiter display_update(500);  // Max 2 updates per second
 *   
 *   if (display_update.allow()) {
 *       updateDisplay();
 *   }
 */
class RateLimiter {
public:
    /**
     * Create a rate limiter
     * @param min_interval_ms Minimum time between allowed operations
     */
    explicit RateLimiter(unsigned long min_interval_ms)
        : last_allowed(millis()), min_interval(min_interval_ms) {}
    
    /**
     * Check if operation is allowed
     * 
     * @return true if enough time has passed since last allow()
     *         false if rate limit not satisfied
     * 
     * Only advances timestamp if returning true
     */
    bool allow() {
        unsigned long now = millis();
        if (now - last_allowed >= min_interval) {
            last_allowed = now;
            return true;
        }
        return false;
    }
    
    /**
     * Reset the limiter (allow immediately)
     */
    void reset() {
        last_allowed = millis();
    }
    
    /**
     * Get time until next operation allowed
     * @return Milliseconds remaining, or 0 if allowed now
     */
    unsigned long timeUntilAllowed() const {
        unsigned long now = millis();
        unsigned long elapsed = now - last_allowed;
        if (elapsed >= min_interval) {
            return 0;
        }
        return min_interval - elapsed;
    }

private:
    unsigned long last_allowed;  // Last time operation was allowed
    unsigned long min_interval;  // Minimum interval between operations
};

// ============================================================================
// STOPWATCH CLASS - Measure elapsed time
// ============================================================================

/**
 * Stopwatch - Measure time intervals
 * 
 * Useful for:
 * - Performance measurement
 * - Timeout detection
 * - Event duration tracking
 * 
 * Usage:
 *   Stopwatch timer;
 *   timer.start();
 *   doSomethingLong();
 *   unsigned long duration = timer.stop();
 *   Serial.printf("Took %lu ms\n", duration);
 */
class Stopwatch {
public:
    /**
     * Create a stopwatch (not started)
     */
    Stopwatch() : start_time(0), is_running(false) {}
    
    /**
     * Start the stopwatch
     */
    void start() {
        start_time = millis();
        is_running = true;
    }
    
    /**
     * Stop and return elapsed time
     * @return Milliseconds elapsed since start()
     */
    unsigned long stop() {
        if (!is_running) {
            return 0;
        }
        is_running = false;
        return millis() - start_time;
    }
    
    /**
     * Get elapsed time without stopping
     * @return Milliseconds since start()
     */
    unsigned long elapsed() const {
        if (!is_running) {
            return 0;
        }
        return millis() - start_time;
    }
    
    /**
     * Check if stopwatch is currently running
     * @return true if started and not stopped
     */
    bool isRunning() const {
        return is_running;
    }
    
    /**
     * Reset stopwatch
     * Stops if running and clears elapsed time
     */
    void reset() {
        start_time = 0;
        is_running = false;
    }

private:
    unsigned long start_time;  // When stopwatch started
    bool is_running;           // Is stopwatch currently running?
};

// ============================================================================
// DEBOUNCE HELPER - Debounce digital inputs/state changes
// ============================================================================

/**
 * Debouncer - Debounce digital state changes
 * 
 * Prevents false triggers from noisy signals.
 * State must be stable for debounce_time before changing.
 * 
 * Usage:
 *   Debouncer button(50);  // 50ms debounce
 *   
 *   if (button.update(digitalRead(BUTTON_PIN))) {
 *       if (button.rose()) {
 *           // Button pressed
 *       }
 *   }
 */
class Debouncer {
public:
    /**
     * Create a debouncer
     * @param debounce_ms How long state must be stable (milliseconds)
     */
    explicit Debouncer(unsigned long debounce_ms)
        : debounce_time(debounce_ms), last_state(0), 
          current_state(0), state_change_time(0) {}
    
    /**
     * Update debouncer with new input state
     * Call this every loop iteration with the current input
     * 
     * @param new_state New input state (typically HIGH/LOW or 0/1)
     * @return true if state has changed (after debouncing)
     */
    bool update(bool new_state) {
        if (new_state != current_state) {
            // State is changing, start debounce timer
            if (!isWithinTimeout(state_change_time, debounce_time)) {
                // Debounce time has passed, accept the change
                current_state = new_state;
                state_change_time = millis();
                return true;
            }
        } else {
            // State is stable
            state_change_time = millis();
        }
        return false;
    }
    
    /**
     * Check if state just rose (low → high)
     * @return true if most recent change was low to high
     */
    bool rose() const {
        return (current_state && !last_state);
    }
    
    /**
     * Check if state just fell (high → low)
     * @return true if most recent change was high to low
     */
    bool fell() const {
        return (!current_state && last_state);
    }
    
    /**
     * Get current stable state
     * @return Current debounced state
     */
    bool read() const {
        return current_state;
    }

private:
    unsigned long debounce_time;     // Time to wait for stable state
    bool last_state;                 // Previous stable state
    bool current_state;              // Current stable state
    unsigned long state_change_time; // When state started changing
};

#endif  // UTILS_TIMING_H
