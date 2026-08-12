#include "app_radio.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include "tea5767.h"
#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace {

static constexpr int RADIO_PRESET_COUNT = 10;
static constexpr int RADIO_NAME_MAX = 16;
static constexpr int DIAL_MARGIN = 14; // 两端留空，MHz 数字不被裁切
static constexpr int RADIO_UI_LEFT = APP_CONTENT_X;
static constexpr int RADIO_UI_TOP = APP_CONTENT_INSET_Y;

struct RadioPreset {
    uint16_t freq; // 0 = 空槽
    char name[RADIO_NAME_MAX + 1];
};

enum class RadioView {
    Main,
    Stations,
    Rename,
};

static Tea5767 g_radio;
static M5Canvas radioCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static int g_canvas_w = 0;
static int g_canvas_h = 0;
static bool g_ready = false;
static bool g_help = false;
static int g_help_page = 0;
static constexpr int RADIO_HELP_PAGES = 2;
static RadioView g_view = RadioView::Main;
static bool g_muted = false;
static bool g_mono = false;
static bool g_seeking = false;
static uint16_t g_freq = 9850; // 98.50 MHz
static uint8_t g_rssi = 0;
static bool g_stereo = false;
static uint32_t g_seek_anim_ms = 0;
static int g_seek_frame = 0;
static RadioPreset g_presets[RADIO_PRESET_COUNT]{};
static int g_sel_slot = 0;
static int g_active_slot = -1;
static char g_rename_buf[RADIO_NAME_MAX + 1];
static int8_t g_tune_repeat_dir = 0;
static uint32_t g_tune_repeat_since_ms = 0;
static uint32_t g_tune_repeat_last_ms = 0;
static constexpr uint32_t RADIO_TUNE_REPEAT_DELAY_MS = 320;
static constexpr uint32_t RADIO_TUNE_REPEAT_RATE_MS = 90;

static int mapFreqToX(const int x0, const int w, const uint16_t freq_centi) {
    const long in = static_cast<long>(freq_centi);
    return x0 + static_cast<int>((in - Tea5767::FREQ_MIN) * w / (Tea5767::FREQ_MAX - Tea5767::FREQ_MIN));
}

static void formatFreqText(const uint16_t freq_centi, char* out, const size_t out_len) {
    const unsigned whole = freq_centi / 100;
    const unsigned frac = (freq_centi / 10) % 10;
    snprintf(out, out_len, "%u.%u", whole, frac);
}

static bool isPresetValid(const RadioPreset& preset) {
    return preset.freq >= Tea5767::FREQ_MIN && preset.freq <= Tea5767::FREQ_MAX;
}

static void presetLabel(const RadioPreset& preset, char* out, const size_t out_len) {
    if (preset.name[0] != '\0') {
        snprintf(out, out_len, "%s", preset.name);
        return;
    }
    if (isPresetValid(preset)) {
        char freq_text[12];
        formatFreqText(preset.freq, freq_text, sizeof(freq_text));
        snprintf(out, out_len, "%s MHz", freq_text);
        return;
    }
    snprintf(out, out_len, "Empty");
}

static void loadRadioPresets() {
    Preferences prefs;
    if (!prefs.begin("radio", true)) {
        return;
    }
    for (int i = 0; i < RADIO_PRESET_COUNT; ++i) {
        char key[4];
        snprintf(key, sizeof(key), "f%d", i);
        g_presets[i].freq = prefs.getUShort(key, 0);
        snprintf(key, sizeof(key), "n%d", i);
        const String name = prefs.getString(key, "");
        strncpy(g_presets[i].name, name.c_str(), RADIO_NAME_MAX);
        g_presets[i].name[RADIO_NAME_MAX] = '\0';
    }
    prefs.end();
}

static void saveRadioPreset(const int idx) {
    if (idx < 0 || idx >= RADIO_PRESET_COUNT) {
        return;
    }
    Preferences prefs;
    if (!prefs.begin("radio", false)) {
        return;
    }
    char key[4];
    snprintf(key, sizeof(key), "f%d", idx);
    prefs.putUShort(key, g_presets[idx].freq);
    snprintf(key, sizeof(key), "n%d", idx);
    prefs.putString(key, g_presets[idx].name);
    prefs.end();
}

// 点阵字绘制到离屏 canvas（与 drawDotText 同风格）
static void drawDotTextOnCanvas(const char* text, const int x, const int y, const int scale,
                                const uint16_t color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    radioCanvas.setFont(&fonts::Font0);
    radioCanvas.setTextSize(1);
    const int w = radioCanvas.textWidth(text);
    M5Canvas spr(&M5Cardputer.Display);
    spr.setColorDepth(16);
    if (scale < 2 || w <= 0 || !spr.createSprite(w, DOT_TEXT_H_1X)) {
        radioCanvas.setTextSize(scale < 1 ? 1 : scale);
        radioCanvas.setTextColor(color, BLACK);
        radioCanvas.setCursor(x, y);
        radioCanvas.print(text);
        radioCanvas.setTextFont(1);
        radioCanvas.setTextSize(1);
        return;
    }
    spr.setFont(&fonts::Font0);
    spr.setTextSize(1);
    spr.fillSprite(BLACK);
    spr.setTextColor(WHITE, BLACK);
    spr.setCursor(0, 0);
    spr.print(text);

    const int block = scale - 1;
    for (int py = 0; py < DOT_TEXT_H_1X; ++py) {
        for (int px = 0; px < w; ++px) {
            if (spr.readPixel(px, py) != 0) {
                radioCanvas.fillRect(x + px * scale, y + py * scale, block, block, color);
            }
        }
    }
    spr.deleteSprite();
    radioCanvas.setTextFont(1);
    radioCanvas.setTextSize(1);
}

// FM 频段刻度：高 30px、距顶 10px；偶数 MHz 长刻度+数字，奇数 MHz 短刻度
static void drawDial(const uint16_t freq_centi) {
    constexpr int dial_y = 10;
    constexpr int dial_h = 30;
    const int dial_w = g_canvas_w;
    const int inner_x = DIAL_MARGIN;
    const int inner_w = dial_w - DIAL_MARGIN * 2;
    const int dial_bottom = dial_y + dial_h - 1;

    // 上下边框
    radioCanvas.drawFastHLine(0, dial_y, dial_w, APP_COLOR_MUTED);
    radioCanvas.drawFastHLine(0, dial_bottom, dial_w, APP_COLOR_MUTED);

    for (int mhz = 88; mhz <= 108; ++mhz) {
        const uint16_t tick_freq = static_cast<uint16_t>(mhz * 100);
        if (tick_freq < Tea5767::FREQ_MIN || tick_freq > Tea5767::FREQ_MAX) {
            continue;
        }
        const bool major = (mhz % 2 == 0);
        const int tick_h = major ? 12 : 6;
        const int x = mapFreqToX(inner_x, inner_w, tick_freq);
        radioCanvas.drawFastVLine(x, dial_bottom - tick_h, tick_h, APP_COLOR_HINT);

        if (!major) {
            continue;
        }

        char label[4];
        snprintf(label, sizeof(label), "%d", mhz);
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        const int tw = radioCanvas.textWidth(label);
        int lx = x - tw / 2;
        if (lx < 0) {
            lx = 0;
        }
        if (lx + tw > dial_w) {
            lx = dial_w - tw;
        }
        radioCanvas.setCursor(lx, dial_y + dial_h + 2);
        radioCanvas.print(label);
    }

    // 已保存电台：刻度尺彩色标记（当前槽位用高亮色）
    for (int i = 0; i < RADIO_PRESET_COUNT; ++i) {
        if (!isPresetValid(g_presets[i])) {
            continue;
        }
        const int px = mapFreqToX(inner_x, inner_w, g_presets[i].freq);
        const bool active = (i == g_active_slot);
        const uint16_t color = active ? APP_COLOR_LABEL : APP_COLOR_OK;
        const int mark_h = active ? 10 : 7;
        radioCanvas.drawFastVLine(px, dial_bottom - mark_h, mark_h, color);
        radioCanvas.fillCircle(px, dial_y + 3, 2, color);
    }

    const int px = mapFreqToX(inner_x, inner_w, freq_centi);
    const int needle_x = constrain(px, 0, dial_w - 2);
    radioCanvas.fillRect(needle_x, dial_y + 1, 2, dial_h - 2, RED);
}

static void drawSignalBars(const int x, const int y, const int bar_w, const int bar_gap, const int max_h,
                           const uint8_t rssi, const bool seeking) {
    constexpr int bars = 5;
    for (int i = 0; i < bars; ++i) {
        const int bx = x + i * (bar_w + bar_gap);
        const int bh = 6 + i * 3;
        const int by = y + max_h - bh;
        radioCanvas.drawRect(bx, by, bar_w, bh, APP_COLOR_MUTED);

        int level = 0;
        if (seeking) {
            level = (g_seek_frame + i) % bars;
        } else if (g_ready) {
            level = static_cast<int>((rssi * bars + 14) / 15);
        }
        if (i < level) {
            uint16_t color = APP_COLOR_OK;
            if (i >= bars - 2) {
                color = APP_COLOR_WARN;
            }
            radioCanvas.fillRect(bx + 1, by + bh - (bh - 2), bar_w - 2, bh - 2, color);
        }
    }
}

// 40×40 播放/停止图标：播放=绿色三角，停止=红色圆角方块
static void drawPlayPauseIcon(const int x, const int y) {
    constexpr int size = 40;
    const bool playing = g_ready && !g_muted;
    if (playing) {
        constexpr int pad = 9;
        radioCanvas.fillTriangle(x + pad, y + pad, x + pad, y + size - pad, x + size - pad,
                                 y + size / 2, APP_COLOR_OK);
    } else {
        constexpr int pad = 11;
        radioCanvas.fillRoundRect(x + pad, y + pad, size - pad * 2, size - pad * 2, 3,
                                  APP_COLOR_ERROR);
    }
}

static int drawTextBadgeOnCanvas(const int x, const int y, const char* label, const int text_size) {
    if (label == nullptr || label[0] == '\0') {
        return 0;
    }
    radioCanvas.setTextSize(text_size);
    const int tw = radioCanvas.textWidth(label);
    constexpr int pad_x = 3;
    constexpr int pad_y = 1;
    const int badge_w = tw + pad_x * 2;
    const int badge_h = 8 * text_size + pad_y * 2;
    radioCanvas.fillRoundRect(x, y, badge_w, badge_h, 2, YELLOW);
    radioCanvas.setTextColor(APP_COLOR_KEY_TEXT, YELLOW);
    radioCanvas.setCursor(x + pad_x, y + pad_y);
    radioCanvas.print(label);
    radioCanvas.setTextSize(1);
    return badge_w;
}

static void drawStatusBadges(const int x, const int y) {
    int cx = x;
    if (!g_ready) {
        cx += drawTextBadgeOnCanvas(cx, y, "NO MOD", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
        return;
    }
    if (g_stereo && !g_mono) {
        cx += drawTextBadgeOnCanvas(cx, y, "ST", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    } else {
        cx += drawTextBadgeOnCanvas(cx, y, "MONO", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    }

    if (g_seeking) {
        radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
        radioCanvas.setCursor(cx + 4, y + 1);
        radioCanvas.print("SEEK");
    }
}

static void drawFrequencyDisplay(const uint16_t freq_centi) {
    char freq_text[12];
    formatFreqText(freq_centi, freq_text, sizeof(freq_text));

    constexpr int scale = 3;
    const int text_w = measureDotTextWidth1x(freq_text) * scale;
    const int text_h = DOT_TEXT_H_1X * scale;
    const int freq_x = (g_canvas_w - text_w) / 2;
    constexpr int freq_y = 56;

    drawDotTextOnCanvas(freq_text, freq_x, freq_y, scale, WHITE);

    radioCanvas.setTextSize(1);
    radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    radioCanvas.setCursor(freq_x + text_w + 4, freq_y + text_h - 10);
    radioCanvas.print("MHz");

    if (g_active_slot >= 0 && g_active_slot < RADIO_PRESET_COUNT) {
        char slot_text[6];
        snprintf(slot_text, sizeof(slot_text), "#%d", g_active_slot + 1);
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, freq_y + 4);
        radioCanvas.print(slot_text);
    }
}

static void drawNoModuleHint() {
    if (g_ready) {
        return;
    }
    radioCanvas.setTextSize(1);
    radioCanvas.setTextColor(APP_COLOR_ERROR, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, 82);
    radioCanvas.print("No TEA5767");
    radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, 94);
    radioCanvas.print("Connect to Ex I2C Port A");
}

static void drawStationsList() {
    radioCanvas.fillSprite(BLACK);
    radioCanvas.setTextSize(1);
    radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, RADIO_UI_TOP);
    radioCanvas.print("Stations");

    constexpr int row_h = 12;
    constexpr int list_y = RADIO_UI_TOP + 12;
    for (int i = 0; i < RADIO_PRESET_COUNT; ++i) {
        const int y = list_y + i * row_h;
        const bool sel = (i == g_sel_slot);
        if (sel) {
            radioCanvas.fillRect(0, y - 1, g_canvas_w, row_h, APP_COLOR_LABEL);
        }

        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        radioCanvas.setTextColor(sel ? BLACK : APP_COLOR_HINT, sel ? APP_COLOR_LABEL : BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, y);
        radioCanvas.print(num);

        radioCanvas.setCursor(RADIO_UI_LEFT + 14, y);
        if (g_view == RadioView::Rename && sel) {
            radioCanvas.print(g_rename_buf);
            radioCanvas.print("_");
        } else {
            char label[RADIO_NAME_MAX + 8];
            presetLabel(g_presets[i], label, sizeof(label));
            radioCanvas.print(label);
        }
    }

    if (g_view == RadioView::Rename) {
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, g_canvas_h - 10);
        radioCanvas.print("Enter save  ` cancel");
    }
}

static bool ensureRadioCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    g_canvas_w = M5Cardputer.Display.width();
    g_canvas_h = M5Cardputer.Display.height();
    radioCanvas.setColorDepth(16);
    if (!radioCanvas.createSprite(g_canvas_w, g_canvas_h)) {
        return false;
    }
    g_canvas_ok = true;
    return true;
}

static void pushRadioFrame() {
    if (g_canvas_ok) {
        radioCanvas.pushSprite(0, 0);
    }
}

static void drawRadioMain() {
    if (!g_canvas_ok) {
        return;
    }

    radioCanvas.fillSprite(BLACK);
    drawDial(g_freq);
    drawFrequencyDisplay(g_freq);
    drawNoModuleHint();

    constexpr int icon_size = 40;
    const int icon_x = g_canvas_w - icon_size - RADIO_UI_LEFT;
    drawPlayPauseIcon(icon_x, 54);

    constexpr int meter_y = 112;
    constexpr int bar_w = 5;
    constexpr int bar_gap = 3;
    constexpr int bars = 5;
    const int bars_w = bars * bar_w + (bars - 1) * bar_gap;
    drawSignalBars(RADIO_UI_LEFT, meter_y, bar_w, bar_gap, 18, g_rssi, g_seeking);
    drawStatusBadges(RADIO_UI_LEFT + bars_w + 10, meter_y + 1);

    pushRadioFrame();
}

static void drawRadioChrome() {
    if (g_view == RadioView::Stations || g_view == RadioView::Rename) {
        drawStationsList();
        pushRadioFrame();
        return;
    }
    drawRadioMain();
}

static void drawHelpPage() {
    beginAppScreen("Radio");
    int y = RADIO_UI_TOP;
    constexpr int x = RADIO_UI_LEFT;
    auto helpKeyLine = [&](const char key, const char* text) {
        int cx = x;
        cx += drawKeyBadge(cx, y, key, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(text);
        y += 12;
    };
    auto helpTextBadgeLine = [&](const char* badge, const char* text) {
        int cx = x;
        cx += drawTextBadge(cx, y, badge, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(text);
        y += 12;
    };
    if (g_help_page == 0) {
        helpTextBadgeLine("-=", "tune 0.1 MHz");
        int cx = x;
        cx += drawArrowBadge(cx, y, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print("tune 0.1 MHz");
        y += 12;
        helpTextBadgeLine("[]", "seek station");
        helpKeyLine('s', "auto scan + save");
        helpKeyLine('m', "mute");
        helpTextBadgeLine("1-0", "recall preset");
        helpKeyLine('l', "station list");
    } else {
        helpKeyLine('r', "rename preset");
        helpTextBadgeLine("=", "save freq to slot");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("Dial");
        y += 12;
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("green");
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.print(" = saved preset");
        y += 12;
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("cyan");
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.print(" = active preset");
        y += 12;
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("Audio out: module jack");
        y += 12;
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("Antenna: module ANT");
    }
    char page_buf[12];
    snprintf(page_buf, sizeof(page_buf), "%d/%d", g_help_page + 1, RADIO_HELP_PAGES);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - 30, M5Cardputer.Display.height() - 12);
    M5Cardputer.Display.print(page_buf);
}

static void applyFrequency(const uint16_t freq_centi) {
    g_freq = freq_centi;
    if (g_ready) {
        g_radio.setFrequency(g_freq);
        g_rssi = g_radio.getRssi();
        g_stereo = g_radio.isStereo();
    }
}

static void tuneBySteps(const int steps) {
    if (steps == 0) {
        return;
    }
    int next = static_cast<int>(g_freq) + steps * static_cast<int>(Tea5767::FREQ_STEP);
    if (next < Tea5767::FREQ_MIN) {
        next = Tea5767::FREQ_MIN;
    }
    if (next > Tea5767::FREQ_MAX) {
        next = Tea5767::FREQ_MAX;
    }
    applyFrequency(static_cast<uint16_t>(next));
    drawRadioChrome();
}

static void runSeek(const bool up) {
    if (!g_ready || g_seeking) {
        return;
    }
    g_seeking = true;
    g_seek_anim_ms = millis();
    drawRadioChrome();

    const bool found = g_radio.seek(up);
    g_freq = g_radio.getFrequency();
    if (g_freq < Tea5767::FREQ_MIN) {
        g_freq = 9850;
    }
    g_rssi = g_radio.getRssi();
    g_stereo = g_radio.isStereo();
    g_seeking = false;
    drawRadioChrome();
    (void)found;
}

static bool isTuneDirectionStillHeld(const int dir) {
    if (dir < 0) {
        return M5Cardputer.Keyboard.isKeyPressed('-') || M5Cardputer.Keyboard.isKeyPressed('e') ||
               M5Cardputer.Keyboard.isKeyPressed('a') || M5Cardputer.Keyboard.isKeyPressed(';') ||
               M5Cardputer.Keyboard.isKeyPressed(',');
    }
    if (dir > 0) {
        return M5Cardputer.Keyboard.isKeyPressed('=') || M5Cardputer.Keyboard.isKeyPressed('+') ||
               M5Cardputer.Keyboard.isKeyPressed('d') || M5Cardputer.Keyboard.isKeyPressed('.') ||
               M5Cardputer.Keyboard.isKeyPressed('/');
    }
    return false;
}

// 自动扫频：从 87.5 往上搜，把结果写入 1-0 槽位
static void runAutoScanAndSavePresets() {
    if (!g_ready || g_seeking) {
        return;
    }
    g_seeking = true;
    g_seek_anim_ms = millis();
    drawRadioChrome();

    for (int i = 0; i < RADIO_PRESET_COUNT; ++i) {
        g_presets[i].freq = 0;
        g_presets[i].name[0] = '\0';
    }

    g_radio.setFrequency(Tea5767::FREQ_MIN);
    uint16_t last_found = 0;
    int found_count = 0;
    for (int attempt = 0; attempt < 220 && found_count < RADIO_PRESET_COUNT; ++attempt) {
        if (!g_radio.seek(true)) {
            break;
        }
        const uint16_t found_freq = g_radio.getFrequency();
        if (found_freq < Tea5767::FREQ_MIN || found_freq > Tea5767::FREQ_MAX) {
            continue;
        }
        // 频点太近通常是同一电台，跳过；回绕到更小频率则结束。
        if (last_found != 0) {
            if (found_freq <= last_found) {
                break;
            }
            const int diff = static_cast<int>(found_freq) - static_cast<int>(last_found);
            if (diff < static_cast<int>(Tea5767::FREQ_STEP) * 2) {
                continue;
            }
        }
        g_presets[found_count].freq = found_freq;
        g_presets[found_count].name[0] = '\0';
        saveRadioPreset(found_count);
        last_found = found_freq;
        found_count++;
    }
    for (int i = found_count; i < RADIO_PRESET_COUNT; ++i) {
        saveRadioPreset(i);
    }

    if (found_count > 0) {
        g_active_slot = 0;
        g_sel_slot = 0;
        applyFrequency(g_presets[0].freq);
    } else {
        g_active_slot = -1;
        g_sel_slot = 0;
        g_rssi = g_radio.getRssi();
        g_stereo = g_radio.isStereo();
    }
    g_seeking = false;
    drawRadioChrome();
}

static void toggleMute() {
    if (!g_ready) {
        return;
    }
    g_muted = !g_muted;
    g_radio.setMute(g_muted);
    drawRadioChrome();
}

static void toggleMono() {
    if (!g_ready) {
        return;
    }
    g_mono = !g_mono;
    g_radio.setMono(g_mono);
    g_stereo = g_radio.isStereo();
    drawRadioChrome();
}

static void loadPresetSlot(const int idx) {
    if (idx < 0 || idx >= RADIO_PRESET_COUNT || !isPresetValid(g_presets[idx])) {
        return;
    }
    g_active_slot = idx;
    g_sel_slot = idx;
    applyFrequency(g_presets[idx].freq);
    drawRadioChrome();
}

static int presetIndexFromKey(const char c) {
    if (c >= '1' && c <= '9') {
        return c - '1';
    }
    if (c == '0') {
        return 9;
    }
    return -1;
}

static void openStationsList() {
    g_view = RadioView::Stations;
    g_rename_buf[0] = '\0';
    if (g_active_slot >= 0 && g_active_slot < RADIO_PRESET_COUNT) {
        g_sel_slot = g_active_slot;
    }
    drawRadioChrome();
}

static void closeStationsListToMain() {
    g_view = RadioView::Main;
    g_rename_buf[0] = '\0';
    M5Cardputer.Display.fillScreen(BLACK);
    drawRadioChrome();
}

static void beginRenamePreset() {
    g_view = RadioView::Rename;
    if (g_presets[g_sel_slot].name[0] != '\0') {
        strncpy(g_rename_buf, g_presets[g_sel_slot].name, sizeof(g_rename_buf) - 1);
    } else {
        g_rename_buf[0] = '\0';
    }
    g_rename_buf[sizeof(g_rename_buf) - 1] = '\0';
    drawRadioChrome();
}

static void cancelRenamePreset() {
    g_view = RadioView::Stations;
    g_rename_buf[0] = '\0';
    drawRadioChrome();
}

static void commitRenamePreset() {
    size_t start = 0;
    while (g_rename_buf[start] == ' ') {
        start++;
    }
    size_t end = strlen(g_rename_buf);
    while (end > start && g_rename_buf[end - 1] == ' ') {
        end--;
    }
    const size_t n = end - start;
    if (n == 0) {
        g_presets[g_sel_slot].name[0] = '\0';
    } else {
        const size_t copy_n = (n > static_cast<size_t>(RADIO_NAME_MAX)) ? RADIO_NAME_MAX : n;
        memcpy(g_presets[g_sel_slot].name, g_rename_buf + start, copy_n);
        g_presets[g_sel_slot].name[copy_n] = '\0';
    }
    saveRadioPreset(g_sel_slot);
    g_view = RadioView::Stations;
    g_rename_buf[0] = '\0';
    drawRadioChrome();
}

static void saveCurrentFreqToSelectedSlot() {
    g_presets[g_sel_slot].freq = g_freq;
    saveRadioPreset(g_sel_slot);
    g_active_slot = g_sel_slot;
    drawRadioChrome();
}

static void loadSelectedPresetAndExit() {
    if (isPresetValid(g_presets[g_sel_slot])) {
        g_active_slot = g_sel_slot;
        applyFrequency(g_presets[g_sel_slot].freq);
    }
    closeStationsListToMain();
}

static bool handleRenameInput(const Keyboard_Class::KeysState& status) {
    if (status.del) {
        const size_t n = strlen(g_rename_buf);
        if (n > 0) {
            g_rename_buf[n - 1] = '\0';
            drawRadioChrome();
        }
        return true;
    }
    if (status.enter) {
        commitRenamePreset();
        return true;
    }
    if (status.space) {
        const size_t n = strlen(g_rename_buf);
        if (n < RADIO_NAME_MAX) {
            g_rename_buf[n] = ' ';
            g_rename_buf[n + 1] = '\0';
            drawRadioChrome();
        }
        return true;
    }
    for (const char c : status.word) {
        if (c == '\b') {
            const size_t n = strlen(g_rename_buf);
            if (n > 0) {
                g_rename_buf[n - 1] = '\0';
                drawRadioChrome();
            }
            return true;
        }
        if (c == 0x1B || c == '`') {
            cancelRenamePreset();
            return true;
        }
        if (c < 32 || c > 126) {
            continue;
        }
        const size_t n = strlen(g_rename_buf);
        if (n < RADIO_NAME_MAX) {
            g_rename_buf[n] = c;
            g_rename_buf[n + 1] = '\0';
            drawRadioChrome();
        }
        return true;
    }
    return true;
}

static bool handleStationsInput(const Keyboard_Class::KeysState& status, const String& key) {
    if (g_view == RadioView::Rename) {
        return handleRenameInput(status);
    }

    if (key == "l" || key == "L" || key == "h" || key == "H") {
        closeStationsListToMain();
        return true;
    }

    const int nav = getMenuNavDelta(status);
    if (nav != 0) {
        g_sel_slot = (g_sel_slot + nav + RADIO_PRESET_COUNT) % RADIO_PRESET_COUNT;
        drawRadioChrome();
        return true;
    }

    if (status.enter || status.space) {
        loadSelectedPresetAndExit();
        return true;
    }

    if (key == "r" || key == "R") {
        beginRenamePreset();
        return true;
    }

    if (key == "=" || key == "+") {
        saveCurrentFreqToSelectedSlot();
        return true;
    }

    for (const char c : status.word) {
        const int idx = presetIndexFromKey(c);
        if (idx >= 0) {
            g_sel_slot = idx;
            drawRadioChrome();
            return true;
        }
    }
    return true;
}

// -= / 方向键微调频率；[] 搜台
static int getTuneDelta(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '-') {
            return -1;
        }
        if (c == '=' || c == '+') {
            return 1;
        }
        // Cardputer 方向区：e/a 减频，d 增频（s 保留给自动搜台）
        if (c == 'e' || c == 'a') {
            return -1;
        }
        if (c == 'd') {
            return 1;
        }
    }
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x50 || hid == 0x33 || hid == 0x36) {
            return -1; // Up / Left / ; ,
        }
        if (hid == 0x51 || hid == 0x4F || hid == 0x37 || hid == 0x38) {
            return 1; // Down / Right / . /
        }
    }
    return 0;
}

static int getSeekDelta(const Keyboard_Class::KeysState& status) {
    return getBracketNavDelta(status);
}

static void restoreMainAfterHelp() {
    g_help = false;
    g_help_page = 0;
    M5Cardputer.Display.fillScreen(BLACK);
    drawRadioChrome();
}

} // namespace

void enterRadioApp() {
    leaveRadioApp();
    g_help = false;
    g_view = RadioView::Main;
    g_muted = false;
    g_mono = false;
    g_seeking = false;
    g_freq = 9850;
    g_rssi = 0;
    g_stereo = false;
    g_sel_slot = 0;
    g_active_slot = -1;
    g_rename_buf[0] = '\0';
    g_tune_repeat_dir = 0;
    g_tune_repeat_since_ms = 0;
    g_tune_repeat_last_ms = 0;

    loadRadioPresets();
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.fillScreen(BLACK);

    g_ready = g_radio.begin(M5Cardputer.Ex_I2C);
    if (g_ready) {
        g_radio.setFrequency(g_freq);
        g_radio.setMute(false);
        g_radio.setMono(false);
        g_rssi = g_radio.getRssi();
        g_stereo = g_radio.isStereo();
    }

    if (!ensureRadioCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_ERROR, BLACK);
        M5Cardputer.Display.setCursor(RADIO_UI_LEFT, RADIO_UI_TOP);
        M5Cardputer.Display.print("Canvas OOM");
        return;
    }
    drawRadioChrome();
}

void leaveRadioApp() {
    g_help = false;
    g_view = RadioView::Main;
    g_rename_buf[0] = '\0';
    if (g_ready) {
        g_radio.setMute(true);
    }
    g_ready = false;
    if (g_canvas_ok) {
        radioCanvas.deleteSprite();
        g_canvas_ok = false;
    }
}

bool isRadioHelpVisible() {
    return g_help;
}

bool closeRadioHelp() {
    if (!g_help) {
        return false;
    }
    restoreMainAfterHelp();
    return true;
}

bool closeRadioStations() {
    if (g_view == RadioView::Rename) {
        cancelRenamePreset();
        return true;
    }
    if (g_view == RadioView::Stations) {
        closeStationsListToMain();
        return true;
    }
    return false;
}

void updateRadioApp() {
    if (g_help || g_view != RadioView::Main) {
        return;
    }

    const uint32_t now = millis();
    if (g_seeking) {
        if (now - g_seek_anim_ms >= 30) {
            g_seek_anim_ms = now;
            g_seek_frame = (g_seek_frame + 1) % 5;
            drawRadioChrome();
        }
        return;
    }

    if (g_tune_repeat_dir != 0) {
        if (!isTuneDirectionStillHeld(g_tune_repeat_dir)) {
            g_tune_repeat_dir = 0;
        } else if (now - g_tune_repeat_since_ms >= RADIO_TUNE_REPEAT_DELAY_MS &&
                   now - g_tune_repeat_last_ms >= RADIO_TUNE_REPEAT_RATE_MS) {
            g_tune_repeat_last_ms = now;
            tuneBySteps(g_tune_repeat_dir);
        }
    }

    if (!g_ready) {
        return;
    }

    static uint32_t last_poll_ms = 0;
    if (now - last_poll_ms >= 200) {
        last_poll_ms = now;
        const uint8_t rssi = g_radio.getRssi();
        const bool stereo = g_radio.isStereo();
        if (rssi != g_rssi || stereo != g_stereo) {
            g_rssi = rssi;
            g_stereo = stereo;
            drawRadioChrome();
        }
    }
}

void handleRadioApp(const Keyboard_Class::KeysState& status) {
    const String key = getPressedKey();

    if (key == "h" || key == "H") {
        if (g_help) {
            restoreMainAfterHelp();
        } else if (g_view == RadioView::Main) {
            g_help = true;
            g_help_page = 0;
            drawHelpPage();
        }
        return;
    }
    if (g_help) {
        const int delta = getMenuNavDelta(status);
        if (delta != 0) {
            g_help_page += delta;
            if (g_help_page < 0) {
                g_help_page = RADIO_HELP_PAGES - 1;
            } else if (g_help_page >= RADIO_HELP_PAGES) {
                g_help_page = 0;
            }
            drawHelpPage();
        }
        return;
    }

    if (g_view == RadioView::Stations || g_view == RadioView::Rename) {
        (void)handleStationsInput(status, key);
        return;
    }

    if (key == "l" || key == "L") {
        openStationsList();
        return;
    }
    if (key == "s" || key == "S") {
        runAutoScanAndSavePresets();
        return;
    }

    if (key == "m" || key == "M") {
        toggleMute();
        return;
    }
    if (key == "o" || key == "O") {
        toggleMono();
        return;
    }

    for (const char c : status.word) {
        const int idx = presetIndexFromKey(c);
        if (idx >= 0) {
            loadPresetSlot(idx);
            return;
        }
    }

    if (status.fn) {
        return;
    }

    const int tune_delta = getTuneDelta(status);
    if (tune_delta != 0) {
        tuneBySteps(tune_delta);
        g_tune_repeat_dir = static_cast<int8_t>(tune_delta);
        g_tune_repeat_since_ms = millis();
        g_tune_repeat_last_ms = g_tune_repeat_since_ms;
        return;
    }
    g_tune_repeat_dir = 0;

    if (!g_ready) {
        return;
    }

    const int seek_delta = getSeekDelta(status);
    if (seek_delta < 0) {
        runSeek(false);
    } else if (seek_delta > 0) {
        runSeek(true);
    }
}
