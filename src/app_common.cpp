#include "app_common.h"
#include "app_config.h"
#include <WiFi.h>
#include <cstring>

void drawInfoLine(const int x, int& y, const char* label, const char* value) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.print(": ");
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.println(value);
    y += INFO_LINE_H;
}

void drawInfoLineInt(const int x, int& y, const char* label, const int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    drawInfoLine(x, y, label, buf);
}

bool ensureConfigWifi() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        return false;
    }

    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid) {
        return true;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    const uint32_t deadline = millis() + 12000;
    while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

String getPressedKey() {
    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    String key;
    for (const char c : status.word) {
        key += c;
    }
    return key;
}
