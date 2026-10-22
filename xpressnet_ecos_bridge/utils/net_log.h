/*
 * Network debug log - a read-only text stream over TCP
 *
 * All debug output (DEBUG_*_PRINT macros and debugPrintf) is appended to a
 * RAM ring buffer; while WiFi is up, a TCP server on NET_LOG_PORT streams it
 * to one connected client (e.g. `ncat <bridge-ip> 23`, PuTTY in raw mode, or
 * a Python socket). A client that connects late first receives the buffered
 * history. Exists because, with LocoNet active, the D1 Mini's UART0 is the
 * LocoNet receiver, so USB serial debug is unavailable or unreliable.
 *
 * Appending is non-blocking and does no network I/O, so it is safe in the
 * time-critical paths; the network side runs only from netLogUpdate().
 */

#ifndef UTILS_NET_LOG_H
#define UTILS_NET_LOG_H

// Appends text to the log ring (and to Serial if DEBUG_TO_SERIAL).
void debugEmit(const char* text);

// Call every loop iteration: starts the server once WiFi is connected,
// accepts a client, and flushes pending log bytes without blocking.
void netLogUpdate();

#endif  // UTILS_NET_LOG_H
