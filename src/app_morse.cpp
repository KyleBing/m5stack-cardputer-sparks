#include "app_morse.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include <cctype>
#include <cstring>

// 摩斯图案尺寸（略放大便于辨认）
static constexpr int MORSE_DOT_R = 5;
static constexpr int MORSE_DASH_W = 22;
static constexpr int MORSE_DASH_H = 7;
static constexpr int MORSE_SYM_GAP = 8;
static constexpr int MORSE_ROW_H = 18;
static constexpr int MORSE_KEY_TEXT_SIZE = 3; // 当前字母 3x
static constexpr uint32_t MORSE_UNIT_MS = 80;
static constexpr int MORSE_SIGNAL_Y = 88;
static constexpr int MORSE_SIGNAL_H = 23;

static bool g_screen_ready = false;
static int g_tone_hz = 1000;
static char g_last_key = '\0';
static const char* g_last_pattern = nullptr;

enum class MorsePhase {
    IDLE,
    TONE,
    GAP,
};

// 非阻塞播放状态机与当前可视化符号。
static MorsePhase g_play_phase = MorsePhase::IDLE;
static const char* g_play_pattern = nullptr;
static int g_play_idx = 0;
static int g_active_idx = -1;
static uint32_t g_play_until_ms = 0;
static uint32_t g_tone_started_ms = 0;
static uint32_t g_tone_duration_ms = 0;
static uint32_t g_last_visual_ms = 0;

// 标准摩斯码表（a-z, 0-9）
static const char* morseForChar(const char c) {
    static const char* table[36] = {
        ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", "..",   ".---",
        "-.-",  ".-..", "--",   "-.",   "---",  ".--.", "--.-", ".-.",  "...",  "-",
        "..-",  "...-", ".--",  "-..-", "-.--", "--..",
        "-----", ".----", "..---", "...--", "....-", ".....",
        "-....", "--...", "---..", "----.",
    };
    if (c >= 'a' && c <= 'z') {
        return table[c - 'a'];
    }
    if (c >= 'A' && c <= 'Z') {
        return table[c - 'A'];
    }
    if (c >= '0' && c <= '9') {
        return table[26 + (c - '0')];
    }
    return nullptr;
}

// 绘制圆点（摩斯点）
static void drawMorseDot(const int cx, const int cy, const uint16_t color) {
    M5Cardputer.Display.fillCircle(cx, cy, MORSE_DOT_R, color);
}

// 绘制横线（摩斯划）
static void drawMorseDash(const int x, const int cy, const uint16_t color) {
    const int y = cy - MORSE_DASH_H / 2;
    M5Cardputer.Display.fillRoundRect(x, y, MORSE_DASH_W, MORSE_DASH_H, 2, color);
}

// 当前正在发声的点/划使用绿色，其余符号保持白色。
static void drawMorsePatternRow(const int x, const int y, const int max_w, const char* pattern,
                                const int active_idx) {
    if (pattern == nullptr) {
        return;
    }
    int cx = x;
    const int cy = y + MORSE_ROW_H / 2;
    int index = 0;
    for (const char* p = pattern; *p != '\0'; ++p, ++index) {
        const uint16_t color = (index == active_idx) ? APP_COLOR_OK : WHITE;
        if (*p == '.') {
            if (cx + MORSE_DOT_R * 2 > x + max_w) {
                break;
            }
            drawMorseDot(cx + MORSE_DOT_R, cy, color);
            cx += MORSE_DOT_R * 2 + MORSE_SYM_GAP;
        } else if (*p == '-') {
            if (cx + MORSE_DASH_W > x + max_w) {
                break;
            }
            drawMorseDash(cx, cy, color);
            cx += MORSE_DASH_W + MORSE_SYM_GAP;
        }
    }
}

static char morsePressedLetter(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    return '\0';
}

static void startMorsePlayback(const char* pattern) {
    if (pattern == nullptr || pattern[0] == '\0') {
        return;
    }
    g_play_pattern = pattern;
    g_play_idx = 0;
    g_active_idx = -1;
    g_play_phase = MorsePhase::GAP;
    g_play_until_ms = millis();
    g_last_visual_ms = 0;
}

// 绘制发声波形和精确符号进度，不清除整页以避免闪烁。
static void drawMorseSignal() {
    const int x = APP_CONTENT_X;
    const int w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    M5Cardputer.Display.fillRect(x, MORSE_SIGNAL_Y, w, MORSE_SIGNAL_H + 8, BLACK);
    M5Cardputer.Display.drawFastHLine(x, MORSE_SIGNAL_Y + MORSE_SIGNAL_H / 2, w, APP_COLOR_MUTED);

    const bool tone_on = g_play_phase == MorsePhase::TONE;
    const uint16_t signal_color = tone_on ? APP_COLOR_OK : APP_COLOR_MUTED;
    int previous_y = MORSE_SIGNAL_Y + MORSE_SIGNAL_H / 2;
    const uint32_t phase = millis() / 4;
    for (int px = 0; px < w; px += 3) {
        const int wave = tone_on ? (((px / 3 + phase) & 1U) ? 7 : -7) : 0;
        const int py = MORSE_SIGNAL_Y + MORSE_SIGNAL_H / 2 + wave;
        if (px > 0) {
            M5Cardputer.Display.drawLine(x + px - 3, previous_y, x + px, py, signal_color);
        }
        previous_y = py;
    }

    M5Cardputer.Display.drawRect(x, MORSE_SIGNAL_Y + MORSE_SIGNAL_H + 3, w, 4, APP_COLOR_MUTED);
    if (tone_on && g_tone_duration_ms > 0) {
        const uint32_t elapsed = millis() - g_tone_started_ms;
        const uint32_t shown = elapsed < g_tone_duration_ms ? elapsed : g_tone_duration_ms;
        const int progress =
            static_cast<int>(static_cast<uint64_t>(w - 2) * shown / g_tone_duration_ms);
        M5Cardputer.Display.fillRect(x + 1, MORSE_SIGNAL_Y + MORSE_SIGNAL_H + 4, progress, 2,
                                    APP_COLOR_OK);
    }
}

static void drawMorseApp(const bool full_init) {
    const int screen_w = M5Cardputer.Display.width();
    const int content_w = screen_w - APP_CONTENT_X * 2;

    if (full_init || !g_screen_ready) {
        beginAppScreen("Morse");
        g_screen_ready = true;
    } else {
        clearAppContentArea();
    }

    // 顶部保留频率和发送状态，主体同时展示字母、点划与波形。
    int y = APP_CONTENT_Y;
    char freq_buf[16];
    snprintf(freq_buf, sizeof(freq_buf), "%d Hz", g_tone_hz);
    drawInfoLineAt(APP_CONTENT_X, y, "freq", freq_buf, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(
        g_play_phase == MorsePhase::IDLE ? APP_COLOR_MUTED : APP_COLOR_OK, BLACK);
    M5Cardputer.Display.setCursor(screen_w - 50, y);
    M5Cardputer.Display.print(g_play_phase == MorsePhase::IDLE ? "READY" : "SENDING");
    y += INFO_LINE_H + 2;

    if (g_last_key != '\0') {
        char key_ch = static_cast<char>(toupper(static_cast<unsigned char>(g_last_key)));
        M5Cardputer.Display.setTextSize(MORSE_KEY_TEXT_SIZE);
        M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print(key_ch);
        drawMorsePatternRow(APP_CONTENT_X + 36, y + 2, content_w - 36, g_last_pattern,
                            g_play_phase == MorsePhase::TONE ? g_active_idx : -1);
    } else {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println("press a-z / 0-9");
    }

    drawMorseSignal();
    const int hint_y = M5Cardputer.Display.height() - 12;
    const KeyHintItem items[] = {
        {'-', "freq-"},
        {'=', "freq+"},
    };
    drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 2, 1, APP_COLOR_HINT);
}

void enterMorseApp() {
    g_screen_ready = false;
    g_last_key = '\0';
    g_last_pattern = nullptr;
    g_play_phase = MorsePhase::IDLE;
    g_play_pattern = nullptr;
    g_active_idx = -1;
    M5Cardputer.Speaker.stop();
    drawMorseApp(true);
}

void leaveMorseApp() {
    M5Cardputer.Speaker.stop();
    g_play_phase = MorsePhase::IDLE;
    g_play_pattern = nullptr;
    g_active_idx = -1;
    releaseSpeakerQuiet();
}

void updateMorseApp() {
    if (g_play_phase == MorsePhase::IDLE || g_play_pattern == nullptr) {
        return;
    }

    const uint32_t now = millis();
    if (now - g_last_visual_ms >= 32) {
        g_last_visual_ms = now;
        drawMorseSignal();
    }
    if (now < g_play_until_ms) {
        return;
    }

    if (g_play_phase == MorsePhase::TONE) {
        M5Cardputer.Speaker.stop();
        g_play_phase = MorsePhase::GAP;
        g_active_idx = -1;
        g_play_until_ms = now + MORSE_UNIT_MS;
        drawMorseApp(false);
        return;
    }

    if (g_play_pattern[g_play_idx] == '\0') {
        g_play_phase = MorsePhase::IDLE;
        g_play_pattern = nullptr;
        g_active_idx = -1;
        releaseSpeakerQuiet(); // 播完关功放，避免空转嗡嗡
        drawMorseApp(false);
        return;
    }

    const char sym = g_play_pattern[g_play_idx];
    const uint32_t tone_ms = (sym == '-') ? MORSE_UNIT_MS * 3 : MORSE_UNIT_MS;
    warmUpSpeakerIfNeeded();
    M5Cardputer.Speaker.tone(g_tone_hz, tone_ms + 20);
    g_active_idx = g_play_idx;
    g_play_idx++;
    g_play_phase = MorsePhase::TONE;
    g_tone_started_ms = now;
    g_tone_duration_ms = tone_ms;
    g_play_until_ms = now + tone_ms;
    drawMorseApp(false);
}

void handleMorseApp(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '=' || c == '+') {
            g_tone_hz = constrain(g_tone_hz + 50, 300, 2000);
            drawMorseApp(false);
            return;
        }
        if (c == '-') {
            g_tone_hz = constrain(g_tone_hz - 50, 300, 2000);
            drawMorseApp(false);
            return;
        }
    }

    const char key = morsePressedLetter(status);
    if (key == '\0') {
        return;
    }

    const char* pattern = morseForChar(key);
    if (pattern == nullptr) {
        return;
    }

    g_last_key = key;
    g_last_pattern = pattern;
    M5Cardputer.Speaker.stop();
    startMorsePlayback(pattern);
    drawMorseApp(false);
}
