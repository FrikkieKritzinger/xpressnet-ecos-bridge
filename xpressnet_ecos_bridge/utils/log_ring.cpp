#include "log_ring.h"

void LogRing::write(const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[write_pos % cap] = data[i];
        write_pos++;
    }
    if ((size_t)(write_pos - read_pos) > cap) {
        uint32_t lost = (write_pos - read_pos) - (uint32_t)cap;
        dropped_total += lost;
        read_pos += lost;
    }
}

size_t LogRing::read(char* out, size_t max) {
    size_t n = pending();
    if (n > max) {
        n = max;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = buf[read_pos % cap];
        read_pos++;
    }
    return n;
}

void LogRing::rewindToOldest() {
    read_pos = (write_pos > cap) ? (uint32_t)(write_pos - cap) : 0;
    dropped_total = 0;
}
