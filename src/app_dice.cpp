#include "app_dice.h"
#include "app_header.h"
#include "app_common.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

static constexpr int DICE_MAX = 5;
static constexpr int DICE_MIN = 1;
static constexpr float REST = 0.48f;
static constexpr float LINEAR_DRAG = 1.45f; // 每秒衰减率，不能按帧直接相乘
static constexpr float ANGULAR_DRAG = 2.10f;
static constexpr float SETTLE_V = 0.16f;
static constexpr float GRAVITY = 18.0f;
static constexpr float FLOOR_BOUNCE = 0.38f;

static constexpr int FACE_PX = 1;
static constexpr int FACE_NX = 6;
static constexpr int FACE_PY = 2;
static constexpr int FACE_NY = 5;
static constexpr int FACE_PZ = 3;
static constexpr int FACE_NZ = 4;

struct Quat {
    float w, x, y, z;
};

struct Die {
    float x, y, z; // y = 高度（屏顶为盒顶方向）
    float vx, vy, vz;
    float yaw, pitch, roll;
    float wx, wy, wz;
    Quat settle_start_q, settle_target_q;
    float settle_t;
    float lineup_start_x, lineup_start_z;
    int up_face;
    bool snapping;
    bool settled;
};

static M5Canvas diceCanvas(&M5Cardputer.Display);
static bool diceCanvasOk = false;
static bool g_help = false;
static bool g_imu_ok = false;
static int g_width = 0;
static int g_height = 0;
static float g_die_r = 0.58f;
static float g_room_x = 6.2f;
static float g_room_z = 4.6f;
static float g_focal = 265.0f;
static float g_cam_elev = 0.78f; // 更俯视，活动范围铺满屏
static float g_cam_dist = 15.0f;
static int g_count = 2;
static Die g_dice[DICE_MAX];
static float g_prev_ax = 0;
static float g_prev_ay = 0;
static float g_prev_az = 1;
static float g_shake = 0;
static bool g_was_shaking = false;
static bool g_imu_sample_ready = false;
static uint32_t g_last_shake_impulse_ms = 0;
static uint32_t g_last_ms = 0;
static uint32_t g_sum = 0;
static bool g_has_rolled = false;
static float g_result_reveal = 0;
static uint32_t g_result_hold_ms = 0;
static bool g_lineup_active = false;
static float g_lineup_progress = 0;

static const float CUBE_VERTS[8][3] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

static const uint8_t CUBE_FACES[6][4] = {
    {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
    {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 5, 6, 2},
};

static const int FACE_VALUE[6] = {FACE_NZ, FACE_PZ, FACE_NY, FACE_PY, FACE_NX, FACE_PX};

static const float FACE_N[6][3] = {
    {0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0},
};

static void diceApplyPalette() {
    // 暖白骰子使用五级明暗，光线方向变化时仍有立体感
    static const uint32_t ivory_rgb[5] = {
        0x77736A, 0xA8A298, 0xD1CCC1, 0xECE8DE, 0xFFFDF5,
    };
    diceCanvas.setPaletteColor(0, 0, 0, 0);
    for (int i = 0; i < 5; ++i) {
        const uint32_t c = ivory_rgb[i];
        diceCanvas.setPaletteColor(static_cast<size_t>(i + 1), static_cast<uint8_t>(c >> 16),
                                   static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c));
    }
    diceCanvas.setPaletteColor(6, 0x5A, 0x56, 0x50); // 倒角暗边
    diceCanvas.setPaletteColor(7, 0xFF, 0xFE, 0xF8); // 倒角高光
    diceCanvas.setPaletteColor(8, 0x17, 0x16, 0x15); // 黑色点数
    diceCanvas.setPaletteColor(9, 0xB5, 0x20, 0x26); // 一点红
    diceCanvas.setPaletteColor(10, 0x12, 0x35, 0x28); // 深绿绒布
    diceCanvas.setPaletteColor(11, 0x18, 0x43, 0x32); // 绒布纹理
    diceCanvas.setPaletteColor(12, 0xF2, 0xE7, 0xBD); // HUD 暖白
    diceCanvas.setPaletteColor(13, 0x06, 0x12, 0x0D); // 阴影核心
    diceCanvas.setPaletteColor(14, 0x0C, 0x27, 0x1C); // 阴影边缘
    diceCanvas.setPaletteColor(15, 0x0B, 0x25, 0x1A); // 暗角
    diceCanvas.setPaletteColor(16, 0x08, 0x14, 0x10); // 结果牌阴影
    diceCanvas.setPaletteColor(17, 0x4A, 0x28, 0x05); // 结果牌底色
    diceCanvas.setPaletteColor(18, 0xD8, 0x8A, 0x08); // 金色边框
    diceCanvas.setPaletteColor(19, 0xFF, 0xE5, 0x72); // 结果文字
    diceCanvas.setPaletteColor(20, 0xFF, 0xFA, 0xCF); // 闪光
    diceCanvas.setPaletteColor(255, 255, 255, 255);
}

static void setupRoom() {
    g_width = M5Cardputer.Display.width();
    g_height = M5Cardputer.Display.height();
    // 扩大桌面边界，让骰子中心能够抵达四周屏幕边缘
    g_die_r = 0.60f;
    g_room_x = 6.2f;
    g_room_z = 4.6f;
    g_focal = 265.0f;
    g_cam_elev = 0.80f;
    g_cam_dist = 15.0f;
}

// Rx * Ry * Rz
static void rotPoint(const float yaw, const float pitch, const float roll, float x, float y, float z,
                     float& ox, float& oy, float& oz) {
    const float cy = cosf(yaw);
    const float sy = sinf(yaw);
    const float cp = cosf(pitch);
    const float sp = sinf(pitch);
    const float cr = cosf(roll);
    const float sr = sinf(roll);

    float x1 = x * cr - y * sr;
    float y1 = x * sr + y * cr;
    float z1 = z;

    float x2 = x1 * cy + z1 * sy;
    float z2 = -x1 * sy + z1 * cy;
    float y2 = y1;

    oy = y2 * cp - z2 * sp;
    oz = y2 * sp + z2 * cp;
    ox = x2;
}

static int faceTowardWorldUp(const Die& d) {
    int best = 0;
    float best_dot = -1e9f;
    for (int f = 0; f < 6; ++f) {
        float nx, ny, nz;
        rotPoint(d.yaw, d.pitch, d.roll, FACE_N[f][0], FACE_N[f][1], FACE_N[f][2], nx, ny, nz);
        if (ny > best_dot) {
            best_dot = ny;
            best = f;
        }
    }
    return FACE_VALUE[best];
}

static void restingPoseAngles(const int face_up, float& yaw, float& pitch, float& roll) {
    yaw = (rand() % 628) / 100.0f;
    pitch = 0;
    roll = 0;
    if (face_up == FACE_PY) {
        pitch = 0;
    } else if (face_up == FACE_NY) {
        pitch = 3.1415926f;
    } else if (face_up == FACE_PZ) {
        yaw = 0;
        pitch = -1.5707963f;
    } else if (face_up == FACE_NZ) {
        yaw = 0;
        pitch = 1.5707963f;
    } else if (face_up == FACE_PX) {
        yaw = 0;
        roll = 1.5707963f;
    } else {
        yaw = 0;
        roll = -1.5707963f;
    }
}

static Quat quatMultiply(const Quat& a, const Quat& b) {
    return {
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

static Quat quatNormalize(const Quat& q) {
    const float length = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (length < 0.0001f) {
        return {1, 0, 0, 0};
    }
    return {q.w / length, q.x / length, q.y / length, q.z / length};
}

static Quat quatFromEuler(const float yaw, const float pitch, const float roll) {
    const Quat qx = {cosf(pitch * 0.5f), sinf(pitch * 0.5f), 0, 0};
    const Quat qy = {cosf(yaw * 0.5f), 0, sinf(yaw * 0.5f), 0};
    const Quat qz = {cosf(roll * 0.5f), 0, 0, sinf(roll * 0.5f)};
    return quatNormalize(quatMultiply(quatMultiply(qx, qy), qz));
}

static Quat quatSlerp(const Quat& from, Quat to, const float t) {
    float dot = from.w * to.w + from.x * to.x + from.y * to.y + from.z * to.z;
    if (dot < 0) {
        dot = -dot;
        to = {-to.w, -to.x, -to.y, -to.z};
    }
    if (dot > 0.9995f) {
        return quatNormalize({
            from.w + (to.w - from.w) * t,
            from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t,
            from.z + (to.z - from.z) * t,
        });
    }
    const float theta = acosf(fminf(1.0f, dot));
    const float sin_theta = sinf(theta);
    const float a = sinf((1.0f - t) * theta) / sin_theta;
    const float b = sinf(t * theta) / sin_theta;
    return {
        from.w * a + to.w * b,
        from.x * a + to.x * b,
        from.y * a + to.y * b,
        from.z * a + to.z * b,
    };
}

static void quatToEuler(const Quat& q, float& yaw, float& pitch, float& roll) {
    const float r00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    const float r01 = 2.0f * (q.x * q.y - q.z * q.w);
    const float r02 = 2.0f * (q.x * q.z + q.y * q.w);
    const float r12 = 2.0f * (q.y * q.z - q.x * q.w);
    const float r22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    yaw = asinf(fmaxf(-1.0f, fminf(1.0f, r02)));
    pitch = atan2f(-r12, r22);
    roll = atan2f(-r01, r00);
}

static void snapRestingPose(Die& d, const int face_up) {
    restingPoseAngles(face_up, d.yaw, d.pitch, d.roll);
    d.vx = d.vy = d.vz = 0;
    d.wx = d.wy = d.wz = 0;
    d.y = g_die_r;
    d.settle_t = 1;
    d.up_face = face_up;
    d.snapping = false;
    d.settled = true;
}

static void beginSettling(Die& d, const int face_up) {
    d.vx = d.vy = d.vz = 0;
    d.wx = d.wy = d.wz = 0;
    d.y = g_die_r;
    d.settle_start_q = quatFromEuler(d.yaw, d.pitch, d.roll);

    int face_index = 0;
    for (int f = 0; f < 6; ++f) {
        if (FACE_VALUE[f] == face_up) {
            face_index = f;
            break;
        }
    }
    float nx, ny, nz;
    rotPoint(d.yaw, d.pitch, d.roll, FACE_N[face_index][0], FACE_N[face_index][1],
             FACE_N[face_index][2], nx, ny, nz);
    const float angle = acosf(fmaxf(-1.0f, fminf(1.0f, ny)));
    float axis_x = -nz;
    float axis_z = nx;
    const float axis_length = sqrtf(axis_x * axis_x + axis_z * axis_z);
    if (axis_length < 0.0001f) {
        axis_x = 1;
        axis_z = 0;
    } else {
        axis_x /= axis_length;
        axis_z /= axis_length;
    }
    const float half = angle * 0.5f;
    const Quat correction = {cosf(half), axis_x * sinf(half), 0, axis_z * sinf(half)};
    // 世界空间最短弧校正乘在当前姿态左侧
    d.settle_target_q = quatNormalize(quatMultiply(correction, d.settle_start_q));
    d.settle_t = 0;
    d.up_face = face_up;
    d.snapping = true;
    d.settled = false;
}

static void placeDie(Die& d, const int index) {
    const float spread_x = g_room_x * 0.55f;
    const float spread_z = g_room_z * 0.55f;
    d.x = ((index % 3) - 1) * spread_x * 0.85f;
    d.z = ((index / 3) - 0.35f) * spread_z;
    d.y = g_die_r;
    d.vx = d.vy = d.vz = 0;
    d.wx = d.wy = d.wz = 0;
    snapRestingPose(d, FACE_PY);
}

static void tossDie(Die& d, const float power) {
    d.snapping = false;
    d.settled = false;
    d.y = g_die_r;
    // 随机方向但保证最低速度，避免两个分量恰好都接近零
    const float angle = (rand() % 6283) * 0.001f;
    const float speed = (3.8f + (rand() % 240) * 0.01f) * power;
    d.vx = cosf(angle) * speed;
    d.vz = sinf(angle) * speed;
    // 只做一次有限高度的抛起，主体运动仍在桌面平面
    d.vy = (3.2f + (rand() % 130) * 0.01f) * sqrtf(power);
    d.wx += ((rand() % 200) - 100) / 28.0f * power;
    d.wy += ((rand() % 200) - 100) / 28.0f * power;
    d.wz += ((rand() % 200) - 100) / 28.0f * power;
}

static void tossAll(const float power) {
    g_has_rolled = true;
    g_result_reveal = 0;
    g_result_hold_ms = 0;
    g_lineup_active = false;
    g_lineup_progress = 0;
    for (int i = 0; i < g_count; ++i) {
        tossDie(g_dice[i], power);
    }
}

static void applyShakeImpulse(const float djx, const float djy, const float djz,
                              const float intensity) {
    const float horizontal = sqrtf(djx * djx + djy * djy);
    const float nx = (horizontal > 0.001f) ? djx / horizontal : 0;
    const float nz = (horizontal > 0.001f) ? djy / horizontal : 0;
    const float strength = 0.45f + intensity * 1.75f;
    g_has_rolled = true;
    g_result_reveal = 0;
    g_result_hold_ms = 0;
    g_lineup_active = false;
    g_lineup_progress = 0;

    for (int i = 0; i < g_count; ++i) {
        Die& d = g_dice[i];
        d.snapping = false;
        d.settled = false;
        const float random_x = ((rand() % 201) - 100) * 0.004f * intensity;
        const float random_z = ((rand() % 201) - 100) * 0.004f * intensity;
        d.vx += nx * strength + random_x;
        d.vz += nz * strength + random_z;
        d.vy = fmaxf(d.vy, 0.35f + intensity * (0.65f + fabsf(djz)));
        d.wx += (nz + random_z) * intensity * 1.8f;
        d.wy += ((rand() % 201) - 100) * 0.012f * intensity;
        d.wz -= (nx + random_x) * intensity * 1.8f;

        // 猛摇仍保留上限，防止连续采样冲量导致穿墙或数值发散
        const float planar_speed = sqrtf(d.vx * d.vx + d.vz * d.vz);
        const float max_speed = 6.0f + intensity * 4.0f;
        if (planar_speed > max_speed) {
            const float scale = max_speed / planar_speed;
            d.vx *= scale;
            d.vz *= scale;
        }
        d.wx = fmaxf(-18.0f, fminf(18.0f, d.wx));
        d.wy = fmaxf(-18.0f, fminf(18.0f, d.wy));
        d.wz = fmaxf(-18.0f, fminf(18.0f, d.wz));
    }
}

static void collideWalls(Die& d) {
    const float lim_x = g_room_x - g_die_r;
    const float lim_z = g_room_z - g_die_r;
    if (d.x < -lim_x) {
        d.x = -lim_x;
        d.vx = fabsf(d.vx) * REST;
    } else if (d.x > lim_x) {
        d.x = lim_x;
        d.vx = -fabsf(d.vx) * REST;
    }
    if (d.z < -lim_z) {
        d.z = -lim_z;
        d.vz = fabsf(d.vz) * REST;
    } else if (d.z > lim_z) {
        d.z = lim_z;
        d.vz = -fabsf(d.vz) * REST;
    }
}

static void collideFloor(Die& d, const float dt) {
    // 桌面碰撞只改变高度；水平运动始终限制在桌面平面
    d.y = g_die_r;
    if (d.vy < -0.55f) {
        d.vy = -d.vy * FLOOR_BOUNCE;
        // 落地冲击带来少量不规则翻转
        d.wx += d.vz * 0.10f;
        d.wz -= d.vx * 0.10f;
    } else {
        d.vy = 0;
    }

    const float linear_damp = expf(-LINEAR_DRAG * dt);
    const float angular_damp = expf(-ANGULAR_DRAG * dt);
    d.vx *= linear_damp;
    d.vz *= linear_damp;
    d.wx *= angular_damp;
    d.wy *= angular_damp;
    d.wz *= angular_damp;

    const float speed = sqrtf(d.vx * d.vx + d.vz * d.vz);
    const int face = faceTowardWorldUp(d);
    d.up_face = face;

    // 平移速度带动翻滚，滑动与自转自然同步
    const float target_wx = d.vz / g_die_r;
    const float target_wz = -d.vx / g_die_r;
    const float follow = fminf(1.0f, dt * 7.0f);
    d.wx += (target_wx - d.wx) * follow;
    d.wz += (target_wz - d.wz) * follow;

    // 线速度够慢后进入缓动阶段，不瞬间跳到最终姿态
    if (speed < SETTLE_V && fabsf(d.vy) < 0.35f) {
        beginSettling(d, face);
    }
}

static void collideDicePair(Die& a, Die& b) {
    // 只在水平面分离，禁止上下叠在一起
    float dx = b.x - a.x;
    float dz = b.z - a.z;
    float dist2 = dx * dx + dz * dz;
    const float min_d = g_die_r * 2.15f;
    if (dist2 < 1e-4f) {
        dx = 1;
        dz = 0;
        dist2 = 1;
    }
    if (dist2 > min_d * min_d) {
        return;
    }
    const float dist = sqrtf(dist2);
    dx /= dist;
    dz /= dist;
    const float overlap = min_d - dist;
    a.x -= dx * overlap * 0.5f;
    a.z -= dz * overlap * 0.5f;
    b.x += dx * overlap * 0.5f;
    b.z += dz * overlap * 0.5f;

    // 都已停稳：只推开位置，不要重新打转
    if ((a.settled || a.snapping) && (b.settled || b.snapping)) {
        return;
    }

    const float vn = (b.vx - a.vx) * dx + (b.vz - a.vz) * dz;
    if (vn > -0.05f) {
        return;
    }
    const float impulse = -(1.0f + REST) * vn * 0.5f;
    a.vx -= impulse * dx;
    a.vz -= impulse * dz;
    b.vx += impulse * dx;
    b.vz += impulse * dz;
    a.snapping = false;
    b.snapping = false;
    a.settled = false;
    b.settled = false;
}

static void stepPhysics(const float dt) {
    for (int i = 0; i < g_count; ++i) {
        Die& d = g_dice[i];
        if (d.snapping) {
            d.settle_t = fminf(1.0f, d.settle_t + dt / 0.48f);
            const float ease = d.settle_t * d.settle_t * (3.0f - 2.0f * d.settle_t);
            const Quat current = quatSlerp(d.settle_start_q, d.settle_target_q, ease);
            quatToEuler(current, d.yaw, d.pitch, d.roll);
            d.y = g_die_r;
            if (d.settle_t >= 1.0f) {
                quatToEuler(d.settle_target_q, d.yaw, d.pitch, d.roll);
                d.snapping = false;
                d.settled = true;
            }
            continue;
        }
        if (d.settled) {
            d.y = g_die_r;
            d.vx = d.vz = 0;
            d.vy = 0;
            d.wx = d.wy = d.wz = 0;
            continue;
        }

        d.vy -= GRAVITY * dt;
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        d.z += d.vz * dt;
        d.yaw += d.wy * dt;
        d.pitch += d.wx * dt;
        d.roll += d.wz * dt;

        collideWalls(d);
        if (d.y <= g_die_r) {
            collideFloor(d, dt);
        } else {
            // 空中只做很轻的阻尼，落地后才受桌面摩擦
            const float air_damp = expf(-0.12f * dt);
            d.vx *= air_damp;
            d.vz *= air_damp;
            d.wx *= air_damp;
            d.wy *= air_damp;
            d.wz *= air_damp;
        }
        if (!d.settled) {
            d.up_face = faceTowardWorldUp(d);
        }
    }

    for (int i = 0; i < g_count; ++i) {
        for (int j = i + 1; j < g_count; ++j) {
            collideDicePair(g_dice[i], g_dice[j]);
        }
    }
}

// 第三视角：斜上方看向盒心
static void projectWorld(const float x, const float y, const float z, float& sx, float& sy,
                         float& depth) {
    const float ca = cosf(g_cam_elev);
    const float sa = sinf(g_cam_elev);
    // 绕 X 俯视：把世界 Y 抬到画面上，Z 变深度
    const float py = y * ca - z * sa;
    const float pz = y * sa + z * ca;
    depth = pz + g_cam_dist;
    if (depth < 0.6f) {
        depth = 0.6f;
    }
    // 使用正交尺度覆盖整屏，避免靠近边缘时产生夸张透视变形
    const float inv = g_focal / g_cam_dist;
    sx = g_width * 0.5f + x * inv;
    sy = g_height * 0.52f - py * inv;
}

static void drawArena() {
    diceCanvas.fillSprite(10);

    // 固定位置的细小绒布纹理，不随帧变化，避免背景闪烁
    for (int y = 3; y < g_height; y += 7) {
        for (int x = (y * 13) % 11; x < g_width; x += 17) {
            if (((x * 7 + y * 11) & 3) == 0) {
                diceCanvas.drawPixel(x, y, 11);
            }
        }
    }

    // 屏幕边缘稍暗，形成桌面光照和景深感
    diceCanvas.fillRect(0, 0, g_width, 3, 15);
    diceCanvas.fillRect(0, g_height - 4, g_width, 4, 15);
    diceCanvas.fillRect(0, 0, 4, g_height, 15);
    diceCanvas.fillRect(g_width - 4, 0, 4, g_height, 15);
}

static void drawPips(const int x0, const int y0, const int x1, const int y1, const int x2,
                     const int y2, const int x3, const int y3, const int face) {
    const uint8_t dot = (face == 1) ? 9 : 8;
    // 坐标位于骰面自身平面，投影后点数会随骰面一起透视
    static const float offs[7][6][2] = {
        {},
        {{0, 0}},
        {{-0.47f, -0.47f}, {0.47f, 0.47f}},
        {{-0.47f, -0.47f}, {0, 0}, {0.47f, 0.47f}},
        {{-0.47f, -0.47f}, {0.47f, -0.47f}, {-0.47f, 0.47f}, {0.47f, 0.47f}},
        {{-0.47f, -0.47f}, {0.47f, -0.47f}, {0, 0}, {-0.47f, 0.47f},
         {0.47f, 0.47f}},
        {{-0.47f, -0.47f}, {0.47f, -0.47f}, {-0.47f, 0}, {0.47f, 0},
         {-0.47f, 0.47f}, {0.47f, 0.47f}},
    };
    if (face < 1 || face > 6) {
        return;
    }
    // 正面保持清晰大点，透视压缩明显的侧面改用小点避免糊成一片
    const float edge_a =
        sqrtf(static_cast<float>((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    const float edge_b =
        sqrtf(static_cast<float>((x3 - x0) * (x3 - x0) + (y3 - y0) * (y3 - y0)));
    const int r = (fminf(edge_a, edge_b) < 11.0f) ? 1 : 2;
    for (int i = 0; i < face; ++i) {
        const float u = (offs[face][i][0] + 1.0f) * 0.5f;
        const float v = (offs[face][i][1] + 1.0f) * 0.5f;
        const float xa = x0 + (x1 - x0) * u;
        const float ya = y0 + (y1 - y0) * u;
        const float xb = x3 + (x2 - x3) * u;
        const float yb = y3 + (y2 - y3) * u;
        diceCanvas.fillCircle(static_cast<int>(xa + (xb - xa) * v),
                              static_cast<int>(ya + (yb - ya) * v), r, dot);
    }
}

static uint8_t faceShade(const Die& d, const int face_index) {
    float nx, ny, nz;
    rotPoint(d.yaw, d.pitch, d.roll, FACE_N[face_index][0], FACE_N[face_index][1],
             FACE_N[face_index][2], nx, ny, nz);
    // 左前上方柔光
    const float light = nx * -0.38f + ny * 0.84f + nz * -0.39f;
    const int level = static_cast<int>((light + 1.0f) * 2.0f) + 1;
    return static_cast<uint8_t>((level < 1) ? 1 : ((level > 5) ? 5 : level));
}

static void drawDieShadow(const Die& d) {
    float sx, sy, depth;
    projectWorld(d.x + 0.10f, 0.02f, d.z + 0.08f, sx, sy, depth);
    const int rx = static_cast<int>(g_focal / g_cam_dist * g_die_r * 1.18f);
    const int ry = (rx * 42) / 100;
    diceCanvas.fillEllipse(static_cast<int>(sx), static_cast<int>(sy), rx + 3, ry + 2, 14);
    diceCanvas.fillEllipse(static_cast<int>(sx), static_cast<int>(sy), rx, ry, 13);
}

static void renderDie(const Die& d) {
    float sx[8];
    float sy[8];
    float depth[8];

    for (int i = 0; i < 8; ++i) {
        float lx = CUBE_VERTS[i][0] * g_die_r;
        float ly = CUBE_VERTS[i][1] * g_die_r;
        float lz = CUBE_VERTS[i][2] * g_die_r;
        float wx, wy, wz;
        rotPoint(d.yaw, d.pitch, d.roll, lx, ly, lz, wx, wy, wz);
        projectWorld(wx + d.x, wy + d.y, wz + d.z, sx[i], sy[i], depth[i]);
    }

    int order[6] = {0, 1, 2, 3, 4, 5};
    float face_d[6];
    for (int f = 0; f < 6; ++f) {
        face_d[f] = (depth[CUBE_FACES[f][0]] + depth[CUBE_FACES[f][1]] + depth[CUBE_FACES[f][2]] +
                     depth[CUBE_FACES[f][3]]) *
                    0.25f;
    }
    // 远→近
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 6; ++j) {
            if (face_d[order[i]] > face_d[order[j]]) {
                const int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    for (int oi = 0; oi < 6; ++oi) {
        const int f = order[oi];
        const uint8_t* idx = CUBE_FACES[f];
        const int x0 = static_cast<int>(sx[idx[0]]);
        const int y0 = static_cast<int>(sy[idx[0]]);
        const int x1 = static_cast<int>(sx[idx[1]]);
        const int y1 = static_cast<int>(sy[idx[1]]);
        const int x2 = static_cast<int>(sx[idx[2]]);
        const int y2 = static_cast<int>(sy[idx[2]]);
        const int x3 = static_cast<int>(sx[idx[3]]);
        const int y3 = static_cast<int>(sy[idx[3]]);

        // 屏幕空间背面剔除（与 Neon FX CUBE 相同）
        const int ax = x1 - x0;
        const int ay = y1 - y0;
        const int bx = x3 - x0;
        const int by = y3 - y0;
        if (ax * by - ay * bx <= 0) {
            continue;
        }

        const uint8_t face_color = faceShade(d, f);
        diceCanvas.fillTriangle(x0, y0, x1, y1, x2, y2, face_color);
        diceCanvas.fillTriangle(x0, y0, x2, y2, x3, y3, face_color);
        diceCanvas.drawLine(x0, y0, x1, y1, 6);
        diceCanvas.drawLine(x1, y1, x2, y2, 6);
        diceCanvas.drawLine(x2, y2, x3, y3, 6);
        diceCanvas.drawLine(x3, y3, x0, y0, 6);

        // 向中心收一像素的高光边，模拟圆润倒角
        const int pcx = (x0 + x1 + x2 + x3) / 4;
        const int pcy = (y0 + y1 + y2 + y3) / 4;
        const int ix0 = x0 + ((pcx > x0) ? 1 : -1);
        const int iy0 = y0 + ((pcy > y0) ? 1 : -1);
        const int ix1 = x1 + ((pcx > x1) ? 1 : -1);
        const int iy1 = y1 + ((pcy > y1) ? 1 : -1);
        const int ix3 = x3 + ((pcx > x3) ? 1 : -1);
        const int iy3 = y3 + ((pcy > y3) ? 1 : -1);
        diceCanvas.drawLine(ix0, iy0, ix1, iy1, 7);
        diceCanvas.drawLine(ix0, iy0, ix3, iy3, 7);

        drawPips(x0, y0, x1, y1, x2, y2, x3, y3, FACE_VALUE[f]);
    }
}

static void recomputeSum() {
    g_sum = 0;
    for (int i = 0; i < g_count; ++i) {
        g_sum += static_cast<uint32_t>(g_dice[i].up_face);
    }
}

static bool allDiceSettled() {
    for (int i = 0; i < g_count; ++i) {
        if (!g_dice[i].settled) {
            return false;
        }
    }
    return true;
}

static void startLineup() {
    g_lineup_active = true;
    g_lineup_progress = 0;
    for (int i = 0; i < g_count; ++i) {
        g_dice[i].lineup_start_x = g_dice[i].x;
        g_dice[i].lineup_start_z = g_dice[i].z;
    }
}

static void updateLineup(const float dt) {
    if (!g_lineup_active) {
        return;
    }
    g_lineup_progress = fminf(1.0f, g_lineup_progress + dt / 0.78f);
    const float ease =
        g_lineup_progress * g_lineup_progress * (3.0f - 2.0f * g_lineup_progress);
    for (int i = 0; i < g_count; ++i) {
        Die& d = g_dice[i];
        // 在原有间距上增加约 5px，最终结果横排更舒展
        const float target_x = (i - (g_count - 1) * 0.5f) * 1.96f;
        const float target_z = 2.45f;
        d.x = d.lineup_start_x + (target_x - d.lineup_start_x) * ease;
        d.z = d.lineup_start_z + (target_z - d.lineup_start_z) * ease;
    }
}

static void drawLineupValues() {
    if (!g_lineup_active || g_lineup_progress < 0.36f) {
        return;
    }
    const float reveal = fminf(1.0f, (g_lineup_progress - 0.36f) / 0.34f);
    const int badge_y = static_cast<int>(132.0f - reveal * 13.0f);
    diceCanvas.setTextSize(1);
    for (int i = 0; i < g_count; ++i) {
        float sx, sy, depth;
        projectWorld(g_dice[i].x, g_die_r, g_dice[i].z, sx, sy, depth);
        const int cx = static_cast<int>(sx);
        diceCanvas.fillCircle(cx, badge_y, 7, 17);
        diceCanvas.drawCircle(cx, badge_y, 7, 18);
        diceCanvas.setTextColor(19);
        char value[2] = {static_cast<char>('0' + g_dice[i].up_face), '\0'};
        diceCanvas.drawCenterString(value, cx + 1, badge_y - 3);
    }
}

static void drawResultBanner() {
    if (!g_has_rolled || g_result_reveal <= 0) {
        return;
    }

    const float ease = g_result_reveal * g_result_reveal * (3.0f - 2.0f * g_result_reveal);
    constexpr int panel_w = 116;
    constexpr int panel_h = 25;
    const int panel_x = (g_width - panel_w) / 2;
    const int panel_y = static_cast<int>(-panel_h + ease * 30.0f);

    diceCanvas.fillRoundRect(panel_x + 3, panel_y + 3, panel_w, panel_h, 6, 16);
    diceCanvas.fillRoundRect(panel_x, panel_y, panel_w, panel_h, 6, 17);
    diceCanvas.drawRoundRect(panel_x, panel_y, panel_w, panel_h, 6, 18);
    diceCanvas.drawRoundRect(panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4, 4, 19);

    char line[20];
    snprintf(line, sizeof(line), "TOTAL %lu", static_cast<unsigned long>(g_sum));
    diceCanvas.setTextSize(2);
    diceCanvas.setTextColor(19);
    diceCanvas.setCursor((g_width - static_cast<int>(strlen(line)) * 12) / 2, panel_y + 6);
    diceCanvas.print(line);

    // 仅在入场阶段扫过一次高光，完成后保持稳定
    if (g_result_reveal < 1.0f) {
        const int glint_x = panel_x + 5 + static_cast<int>(ease * (panel_w - 10));
        diceCanvas.drawFastVLine(glint_x, panel_y + 4, panel_h - 8, 20);
    }
}

static int drawHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

static int drawHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static void drawHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawHelpColHeader(0, col_y, col_w, "keymap");
    y = drawHelpBadge(2, y, "-=", "dice - / +");
    y = drawHelpBadge(2, y, "SPC", "toss");
    y = drawHelpKey(2, y, 'h', "help / close");
    y = drawHelpBadge(2, y, "IMU", "shake = toss");

    y = drawHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawHelpText(manual_x + 2, y, "Felt tabletop");
    y = drawHelpText(manual_x + 2, y, "Planar roll + bounce");
    y = drawHelpText(manual_x + 2, y, "No vertical stack");
    y = drawHelpText(manual_x + 2, y, "Final: face up");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

static bool ensureCanvas() {
    if (diceCanvasOk) {
        return true;
    }
    diceCanvas.setColorDepth(8);
    if (!diceCanvas.createSprite(g_width, g_height)) {
        return false;
    }
    if (!diceCanvas.createPalette()) {
        diceCanvas.deleteSprite();
        return false;
    }
    diceCanvasOk = true;
    diceApplyPalette();
    return true;
}

} // namespace

void enterDiceApp() {
    leaveDiceApp();
    g_help = false;
    g_count = 2;
    g_shake = 0;
    g_was_shaking = false;
    g_imu_sample_ready = false;
    g_last_shake_impulse_ms = 0;
    g_last_ms = millis();
    g_prev_ax = 0;
    g_prev_ay = 0;
    g_prev_az = 1;
    g_has_rolled = false;
    g_result_reveal = 0;
    g_result_hold_ms = 0;
    g_lineup_active = false;
    g_lineup_progress = 0;
    srand(static_cast<unsigned>(millis()));

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();
    M5.Imu.update();
    g_imu_ok = M5.Imu.isEnabled();

    setupRoom();
    for (int i = 0; i < DICE_MAX; ++i) {
        placeDie(g_dice[i], i);
    }
    recomputeSum();

    if (!g_imu_ok) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("IMU not found");
        return;
    }
    if (!ensureCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.printf("Canvas OOM (max %u)", ESP.getMaxAllocHeap());
        return;
    }
    updateDiceApp();
}

void leaveDiceApp() {
    g_help = false;
    if (diceCanvasOk) {
        diceCanvas.deleteSprite();
        diceCanvasOk = false;
    }
}

bool isDiceHelpVisible() {
    return g_help;
}

void updateDiceApp() {
    if (g_help || !g_imu_ok) {
        return;
    }
    if (!ensureCanvas()) {
        return;
    }

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5.Imu.update();

    float ax = 0;
    float ay = 0;
    float az = 0;
    M5.Imu.getAccel(&ax, &ay, &az);

    const uint32_t now = millis();
    float dt = (now - g_last_ms) / 1000.0f;
    if (dt < 0.001f) {
        dt = 0.001f;
    }
    if (dt > 0.033f) {
        dt = 0.033f;
    }
    g_last_ms = now;

    float djx = 0;
    float djy = 0;
    float djz = 0;
    float jerk = 0;
    if (g_imu_sample_ready) {
        djx = ax - g_prev_ax;
        djy = ay - g_prev_ay;
        djz = az - g_prev_az;
        jerk = sqrtf(djx * djx + djy * djy + djz * djz);
    } else {
        // 第一帧只建立基线，避免设备静止进入页面时误触发
        g_imu_sample_ready = true;
    }
    g_prev_ax = ax;
    g_prev_ay = ay;
    g_prev_az = az;
    g_shake = g_shake * 0.72f + jerk * 0.28f;

    // 提高死区：只有明显晃动才触发，越猛烈则连续冲量越大
    const float intensity = fminf(3.0f, fmaxf(0.0f, (g_shake - 0.22f) * 2.3f));
    const bool shaking = intensity > 0.45f;
    const uint32_t impulse_interval = (intensity > 1.4f) ? 65 : 145;
    if (shaking &&
        (!g_was_shaking || now - g_last_shake_impulse_ms >= impulse_interval)) {
        if (!g_was_shaking) {
            tossAll(0.24f + intensity * 0.56f);
        } else {
            applyShakeImpulse(djx, djy, djz, intensity);
        }
        g_last_shake_impulse_ms = now;
    }
    g_was_shaking = shaking;

    const int sub = 2;
    const float h = dt / sub;
    if (!g_lineup_active) {
        for (int s = 0; s < sub; ++s) {
            stepPhysics(h);
        }
    }
    recomputeSum();
    if (g_has_rolled && allDiceSettled()) {
        if (g_result_reveal < 1.0f) {
            g_result_reveal = fminf(1.0f, g_result_reveal + dt * 2.4f);
            if (g_result_reveal >= 1.0f) {
                g_result_hold_ms = now;
            }
        } else if (!g_lineup_active && g_result_hold_ms != 0 &&
                   now - g_result_hold_ms >= 1000) {
            startLineup();
        }
        updateLineup(dt);
    } else {
        g_result_reveal = 0;
        g_result_hold_ms = 0;
    }

    drawArena();

    // 远→近画骰子
    int order[DICE_MAX];
    float depth[DICE_MAX];
    for (int i = 0; i < g_count; ++i) {
        order[i] = i;
        float sx, sy, d;
        projectWorld(g_dice[i].x, g_dice[i].y, g_dice[i].z, sx, sy, d);
        depth[i] = d;
    }
    for (int i = 0; i < g_count - 1; ++i) {
        for (int j = i + 1; j < g_count; ++j) {
            if (depth[order[i]] < depth[order[j]]) {
                const int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (int i = 0; i < g_count; ++i) {
        drawDieShadow(g_dice[order[i]]);
    }
    for (int i = 0; i < g_count; ++i) {
        renderDie(g_dice[order[i]]);
    }

    drawLineupValues();
    drawResultBanner();
    diceCanvas.pushSprite(0, 0);
}

void handleDiceApp(const Keyboard_Class::KeysState& status) {
    for (char c : status.word) {
        if (c == 'h' || c == 'H') {
            g_help = !g_help;
            if (g_help) {
                drawHelpPage();
            } else {
                M5Cardputer.Display.clear();
                if (diceCanvasOk) {
                    diceCanvas.pushSprite(0, 0);
                }
            }
            return;
        }
        if (g_help) {
            continue;
        }
        if (c == ' ') {
            tossAll(1.35f);
        } else if (c == '-' || c == ',') {
            if (g_count > DICE_MIN) {
                --g_count;
                recomputeSum();
                g_has_rolled = false;
                g_result_reveal = 0;
                g_result_hold_ms = 0;
                g_lineup_active = false;
                g_lineup_progress = 0;
            }
        } else if (c == '=' || c == '+' || c == '.') {
            if (g_count < DICE_MAX) {
                placeDie(g_dice[g_count], g_count);
                ++g_count;
                recomputeSum();
                g_has_rolled = false;
                g_result_reveal = 0;
                g_result_hold_ms = 0;
                g_lineup_active = false;
                g_lineup_progress = 0;
            }
        }
    }
}
