#include "dev_ota.h"

#include "../config.h"

#if ENABLE_DEV_OTA

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "debug.h"

// Created on first use, not as globals: other globals' constructors run
// before this file's static initialization, and a server built at load time
// would be one more thing to get wrong that way (see utils/log_ring.h).
static ESP8266WebServer* g_dev_server = nullptr;
static ESP8266HTTPUpdateServer* g_dev_updater = nullptr;

void devOtaUpdate() {
    if (g_dev_server == nullptr) {
        if (WiFi.status() != WL_CONNECTED) {
            return;
        }
        g_dev_server = new ESP8266WebServer(DEV_OTA_PORT);
        g_dev_updater = new ESP8266HTTPUpdateServer();
        g_dev_updater->setup(g_dev_server, "/update");
        g_dev_server->begin();
        DEBUG_PRINTF("DEV OTA listening on port %u (/update) - dev build, no authentication\n",
                     (unsigned)DEV_OTA_PORT);
    }
    g_dev_server->handleClient();
}

#else  // !ENABLE_DEV_OTA

void devOtaUpdate() {}

#endif  // ENABLE_DEV_OTA
