#include "app_games.h"
#include "app_bezier_wave.h"
#include "app_common.h"
#include "app_header.h"
#include "app_dice.h"
#include "app_life.h"
#include "app_lissajous.h"
#include "app_matrix_rain.h"
#include "app_minesweeper.h"
#include "app_neon_fx.h"
#include "app_newton_cradle.h"
#include "app_particle_clock.h"
#include "app_snake.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_system.h>

namespace {

static constexpr float PI_F = 3.14159265f;
static constexpr float TWO_PI_F = PI_F * 2.0f;

enum class GameMode {
    HUB,
    COIN,
    DOUBLE_PENDULUM,
    WHEEL,
    DICE,
    NEWTON_CRADLE,
    NEON_FX,
    CURVES,
    MINESWEEPER,
    SNAKE,
    LIFE,
    MATRIX_RAIN,
    BEZIER_WAVE,
    PARTICLE_CLOCK,
    LISSAJOUS,
};

struct TracePoint {
    int16_t x;
    int16_t y;
};

static M5Canvas gamesCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static bool g_help = false;
static bool g_imu_ok = false;
static int g_width = 0;
static int g_height = 0;
static uint32_t g_last_ms = 0;
static GameMode g_mode = GameMode::HUB;
static int g_hub_page = 0;

static constexpr int GAMES_HUB_ITEMS_PER_PAGE = 8;

struct GamesHubItem {
    const char* title;
    GameMode mode;
};

static constexpr GamesHubItem GAMES_HUB_ITEMS[] = {
    {"COIN", GameMode::COIN},
    {"CHAOS", GameMode::DOUBLE_PENDULUM},
    {"WHEEL", GameMode::WHEEL},
    {"DICE", GameMode::DICE},
    {"PHYS", GameMode::NEWTON_CRADLE},
    {"NEON FX", GameMode::NEON_FX},
    {"CURVES", GameMode::CURVES},
    {"MINES", GameMode::MINESWEEPER},
    {"SNAKE", GameMode::SNAKE},
    {"LIFE", GameMode::LIFE},
    {"MATRIX", GameMode::MATRIX_RAIN},
    {"WAVE", GameMode::BEZIER_WAVE},
    {"PCLOCK", GameMode::PARTICLE_CLOCK},
    {"LISSA", GameMode::LISSAJOUS},
};
static constexpr int GAMES_HUB_ITEM_COUNT =
    static_cast<int>(sizeof(GAMES_HUB_ITEMS) / sizeof(GAMES_HUB_ITEMS[0]));

// IMU 滤波与摇晃边沿由硬币、迷宫和转盘共用。
static float g_ax = 0.0f;
static float g_ay = 0.0f;
static float g_az = 1.0f;
static float g_prev_ax = 0.0f;
static float g_prev_ay = 0.0f;
static float g_prev_az = 1.0f;
static bool g_imu_sample_ready = false;
static bool g_was_shaking = false;

// 硬币：正视时宽高相等的正圆；落地中心与阴影对齐。
static constexpr float COIN_RADIUS = 28.0f;
static constexpr float COIN_GROUND_Y = 86.0f;
static constexpr int COIN_SHADOW_Y = 115;
static constexpr float COIN_SHADOW_FADE_H = 58.0f; // 接近峰值时阴影几乎消失
static constexpr float COIN_SHADOW_MAX_R = COIN_RADIUS * 0.86f; // 落地阴影略小于硬币
static float g_coin_y = COIN_GROUND_Y;
static float g_coin_vy = 0.0f;
static float g_coin_angle = 0.0f;
static float g_coin_omega = 0.0f;
static int g_coin_bounces = 0;
static bool g_coin_tossing = false;
static bool g_coin_heads = true;
static bool g_coin_has_tossed = false;
static float g_coin_result_reveal = 0.0f;

// 双摆状态。
static constexpr float PEND_L1_PX = 44.0f;
static constexpr float PEND_L2_PX_DEFAULT = 58.0f;
static constexpr float PEND_L2_PX_MIN = 28.0f;
static constexpr float PEND_L2_PX_MAX = 76.0f;
static constexpr float PEND_L2_PX_STEP = 4.0f;
static constexpr float PEND_L1_PHYS = 1.0f;
static constexpr float PEND_L2_PHYS_DEFAULT = 1.45f;
static float g_pend_a1 = 1.72f;
static float g_pend_a2 = 1.10f;
static float g_pend_w1 = 0.0f;
static float g_pend_w2 = 0.0f;
static float g_pend_l2_px = PEND_L2_PX_DEFAULT; // 第二段像素长度，-= 调节
static TracePoint g_trace[72];
static int g_trace_count = 0;
static int g_trace_head = 0;
static float g_trace_accum = 0.0f;

// 抽奖轮状态。
static constexpr int WHEEL_SEGMENTS_MIN = 2;
static constexpr int WHEEL_SEGMENTS_MAX = 12;
static constexpr uint32_t WHEEL_FULL_POWER_MS = 1800;
static int g_wheel_segments = 8;
static float g_wheel_angle = 0.0f;
static float g_wheel_omega = 0.0f;
static bool g_wheel_spinning = false;
static int g_wheel_result = -1;
static float g_wheel_result_reveal = 0.0f;
static bool g_wheel_space_held = false;
static uint32_t g_wheel_space_press_ms = 0;

// 方程曲线：数字键切曲线，-= /,/. /qe 调参。
static constexpr int CURVE_COUNT = 9;
static int g_curve_id = 0;
static float g_curve_amp = 1.0f;
static float g_curve_freq = 1.0f;
static float g_curve_phase = 0.0f;
static bool g_curve_animate = true;

static float randomUnit() {
    return static_cast<float>(esp_random() & 0x00FFFFFFU) / 16777215.0f;
}

static float wrapAngle(float angle) {
    while (angle >= TWO_PI_F) {
        angle -= TWO_PI_F;
    }
    while (angle < 0.0f) {
        angle += TWO_PI_F;
    }
    return angle;
}

static void applyPalette() {
    gamesCanvas.setPaletteColor(0, 0x05, 0x08, 0x0D);
    gamesCanvas.setPaletteColor(1, 0x0D, 0x16, 0x22);
    gamesCanvas.setPaletteColor(2, 0x17, 0x27, 0x38);
    gamesCanvas.setPaletteColor(3, 0x2D, 0x48, 0x5E);
    gamesCanvas.setPaletteColor(4, 0xE9, 0xC4, 0x6A);
    gamesCanvas.setPaletteColor(5, 0xFF, 0xE5, 0x91);
    gamesCanvas.setPaletteColor(6, 0xB9, 0x7A, 0x2F);
    gamesCanvas.setPaletteColor(7, 0xF4, 0xF1, 0xE8);
    gamesCanvas.setPaletteColor(8, 0xA9, 0xB4, 0xC0);
    gamesCanvas.setPaletteColor(9, 0x42, 0xD3, 0x92);
    gamesCanvas.setPaletteColor(10, 0xFF, 0x5E, 0x68);
    gamesCanvas.setPaletteColor(11, 0x56, 0xA8, 0xFF);
    gamesCanvas.setPaletteColor(12, 0xB0, 0x6C, 0xFF);
    gamesCanvas.setPaletteColor(13, 0xFF, 0x9D, 0x3F);
    gamesCanvas.setPaletteColor(14, 0x21, 0x63, 0x39);
    gamesCanvas.setPaletteColor(15, 0x39, 0xA8, 0x5C);
    // 结果牌金色，风格对齐 Dice。
    gamesCanvas.setPaletteColor(16, 0x08, 0x14, 0x10);
    gamesCanvas.setPaletteColor(17, 0x4A, 0x28, 0x05);
    gamesCanvas.setPaletteColor(18, 0xD8, 0x8A, 0x08);
    gamesCanvas.setPaletteColor(19, 0xFF, 0xE5, 0x72);
    gamesCanvas.setPaletteColor(20, 0xFF, 0xFA, 0xCF);
}

static bool ensureCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    gamesCanvas.setColorDepth(8);
    if (!gamesCanvas.createSprite(g_width, g_height)) {
        return false;
    }
    if (!gamesCanvas.createPalette()) {
        gamesCanvas.deleteSprite();
        return false;
    }
    g_canvas_ok = true;
    applyPalette();
    return true;
}

static const char* modeName() {
    switch (g_mode) {
        case GameMode::COIN:
            return "COIN TOSS";
        case GameMode::DOUBLE_PENDULUM:
            return "DOUBLE PENDULUM";
        case GameMode::WHEEL:
            return "PRIZE WHEEL";
        case GameMode::DICE:
            return "DICE";
        case GameMode::NEWTON_CRADLE:
            return "NEWTON CRADLE";
        case GameMode::NEON_FX:
            return "NEON FX";
        case GameMode::CURVES:
            return "CURVES";
        case GameMode::MINESWEEPER:
            return "MINESWEEPER";
        case GameMode::SNAKE:
            return "SNAKE";
        case GameMode::LIFE:
            return "CONWAY LIFE";
        case GameMode::MATRIX_RAIN:
            return "MATRIX RAIN";
        case GameMode::BEZIER_WAVE:
            return "BEZIER WAVE";
        case GameMode::PARTICLE_CLOCK:
            return "PARTICLE CLOCK";
        case GameMode::LISSAJOUS:
            return "LISSAJOUS";
        default:
            return "MINI GAMES";
    }
}

static void drawTopLabel(const char* right = nullptr) {
    gamesCanvas.setTextSize(1);
    gamesCanvas.setTextColor(5);
    gamesCanvas.setCursor(4, 3);
    gamesCanvas.print(modeName());
    if (right != nullptr) {
        gamesCanvas.setTextColor(8);
        gamesCanvas.setCursor(g_width - static_cast<int>(strlen(right)) * 6 - 4, 3);
        gamesCanvas.print(right);
    }
}

static void resetCoin() {
    g_coin_y = COIN_GROUND_Y;
    g_coin_vy = 0.0f;
    g_coin_angle = g_coin_heads ? 0.0f : PI_F;
    g_coin_omega = 0.0f;
    g_coin_bounces = 0;
    g_coin_tossing = false;
    g_coin_has_tossed = false;
    g_coin_result_reveal = 0.0f;
}

static void tossCoin() {
    g_coin_heads = (esp_random() & 1U) != 0;
    g_coin_y = COIN_GROUND_Y;
    g_coin_vy = -245.0f - randomUnit() * 55.0f;
    g_coin_omega = 17.0f + randomUnit() * 7.0f;
    g_coin_bounces = 0;
    g_coin_tossing = true;
    g_coin_has_tossed = true;
    g_coin_result_reveal = 0.0f;
}

static void stepCoin(const float dt) {
    if (!g_coin_tossing) {
        if (g_coin_has_tossed && g_coin_result_reveal < 1.0f) {
            g_coin_result_reveal = fminf(1.0f, g_coin_result_reveal + dt * 2.4f);
        }
        return;
    }
    g_coin_vy += 430.0f * dt;
    g_coin_y += g_coin_vy * dt;
    g_coin_angle += g_coin_omega * dt;
    g_coin_omega *= expf(-0.30f * dt);

    if (g_coin_y >= COIN_GROUND_Y && g_coin_vy > 0.0f) {
        g_coin_y = COIN_GROUND_Y;
        g_coin_vy *= -0.38f;
        g_coin_omega *= 0.68f;
        g_coin_bounces++;
    }

    if (g_coin_bounces >= 3 && fabsf(g_coin_vy) < 34.0f) {
        const float target = g_coin_heads ? 0.0f : PI_F;
        float delta = target - fmodf(g_coin_angle, TWO_PI_F);
        while (delta > PI_F) {
            delta -= TWO_PI_F;
        }
        while (delta < -PI_F) {
            delta += TWO_PI_F;
        }
        g_coin_angle += delta * fminf(1.0f, dt * 10.0f);
        g_coin_omega *= expf(-7.0f * dt);
        if (fabsf(delta) < 0.025f && fabsf(g_coin_vy) < 5.0f) {
            g_coin_angle = target;
            g_coin_vy = 0.0f;
            g_coin_omega = 0.0f;
            g_coin_tossing = false;
        }
    }
}

static int coinFaceX(const float normalized_x, const int half_w) {
    return 120 + static_cast<int>(normalized_x * half_w);
}

// 正面绘制侧脸，反面绘制放射徽记；压缩 X 坐标后仍随硬币透视变窄。
static void drawCoinFace(const int cy, const int half_w, const int half_h, const bool heads) {
    if (half_w < 10) {
        return;
    }
    const uint8_t motif = heads ? 6 : 3;
    const float sy = half_h / COIN_RADIUS;
    if (heads) {
        const int head_rx = fmaxf(2.0f, half_w * 0.30f);
        gamesCanvas.fillEllipse(coinFaceX(-0.10f, half_w), cy - static_cast<int>(6.0f * sy),
                                head_rx, static_cast<int>(10.0f * sy), motif);
        gamesCanvas.fillTriangle(coinFaceX(0.12f, half_w), cy - static_cast<int>(9.0f * sy),
                                 coinFaceX(0.42f, half_w), cy - static_cast<int>(3.0f * sy),
                                 coinFaceX(0.10f, half_w), cy, motif);
        gamesCanvas.fillEllipse(coinFaceX(-0.20f, half_w), cy + static_cast<int>(11.0f * sy),
                                fmaxf(3.0f, half_w * 0.50f), static_cast<int>(5.0f * sy), motif);
        gamesCanvas.drawFastVLine(coinFaceX(-0.48f, half_w), cy - static_cast<int>(10.0f * sy),
                                  static_cast<int>(20.0f * sy), motif);
        gamesCanvas.fillCircle(coinFaceX(0.02f, half_w), cy - static_cast<int>(7.0f * sy), 1, 5);
    } else {
        gamesCanvas.drawEllipse(120, cy, fmaxf(3.0f, half_w * 0.55f),
                                static_cast<int>(14.0f * sy), motif);
        gamesCanvas.fillEllipse(120, cy, fmaxf(2.0f, half_w * 0.18f),
                                static_cast<int>(5.0f * sy), motif);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * PI_F / 4.0f;
            const int x0 = coinFaceX(cosf(angle) * 0.31f, half_w);
            const int y0 = cy + static_cast<int>(sinf(angle) * 8.0f * sy);
            const int x1 = coinFaceX(cosf(angle) * 0.67f, half_w);
            const int y1 = cy + static_cast<int>(sinf(angle) * 17.0f * sy);
            gamesCanvas.drawLine(x0, y0, x1, y1, motif);
        }
    }
}

// 金色结果牌，动画与 Dice 一致。
static void drawCoinResultBanner() {
    if (!g_coin_has_tossed || g_coin_tossing || g_coin_result_reveal <= 0.0f) {
        return;
    }
    const float ease =
        g_coin_result_reveal * g_coin_result_reveal * (3.0f - 2.0f * g_coin_result_reveal);
    constexpr int panel_w = 116;
    constexpr int panel_h = 25;
    const int panel_x = (g_width - panel_w) / 2;
    const int panel_y = static_cast<int>(-panel_h + ease * 30.0f);

    gamesCanvas.fillRoundRect(panel_x + 3, panel_y + 3, panel_w, panel_h, 6, 16);
    gamesCanvas.fillRoundRect(panel_x, panel_y, panel_w, panel_h, 6, 17);
    gamesCanvas.drawRoundRect(panel_x, panel_y, panel_w, panel_h, 6, 18);
    gamesCanvas.drawRoundRect(panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4, 4, 19);

    const char* line = g_coin_heads ? "HEADS" : "TAILS";
    gamesCanvas.setTextSize(2);
    gamesCanvas.setTextColor(19);
    gamesCanvas.setCursor((g_width - static_cast<int>(strlen(line)) * 12) / 2, panel_y + 6);
    gamesCanvas.print(line);

    if (g_coin_result_reveal < 1.0f) {
        const int glint_x = panel_x + 5 + static_cast<int>(ease * (panel_w - 10));
        gamesCanvas.drawFastVLine(glint_x, panel_y + 4, panel_h - 8, 20);
    }
}

static void drawCoin() {
    gamesCanvas.fillSprite(0);
    drawTopLabel(g_imu_ok ? "IMU" : "KEY");

    // 高度越高阴影越小越淡，峰值时几乎不可见；落地时略小于硬币半径。
    const float height = fmaxf(0.0f, COIN_GROUND_Y - g_coin_y);
    const float fade = 1.0f - fminf(1.0f, height / COIN_SHADOW_FADE_H);
    const float shadow_strength = fade * fade;
    if (shadow_strength > 0.04f) {
        const int shadow_w = fmaxf(2.0f, COIN_SHADOW_MAX_R * shadow_strength);
        const int shadow_h = fmaxf(1.0f, 5.0f * shadow_strength);
        gamesCanvas.fillEllipse(120, COIN_SHADOW_Y, shadow_w, shadow_h, 1);
        if (shadow_w > 6 && shadow_h > 1) {
            gamesCanvas.fillEllipse(120, COIN_SHADOW_Y - 1, shadow_w - 4,
                                    fmaxf(1.0f, shadow_h - 2.0f), 2);
        }
    }

    // 正视宽高相等；翻转时只压缩水平方向。
    const float face = fabsf(cosf(g_coin_angle));
    const int half_w = 3 + static_cast<int>((COIN_RADIUS - 3.0f) * face);
    const int half_h = static_cast<int>(COIN_RADIUS);
    const int cy = static_cast<int>(g_coin_y);
    const bool visible_heads = cosf(g_coin_angle) >= 0.0f;
    gamesCanvas.fillEllipse(120, cy + 2, half_w + 2, half_h, 6);
    gamesCanvas.fillEllipse(120, cy, half_w, half_h, visible_heads ? 4 : 5);
    if (half_w > 8) {
        gamesCanvas.drawEllipse(120, cy, half_w - 3, half_h - 4, visible_heads ? 5 : 4);
        gamesCanvas.drawEllipse(120, cy, half_w, half_h, 6);
        drawCoinFace(cy, half_w, half_h, visible_heads);
    } else {
        gamesCanvas.fillRect(120 - half_w, cy - half_h + 2, half_w * 2, half_h * 2 - 4, 6);
        gamesCanvas.drawFastVLine(120 - half_w, cy - half_h + 4, half_h * 2 - 8, 5);
    }

    drawCoinResultBanner();
}

static void clearPendulumTrace() {
    g_trace_count = 0;
    g_trace_head = 0;
    g_trace_accum = 0.0f;
}

static void resetPendulum(const bool randomize) {
    g_pend_a1 = randomize ? (0.8f + randomUnit() * 1.6f) : 1.72f;
    g_pend_a2 = randomize ? (-1.2f + randomUnit() * 2.4f) : 1.10f;
    g_pend_w1 = 0.0f;
    g_pend_w2 = 0.0f;
    clearPendulumTrace();
}

// 调节第二段长度；改长后清空残影，避免旧轨迹错位
static void changePendulumL2(const float delta) {
    const float next = constrain(g_pend_l2_px + delta, PEND_L2_PX_MIN, PEND_L2_PX_MAX);
    if (next == g_pend_l2_px) {
        return;
    }
    g_pend_l2_px = next;
    clearPendulumTrace();
}

static void stepPendulum(const float dt) {
    constexpr float g = 9.4f;
    constexpr float m1 = 1.0f;
    // 末端质量更小、第二段更长，更容易走出混沌轨迹
    constexpr float m2 = 0.35f;
    constexpr float l1 = PEND_L1_PHYS;
    // 物理 l2 随像素长度同比缩放，保持默认比例
    const float l2 = PEND_L2_PHYS_DEFAULT * (g_pend_l2_px / PEND_L2_PX_DEFAULT);
    const float delta = g_pend_a1 - g_pend_a2;
    const float den = 2.0f * m1 + m2 - m2 * cosf(2.0f * delta);
    if (fabsf(den) < 0.001f) {
        return;
    }

    const float a1_acc =
        (-g * (2.0f * m1 + m2) * sinf(g_pend_a1) -
         m2 * g * sinf(g_pend_a1 - 2.0f * g_pend_a2) -
         2.0f * sinf(delta) * m2 *
             (g_pend_w2 * g_pend_w2 * l2 +
              g_pend_w1 * g_pend_w1 * l1 * cosf(delta))) /
        (l1 * den);
    const float a2_acc =
        (2.0f * sinf(delta) *
         (g_pend_w1 * g_pend_w1 * l1 * (m1 + m2) +
          g * (m1 + m2) * cosf(g_pend_a1) +
          g_pend_w2 * g_pend_w2 * l2 * m2 * cosf(delta))) /
        (l2 * den);

    // 半隐式积分配合轻阻尼，限制异常帧造成的能量爆炸。
    g_pend_w1 = constrain((g_pend_w1 + a1_acc * dt) * expf(-0.012f * dt), -14.0f, 14.0f);
    g_pend_w2 = constrain((g_pend_w2 + a2_acc * dt) * expf(-0.012f * dt), -14.0f, 14.0f);
    g_pend_a1 += g_pend_w1 * dt;
    g_pend_a2 += g_pend_w2 * dt;
}

static void pendulumPoints(int& x1, int& y1, int& x2, int& y2) {
    constexpr int pivot_x = 120;
    constexpr int pivot_y = 18;
    const float l1 = PEND_L1_PX;
    const float l2 = g_pend_l2_px;
    x1 = pivot_x + static_cast<int>(l1 * sinf(g_pend_a1));
    y1 = pivot_y + static_cast<int>(l1 * cosf(g_pend_a1));
    x2 = x1 + static_cast<int>(l2 * sinf(g_pend_a2));
    y2 = y1 + static_cast<int>(l2 * cosf(g_pend_a2));
}

static void addPendulumTrace(const float dt) {
    g_trace_accum += dt;
    if (g_trace_accum < 0.018f) {
        return;
    }
    g_trace_accum = 0.0f;
    int x1, y1, x2, y2;
    pendulumPoints(x1, y1, x2, y2);
    g_trace[g_trace_head] = {static_cast<int16_t>(x2), static_cast<int16_t>(y2)};
    g_trace_head = (g_trace_head + 1) % static_cast<int>(sizeof(g_trace) / sizeof(g_trace[0]));
    if (g_trace_count < static_cast<int>(sizeof(g_trace) / sizeof(g_trace[0]))) {
        g_trace_count++;
    }
}

static void drawPendulum() {
    gamesCanvas.fillSprite(0);
    char l2_label[12];
    snprintf(l2_label, sizeof(l2_label), "L2 %d", static_cast<int>(g_pend_l2_px + 0.5f));
    drawTopLabel(l2_label);

    for (int i = 1; i < g_trace_count; ++i) {
        const int capacity = static_cast<int>(sizeof(g_trace) / sizeof(g_trace[0]));
        const int first = (g_trace_head - g_trace_count + capacity) % capacity;
        const TracePoint& a = g_trace[(first + i - 1) % capacity];
        const TracePoint& b = g_trace[(first + i) % capacity];
        const uint8_t color = (i < g_trace_count / 3) ? 3 : ((i < g_trace_count * 2 / 3) ? 11 : 12);
        gamesCanvas.drawLine(a.x, a.y, b.x, b.y, color);
    }

    int x1, y1, x2, y2;
    pendulumPoints(x1, y1, x2, y2);
    gamesCanvas.drawLine(120, 18, x1, y1, 8);
    gamesCanvas.drawLine(x1, y1, x2, y2, 7);
    gamesCanvas.fillCircle(120, 18, 3, 5);
    gamesCanvas.fillCircle(x1, y1, 7, 11);
    gamesCanvas.fillCircle(x1 - 2, y1 - 2, 2, 7);
    // 末端球更小，对应更轻的 m2
    gamesCanvas.fillCircle(x2, y2, 5, 12);
    gamesCanvas.fillCircle(x2 - 1, y2 - 2, 1, 7);
}

static void spinWheel(const float charge) {
    const float c = fminf(1.0f, fmaxf(0.0f, charge));
    g_wheel_omega = 6.0f + c * 10.0f + randomUnit() * (2.0f + c * 3.0f);
    g_wheel_spinning = true;
    g_wheel_result = -1;
    g_wheel_result_reveal = 0.0f;
    g_wheel_space_held = false;
}

// 停止转动并回到初始姿态（改格数时调用）
static void resetWheelState() {
    g_wheel_angle = 0.0f;
    g_wheel_omega = 0.0f;
    g_wheel_spinning = false;
    g_wheel_result = -1;
    g_wheel_result_reveal = 0.0f;
    g_wheel_space_held = false;
    g_wheel_space_press_ms = 0;
}

static void changeWheelSegments(const int delta) {
    const int next = g_wheel_segments + delta;
    if (next < WHEEL_SEGMENTS_MIN || next > WHEEL_SEGMENTS_MAX) {
        return;
    }
    if (next == g_wheel_segments) {
        return;
    }
    g_wheel_segments = next;
    resetWheelState();
}

// 空格 / BtnA 长按蓄力，松开后按力度开转（对齐 Dice）
static bool isWheelChargeDown() {
    if (M5Cardputer.BtnA.isPressed()) {
        return true;
    }
    const Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
    return M5Cardputer.Keyboard.isPressed() && keys.space;
}

static void updateWheelSpaceSpin(const uint32_t now) {
    if (g_wheel_spinning) {
        return;
    }
    const bool charge_down = isWheelChargeDown();
    if (charge_down && !g_wheel_space_held) {
        g_wheel_space_held = true;
        g_wheel_space_press_ms = now;
        return;
    }
    if (charge_down || !g_wheel_space_held) {
        return;
    }
    const uint32_t held_ms = now - g_wheel_space_press_ms;
    const float charge =
        fminf(1.0f, static_cast<float>(held_ms) / static_cast<float>(WHEEL_FULL_POWER_MS));
    spinWheel(charge);
    g_wheel_space_held = false;
}

// 指针固定在十二点（角度 -PI/2），扇区 seg 覆盖 [angle+seg*step, angle+(seg+1)*step)
static int wheelIndexAtPointer() {
    const float step = TWO_PI_F / g_wheel_segments;
    const float local = wrapAngle(-PI_F * 0.5f - g_wheel_angle);
    return static_cast<int>(local / step) % g_wheel_segments;
}

static void stepWheel(const float dt) {
    if (!g_wheel_spinning) {
        return;
    }
    g_wheel_angle = wrapAngle(g_wheel_angle + g_wheel_omega * dt);
    g_wheel_omega *= expf(-0.72f * dt);
    if (g_wheel_omega < 0.22f) {
        g_wheel_omega = 0.0f;
        g_wheel_spinning = false;
        g_wheel_result = wheelIndexAtPointer();
        g_wheel_result_reveal = 0.0f;
    }
}

// 蓄力条（底栏，对齐 Dice）
static void drawWheelChargeIndicator(const uint32_t now) {
    if (!g_wheel_space_held || g_wheel_spinning) {
        return;
    }
    const uint32_t held_ms = now - g_wheel_space_press_ms;
    const float charge =
        fminf(1.0f, static_cast<float>(held_ms) / static_cast<float>(WHEEL_FULL_POWER_MS));
    constexpr int panel_w = 116;
    constexpr int panel_h = 14;
    constexpr int bar_w = 48;
    const int panel_x = (g_width - panel_w) / 2;
    const int panel_y = g_height - panel_h - 4;
    const int bar_x = panel_x + 38;
    const int bar_y = panel_y + 4;

    gamesCanvas.fillRoundRect(panel_x, panel_y, panel_w, panel_h, 4, 16);
    gamesCanvas.drawRoundRect(panel_x, panel_y, panel_w, panel_h, 4, 18);
    gamesCanvas.setTextSize(1);
    gamesCanvas.setTextColor(19);
    gamesCanvas.setCursor(panel_x + 5, panel_y + 3);
    gamesCanvas.print("POWER");
    gamesCanvas.drawRect(bar_x, bar_y, bar_w, 6, 18);
    gamesCanvas.fillRect(bar_x + 1, bar_y + 1, static_cast<int>((bar_w - 2) * charge), 4, 19);

    char percent[6];
    snprintf(percent, sizeof(percent), "%3d%%", static_cast<int>(charge * 100.0f));
    gamesCanvas.setCursor(panel_x + 89, panel_y + 3);
    gamesCanvas.print(percent);
}

// 金色结果牌：滑入 + 高亮中奖格
static void drawWheelResultBanner() {
    if (g_wheel_result < 0 || g_wheel_result_reveal <= 0.0f) {
        return;
    }
    const float ease =
        g_wheel_result_reveal * g_wheel_result_reveal * (3.0f - 2.0f * g_wheel_result_reveal);
    constexpr int panel_w = 116;
    constexpr int panel_h = 25;
    const int panel_x = (g_width - panel_w) / 2;
    const int panel_y = static_cast<int>(-panel_h + ease * 30.0f);

    gamesCanvas.fillRoundRect(panel_x + 3, panel_y + 3, panel_w, panel_h, 6, 16);
    gamesCanvas.fillRoundRect(panel_x, panel_y, panel_w, panel_h, 6, 17);
    gamesCanvas.drawRoundRect(panel_x, panel_y, panel_w, panel_h, 6, 18);
    gamesCanvas.drawRoundRect(panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4, 4, 19);

    char line[16];
    snprintf(line, sizeof(line), "#%d", g_wheel_result + 1);
    gamesCanvas.setTextSize(2);
    gamesCanvas.setTextColor(19);
    gamesCanvas.setCursor((g_width - static_cast<int>(strlen(line)) * 12) / 2, panel_y + 6);
    gamesCanvas.print(line);

    if (g_wheel_result_reveal < 1.0f) {
        const int glint_x = panel_x + 5 + static_cast<int>(ease * (panel_w - 10));
        gamesCanvas.drawFastVLine(glint_x, panel_y + 4, panel_h - 8, 20);
    }
}

static void drawWheel() {
    gamesCanvas.fillSprite(0);
    static char top[16];
    if (g_wheel_spinning) {
        strncpy(top, "SPIN...", sizeof(top));
        top[sizeof(top) - 1] = '\0';
    } else if (g_wheel_space_held) {
        strncpy(top, "HOLD", sizeof(top));
        top[sizeof(top) - 1] = '\0';
    } else if (g_wheel_result >= 0 && g_wheel_result_reveal > 0.0f) {
        strncpy(top, "WINNER", sizeof(top));
        top[sizeof(top) - 1] = '\0';
    } else {
        snprintf(top, sizeof(top), "%d ITEMS", g_wheel_segments);
    }
    drawTopLabel(top);

    constexpr int cx = 120;
    constexpr int cy = 68;
    constexpr int radius = 48;
    constexpr int slices = 6;
    static const uint8_t colors[] = {10, 13, 4, 9, 11, 12, 6, 3};
    const int color_count = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    const float step = TWO_PI_F / g_wheel_segments;
    const bool show_winner = g_wheel_result >= 0 && g_wheel_result_reveal > 0.0f;
    for (int seg = 0; seg < g_wheel_segments; ++seg) {
        const bool winner = show_winner && seg == g_wheel_result;
        for (int part = 0; part < slices; ++part) {
            const float a0 = g_wheel_angle + seg * step + part * step / slices;
            const float a1 = g_wheel_angle + seg * step + (part + 1) * step / slices;
            const int x0 = cx + static_cast<int>(cosf(a0) * radius);
            const int y0 = cy + static_cast<int>(sinf(a0) * radius);
            const int x1 = cx + static_cast<int>(cosf(a1) * radius);
            const int y1 = cy + static_cast<int>(sinf(a1) * radius);
            const uint8_t fill = winner ? 5 : colors[seg % color_count];
            gamesCanvas.fillTriangle(cx, cy, x0, y0, x1, y1, fill);
        }
        const float mid = g_wheel_angle + (seg + 0.5f) * step;
        const int tx = cx + static_cast<int>(cosf(mid) * 31.0f) - ((seg >= 9) ? 6 : 3);
        const int ty = cy + static_cast<int>(sinf(mid) * 31.0f) - 4;
        gamesCanvas.setTextSize(1);
        gamesCanvas.setTextColor(winner ? 0 : 7);
        gamesCanvas.setCursor(tx, ty);
        gamesCanvas.print(seg + 1);
        if (winner) {
            const float a0 = g_wheel_angle + seg * step;
            const float a1 = g_wheel_angle + (seg + 1) * step;
            const int x0 = cx + static_cast<int>(cosf(a0) * radius);
            const int y0 = cy + static_cast<int>(sinf(a0) * radius);
            const int x1 = cx + static_cast<int>(cosf(a1) * radius);
            const int y1 = cy + static_cast<int>(sinf(a1) * radius);
            gamesCanvas.drawLine(cx, cy, x0, y0, 19);
            gamesCanvas.drawLine(cx, cy, x1, y1, 19);
            gamesCanvas.drawLine(x0, y0, x1, y1, 19);
        }
    }
    gamesCanvas.drawCircle(cx, cy, radius, 7);
    gamesCanvas.fillCircle(cx, cy, 7, 0);
    gamesCanvas.fillCircle(cx, cy, 4, 5);

    // 固定指针位于十二点方向，轮盘在其下转动。
    gamesCanvas.fillTriangle(cx, 15, cx - 7, 5, cx + 7, 5, 5);
    gamesCanvas.drawTriangle(cx, 15, cx - 7, 5, cx + 7, 5, 6);

    drawWheelResultBanner();
    drawWheelChargeIndicator(millis());
}

static const char* curveName(const int id) {
    static const char* names[CURVE_COUNT] = {
        "SIN", "COS", "PARA", "CUBIC", "EXP", "LOG", "CIRCLE", "HEART", "LISSA",
    };
    if (id < 0 || id >= CURVE_COUNT) {
        return "?";
    }
    return names[id];
}

static const char* curveFormula(const int id) {
    static const char* formulas[CURVE_COUNT] = {
        "a*sin(bx+p)", "a*cos(bx+p)", "a*x^2", "a*x^3", "a*e^(bx)",
        "a*ln(bx)", "x^2+y^2=a^2", "cardioid", "a*sin(bt),a*sin(ct+p)",
    };
    if (id < 0 || id >= CURVE_COUNT) {
        return "";
    }
    return formulas[id];
}

static void resetCurves() {
    g_curve_id = 0;
    g_curve_amp = 1.0f;
    g_curve_freq = 1.0f;
    g_curve_phase = 0.0f;
    g_curve_animate = true;
}

static void stepCurves(const float dt) {
    if (g_curve_animate) {
        g_curve_phase = wrapAngle(g_curve_phase + dt * 1.35f * g_curve_freq);
    }
}

// 采样常用曲线并连线；参数 a/b/p 分别对应幅度、频率、相位。
static void drawCurves() {
    gamesCanvas.fillSprite(0);
    char right[12];
    snprintf(right, sizeof(right), "%d/%d", g_curve_id + 1, CURVE_COUNT);
    drawTopLabel(right);

    constexpr int ox = 120;
    constexpr int oy = 72;
    constexpr float unit = 28.0f;
    gamesCanvas.drawFastHLine(8, oy, g_width - 16, 2);
    gamesCanvas.drawFastVLine(ox, 16, 108, 2);

    gamesCanvas.setTextSize(1);
    gamesCanvas.setTextColor(5);
    gamesCanvas.setCursor(4, 16);
    gamesCanvas.print(curveName(g_curve_id));
    gamesCanvas.setTextColor(8);
    gamesCanvas.setCursor(4, 28);
    gamesCanvas.print(curveFormula(g_curve_id));

    char params[28];
    snprintf(params, sizeof(params), "a%.1f b%.1f %s", g_curve_amp, g_curve_freq,
             g_curve_animate ? "RUN" : "PAUSE");
    gamesCanvas.setCursor(g_width - static_cast<int>(strlen(params)) * 6 - 4, 16);
    gamesCanvas.print(params);

    const float a = g_curve_amp;
    const float b = g_curve_freq;
    const float p = g_curve_phase;
    int prev_x = -999;
    int prev_y = -999;
    const int samples = (g_curve_id == 6 || g_curve_id == 7 || g_curve_id == 8) ? 180 : 120;

    for (int i = 0; i <= samples; ++i) {
        float px = 0.0f;
        float py = 0.0f;
        if (g_curve_id <= 5) {
            const float t = -2.2f + 4.4f * static_cast<float>(i) / samples;
            px = t;
            if (g_curve_id == 0) {
                py = a * sinf(b * t + p);
            } else if (g_curve_id == 1) {
                py = a * cosf(b * t + p);
            } else if (g_curve_id == 2) {
                py = a * t * t * 0.55f - a * 0.35f;
            } else if (g_curve_id == 3) {
                py = a * t * t * t * 0.35f;
            } else if (g_curve_id == 4) {
                py = a * (expf(b * t * 0.55f) - 1.0f) * 0.45f;
            } else {
                const float arg = fmaxf(0.12f, 1.15f + b * t * 0.45f);
                py = a * logf(arg) * 0.85f;
            }
        } else if (g_curve_id == 6) {
            const float th = TWO_PI_F * static_cast<float>(i) / samples + p;
            const float r = fmaxf(0.25f, a);
            px = r * cosf(th);
            py = r * sinf(th);
        } else if (g_curve_id == 7) {
            const float th = TWO_PI_F * static_cast<float>(i) / samples + p;
            const float s = sinf(th);
            px = a * 16.0f * s * s * s / 17.0f;
            py = -a * (13.0f * cosf(th) - 5.0f * cosf(2.0f * th) - 2.0f * cosf(3.0f * th) -
                       cosf(4.0f * th)) /
                 17.0f;
        } else {
            const float th = TWO_PI_F * static_cast<float>(i) / samples;
            px = a * sinf(b * th);
            py = a * sinf((b + 1.0f) * th + p);
        }

        const int sx = ox + static_cast<int>(px * unit);
        const int sy = oy - static_cast<int>(py * unit);
        if (sx >= 2 && sx < g_width - 2 && sy >= 16 && sy < g_height - 2) {
            if (prev_x > -900) {
                gamesCanvas.drawLine(prev_x, prev_y, sx, sy, 11);
            }
            prev_x = sx;
            prev_y = sy;
        } else {
            prev_x = -999;
        }
    }
}

static uint16_t gamesHubBg() {
    return M5Cardputer.Display.color565(0x05, 0x08, 0x0D);
}

static uint16_t gamesHubCardBg() {
    return M5Cardputer.Display.color565(0x0D, 0x16, 0x22);
}

static uint16_t gamesHubAccent() {
    return M5Cardputer.Display.color565(0xE9, 0xC4, 0x6A); // 暖金主色
}

// 边框用浅暖金，弱于徽章主色
static uint16_t gamesHubBorder() {
    return M5Cardputer.Display.color565(0x9A, 0x82, 0x48);
}

static uint16_t gamesHubTitleColor() {
    return M5Cardputer.Display.color565(0xF4, 0xF1, 0xE8);
}

// Games hub 卡片：尺寸与主菜单一致
static void drawHubCard(const int x, const int y, const int number, const char* title) {
    const uint16_t card_bg = gamesHubCardBg();
    const uint16_t accent = gamesHubAccent();
    M5Cardputer.Display.fillRoundRect(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, 4, gamesHubBorder());
    M5Cardputer.Display.fillRoundRect(x + 3, y + 3, 18, 16, 3, accent); // 整块序号徽章左移 1px
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, accent);
    M5Cardputer.Display.setCursor(x + 9, y + 7);
    M5Cardputer.Display.print(number);
    M5Cardputer.Display.setTextColor(gamesHubTitleColor(), card_bg);
    M5Cardputer.Display.setCursor(x + 28, y + 7);
    M5Cardputer.Display.print(title);
}

static int getGamesHubPageCount() {
    return (GAMES_HUB_ITEM_COUNT + GAMES_HUB_ITEMS_PER_PAGE - 1) / GAMES_HUB_ITEMS_PER_PAGE;
}

static void drawHubCardAt(const int index, const int number, const char* title) {
    const int row = index / APP_HUB_CARD_COLS;
    const int col = index % APP_HUB_CARD_COLS;
    const int x = APP_HUB_CARD_ORIGIN_X + col * (APP_HUB_CARD_W + APP_HUB_CARD_GAP_X);
    const int y = APP_HUB_CARD_ORIGIN_Y + row * (APP_HUB_CARD_H + APP_HUB_CARD_GAP_Y);
    drawHubCard(x, y, number, title);
}

static void drawHubCards() {
    // 每页最多四行八项，数字键按当前页从 1 重新编号。
    const int start = g_hub_page * GAMES_HUB_ITEMS_PER_PAGE;
    const int end = min(start + GAMES_HUB_ITEMS_PER_PAGE, GAMES_HUB_ITEM_COUNT);
    for (int item = start; item < end; ++item) {
        const int slot = item - start;
        drawHubCardAt(slot, slot + 1, GAMES_HUB_ITEMS[item].title);
    }
}

static void showGamesHubScreen() {
    beginAppHubScreen("MINI GAMES", gamesHubBg(), g_hub_page, getGamesHubPageCount());
    drawHubCards();
}

static void drawHelp() {
    clearAppHeaderStatusRefresh(); // Help 自绘全屏标题，无共享 header
    // Hub 用 Games；进游戏后用卡片名作副标题
    const char* subtitle = "Games";
    for (const GamesHubItem& item : GAMES_HUB_ITEMS) {
        if (item.mode == g_mode) {
            subtitle = item.title;
            break;
        }
    }
    int y = drawAppHelpBegin(subtitle);
    constexpr int x = APP_HELP_CONTENT_X;

    switch (g_mode) {
        case GameMode::COIN:
            y = drawAppHelpBadge(x, y, "SPC", "toss the coin");
            y = drawAppHelpBadge(x, y, "IMU", "shake to toss");
            (void)drawAppHelpText(x, y, "Front portrait / back emblem");
            break;
        case GameMode::DOUBLE_PENDULUM:
            y = drawAppHelpBadge(x, y, "SPC", "reset");
            y = drawAppHelpKey(x, y, 'r', "random initial pose");
            y = drawAppHelpBadge(x, y, "-=", "2nd arm length");
            (void)drawAppHelpText(x, y, "Coupled chaotic pendulum");
            break;
        case GameMode::WHEEL:
            y = drawAppHelpBadge(x, y, "SPC", "hold = power");
            y = drawAppHelpBadge(x, y, "IMU", "shake to spin");
            (void)drawAppHelpBadge(x, y, "-=", "items 2-12, resets spin");
            break;
        case GameMode::DICE:
            y = drawAppHelpBadge(x, y, "SPC", "hold and release to toss");
            y = drawAppHelpBadge(x, y, "IMU", "shake to toss");
            (void)drawAppHelpBadge(x, y, "-=", "dice count 1 - 5");
            break;
        case GameMode::NEWTON_CRADLE:
            y = drawAppHelpBadge(x, y, "123", "launch ball count");
            y = drawAppHelpBadge(x, y, "SPC", "replay");
            (void)drawAppHelpKey(x, y, 'r', "reset to one ball");
            break;
        case GameMode::NEON_FX:
            y = drawAppHelpBadge(x, y, "EASD", "move core / cube");
            y = drawAppHelpKey(x, y, 'm', "cycle pattern");
            y = drawAppHelpKey(x, y, 'c', "change color");
            y = drawAppHelpBadge(x, y, "-=", "animation speed");
            y = drawAppHelpKey(x, y, 'r', "reverse");
            (void)drawAppHelpBadge(x, y, "SPC", "pulse");
            break;
        case GameMode::CURVES:
            y = drawAppHelpBadge(x, y, "1-9", "select curve");
            y = drawAppHelpBadge(x, y, "-=", "amplitude a");
            y = drawAppHelpBadge(x, y, ",.", "frequency b");
            y = drawAppHelpBadge(x, y, "QE", "phase p");
            y = drawAppHelpBadge(x, y, "SPC", "toggle animate");
            (void)drawAppHelpKey(x, y, 'r', "reset params");
            break;
        case GameMode::MINESWEEPER:
            y = drawAppHelpBadge(x, y, ";,./", "move cursor, hold repeat");
            y = drawAppHelpBadge(x, y, "SPC ]", "dig / chord on number");
            y = drawAppHelpBadge(x, y, "F [", "toggle flag");
            y = drawAppHelpKey(x, y, 'i', "IMU tilt cursor");
            y = drawAppHelpBadge(x, y, "1-3", "level, R new game");
            y = drawAppHelpKey(x, y, 'b', "records best / streak");
            (void)drawAppHelpText(x, y, "First dig is always safe");
            break;
        case GameMode::SNAKE:
            y = drawAppHelpBadge(x, y, ";,./", "steer, EASD also works");
            y = drawAppHelpBadge(x, y, "SPC", "start / pause / replay");
            y = drawAppHelpKey(x, y, 'i', "IMU tilt steer");
            y = drawAppHelpKey(x, y, 'm', "wall or wrap mode");
            y = drawAppHelpBadge(x, y, "-=", "speed level 1 - 5");
            y = drawAppHelpKey(x, y, 'r', "new game");
            (void)drawAppHelpText(x, y, "Gold fruit is worth 5");
            break;
        case GameMode::LIFE:
            y = drawAppHelpBadge(x, y, "SPC", "run / pause");
            y = drawAppHelpKey(x, y, 'n', "single step");
            y = drawAppHelpKey(x, y, 'r', "random soup, C clear");
            y = drawAppHelpBadge(x, y, "1-6", "glider gun pulsar...");
            y = drawAppHelpBadge(x, y, ";,./", "move, ENT toggle");
            y = drawAppHelpBadge(x, y, "-=", "speed 1 - 5");
            (void)drawAppHelpText(x, y, "Edges wrap around");
            break;
        case GameMode::MATRIX_RAIN:
            y = drawAppHelpBadge(x, y, "SPC", "pulse burst");
            y = drawAppHelpBadge(x, y, "-=", "fall speed 1 - 5");
            y = drawAppHelpKey(x, y, 'r', "reshuffle columns");
            (void)drawAppHelpText(x, y, "Matrix-style code rain");
            break;
        case GameMode::BEZIER_WAVE:
            y = drawAppHelpBadge(x, y, "SPC", "amplitude pulse");
            y = drawAppHelpBadge(x, y, "-=", "wave speed 1 - 5");
            y = drawAppHelpKey(x, y, 'c', "cycle color theme");
            y = drawAppHelpKey(x, y, 'r', "reset phase");
            (void)drawAppHelpText(x, y, "Layered bezier silk waves");
            break;
        case GameMode::PARTICLE_CLOCK:
            y = drawAppHelpBadge(x, y, "SPC", "reshuffle morph");
            y = drawAppHelpKey(x, y, 'm', "toggle HH:MM / HH:MM:SS");
            y = drawAppHelpKey(x, y, 'r', "reshuffle morph");
            (void)drawAppHelpText(x, y, "Particles form the clock");
            break;
        case GameMode::LISSAJOUS:
            y = drawAppHelpBadge(x, y, "SPC", "phase pulse");
            y = drawAppHelpBadge(x, y, "-=", "anim speed 1 - 5");
            y = drawAppHelpKey(x, y, 'c', "cycle color theme");
            y = drawAppHelpKey(x, y, 'r', "reset frequencies");
            (void)drawAppHelpText(x, y, "Lissajous a:b curves");
            break;
        default:
            y = drawAppHelpBadge(x, y, "1-8", "enter game on this page");
            y = drawAppHelpBadge(x, y, "[]", "flip hub page");
            y = drawAppHelpLabelText(x, y, "P1", APP_COLOR_LABEL, " Coin Chaos Wheel Dice");
            y = drawAppHelpText(x, y, "   Phys Neon Curves Mines");
            y = drawAppHelpLabelText(x, y, "P2", APP_COLOR_OK, " Snake Life Matrix Wave");
            y = drawAppHelpText(x, y, "   PClock Lissa");
            (void)drawAppHelpKey(x, y, 'h', "open help inside a game");
            break;
    }
    // ESC 全局关 Help；底栏仅 h close
    drawHelpHintRight("close");
}

static bool isExternalMode(const GameMode mode) {
    return mode == GameMode::DICE || mode == GameMode::NEWTON_CRADLE ||
           mode == GameMode::NEON_FX || mode == GameMode::MINESWEEPER ||
           mode == GameMode::SNAKE || mode == GameMode::LIFE ||
           mode == GameMode::MATRIX_RAIN || mode == GameMode::BEZIER_WAVE ||
           mode == GameMode::PARTICLE_CLOCK || mode == GameMode::LISSAJOUS;
}

static void leaveModeApp(const GameMode mode) {
    if (mode == GameMode::DICE) {
        leaveDiceApp();
    } else if (mode == GameMode::NEWTON_CRADLE) {
        leaveNewtonCradleApp();
    } else if (mode == GameMode::NEON_FX) {
        leaveNeonFxApp();
    } else if (mode == GameMode::MINESWEEPER) {
        leaveMinesweeperApp();
    } else if (mode == GameMode::SNAKE) {
        leaveSnakeApp();
    } else if (mode == GameMode::LIFE) {
        leaveLifeApp();
    } else if (mode == GameMode::MATRIX_RAIN) {
        leaveMatrixRainApp();
    } else if (mode == GameMode::BEZIER_WAVE) {
        leaveBezierWaveApp();
    } else if (mode == GameMode::PARTICLE_CLOCK) {
        leaveParticleClockApp();
    } else if (mode == GameMode::LISSAJOUS) {
        leaveLissajousApp();
    }
}

static void drawExternalFrame() {
    if (g_mode == GameMode::DICE) {
        updateDiceApp();
    } else if (g_mode == GameMode::NEWTON_CRADLE) {
        updateNewtonCradleApp();
    } else if (g_mode == GameMode::NEON_FX) {
        updateNeonFxApp();
    } else if (g_mode == GameMode::MINESWEEPER) {
        updateMinesweeperApp();
    } else if (g_mode == GameMode::SNAKE) {
        updateSnakeApp();
    } else if (g_mode == GameMode::LIFE) {
        updateLifeApp();
    } else if (g_mode == GameMode::MATRIX_RAIN) {
        updateMatrixRainApp();
    } else if (g_mode == GameMode::BEZIER_WAVE) {
        updateBezierWaveApp();
    } else if (g_mode == GameMode::PARTICLE_CLOCK) {
        updateParticleClockApp();
    } else if (g_mode == GameMode::LISSAJOUS) {
        updateLissajousApp();
    }
}

static void drawCurrent() {
    if (g_help) {
        drawHelp();
        return;
    }
    if (g_mode == GameMode::HUB) {
        if (!g_help) {
            showGamesHubScreen();
        }
        return;
    }
    if (isExternalMode(g_mode)) {
        drawExternalFrame();
        return;
    }
    if (!g_canvas_ok) {
        return;
    }
    switch (g_mode) {
        case GameMode::COIN:
            drawCoin();
            break;
        case GameMode::DOUBLE_PENDULUM:
            drawPendulum();
            break;
        case GameMode::WHEEL:
            drawWheel();
            break;
        case GameMode::CURVES:
            drawCurves();
            break;
        default:
            break;
    }
    gamesCanvas.pushSprite(0, 0);
}

static bool sampleImu() {
    M5.Imu.update();
    if (!g_imu_ok) {
        return false;
    }
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    M5.Imu.getAccel(&ax, &ay, &az);
    g_ax += (ax - g_ax) * 0.18f;
    g_ay += (ay - g_ay) * 0.18f;
    g_az += (az - g_az) * 0.18f;

    if (!g_imu_sample_ready) {
        g_prev_ax = ax;
        g_prev_ay = ay;
        g_prev_az = az;
        g_imu_sample_ready = true;
        return false;
    }
    const float dx = ax - g_prev_ax;
    const float dy = ay - g_prev_ay;
    const float dz = az - g_prev_az;
    g_prev_ax = ax;
    g_prev_ay = ay;
    g_prev_az = az;
    const bool shaking = (dx * dx + dy * dy + dz * dz) > 0.22f;
    const bool edge = shaking && !g_was_shaking;
    g_was_shaking = shaking;
    return edge;
}

static void selectMode(const GameMode mode) {
    leaveModeApp(g_mode);
    if (mode != GameMode::HUB) {
        // 离开 hub 后为全屏游戏；hub 重进时 beginAppHubScreen 会再 opt-in
        clearAppHeaderStatusRefresh();
    }
    if (mode == GameMode::HUB) {
        if (g_canvas_ok) {
            gamesCanvas.deleteSprite();
            g_canvas_ok = false;
        }
    } else if (isExternalMode(mode)) {
        if (g_canvas_ok) {
            gamesCanvas.deleteSprite();
            g_canvas_ok = false;
        }
    } else if (!ensureCanvas()) {
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Games canvas OOM");
        return;
    }

    g_mode = mode;
    g_help = false;
    g_last_ms = millis();
    if (mode == GameMode::COIN) {
        resetCoin();
    } else if (mode == GameMode::DOUBLE_PENDULUM) {
        resetPendulum(false);
    } else if (mode == GameMode::WHEEL) {
        resetWheelState();
    } else if (mode == GameMode::CURVES) {
        resetCurves();
    } else if (mode == GameMode::DICE) {
        enterDiceApp();
    } else if (mode == GameMode::NEWTON_CRADLE) {
        enterNewtonCradleApp(true);
    } else if (mode == GameMode::NEON_FX) {
        enterNeonFxApp();
    } else if (mode == GameMode::MINESWEEPER) {
        enterMinesweeperApp();
    } else if (mode == GameMode::SNAKE) {
        enterSnakeApp();
    } else if (mode == GameMode::LIFE) {
        enterLifeApp();
    } else if (mode == GameMode::MATRIX_RAIN) {
        enterMatrixRainApp();
    } else if (mode == GameMode::BEZIER_WAVE) {
        enterBezierWaveApp();
    } else if (mode == GameMode::PARTICLE_CLOCK) {
        enterParticleClockApp();
    } else if (mode == GameMode::LISSAJOUS) {
        enterLissajousApp();
    }
    drawCurrent();
}

} // namespace

void enterGamesApp() {
    leaveGamesApp();
    g_width = M5Cardputer.Display.width();
    g_height = M5Cardputer.Display.height();
    g_mode = GameMode::HUB;
    g_hub_page = 0;
    g_help = false;
    g_last_ms = millis();
    g_imu_sample_ready = false;
    g_was_shaking = false;
    g_ax = 0.0f;
    g_ay = 0.0f;
    g_az = 1.0f;
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5.Imu.update();
    g_imu_ok = M5.Imu.isEnabled();
    showGamesHubScreen();
}

void leaveGamesApp() {
    leaveModeApp(g_mode);
    if (g_canvas_ok) {
        gamesCanvas.deleteSprite();
        g_canvas_ok = false;
    }
    g_mode = GameMode::HUB;
    g_help = false;
}

void updateGamesApp() {
    if (g_help || g_mode == GameMode::HUB) {
        return;
    }
    if (isExternalMode(g_mode)) {
        drawExternalFrame();
        return;
    }
    if (!g_canvas_ok) {
        return;
    }
    const uint32_t now = millis();
    float dt = (now - g_last_ms) * 0.001f;
    g_last_ms = now;
    dt = constrain(dt, 0.001f, 0.033f);
    const bool shake_edge = sampleImu();

    if (g_mode == GameMode::COIN) {
        if (shake_edge && !g_coin_tossing) {
            tossCoin();
        }
        stepCoin(dt);
    } else if (g_mode == GameMode::DOUBLE_PENDULUM) {
        constexpr int substeps = 5;
        for (int i = 0; i < substeps; ++i) {
            stepPendulum(dt / substeps);
        }
        addPendulumTrace(dt);
    } else if (g_mode == GameMode::WHEEL) {
        updateWheelSpaceSpin(now);
        if (shake_edge && !g_wheel_spinning && !g_wheel_space_held) {
            spinWheel(0.55f);
        }
        stepWheel(dt);
        if (!g_wheel_spinning && g_wheel_result >= 0) {
            if (g_wheel_result_reveal < 1.0f) {
                g_wheel_result_reveal = fminf(1.0f, g_wheel_result_reveal + dt * 2.4f);
            }
        } else if (g_wheel_spinning) {
            g_wheel_result_reveal = 0.0f;
        }
    } else if (g_mode == GameMode::CURVES) {
        stepCurves(dt);
    }
    drawCurrent();
}

// BtnA 边沿触发（与空格同效的单次动作）
static void triggerGamesBtnAAction() {
    if (g_help) {
        return;
    }
    switch (g_mode) {
        case GameMode::COIN:
            if (!g_coin_tossing) {
                tossCoin();
            }
            break;
        case GameMode::DOUBLE_PENDULUM:
            resetPendulum(false);
            break;
        case GameMode::CURVES:
            g_curve_animate = !g_curve_animate;
            break;
        default:
            break;
    }
}

void pollGamesBtnA() {
    if (g_help || g_mode == GameMode::HUB) {
        return;
    }
    if (g_mode == GameMode::NEWTON_CRADLE) {
        pollNewtonCradleBtnA();
        return;
    }
    if (g_mode == GameMode::NEON_FX) {
        pollNeonFxBtnA();
        return;
    }
    if (g_mode == GameMode::MINESWEEPER) {
        pollMinesweeperBtnA();
        return;
    }
    if (g_mode == GameMode::SNAKE) {
        pollSnakeBtnA();
        return;
    }
    if (g_mode == GameMode::LIFE) {
        pollLifeBtnA();
        return;
    }
    if (g_mode == GameMode::MATRIX_RAIN) {
        pollMatrixRainBtnA();
        return;
    }
    if (g_mode == GameMode::BEZIER_WAVE) {
        pollBezierWaveBtnA();
        return;
    }
    if (g_mode == GameMode::PARTICLE_CLOCK) {
        pollParticleClockBtnA();
        return;
    }
    if (g_mode == GameMode::LISSAJOUS) {
        pollLissajousBtnA();
        return;
    }
    // DICE / WHEEL 蓄力在 update 里轮询 isPressed
    if (M5Cardputer.BtnA.wasPressed()) {
        triggerGamesBtnAAction();
    }
}

void handleGamesApp(const Keyboard_Class::KeysState& status) {
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'h') {
            g_help = !g_help;
            g_wheel_space_held = false;
            g_last_ms = millis();
            if (!g_help && g_mode == GameMode::HUB) {
                showGamesHubScreen();
                return;
            }
            drawCurrent();
            return;
        }
        if (g_help) {
            continue;
        }
        if (g_mode == GameMode::HUB && c >= '1' && c <= '8') {
            const int item = g_hub_page * GAMES_HUB_ITEMS_PER_PAGE + (c - '1');
            if (item < GAMES_HUB_ITEM_COUNT) {
                selectMode(GAMES_HUB_ITEMS[item].mode);
                return;
            }
        }
        if (c == ' ' && g_mode == GameMode::COIN && !g_coin_tossing) {
            tossCoin();
        } else if (c == ' ' && g_mode == GameMode::DOUBLE_PENDULUM) {
            resetPendulum(false);
        } else if (c == 'r' && g_mode == GameMode::DOUBLE_PENDULUM) {
            resetPendulum(true);
        } else if (c == '-' && g_mode == GameMode::DOUBLE_PENDULUM) {
            changePendulumL2(-PEND_L2_PX_STEP);
        } else if ((c == '=' || c == '+') && g_mode == GameMode::DOUBLE_PENDULUM) {
            changePendulumL2(PEND_L2_PX_STEP);
        } else if ((c == '-' || c == ',') && g_mode == GameMode::WHEEL) {
            changeWheelSegments(-1);
        } else if ((c == '=' || c == '+' || c == '.') && g_mode == GameMode::WHEEL) {
            changeWheelSegments(1);
        } else if (g_mode == GameMode::CURVES) {
            if (c >= '1' && c <= '9') {
                g_curve_id = c - '1';
            } else if (c == '-') {
                g_curve_amp = fmaxf(0.2f, g_curve_amp - 0.1f);
            } else if (c == '=' || c == '+') {
                g_curve_amp = fminf(2.5f, g_curve_amp + 0.1f);
            } else if (c == ',') {
                g_curve_freq = fmaxf(0.2f, g_curve_freq - 0.1f);
            } else if (c == '.') {
                g_curve_freq = fminf(4.0f, g_curve_freq + 0.1f);
            } else if (c == 'q') {
                g_curve_phase = wrapAngle(g_curve_phase - 0.2f);
            } else if (c == 'e') {
                g_curve_phase = wrapAngle(g_curve_phase + 0.2f);
            } else if (c == ' ') {
                g_curve_animate = !g_curve_animate;
            } else if (c == 'r') {
                g_curve_amp = 1.0f;
                g_curve_freq = 1.0f;
                g_curve_phase = 0.0f;
                g_curve_animate = true;
            }
        }
    }
    if (g_mode == GameMode::HUB) {
        int delta = getMenuNavDelta(status);
        if (delta == 0) {
            delta = getBracketNavDelta(status);
        }
        const int page_count = getGamesHubPageCount();
        if (delta != 0 && page_count > 1) {
            g_hub_page = (g_hub_page + delta + page_count) % page_count;
            showGamesHubScreen();
        }
        return;
    }
    if (g_mode == GameMode::DICE) {
        handleDiceApp(status);
    } else if (g_mode == GameMode::NEWTON_CRADLE) {
        handleNewtonCradleApp(status);
    } else if (g_mode == GameMode::NEON_FX) {
        handleNeonFxApp(status);
    } else if (g_mode == GameMode::MINESWEEPER) {
        handleMinesweeperApp(status);
    } else if (g_mode == GameMode::SNAKE) {
        handleSnakeApp(status);
    } else if (g_mode == GameMode::LIFE) {
        handleLifeApp(status);
    } else if (g_mode == GameMode::MATRIX_RAIN) {
        handleMatrixRainApp(status);
    } else if (g_mode == GameMode::BEZIER_WAVE) {
        handleBezierWaveApp(status);
    } else if (g_mode == GameMode::PARTICLE_CLOCK) {
        handleParticleClockApp(status);
    } else if (g_mode == GameMode::LISSAJOUS) {
        handleLissajousApp(status);
    }
    drawCurrent();
}

bool handleGamesBack() {
    if (g_mode == GameMode::HUB) {
        return false;
    }
    selectMode(GameMode::HUB);
    return true;
}

// 关闭 Help 并重绘当前界面
bool closeGamesHelp() {
    if (!g_help) {
        return false;
    }
    g_help = false;
    if (g_mode == GameMode::HUB) {
        showGamesHubScreen();
    } else {
        drawCurrent();
    }
    return true;
}

bool isGamesHelpVisible() {
    return g_help;
}
