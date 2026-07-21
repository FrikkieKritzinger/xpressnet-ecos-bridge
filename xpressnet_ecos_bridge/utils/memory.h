/*
 * Memory Utilities - ESP8266 Memory Management
 * 
 * Provides memory diagnostics and management for ESP8266
 * 
 * ESP8266 Memory:
 * - Total: 160 KB RAM
 * - OS reserved: ~60 KB
 * - Available: ~100 KB
 * - Stack: ~5 KB
 * - State engine: ~1.5 KB (50 locos)
 * - TCP buffers: ~2 KB
 * 
 * Usage:
 *   if (getLowMemoryWarning()) {
 *       // Take action - maybe clear unused data
 *   }
 */

#ifndef UTILS_MEMORY_H
#define UTILS_MEMORY_H

#include <cstdint>
#include <Arduino.h>

// ============================================================================
// ESP8266 MEMORY CONSTANTS
// ============================================================================

#define ESP8266_TOTAL_HEAP          160000    // 160 KB total RAM
#define ESP8266_OS_RESERVED         60000     // OS uses ~60 KB
#define ESP8266_AVAILABLE_HEAP      100000    // User available ~100 KB
#define ESP8266_STACK_SIZE          5000      // Stack ~5 KB

// Warning thresholds (in bytes)
#define MEMORY_WARNING_CRITICAL     10000     // Less than 10 KB free
#define MEMORY_WARNING_LOW          20000     // Less than 20 KB free
#define MEMORY_WARNING_NORMAL       30000     // Normal threshold

// ============================================================================
// MEMORY QUERY FUNCTIONS
// ============================================================================

/**
 * Get free heap memory
 * @return Bytes of free heap memory
 * 
 * Usage:
 *   uint32_t free = getFreeHeap();
 *   Serial.printf("Free: %u bytes\n", free);
 */
inline uint32_t getFreeHeap() {
    return ESP.getFreeHeap();
}

/**
 * Get largest contiguous free block
 * Important for allocations - fragmentation matters
 * @return Largest free contiguous block in bytes
 */
inline uint32_t getMaxAllocHeap() {
    return ESP.getMaxFreeBlockSize();
}

/**
 * Get approximate heap fragmentation percentage
 * 0% = no fragmentation (one big free block)
 * 100% = highly fragmented (many small blocks)
 * 
 * @return Fragmentation percentage (0-100)
 */
inline uint8_t getHeapFragmentation() {
    uint32_t free = ESP.getFreeHeap();
    uint32_t max_block = ESP.getMaxFreeBlockSize();
    
    if (free == 0) return 100;
    if (max_block == free) return 0;
    
    return (100 * (free - max_block)) / free;
}

/**
 * Get CPU frequency in MHz
 * @return Frequency (80 or 160 MHz typically)
 */
inline uint32_t getCpuFreqMhz() {
    return ESP.getCpuFreqMHz();  // Note: method name has capital Z
}

/**
 * Get sketch size (compiled code)
 * @return Size in bytes
 */
inline uint32_t getSketchSize() {
    return ESP.getSketchSize();
}

/**
 * Get free space in flash
 * @return Bytes available for SPIFFS or future use
 */
inline uint32_t getFreeSketchSpace() {
    return ESP.getFreeSketchSpace();
}

// ============================================================================
// MEMORY STATUS FUNCTIONS
// ============================================================================

/**
 * Check if memory is running low (warning level)
 * @return true if free heap < MEMORY_WARNING_LOW
 */
inline bool isMemoryLow() {
    return ESP.getFreeHeap() < MEMORY_WARNING_LOW;
}

/**
 * Check if memory is critically low (danger level)
 * @return true if free heap < MEMORY_WARNING_CRITICAL
 */
inline bool isMemoryCritical() {
    return ESP.getFreeHeap() < MEMORY_WARNING_CRITICAL;
}

/**
 * Get memory pressure as percentage
 * 0% = plenty of memory free
 * 100% = no memory left
 * 
 * @return Memory usage percentage (0-100)
 */
inline uint8_t getMemoryPressure() {
    uint32_t free = ESP.getFreeHeap();
    uint32_t used = ESP8266_AVAILABLE_HEAP - free;
    return (100 * used) / ESP8266_AVAILABLE_HEAP;
}

/**
 * Get memory status as string (for display)
 * @return "OK", "LOW", or "CRITICAL"
 */
inline const char* getMemoryStatus() {
    if (isMemoryCritical()) return "CRITICAL";
    if (isMemoryLow()) return "LOW";
    return "OK";
}

// ============================================================================
// MEMORY DIAGNOSTIC STRUCTURE
// ============================================================================

/**
 * Complete memory diagnostics snapshot
 * Useful for logging and debugging
 */
struct MemoryDiagnostics {
    uint32_t free_heap;           // Free heap in bytes
    uint32_t largest_block;       // Largest contiguous free block
    uint8_t fragmentation;        // Fragmentation percentage
    uint8_t pressure;             // Memory usage percentage
    uint32_t sketch_size;         // Compiled sketch size
    uint32_t free_sketch_space;   // Free flash space
    uint8_t cpu_freq_mhz;         // CPU frequency
};

/**
 * Get complete memory diagnostics
 * Convenient for logging entire status
 * 
 * @return MemoryDiagnostics struct with all values
 * 
 * Usage:
 *   MemoryDiagnostics mem = getMemoryDiagnostics();
 *   Serial.printf("Free: %u, Fragmentation: %u%%\n", 
 *                 mem.free_heap, mem.fragmentation);
 */
inline MemoryDiagnostics getMemoryDiagnostics() {
    return MemoryDiagnostics{
        .free_heap = ESP.getFreeHeap(),
        .largest_block = ESP.getMaxFreeBlockSize(),
        .fragmentation = getHeapFragmentation(),
        .pressure = getMemoryPressure(),
        .sketch_size = ESP.getSketchSize(),
        .free_sketch_space = ESP.getFreeSketchSpace(),
        .cpu_freq_mhz = (uint8_t)ESP.getCpuFreqMHz()  // Note: capital Z
    };
}

// ============================================================================
// MEMORY WARNING HANDLER (Optional Callback)
// ============================================================================

/**
 * Callback function type for memory warnings
 * User can register a function to be called when memory gets low
 * 
 * typedef void (*MemoryWarningCallback)(const MemoryDiagnostics&);
 * 
 * Example:
 *   void onMemoryWarning(const MemoryDiagnostics& mem) {
 *       Serial.printf("WARNING: Free heap = %u bytes\n", mem.free_heap);
 *       // Take corrective action
 *   }
 */

// Global callback (optional - only if user defines it)
extern void onMemoryWarning(const MemoryDiagnostics& mem);  // Weak symbol

/**
 * Check memory and call warning callback if needed
 * Should be called periodically from main loop
 * 
 * @param critical_threshold Treat as critical at this threshold
 * @return true if warning was triggered
 * 
 * Usage in main loop:
 *   static TimedTask mem_check(30000);  // Every 30 seconds
 *   if (mem_check.shouldExecute()) {
 *       checkMemoryAndWarn(MEMORY_WARNING_LOW);
 *   }
 */
inline bool checkMemoryAndWarn(uint32_t critical_threshold = MEMORY_WARNING_CRITICAL) {
    uint32_t free = ESP.getFreeHeap();
    
    if (free < critical_threshold) {
        MemoryDiagnostics diag = getMemoryDiagnostics();
        onMemoryWarning(diag);  // This will be weak - linking fails if not defined
        return true;
    }
    return false;
}

// ============================================================================
// MEMORY TESTING UTILITIES
// ============================================================================

#if ENABLE_DEBUG

/**
 * Print memory diagnostics to serial
 * 
 * Usage:
 *   debugPrintMemory();
 */
inline void debugPrintMemory() {
    MemoryDiagnostics mem = getMemoryDiagnostics();
    
    Serial.println("\n=== Memory Diagnostics ===");
    Serial.printf("Free heap:        %lu bytes\n", mem.free_heap);
    Serial.printf("Largest block:    %lu bytes\n", mem.largest_block);
    Serial.printf("Fragmentation:    %u%%\n", mem.fragmentation);
    Serial.printf("Memory usage:     %u%%\n", mem.pressure);
    Serial.printf("Sketch size:      %lu bytes\n", mem.sketch_size);
    Serial.printf("Free flash:       %lu bytes\n", mem.free_sketch_space);
    Serial.printf("CPU frequency:    %u MHz\n", mem.cpu_freq_mhz);
    Serial.printf("Memory status:    %s\n", getMemoryStatus());
    Serial.println("==========================\n");
}

/**
 * Allocate test memory to verify fragmentation handling
 * Useful for stress testing
 * 
 * @param size_bytes Size to allocate
 * @return Pointer to allocated memory (nullptr if fails)
 * 
 * Note: Memory is not freed - test only!
 * Call multiple times with different sizes to fragment heap
 */
inline void* testAllocateMemory(size_t size_bytes) {
    void* ptr = malloc(size_bytes);
    if (ptr) {
        Serial.printf("Allocated %u bytes at %p, free heap now: %lu\n", 
                     size_bytes, ptr, ESP.getFreeHeap());
    } else {
        Serial.printf("Failed to allocate %u bytes!\n", size_bytes);
    }
    return ptr;
}

#else
    inline void debugPrintMemory() {}
    inline void* testAllocateMemory(size_t size_bytes) { (void)size_bytes; return nullptr; }
#endif

// ============================================================================
// MEMORY GUARD - Detect stack overflow
// ============================================================================

/**
 * Get approximate stack pointer
 * Can be used to detect stack overflow
 * Lower address = closer to stack overflow (dangerous!)
 * 
 * @return Approximate current stack pointer value
 */
inline uint32_t getStackPointer() {
    uint32_t sp;
    __asm__ __volatile__ ("mov %0, sp" : "=a" (sp) : );
    return sp;
}

/**
 * Estimate stack space remaining
 * This is approximate - not guaranteed accurate
 * 
 * @return Estimated free stack space in bytes
 */
inline uint32_t getStackFreeSpace() {
    // ESP8266 stack grows downward, starts at high address
    // This is very approximate
    uint32_t sp = getStackPointer();
    uint32_t heap_end = (uint32_t)malloc(0) + 4096;
    
    if (sp > heap_end) {
        return sp - heap_end;
    }
    return 0;  // Stack collision detected
}

#endif  // UTILS_MEMORY_H
