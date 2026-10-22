/*
 * LogRing - fixed-size byte ring for the network debug log
 *
 * Writers only ever append (never block, never touch the network), so it is
 * safe to log from time-critical code; a separate reader drains it when a
 * log client is connected. When nothing is reading, the oldest bytes are
 * overwritten, so a client that connects later still gets the most recent
 * history. Storage is supplied by the caller (no dynamic allocation).
 *
 * No Arduino dependency - unit-tested natively.
 */

#ifndef UTILS_LOG_RING_H
#define UTILS_LOG_RING_H

#include <cstddef>
#include <cstdint>

class LogRing {
public:
    LogRing(char* storage, size_t capacity)
        : buf(storage), cap(capacity), write_pos(0), read_pos(0), dropped_total(0) {}

    // Appends data. If the reader has fallen more than `capacity` bytes
    // behind, its position jumps forward and the lost bytes are counted.
    void write(const char* data, size_t len);

    // Bytes written but not yet read.
    size_t pending() const { return (size_t)(write_pos - read_pos); }

    // Copies up to max pending bytes to out and advances the reader.
    size_t read(char* out, size_t max);

    // Restarts the reader at the oldest byte still retained (a newly
    // connected client gets the buffered history) and forgets earlier drops.
    void rewindToOldest();

    // Total bytes the reader lost because writers overran it.
    uint32_t dropped() const { return dropped_total; }

    size_t capacity() const { return cap; }

private:
    char* buf;
    size_t cap;
    uint32_t write_pos;   // monotonically increasing byte counters
    uint32_t read_pos;
    uint32_t dropped_total;
};

#endif  // UTILS_LOG_RING_H
