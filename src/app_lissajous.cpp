#include "app_lissajous.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

static constexpr float PI_F = 3.14159265f;
static constexpr float TWO_PI_F = PI_F * 2.0f;
static constexpr int CURVE_SAMPLES = 280; // 曲线采样点数
static constexpr int SPEED_COUNT = 5;
static constexpr float SPEED_MUL[SPEED_COUNT] = {0.4f, 0.7f, 1.0f, 1.5f, 2.2f};
static constexpr int THEME_COUNT = 3;
static constexpr float BASE_A = 3.0f;
static constexpr float BASE_B = 2.0f;
static constexpr float PHASE_RATE = 0.55f; // 相位旋转角速度（× speed）
static constexpr float MORPH_RATE = 0.35f; // 频率比形变速度（× speed）

static M5Canvas lisaCanvas(&M5Cardputer.Display);
static bool g_ok = false;
static int g_w = 0;
static int g_h = 0;
static int g_cx = 0;
static int g_cy = 0;
static float g_amp_x = 0.0f;
static float g_amp_y = 0.0f;
static float g_phase = PI_F / 2.0f; // 相位差 δ，匀速推进
static float g_morph = 0.0f;        // 形变时钟，驱动 a:b 缓变
static float g_pulse = 0.0f;
static int g_speed = 2;
static int g_theme = 0;
static uint32_t g_last_ms = 0;

// 简易 HSL→RGB（h:0..360）
static void hslToRgb(const float h, const float s, const float l, uint8_t& r, uint8_t& g,
                     uint8_t& b) {
    const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    const float hh = fmodf(h, 360.0f) / 60.0f;
    const float x = c * (1.0f - fabsf(fmodf(hh, 2.0f) - 1.0f));
    float rf = 0, gf = 0, bf = 0;
    if (hh < 1) {
        rf = c;
        gf = x;
    } else if (hh < 2) {
        rf = x;
        gf = c;
    } else if (hh < 3) {
        gf = c;
        bf = x;
    } else if (hh < 4) {
        gf = x;
        bf = c;
    } else if (hh < 5) {
        rf = x;
        bf = c;
    } else {
        rf = c;
        bf = x;
    }
    const float m = l - c * 0.5f;
    r = static_cast<uint8_t>(constrain((rf + m) * 255.0f, 0.0f, 255.0f));
    g = static_cast<uint8_t>(constrain((gf + m) * 255.0f, 0.0f, 255.0f));
    b = static_cast<uint8_t>(constrain((bf + m) * 255.0f, 0.0f, 255.0f));
}

static void applyPalette() {
    lisaCanvas.setPaletteColor(0, 0x04, 0x05, 0x0C);
    const float base_hue[] = {195.0f, 280.0f, 25.0f}; // 青 / 紫 / 暖橙
    for (int i = 1; i <= 6; ++i) {
        const float t = static_cast<float>(i) / 6.0f;
        uint8_t r, g, b;
        hslToRgb(base_hue[g_theme] + t * 28.0f, 0.85f, 0.28f + t * 0.45f, r, g, b);
        lisaCanvas.setPaletteColor(i, r, g, b);
    }
    uint8_t hr, hg, hb;
    hslToRgb(base_hue[g_theme] + 40.0f, 0.55f, 0.92f, hr, hg, hb);
    lisaCanvas.setPaletteColor(7, hr, hg, hb);
}

static void drawCurve() {
    // a:b 在基频附近缓变，不乘累积时间，避免相位越转越快
    const float a = BASE_A + 0.85f * sinf(g_morph * 0.7f);
    const float b = BASE_B + 0.65f * cosf(g_morph * 0.55f);
    const float delta = g_phase + g_pulse * 1.2f;
    const float amp_boost = 1.0f + g_pulse * 0.25f;
    const float ax = g_amp_x * amp_boost;
    const float ay = g_amp_y * amp_boost;

    // s 只是曲线参数（固定区间），与动画时钟分离
    const float span = TWO_PI_F * 3.0f;
    float prev_x = g_cx + ax * sinf(delta);
    float prev_y = g_cy;

    for (int i = 1; i <= CURVE_SAMPLES; ++i) {
        const float s = span * static_cast<float>(i) / CURVE_SAMPLES;
        const float x = g_cx + ax * sinf(a * s + delta);
        const float y = g_cy + ay * sinf(b * s);

        const uint8_t color = (i == CURVE_SAMPLES) ? 7 : 5;
        lisaCanvas.drawLine(static_cast<int>(prev_x), static_cast<int>(prev_y),
                            static_cast<int>(x), static_cast<int>(y), color);
        lisaCanvas.drawLine(static_cast<int>(prev_x), static_cast<int>(prev_y) + 1,
                            static_cast<int>(x), static_cast<int>(y) + 1, 4);
        prev_x = x;
        prev_y = y;
    }
}

static void render() {
    // 每帧清屏，无残影
    lisaCanvas.fillScreen(0);
    drawCurve();
    lisaCanvas.pushSprite(0, 0);
}

} // namespace

void enterLissajousApp() {
    leaveLissajousApp();
    g_w = M5Cardputer.Display.width();
    g_h = M5Cardputer.Display.height();
    g_cx = g_w / 2;
    g_cy = g_h / 2;
    g_amp_x = g_w * 0.42f;
    g_amp_y = g_h * 0.40f;
    g_phase = PI_F * 0.5f;
    g_morph = 0.0f;
    g_pulse = 0.0f;
    g_speed = 2;
    g_theme = 0;
    g_last_ms = millis();

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    lisaCanvas.setColorDepth(8);
    if (!lisaCanvas.createSprite(g_w, g_h)) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Lissa OOM");
        return;
    }
    if (!lisaCanvas.createPalette()) {
        lisaCanvas.deleteSprite();
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Lissa palette OOM");
        return;
    }
    g_ok = true;
    applyPalette();
    render();
}

void leaveLissajousApp() {
    if (g_ok) {
        lisaCanvas.deleteSprite();
        g_ok = false;
    }
}

void updateLissajousApp() {
    if (!g_ok) {
        return;
    }
    const uint32_t now = millis();
    float dt = (now - g_last_ms) * 0.001f;
    g_last_ms = now;
    dt = constrain(dt, 0.001f, 0.05f);

    // 速度档只缩放相位 / 形变推进，曲线参数本身不随时间累积
    const float step = dt * SPEED_MUL[g_speed];
    g_phase += step * PHASE_RATE;
    g_morph += step * MORPH_RATE;
    // 防止相位无限增大影响精度
    if (g_phase > TWO_PI_F * 8.0f) {
        g_phase = fmodf(g_phase, TWO_PI_F);
    }
    if (g_morph > TWO_PI_F * 8.0f) {
        g_morph = fmodf(g_morph, TWO_PI_F);
    }

    if (g_pulse > 0.0f) {
        g_pulse = fmaxf(0.0f, g_pulse - dt * 1.6f);
    }

    render();
}

void handleLissajousApp(const Keyboard_Class::KeysState& status) {
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == '-' && g_speed > 0) {
            --g_speed;
        } else if ((c == '=' || c == '+') && g_speed < SPEED_COUNT - 1) {
            ++g_speed;
        } else if (c == 'c' || c == 'm') {
            g_theme = (g_theme + 1) % THEME_COUNT;
            applyPalette();
        } else if (c == 'r') {
            g_phase = PI_F * 0.5f;
            g_morph = 0.0f;
        } else if (c == ' ') {
            g_pulse = 1.0f;
        }
    }
}

void pollLissajousBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        g_pulse = 1.0f;
    }
}
