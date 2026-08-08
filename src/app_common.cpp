#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_icons.h"
#include <cctype>
#include <cmath>
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

int drawArrowUpDownFlatBadge(const int x, const int y, const int text_size) {
    return drawArrowBadgeImpl(x, y, text_size, ICON_ARROW_UD_FLAT_W, ICON_ARROW_H,
                              drawIconArrowUpDownFlat);
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

// 全屏 Help：无 header，黑底 + size-2 "Help" + 可选浅色副标题
int drawAppHelpBegin(const char* subtitle) {
    M5Cardputer.Display.fillScreen(BLACK);
    constexpr int title_y = APP_HELP_EDGE;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X, title_y);
    M5Cardputer.Display.print("Help");

    if (subtitle != nullptr && subtitle[0] != '\0') {
        const int help_w = M5Cardputer.Display.textWidth("Help");
        // size-2≈16px、size-1≈8px，副标题垂直居中
        const int sub_y = title_y + 4;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 浅色区分主标题
        M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X + help_w + APP_HELP_SUBTITLE_GAP, sub_y);
        M5Cardputer.Display.print(subtitle);
    }
    return title_y + 16 + 7; // 标题高 + 与内容区间距
}

int drawAppHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + APP_HELP_LINE_H;
}

int drawAppHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + APP_HELP_LINE_H;
}

int drawAppHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + APP_HELP_LINE_H;
}

int drawAppHelpArrows(const int x, const int y, const char* text) {
    const int cx = x + drawArrowBadge(x, y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + APP_HELP_LINE_H;
}

int drawAppHelpTextColored(const int x, const int y, const char* text, const uint16_t color) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + APP_HELP_LINE_H;
}

int drawAppHelpLabelText(const int x, const int y, const char* label, const uint16_t label_color,
                         const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(label_color, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 标签后恢复说明色
    M5Cardputer.Display.print(text);
    return y + APP_HELP_LINE_H;
}

// 多页：左下箭头徽章 + N/M；右侧统一 h close
void drawAppHelpFooter(const int page, const int page_count) {
    if (page_count > 1) {
        const int hint_y = M5Cardputer.Display.height() - 12;
        int cx = APP_HELP_CONTENT_X;
        cx += drawArrowBadge(cx, hint_y, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复 tip 色
        M5Cardputer.Display.setCursor(cx, hint_y + 1);
        char buf[12];
        const int safe_page = page < 0 ? 0 : page;
        snprintf(buf, sizeof(buf), "%d/%d", safe_page + 1, page_count);
        M5Cardputer.Display.print(buf);
    }
    drawHelpHintRight("close");
}

int getHelpNavDelta(const Keyboard_Class::KeysState& status) {
    int delta = getMenuNavDelta(status);
    if (delta == 0) {
        delta = getBracketNavDelta(status);
    }
    return delta;
}

int applyHelpPageDelta(const int page, const int page_count, const int delta) {
    if (page_count <= 1 || delta == 0) {
        return page;
    }
    return (page + (delta % page_count) + page_count) % page_count;
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
    // Help / tip 右侧同样至少留 APP_HELP_EDGE
    const int hx = screen_w - APP_HELP_EDGE - help_w;

    int cx = hx + drawKeyBadge(hx, y, help_item.key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print(help_item.text);
}

// 提示小字：',' 左箭头，'.' 右箭头
// 横向进度条：已占用全高实心无边框；未占用完整边框（含左右）
void drawPercentBar(const int x, const int y, const int w, const int h, const int percent,
                    const uint16_t fill_color, const uint16_t border_color,
                    const uint16_t empty_bg) {
    if (w <= 0 || h <= 0) {
        return;
    }
    const int pct = constrain(percent, 0, 100);
    const int fill_w = w * pct / 100;
    if (fill_w > 0) {
        M5Cardputer.Display.fillRect(x, y, fill_w, h, fill_color);
    }
    const int empty_w = w - fill_w;
    if (empty_w <= 0) {
        return;
    }
    const int empty_x = x + fill_w;
    if (empty_w > 2 && h > 2) {
        M5Cardputer.Display.fillRect(empty_x + 1, y + 1, empty_w - 2, h - 2, empty_bg);
    }
    M5Cardputer.Display.drawRect(empty_x, y, empty_w, h, border_color);
}

// 纵向进度条：自下而上全宽实心；未占用完整边框（含上下）
void drawPercentBarV(const int x, const int y, const int w, const int h, const int percent,
                     const uint16_t fill_color, const uint16_t border_color,
                     const uint16_t empty_bg) {
    if (w <= 0 || h <= 0) {
        return;
    }
    const int pct = constrain(percent, 0, 100);
    const int fill_h = h * pct / 100;
    if (fill_h > 0) {
        M5Cardputer.Display.fillRect(x, y + h - fill_h, w, fill_h, fill_color);
    }
    const int empty_h = h - fill_h;
    if (empty_h <= 0) {
        return;
    }
    if (w > 2 && empty_h > 2) {
        M5Cardputer.Display.fillRect(x + 1, y + 1, w - 2, empty_h - 2, empty_bg);
    }
    M5Cardputer.Display.drawRect(x, y, w, empty_h, border_color);
}

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

int measureDotTextWidth1x(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    const int w = M5Cardputer.Display.textWidth(text);
    M5Cardputer.Display.setTextFont(1);
    return w;
}

// 点阵风格文字：先 1x 渲染到离屏 sprite，再按 scale 画带 1px 缝隙的方块
void drawDotText(const char* text, const int x, const int y, const int scale,
                 const uint16_t color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    const int w = M5Cardputer.Display.textWidth(text);
    M5Canvas spr(&M5Cardputer.Display);
    spr.setColorDepth(16);
    if (scale < 2 || w <= 0 || !spr.createSprite(w, DOT_TEXT_H_1X)) {
        M5Cardputer.Display.setTextSize(scale < 1 ? 1 : scale);
        M5Cardputer.Display.setTextColor(color, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(text);
        M5Cardputer.Display.setTextFont(1);
        M5Cardputer.Display.setTextSize(1);
        return;
    }
    spr.setFont(&fonts::Font0);
    spr.setTextSize(1);
    spr.fillSprite(BLACK);
    spr.setTextColor(WHITE, BLACK);
    spr.setCursor(0, 0);
    spr.print(text);

    const int block = scale - 1; // 留 1px 缝隙
    for (int py = 0; py < DOT_TEXT_H_1X; py++) {
        for (int px = 0; px < w; px++) {
            if (spr.readPixel(px, py) != 0) {
                M5Cardputer.Display.fillRect(x + px * scale, y + py * scale, block, block, color);
            }
        }
    }
    spr.deleteSprite();
    M5Cardputer.Display.setTextFont(1);
    M5Cardputer.Display.setTextSize(1);
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

bool isScreenshotSoundEnabled() {
    if (!getAppConfig().loaded) {
        return true;
    }
    return getAppConfig().screenshot_sound;
}

void playTimeKeyTone(const float freq_hz, const uint32_t duration_ms) {
    if (!isTimeKeySoundEnabled()) {
        return;
    }
    playUiTone(freq_hz, duration_ms);
}

// ===== IMU 倾斜方向 =====

// 主循环调用很密，限一下采样频率省 I2C
static constexpr uint32_t IMU_TILT_SAMPLE_MS = 15;
// 低通系数：越小越稳，越大越跟手
static constexpr float IMU_TILT_SMOOTH = 0.40f;
// 主轴优势倍数：斜着倾时避免两个方向来回跳，越接近 1 越容易认斜着的那一侧
static constexpr float IMU_TILT_DOMINANCE = 1.15f;
// Cardputer 屏幕上方对应机身加速度的 -y 轴；屏幕右方不写死，
// 由「上方 × 屏幕法线」推出，法线取校准时的重力方向（握持时屏幕总是朝上的那一侧）
static constexpr float IMU_SCREEN_UP_AXIS[3] = {0.0f, -1.0f, 0.0f};
// 屏幕立得几乎垂直时上方轴会与重力重合，退化用这个轴先定右方
static constexpr float IMU_SCREEN_RIGHT_FALLBACK[3] = {-1.0f, 0.0f, 0.0f};

static float imuDot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void imuCross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// 把机身轴投影到「垂直于重力」的平面上再归一化；几乎与重力同向时退化返回 false
static bool imuProjectPerp(const float axis[3], const float base[3], float out[3]) {
    const float along = imuDot3(axis, base);
    for (int i = 0; i < 3; ++i) {
        out[i] = axis[i] - along * base[i];
    }
    const float norm = sqrtf(imuDot3(out, out));
    if (norm < 0.25f) {
        out[0] = axis[0];
        out[1] = axis[1];
        out[2] = axis[2];
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        out[i] /= norm;
    }
    return true;
}

bool isImuTiltAvailable() {
    return M5.Imu.isEnabled();
}

// 倾得越多走得越快，接近鼠标那种「一直倾就一直走」的手感
static uint32_t imuRepeatInterval(const ImuTiltConfig& cfg, const float mag) {
    if (cfg.repeat_slow_ms == 0) {
        return 0;
    }
    if (cfg.repeat_fast_ms == 0 || cfg.repeat_fast_ms >= cfg.repeat_slow_ms) {
        return cfg.repeat_slow_ms;
    }
    const float span = cfg.full - cfg.enter;
    float t = (span > 0.01f) ? (mag - cfg.enter) / span : 1.0f;
    t = fmaxf(0.0f, fminf(1.0f, t));
    const float slow = static_cast<float>(cfg.repeat_slow_ms);
    const float fast = static_cast<float>(cfg.repeat_fast_ms);
    return static_cast<uint32_t>(slow - (slow - fast) * t);
}

void imuTiltReset(ImuTiltState& state) {
    state = ImuTiltState{};
}

bool imuTiltPoll(ImuTiltState& state, const ImuTiltConfig& cfg, int& dx, int& dy) {
    dx = 0;
    dy = 0;
    if (!M5.Imu.isEnabled()) {
        return false;
    }
    const uint32_t now = millis();
    if (state.base_ready && now - state.last_sample_ms < IMU_TILT_SAMPLE_MS) {
        return false;
    }
    state.last_sample_ms = now;

    M5.Imu.update();
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    M5.Imu.getAccel(&ax, &ay, &az);

    // 只取重力方向，晃动导致的模长变化不参与判定
    float g[3] = {ax, ay, az};
    const float g_norm = sqrtf(imuDot3(g, g));
    if (g_norm < 0.35f) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        g[i] /= g_norm;
    }

    if (!state.base_ready) {
        // 校准：记下当前重力方向，并在垂直于它的平面里建立屏幕上 / 右两个基向量。
        // 直接拿单轴分量做基准的话，端着的姿态本身已经吃掉大半量程，
        // 继续朝同一侧倾会顶到 ±1g 没法再变大，那个方向就永远触发不了。
        for (int i = 0; i < 3; ++i) {
            state.base[i] = g[i];
        }
        if (imuProjectPerp(IMU_SCREEN_UP_AXIS, state.base, state.up)) {
            imuCross3(state.up, state.base, state.right);
        } else {
            imuProjectPerp(IMU_SCREEN_RIGHT_FALLBACK, state.base, state.right);
            imuCross3(state.base, state.right, state.up);
        }
        state.tilt_x = 0.0f;
        state.tilt_y = 0.0f;
        state.base_ready = true;
        return false;
    }

    // 去掉沿校准重力的分量，剩下的垂直分量就是相对校准姿态的倾斜，模长即倾角正弦，
    // 四个方向量程对称，握持角度再大也不会有哪一侧顶到量程尽头
    const float along = imuDot3(g, state.base);
    float perp[3];
    for (int i = 0; i < 3; ++i) {
        perp[i] = g[i] - along * state.base[i];
    }
    const float raw_x = imuDot3(perp, state.right);
    const float raw_y = -imuDot3(perp, state.up);
    state.tilt_x += (raw_x - state.tilt_x) * IMU_TILT_SMOOTH;
    state.tilt_y += (raw_y - state.tilt_y) * IMU_TILT_SMOOTH;

    const float mag_x = fabsf(state.tilt_x);
    const float mag_y = fabsf(state.tilt_y);
    int next_x = 0;
    int next_y = 0;
    // 已经在某个方向上时用较低的 leave 阈值，回中才算松开
    if (mag_x >= mag_y * IMU_TILT_DOMINANCE) {
        if (mag_x >= ((state.dir_x != 0) ? cfg.leave : cfg.enter)) {
            next_x = (state.tilt_x > 0.0f) ? 1 : -1;
        }
    } else if (mag_y >= mag_x * IMU_TILT_DOMINANCE) {
        if (mag_y >= ((state.dir_y != 0) ? cfg.leave : cfg.enter)) {
            next_y = (state.tilt_y > 0.0f) ? 1 : -1;
        }
    }

    if (next_x == 0 && next_y == 0) {
        state.dir_x = 0;
        state.dir_y = 0;
        return false;
    }
    if (next_x != state.dir_x || next_y != state.dir_y) {
        state.dir_x = static_cast<int8_t>(next_x);
        state.dir_y = static_cast<int8_t>(next_y);
        state.hold_since_ms = now;
        state.last_emit_ms = now;
        dx = next_x;
        dy = next_y;
        return true;
    }
    const uint32_t interval = imuRepeatInterval(cfg, fmaxf(mag_x, mag_y));
    if (interval == 0) {
        return false;
    }
    if (now - state.hold_since_ms >= cfg.repeat_delay_ms &&
        now - state.last_emit_ms >= interval) {
        state.last_emit_ms = now;
        dx = next_x;
        dy = next_y;
        return true;
    }
    return false;
}
