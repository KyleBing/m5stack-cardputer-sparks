#include "app_minesweeper.h"
#include "app_common.h"
#include <LittleFS.h>
#include <cstdio>
#include <cstring>
#include <esp_system.h>

namespace {

static constexpr int MINES_TOP = 14; // 顶栏高度
static constexpr int MINES_LEVEL_COUNT = 3;
static constexpr int MINES_MAX_CELLS = 242; // 22 x 11（HARD）

// 长按方向键的连发节奏
static constexpr uint32_t MINES_REPEAT_DELAY_MS = 340;
static constexpr uint32_t MINES_REPEAT_RATE_MS = 85;

// IMU 倾斜移动光标：像鼠标一样只要保持倾斜就一直走，倾得越多走得越快
static constexpr ImuTiltConfig MINES_IMU_TILT = {0.12f, 0.06f, 0.38f, 90, 260, 45};
static constexpr uint32_t MINES_IMU_WARN_MS = 1500; // 无 IMU 时提示停留时间

static constexpr const char* MINES_REC_PATH = "/mines_rec.dat";
static constexpr uint32_t MINES_REC_MAGIC = 0x4D494E32; // 'MIN2'

// 格子状态
static constexpr uint8_t CELL_HIDDEN = 0;
static constexpr uint8_t CELL_REVEALED = 1;
static constexpr uint8_t CELL_FLAGGED = 2;

enum class MinesState : uint8_t {
    Ready = 0, // 还没翻开第一格，雷未布置
    Playing,
    Won,
    Lost,
};

struct MinesLevel {
    const char* name;
    uint8_t cols;
    uint8_t rows;
    uint8_t mines;
    uint8_t cell; // 单格像素边长
};

static constexpr MinesLevel MINES_LEVELS[MINES_LEVEL_COUNT] = {
    {"EASY", 10, 7, 10, 16},
    {"NORMAL", 15, 9, 22, 12},
    {"HARD", 22, 11, 50, 10},
};

// 每个难度分开记录：最快通关、胜负场次、连胜
struct MinesLevelRecord {
    uint32_t best_ms;
    uint16_t played;
    uint16_t won;
    uint16_t streak;
    uint16_t best_streak;
};

struct MinesRecord {
    uint32_t magic;
    MinesLevelRecord level[MINES_LEVEL_COUNT];
};

static M5Canvas minesCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static int g_width = 0;
static int g_height = 0;

static int g_level = 1;
static uint8_t g_mine[MINES_MAX_CELLS];
static uint8_t g_adj[MINES_MAX_CELLS];
static uint8_t g_cell[MINES_MAX_CELLS];
static uint16_t g_stack[MINES_MAX_CELLS];

static MinesState g_state = MinesState::Ready;
static int g_cursor = 0;
static int g_flags = 0;
static int g_revealed = 0;
static int g_explode_idx = -1;
static uint32_t g_start_ms = 0;
static uint32_t g_elapsed_ms = 0;
static bool g_new_best = false;
static bool g_show_records = false;
static bool g_dirty = true;
static uint32_t g_shown_sec = 0xFFFFFFFFu;

static int8_t g_repeat_dc = 0;
static int8_t g_repeat_dr = 0;
static uint32_t g_repeat_since_ms = 0;
static uint32_t g_repeat_last_ms = 0;

static bool g_imu_ctrl = false; // IMU 倾斜操控开关（会话内保持）
static ImuTiltState g_imu_tilt;
static uint32_t g_imu_warn_until_ms = 0;
static uint32_t g_imu_draw_ms = 0; // 倾斜指示点的重绘节流

static MinesRecord g_rec;

static const MinesLevel& level() {
    return MINES_LEVELS[g_level];
}

static int cellCount() {
    return level().cols * level().rows;
}

static void recReset() {
    memset(&g_rec, 0, sizeof(g_rec));
    g_rec.magic = MINES_REC_MAGIC;
}

static void recLoad() {
    if (!LittleFS.exists(MINES_REC_PATH)) {
        recReset();
        return;
    }
    File f = LittleFS.open(MINES_REC_PATH, "r");
    if (!f) {
        recReset();
        return;
    }
    const size_t n = f.read(reinterpret_cast<uint8_t*>(&g_rec), sizeof(g_rec));
    f.close();
    if (n != sizeof(g_rec) || g_rec.magic != MINES_REC_MAGIC) {
        recReset();
    }
}

static void recSave() {
    File f = LittleFS.open(MINES_REC_PATH, "w");
    if (!f) {
        return;
    }
    f.write(reinterpret_cast<const uint8_t*>(&g_rec), sizeof(g_rec));
    f.close();
}

static void applyPalette() {
    minesCanvas.setPaletteColor(0, 0x05, 0x08, 0x0D);  // 背景
    minesCanvas.setPaletteColor(1, 0x17, 0x27, 0x38);  // 顶栏 / 面板
    minesCanvas.setPaletteColor(2, 0x4A, 0x5A, 0x6B);  // 未翻开格面
    minesCanvas.setPaletteColor(3, 0x74, 0x88, 0x9B);  // 格面高光
    minesCanvas.setPaletteColor(4, 0x28, 0x34, 0x42);  // 格面阴影
    minesCanvas.setPaletteColor(5, 0x0F, 0x17, 0x20);  // 已翻开格面
    minesCanvas.setPaletteColor(6, 0x20, 0x2C, 0x39);  // 已翻开描边
    minesCanvas.setPaletteColor(7, 0xF4, 0xF1, 0xE8);  // 主文字
    minesCanvas.setPaletteColor(8, 0xA9, 0xB4, 0xC0);  // 次文字 / 旗杆
    minesCanvas.setPaletteColor(9, 0xE9, 0xC4, 0x6A);  // 标题金
    minesCanvas.setPaletteColor(10, 0xFF, 0x9D, 0x3F); // 光标
    minesCanvas.setPaletteColor(11, 0xFF, 0x5E, 0x68); // 红旗 / 爆炸
    // 12..19 对应数字 1..8 的经典配色
    minesCanvas.setPaletteColor(12, 0x56, 0xA8, 0xFF);
    minesCanvas.setPaletteColor(13, 0x42, 0xD3, 0x92);
    minesCanvas.setPaletteColor(14, 0xFF, 0x6B, 0x6B);
    minesCanvas.setPaletteColor(15, 0xB0, 0x6C, 0xFF);
    minesCanvas.setPaletteColor(16, 0xFF, 0xB4, 0x4D);
    minesCanvas.setPaletteColor(17, 0x4A, 0xD6, 0xD6);
    minesCanvas.setPaletteColor(18, 0xF0, 0xF0, 0xF0);
    minesCanvas.setPaletteColor(19, 0x9A, 0xA6, 0xB2);
    minesCanvas.setPaletteColor(20, 0x0A, 0x10, 0x18); // 面板阴影
    minesCanvas.setPaletteColor(21, 0x2D, 0x48, 0x5E); // 面板边框
    minesCanvas.setPaletteColor(22, 0x14, 0x18, 0x1E); // 雷体（爆炸格上）
    minesCanvas.setPaletteColor(23, 0xFF, 0xE5, 0x91); // 高光
}

static bool ensureCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    minesCanvas.setColorDepth(8);
    if (!minesCanvas.createSprite(g_width, g_height)) {
        return false;
    }
    if (!minesCanvas.createPalette()) {
        minesCanvas.deleteSprite();
        return false;
    }
    g_canvas_ok = true;
    applyPalette();
    return true;
}

static int fieldOriginX() {
    return (g_width - level().cols * level().cell) / 2;
}

static int fieldOriginY() {
    const int avail = g_height - MINES_TOP;
    return MINES_TOP + (avail - level().rows * level().cell) / 2;
}

// 遍历 idx 的 8 邻格，回调外部逻辑用下标数组返回
static int neighborsOf(const int idx, int* out) {
    const int cols = level().cols;
    const int rows = level().rows;
    const int r = idx / cols;
    const int c = idx % cols;
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) {
                continue;
            }
            const int nr = r + dr;
            const int nc = c + dc;
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) {
                continue;
            }
            out[n++] = nr * cols + nc;
        }
    }
    return n;
}

static void resetBoard() {
    memset(g_mine, 0, sizeof(g_mine));
    memset(g_adj, 0, sizeof(g_adj));
    memset(g_cell, CELL_HIDDEN, sizeof(g_cell));
    g_state = MinesState::Ready;
    g_flags = 0;
    g_revealed = 0;
    g_explode_idx = -1;
    g_elapsed_ms = 0;
    g_start_ms = 0;
    g_new_best = false;
    g_cursor = (level().rows / 2) * level().cols + level().cols / 2;
    g_repeat_dc = 0;
    g_repeat_dr = 0;
    g_shown_sec = 0xFFFFFFFFu;
    g_dirty = true;
}

// 第一次翻开之后才布雷，保证首格及其 8 邻格必定安全
static void placeMines(const int safe_idx) {
    const int total = cellCount();
    int forbidden[9];
    const int fn = neighborsOf(safe_idx, forbidden);

    int candidates[MINES_MAX_CELLS];
    int n = 0;
    for (int i = 0; i < total; ++i) {
        if (i == safe_idx) {
            continue;
        }
        bool skip = false;
        for (int k = 0; k < fn; ++k) {
            if (forbidden[k] == i) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            candidates[n++] = i;
        }
    }
    // 空间不够时退化为只保护首格本身
    if (n < level().mines) {
        n = 0;
        for (int i = 0; i < total; ++i) {
            if (i != safe_idx) {
                candidates[n++] = i;
            }
        }
    }

    const int want = (level().mines < n) ? level().mines : n;
    for (int k = 0; k < want; ++k) {
        const int j = k + static_cast<int>(esp_random() % static_cast<uint32_t>(n - k));
        const int tmp = candidates[k];
        candidates[k] = candidates[j];
        candidates[j] = tmp;
        g_mine[candidates[k]] = 1;
    }

    int nb[9];
    for (int i = 0; i < total; ++i) {
        if (g_mine[i]) {
            continue;
        }
        const int cnt = neighborsOf(i, nb);
        int adj = 0;
        for (int k = 0; k < cnt; ++k) {
            adj += g_mine[nb[k]] ? 1 : 0;
        }
        g_adj[i] = static_cast<uint8_t>(adj);
    }
}

static void finishWin() {
    g_state = MinesState::Won;
    g_elapsed_ms = millis() - g_start_ms;
    // 通关时把剩下的雷自动插旗，画面收尾更完整
    const int total = cellCount();
    for (int i = 0; i < total; ++i) {
        if (g_mine[i] && g_cell[i] != CELL_FLAGGED) {
            g_cell[i] = CELL_FLAGGED;
        }
    }
    g_flags = level().mines;

    MinesLevelRecord& rec = g_rec.level[g_level];
    rec.played++;
    rec.won++;
    rec.streak++;
    if (rec.streak > rec.best_streak) {
        rec.best_streak = rec.streak;
    }
    if (rec.best_ms == 0 || g_elapsed_ms < rec.best_ms) {
        rec.best_ms = g_elapsed_ms;
        g_new_best = true;
    }
    recSave();
    g_dirty = true;
}

static void finishLose(const int idx) {
    g_state = MinesState::Lost;
    g_elapsed_ms = millis() - g_start_ms;
    g_explode_idx = idx;
    // 摊开所有未标记的雷
    const int total = cellCount();
    for (int i = 0; i < total; ++i) {
        if (g_mine[i] && g_cell[i] == CELL_HIDDEN) {
            g_cell[i] = CELL_REVEALED;
        }
    }

    MinesLevelRecord& rec = g_rec.level[g_level];
    rec.played++;
    rec.streak = 0;
    recSave();
    g_dirty = true;
}

static void checkWin() {
    if (g_revealed == cellCount() - level().mines) {
        finishWin();
    }
}

// 从 idx 展开：数字为 0 时沿邻格继续，用显式栈避免深递归
// 入栈时即标记已翻开，保证每格最多入栈一次，栈深不会超过格子总数
static void floodReveal(const int start) {
    if (g_cell[start] != CELL_HIDDEN) {
        return;
    }
    g_cell[start] = CELL_REVEALED;
    g_revealed++;
    int top = 0;
    g_stack[top++] = static_cast<uint16_t>(start);
    int nb[9];
    while (top > 0) {
        const int idx = g_stack[--top];
        if (g_adj[idx] != 0) {
            continue;
        }
        const int cnt = neighborsOf(idx, nb);
        for (int k = 0; k < cnt; ++k) {
            const int n = nb[k];
            if (g_cell[n] == CELL_HIDDEN) {
                g_cell[n] = CELL_REVEALED;
                g_revealed++;
                g_stack[top++] = static_cast<uint16_t>(n);
            }
        }
    }
}

static void revealAt(const int idx) {
    if (g_cell[idx] != CELL_HIDDEN) {
        return;
    }
    if (g_state == MinesState::Ready) {
        placeMines(idx);
        g_state = MinesState::Playing;
        g_start_ms = millis();
    }
    if (g_mine[idx]) {
        g_cell[idx] = CELL_REVEALED;
        finishLose(idx);
        return;
    }
    floodReveal(idx);
    g_dirty = true;
    checkWin();
}

// 和弦：已翻开的数字格上，若周围旗数与数字相同则一次性翻开其余邻格
static void chordAt(const int idx) {
    if (g_cell[idx] != CELL_REVEALED || g_adj[idx] == 0) {
        return;
    }
    int nb[9];
    const int cnt = neighborsOf(idx, nb);
    int flags = 0;
    for (int k = 0; k < cnt; ++k) {
        if (g_cell[nb[k]] == CELL_FLAGGED) {
            flags++;
        }
    }
    if (flags != g_adj[idx]) {
        return;
    }
    for (int k = 0; k < cnt; ++k) {
        const int n = nb[k];
        if (g_cell[n] != CELL_HIDDEN) {
            continue;
        }
        if (g_mine[n]) {
            g_cell[n] = CELL_REVEALED;
            finishLose(n);
            return;
        }
        floodReveal(n);
    }
    g_dirty = true;
    checkWin();
}

static void digAtCursor() {
    if (g_state == MinesState::Won || g_state == MinesState::Lost) {
        return;
    }
    if (g_cell[g_cursor] == CELL_FLAGGED) {
        return;
    }
    if (g_cell[g_cursor] == CELL_REVEALED) {
        chordAt(g_cursor);
        return;
    }
    revealAt(g_cursor);
}

static void toggleFlag() {
    if (g_state == MinesState::Won || g_state == MinesState::Lost) {
        return;
    }
    if (g_cell[g_cursor] == CELL_REVEALED) {
        return;
    }
    if (g_cell[g_cursor] == CELL_FLAGGED) {
        g_cell[g_cursor] = CELL_HIDDEN;
        g_flags--;
    } else {
        g_cell[g_cursor] = CELL_FLAGGED;
        g_flags++;
    }
    g_dirty = true;
}

static void moveCursor(const int dc, const int dr) {
    const int cols = level().cols;
    const int rows = level().rows;
    int r = g_cursor / cols + dr;
    int c = g_cursor % cols + dc;
    if (c < 0) {
        c = cols - 1;
    } else if (c >= cols) {
        c = 0;
    }
    if (r < 0) {
        r = rows - 1;
    } else if (r >= rows) {
        r = 0;
    }
    g_cursor = r * cols + c;
    g_dirty = true;
}

static uint8_t numberColor(const int n) {
    if (n < 1 || n > 8) {
        return 7;
    }
    return static_cast<uint8_t>(11 + n);
}

static void drawHiddenFace(const int x, const int y, const int cell) {
    minesCanvas.fillRect(x, y, cell - 1, cell - 1, 2);
    minesCanvas.drawFastHLine(x, y, cell - 1, 3);
    minesCanvas.drawFastVLine(x, y, cell - 1, 3);
    minesCanvas.drawFastHLine(x, y + cell - 2, cell - 1, 4);
    minesCanvas.drawFastVLine(x + cell - 2, y, cell - 1, 4);
}

static void drawFlagGlyph(const int x, const int y, const int cell) {
    const int pole_x = x + cell / 2;
    const int top = y + 2;
    const int bottom = y + cell - 4;
    minesCanvas.drawFastVLine(pole_x, top, bottom - top + 1, 8);
    minesCanvas.fillTriangle(pole_x, top, pole_x, top + cell / 2 - 2, pole_x - cell / 2 + 2,
                             top + cell / 4, 11);
    minesCanvas.drawFastHLine(pole_x - 2, bottom, 5, 8);
}

static void drawMineGlyph(const int x, const int y, const int cell, const bool exploded) {
    const int cx = x + (cell - 1) / 2;
    const int cy = y + (cell - 1) / 2;
    const int r = (cell >= 14) ? 4 : 3;
    const uint8_t body = exploded ? 22 : 8;
    minesCanvas.fillCircle(cx, cy, r, body);
    minesCanvas.drawFastHLine(cx - r - 1, cy, r * 2 + 3, body);
    minesCanvas.drawFastVLine(cx, cy - r - 1, r * 2 + 3, body);
    minesCanvas.drawPixel(cx - 1, cy - 1, exploded ? 11 : 23);
}

static void drawWrongFlagGlyph(const int x, const int y, const int cell) {
    minesCanvas.drawLine(x + 2, y + 2, x + cell - 4, y + cell - 4, 11);
    minesCanvas.drawLine(x + cell - 4, y + 2, x + 2, y + cell - 4, 11);
}

static void drawBoard() {
    const int cols = level().cols;
    const int cell = level().cell;
    const int ox = fieldOriginX();
    const int oy = fieldOriginY();
    const int total = cellCount();
    const bool lost = g_state == MinesState::Lost;

    for (int i = 0; i < total; ++i) {
        const int x = ox + (i % cols) * cell;
        const int y = oy + (i / cols) * cell;
        const uint8_t st = g_cell[i];

        if (st == CELL_FLAGGED) {
            drawHiddenFace(x, y, cell);
            if (lost && !g_mine[i]) {
                drawMineGlyph(x, y, cell, false);
                drawWrongFlagGlyph(x, y, cell);
            } else {
                drawFlagGlyph(x, y, cell);
            }
            continue;
        }
        if (st == CELL_HIDDEN) {
            drawHiddenFace(x, y, cell);
            continue;
        }

        const bool exploded = (i == g_explode_idx);
        minesCanvas.fillRect(x, y, cell - 1, cell - 1, exploded ? 11 : 5);
        minesCanvas.drawRect(x, y, cell - 1, cell - 1, 6);
        if (g_mine[i]) {
            drawMineGlyph(x, y, cell, exploded);
        } else if (g_adj[i] > 0) {
            const int size = (cell >= 14) ? 2 : 1;
            const int cw = 6 * size;
            const int ch = 8 * size;
            minesCanvas.setTextSize(size);
            minesCanvas.setTextColor(numberColor(g_adj[i]));
            minesCanvas.setCursor(x + (cell - 1 - cw) / 2, y + (cell - 1 - ch) / 2);
            minesCanvas.print(static_cast<int>(g_adj[i]));
        }
    }

    // 光标框在格子外沿，不遮挡数字
    const int cx = ox + (g_cursor % cols) * cell;
    const int cy = oy + (g_cursor / cols) * cell;
    minesCanvas.drawRect(cx - 1, cy - 1, cell + 1, cell + 1, 10);
}

static uint32_t elapsedMs() {
    if (g_state == MinesState::Playing) {
        return millis() - g_start_ms;
    }
    return g_elapsed_ms;
}

// 倾斜指示：方框里的点跟着倾斜偏移，四个方向是否都识别一眼可见
static void drawTiltDot(const int x, const int y) {
    constexpr int box = 9;
    constexpr int reach = 3;
    minesCanvas.drawRect(x, y, box, box, 21);
    const int cx = x + box / 2;
    const int cy = y + box / 2;
    minesCanvas.drawPixel(cx, cy, 8);
    const float scale = static_cast<float>(reach) / MINES_IMU_TILT.enter;
    const int ox = static_cast<int>(constrain(g_imu_tilt.tilt_x * scale, -reach, reach));
    const int oy = static_cast<int>(constrain(g_imu_tilt.tilt_y * scale, -reach, reach));
    minesCanvas.fillRect(cx + ox - 1, cy + oy - 1, 2, 2, 23);
}

// IMU 介入标识：金色徽章画在剩余雷数与难度名之间的空档
static void drawImuBadge(const uint32_t now) {
    if (g_imu_ctrl) {
        minesCanvas.fillRoundRect(40, 2, 21, 10, 2, 9);
        minesCanvas.setTextColor(0);
        minesCanvas.setCursor(42, 3);
        minesCanvas.print("IMU");
        drawTiltDot(63, 2);
        return;
    }
    if (now < g_imu_warn_until_ms) {
        minesCanvas.setTextColor(11);
        minesCanvas.setCursor(40, 3);
        minesCanvas.print("NO IMU");
    }
}

static void drawTopBar(const uint32_t now) {
    minesCanvas.fillRect(0, 0, g_width, MINES_TOP - 1, 1);
    minesCanvas.setTextSize(1);

    // 左侧：小红旗 + 剩余雷数
    minesCanvas.fillTriangle(6, 3, 6, 8, 2, 5, 11);
    minesCanvas.drawFastVLine(6, 3, 8, 8);
    int remain = level().mines - g_flags;
    if (remain < -99) {
        remain = -99;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", remain);
    minesCanvas.setTextColor(remain < 0 ? 11 : 7);
    minesCanvas.setCursor(12, 3);
    minesCanvas.print(buf);

    const char* mid = level().name;
    if (g_state == MinesState::Won) {
        mid = "CLEARED";
    } else if (g_state == MinesState::Lost) {
        mid = "BOOM";
    }
    minesCanvas.setTextColor((g_state == MinesState::Lost) ? 11 : 9);
    minesCanvas.setCursor((g_width - static_cast<int>(strlen(mid)) * 6) / 2, 3);
    minesCanvas.print(mid);

    uint32_t sec = elapsedMs() / 1000;
    if (sec > 999) {
        sec = 999;
    }
    snprintf(buf, sizeof(buf), "%03lus", static_cast<unsigned long>(sec));
    minesCanvas.setTextColor(8);
    minesCanvas.setCursor(g_width - static_cast<int>(strlen(buf)) * 6 - 4, 3);
    minesCanvas.print(buf);

    drawImuBadge(now);
}

static void drawResultBanner() {
    if (g_state != MinesState::Won && g_state != MinesState::Lost) {
        return;
    }
    constexpr int panel_w = 148;
    constexpr int panel_h = 44;
    const int x = (g_width - panel_w) / 2;
    const int y = (g_height - panel_h) / 2 + 6;
    minesCanvas.fillRoundRect(x + 3, y + 3, panel_w, panel_h, 6, 20);
    minesCanvas.fillRoundRect(x, y, panel_w, panel_h, 6, 1);
    minesCanvas.drawRoundRect(x, y, panel_w, panel_h, 6, g_state == MinesState::Won ? 9 : 11);

    const char* title = (g_state == MinesState::Won) ? "CLEARED" : "BOOM";
    minesCanvas.setTextSize(2);
    minesCanvas.setTextColor(g_state == MinesState::Won ? 9 : 11);
    minesCanvas.setCursor(x + (panel_w - static_cast<int>(strlen(title)) * 12) / 2, y + 6);
    minesCanvas.print(title);

    char line[28];
    if (g_state == MinesState::Won) {
        if (g_new_best) {
            snprintf(line, sizeof(line), "%lus  NEW BEST!",
                     static_cast<unsigned long>(g_elapsed_ms / 1000));
        } else {
            const uint32_t best = g_rec.level[g_level].best_ms / 1000;
            snprintf(line, sizeof(line), "%lus   best %lus",
                     static_cast<unsigned long>(g_elapsed_ms / 1000),
                     static_cast<unsigned long>(best));
        }
    } else {
        snprintf(line, sizeof(line), "R restart   B records");
    }
    minesCanvas.setTextSize(1);
    minesCanvas.setTextColor(7);
    minesCanvas.setCursor(x + (panel_w - static_cast<int>(strlen(line)) * 6) / 2, y + 28);
    minesCanvas.print(line);
}

static void drawRecords() {
    minesCanvas.fillSprite(0);
    minesCanvas.setTextSize(1);
    minesCanvas.setTextColor(9);
    minesCanvas.setCursor(6, 5);
    minesCanvas.print("MINESWEEPER RECORDS");

    minesCanvas.setTextColor(8);
    minesCanvas.setCursor(6, 24);
    minesCanvas.print("LEVEL   BEST   WIN/PLAY  STREAK");
    minesCanvas.drawFastHLine(6, 34, g_width - 12, 21);

    char buf[40];
    for (int i = 0; i < MINES_LEVEL_COUNT; ++i) {
        const MinesLevelRecord& rec = g_rec.level[i];
        char best[8];
        if (rec.best_ms == 0) {
            snprintf(best, sizeof(best), "  --");
        } else {
            snprintf(best, sizeof(best), "%3lus", static_cast<unsigned long>(rec.best_ms / 1000));
        }
        snprintf(buf, sizeof(buf), "%-7s %s  %3u/%-3u %5u", MINES_LEVELS[i].name, best,
                 static_cast<unsigned>(rec.won), static_cast<unsigned>(rec.played),
                 static_cast<unsigned>(rec.best_streak));
        minesCanvas.setTextColor(i == g_level ? 7 : 8);
        minesCanvas.setCursor(6, 42 + i * 14);
        minesCanvas.print(buf);
    }

    const MinesLevelRecord& cur = g_rec.level[g_level];
    snprintf(buf, sizeof(buf), "current streak  %u", static_cast<unsigned>(cur.streak));
    minesCanvas.setTextColor(10);
    minesCanvas.setCursor(6, 96);
    minesCanvas.print(buf);

    minesCanvas.setTextColor(8);
    minesCanvas.setCursor(6, 118);
    minesCanvas.print("B close   1-3 level   R new game");
}

static void render() {
    if (!g_canvas_ok) {
        return;
    }
    if (g_show_records) {
        drawRecords();
    } else {
        minesCanvas.fillSprite(0);
        drawBoard();
        drawTopBar(millis());
        drawResultBanner();
    }
    minesCanvas.pushSprite(0, 0);
    g_dirty = false;
    g_shown_sec = elapsedMs() / 1000;
}

// 方向键：HID 上下左右 / Cardputer 的 ; , . / / EASD
// Cardputer 键盘是整齐网格，s 正上方是 e 不是 w，所以上键取 e
static bool readDirection(const Keyboard_Class::KeysState& status, int& dc, int& dr) {
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
        if (c == 'e') {
            dr = -1;
        } else if (c == 's') {
            dr = 1;
        } else if (c == 'a') {
            dc = -1;
        } else if (c == 'd') {
            dc = 1;
        }
    }
    return dc != 0 || dr != 0;
}

// 长按连发：按住的方向仍在按下时按固定节奏继续移动
static bool isDirectionStillHeld(const int dc, const int dr) {
    if (dr < 0) {
        return M5Cardputer.Keyboard.isKeyPressed(';') || M5Cardputer.Keyboard.isKeyPressed('e');
    }
    if (dr > 0) {
        return M5Cardputer.Keyboard.isKeyPressed('.') || M5Cardputer.Keyboard.isKeyPressed('s');
    }
    if (dc < 0) {
        return M5Cardputer.Keyboard.isKeyPressed(',') || M5Cardputer.Keyboard.isKeyPressed('a');
    }
    return M5Cardputer.Keyboard.isKeyPressed('/') || M5Cardputer.Keyboard.isKeyPressed('d');
}

// 倾斜移动光标：记录界面下不响应
static void pollImuCursor() {
    if (!g_imu_ctrl || g_show_records) {
        return;
    }
    int dc = 0;
    int dr = 0;
    if (imuTiltPoll(g_imu_tilt, MINES_IMU_TILT, dc, dr)) {
        moveCursor(dc, dr);
    }
}

// 开启时把当前握持姿态记为中立位，姿势变了再按一次 I 即可重新校准
static void toggleImuCtrl() {
    if (!isImuTiltAvailable()) {
        g_imu_ctrl = false;
        g_imu_warn_until_ms = millis() + MINES_IMU_WARN_MS;
        g_dirty = true;
        return;
    }
    g_imu_ctrl = !g_imu_ctrl;
    g_imu_warn_until_ms = 0;
    imuTiltReset(g_imu_tilt);
    g_dirty = true;
}

static void startNewGame() {
    resetBoard();
}

static void setLevel(const int index) {
    if (index < 0 || index >= MINES_LEVEL_COUNT || index == g_level) {
        return;
    }
    g_level = index;
    startNewGame();
}

} // namespace

void enterMinesweeperApp() {
    leaveMinesweeperApp();
    g_width = M5Cardputer.Display.width();
    g_height = M5Cardputer.Display.height();
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.clear();

    if (!ensureCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.setCursor(4, 4);
        M5Cardputer.Display.print("Mines canvas OOM");
        return;
    }

    recLoad();
    g_show_records = false;
    if (g_imu_ctrl && !isImuTiltAvailable()) {
        g_imu_ctrl = false;
    }
    g_imu_warn_until_ms = 0;
    imuTiltReset(g_imu_tilt);
    startNewGame();
    render();
}

void leaveMinesweeperApp() {
    if (g_canvas_ok) {
        minesCanvas.deleteSprite();
        g_canvas_ok = false;
    }
}

void updateMinesweeperApp() {
    if (!g_canvas_ok) {
        return;
    }
    const uint32_t now = millis();

    pollImuCursor();
    if (g_imu_warn_until_ms != 0 && now >= g_imu_warn_until_ms) {
        g_imu_warn_until_ms = 0;
        g_dirty = true;
    }
    // 倾斜指示点要跟手，但没必要每次主循环都刷
    if (g_imu_ctrl && !g_show_records && now - g_imu_draw_ms >= 100) {
        g_imu_draw_ms = now;
        g_dirty = true;
    }

    if ((g_repeat_dc != 0 || g_repeat_dr != 0) && !g_show_records) {
        if (!isDirectionStillHeld(g_repeat_dc, g_repeat_dr)) {
            g_repeat_dc = 0;
            g_repeat_dr = 0;
        } else if (now - g_repeat_since_ms >= MINES_REPEAT_DELAY_MS &&
                   now - g_repeat_last_ms >= MINES_REPEAT_RATE_MS) {
            g_repeat_last_ms = now;
            moveCursor(g_repeat_dc, g_repeat_dr);
        }
    }

    // 计时到秒才重绘，静态画面不空转
    if (!g_dirty && g_state == MinesState::Playing && elapsedMs() / 1000 != g_shown_sec) {
        g_dirty = true;
    }
    if (g_dirty) {
        render();
    }
}

void handleMinesweeperApp(const Keyboard_Class::KeysState& status) {
    int dc = 0;
    int dr = 0;
    if (!g_show_records && readDirection(status, dc, dr)) {
        moveCursor(dc, dr);
        g_repeat_dc = static_cast<int8_t>(dc);
        g_repeat_dr = static_cast<int8_t>(dr);
        g_repeat_since_ms = millis();
        g_repeat_last_ms = g_repeat_since_ms;
        return;
    }

    if (status.enter && !g_show_records) {
        digAtCursor();
        return;
    }

    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'b') {
            g_show_records = !g_show_records;
            g_dirty = true;
        } else if (c == 'r') {
            g_show_records = false;
            startNewGame();
        } else if (c >= '1' && c <= '3') {
            g_show_records = false;
            setLevel(c - '1');
        } else if (g_show_records) {
            continue;
        } else if (c == ' ' || c == ']') {
            // ] 与 [ 相邻，单手就能在插旗和排雷之间切换
            digAtCursor();
        } else if (c == 'f' || c == '[') {
            toggleFlag();
        } else if (c == 'i') {
            toggleImuCtrl();
        }
    }
}

void pollMinesweeperBtnA() {
    if (M5Cardputer.BtnA.wasPressed()) {
        if (g_show_records) {
            g_show_records = false;
            g_dirty = true;
            return;
        }
        digAtCursor();
    }
}
