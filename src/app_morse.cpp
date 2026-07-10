#include "app_morse.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include <cstring>

static constexpr int MORSE_DOT_R = 3;
static constexpr int MORSE_DASH_W = 14;
static constexpr int MORSE_DASH_H = 4;
static constexpr int MORSE_SYM_GAP = 6;
static constexpr int MORSE_ROW_H = 12;
static constexpr uint32_t MORSE_UNIT_MS = 80;

static bool g_screen_ready = false;
static int g_tone_hz = 700;
static char g_last_key = '\0';
static const char* g_last_pattern = nullptr;

// 播放状态机
static bool g_playing = false;
static const char* g_play_pattern = nullptr;
static int g_play_idx = 0;
static bool g_play_tone_on = false;
static uint32_t g_play_until_ms = 0;

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
static void drawMorseDot(const int cx, const int cy) {
    M5Cardputer.Display.fillCircle(cx, cy, MORSE_DOT_R, WHITE);
}

// 绘制横线（摩斯划）
static void drawMorseDash(const int x, const int cy) {
    const int y = cy - MORSE_DASH_H / 2;
    M5Cardputer.Display.fillRect(x, y, MORSE_DASH_W, MORSE_DASH_H, WHITE);
}

// 绘制摩斯图案行（图形，非字符）
static void drawMorsePatternRow(const int x, const int y, const int max_w, const char* pattern) {
    if (pattern == nullptr) {
        return;
    }
    int cx = x;
    const int cy = y + MORSE_ROW_H / 2;
    for (const char* p = pattern; *p != '\0'; ++p) {
        if (*p == '.') {
            if (cx + MORSE_DOT_R * 2 > x + max_w) {
                break;
            }
            drawMorseDot(cx + MORSE_DOT_R, cy);
            cx += MORSE_DOT_R * 2 + MORSE_SYM_GAP;
        } else if (*p == '-') {
            if (cx + MORSE_DASH_W > x + max_w) {
                break;
            }
            drawMorseDash(cx, cy);
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
    g_playing = true;
    g_play_pattern = pattern;
    g_play_idx = 0;
    g_play_tone_on = false;
    g_play_until_ms = millis();
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

    int y = APP_CONTENT_Y;

    char freq_buf[16];
    snprintf(freq_buf, sizeof(freq_buf), "%d Hz", g_tone_hz);
    drawInfoLineAt(APP_CONTENT_X, y, "freq", freq_buf, 1);
    y += INFO_LINE_H + 2;

    if (g_last_key != '\0') {
        char key_buf[4];
        snprintf(key_buf, sizeof(key_buf), "%c", g_last_key);
        drawInfoLineAt(APP_CONTENT_X, y, "key", key_buf, 2);
        y += INFO_LINE_H_2X + 4;
    } else {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println("press a-z / 0-9");
        y += INFO_LINE_H + 4;
    }

    if (g_last_pattern != nullptr) {
        drawMorsePatternRow(APP_CONTENT_X, y, content_w, g_last_pattern);
        y += MORSE_ROW_H + 4;
    }

    if (g_playing) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("sending...");
    }

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
    g_playing = false;
    g_play_pattern = nullptr;
    M5Cardputer.Speaker.stop();
    drawMorseApp(true);
}

void updateMorseApp() {
    if (!g_playing || g_play_pattern == nullptr) {
        return;
    }

    const uint32_t now = millis();
    if (now < g_play_until_ms) {
        return;
    }

    if (g_play_tone_on) {
        M5Cardputer.Speaker.stop();
        g_play_tone_on = false;
        g_play_until_ms = now + MORSE_UNIT_MS;
        return;
    }

    if (g_play_pattern[g_play_idx] == '\0') {
        g_playing = false;
        g_play_pattern = nullptr;
        drawMorseApp(false);
        return;
    }

    const char sym = g_play_pattern[g_play_idx];
    const uint32_t tone_ms = (sym == '-') ? MORSE_UNIT_MS * 3 : MORSE_UNIT_MS;
    M5Cardputer.Speaker.tone(g_tone_hz, tone_ms + 50);
    g_play_tone_on = true;
    g_play_until_ms = now + tone_ms;
    g_play_idx++;

    if (g_play_pattern[g_play_idx] != '\0') {
        g_play_until_ms += MORSE_UNIT_MS;
    }
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
