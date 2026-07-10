#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include "app_icons.h"
#include <WiFi.h>
#include <cctype>
#include <cstring>

// 绘制按键字母块（黄底黑字）
int drawKeyBadge(const int x, const int y, char key, const int text_size) {
    const int size = (text_size == 2) ? 2 : 1;
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(key)));
    const char str[2] = {letter, '\0'};

    M5Cardputer.Display.setTextSize(size);
    const int tw = M5Cardputer.Display.textWidth(str);
    const int th = 8 * size;
    constexpr int pad_x = 2;
    constexpr int pad_y = 1;
    const int bw = tw + pad_x * 2;
    const int bh = th + pad_y * 2;

    M5Cardputer.Display.fillRoundRect(x, y, bw, bh, 2, APP_COLOR_MENU_KEY);
    M5Cardputer.Display.setTextColor(APP_COLOR_KEY_TEXT, APP_COLOR_MENU_KEY);
    M5Cardputer.Display.setCursor(x + pad_x, y + pad_y);
    M5Cardputer.Display.print(str);

    constexpr int gap = 3;
    return bw + gap;
}

void drawKeyHintsRow(const int x, const int y, const KeyHintItem* items, const int item_count,
                     const int text_size, const uint16_t color) {
    if (items == nullptr || item_count <= 0) {
        return;
    }

    int cx = x;
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(color, BLACK);

    for (int i = 0; i < item_count; i++) {
        const KeyHintItem& item = items[i];
        cx += drawKeyBadge(cx, y, item.key, text_size);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.setTextColor(color, BLACK);
        M5Cardputer.Display.print(item.text);
        cx += M5Cardputer.Display.textWidth(item.text);
        if (i != item_count - 1) {
            M5Cardputer.Display.setCursor(cx, y);
            M5Cardputer.Display.print(" ");
            cx += M5Cardputer.Display.textWidth(" ");
        }
    }
}

// 提示小字：',' 左箭头，'.' 右箭头
void drawHintText(const int x, const int y, const char* text, const int text_size) {
    const int size = (text_size == 2) ? 2 : 1;
    M5Cardputer.Display.setTextSize(size);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    int cx = x;
    const int arrow_cy = y + 4 * size;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == ',') {
            drawIconArrowLeft(cx, arrow_cy, APP_COLOR_HINT);
            cx += ICON_ARROW_W + 2;
        } else if (*p == '.') {
            drawIconArrowRight(cx, arrow_cy, APP_COLOR_HINT);
            cx += ICON_ARROW_W + 2;
        } else {
            M5Cardputer.Display.setCursor(cx, y);
            const char ch[2] = {*p, '\0'};
            M5Cardputer.Display.print(ch);
            cx += M5Cardputer.Display.textWidth(ch);
        }
    }
}

void drawInfoLineAt(const int x, const int y, const char* label, const char* value,
                    const int text_size) {
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.print(": ");
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.println(value);
}

void drawInfoLine(const int x, int& y, const char* label, const char* value) {
    drawInfoLineAt(x, y, label, value, 1);
    y += INFO_LINE_H;
}

const char* getChargingStatusText() {
    switch (M5Cardputer.Power.isCharging()) {
        case m5::Power_Class::is_charging_t::is_charging:
            return "ON";
        case m5::Power_Class::is_charging_t::is_discharging:
            return "OFF";
        default:
            return "N/A";
    }
}

bool isBatteryCharging() {
    return M5Cardputer.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
}

void drawInfoLineInt(const int x, int& y, const char* label, const int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    drawInfoLine(x, y, label, buf);
}

bool ensureConfigWifi(const uint32_t timeout_ms) {
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

    const uint32_t deadline = millis() + timeout_ms;
    while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

void releaseConfigWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    // 立即刷新 header，避免 WiFi 图标残影
    updateAppHeaderStatus();
}

String getPressedKey() {
    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    String key;
    for (const char c : status.word) {
        key += c;
    }
    return key;
}

// 排空键盘/BtnA：等待全部松开，再吞掉唤醒/松开产生的边沿事件
void flushCardputerInput() {
    constexpr uint32_t kReleaseTimeoutMs = 3000;
    const uint32_t start = millis();
    while (millis() - start < kReleaseTimeoutMs) {
        M5Cardputer.update();
        const bool any_down =
            M5Cardputer.BtnA.isPressed() || M5Cardputer.Keyboard.isPressed() != 0;
        if (!any_down) {
            break;
        }
        delay(10);
    }

    // 吞掉 isChange / wasPressed，避免休眠期间按键在醒来后触发菜单
    for (int i = 0; i < 8; i++) {
        M5Cardputer.update();
        (void)M5Cardputer.Keyboard.isChange();
        (void)M5Cardputer.BtnA.wasPressed();
        (void)M5Cardputer.BtnA.wasReleased();
        delay(10);
    }
}

// 检测翻页键：-1 上一页，0 无，1 下一页
int getMenuNavDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x50 || hid == 0x33 || hid == 0x36) {
            return -1;  // Up / Left / ; ,
        }
        if (hid == 0x51 || hid == 0x4F || hid == 0x37 || hid == 0x38) {
            return 1;   // Down / Right / . /
        }
    }
    for (const char c : status.word) {
        if (c == ';' || c == ',') {
            return -1;
        }
        if (c == '.' || c == '/') {
            return 1;
        }
    }
    return 0;
}
