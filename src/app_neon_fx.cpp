#include "app_neon_fx.h"
#include "app_header.h"
#include "app_common.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

static constexpr int FX_PATTERN_COUNT = 4;
static constexpr int FX_THEME_COUNT = 4;
static constexpr int FX_PALETTE_COLORS = 254;
static constexpr int FX_CUBE_PATTERN = 3;

static M5Canvas fxCanvas(&M5Cardputer.Display);
static bool fxCanvasOk = false;
static bool g_help_visible = false;
static int fxWidth = 0;
static int fxHeight = 0;
static int fxCenterX = 0;
static int fxCenterY = 0;
static int fxPattern = 0;
static int fxTheme = 0;
static int fxSpeed = 3;
static int fxDirection = 1;
static uint32_t fxPalette[FX_THEME_COUNT][FX_PALETTE_COLORS];
static bool fxPaletteReady = false;
static uint32_t fxStartMs = 0;
static uint32_t fxPulseMs = 0;
static uint32_t fxFpsWindowMs = 0;
static uint32_t fxFrameCount = 0;
static uint16_t fxFps = 0;
// 立方体额外姿态（EASD 微调）
static float fxCubeYawBias = 0.0f;
static float fxCubePitchBias = 0.0f;

static const char* const FX_PATTERN_NAMES[FX_PATTERN_COUNT] = {
    "VORTEX",
    "PLASMA",
    "TUNNEL",
    "CUBE",
};

// 单位立方体 8 顶点
static const float CUBE_VERTS[8][3] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

// 6 面：每面 4 顶点索引（逆时针）
static const uint8_t CUBE_FACES[6][4] = {
    {0, 1, 2, 3}, // back  z-
    {4, 7, 6, 5}, // front z+
    {0, 4, 5, 1}, // bottom y-
    {3, 2, 6, 7}, // top y+
    {0, 3, 7, 4}, // left x-
    {1, 5, 6, 2}, // right x+
};

// 整数 HSV 转 RGB888，调色板只在进入 App 时生成一次。
static uint32_t fxHsv(const uint16_t hue, const uint8_t saturation, const uint8_t value) {
    const uint8_t region = (hue / 60) % 6;
    const uint16_t remainder = (hue % 60) * 255 / 60;
    const uint8_t p = static_cast<uint16_t>(value) * (255 - saturation) / 255;
    const uint8_t q =
        static_cast<uint16_t>(value) * (255 - static_cast<uint16_t>(saturation) * remainder / 255) /
        255;
    const uint8_t t = static_cast<uint16_t>(value) *
                      (255 - static_cast<uint16_t>(saturation) * (255 - remainder) / 255) / 255;

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    switch (region) {
        case 0:
            r = value;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = value;
            b = p;
            break;
        case 2:
            r = p;
            g = value;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = value;
            break;
        case 4:
            r = t;
            g = p;
            b = value;
            break;
        default:
            r = value;
            g = p;
            b = q;
            break;
    }
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

static void fxBuildPalettes() {
    if (fxPaletteReady) {
        return;
    }

    for (int i = 0; i < FX_PALETTE_COLORS; ++i) {
        const uint16_t wheel = static_cast<uint16_t>(i * 360 / FX_PALETTE_COLORS);
        const uint8_t wave = static_cast<uint8_t>(128 + 127 * sinf(i * 6.2831853f / 254.0f));
        fxPalette[0][i] = fxHsv((wheel + 185) % 360, 245, static_cast<uint8_t>(110 + wave / 2));
        fxPalette[1][i] =
            fxHsv(static_cast<uint16_t>(285 + (i * 95 / FX_PALETTE_COLORS)) % 360, 250,
                  static_cast<uint8_t>(95 + wave * 5 / 8));
        fxPalette[2][i] =
            fxHsv(static_cast<uint16_t>(18 + (i * 55 / FX_PALETTE_COLORS)), 255,
                  static_cast<uint8_t>(80 + wave * 11 / 16));
        fxPalette[3][i] =
            fxHsv(static_cast<uint16_t>(125 + (i * 105 / FX_PALETTE_COLORS)), 235,
                  static_cast<uint8_t>(85 + wave * 2 / 3));
    }
    fxPaletteReady = true;
}

// 形态切换或移动核心时才重建索引场；正常动画帧不做逐像素运算。
static void fxBuildField() {
    if (!fxCanvasOk || fxPattern == FX_CUBE_PATTERN) {
        return;
    }
    auto* pixels = static_cast<uint8_t*>(fxCanvas.getBuffer());
    if (pixels == nullptr) {
        return;
    }

    for (int y = 0; y < fxHeight; ++y) {
        for (int x = 0; x < fxWidth; ++x) {
            const float dx = static_cast<float>(x - fxCenterX);
            const float dy = static_cast<float>(y - fxCenterY);
            const float radius = sqrtf(dx * dx + dy * dy) + 0.001f;
            const float angle = atan2f(dy, dx);
            // 用 sin/cos(angle) 代替 angle*k，避免 atan2 在左侧负 X 轴的 π/-π 割缝
            const float sa = sinf(angle);
            const float ca = cosf(angle);
            float value = 0.0f;

            if (fxPattern == 0) {
                value = radius * 2.8f + sinf(angle * 3.0f + radius * 0.18f) * 72.0f +
                        cosf(angle * 2.0f - radius * 0.11f) * 48.0f + sa * ca * 18.0f;
            } else if (fxPattern == 1) {
                value = (sinf((x + fxCenterX) * 0.085f) +
                         sinf((y - fxCenterY) * 0.145f) +
                         sinf((x + y) * 0.055f) + sinf(radius * 0.12f)) *
                        51.0f;
            } else {
                value = 1280.0f / (radius + 7.0f) + sinf(angle * 4.0f) * 42.0f +
                        cosf(angle * 3.0f + radius * 0.08f) * 34.0f + radius * 1.7f;
            }

            int color = static_cast<int>(value);
            color %= FX_PALETTE_COLORS;
            if (color < 0) {
                color += FX_PALETTE_COLORS;
            }
            pixels[y * fxWidth + x] = static_cast<uint8_t>(color + 1);
        }
    }
}

static uint32_t fxBoostColor(const uint32_t color, const uint8_t boost) {
    const uint8_t r = static_cast<uint8_t>(color >> 16);
    const uint8_t g = static_cast<uint8_t>(color >> 8);
    const uint8_t b = static_cast<uint8_t>(color);
    const uint8_t rr = static_cast<uint8_t>(r + ((255 - r) * boost >> 8));
    const uint8_t gg = static_cast<uint8_t>(g + ((255 - g) * boost >> 8));
    const uint8_t bb = static_cast<uint8_t>(b + ((255 - b) * boost >> 8));
    return (static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(gg) << 8) | bb;
}

static void fxApplyPalette(const uint32_t now) {
    const uint32_t elapsed = now - fxStartMs;
    int phase = static_cast<int>((elapsed * fxSpeed / 9) % FX_PALETTE_COLORS);
    if (fxDirection < 0) {
        phase = FX_PALETTE_COLORS - 1 - phase;
    }

    uint8_t boost = 0;
    const uint32_t pulseAge = now - fxPulseMs;
    if (fxPulseMs != 0 && pulseAge < 420) {
        boost = static_cast<uint8_t>((420 - pulseAge) * 220 / 420);
        phase = (phase + static_cast<int>((420 - pulseAge) / 3)) % FX_PALETTE_COLORS;
    }

    fxCanvas.setPaletteColor(0, 0, 0, 0);
    fxCanvas.setPaletteColor(255, 255, 255, 255);
    for (int i = 0; i < FX_PALETTE_COLORS; ++i) {
        const int source = (i + phase) % FX_PALETTE_COLORS;
        const uint32_t color = fxBoostColor(fxPalette[fxTheme][source], boost);
        fxCanvas.setPaletteColor(static_cast<size_t>(i + 1), color);
    }
}

// 立方体：固定 6 面色 + 边线白
static void fxApplyCubePalette(const uint32_t now) {
    uint8_t boost = 0;
    const uint32_t pulseAge = now - fxPulseMs;
    if (fxPulseMs != 0 && pulseAge < 420) {
        boost = static_cast<uint8_t>((420 - pulseAge) * 220 / 420);
    }

    // 6 面取当前主题上均匀取样的霓虹色
    for (int f = 0; f < 6; ++f) {
        const int src = (f * FX_PALETTE_COLORS / 6 + fxTheme * 37) % FX_PALETTE_COLORS;
        const uint32_t color = fxBoostColor(fxPalette[fxTheme][src], boost);
        fxCanvas.setPaletteColor(static_cast<size_t>(f + 1), color);
    }
    fxCanvas.setPaletteColor(0, 0, 0, 0);
    fxCanvas.setPaletteColor(7, fxBoostColor(0xE8E8E8, boost)); // 边线
    fxCanvas.setPaletteColor(255, 255, 255, 255);
}

// 软渲染旋转立方体（画家算法填面 + 线框）
static void fxRenderCube(const uint32_t now) {
    fxCanvas.fillSprite(0);

    const float t = (now - fxStartMs) * 0.001f * fxSpeed * 0.55f * static_cast<float>(fxDirection);
    const float yaw = t * 1.15f + fxCubeYawBias;
    const float pitch = t * 0.82f + fxCubePitchBias;
    const float roll = t * 0.47f;

    const float cy = cosf(yaw);
    const float sy = sinf(yaw);
    const float cp = cosf(pitch);
    const float sp = sinf(pitch);
    const float cr = cosf(roll);
    const float sr = sinf(roll);

    float scale = 38.0f;
    if (fxPulseMs != 0) {
        const uint32_t pulseAge = now - fxPulseMs;
        if (pulseAge < 420) {
            scale += (420 - pulseAge) * 0.045f;
        }
    }

    float sx[8];
    float sy2[8];
    float sz[8];

    for (int i = 0; i < 8; ++i) {
        float x = CUBE_VERTS[i][0];
        float y = CUBE_VERTS[i][1];
        float z = CUBE_VERTS[i][2];

        // Rx * Ry * Rz
        float x1 = x * cr - y * sr;
        float y1 = x * sr + y * cr;
        float z1 = z;

        float x2 = x1 * cy + z1 * sy;
        float z2 = -x1 * sy + z1 * cy;
        float y2 = y1;

        float y3 = y2 * cp - z2 * sp;
        float z3 = y2 * sp + z2 * cp;
        float x3 = x2;

        // 简易透视
        const float depth = z3 + 4.2f;
        const float persp = scale / depth;
        sx[i] = fxWidth * 0.5f + x3 * persp;
        sy2[i] = fxHeight * 0.5f + y3 * persp;
        sz[i] = z3;
    }

    // 按面平均深度从远到近排序
    int order[6] = {0, 1, 2, 3, 4, 5};
    float face_z[6];
    for (int f = 0; f < 6; ++f) {
        face_z[f] = (sz[CUBE_FACES[f][0]] + sz[CUBE_FACES[f][1]] + sz[CUBE_FACES[f][2]] +
                     sz[CUBE_FACES[f][3]]) *
                    0.25f;
    }
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 6; ++j) {
            if (face_z[order[i]] > face_z[order[j]]) {
                const int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    for (int oi = 0; oi < 6; ++oi) {
        const int f = order[oi];
        const uint8_t* idx = CUBE_FACES[f];
        const int x0 = static_cast<int>(sx[idx[0]]);
        const int y0 = static_cast<int>(sy2[idx[0]]);
        const int x1 = static_cast<int>(sx[idx[1]]);
        const int y1 = static_cast<int>(sy2[idx[1]]);
        const int x2 = static_cast<int>(sx[idx[2]]);
        const int y2 = static_cast<int>(sy2[idx[2]]);
        const int x3 = static_cast<int>(sx[idx[3]]);
        const int y3 = static_cast<int>(sy2[idx[3]]);
        const uint8_t face_color = static_cast<uint8_t>(f + 1);

        // 背面剔除：屏幕空间叉积
        const int ax = x1 - x0;
        const int ay = y1 - y0;
        const int bx = x3 - x0;
        const int by = y3 - y0;
        if (ax * by - ay * bx <= 0) {
            continue;
        }

        fxCanvas.fillTriangle(x0, y0, x1, y1, x2, y2, face_color);
        fxCanvas.fillTriangle(x0, y0, x2, y2, x3, y3, face_color);
        fxCanvas.drawLine(x0, y0, x1, y1, 7);
        fxCanvas.drawLine(x1, y1, x2, y2, 7);
        fxCanvas.drawLine(x2, y2, x3, y3, 7);
        fxCanvas.drawLine(x3, y3, x0, y0, 7);
    }
}

static void fxDrawFpsOverlay() {
    char fps_line[12];
    snprintf(fps_line, sizeof(fps_line), "%u", fxFps);
    constexpr int fps_pad_w = 26;
    constexpr int fps_pad_h = 10;
    M5Cardputer.Display.fillRect(0, 0, fps_pad_w, fps_pad_h, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.setCursor(2, 1);
    M5Cardputer.Display.print(fps_line);
}

static void fxMoveCore(const int dx, const int dy) {
    if (fxPattern == FX_CUBE_PATTERN) {
        // 立方体：EASD 微调姿态
        fxCubeYawBias += dx * 0.08f;
        fxCubePitchBias += dy * 0.08f;
        return;
    }
    fxCenterX = constrain(fxCenterX + dx, 12, fxWidth - 13);
    fxCenterY = constrain(fxCenterY + dy, 10, fxHeight - 11);
    fxBuildField();
}

static void fxOnPatternChanged() {
    if (fxPattern == FX_CUBE_PATTERN) {
        return;
    }
    fxBuildField();
}

// ===== Help 页 =====

static int drawFxHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

static int drawFxHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawFxHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawFxHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static void drawNeonFxHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawFxHelpColHeader(0, col_y, col_w, "keymap");
    y = drawFxHelpBadge(2, y, "EASD", "move/orbit");
    y = drawFxHelpKey(2, y, 'c', "cycle theme");
    y = drawFxHelpKey(2, y, 'm', "cycle pattern");
    y = drawFxHelpBadge(2, y, "-=", "speed -/+");
    y = drawFxHelpKey(2, y, 'r', "reverse");
    y = drawFxHelpKey(2, y, 'h', "help / close");
    y = drawFxHelpBadge(2, y, "SPC", "pulse");

    y = drawFxHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawFxHelpText(manual_x + 2, y, "Neon + soft 3D");
    char fps_line[28];
    snprintf(fps_line, sizeof(fps_line), "FPS %u  %s x%d", fxFps,
             FX_PATTERN_NAMES[fxPattern], fxSpeed);
    y = drawFxHelpText(manual_x + 2, y, fps_line);
    y = drawFxHelpText(manual_x + 2, y, "4 patterns x 4 themes");
    y = drawFxHelpText(manual_x + 2, y, "CUBE = filled cube");
    y = drawFxHelpText(manual_x + 2, y, "space = pulse flash");
    y = drawFxHelpText(manual_x + 2, y, "BtnGO back to menu");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

} // namespace

void enterNeonFxApp() {
    leaveNeonFxApp();
    // 全屏：无 header / tip，画布覆盖整屏
    M5Cardputer.Display.clear();
    g_help_visible = false;

    fxWidth = M5Cardputer.Display.width();
    fxHeight = M5Cardputer.Display.height();
    fxCenterX = fxWidth / 2;
    fxCenterY = fxHeight / 2;
    fxPattern = 0;
    fxTheme = 0;
    fxSpeed = 3;
    fxDirection = 1;
    fxPulseMs = 0;
    fxCubeYawBias = 0.0f;
    fxCubePitchBias = 0.0f;
    fxFps = 0;
    fxFrameCount = 0;
    fxStartMs = millis();
    fxFpsWindowMs = fxStartMs;

    // 8-bit 索引画布约 32 KB（全屏 240x135）；比 16-bit 双缓冲节省一半内存。
    fxCanvas.setColorDepth(8);
    if (!fxCanvas.createSprite(fxWidth, fxHeight)) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 0);
        M5Cardputer.Display.printf("Canvas OOM (max %u)", ESP.getMaxAllocHeap());
        return;
    }
    if (!fxCanvas.createPalette()) {
        fxCanvas.deleteSprite();
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 0);
        M5Cardputer.Display.print("Palette OOM");
        return;
    }
    fxCanvasOk = true;
    fxBuildPalettes();
    fxBuildField();
    fxApplyPalette(fxStartMs);
    fxCanvas.pushSprite(0, 0);
}

void leaveNeonFxApp() {
    g_help_visible = false;
    if (fxCanvasOk) {
        fxCanvas.deleteSprite();
        fxCanvasOk = false;
    }
}

bool isNeonFxHelpVisible() {
    return g_help_visible;
}

void updateNeonFxApp() {
    if (!fxCanvasOk) {
        return;
    }
    // Help 页可见时不刷动画，避免覆盖 help 页并节省 CPU
    if (g_help_visible) {
        return;
    }

    const uint32_t now = millis();
    if (fxPattern == FX_CUBE_PATTERN) {
        fxApplyCubePalette(now);
        fxRenderCube(now);
    } else {
        fxApplyPalette(now);
    }

    ++fxFrameCount;
    const uint32_t fpsElapsed = now - fxFpsWindowMs;
    if (fpsElapsed >= 500) {
        fxFps = static_cast<uint16_t>(fxFrameCount * 1000U / fpsElapsed);
        fxFrameCount = 0;
        fxFpsWindowMs = now;
    }
    fxCanvas.pushSprite(0, 0);
    fxDrawFpsOverlay();
}

void handleNeonFxApp(const Keyboard_Class::KeysState& status) {
    // h 切换 help 页（在 help 页可见时也仅响应 h 关闭）
    bool has_h = false;
    for (char c : status.word) {
        if (c == 'h' || c == 'H') {
            has_h = true;
            break;
        }
    }
    if (has_h) {
        g_help_visible = !g_help_visible;
        if (g_help_visible) {
            drawNeonFxHelpPage();
        } else {
            // 关闭 help：直接 push 当前画布恢复全屏 FX
            fxCanvas.pushSprite(0, 0);
            // 重置 FPS 计数窗口，避免 help 期间计入长间隔
            fxFpsWindowMs = millis();
            fxFrameCount = 0;
        }
        return;
    }
    if (g_help_visible) {
        return;
    }
    for (char c : status.word) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        // Cardputer 键盘是整齐网格，s 正上方是 e 不是 w，所以上键取 e
        if (c == 'e') {
            fxMoveCore(0, -8);
        } else if (c == 'a') {
            fxMoveCore(-10, 0);
        } else if (c == 's') {
            fxMoveCore(0, 8);
        } else if (c == 'd') {
            fxMoveCore(10, 0);
        } else if (c == 'c') {
            fxTheme = (fxTheme + 1) % FX_THEME_COUNT;
        } else if (c == 'm' || c == ',' || c == '.') {
            fxPattern = (fxPattern + 1) % FX_PATTERN_COUNT;
            fxOnPatternChanged();
        } else if (c == '-' && fxSpeed > 1) {
            --fxSpeed;
        } else if ((c == '=' || c == '+') && fxSpeed < 8) {
            ++fxSpeed;
        } else if (c == 'r') {
            fxDirection = -fxDirection;
        } else if (c == ' ') {
            fxPulseMs = millis();
        }
    }
}

void pollNeonFxBtnA() {
    if (g_help_visible) {
        return;
    }
    if (M5Cardputer.BtnA.wasPressed()) {
        fxPulseMs = millis();
    }
}
