#include "app_newton_cradle.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

static constexpr int BALL_COUNT = 5;
static constexpr float PI_F = 3.14159265f;
static constexpr float PENDULUM_LENGTH = 68.0f;
static constexpr float BALL_RADIUS = 12.0f;
static constexpr float BALL_DIAMETER = BALL_RADIUS * 2.0f;
static constexpr float PIVOT_SPACING = BALL_DIAMETER;
static constexpr float GRAVITY_PX = 455.0f;
static constexpr float AIR_DAMPING = 0.028f;
static constexpr float COLLISION_RESTITUTION = 0.992f;

struct Pendulum {
    float angle;
    float angular_velocity;
};

struct BobState {
    float x;
    float y;
    float vx;
    float vy;
    float tx;
    float ty;
};

static M5Canvas cradleCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static int g_width = 0;
static int g_height = 0;
static int g_launch_count = 1;
static uint32_t g_last_ms = 0;
static Pendulum g_balls[BALL_COUNT];
static bool g_embedded = false;

static float pivotX(const int index) {
    return g_width * 0.5f + (index - (BALL_COUNT - 1) * 0.5f) * PIVOT_SPACING;
}

static void getBobState(const int index, BobState& state) {
    const float angle = g_balls[index].angle;
    const float omega = g_balls[index].angular_velocity;
    state.tx = cosf(angle);
    state.ty = -sinf(angle);
    state.x = pivotX(index) + PENDULUM_LENGTH * sinf(angle);
    state.y = 27.0f + PENDULUM_LENGTH * cosf(angle);
    state.vx = PENDULUM_LENGTH * omega * state.tx;
    state.vy = PENDULUM_LENGTH * omega * state.ty;
}

static void applyPalette() {
    cradleCanvas.setPaletteColor(0, 0x0B, 0x0D, 0x11);
    cradleCanvas.setPaletteColor(1, 0x12, 0x16, 0x1C);
    cradleCanvas.setPaletteColor(2, 0x18, 0x1D, 0x24);
    cradleCanvas.setPaletteColor(3, 0x05, 0x06, 0x08);
    cradleCanvas.setPaletteColor(4, 0x28, 0x12, 0x0A);
    cradleCanvas.setPaletteColor(5, 0x55, 0x28, 0x12);
    cradleCanvas.setPaletteColor(6, 0x8B, 0x4A, 0x24);
    cradleCanvas.setPaletteColor(7, 0xC1, 0x78, 0x3D);
    cradleCanvas.setPaletteColor(8, 0x22, 0x27, 0x2D);
    cradleCanvas.setPaletteColor(9, 0x56, 0x60, 0x69);
    cradleCanvas.setPaletteColor(10, 0xA7, 0xB0, 0xB8);
    cradleCanvas.setPaletteColor(11, 0xE6, 0xEB, 0xEF);
    cradleCanvas.setPaletteColor(12, 0xD8, 0xC9, 0x9D);
    cradleCanvas.setPaletteColor(13, 0x7D, 0x83, 0x89);
    cradleCanvas.setPaletteColor(14, 0x14, 0x17, 0x1B);
    cradleCanvas.setPaletteColor(15, 0x20, 0x25, 0x2B);

    // 钢球由暗到亮的十级反射色
    static const uint32_t steel[10] = {
        0x20252A, 0x30373D, 0x465059, 0x5D6972, 0x77838C,
        0x929DA5, 0xADB6BC, 0xC8CFD3, 0xE2E7E9, 0xFAFCFC,
    };
    for (int i = 0; i < 10; ++i) {
        const uint32_t color = steel[i];
        cradleCanvas.setPaletteColor(
            static_cast<size_t>(16 + i), static_cast<uint8_t>(color >> 16),
            static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color));
    }
}

static bool ensureCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    cradleCanvas.setColorDepth(8);
    if (!cradleCanvas.createSprite(g_width, g_height)) {
        return false;
    }
    if (!cradleCanvas.createPalette()) {
        cradleCanvas.deleteSprite();
        return false;
    }
    g_canvas_ok = true;
    applyPalette();
    return true;
}

static void launchBalls(const int count) {
    g_launch_count = count;
    for (int i = 0; i < BALL_COUNT; ++i) {
        g_balls[i].angle = (i < count) ? -0.78f : 0.0f;
        g_balls[i].angular_velocity = 0.0f;
    }
}

static void solveContact(const int left, const int right) {
    BobState a;
    BobState b;
    getBobState(left, a);
    getBobState(right, b);

    float nx = b.x - a.x;
    float ny = b.y - a.y;
    const float dist_sq = nx * nx + ny * ny;
    const float contact_dist = BALL_DIAMETER + 0.22f;
    if (dist_sq > contact_dist * contact_dist || dist_sq < 0.001f) {
        return;
    }

    const float dist = sqrtf(dist_sq);
    nx /= dist;
    ny /= dist;
    const float tangent_a = a.tx * nx + a.ty * ny;
    const float tangent_b = b.tx * nx + b.ty * ny;
    const float closing = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;

    // 约束摆的有效质量沿碰撞法线变化，冲量需投影回角速度
    const float effective = tangent_a * tangent_a + tangent_b * tangent_b;
    if (closing > 0.01f && effective > 0.02f) {
        const float impulse = (1.0f + COLLISION_RESTITUTION) * closing / effective;
        g_balls[left].angular_velocity -= impulse * tangent_a / PENDULUM_LENGTH;
        g_balls[right].angular_velocity += impulse * tangent_b / PENDULUM_LENGTH;
    }

    // 消除数值穿透，避免钢球视觉上互相嵌入
    const float overlap = BALL_DIAMETER - dist;
    if (overlap > 0.0f) {
        const float correction = overlap * 0.51f / PENDULUM_LENGTH;
        g_balls[left].angle -= correction * tangent_a;
        g_balls[right].angle += correction * tangent_b;
    }
}

static void stepPhysics(const float dt) {
    for (int i = 0; i < BALL_COUNT; ++i) {
        Pendulum& ball = g_balls[i];
        const float angular_accel = -(GRAVITY_PX / PENDULUM_LENGTH) * sinf(ball.angle);
        ball.angular_velocity += angular_accel * dt;
        ball.angular_velocity *= expf(-AIR_DAMPING * dt);
        ball.angle += ball.angular_velocity * dt;
    }

    // 多次顺逆序求解，让动量能在同一子步穿过整列钢球
    for (int iteration = 0; iteration < 7; ++iteration) {
        if ((iteration & 1) == 0) {
            for (int i = 0; i < BALL_COUNT - 1; ++i) {
                solveContact(i, i + 1);
            }
        } else {
            for (int i = BALL_COUNT - 2; i >= 0; --i) {
                solveContact(i, i + 1);
            }
        }
    }
}

static void drawBackground() {
    cradleCanvas.fillSprite(0);
    for (int y = 0; y < g_height; ++y) {
        if (y > 38) {
            cradleCanvas.drawFastHLine(0, y, g_width, (y < 93) ? 1 : 2);
        }
    }

    // 固定颗粒模拟低照度工作台墙面
    for (int y = 5; y < 106; y += 9) {
        for (int x = (y * 5) % 13; x < g_width; x += 19) {
            if (((x + y * 3) & 7) < 2) {
                cradleCanvas.drawPixel(x, y, 15);
            }
        }
    }
    cradleCanvas.fillRect(0, 113, g_width, g_height - 113, 3);
    cradleCanvas.drawFastHLine(0, 113, g_width, 8);
}

static void drawFrameBack() {
    // 胡桃木底座及金属支架
    cradleCanvas.fillRoundRect(14, 106, g_width - 28, 20, 5, 4);
    cradleCanvas.fillRoundRect(17, 108, g_width - 34, 15, 4, 5);
    cradleCanvas.drawFastHLine(22, 110, g_width - 44, 7);
    cradleCanvas.drawFastHLine(22, 120, g_width - 44, 4);

    cradleCanvas.fillRoundRect(22, 15, 8, 95, 3, 8);
    cradleCanvas.fillRoundRect(g_width - 30, 15, 8, 95, 3, 8);
    cradleCanvas.drawFastVLine(24, 20, 87, 9);
    cradleCanvas.drawFastVLine(g_width - 28, 20, 87, 9);

    cradleCanvas.fillRoundRect(17, 12, g_width - 34, 12, 4, 8);
    cradleCanvas.fillRoundRect(20, 14, g_width - 40, 6, 3, 9);
    cradleCanvas.drawFastHLine(24, 15, g_width - 48, 10);
}

static void drawBallShadow(const BobState& state) {
    const int shadow_x = static_cast<int>(state.x + 5.0f);
    const int distance = static_cast<int>(116.0f - state.y);
    const int radius = (distance > 20) ? 8 : 11;
    cradleCanvas.fillEllipse(shadow_x, 115, radius + 3, 3, 14);
    cradleCanvas.fillEllipse(shadow_x, 115, radius, 2, 3);
}

static void drawSteelBall(const int cx, const int cy) {
    const int radius = static_cast<int>(BALL_RADIUS);
    cradleCanvas.fillCircle(cx, cy, radius + 1, 3);

    // 根据球面法线逐像素计算漫反射和高光
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float rr = static_cast<float>(dx * dx + dy * dy);
            if (rr > BALL_RADIUS * BALL_RADIUS) {
                continue;
            }
            const float nx = dx / BALL_RADIUS;
            const float ny = dy / BALL_RADIUS;
            const float nz = sqrtf(fmaxf(0.0f, 1.0f - nx * nx - ny * ny));
            const float diffuse = fmaxf(0.0f, nx * -0.42f + ny * -0.55f + nz * 0.72f);
            const float spec_axis = fmaxf(0.0f, nx * -0.34f + ny * -0.48f + nz * 0.81f);
            const float specular = powf(spec_axis, 18.0f);
            float brightness = 0.08f + diffuse * 0.64f + specular * 0.55f;
            // 下缘加入环境反射暗带，增强抛光金属质感
            if (ny > 0.56f && ny < 0.78f) {
                brightness *= 0.62f;
            }
            int shade = static_cast<int>(brightness * 9.0f);
            if (shade < 0) {
                shade = 0;
            } else if (shade > 9) {
                shade = 9;
            }
            cradleCanvas.drawPixel(cx + dx, cy + dy, static_cast<uint8_t>(16 + shade));
        }
    }
    cradleCanvas.drawCircle(cx, cy, radius, 13);
    cradleCanvas.fillCircle(cx - 5, cy - 6, 1, 25);
}

static void drawScene() {
    drawBackground();
    drawFrameBack();

    BobState states[BALL_COUNT];
    for (int i = 0; i < BALL_COUNT; ++i) {
        getBobState(i, states[i]);
        drawBallShadow(states[i]);
    }

    // 双线吊索先画，钢球覆盖线端
    for (int i = 0; i < BALL_COUNT; ++i) {
        const int px = static_cast<int>(pivotX(i));
        const int bx = static_cast<int>(states[i].x);
        const int by = static_cast<int>(states[i].y);
        cradleCanvas.drawLine(px - 3, 23, bx - 4, by, 8);
        cradleCanvas.drawLine(px + 3, 23, bx + 4, by, 8);
        cradleCanvas.drawLine(px - 2, 23, bx - 3, by, 10);
        cradleCanvas.drawLine(px + 2, 23, bx + 3, by, 10);
        cradleCanvas.fillCircle(px, 22, 2, 11);
    }

    for (int i = 0; i < BALL_COUNT; ++i) {
        drawSteelBall(static_cast<int>(states[i].x), static_cast<int>(states[i].y));
    }

    char status[20];
    snprintf(status, sizeof(status), "%d BALL%s", g_launch_count, (g_launch_count > 1) ? "S" : "");
    cradleCanvas.setTextSize(1);
    cradleCanvas.setTextColor(12);
    cradleCanvas.setCursor(4, 3);
    cradleCanvas.print("NEWTON CRADLE");
    cradleCanvas.setCursor(g_width - static_cast<int>(strlen(status)) * 6 - 4, 3);
    cradleCanvas.print(status);

    if (!g_embedded) {
        cradleCanvas.fillRect(0, 127, g_width, 8, 0);
        cradleCanvas.setTextColor(10);
        cradleCanvas.setCursor(4, 128);
        cradleCanvas.print("1/2/3 launch   SPC replay");
    }
}

} // namespace

void enterNewtonCradleApp(const bool embedded) {
    leaveNewtonCradleApp();
    g_embedded = embedded;
    g_width = M5Cardputer.Display.width();
    g_height = M5Cardputer.Display.height();
    g_last_ms = millis();
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();
    launchBalls(1);

    if (!ensureCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Cradle canvas OOM");
        return;
    }
    updateNewtonCradleApp();
}

void leaveNewtonCradleApp() {
    if (g_canvas_ok) {
        cradleCanvas.deleteSprite();
        g_canvas_ok = false;
    }
}

void updateNewtonCradleApp() {
    if (!g_canvas_ok) {
        return;
    }

    const uint32_t now = millis();
    float dt = (now - g_last_ms) * 0.001f;
    g_last_ms = now;
    if (dt < 0.001f) {
        dt = 0.001f;
    } else if (dt > 0.033f) {
        dt = 0.033f;
    }

    constexpr int substeps = 4;
    for (int i = 0; i < substeps; ++i) {
        stepPhysics(dt / substeps);
    }
    drawScene();
    cradleCanvas.pushSprite(0, 0);
}

void handleNewtonCradleApp(const Keyboard_Class::KeysState& status) {
    for (char c : status.word) {
        if (c >= '1' && c <= '3') {
            launchBalls(c - '0');
        } else if (c == ' ') {
            launchBalls(g_launch_count);
        } else if (c == 'r' || c == 'R') {
            launchBalls(1);
        }
    }
}

void pollNewtonCradleBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        launchBalls(g_launch_count);
    }
}
