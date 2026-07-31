#include "app_life.h"
#include <cstdio>
#include <cstring>
#include <esp_system.h>

namespace {

static constexpr int LIFE_CELL = 4;
static constexpr int LIFE_COLS = 60; // 240 / 4
static constexpr int LIFE_ROWS = 30; // (135 - 14) 取整到 4 的倍数
static constexpr int LIFE_TOP = 14;  // 顶栏高度
static constexpr int LIFE_CELL_COUNT = LIFE_COLS * LIFE_ROWS;
static constexpr int LIFE_PATTERN_COUNT = 6;

// 演化速度档位（每代间隔毫秒）
static constexpr uint16_t LIFE_STEP_MS[] = {320, 200, 120, 70, 40};
static constexpr int LIFE_SPEED_COUNT =
    static_cast<int>(sizeof(LIFE_STEP_MS) / sizeof(LIFE_STEP_MS[0]));

enum class LifeStable : uint8_t {
    None = 0,
    Still,      // 与上一代完全相同
    Oscillator, // 与上上代相同（周期 2）
    Dead,       // 全灭
};

static M5Canvas lifeCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static int g_width = 0;
static int g_height = 0;

// 0 表示死；1..255 表示已存活代数，用于按年龄着色
static uint8_t g_cells[LIFE_CELL_COUNT];
static uint8_t g_next[LIFE_CELL_COUNT];

static bool g_running = true;
static uint32_t g_gen = 0;
static int g_pop = 0;
static int g_speed = 2;
static uint32_t g_last_step_ms = 0;
static uint32_t g_hash_prev1 = 0;
static uint32_t g_hash_prev2 = 0;
static LifeStable g_stable = LifeStable::None;
static int g_cursor_r = LIFE_ROWS / 2;
static int g_cursor_c = LIFE_COLS / 2;
static bool g_dirty = true;

// 图案数据：每对为 (row, col)，相对图案左上角
static const int8_t PAT_GLIDER[] = {0, 1, 1, 2, 2, 0, 2, 1, 2, 2};
static const int8_t PAT_GUN[] = {
    5,  1,  5,  2,  6,  1,  6,  2,  5,  11, 6,  11, 7,  11, 4,  12, 8,  12,
    3,  13, 9,  13, 3,  14, 9,  14, 6,  15, 4,  16, 8,  16, 5,  17, 6,  17,
    7,  17, 6,  18, 3,  21, 4,  21, 5,  21, 3,  22, 4,  22, 5,  22, 2,  23,
    6,  23, 1,  25, 2,  25, 6,  25, 7,  25, 3,  35, 4,  35, 3,  36, 4,  36,
};
static const int8_t PAT_PULSAR[] = {
    0,  2,  0,  3,  0,  4,  0,  8,  0,  9,  0,  10, 2,  0,  2,  5,  2,  7,  2,  12,
    3,  0,  3,  5,  3,  7,  3,  12, 4,  0,  4,  5,  4,  7,  4,  12, 5,  2,  5,  3,
    5,  4,  5,  8,  5,  9,  5,  10, 7,  2,  7,  3,  7,  4,  7,  8,  7,  9,  7,  10,
    8,  0,  8,  5,  8,  7,  8,  12, 9,  0,  9,  5,  9,  7,  9,  12, 10, 0,  10, 5,
    10, 7,  10, 12, 12, 2,  12, 3,  12, 4,  12, 8,  12, 9,  12, 10,
};
static const int8_t PAT_LWSS[] = {0, 1, 0, 2, 0, 3, 0, 4, 1, 0, 1, 4, 2, 4, 3, 0, 3, 3};
static const int8_t PAT_RPENT[] = {0, 1, 0, 2, 1, 0, 1, 1, 2, 1};
static const int8_t PAT_ACORN[] = {0, 1, 1, 3, 2, 0, 2, 1, 2, 4, 2, 5, 2, 6};

struct LifePattern {
    const char* name;
    const int8_t* cells;
    int pair_count;
    int rows;
    int cols;
};

static const LifePattern LIFE_PATTERNS[LIFE_PATTERN_COUNT] = {
    {"GLIDER", PAT_GLIDER, static_cast<int>(sizeof(PAT_GLIDER) / 2), 3, 3},
    {"GUN", PAT_GUN, static_cast<int>(sizeof(PAT_GUN) / 2), 10, 37},
    {"PULSAR", PAT_PULSAR, static_cast<int>(sizeof(PAT_PULSAR) / 2), 13, 13},
    {"LWSS", PAT_LWSS, static_cast<int>(sizeof(PAT_LWSS) / 2), 4, 5},
    {"R-PENT", PAT_RPENT, static_cast<int>(sizeof(PAT_RPENT) / 2), 3, 3},
    {"ACORN", PAT_ACORN, static_cast<int>(sizeof(PAT_ACORN) / 2), 3, 7},
};

static const char* g_pattern_name = "RANDOM";

static void applyPalette() {
    lifeCanvas.setPaletteColor(0, 0x05, 0x08, 0x0D);  // 背景
    lifeCanvas.setPaletteColor(1, 0x10, 0x18, 0x21);  // 网格暗点
    lifeCanvas.setPaletteColor(2, 0xEC, 0xFF, 0xF4);  // 刚出生
    lifeCanvas.setPaletteColor(3, 0x8B, 0xF5, 0xBE);  // 存活 2 代
    lifeCanvas.setPaletteColor(4, 0x42, 0xD3, 0x92);  // 存活 3-5 代
    lifeCanvas.setPaletteColor(5, 0x27, 0x9A, 0x69);  // 存活 6-15 代
    lifeCanvas.setPaletteColor(6, 0x1A, 0x6A, 0x4A);  // 长寿细胞
    lifeCanvas.setPaletteColor(7, 0xF4, 0xF1, 0xE8);  // 主文字
    lifeCanvas.setPaletteColor(8, 0xA9, 0xB4, 0xC0);  // 次文字
    lifeCanvas.setPaletteColor(9, 0xE9, 0xC4, 0x6A);  // 标题金
    lifeCanvas.setPaletteColor(10, 0xFF, 0x9D, 0x3F); // 光标
    lifeCanvas.setPaletteColor(11, 0x17, 0x27, 0x38); // 面板
    lifeCanvas.setPaletteColor(12, 0x56, 0xA8, 0xFF); // 信息蓝
}

static bool ensureCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    lifeCanvas.setColorDepth(8);
    if (!lifeCanvas.createSprite(g_width, g_height)) {
        return false;
    }
    if (!lifeCanvas.createPalette()) {
        lifeCanvas.deleteSprite();
        return false;
    }
    g_canvas_ok = true;
    applyPalette();
    return true;
}

// 年龄映射到调色板：越久越暗，形成拖尾感
static uint8_t ageColor(const uint8_t age) {
    if (age <= 1) {
        return 2;
    }
    if (age == 2) {
        return 3;
    }
    if (age <= 5) {
        return 4;
    }
    if (age <= 15) {
        return 5;
    }
    return 6;
}

static uint32_t lifeHash() {
    uint32_t h = 2166136261u;
    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        h ^= g_cells[i] ? 1u : 0u;
        h *= 16777619u;
    }
    return h;
}

static int countPopulation() {
    int pop = 0;
    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        if (g_cells[i] != 0) {
            pop++;
        }
    }
    return pop;
}

// 换局后重置代数与稳定检测
static void resetStats() {
    g_gen = 0;
    g_pop = countPopulation();
    g_hash_prev1 = 0;
    g_hash_prev2 = 0;
    g_stable = LifeStable::None;
    g_dirty = true;
}

static void clearGrid() {
    memset(g_cells, 0, sizeof(g_cells));
    g_pattern_name = "EMPTY";
    resetStats();
}

static void randomFill() {
    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        // 约 30% 密度，混沌演化最活跃
        g_cells[i] = ((esp_random() % 100u) < 30u) ? 1 : 0;
    }
    g_pattern_name = "RANDOM";
    resetStats();
}

static void loadPattern(const int index) {
    if (index < 0 || index >= LIFE_PATTERN_COUNT) {
        return;
    }
    const LifePattern& pat = LIFE_PATTERNS[index];
    memset(g_cells, 0, sizeof(g_cells));
    const int base_r = (LIFE_ROWS - pat.rows) / 2;
    const int base_c = (LIFE_COLS - pat.cols) / 2;
    for (int i = 0; i < pat.pair_count; ++i) {
        const int r = base_r + pat.cells[i * 2];
        const int c = base_c + pat.cells[i * 2 + 1];
        if (r >= 0 && r < LIFE_ROWS && c >= 0 && c < LIFE_COLS) {
            g_cells[r * LIFE_COLS + c] = 1;
        }
    }
    g_pattern_name = pat.name;
    resetStats();
}

// 环形边界（上下左右相接）的标准 B3/S23 规则
static void stepLife() {
    for (int r = 0; r < LIFE_ROWS; ++r) {
        const int up = ((r - 1 + LIFE_ROWS) % LIFE_ROWS) * LIFE_COLS;
        const int mid = r * LIFE_COLS;
        const int dn = ((r + 1) % LIFE_ROWS) * LIFE_COLS;
        for (int c = 0; c < LIFE_COLS; ++c) {
            const int cl = (c - 1 + LIFE_COLS) % LIFE_COLS;
            const int cr = (c + 1) % LIFE_COLS;
            int n = 0;
            n += g_cells[up + cl] ? 1 : 0;
            n += g_cells[up + c] ? 1 : 0;
            n += g_cells[up + cr] ? 1 : 0;
            n += g_cells[mid + cl] ? 1 : 0;
            n += g_cells[mid + cr] ? 1 : 0;
            n += g_cells[dn + cl] ? 1 : 0;
            n += g_cells[dn + c] ? 1 : 0;
            n += g_cells[dn + cr] ? 1 : 0;

            const uint8_t cur = g_cells[mid + c];
            uint8_t nxt = 0;
            if (cur != 0) {
                nxt = (n == 2 || n == 3) ? static_cast<uint8_t>((cur < 255) ? cur + 1 : 255) : 0;
            } else if (n == 3) {
                nxt = 1;
            }
            g_next[mid + c] = nxt;
        }
    }
    memcpy(g_cells, g_next, sizeof(g_cells));

    g_gen++;
    g_pop = countPopulation();

    const uint32_t h = lifeHash();
    if (g_pop == 0) {
        g_stable = LifeStable::Dead;
    } else if (h == g_hash_prev1) {
        g_stable = LifeStable::Still;
    } else if (h == g_hash_prev2) {
        g_stable = LifeStable::Oscillator;
    } else {
        g_stable = LifeStable::None;
    }
    g_hash_prev2 = g_hash_prev1;
    g_hash_prev1 = h;
    g_dirty = true;
}

static const char* stateText() {
    switch (g_stable) {
        case LifeStable::Dead:
            return "DEAD";
        case LifeStable::Still:
            return "STILL";
        case LifeStable::Oscillator:
            return "OSC";
        default:
            break;
    }
    return g_running ? "RUN" : "PAUSE";
}

static void drawTopBar() {
    lifeCanvas.fillRect(0, 0, g_width, LIFE_TOP - 1, 11);
    lifeCanvas.setTextSize(1);
    lifeCanvas.setTextColor(9);
    lifeCanvas.setCursor(4, 3);
    lifeCanvas.print("LIFE");

    char buf[16];
    snprintf(buf, sizeof(buf), "G%lu", static_cast<unsigned long>(g_gen));
    lifeCanvas.setTextColor(7);
    lifeCanvas.setCursor(36, 3);
    lifeCanvas.print(buf);

    snprintf(buf, sizeof(buf), "P%d", g_pop);
    lifeCanvas.setTextColor(12);
    lifeCanvas.setCursor(90, 3);
    lifeCanvas.print(buf);

    lifeCanvas.setTextColor(8);
    lifeCanvas.setCursor(136, 3);
    lifeCanvas.print(g_pattern_name);

    snprintf(buf, sizeof(buf), "%s x%d", stateText(), g_speed + 1);
    const int right_w = static_cast<int>(strlen(buf)) * 6;
    lifeCanvas.setTextColor((g_stable == LifeStable::None) ? 7 : 10);
    lifeCanvas.setCursor(g_width - right_w - 4, 3);
    lifeCanvas.print(buf);
}

static void drawGrid() {
    lifeCanvas.fillRect(0, LIFE_TOP - 1, g_width, g_height - LIFE_TOP + 1, 0);

    // 稀疏场景下只画活细胞，比逐格填充快得多
    for (int r = 0; r < LIFE_ROWS; ++r) {
        const int y = LIFE_TOP + r * LIFE_CELL;
        const int row_base = r * LIFE_COLS;
        for (int c = 0; c < LIFE_COLS; ++c) {
            const uint8_t age = g_cells[row_base + c];
            if (age == 0) {
                continue;
            }
            lifeCanvas.fillRect(c * LIFE_CELL, y, LIFE_CELL - 1, LIFE_CELL - 1, ageColor(age));
        }
    }

    // 暂停时显示编辑光标
    if (!g_running) {
        const int x = g_cursor_c * LIFE_CELL;
        const int y = LIFE_TOP + g_cursor_r * LIFE_CELL;
        lifeCanvas.drawRect(x - 1, y - 1, LIFE_CELL + 1, LIFE_CELL + 1, 10);
    }
}

static void render() {
    if (!g_canvas_ok) {
        return;
    }
    drawGrid();
    drawTopBar();
    lifeCanvas.pushSprite(0, 0);
    g_dirty = false;
}

static void moveCursor(const int dc, const int dr) {
    if (dc == 0 && dr == 0) {
        return;
    }
    g_cursor_c = (g_cursor_c + dc + LIFE_COLS) % LIFE_COLS;
    g_cursor_r = (g_cursor_r + dr + LIFE_ROWS) % LIFE_ROWS;
    // 移动光标即进入编辑，自动暂停
    g_running = false;
    g_dirty = true;
}

static void toggleCellAtCursor() {
    uint8_t& cell = g_cells[g_cursor_r * LIFE_COLS + g_cursor_c];
    cell = cell ? 0 : 1;
    g_pop = countPopulation();
    g_pattern_name = "CUSTOM";
    g_stable = LifeStable::None;
    g_hash_prev1 = 0;
    g_hash_prev2 = 0;
    g_dirty = true;
}

// 方向键：HID 上下左右 / Cardputer 的 ; , . / / WASD
static void readDirection(const Keyboard_Class::KeysState& status, int& dc, int& dr) {
    dc = 0;
    dr = 0;
    for (const uint8_t hid : status.hid_keys) {
        switch (hid) {
            case 0x52:
            case 0x33:
                dr = -1;
                break;
            case 0x51:
            case 0x37:
                dr = 1;
                break;
            case 0x50:
            case 0x36:
                dc = -1;
                break;
            case 0x4F:
            case 0x38:
                dc = 1;
                break;
            default:
                break;
        }
    }
    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'w') {
            dr = -1;
        } else if (c == 's') {
            dr = 1;
        } else if (c == 'a') {
            dc = -1;
        } else if (c == 'd') {
            dc = 1;
        }
    }
}

} // namespace

void enterLifeApp() {
    leaveLifeApp();
    g_width = M5Cardputer.Display.width();
    g_height = M5Cardputer.Display.height();
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    if (!ensureCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Life canvas OOM");
        return;
    }

    g_running = true;
    g_speed = 2;
    g_cursor_r = LIFE_ROWS / 2;
    g_cursor_c = LIFE_COLS / 2;
    g_last_step_ms = millis();
    randomFill();
    render();
}

void leaveLifeApp() {
    if (g_canvas_ok) {
        lifeCanvas.deleteSprite();
        g_canvas_ok = false;
    }
}

void updateLifeApp() {
    if (!g_canvas_ok) {
        return;
    }
    const uint32_t now = millis();
    // 已判定静止 / 周期 2 时停止推进，省电也避免刷屏
    const bool frozen = (g_stable == LifeStable::Dead || g_stable == LifeStable::Still);
    if (g_running && !frozen && (now - g_last_step_ms) >= LIFE_STEP_MS[g_speed]) {
        g_last_step_ms = now;
        stepLife();
    }
    if (g_dirty) {
        render();
    }
}

void handleLifeApp(const Keyboard_Class::KeysState& status) {
    int dc = 0;
    int dr = 0;
    readDirection(status, dc, dr);
    if (dc != 0 || dr != 0) {
        moveCursor(dc, dr);
        return;
    }

    if (status.enter) {
        toggleCellAtCursor();
        return;
    }

    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == ' ') {
            g_running = !g_running;
            g_last_step_ms = millis();
            g_dirty = true;
        } else if (c == 'n') {
            g_running = false;
            stepLife();
        } else if (c == 'r') {
            randomFill();
            g_running = true;
            g_last_step_ms = millis();
        } else if (c == 'c') {
            clearGrid();
            g_running = false;
        } else if (c >= '1' && c <= '6') {
            loadPattern(c - '1');
            g_running = true;
            g_last_step_ms = millis();
        } else if (c == '-') {
            if (g_speed > 0) {
                g_speed--;
                g_dirty = true;
            }
        } else if (c == '=' || c == '+') {
            if (g_speed < LIFE_SPEED_COUNT - 1) {
                g_speed++;
                g_dirty = true;
            }
        }
    }
}

void pollLifeBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        g_running = !g_running;
        g_last_step_ms = millis();
        g_dirty = true;
    }
}
