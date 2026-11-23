// Writes the real Setup Mode pages (config, saved, update) to HTML files so they can be
// rendered for the user documentation. Built natively against the firmware's own page
// builders, so the screenshots always match the shipped firmware.
//
//   g++ -std=c++17 -I xpressnet_ecos_bridge -I xpressnet_ecos_bridge/test_support \
//       tools/dump_setup_pages.cpp xpressnet_ecos_bridge/setup_web_form.cpp \
//       xpressnet_ecos_bridge/eeprom_config.cpp -o dump_setup_pages.exe
//   dump_setup_pages.exe <output-dir>

#include <cstdio>
#include <cstring>
#include <string>
#include "eeprom_config.h"
#include "setup_web_form.h"
#include "version.h"

static void writeFile(const std::string& path, const char* text) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        fprintf(stderr, "cannot write %s\n", path.c_str());
        return;
    }
    fputs(text, f);
    fclose(f);
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    static char buffer[8192];

    EepromConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    eepromConfigLoadDefaults(cfg);
    strncpy(cfg.wifi_ssid, "MyLayoutWiFi", sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_password, "********", sizeof(cfg.wifi_password) - 1);
    strncpy(cfg.ecos_ip, "192.168.0.50", sizeof(cfg.ecos_ip) - 1);
    strncpy(cfg.bridge_ip, "192.168.0.51", sizeof(cfg.bridge_ip) - 1);
    strncpy(cfg.bridge_gateway, "192.168.0.1", sizeof(cfg.bridge_gateway) - 1);
    strncpy(cfg.bridge_subnet, "255.255.255.0", sizeof(cfg.bridge_subnet) - 1);

    if (buildConfigPageHtml(buffer, sizeof(buffer), cfg) > 0) writeFile(dir + "/setup_config.html", buffer);
    if (buildSavedConfirmationHtml(buffer, sizeof(buffer)) > 0) writeFile(dir + "/setup_saved.html", buffer);
    if (buildUpdatePageHtml(buffer, sizeof(buffer), FIRMWARE_VERSION, "Nov 23 2026 10:00:00") > 0)
        writeFile(dir + "/setup_update.html", buffer);
    return 0;
}
