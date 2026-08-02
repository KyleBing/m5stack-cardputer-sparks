#include "app_particle_clock.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_system.h>

namespace {

static constexpr int DIGIT_W = 5;
static constexpr int DIGIT_H = 7;
static constexpr int CELL = 6;          // 大号点阵（240 宽可放下 HH:MM:SS）
static constexpr int DOT = 4;           // 粒子方块边长
static constexpr int MAX_TARGETS = 200;
static constexpr int MAX_PARTICLES = 200;
static constexpr float EASE_SPEED = 7.5f;

// 5x7 点阵数字
static const uint8_t DIGIT_GLYPHS[10][DIGIT_H] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 2
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
};

struct Particle {
    float x, y;
    float tx, ty;
    uint8_t color;
    bool active;
};

struct Target {
    int16_t x;
    int16_t y;
    uint8_t color;
};

static M5Canvas clockCanvas(&M5Cardputer.Display);
static bool g_ok = false;
static int g_w = 0;
static int g_h = 0;
static Particle g_parts[MAX_PARTICLES];
static int g_part_count = 0;
static Target g_targets[MAX_TARGETS];
static int g_target_count = 0;
static char g_time_str[9] = "00:00:00";
static bool g_show_seconds = true;
static uint32_t g_last_ms = 0;

static void applyPalette() {
    clockCanvas.setPaletteColor(0, 0x05, 0x06, 0x12);
    clockCanvas.setPaletteColor(1, 0x2A, 0x40, 0x80);
    clockCanvas.setPaletteColor(2, 0x4A, 0x78, 0xE0);
    clockCanvas.setPaletteColor(3, 0x70, 0xB0, 0xFF);
    clockCanvas.setPaletteColor(4, 0xA8, 0xDC, 0xFF);
    clockCanvas.setPaletteColor(5, 0xFF, 0xC8, 0x60); // 冒号
}

static void collectGlyphTargets(const int digit, const int origin_x, const int origin_y) {
    if (digit < 0 || digit > 9) {
        return;
    }
    for (int row = 0; row < DIGIT_H; ++row) {
        const uint8_t bits = DIGIT_GLYPHS[digit][row];
        for (int col = 0; col < DIGIT_W; ++col) {
            if ((bits & (1 << (DIGIT_W - 1 - col))) == 0) {
                continue;
            }
            if (g_target_count >= MAX_TARGETS) {
                return;
            }
            g_targets[g_target_count].x =
                static_cast<int16_t>(origin_x + col * CELL + CELL / 2);
            g_targets[g_target_count].y =
                static_cast<int16_t>(origin_y + row * CELL + CELL / 2);
            g_targets[g_target_count].color = static_cast<uint8_t>(3 + (esp_random() % 2));
            ++g_target_count;
        }
    }
}

static void collectColonTargets(const int origin_x, const int origin_y) {
    const int dots[2] = {2, 5};
    for (int i = 0; i < 2; ++i) {
        if (g_target_count >= MAX_TARGETS) {
            return;
        }
        g_targets[g_target_count].x = static_cast<int16_t>(origin_x + CELL / 2);
        g_targets[g_target_count].y =
            static_cast<int16_t>(origin_y + dots[i] * CELL + CELL / 2);
        g_targets[g_target_count].color = 5;
        ++g_target_count;
    }
}

static void rebuildTargets(const char* text) {
    g_target_count = 0;
    const int len = static_cast<int>(strlen(text));
    const int glyph_px_w = DIGIT_W * CELL;
    const int glyph_px_h = DIGIT_H * CELL;
    const int colon_w = CELL;
    const int gap = 4;
    int total_w = 0;
    for (int i = 0; i < len; ++i) {
        total_w += (text[i] == ':') ? colon_w : glyph_px_w;
        if (i + 1 < len) {
            total_w += gap;
        }
    }
    int x = (g_w - total_w) / 2;
    const int y = (g_h - glyph_px_h) / 2;

    for (int i = 0; i < len; ++i) {
        const char ch = text[i];
        if (ch == ':') {
            collectColonTargets(x, y);
            x += colon_w + gap;
        } else if (ch >= '0' && ch <= '9') {
            collectGlyphTargets(ch - '0', x, y);
            x += glyph_px_w + gap;
        }
    }
}

// 新粒子出生在时钟区域附近，不从屏幕边缘飞入
static void spawnNearClock(Particle& p, const float tx, const float ty) {
    p.x = tx + static_cast<float>(static_cast<int>(esp_random() % 25) - 12);
    p.y = ty + static_cast<float>(static_cast<int>(esp_random() % 21) - 10);
    p.tx = tx;
    p.ty = ty;
    p.color = 3;
    p.active = true;
}

// 粒子数严格对齐目标点：只做中间字形变换，无边缘发散
static void assignTargetsToParticles() {
    // 目标变多：在现有粒子附近补新点
    while (g_part_count < g_target_count && g_part_count < MAX_PARTICLES) {
        const Target& t = g_targets[g_part_count];
        spawnNearClock(g_parts[g_part_count], static_cast<float>(t.x), static_cast<float>(t.y));
        ++g_part_count;
    }
    // 目标变少：直接丢掉多余粒子（不飞出屏幕）
    if (g_part_count > g_target_count) {
        g_part_count = g_target_count;
    }

    for (int i = 0; i < g_part_count; ++i) {
        Particle& p = g_parts[i];
        p.tx = static_cast<float>(g_targets[i].x);
        p.ty = static_cast<float>(g_targets[i].y);
        p.color = g_targets[i].color;
        p.active = true;
    }
}

// 打乱粒子与目标的对应，触发一次中间区域的形变动画
static void reshuffleMorph() {
    if (g_part_count < 2) {
        return;
    }
    for (int i = g_part_count - 1; i > 0; --i) {
        const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
        const float tx = g_parts[i].tx;
        const float ty = g_parts[i].ty;
        g_parts[i].tx = g_parts[j].tx;
        g_parts[i].ty = g_parts[j].ty;
        g_parts[j].tx = tx;
        g_parts[j].ty = ty;
    }
}

static void readClockString(char* out, const size_t out_len) {
    time_t now = time(nullptr);
    struct tm local{};
    if (now < 1600000000 || localtime_r(&now, &local) == nullptr) {
        const uint32_t sec = millis() / 1000;
        const int hh = static_cast<int>((sec / 3600) % 24);
        const int mm = static_cast<int>((sec / 60) % 60);
        const int ss = static_cast<int>(sec % 60);
        if (g_show_seconds) {
            snprintf(out, out_len, "%02d:%02d:%02d", hh, mm, ss);
        } else {
            snprintf(out, out_len, "%02d:%02d", hh, mm);
        }
        return;
    }
    if (g_show_seconds) {
        snprintf(out, out_len, "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        snprintf(out, out_len, "%02d:%02d", local.tm_hour, local.tm_min);
    }
}

static void syncToTime(const bool force) {
    char buf[9];
    readClockString(buf, sizeof(buf));
    if (!force && strcmp(buf, g_time_str) == 0) {
        return;
    }
    strncpy(g_time_str, buf, sizeof(g_time_str) - 1);
    g_time_str[sizeof(g_time_str) - 1] = '\0';
    rebuildTargets(g_time_str);
    assignTargetsToParticles();
}

static void stepParticles(const float dt) {
    const float k = 1.0f - expf(-EASE_SPEED * dt);
    for (int i = 0; i < g_part_count; ++i) {
        Particle& p = g_parts[i];
        if (!p.active) {
            continue;
        }
        p.x += (p.tx - p.x) * k;
        p.y += (p.ty - p.y) * k;
    }
}

static void render() {
    clockCanvas.fillScreen(0);
    const int half = DOT / 2;
    for (int i = 0; i < g_part_count; ++i) {
        const Particle& p = g_parts[i];
        if (!p.active) {
            continue;
        }
        const int x = static_cast<int>(p.x) - half;
        const int y = static_cast<int>(p.y) - half;
        if (x < -DOT || y < -DOT || x >= g_w || y >= g_h) {
            continue;
        }
        clockCanvas.fillRect(x, y, DOT, DOT, p.color);
    }
    clockCanvas.pushSprite(0, 0);
}

} // namespace

void enterParticleClockApp() {
    leaveParticleClockApp();
    g_w = M5Cardputer.Display.width();
    g_h = M5Cardputer.Display.height();
    g_part_count = 0;
    g_target_count = 0;
    g_show_seconds = true;
    g_time_str[0] = '\0';
    g_last_ms = millis();

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    clockCanvas.setColorDepth(8);
    if (!clockCanvas.createSprite(g_w, g_h)) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("PClock OOM");
        return;
    }
    if (!clockCanvas.createPalette()) {
        clockCanvas.deleteSprite();
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("PClock palette OOM");
        return;
    }
    g_ok = true;
    applyPalette();
    syncToTime(true);
    // 首帧直接落在目标位置，避免进场边缘飞入
    for (int i = 0; i < g_part_count; ++i) {
        g_parts[i].x = g_parts[i].tx;
        g_parts[i].y = g_parts[i].ty;
    }
    render();
}

void leaveParticleClockApp() {
    if (g_ok) {
        clockCanvas.deleteSprite();
        g_ok = false;
    }
}

void updateParticleClockApp() {
    if (!g_ok) {
        return;
    }
    const uint32_t now = millis();
    float dt = (now - g_last_ms) * 0.001f;
    g_last_ms = now;
    dt = constrain(dt, 0.001f, 0.05f);

    syncToTime(false);
    stepParticles(dt);
    render();
}

void handleParticleClockApp(const Keyboard_Class::KeysState& status) {
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'm' || c == 's') {
            g_show_seconds = !g_show_seconds;
            syncToTime(true);
        } else if (c == 'r' || c == ' ') {
            // 仅在字形内部打乱重聚，不向外发散
            reshuffleMorph();
        }
    }
}

void pollParticleClockBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        reshuffleMorph();
    }
}
