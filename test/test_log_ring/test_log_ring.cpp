/*
 * LogRing Unit Tests
 *
 * Tests for utils/log_ring.h/.cpp - the fixed-size ring behind the network
 * debug log (no Arduino dependency; the WiFiServer glue is in net_log.cpp).
 */

#include <cstring>
#include <string>
#include <unity.h>
#include "utils/log_ring.h"

static char storage[16];
static LogRing ring(storage, sizeof(storage));

void setUp(void) {
    ring = LogRing(storage, sizeof(storage));
}
void tearDown(void) {}

static std::string readAll() {
    char buf[64];
    size_t n = ring.read(buf, sizeof(buf));
    return std::string(buf, n);
}

void test_empty_ring_has_nothing_pending(void) {
    TEST_ASSERT_EQUAL_size_t(0, ring.pending());
    char buf[4];
    TEST_ASSERT_EQUAL_size_t(0, ring.read(buf, sizeof(buf)));
}

void test_written_bytes_read_back_in_order(void) {
    ring.write("abc", 3);
    ring.write("def", 3);
    TEST_ASSERT_EQUAL_size_t(6, ring.pending());
    TEST_ASSERT_EQUAL_STRING("abcdef", readAll().c_str());
    TEST_ASSERT_EQUAL_size_t(0, ring.pending());
}

void test_read_respects_max(void) {
    ring.write("abcdef", 6);
    char buf[4];
    TEST_ASSERT_EQUAL_size_t(4, ring.read(buf, 4));
    TEST_ASSERT_EQUAL_size_t(2, ring.pending());
    TEST_ASSERT_EQUAL_STRING("ef", readAll().c_str());
}

void test_wraps_around_the_end_of_storage(void) {
    ring.write("0123456789", 10);
    readAll();
    ring.write("ABCDEFGHIJ", 10);   // crosses the 16-byte boundary
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJ", readAll().c_str());
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());
}

void test_overrun_drops_oldest_and_counts(void) {
    ring.write("0123456789ABCDEF", 16);   // exactly full
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());
    ring.write("XYZ", 3);                 // pushes out "012"
    TEST_ASSERT_EQUAL_UINT32(3, ring.dropped());
    TEST_ASSERT_EQUAL_STRING("3456789ABCDEFXYZ", readAll().c_str());
}

void test_rewind_returns_buffered_history(void) {
    ring.write("hello ", 6);
    readAll();                             // an earlier client consumed it
    ring.write("world", 5);
    ring.rewindToOldest();                 // a new client connects
    TEST_ASSERT_EQUAL_STRING("hello world", readAll().c_str());
}

void test_rewind_after_wrap_starts_at_oldest_retained(void) {
    ring.write("0123456789ABCDEF", 16);
    ring.write("XYZ", 3);
    ring.rewindToOldest();
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());   // history, not a loss
    TEST_ASSERT_EQUAL_STRING("3456789ABCDEFXYZ", readAll().c_str());
}

void test_writes_with_no_reader_keep_only_recent_history(void) {
    for (int i = 0; i < 10; i++) {
        ring.write("0123456789", 10);
    }
    ring.rewindToOldest();
    TEST_ASSERT_EQUAL_size_t(16, ring.pending());
}

// Real crash found live 2026-10-22: a global object's constructor logged
// before the ring's own file had run its static initializers, so the ring was
// still all zero (capacity 0) and write() divided by zero - a boot loop.
void test_zero_capacity_ring_is_inert(void) {
    LogRing empty(nullptr, 0);
    empty.write("abc", 3);
    char buf[4];
    TEST_ASSERT_EQUAL_size_t(0, empty.read(buf, sizeof(buf)));
    empty.rewindToOldest();
    TEST_ASSERT_EQUAL_size_t(0, empty.pending());
}

// A namespace-scope ring must be constant-initialized (valid before any
// dynamic initializer runs), not merely cheap to construct.
static char early_storage[8];
static constexpr LogRing early_ring(early_storage, sizeof(early_storage));

void test_global_ring_is_constant_initialized(void) {
    TEST_ASSERT_EQUAL_size_t(8, early_ring.capacity());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_empty_ring_has_nothing_pending);
    RUN_TEST(test_written_bytes_read_back_in_order);
    RUN_TEST(test_read_respects_max);
    RUN_TEST(test_wraps_around_the_end_of_storage);
    RUN_TEST(test_overrun_drops_oldest_and_counts);
    RUN_TEST(test_rewind_returns_buffered_history);
    RUN_TEST(test_rewind_after_wrap_starts_at_oldest_retained);
    RUN_TEST(test_writes_with_no_reader_keep_only_recent_history);
    RUN_TEST(test_zero_capacity_ring_is_inert);
    RUN_TEST(test_global_ring_is_constant_initialized);

    return UNITY_END();
}
