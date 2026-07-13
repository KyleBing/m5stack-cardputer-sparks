#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_icons.h"
#include <WiFi.h>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <time.h>

// 应用本地时区（优先 config.json 的 timezone，否则默认东八区）
void applyLocalTimezone() {
    setenv("TZ", getAppTimezone(), 1);
    tzset();
}

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

// 绘制文本徽章（黄底黑字，样式与 drawKeyBadge 一致）
int drawTextBadge(const int x, const int y, const char* label, const int text_size) {
    if (label == nullptr || label[0] == '\0') {
        return 0;
    }
    const int size = (text_size == 2) ? 2 : 1;
    M5Cardputer.Display.setTextSize(size);
    const int tw = M5Cardputer.Display.textWidth(label);
    const int th = 8 * size;
    constexpr int pad_x = 2;
    constexpr int pad_y = 1;
    const int bw = tw + pad_x * 2;
    const int bh = th + pad_y * 2;

    M5Cardputer.Display.fillRoundRect(x, y, bw, bh, 2, APP_COLOR_MENU_KEY);
    M5Cardputer.Display.setTextColor(APP_COLOR_KEY_TEXT, APP_COLOR_MENU_KEY);
    M5Cardputer.Display.setCursor(x + pad_x, y + pad_y);
    M5Cardputer.Display.print(label);

    constexpr int gap = 3;
    return bw + gap;
}

// 绘制箭头徽章（黄底黑箭头，样式与 drawKeyBadge 一致）
static int drawArrowBadgeImpl(const int x, const int y, const int text_size, const int icon_w,
                              const int icon_h,
                              void (*draw_icon)(int, int, uint16_t)) {
    const int size = (text_size == 2) ? 2 : 1;
    constexpr int pad_x = 2;
    constexpr int pad_y = 1;
    const int bw = icon_w + pad_x * 2;
    const int bh = icon_h + pad_y * 2 + (size - 1) * 4;
    const int icon_cy = y + bh / 2;

    M5Cardputer.Display.fillRoundRect(x, y, bw, bh, 2, APP_COLOR_MENU_KEY);
    draw_icon(x + pad_x, icon_cy, APP_COLOR_KEY_TEXT);

    constexpr int gap = 3;
    return bw + gap;
}

// 绘制左右箭头徽章（黄底黑箭头，样式与 drawKeyBadge 一致）
int drawArrowBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_LR_W, ICON_ARROW_H, drawIconArrowLeftRight);
}

int drawArrowUpDownBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_W, ICON_ARROW_UD_H, drawIconArrowUpDown);
}

int drawArrowLeftBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_W, ICON_ARROW_H, drawIconArrowLeft);
}

int drawArrowRightBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_W, ICON_ARROW_H, drawIconArrowRight);
}

int drawArrowUpBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_W, ICON_ARROW_H, drawIconArrowUp);
}

int drawArrowDownBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_W, ICON_ARROW_H, drawIconArrowDown);
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

// 底栏右下角 h help/close
void drawHelpHintRight(const char* help_label) {
    const char* label = (help_label != nullptr && help_label[0] != '\0') ? help_label : "help";
    const int y = M5Cardputer.Display.height() - 12;
    const int screen_w = M5Cardputer.Display.width();
    const KeyHintItem help_item = {'h', label};

    M5Cardputer.Display.setTextSize(1);
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(help_item.key)));
    const char str[2] = {letter, '\0'};
    const int tw = M5Cardputer.Display.textWidth(str);
    constexpr int pad_x = 2;
    const int badge_w = tw + pad_x * 2 + 3;
    const int help_w = badge_w + M5Cardputer.Display.textWidth(help_item.text);
    const int hx = screen_w - APP_CONTENT_X - help_w;

    int cx = hx + drawKeyBadge(hx, y, help_item.key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(help_item.text);
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
    applyWifiRadioSleepPolicy();
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

// btngo：边沿检测用（休眠唤醒后需 resetBtnGoEdge）
static bool s_btngo_last_down = false;

// btngo：提示标签（UI 文案，不显示物理键符 `）
const char* btnGoHintLabel() {
#if BTNGO_USE_KEYBOARD
    return "ESC";
#else
    return "GO";  // 侧边 BtnA
#endif
}

void resetBtnGoEdge() {
    s_btngo_last_down = false;
}

// btngo：是否按下返回主菜单键（边沿触发）
bool wasBtnGoPressed() {
#if BTNGO_USE_KEYBOARD
    // 勿调用 Keyboard.isChange()：它会改写 _last_key_size，吞掉边沿导致其它按键失效
    bool down = false;
    if (M5Cardputer.Keyboard.isPressed()) {
        const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        for (const uint8_t hid : status.hid_keys) {
            if (hid == BTNGO_HID) {
                down = true;
                break;
            }
        }
        if (!down) {
            for (const char c : status.word) {
                if (c == BTNGO_KEY_CHAR || c == '~') {
                    down = true;
                    break;
                }
            }
        }
    }
    const bool edge = down && !s_btngo_last_down;
    s_btngo_last_down = down;
    return edge;
#else
    return M5Cardputer.BtnA.wasPressed();
#endif
}

// 排空键盘/BtnA：等待松开，再吞掉唤醒/松开产生的边沿事件
void flushCardputerInput(const bool wait_btn_a) {
    constexpr uint32_t kReleaseTimeoutMs = 3000;
    const uint32_t start = millis();
    while (millis() - start < kReleaseTimeoutMs) {
        M5Cardputer.update();
        const bool kb_down = M5Cardputer.Keyboard.isPressed() != 0;
        const bool btn_down = wait_btn_a && M5Cardputer.BtnA.isPressed();
        if (!kb_down && !btn_down) {
            // 再稳定几帧，避免矩阵抖动留下鬼键
            bool stable = true;
            for (int i = 0; i < 5; i++) {
                delay(10);
                M5Cardputer.update();
                if (M5Cardputer.Keyboard.isPressed() != 0 ||
                    (wait_btn_a && M5Cardputer.BtnA.isPressed())) {
                    stable = false;
                    break;
                }
            }
            if (stable) {
                break;
            }
        }
        delay(10);
    }

    // 吞掉 isChange / wasPressed，同步 Keyboard._last_key_size
    for (int i = 0; i < 12; i++) {
        M5Cardputer.update();
        (void)M5Cardputer.Keyboard.isChange();
        (void)M5Cardputer.BtnA.wasPressed();
        (void)M5Cardputer.BtnA.wasReleased();
        delay(10);
    }
    resetBtnGoEdge();

    // 唤醒键仍可能按住：短等松开并再吞一次边沿（不阻塞太久）
    if (!wait_btn_a) {
        const uint32_t btn_start = millis();
        while (millis() - btn_start < 1200) {
            M5Cardputer.update();
            if (!M5Cardputer.BtnA.isPressed()) {
                break;
            }
            delay(10);
        }
        for (int i = 0; i < 6; i++) {
            M5Cardputer.update();
            (void)M5Cardputer.Keyboard.isChange();
            (void)M5Cardputer.BtnA.wasPressed();
            (void)M5Cardputer.BtnA.wasReleased();
            delay(10);
        }
        resetBtnGoEdge();
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

// 距上次有效出声超过该间隔则认为功放/I2S 已冷掉
static constexpr uint32_t SPK_WARM_IDLE_MS = 30000;
static uint32_t g_spk_last_ready_ms = 0;

// I2S 与功放冷启动时前几十毫秒常丢样，先静音跑一段预热
void warmUpSpeakerIfNeeded() {
    const uint32_t now = millis();
    if (M5Cardputer.Speaker.isRunning() && (now - g_spk_last_ready_ms) < SPK_WARM_IDLE_MS) {
        return;
    }
    const uint8_t vol = M5Cardputer.Speaker.getVolume();
    M5Cardputer.Speaker.setVolume(0);
    M5Cardputer.Speaker.tone(1000, 80);
    delay(100); // 等 DMA 起转、功放就绪
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.setVolume(vol == 0 ? 64 : vol);
    g_spk_last_ready_ms = millis();
}

void playUiTone(const float freq_hz, const uint32_t duration_ms) {
    warmUpSpeakerIfNeeded();
    M5Cardputer.Speaker.tone(freq_hz, duration_ms);
    g_spk_last_ready_ms = millis();
}

bool isTimeKeySoundEnabled() {
    // 未加载配置时默认开启
    if (!getAppConfig().loaded) {
        return true;
    }
    return getAppConfig().time_key_sound;
}

bool isMijiaOnOffSoundEnabled() {
    if (!getAppConfig().loaded) {
        return true;
    }
    return getAppConfig().mijia_on_off_sound;
}

void playTimeKeyTone(const float freq_hz, const uint32_t duration_ms) {
    if (!isTimeKeySoundEnabled()) {
        return;
    }
    playUiTone(freq_hz, duration_ms);
}
