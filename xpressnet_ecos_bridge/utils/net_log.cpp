#include "net_log.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <cstring>

#include "../config.h"
#include "log_ring.h"

#if ENABLE_NET_LOG

static char g_log_storage[NET_LOG_BUFFER_SIZE];
static LogRing g_log_ring(g_log_storage, NET_LOG_BUFFER_SIZE);
static WiFiServer g_log_server(NET_LOG_PORT);
static WiFiClient g_log_client;
static bool g_log_server_started = false;
static uint32_t g_log_reported_drops = 0;
static unsigned long g_log_last_active_ms = 0;
static bool g_log_had_client = false;

// A client that reconnects within this time of the last one gets no history
// replay. Replaying 4 KB on every reconnect made two competing log clients
// (each reconnecting every ~2 s) load the bridge enough to disturb XpressNet.
static const unsigned long LOG_REPLAY_QUIET_MS = 10000;

void debugEmit(const char* text) {
    g_log_ring.write(text, strlen(text));
#if DEBUG_TO_SERIAL
    Serial.print(text);
#endif
}

void netLogUpdate() {
    if (!g_log_server_started) {
        if (WiFi.status() != WL_CONNECTED) {
            return;
        }
        g_log_server.begin();
        g_log_server.setNoDelay(true);
        g_log_server_started = true;
        char msg[48];
        snprintf(msg, sizeof(msg), "Net log listening on port %u\n", (unsigned)NET_LOG_PORT);
        debugEmit(msg);
    }

    WiFiClient incoming = g_log_server.available();
    if (incoming) {
        if (g_log_client) {
            g_log_client.stop();
        }
        g_log_client = incoming;
        if (!g_log_had_client || (millis() - g_log_last_active_ms) > LOG_REPLAY_QUIET_MS) {
            g_log_ring.rewindToOldest();
        } else {
            g_log_ring.skipToNewest();
        }
        g_log_had_client = true;
        g_log_last_active_ms = millis();
        g_log_reported_drops = 0;
    }

    if (!g_log_client) {
        return;
    }
    if (!g_log_client.connected()) {
        g_log_client.stop();
        return;
    }
    g_log_last_active_ms = millis();

    // The log is one-way; discard anything the client types.
    while (g_log_client.available()) {
        g_log_client.read();
    }

    if (g_log_ring.dropped() != g_log_reported_drops) {
        g_log_reported_drops = g_log_ring.dropped();
        char msg[48];
        snprintf(msg, sizeof(msg), "\n[log overrun: %lu bytes lost]\n", (unsigned long)g_log_reported_drops);
        g_log_client.write(msg, strlen(msg));
    }

    int room = g_log_client.availableForWrite();
    if (room > 0 && g_log_ring.pending() > 0) {
        char chunk[256];
        size_t n = (size_t)room < sizeof(chunk) ? (size_t)room : sizeof(chunk);
        n = g_log_ring.read(chunk, n);
        g_log_client.write(chunk, n);
    }
}

#else  // !ENABLE_NET_LOG

void debugEmit(const char* text) {
#if DEBUG_TO_SERIAL
    Serial.print(text);
#else
    (void)text;
#endif
}

void netLogUpdate() {}

#endif  // ENABLE_NET_LOG
