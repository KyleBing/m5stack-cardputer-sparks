#include "app_icons.h"
#include "M5Cardputer.h"
#include <cmath>

// ===== Logo =====

struct AppLogoCircle {
    int center_x;
    int center_y;
    int radius;
    int border_width;
    uint32_t border_color;
    uint32_t fill_color;
};

static const AppLogoCircle APP_LOGO_CIRCLES[] = {
    {25, 19, 20, 2, 0x000000, 0x30D158},  // green
    {46, 31, 20, 2, 0x000000, 0x3CD3FE},  // cyan
    {20, 38, 20, 2, 0x000000, 0xFF4245},  // red
    {42, 43, 14, 2, 0x000000, 0xFFD600},  // yellow
};

static uint16_t colorFromRgb(const uint32_t rgb) {
    return M5Cardputer.Display.color565(
        static_cast<uint8_t>((rgb >> 16) & 0xFF),
        static_cast<uint8_t>((rgb >> 8) & 0xFF),
        static_cast<uint8_t>(rgb & 0xFF));
}

void drawAppLogo(const int dest_x, const int dest_y, const int size) {
    const int circle_count = sizeof(APP_LOGO_CIRCLES) / sizeof(APP_LOGO_CIRCLES[0]);
    for (int i = 0; i < circle_count; i++) {
        const AppLogoCircle& c = APP_LOGO_CIRCLES[i];
        const int cx = dest_x + c.center_x * size / APP_LOGO_DESIGN_SIZE;
        const int cy = dest_y + c.center_y * size / APP_LOGO_DESIGN_SIZE;
        const int r = c.radius * size / APP_LOGO_DESIGN_SIZE;
        const int border = (c.border_width * size + APP_LOGO_DESIGN_SIZE - 1) / APP_LOGO_DESIGN_SIZE;
        if (r <= 0) {
            continue;
        }
        M5Cardputer.Display.fillCircle(cx, cy, r, colorFromRgb(c.fill_color));
        for (int b = 0; b < border; b++) {
            M5Cardputer.Display.drawCircle(cx, cy, r - b, colorFromRgb(c.border_color));
        }
    }
}

// ===== 方向箭头 =====

void drawIconArrowLeft(const int x, const int cy, const uint16_t color) {
    M5Cardputer.Display.fillTriangle(x + 5, cy - 3, x, cy, x + 5, cy + 3, color);
}

void drawIconArrowRight(const int x, const int cy, const uint16_t color) {
    M5Cardputer.Display.fillTriangle(x, cy - 3, x + 5, cy, x, cy + 3, color);
}

void drawIconArrowUp(const int x, const int cy, const uint16_t color) {
    const int cx = x + ICON_ARROW_W / 2;
    M5Cardputer.Display.fillTriangle(cx, cy - 3, cx - 3, cy + 2, cx + 3, cy + 2, color);
}

void drawIconArrowDown(const int x, const int cy, const uint16_t color) {
    const int cx = x + ICON_ARROW_W / 2;
    M5Cardputer.Display.fillTriangle(cx, cy + 3, cx - 3, cy - 2, cx + 3, cy - 2, color);
}

// 左右箭头合成（x 为左缘，cy 为垂直中心）
void drawIconArrowLeftRight(const int x, const int cy, const uint16_t color) {
    drawIconArrowLeft(x, cy, color);
    drawIconArrowRight(x + ICON_ARROW_W + 2, cy, color);
}

// 上下箭头纵向合成（x 为左缘，cy 为垂直中心；偏高，tip 慎用）
void drawIconArrowUpDown(const int x, const int cy, const uint16_t color) {
    drawIconArrowUp(x, cy - 4, color);
    drawIconArrowDown(x, cy + 4, color);
}

// 上下箭头横向并排（与左右合成同高，适合 tip）
void drawIconArrowUpDownFlat(const int x, const int cy, const uint16_t color) {
    drawIconArrowUp(x, cy, color);
    drawIconArrowDown(x + ICON_ARROW_W + 2, cy, color);
}

// ===== 返回图标（header） =====

void drawIconBack(const int x, const int y, const uint16_t color) {
    const int cx = x + ICON_BACK_W / 2 + 1;
    const int cy = y + ICON_BACK_H / 2 + 1;
    constexpr int r = 5;

    // 回退图标：左箭头 + 回弯圆弧（不绘制底框）
    const int ay = cy - 5;
    M5Cardputer.Display.drawLine(x + 5, ay, cx + 1, ay, color);
    M5Cardputer.Display.fillTriangle(x + 2, ay, x + 6, ay - 4, x + 6, ay + 4, color);
    for (int deg = -90; deg <= 90; deg++) {
        const float rad = deg * 3.14159265f / 180.0f;
        const int px = cx + static_cast<int>(r * cosf(rad));
        const int py = cy + static_cast<int>(r * sinf(rad));
        M5Cardputer.Display.drawPixel(px, py, color);
    }
}

// ===== WiFi 信号条 =====

int signalLevelFromRssi(const int rssi) {
    if (rssi >= -50) {
        return 4;
    }
    if (rssi >= -60) {
        return 3;
    }
    if (rssi >= -70) {
        return 2;
    }
    if (rssi >= -80) {
        return 1;
    }
    return 0;
}

void drawSignalBars(const int x, const int y, const int rssi, const uint16_t color) {
    constexpr int BAR_COUNT = 4;
    constexpr int BAR_W = 2;
    constexpr int BAR_GAP = 1;
    constexpr int HEIGHTS[BAR_COUNT] = {2, 4, 6, 8};

    const int level = signalLevelFromRssi(rssi);
    const int base_y = y + ICON_SIGNAL_H;

    for (int i = 0; i < BAR_COUNT; i++) {
        const int bx = x + i * (BAR_W + BAR_GAP);
        const int h = HEIGHTS[i];
        const int by = base_y - h;
        // 有信号全高实心；无信号完整边框
        if (i < level) {
            M5Cardputer.Display.fillRect(bx, by, BAR_W, h, color);
        } else {
            M5Cardputer.Display.drawRect(bx, by, BAR_W, h, DARKGREY);
        }
    }
}

// ===== WiFi 扇形圆图标（header） =====

// 左下锚点为圆心，绘制右上象限 1/4 圆弧
static void drawWifiQuarterArc(const int cx, const int cy, const int radius,
                               const uint16_t color) {
    if (radius <= 0) {
        return;
    }
    for (int deg = 0; deg <= 90; deg++) {
        const float rad = deg * 3.14159265f / 180.0f;
        const int px = cx + static_cast<int>(radius * cosf(rad) + 0.5f);
        const int py = cy - static_cast<int>(radius * sinf(rad) + 0.5f);
        M5Cardputer.Display.drawPixel(px, py, color);
    }
}

void drawIconWifi(const int x, const int y, const int rssi, const uint16_t color) {
    const int level = signalLevelFromRssi(rssi);
    const int cx = x;
    const int cy = y + ICON_WIFI_H - 1;

    M5Cardputer.Display.fillRect(cx, cy - WIFI_INNER_SIDE + 1, WIFI_INNER_SIDE, WIFI_INNER_SIDE,
                                 color);

    for (int i = 0; i < WIFI_RING_COUNT; i++) {
        // 半径：内块外缘起算，每层 WIFI_RING_STEP
        const int radius = WIFI_INNER_SIDE + WIFI_RING_STEP * (i + 1) - 1;
        const bool lit = level >= i + 2;
        drawWifiQuarterArc(cx, cy, radius, lit ? color : DARKGREY);
    }
}

// ===== 蓝牙 =====

void drawIconBle(const int x, const int y, const uint16_t color) {
    // 原字形整体左上移 1px，去掉空边；几何不拉伸
    // 原：left=x+1..right=x+8，top=y+1..bottom=y+13 → 现贴 x..x+7，y..y+12
    const int mid = x + 3;
    const int left = x;
    const int right = x + 7;
    const int top = y;
    const int center = y + 6;
    const int bottom = y + 12;

    // 中心竖线
    M5Cardputer.Display.drawLine(mid, top, mid, bottom, color);

    // 右侧菱形折线
    M5Cardputer.Display.drawLine(mid, top + 1, right, y + 3, color);
    M5Cardputer.Display.drawLine(right, y + 3, mid, center, color);
    M5Cardputer.Display.drawLine(mid, center, right, y + 9, color);
    M5Cardputer.Display.drawLine(right, y + 9, mid, bottom - 1, color);

    // 左侧装饰折线
    M5Cardputer.Display.drawLine(left, y + 3, mid, center, color);
    M5Cardputer.Display.drawLine(mid, center, left, y + 9, color);
}

// ===== 充电闪电 =====

void drawIconChargingBolt(const int zone_x, const int y, const int body_h) {
    constexpr int zone_w = 6;
    const int cx = zone_x + zone_w / 2;
    const int cy = y + body_h / 2;
    const int s = body_h <= 12 ? 1 : 0;
    M5Cardputer.Display.fillTriangle(cx + 1 - s, cy - 4 + s, cx - 2, cy + 1, cx, cy + 1, YELLOW);
    M5Cardputer.Display.fillTriangle(cx - 1 + s, cy + 4 - s, cx + 2, cy - 1, cx, cy - 1, YELLOW);
}

// ===== 电池图标 =====

static constexpr int BATTERY_SEGMENTS = 5;
static constexpr int BATTERY_SEG_W = 3;
static constexpr int BATTERY_SEG_H = 8;
static constexpr int BATTERY_SEG_GAP = 1;
static constexpr int BATTERY_BORDER = 1;
static constexpr int BATTERY_INNER_GAP = 1;
static constexpr int BATTERY_HEAD_W = 2;
static constexpr int BATTERY_HEAD_H = 4;
static constexpr int BOLT_ZONE_W = 6;
static constexpr int BOLT_BATTERY_GAP = 1;

static int getBatteryInset() {
    return BATTERY_BORDER + BATTERY_INNER_GAP;
}

static int getBatteryInnerWidth() {
    return BATTERY_SEGMENTS * BATTERY_SEG_W + (BATTERY_SEGMENTS - 1) * BATTERY_SEG_GAP;
}

static int getBatteryBodyWidth() {
    return getBatteryInnerWidth() + getBatteryInset() * 2;
}

int getIconBatteryBodyHeight() {
    return BATTERY_SEG_H + getBatteryInset() * 2;
}

static int getBatteryIndicatorWidth() {
    return BATTERY_HEAD_W + getBatteryBodyWidth();
}

int getIconBatteryDisplayWidth(const bool charging) {
    return getBatteryIndicatorWidth() + (charging ? BOLT_ZONE_W + BOLT_BATTERY_GAP : 0);
}

void drawIconBattery(const int x, const int y, const int level, const bool charging) {
    const int body_w = getBatteryBodyWidth();
    const int body_h = getIconBatteryBodyHeight();
    const int bolt_offset = charging ? BOLT_ZONE_W + BOLT_BATTERY_GAP : 0;
    const int battery_x = x + bolt_offset;
    // 正极朝左：head | body
    const int head_x = battery_x;
    const int body_x = battery_x + BATTERY_HEAD_W;
    const int total_w = getBatteryIndicatorWidth();
    const uint16_t accent = charging ? GREEN : WHITE;

    M5Cardputer.Display.fillRect(x, y, bolt_offset + total_w, body_h, BLACK);

    if (charging) {
        drawIconChargingBolt(x, y, body_h);
    }

    const int head_y = y + (body_h - BATTERY_HEAD_H) / 2;
    M5Cardputer.Display.fillRect(head_x, head_y, BATTERY_HEAD_W, BATTERY_HEAD_H, accent);
    M5Cardputer.Display.drawRect(body_x, y, body_w, body_h, accent);

    const int inset = getBatteryInset();
    const int seg_x0 = body_x + inset;
    const int seg_y = y + inset;
    const int filled = constrain((level + 19) / 20, 0, BATTERY_SEGMENTS);
    for (int i = 0; i < BATTERY_SEGMENTS; i++) {
        const int sx = seg_x0 + i * (BATTERY_SEG_W + BATTERY_SEG_GAP);
        // 从远离正极一侧开始亮（正极在左 → 从右往左填）；无电格子不画内框
        if (i >= BATTERY_SEGMENTS - filled) {
            M5Cardputer.Display.fillRect(sx, seg_y, BATTERY_SEG_W, BATTERY_SEG_H, accent);
        }
    }
}

// 10 格：格宽略窄，整体高度与原电池一致
static constexpr int BATTERY10_SEGMENTS = 10;
static constexpr int BATTERY10_SEG_W = 2;

static int getBattery10InnerWidth() {
    return BATTERY10_SEGMENTS * BATTERY10_SEG_W + (BATTERY10_SEGMENTS - 1) * BATTERY_SEG_GAP;
}

static int getBattery10BodyWidth() {
    return getBattery10InnerWidth() + getBatteryInset() * 2;
}

int getIconBattery10DisplayWidth() {
    return BATTERY_HEAD_W + getBattery10BodyWidth();
}

void drawIconBattery10(const int x, const int y, const int level) {
    const int body_w = getBattery10BodyWidth();
    const int body_h = getIconBatteryBodyHeight();
    const int head_x = x;
    const int body_x = x + BATTERY_HEAD_W;
    const int total_w = getIconBattery10DisplayWidth();
    constexpr uint16_t accent = WHITE;

    M5Cardputer.Display.fillRect(x, y, total_w, body_h, BLACK);

    const int head_y = y + (body_h - BATTERY_HEAD_H) / 2;
    M5Cardputer.Display.fillRect(head_x, head_y, BATTERY_HEAD_W, BATTERY_HEAD_H, accent);
    M5Cardputer.Display.drawRect(body_x, y, body_w, body_h, accent);

    const int inset = getBatteryInset();
    const int seg_x0 = body_x + inset;
    const int seg_y = y + inset;
    // 每格 10%：(level+9)/10 → 1..10；0% 全灭
    const int filled = constrain((level + 9) / 10, 0, BATTERY10_SEGMENTS);
    for (int i = 0; i < BATTERY10_SEGMENTS; i++) {
        const int sx = seg_x0 + i * (BATTERY10_SEG_W + BATTERY_SEG_GAP);
        if (i >= BATTERY10_SEGMENTS - filled) {
            M5Cardputer.Display.fillRect(sx, seg_y, BATTERY10_SEG_W, BATTERY_SEG_H, accent);
        }
    }
}

// ===== 运行 / 停止 =====

void drawIconPlay(const int x, const int cy, const uint16_t color) {
    // 右向三角：播放
    const int half = ICON_PLAY_H / 2;
    M5Cardputer.Display.fillTriangle(x, cy - half, x, cy + half, x + ICON_PLAY_W - 1, cy, color);
}

void drawIconStop(const int x, const int cy, const uint16_t color) {
    // 方块：停止
    M5Cardputer.Display.fillRect(x, cy - ICON_STOP_H / 2, ICON_STOP_W, ICON_STOP_H, color);
}

// ===== 分页 indicator（横向长条；active = 电池高度 - 2）=====

int getIconPageDotsWidth(const int page_count) {
    if (page_count <= 1) {
        return 0;
    }
    return page_count * ICON_PAGE_DOT_W + (page_count - 1) * ICON_PAGE_DOT_GAP;
}

void drawIconPageDots(const int x, const int cy, const int page, const int page_count) {
    if (page_count <= 1) {
        return;
    }

    const int active_h = getIconBatteryBodyHeight() - 2;
    const int step = ICON_PAGE_DOT_W + ICON_PAGE_DOT_GAP;
    for (int i = 0; i < page_count; i++) {
        const bool active = (i == page);
        const int h = active ? active_h : ICON_PAGE_DOT_H;
        const int y = cy - h / 2;
        const uint16_t color = active ? ICON_PAGE_DOT_ACTIVE : ICON_PAGE_DOT_IDLE;
        M5Cardputer.Display.fillRect(x + i * step, y, ICON_PAGE_DOT_W, h, color);
    }
}

