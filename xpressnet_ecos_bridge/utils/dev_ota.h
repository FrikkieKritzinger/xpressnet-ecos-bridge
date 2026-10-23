/*
 * Development-only network firmware updater
 *
 * Lets the PC push a new firmware over WiFi while the D1 stays seated in the
 * motherboard (flashing over USB is unreliable there once LocoNet shares the
 * serial pins). A plain HTTP server on DEV_OTA_PORT with the standard
 * ESP8266HTTPUpdateServer at /update; `python tools/devflash.py` builds and
 * uploads to it.
 *
 * NOT for release: it has no authentication, so anyone on the LAN could
 * replace the firmware. It is compiled in only by the `wemos_dev`
 * PlatformIO environment (-DENABLE_DEV_OTA=1); the normal `wemos` build and
 * any published .bin contain none of it. The project's real over-the-air
 * update remains the Setup Mode page.
 */

#ifndef UTILS_DEV_OTA_H
#define UTILS_DEV_OTA_H

// Dev builds can also run named experiments on demand: `GET /action?name=<x>`
// on the same port calls this handler with <x>. Not present in release builds.
typedef void (*DevActionHandler)(const char* name);
void devOtaSetActionHandler(DevActionHandler handler);

// Call every loop iteration. Starts the server once WiFi is connected and
// serves requests; a no-op when ENABLE_DEV_OTA is 0.
void devOtaUpdate();

#endif  // UTILS_DEV_OTA_H
