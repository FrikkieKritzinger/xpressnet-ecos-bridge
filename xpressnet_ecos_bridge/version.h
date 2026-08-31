/*
 * Firmware Version - Single Source of Truth
 *
 * Bump this by hand when cutting a release (Phase 6 step 3 - OTA updates).
 * Shown on the OLED and the Setup Mode pages so a completed update is
 * actually confirmable, using a conventional version number rather than
 * a raw build timestamp - easier for end users to recognize and track
 * across releases. FIRMWARE_BUILD_INFO (config.h, __DATE__/__TIME__)
 * still exists alongside this as a secondary, more precise detail for
 * disambiguating builds sharing the same not-yet-bumped version number
 * during development.
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "1.1.1"

#endif  // VERSION_H
