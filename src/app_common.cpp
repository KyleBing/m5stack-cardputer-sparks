#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_icons.h"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <time.h>
#include <driver/gpio.h>

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

    const int text_y = y + 1; // 普通文字下移 1px，徽章不动
    int cx = x;
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(color, BLACK);

    for (int i = 0; i < item_count; i++) {
        const KeyHintItem& item = items[i];
        cx += drawKeyBadge(cx, y, item.key, text_size);
        M5Cardputer.Display.setCursor(cx, text_y);
        M5Cardputer.Display.setTextColor(color, BLACK);
        M5Cardputer.Display.print(item.text);
        cx += M5Cardputer.Display.textWidth(item.text);
        if (i != item_count - 1) {
            M5Cardputer.Display.setCursor(cx, text_y);
            M5Cardputer.Display.print(" ");
            cx += M5Cardputer.Display.textWidth(" ");
        }
    }
}

// 底栏右下角 h help/close（徽章不动，说明文字下移 1px；y_offset 整行下移）
void drawHelpHintRight(const char* help_label, const int y_offset) {
    const char* label = (help_label != nullptr && help_label[0] != '\0') ? help_label : "help";
    const int y = M5Cardputer.Display.height() - 12 + y_offset;
    const int text_y = y + 1;
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
    M5Cardputer.Display.setCursor(cx, text_y);
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
    return ensureStaWifi(timeout_ms);
}

void releaseConfigWifi() {
    releaseStaWifi();
}

void forceReleaseConfigWifi() {
    forceShutdownStaWifi();
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

// 检测 [ ] 翻页键：-1 上一页，0 无，1 下一页
int getBracketNavDelta(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '[') {
            return -1;
        }
        if (c == ']') {
            return 1;
        }
    }
    return 0;
}

// 音量连续调节时，空闲后再写 LittleFS，避免挡界面刷新
static constexpr uint32_t SPK_VOL_SAVE_DEBOUNCE_MS = 400;
static uint32_t g_spk_last_ready_ms = 0;
static uint8_t g_spk_vol_to_save = 25;
static bool g_spk_vol_dirty = false;
static uint32_t g_spk_vol_dirty_ms = 0;
// 提示音结束后延后关功放（0=无待释放）
static uint32_t g_spk_quiet_at_ms = 0;
// 喇叭脚已拉低 hold：再 release 会 gpio_reset 瞬间浮空 → NS4168 破音
static bool g_spk_pins_held = false;

static void holdSpkPinLow(const int pin) {
    if (pin < 0) {
        return;
    }
    const gpio_num_t gp = static_cast<gpio_num_t>(pin);
    // 播过音后脚仍挂在 I2S 矩阵上；reset → 拉低 → hold，防止再被外设抢走
    gpio_hold_dis(gp);
    gpio_reset_pin(gp);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    gpio_hold_en(gp);
}

void releaseAudioPinHolds() {
    auto dis = [](const int pin) {
        if (pin >= 0) {
            gpio_hold_dis(static_cast<gpio_num_t>(pin));
        }
    };
    const auto spk = M5Cardputer.Speaker.config();
    const auto mic = M5Cardputer.Mic.config();
    dis(spk.pin_data_out);
    dis(spk.pin_bck);
    dis(spk.pin_ws);
    dis(mic.pin_data_in);
    g_spk_pins_held = false;
}

uint8_t getAppSpeakerVolumePercent() {
    if (g_spk_vol_dirty) {
        return g_spk_vol_to_save;
    }
    if (getAppConfig().loaded) {
        return getAppConfig().speaker_volume;
    }
    return 25;
}

void applyAppSpeakerVolume() {
    if (!M5Cardputer.Speaker.isRunning()) {
        return;
    }
    M5Cardputer.Speaker.setVolume(speakerVolumePercentToHw(getAppSpeakerVolumePercent()));
}

void adjustAppSpeakerVolume(const int delta_percent) {
    const int next = constrain(static_cast<int>(getAppSpeakerVolumePercent()) + delta_percent, 0, 100);
    g_spk_vol_to_save = static_cast<uint8_t>(next);
    g_spk_vol_dirty = true;
    g_spk_vol_dirty_ms = millis();
    // 立刻同步内存，避免其它配置 save→loadAppConfig 把 UI 打回旧音量
    setAppConfigSpeakerVolumeLocal(g_spk_vol_to_save);
    applyAppSpeakerVolume();
}

void flushSpeakerVolumeSave() {
    if (!g_spk_vol_dirty) {
        return;
    }
    // 写盘成功才清脏标记；失败则下次 poll 再试
    if (saveAppConfigSpeakerVolume(g_spk_vol_to_save)) {
        g_spk_vol_dirty = false;
    }
}

void pollSpeakerVolumeSave() {
    if (!g_spk_vol_dirty) {
        return;
    }
    if (static_cast<int32_t>(millis() - g_spk_vol_dirty_ms) < static_cast<int32_t>(SPK_VOL_SAVE_DEBOUNCE_MS)) {
        return;
    }
    flushSpeakerVolumeSave();
}

// 关 I2S 并把喇叭脚拉低：Cardputer NS4168 在 BCLK/SDATA/LRCLK 悬空时会嗡嗡
void releaseSpeakerQuiet() {
    g_spk_quiet_at_ms = 0;
    const bool spk_running = M5Cardputer.Speaker.isRunning();
    // 已静音且脚已 hold：跳过，避免 showMenu 等路径重复 gpio_reset 破音
    if (!spk_running && g_spk_pins_held) {
        g_spk_last_ready_ms = 0;
        return;
    }
    // 已在跑：先静音再卸，减轻 end 瞬间破音；未 begin 则只拉脚
    if (spk_running) {
        M5Cardputer.Speaker.setVolume(0);
        M5Cardputer.Speaker.stop();
        M5Cardputer.Speaker.end();
    }
    const auto cfg = M5Cardputer.Speaker.config();
    const auto mic = M5Cardputer.Mic.config();
    holdSpkPinLow(cfg.pin_data_out);
    holdSpkPinLow(cfg.pin_bck);
    // LRCLK(G43) 与 PDM Mic CLK 共用；Mic 运行时由 Mic 驱动，不要抢
    if (!M5Cardputer.Mic.isRunning()) {
        holdSpkPinLow(cfg.pin_ws);
        holdSpkPinLow(mic.pin_data_in); // G46 一并拉住，避免浮空耦合
        g_spk_pins_held = true;
    } else {
        // Mic 占用 WS 时未 hold 全套脚，下次仍需再走一遍
        g_spk_pins_held = false;
    }
    g_spk_last_ready_ms = 0;
}

// Mic 卸 PDM 后 G43 常仍挂在矩阵上，仅 gpio hold 压不住 NS4168；
// 与进 Time 播键音同理：先 Speaker.begin 抢回脚，再静音 end + hold。
void reclaimAndReleaseSpeakerQuiet() {
    if (M5Cardputer.Mic.isRunning()) {
        // 仍在采麦时不要抢 WS
        releaseSpeakerQuiet();
        return;
    }
    releaseAudioPinHolds();
    if (!M5Cardputer.Speaker.isRunning()) {
        M5Cardputer.Speaker.begin();
    }
    if (M5Cardputer.Speaker.isRunning()) {
        M5Cardputer.Speaker.setVolume(0);
        M5Cardputer.Speaker.stop();
        delay(10);
        M5Cardputer.Speaker.end();
        delay(15); // 等 Speaker I2S 矩阵松开再 hold
    }
    releaseSpeakerQuiet();
}

void pollSpeakerQuietRelease() {
    // 已取消提示音播完自动静音；保留接口供 cancel 清零
    if (g_spk_quiet_at_ms == 0) {
        return;
    }
    g_spk_quiet_at_ms = 0;
}

void cancelSpeakerQuietRelease() {
    g_spk_quiet_at_ms = 0;
}

// 需要出声时 begin 并套用音量（不再静音预热，避免 end/冷启动破音）
void warmUpSpeakerIfNeeded() {
    g_spk_quiet_at_ms = 0;
    releaseAudioPinHolds(); // 内含 g_spk_pins_held = false
    if (!M5Cardputer.Speaker.isRunning()) {
        M5Cardputer.Speaker.begin();
    }
    applyAppSpeakerVolume();
    g_spk_last_ready_ms = millis();
}

void playUiTone(const float freq_hz, const uint32_t duration_ms, const bool auto_quiet) {
    (void)auto_quiet; // 已取消播完自动静音（冷启动易破音）
    warmUpSpeakerIfNeeded();
    M5Cardputer.Speaker.tone(freq_hz, duration_ms);
    g_spk_last_ready_ms = millis();
    g_spk_quiet_at_ms = 0;
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
