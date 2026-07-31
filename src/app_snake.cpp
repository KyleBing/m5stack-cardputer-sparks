#include "app_snake.h"
#include "app_common.h"
#include <LittleFS.h>
#include <cstdio>
#include <cstring>
#include <esp_system.h>

namespace {

static constexpr int SNAKE_CELL = 6;
static constexpr int SNAKE_COLS = 40; // 240 / 6
static constexpr int SNAKE_ROWS = 20; // 120 / 6
static constexpr int SNAKE_TOP = 14;  // 顶栏高度
static constexpr int SNAKE_CELL_COUNT = SNAKE_COLS * SNAKE_ROWS;
static constexpr int SNAKE_START_LEN = 4;
static constexpr uint32_t SNAKE_BONUS_MS = 7000; // 金色果实存在时间
static constexpr int SNAKE_BONUS_EVERY = 5;      // 每吃 5 个普通果实刷一次
static constexpr int SNAKE_BONUS_SCORE = 5;

// 速度档位：每步间隔毫秒（吃果实后再逐步加快）
static constexpr uint16_t SNAKE_BASE_MS[] = {210, 170, 135, 105, 80};
static constexpr int SNAKE_SPEED_COUNT =
    static_cast<int>(sizeof(SNAKE_BASE_MS) / sizeof(SNAKE_BASE_MS[0]));
static constexpr uint16_t SNAKE_MIN_MS = 55;

// 格子占用类型
static constexpr uint8_t OCC_EMPTY = 0;
static constexpr uint8_t OCC_BODY = 1;
static constexpr uint8_t OCC_FOOD = 2;
static constexpr uint8_t OCC_BONUS = 3;

// IMU 倾斜转向：蛇本来就会一直往前走，只在倾斜方向变化时转向一次，不连发
static constexpr ImuTiltConfig SNAKE_IMU_TILT = {0.16f, 0.08f, 0.38f, 0, 0, 0};
static constexpr uint32_t SNAKE_IMU_WARN_MS = 1500; // 无 IMU 时提示停留时间

static constexpr const char* SNAKE_REC_PATH = "/snake_rec.dat";
static constexpr uint32_t SNAKE_REC_MAGIC = 0x534E4B31; // 'SNK1'

enum class SnakeState : uint8_t {
    Ready = 0,
    Playing,
    Paused,
    Over,
};

// 撞墙死 / 穿墙两种玩法，最高分分开记
struct SnakeRecord {
    uint32_t magic;
    uint16_t best[2];
    uint16_t played;
};

static M5Canvas snakeCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static int g_width = 0;
static int g_height = 0;

static uint8_t g_occ[SNAKE_CELL_COUNT];
static uint16_t g_body[SNAKE_CELL_COUNT];
static int g_head = 0;
static int g_tail = 0;
static int g_len = 0;

static int g_dir_x = 1;
static int g_dir_y = 0;
// 一步内连按两次方向时缓存，避免快速转向被吞
static int8_t g_queue_x[2];
static int8_t g_queue_y[2];
static int g_queue_count = 0;

static SnakeState g_state = SnakeState::Ready;
static int g_score = 0;
static int g_eaten = 0;
static int g_speed = 2;
static bool g_wrap = false;
static uint32_t g_last_tick_ms = 0;
static int g_bonus_cell = -1;
static uint32_t g_bonus_until_ms = 0;
static bool g_new_best = false;
static bool g_dirty = true;

static bool g_imu_ctrl = false; // IMU 倾斜操控开关（会话内保持）
static ImuTiltState g_imu_tilt;
static uint32_t g_imu_warn_until_ms = 0;
static uint32_t g_imu_draw_ms = 0; // 倾斜指示点的重绘节流

static SnakeRecord g_rec;

static void recReset() {
    memset(&g_rec, 0, sizeof(g_rec));
    g_rec.magic = SNAKE_REC_MAGIC;
}

static void recLoad() {
    if (!LittleFS.exists(SNAKE_REC_PATH)) {
        recReset();
        return;
    }
    File f = LittleFS.open(SNAKE_REC_PATH, "r");
    if (!f) {
        recReset();
        return;
    }
    const size_t n = f.read(reinterpret_cast<uint8_t*>(&g_rec), sizeof(g_rec));
    f.close();
    if (n != sizeof(g_rec) || g_rec.magic != SNAKE_REC_MAGIC) {
        recReset();
    }
}

static void recSave() {
    File f = LittleFS.open(SNAKE_REC_PATH, "w");
    if (!f) {
        return;
    }
    f.write(reinterpret_cast<const uint8_t*>(&g_rec), sizeof(g_rec));
    f.close();
}

static void applyPalette() {
    snakeCanvas.setPaletteColor(0, 0x05, 0x08, 0x0D);  // 背景
    snakeCanvas.setPaletteColor(1, 0x0E, 0x16, 0x1F);  // 场地网格
    snakeCanvas.setPaletteColor(2, 0xD8, 0xFF, 0xB0);  // 蛇头
    snakeCanvas.setPaletteColor(3, 0x6C, 0xE0, 0x5A);  // 靠近头部
    snakeCanvas.setPaletteColor(4, 0x38, 0xA8, 0x46);  // 蛇身
    snakeCanvas.setPaletteColor(5, 0x1E, 0x6B, 0x33);  // 蛇尾
    snakeCanvas.setPaletteColor(6, 0xFF, 0x5E, 0x68);  // 普通果实
    snakeCanvas.setPaletteColor(7, 0xF4, 0xF1, 0xE8);  // 主文字
    snakeCanvas.setPaletteColor(8, 0xA9, 0xB4, 0xC0);  // 次文字
    snakeCanvas.setPaletteColor(9, 0xE9, 0xC4, 0x6A);  // 金色 / 标题
    snakeCanvas.setPaletteColor(10, 0x17, 0x27, 0x38); // 面板
    snakeCanvas.setPaletteColor(11, 0x2D, 0x48, 0x5E); // 边框
    snakeCanvas.setPaletteColor(12, 0x56, 0xA8, 0xFF); // 信息蓝
    snakeCanvas.setPaletteColor(13, 0x0A, 0x10, 0x18); // 面板阴影
    snakeCanvas.setPaletteColor(14, 0xFF, 0xE5, 0x91); // 高光
}

static bool ensureCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    snakeCanvas.setColorDepth(8);
    if (!snakeCanvas.createSprite(g_width, g_height)) {
        return false;
    }
    if (!snakeCanvas.createPalette()) {
        snakeCanvas.deleteSprite();
        return false;
    }
    g_canvas_ok = true;
    applyPalette();
    return true;
}

static uint16_t& bestForMode() {
    return g_rec.best[g_wrap ? 1 : 0];
}

static int randomEmptyCell() {
    int empty = 0;
    for (int i = 0; i < SNAKE_CELL_COUNT; ++i) {
        if (g_occ[i] == OCC_EMPTY) {
            empty++;
        }
    }
    if (empty == 0) {
        return -1;
    }
    int pick = static_cast<int>(esp_random() % static_cast<uint32_t>(empty));
    for (int i = 0; i < SNAKE_CELL_COUNT; ++i) {
        if (g_occ[i] != OCC_EMPTY) {
            continue;
        }
        if (pick == 0) {
            return i;
        }
        pick--;
    }
    return -1;
}

static void spawnFood() {
    const int cell = randomEmptyCell();
    if (cell >= 0) {
        g_occ[cell] = OCC_FOOD;
    }
}

static void clearBonus() {
    if (g_bonus_cell >= 0 && g_occ[g_bonus_cell] == OCC_BONUS) {
        g_occ[g_bonus_cell] = OCC_EMPTY;
    }
    g_bonus_cell = -1;
    g_bonus_until_ms = 0;
}

static void spawnBonus(const uint32_t now) {
    clearBonus();
    const int cell = randomEmptyCell();
    if (cell < 0) {
        return;
    }
    g_bonus_cell = cell;
    g_occ[cell] = OCC_BONUS;
    g_bonus_until_ms = now + SNAKE_BONUS_MS;
}

static void resetGame() {
    memset(g_occ, OCC_EMPTY, sizeof(g_occ));
    g_len = SNAKE_START_LEN;
    g_tail = 0;
    g_head = g_len - 1;
    const int row = SNAKE_ROWS / 2;
    const int col_start = SNAKE_COLS / 2 - SNAKE_START_LEN / 2;
    for (int i = 0; i < g_len; ++i) {
        const int cell = row * SNAKE_COLS + col_start + i;
        g_body[i] = static_cast<uint16_t>(cell);
        g_occ[cell] = OCC_BODY;
    }
    g_dir_x = 1;
    g_dir_y = 0;
    g_queue_count = 0;
    g_score = 0;
    g_eaten = 0;
    g_bonus_cell = -1;
    g_bonus_until_ms = 0;
    g_new_best = false;
    g_state = SnakeState::Ready;
    g_last_tick_ms = millis();
    spawnFood();
    g_dirty = true;
}

static uint16_t tickInterval() {
    const int base = SNAKE_BASE_MS[g_speed];
    // 每吃一个加快 3ms，封顶到 SNAKE_MIN_MS
    int iv = base - g_eaten * 3;
    if (iv < SNAKE_MIN_MS) {
        iv = SNAKE_MIN_MS;
    }
    return static_cast<uint16_t>(iv);
}

static void pushDirection(const int dx, const int dy) {
    // 以最后一次生效 / 排队的方向为准，禁止 180 度掉头
    int last_x = g_dir_x;
    int last_y = g_dir_y;
    if (g_queue_count > 0) {
        last_x = g_queue_x[g_queue_count - 1];
        last_y = g_queue_y[g_queue_count - 1];
    }
    if (dx == -last_x && dy == -last_y) {
        return;
    }
    if (dx == last_x && dy == last_y) {
        return;
    }
    if (g_queue_count >= 2) {
        return;
    }
    g_queue_x[g_queue_count] = static_cast<int8_t>(dx);
    g_queue_y[g_queue_count] = static_cast<int8_t>(dy);
    g_queue_count++;
}

static void endGame() {
    g_state = SnakeState::Over;
    g_rec.played++;
    if (g_score > static_cast<int>(bestForMode())) {
        bestForMode() = static_cast<uint16_t>(g_score);
        g_new_best = true;
    }
    recSave();
    g_dirty = true;
}

static void tickSnake(const uint32_t now) {
    if (g_queue_count > 0) {
        g_dir_x = g_queue_x[0];
        g_dir_y = g_queue_y[0];
        g_queue_x[0] = g_queue_x[1];
        g_queue_y[0] = g_queue_y[1];
        g_queue_count--;
    }

    const int head_cell = g_body[g_head];
    int nr = head_cell / SNAKE_COLS + g_dir_y;
    int nc = head_cell % SNAKE_COLS + g_dir_x;
    if (nr < 0 || nr >= SNAKE_ROWS || nc < 0 || nc >= SNAKE_COLS) {
        if (!g_wrap) {
            endGame();
            return;
        }
        nr = (nr + SNAKE_ROWS) % SNAKE_ROWS;
        nc = (nc + SNAKE_COLS) % SNAKE_COLS;
    }
    const int next = nr * SNAKE_COLS + nc;

    const bool ate_food = g_occ[next] == OCC_FOOD;
    const bool ate_bonus = g_occ[next] == OCC_BONUS;
    const bool grow = ate_food || ate_bonus;
    if (!grow) {
        // 尾部本步会让开，先腾出格子再判碰撞
        g_occ[g_body[g_tail]] = OCC_EMPTY;
    }
    if (g_occ[next] == OCC_BODY) {
        endGame();
        return;
    }

    if (ate_bonus) {
        g_bonus_cell = -1;
        g_bonus_until_ms = 0;
        g_score += SNAKE_BONUS_SCORE;
    } else if (ate_food) {
        g_score += 1;
        g_eaten++;
    }

    g_head = (g_head + 1) % SNAKE_CELL_COUNT;
    g_body[g_head] = static_cast<uint16_t>(next);
    g_occ[next] = OCC_BODY;
    if (grow) {
        g_len++;
    } else {
        g_tail = (g_tail + 1) % SNAKE_CELL_COUNT;
    }

    if (ate_food) {
        spawnFood();
        if (g_eaten % SNAKE_BONUS_EVERY == 0) {
            spawnBonus(now);
        }
    }
    if (g_len >= SNAKE_CELL_COUNT) {
        endGame();
        return;
    }
    g_dirty = true;
}

static int cellX(const int cell) {
    return (cell % SNAKE_COLS) * SNAKE_CELL;
}

static int cellY(const int cell) {
    return SNAKE_TOP + (cell / SNAKE_COLS) * SNAKE_CELL;
}

// 倾斜指示：方框里的点跟着倾斜偏移，四个方向是否都识别一眼可见
static void drawTiltDot(const int x, const int y) {
    constexpr int box = 9;
    constexpr int reach = 3;
    snakeCanvas.drawRect(x, y, box, box, 11);
    const int cx = x + box / 2;
    const int cy = y + box / 2;
    snakeCanvas.drawPixel(cx, cy, 8);
    const float scale = static_cast<float>(reach) / SNAKE_IMU_TILT.enter;
    const int ox = static_cast<int>(constrain(g_imu_tilt.tilt_x * scale, -reach, reach));
    const int oy = static_cast<int>(constrain(g_imu_tilt.tilt_y * scale, -reach, reach));
    snakeCanvas.fillRect(cx + ox - 1, cy + oy - 1, 2, 2, 14);
}

// IMU 介入标识：金色徽章画在最高分与模式之间的空档
static void drawImuBadge(const uint32_t now) {
    if (g_imu_ctrl) {
        snakeCanvas.fillRoundRect(157, 2, 21, 10, 2, 9);
        snakeCanvas.setTextColor(13);
        snakeCanvas.setCursor(159, 3);
        snakeCanvas.print("IMU");
        drawTiltDot(180, 2);
        return;
    }
    if (now < g_imu_warn_until_ms) {
        snakeCanvas.setTextColor(6);
        snakeCanvas.setCursor(151, 3);
        snakeCanvas.print("NO IMU");
    }
}

static void drawTopBar(const uint32_t now) {
    snakeCanvas.fillRect(0, 0, g_width, SNAKE_TOP - 1, 10);
    snakeCanvas.setTextSize(1);
    snakeCanvas.setTextColor(9);
    snakeCanvas.setCursor(4, 3);
    snakeCanvas.print("SNAKE");

    char buf[16];
    snprintf(buf, sizeof(buf), "S%d", g_score);
    snakeCanvas.setTextColor(7);
    snakeCanvas.setCursor(46, 3);
    snakeCanvas.print(buf);

    snprintf(buf, sizeof(buf), "L%d", g_len);
    snakeCanvas.setTextColor(12);
    snakeCanvas.setCursor(88, 3);
    snakeCanvas.print(buf);

    snprintf(buf, sizeof(buf), "B%u", static_cast<unsigned>(bestForMode()));
    snakeCanvas.setTextColor(8);
    snakeCanvas.setCursor(130, 3);
    snakeCanvas.print(buf);

    snprintf(buf, sizeof(buf), "%s x%d", g_wrap ? "WRAP" : "WALL", g_speed + 1);
    const int right_w = static_cast<int>(strlen(buf)) * 6;
    snakeCanvas.setTextColor(g_wrap ? 12 : 8);
    snakeCanvas.setCursor(g_width - right_w - 4, 3);
    snakeCanvas.print(buf);

    drawImuBadge(now);
}

static void drawField() {
    snakeCanvas.fillRect(0, SNAKE_TOP - 1, g_width, g_height - SNAKE_TOP + 1, 0);
    // 稀疏点阵提示格子，不抢蛇身对比度
    for (int r = 0; r < SNAKE_ROWS; r += 2) {
        for (int c = 0; c < SNAKE_COLS; c += 2) {
            snakeCanvas.drawPixel(c * SNAKE_CELL + SNAKE_CELL / 2,
                                  SNAKE_TOP + r * SNAKE_CELL + SNAKE_CELL / 2, 1);
        }
    }
    if (!g_wrap) {
        snakeCanvas.drawFastHLine(0, SNAKE_TOP - 1, g_width, 11);
    }
}

static void drawFood() {
    for (int i = 0; i < SNAKE_CELL_COUNT; ++i) {
        if (g_occ[i] != OCC_FOOD) {
            continue;
        }
        const int x = cellX(i);
        const int y = cellY(i);
        snakeCanvas.fillRect(x + 1, y + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, 6);
        snakeCanvas.drawPixel(x + 2, y + 2, 14);
    }
}

static void drawBonus(const uint32_t now) {
    if (g_bonus_cell < 0) {
        return;
    }
    // 剩余不足 2 秒时闪烁提醒
    const bool expiring = (g_bonus_until_ms > now) && (g_bonus_until_ms - now < 2000);
    if (expiring && ((now / 150) % 2) == 0) {
        return;
    }
    const int x = cellX(g_bonus_cell);
    const int y = cellY(g_bonus_cell);
    snakeCanvas.fillRect(x, y, SNAKE_CELL, SNAKE_CELL, 9);
    snakeCanvas.drawRect(x, y, SNAKE_CELL, SNAKE_CELL, 14);
    snakeCanvas.drawPixel(x + 2, y + 2, 7);
}

static void drawSnake() {
    // 从尾到头渐亮，方向一目了然
    int idx = g_tail;
    for (int i = 0; i < g_len; ++i) {
        const int cell = g_body[idx];
        const int x = cellX(cell);
        const int y = cellY(cell);
        uint8_t color = 5;
        const int from_head = g_len - 1 - i;
        if (from_head == 0) {
            color = 2;
        } else if (from_head < 4) {
            color = 3;
        } else if (from_head < g_len / 2) {
            color = 4;
        }
        if (from_head == 0) {
            snakeCanvas.fillRect(x, y, SNAKE_CELL, SNAKE_CELL, color);
            // 眼睛朝向当前方向
            const int ex = x + 2 + g_dir_x * 2;
            const int ey = y + 2 + g_dir_y * 2;
            snakeCanvas.drawPixel(ex, ey, 0);
        } else {
            snakeCanvas.fillRect(x, y, SNAKE_CELL - 1, SNAKE_CELL - 1, color);
        }
        idx = (idx + 1) % SNAKE_CELL_COUNT;
    }
}

static void drawCenterPanel(const char* title, const char* line1, const char* line2) {
    constexpr int panel_w = 150;
    constexpr int panel_h = 62;
    const int x = (g_width - panel_w) / 2;
    const int y = (g_height - panel_h) / 2 + 4;
    snakeCanvas.fillRoundRect(x + 3, y + 3, panel_w, panel_h, 6, 13);
    snakeCanvas.fillRoundRect(x, y, panel_w, panel_h, 6, 10);
    snakeCanvas.drawRoundRect(x, y, panel_w, panel_h, 6, 9);

    snakeCanvas.setTextSize(2);
    snakeCanvas.setTextColor(9);
    snakeCanvas.setCursor(x + (panel_w - static_cast<int>(strlen(title)) * 12) / 2, y + 8);
    snakeCanvas.print(title);

    snakeCanvas.setTextSize(1);
    snakeCanvas.setTextColor(7);
    snakeCanvas.setCursor(x + (panel_w - static_cast<int>(strlen(line1)) * 6) / 2, y + 30);
    snakeCanvas.print(line1);

    snakeCanvas.setTextColor(8);
    snakeCanvas.setCursor(x + (panel_w - static_cast<int>(strlen(line2)) * 6) / 2, y + 44);
    snakeCanvas.print(line2);
}

static void drawOverlay() {
    char line1[28];
    if (g_state == SnakeState::Ready) {
        snprintf(line1, sizeof(line1), "best %u   games %u", static_cast<unsigned>(bestForMode()),
                 static_cast<unsigned>(g_rec.played));
        drawCenterPanel("SNAKE", line1, "arrows / EASD to start");
    } else if (g_state == SnakeState::Paused) {
        snprintf(line1, sizeof(line1), "score %d   len %d", g_score, g_len);
        drawCenterPanel("PAUSED", line1, "SPC resume");
    } else if (g_state == SnakeState::Over) {
        snprintf(line1, sizeof(line1), "score %d   best %u", g_score,
                 static_cast<unsigned>(bestForMode()));
        drawCenterPanel("GAME OVER", line1, g_new_best ? "NEW BEST!  SPC replay" : "SPC replay");
    }
}

static void render(const uint32_t now) {
    if (!g_canvas_ok) {
        return;
    }
    drawField();
    drawFood();
    drawBonus(now);
    drawSnake();
    drawTopBar(now);
    drawOverlay();
    snakeCanvas.pushSprite(0, 0);
    g_dirty = false;
}

// 方向键：HID 上下左右 / Cardputer 的 ; , . / / EASD
// Cardputer 键盘是整齐网格，s 正上方是 e 不是 w，所以上键取 e
static bool readDirection(const Keyboard_Class::KeysState& status, int& dx, int& dy) {
    dx = 0;
    dy = 0;
    for (const uint8_t hid : status.hid_keys) {
        switch (hid) {
            case 0x52:
            case 0x33:
                dy = -1;
                dx = 0;
                break;
            case 0x51:
            case 0x37:
                dy = 1;
                dx = 0;
                break;
            case 0x50:
            case 0x36:
                dx = -1;
                dy = 0;
                break;
            case 0x4F:
            case 0x38:
                dx = 1;
                dy = 0;
                break;
            default:
                break;
        }
    }
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'e') {
            dx = 0;
            dy = -1;
        } else if (c == 's') {
            dx = 0;
            dy = 1;
        } else if (c == 'a') {
            dx = -1;
            dy = 0;
        } else if (c == 'd') {
            dx = 1;
            dy = 0;
        }
    }
    return dx != 0 || dy != 0;
}

// 倾斜转向：待开局时顺手开局，只在游戏中改方向
static void pollImuSteer() {
    if (!g_imu_ctrl) {
        return;
    }
    if (g_state != SnakeState::Playing && g_state != SnakeState::Ready) {
        return;
    }
    int dx = 0;
    int dy = 0;
    if (!imuTiltPoll(g_imu_tilt, SNAKE_IMU_TILT, dx, dy)) {
        return;
    }
    if (g_state == SnakeState::Ready) {
        g_state = SnakeState::Playing;
        g_last_tick_ms = millis();
    }
    pushDirection(dx, dy);
    g_dirty = true;
}

// 开启时把当前握持姿态记为中立位，姿势变了再按一次 I 即可重新校准
static void toggleImuCtrl() {
    if (!isImuTiltAvailable()) {
        g_imu_ctrl = false;
        g_imu_warn_until_ms = millis() + SNAKE_IMU_WARN_MS;
        g_dirty = true;
        return;
    }
    g_imu_ctrl = !g_imu_ctrl;
    g_imu_warn_until_ms = 0;
    imuTiltReset(g_imu_tilt);
    g_dirty = true;
}

static void togglePause() {
    if (g_state == SnakeState::Over) {
        resetGame();
        g_state = SnakeState::Playing;
        g_last_tick_ms = millis();
    } else if (g_state == SnakeState::Playing) {
        g_state = SnakeState::Paused;
    } else {
        g_state = SnakeState::Playing;
        g_last_tick_ms = millis();
    }
    g_dirty = true;
}

} // namespace

void enterSnakeApp() {
    leaveSnakeApp();
    g_width = M5Cardputer.Display.width();
    g_height = M5Cardputer.Display.height();
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    if (!ensureCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Snake canvas OOM");
        return;
    }

    recLoad();
    g_speed = 2;
    if (g_imu_ctrl && !isImuTiltAvailable()) {
        g_imu_ctrl = false;
    }
    g_imu_warn_until_ms = 0;
    imuTiltReset(g_imu_tilt);
    resetGame();
    render(millis());
}

void leaveSnakeApp() {
    if (g_canvas_ok) {
        snakeCanvas.deleteSprite();
        g_canvas_ok = false;
    }
}

void updateSnakeApp() {
    if (!g_canvas_ok) {
        return;
    }
    const uint32_t now = millis();
    pollImuSteer();
    if (g_imu_warn_until_ms != 0 && now >= g_imu_warn_until_ms) {
        g_imu_warn_until_ms = 0;
        g_dirty = true;
    }
    // 倾斜指示点要跟手，但没必要每次主循环都刷
    if (g_imu_ctrl && now - g_imu_draw_ms >= 80) {
        g_imu_draw_ms = now;
        g_dirty = true;
    }
    if (g_state == SnakeState::Playing) {
        if (g_bonus_cell >= 0 && now >= g_bonus_until_ms) {
            clearBonus();
            g_dirty = true;
        }
        if (now - g_last_tick_ms >= tickInterval()) {
            g_last_tick_ms = now;
            tickSnake(now);
        }
        // 金色果实闪烁需要持续刷新
        if (g_bonus_cell >= 0) {
            g_dirty = true;
        }
    }
    if (g_dirty) {
        render(now);
    }
}

void handleSnakeApp(const Keyboard_Class::KeysState& status) {
    int dx = 0;
    int dy = 0;
    if (readDirection(status, dx, dy)) {
        if (g_state == SnakeState::Ready || g_state == SnakeState::Paused) {
            g_state = SnakeState::Playing;
            g_last_tick_ms = millis();
        }
        if (g_state == SnakeState::Playing) {
            pushDirection(dx, dy);
        }
        g_dirty = true;
        return;
    }

    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == ' ') {
            togglePause();
        } else if (c == 'r') {
            resetGame();
        } else if (c == 'm') {
            g_wrap = !g_wrap;
            resetGame();
        } else if (c == 'i') {
            toggleImuCtrl();
        } else if (c == '-') {
            if (g_speed > 0) {
                g_speed--;
                g_dirty = true;
            }
        } else if (c == '=' || c == '+') {
            if (g_speed < SNAKE_SPEED_COUNT - 1) {
                g_speed++;
                g_dirty = true;
            }
        }
    }
}

void pollSnakeBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        togglePause();
    }
}
