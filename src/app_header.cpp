#include "app_header.h"
#include "app_logo.h"
#include "M5Cardputer.h"

static constexpr int MENU_LOGO_SIZE = 24;
static constexpr int BATTERY_SEGMENTS = 5;
static constexpr int BATTERY_SEG_W = 4;
static constexpr int BATTERY_SEG_H = 10;
static constexpr int BATTERY_SEG_GAP = 2;
static constexpr int BATTERY_BORDER = 1;
static constexpr int BATTERY_INNER_GAP = 2;
static constexpr int BATTERY_HEAD_W = 1;
static constexpr int BATTERY_HEAD_H = 6;

// 边框内侧到电池节的间距（四边各 2px）
static int getBatteryInset() {
    return BATTERY_BORDER + BATTERY_INNER_GAP;
}

// 内部 5 格总宽
static int getBatteryInnerWidth() {
    return BATTERY_SEGMENTS * BATTERY_SEG_W + (BATTERY_SEGMENTS - 1) * BATTERY_SEG_GAP;
}

// 带 1px 边框的电池主体宽高
static int getBatteryBodyWidth() {
    return getBatteryInnerWidth() + getBatteryInset() * 2;
}

static int getBatteryBodyHeight() {
    return BATTERY_SEG_H + getBatteryInset() * 2;
}

// 含左侧电池头的总宽
static int getBatteryIndicatorWidth() {
    return BATTERY_HEAD_W + getBatteryBodyWidth();
}

// 主菜单 header 右侧：分页圆点左侧的电量块起始 x
static int getMenuBatteryX(const int screen_w, const int page_count) {
    int right = screen_w - 4;
    if (page_count > 1) {
        constexpr int dot_r = 2;
        constexpr int dot_gap = 6;
        const int dots_w = page_count * dot_r * 2 + (page_count - 1) * dot_gap;
        right -= dots_w + 6;
    }
    return right - getBatteryIndicatorWidth();
}

// 电池外形：左头 + 1px 外框 + 内部分格
static void drawBatteryIndicator(const int x, const int y, const int level) {
    const int body_w = getBatteryBodyWidth();
    const int body_h = getBatteryBodyHeight();
    const int body_x = x + BATTERY_HEAD_W;
    const int total_w = getBatteryIndicatorWidth();

    M5Cardputer.Display.fillRect(x - 1, y - 1, total_w + 2, body_h + 2, BLACK);

    // 左侧电池头（正极凸起）
    const int head_y = y + (body_h - BATTERY_HEAD_H) / 2;
    M5Cardputer.Display.fillRect(x, head_y, BATTERY_HEAD_W, BATTERY_HEAD_H, WHITE);

    // 外层 1px 边框
    M5Cardputer.Display.drawRect(body_x, y, body_w, body_h, WHITE);

    const int inset = getBatteryInset();
    const int seg_x0 = body_x + inset;
    const int seg_y = y + inset;
    const int filled = constrain((level + 19) / 20, 0, BATTERY_SEGMENTS);
    for (int i = 0; i < BATTERY_SEGMENTS; i++) {
        const int sx = seg_x0 + i * (BATTERY_SEG_W + BATTERY_SEG_GAP);
        if (i < filled) {
            M5Cardputer.Display.fillRect(sx, seg_y, BATTERY_SEG_W, BATTERY_SEG_H, WHITE);
        } else {
            M5Cardputer.Display.drawRect(sx, seg_y, BATTERY_SEG_W, BATTERY_SEG_H, DARKGREY);
        }
    }
}

static void drawMenuBatteryIndicator(const int screen_w, const int page_count) {
    const int x = getMenuBatteryX(screen_w, page_count);
    const int y = (APP_HEADER_H - getBatteryBodyHeight()) / 2;
    drawBatteryIndicator(x, y, M5Cardputer.Power.getBatteryLevel());
}

// 绘制右侧 BtnA(GO) 返回按钮样式
static void drawBackButton(const int screen_w) {
    constexpr int btn_w = 36;
    constexpr int btn_h = 18;
    constexpr int btn_r = 4;
    const int btn_x = screen_w - btn_w - 2;
    const int btn_y = (APP_HEADER_H - btn_h) / 2;

    M5Cardputer.Display.fillRoundRect(btn_x, btn_y, btn_w, btn_h, btn_r, DARKGREY);
    M5Cardputer.Display.drawRoundRect(btn_x, btn_y, btn_w, btn_h, btn_r, WHITE);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, DARKGREY);
    M5Cardputer.Display.drawCenterString("GO", btn_x + btn_w / 2, btn_y + 5);
}

// 绘制底部分隔线
static void drawHeaderDivider(const int screen_w) {
    M5Cardputer.Display.drawFastHLine(0, APP_HEADER_H - 1, screen_w, DARKGREY);
}

void drawAppScreenHeader(const char* title) {
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, 0, screen_w, APP_HEADER_H, BLACK);

    // 应用名 2 倍字号，GO 按钮保持 1 倍
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(4, (APP_HEADER_H - 16) / 2);
    M5Cardputer.Display.print(title);

    drawBackButton(screen_w);
    drawHeaderDivider(screen_w);
    // GO 按钮会设置 DARKGREY 背景色，子界面绘制前恢复
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void drawMenuScreenHeader(const char* app_name, const int page, const int page_count) {
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, 0, screen_w, APP_HEADER_H, BLACK);

    const int logo_y = (APP_HEADER_H - MENU_LOGO_SIZE) / 2;
    drawAppLogo(2, logo_y, MENU_LOGO_SIZE);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(2 + MENU_LOGO_SIZE + 4, logo_y + 4);
    M5Cardputer.Display.print(app_name);

    drawMenuBatteryIndicator(screen_w, page_count);

    if (page_count > 1) {
        constexpr int dot_r = 2;
        constexpr int dot_gap = 6;
        const int dots_w = page_count * dot_r * 2 + (page_count - 1) * dot_gap;
        int dot_x = screen_w - dots_w - 4;
        const int dot_cy = APP_HEADER_H / 2;
        for (int i = 0; i < page_count; i++) {
            const int cx = dot_x + dot_r + i * (dot_r * 2 + dot_gap);
            if (i == page) {
                M5Cardputer.Display.fillCircle(cx, dot_cy, dot_r, WHITE);
            } else {
                M5Cardputer.Display.drawCircle(cx, dot_cy, dot_r, DARKGREY);
            }
        }
    }

    drawHeaderDivider(screen_w);
}

void updateMenuScreenBattery(const int page_count) {
    drawMenuBatteryIndicator(M5Cardputer.Display.width(), page_count);
}

void beginAppScreen(const char* title) {
    M5Cardputer.Display.clear();
    drawAppScreenHeader(title);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}
