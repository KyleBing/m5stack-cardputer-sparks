#include "app_matrix_rain.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_system.h>

namespace {

static constexpr int RAIN_COLS = 48; // 多列叠加，营造层次
static constexpr int RAIN_FADE = 18;  // 更细的明暗阶梯 = 透明度层次
static constexpr int RAIN_SPEED_COUNT = 5;
static constexpr float RAIN_SPEED_MUL[RAIN_SPEED_COUNT] = {0.45f, 0.7f, 1.0f, 1.45f, 2.0f};
static constexpr int RAIN_LAYERS = 3; // 远 / 中 / 近

// 代码感字符集
static const char RAIN_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%&*+=<>";

struct RainCol {
    float y;
    float speed;
    uint8_t len;
    uint8_t layer; // 0 远景暗慢，1 中景，2 近景亮快
    char ch;
};

static M5Canvas rainCanvas(&M5Cardputer.Display);
static bool g_ok = false;
static int g_w = 0;
static int g_h = 0;
static int g_col_w = 5;
static RainCol g_cols[RAIN_COLS];
static int g_speed = 2;
static uint32_t g_last_ms = 0;
static uint32_t g_pulse_ms = 0;
static uint8_t g_fade_tick = 0;

static void applyPalette() {
    rainCanvas.setPaletteColor(0, 0x00, 0x00, 0x00);
    // 1..RAIN_FADE：近黑绿 → 亮绿，模拟不同透明度
    for (int i = 1; i <= RAIN_FADE; ++i) {
        const float t = static_cast<float>(i) / RAIN_FADE;
        // 非线性：暗部更密，亮部拉开层次
        const float g = t * t;
        rainCanvas.setPaletteColor(
            i, 0, static_cast<uint8_t>(18 + g * 210), static_cast<uint8_t>(g * 28));
    }
    // 近景头部高光（近白）
    rainCanvas.setPaletteColor(RAIN_FADE + 1, 0xB8, 0xFF, 0xC0);
    rainCanvas.setPaletteColor(RAIN_FADE + 2, 0xE8, 0xFF, 0xEC);
}

static void resetCol(RainCol& col, const bool spawn_above) {
    col.layer = static_cast<uint8_t>(esp_random() % RAIN_LAYERS);
    // 远慢暗、近亮快
    const float layer_spd[] = {0.35f, 0.7f, 1.25f};
    const uint8_t layer_len[] = {14, 10, 7};
    col.speed = (22.0f + static_cast<float>(esp_random() % 50)) * layer_spd[col.layer];
    col.len = layer_len[col.layer] + static_cast<uint8_t>(esp_random() % 5);
    col.ch = RAIN_CHARS[esp_random() % (sizeof(RAIN_CHARS) - 1)];
    col.y = spawn_above ? static_cast<float>(-(esp_random() % (g_h + 60)))
                        : static_cast<float>(esp_random() % g_h);
}

static void initCols() {
    for (int i = 0; i < RAIN_COLS; ++i) {
        resetCol(g_cols[i], true);
        g_cols[i].y = static_cast<float>(-(esp_random() % (g_h * 3)));
        // 均匀分布三层，避免全挤在一层
        g_cols[i].layer = static_cast<uint8_t>(i % RAIN_LAYERS);
        const float layer_spd[] = {0.35f, 0.7f, 1.25f};
        const uint8_t layer_len[] = {14, 10, 7};
        g_cols[i].speed =
            (22.0f + static_cast<float>(esp_random() % 50)) * layer_spd[g_cols[i].layer];
        g_cols[i].len = layer_len[g_cols[i].layer] + static_cast<uint8_t>(esp_random() % 5);
    }
}

// 慢速压暗：隔帧 / 半档衰减，拖尾更长、层次更明显
static void fadeFrame() {
    auto* pixels = static_cast<uint8_t*>(rainCanvas.getBuffer());
    if (pixels == nullptr) {
        return;
    }
    ++g_fade_tick;
    // 偶帧才衰减，等效更低的“黑幕透明度”
    if ((g_fade_tick & 1u) != 0u) {
        return;
    }
    const int n = g_w * g_h;
    for (int i = 0; i < n; ++i) {
        uint8_t& p = pixels[i];
        if (p == 0) {
            continue;
        }
        if (p > RAIN_FADE) {
            p = RAIN_FADE;
        } else if (p > 1) {
            --p;
        } else {
            p = 0;
        }
    }
}

// 按层取基础亮度上限：远景整体更暗
static uint8_t layerHeadBright(const uint8_t layer) {
    if (layer == 0) {
        return static_cast<uint8_t>(RAIN_FADE * 4 / 10);
    }
    if (layer == 1) {
        return static_cast<uint8_t>(RAIN_FADE * 7 / 10);
    }
    return static_cast<uint8_t>(RAIN_FADE + 1); // 近景可用高光
}

static void stepAndDraw(const float dt) {
    rainCanvas.setTextSize(1);
    rainCanvas.setFont(&fonts::Font0);
    const float mul = RAIN_SPEED_MUL[g_speed];
    const bool pulsing = (millis() - g_pulse_ms) < 280;

    // 先远后近绘制，近景盖住远景
    for (int layer = 0; layer < RAIN_LAYERS; ++layer) {
        for (int c = 0; c < RAIN_COLS; ++c) {
            RainCol& col = g_cols[c];
            if (col.layer != layer) {
                continue;
            }
            const int x = (c * g_col_w + (layer * 2)) % g_w; // 层间错位，避免重叠死板
            const int hy = static_cast<int>(col.y);
            const uint8_t head_b = layerHeadBright(col.layer);
            const int step_y = (layer == 0) ? 7 : 8; // 远景字距略紧

            // 头部
            if (hy >= 0 && hy < g_h) {
                rainCanvas.setTextColor(head_b);
                rainCanvas.setCursor(x, hy);
                rainCanvas.write(col.ch);
            }

            // 拖尾：距头越远越暗，形成透明度渐变
            for (int t = 1; t < col.len; ++t) {
                const int ty = hy - t * step_y;
                if (ty < -8 || ty >= g_h) {
                    continue;
                }
                // 线性衰减到接近 0
                int bright = static_cast<int>(head_b) - (t * static_cast<int>(head_b) / col.len);
                if (layer == 0) {
                    bright = bright * 2 / 3; // 远景再压暗一档
                }
                if (bright < 1) {
                    continue;
                }
                char ch = col.ch;
                if (pulsing || (esp_random() % 10u) == 0u) {
                    ch = RAIN_CHARS[esp_random() % (sizeof(RAIN_CHARS) - 1)];
                }
                rainCanvas.setTextColor(static_cast<uint8_t>(bright));
                rainCanvas.setCursor(x, ty);
                rainCanvas.write(ch);
            }
        }
    }

    // 统一推进位置
    for (int c = 0; c < RAIN_COLS; ++c) {
        RainCol& col = g_cols[c];
        col.y += col.speed * mul * dt;
        if (col.y - col.len * 8 > g_h) {
            resetCol(col, true);
        }
        if ((esp_random() % 24u) == 0u) {
            col.ch = RAIN_CHARS[esp_random() % (sizeof(RAIN_CHARS) - 1)];
        }
    }
}

static void render(const float dt) {
    fadeFrame();
    stepAndDraw(dt);
    rainCanvas.pushSprite(0, 0);
}

} // namespace

void enterMatrixRainApp() {
    leaveMatrixRainApp();
    g_w = M5Cardputer.Display.width();
    g_h = M5Cardputer.Display.height();
    g_col_w = max(4, g_w / (RAIN_COLS / 2));
    g_speed = 2;
    g_pulse_ms = 0;
    g_fade_tick = 0;
    g_last_ms = millis();

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    rainCanvas.setColorDepth(8);
    if (!rainCanvas.createSprite(g_w, g_h)) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Matrix OOM");
        return;
    }
    if (!rainCanvas.createPalette()) {
        rainCanvas.deleteSprite();
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Matrix palette OOM");
        return;
    }
    g_ok = true;
    applyPalette();
    rainCanvas.fillScreen(0);
    initCols();
    render(0.016f);
}

void leaveMatrixRainApp() {
    if (g_ok) {
        rainCanvas.deleteSprite();
        g_ok = false;
    }
}

void updateMatrixRainApp() {
    if (!g_ok) {
        return;
    }
    const uint32_t now = millis();
    float dt = (now - g_last_ms) * 0.001f;
    g_last_ms = now;
    dt = constrain(dt, 0.001f, 0.05f);
    render(dt);
}

void handleMatrixRainApp(const Keyboard_Class::KeysState& status) {
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == '-' && g_speed > 0) {
            --g_speed;
        } else if ((c == '=' || c == '+') && g_speed < RAIN_SPEED_COUNT - 1) {
            ++g_speed;
        } else if (c == 'r') {
            initCols();
            if (g_ok) {
                rainCanvas.fillScreen(0);
            }
        } else if (c == ' ') {
            g_pulse_ms = millis();
            for (int i = 0; i < RAIN_COLS; ++i) {
                g_cols[i].speed *= 1.8f;
                g_cols[i].ch = RAIN_CHARS[esp_random() % (sizeof(RAIN_CHARS) - 1)];
            }
        }
    }
}

void pollMatrixRainBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        g_pulse_ms = millis();
        for (int i = 0; i < RAIN_COLS; ++i) {
            g_cols[i].speed *= 1.8f;
            g_cols[i].ch = RAIN_CHARS[esp_random() % (sizeof(RAIN_CHARS) - 1)];
        }
    }
}
