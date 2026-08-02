#include "app_bezier_wave.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

static constexpr float PI_F = 3.14159265f;
static constexpr int WAVE_COUNT = 5;
static constexpr int WAVE_SEGMENTS = 24; // 每条波采样段数
static constexpr int WAVE_SPEED_COUNT = 5;
static constexpr float WAVE_SPEED_MUL[WAVE_SPEED_COUNT] = {0.4f, 0.7f, 1.0f, 1.5f, 2.2f};

struct WaveLayer {
    float amp;     // 振幅
    float freq;    // 水平频率
    float phase;   // 相位
    float speed;   // 相位角速度
    float y_base;  // 基线相对中心偏移
    uint8_t color; // 调色板索引
};

static M5Canvas waveCanvas(&M5Cardputer.Display);
static bool g_ok = false;
static int g_w = 0;
static int g_h = 0;
static int g_speed = 2;
static int g_theme = 0;
static float g_t = 0.0f;
static float g_pulse = 0.0f;
static uint32_t g_last_ms = 0;
static WaveLayer g_waves[WAVE_COUNT];

static constexpr int THEME_COUNT = 3;

static void applyPalette() {
    waveCanvas.setPaletteColor(0, 0x04, 0x06, 0x10); // 背景
    if (g_theme == 0) {
        // 青绿疗愈
        waveCanvas.setPaletteColor(1, 0x0A, 0x2A, 0x32);
        waveCanvas.setPaletteColor(2, 0x12, 0x5A, 0x68);
        waveCanvas.setPaletteColor(3, 0x1E, 0x9A, 0xA8);
        waveCanvas.setPaletteColor(4, 0x3C, 0xD4, 0xC8);
        waveCanvas.setPaletteColor(5, 0xA8, 0xFF, 0xE8);
    } else if (g_theme == 1) {
        // 紫蓝科技
        waveCanvas.setPaletteColor(1, 0x18, 0x10, 0x38);
        waveCanvas.setPaletteColor(2, 0x38, 0x28, 0x78);
        waveCanvas.setPaletteColor(3, 0x5A, 0x48, 0xC0);
        waveCanvas.setPaletteColor(4, 0x88, 0x78, 0xFF);
        waveCanvas.setPaletteColor(5, 0xD8, 0xE0, 0xFF);
    } else {
        // 暖橙夕阳
        waveCanvas.setPaletteColor(1, 0x28, 0x10, 0x08);
        waveCanvas.setPaletteColor(2, 0x68, 0x28, 0x10);
        waveCanvas.setPaletteColor(3, 0xB0, 0x4A, 0x18);
        waveCanvas.setPaletteColor(4, 0xF0, 0x8A, 0x30);
        waveCanvas.setPaletteColor(5, 0xFF, 0xD8, 0xA0);
    }
    waveCanvas.setPaletteColor(6, 0xEC, 0xFF, 0xF4); // 文字
}

static void initWaves() {
    // 多层不同频率 / 振幅 / 透明度叠加
    const float mid = g_h * 0.5f;
    g_waves[0] = {18.0f, 1.1f, 0.0f, 1.2f, mid - 8.0f, 2};
    g_waves[1] = {24.0f, 0.85f, 1.0f, 0.9f, mid + 2.0f, 3};
    g_waves[2] = {30.0f, 0.65f, 2.2f, 0.7f, mid + 10.0f, 4};
    g_waves[3] = {14.0f, 1.6f, 0.5f, 1.6f, mid - 16.0f, 1};
    g_waves[4] = {20.0f, 1.25f, 3.0f, 1.1f, mid + 18.0f, 5};
}

// 二次贝塞尔：用 sin 驱动控制点，形成丝滑起伏
static void drawBezierWave(const WaveLayer& w, const float t, const float amp_boost) {
    const float amp = w.amp * (1.0f + amp_boost);
    const float phase = w.phase + t * w.speed;

    // 分段：每段用三点二次贝塞尔连接，覆盖全宽
    float prev_x = 0.0f;
    float prev_y = w.y_base + sinf(phase) * amp * 0.35f;

    for (int i = 1; i <= WAVE_SEGMENTS; ++i) {
        const float u0 = static_cast<float>(i - 1) / WAVE_SEGMENTS;
        const float u1 = static_cast<float>(i) / WAVE_SEGMENTS;
        const float mid_u = (u0 + u1) * 0.5f;

        const float x0 = u0 * (g_w - 1);
        const float x1 = u1 * (g_w - 1);
        const float xm = mid_u * (g_w - 1);

        // 端点与控制点均由 sin 调制，多谐波让曲线更有绸缎感
        const float y0 = w.y_base + sinf(phase + u0 * w.freq * PI_F * 2.0f) * amp +
                         sinf(phase * 0.5f + u0 * 4.0f) * amp * 0.18f;
        const float y1 = w.y_base + sinf(phase + u1 * w.freq * PI_F * 2.0f) * amp +
                         sinf(phase * 0.5f + u1 * 4.0f) * amp * 0.18f;
        // 控制点抬得更高，形成拱起的二次曲线
        const float cy = w.y_base + sinf(phase + mid_u * w.freq * PI_F * 2.0f + 0.6f) * amp * 1.35f +
                         cosf(phase * 0.7f + mid_u * 3.0f) * amp * 0.25f;

        // 用折线细分二次贝塞尔（设备无 quadraticCurveTo）
        constexpr int SUB = 6;
        for (int s = 1; s <= SUB; ++s) {
            const float tt = static_cast<float>(s) / SUB;
            const float omt = 1.0f - tt;
            const float bx = omt * omt * x0 + 2.0f * omt * tt * xm + tt * tt * x1;
            const float by = omt * omt * y0 + 2.0f * omt * tt * cy + tt * tt * y1;
            waveCanvas.drawLine(static_cast<int>(prev_x), static_cast<int>(prev_y),
                                static_cast<int>(bx), static_cast<int>(by), w.color);
            // 加粗：邻近像素再画一条，增强可见度
            waveCanvas.drawLine(static_cast<int>(prev_x), static_cast<int>(prev_y) + 1,
                                static_cast<int>(bx), static_cast<int>(by) + 1, w.color);
            prev_x = bx;
            prev_y = by;
        }
    }
}

static void render() {
    waveCanvas.fillScreen(0);
    const float amp_boost = g_pulse * 0.55f;
    // 从暗到亮画，亮层盖在上面
    for (int i = 0; i < WAVE_COUNT; ++i) {
        drawBezierWave(g_waves[i], g_t, amp_boost);
    }

    waveCanvas.pushSprite(0, 0);
}

} // namespace

void enterBezierWaveApp() {
    leaveBezierWaveApp();
    g_w = M5Cardputer.Display.width();
    g_h = M5Cardputer.Display.height();
    g_speed = 2;
    g_theme = 0;
    g_t = 0.0f;
    g_pulse = 0.0f;
    g_last_ms = millis();

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    waveCanvas.setColorDepth(8);
    if (!waveCanvas.createSprite(g_w, g_h)) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Wave OOM");
        return;
    }
    if (!waveCanvas.createPalette()) {
        waveCanvas.deleteSprite();
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Wave palette OOM");
        return;
    }
    g_ok = true;
    applyPalette();
    initWaves();
    render();
}

void leaveBezierWaveApp() {
    if (g_ok) {
        waveCanvas.deleteSprite();
        g_ok = false;
    }
}

void updateBezierWaveApp() {
    if (!g_ok) {
        return;
    }
    const uint32_t now = millis();
    float dt = (now - g_last_ms) * 0.001f;
    g_last_ms = now;
    dt = constrain(dt, 0.001f, 0.05f);

    g_t += dt * WAVE_SPEED_MUL[g_speed];
    if (g_pulse > 0.0f) {
        g_pulse = fmaxf(0.0f, g_pulse - dt * 1.8f);
    }
    render();
}

void handleBezierWaveApp(const Keyboard_Class::KeysState& status) {
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == '-' && g_speed > 0) {
            --g_speed;
        } else if ((c == '=' || c == '+') && g_speed < WAVE_SPEED_COUNT - 1) {
            ++g_speed;
        } else if (c == 'c' || c == 'm') {
            g_theme = (g_theme + 1) % THEME_COUNT;
            applyPalette();
        } else if (c == 'r') {
            g_t = 0.0f;
            initWaves();
        } else if (c == ' ') {
            g_pulse = 1.0f;
        }
    }
}

void pollBezierWaveBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        g_pulse = 1.0f;
    }
}
