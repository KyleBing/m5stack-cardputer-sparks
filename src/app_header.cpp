#include "app_header.h"
#include "app_icons.h"
#include "app_common.h"
#include "app_connectivity.h"
#include "app_cursor.h"
#include "app_hid_keyboard.h"
#include "app_ir.h"
#include "app_mijia.h"
#include "app_mijia_ui.h"
#include "app_web.h"
#include "M5Cardputer.h"
#include <cstring>

static constexpr int MENU_LOGO_SIZE = 24;
static constexpr int HEADER_STATUS_CLEAR_PAD = 2;
// 右侧图标/分页 indicator 相对屏幕右缘的最小间距
static constexpr int HEADER_RIGHT_PAD = 8;
static bool s_app_header_draw_divider = true;
static bool s_app_header_include_battery = false;
// 共享 header 已绘制 → 允许定时刷新状态图标（opt-in）
static bool s_app_header_status_refresh = false;
// 子界面 header 分页 indicator（hub 页用）；page_count <= 1 表示不显示
static int s_app_header_page = 0;
static int s_app_header_page_count = 1;
// 米家控制页设备 indicator；count <= 0 表示不显示
static int s_app_header_pager_idx = 0;
static int s_app_header_pager_count = 0;
static int headerStatusIconY(const int icon_h) {
    // 图标几何中心对齐 header 垂直中线（避免奇偶高度差 1px）
    return APP_HEADER_H / 2 - icon_h / 2;
}

static int headerPageDotsWidth(const int page_count) {
    return getIconPageDotsWidth(page_count);
}

static int getMenuStatusRightX(const int screen_w, const int page_count) {
    int right = screen_w - HEADER_RIGHT_PAD;
    const int dots_w = headerPageDotsWidth(page_count);
    if (dots_w > 0) {
        // 状态图标与分页圆点间距
        right -= dots_w + APP_HEADER_ICON_GAP;
    }
    return right;
}

// 子界面分页圆点靠右对齐
static int getAppPageDotsX(const int screen_w) {
    return screen_w - HEADER_RIGHT_PAD - headerPageDotsWidth(s_app_header_page_count);
}

// 子界面状态图标右边界；有分页圆点时为其让位
static int getAppStatusRightX(const int screen_w) {
    int right = screen_w - HEADER_RIGHT_PAD;
    const int dots_w = headerPageDotsWidth(s_app_header_page_count);
    if (dots_w > 0) {
        right -= dots_w + APP_HEADER_ICON_GAP;
    }
    return right;
}

// 状态图标区总宽（仅在已放置图标之间插入 APP_HEADER_ICON_GAP）
// 顺序（右→左）：battery? / wifi / ble（设备 indicator 在左侧标题旁）
static int getHeaderStatusWidth(const bool include_battery, const bool wifi, const bool ble,
                                const bool charging) {
    int w = 0;
    if (include_battery) {
        w += getIconBatteryDisplayWidth(charging);
    }
    if (wifi) {
        w += (w > 0 ? APP_HEADER_ICON_GAP : 0) + ICON_WIFI_W;
    }
    if (ble) {
        w += (w > 0 ? APP_HEADER_ICON_GAP : 0) + ICON_BLE_W;
    }
    return w;
}

// 计算状态图标区最左 x（与 drawHeaderStatusIcons 布局一致）
static int headerStatusLeftX(const int status_right, const bool include_battery, const bool wifi,
                             const bool ble, const bool charging) {
    return status_right - getHeaderStatusWidth(include_battery, wifi, ble, charging);
}

// 从右向左绘制连接状态图标，在 header 内垂直居中
static int drawHeaderStatusIcons(const int right_x, const bool include_battery) {
    const bool wifi = isWifiStaConnected();
    const bool ble = isBleStackReady();
    const bool charging = isBatteryCharging();
    const int body_h = getIconBatteryBodyHeight();

    int x = right_x;
    bool placed = false;
    if (include_battery) {
        x -= getIconBatteryDisplayWidth(charging);
        drawIconBattery(x, headerStatusIconY(body_h), M5Cardputer.Power.getBatteryLevel(),
                        charging);
        placed = true;
    }
    // STA 已连接时显示信号强度
    if (wifi) {
        x -= (placed ? APP_HEADER_ICON_GAP : 0) + ICON_WIFI_W;
        drawIconWifi(x, headerStatusIconY(ICON_WIFI_H), getWifiStaRssi(), WHITE);
        placed = true;
    }
    if (ble) {
        x -= (placed ? APP_HEADER_ICON_GAP : 0) + ICON_BLE_W;
        drawIconBle(x, headerStatusIconY(ICON_BLE_H), WHITE);
        placed = true;
    }
    return x;
}

// 仅清除分割线以上区域，避免盖住底边线
static void clearHeaderStatusArea(const int left_x, const int right_x) {
    if (right_x <= left_x) {
        return;
    }
    M5Cardputer.Display.fillRect(left_x, 0, right_x - left_x, APP_HEADER_H - 1, BLACK);
}

static void drawHeaderDivider(const int screen_w) {
    // header 底边框 #3A3A3A
    M5Cardputer.Display.drawFastHLine(0, APP_HEADER_H - 1, screen_w,
                                      M5Cardputer.Display.color565(0x3A, 0x3A, 0x3A));
}

// page_count <= 1 时不画圆点，同时清掉上个界面残留的分页状态
static void drawAppHeaderCore(const char* title, const char* accent, const uint16_t accent_color,
                              const bool draw_divider, const int page, const int page_count) {
    s_app_header_status_refresh = true;
    s_app_header_draw_divider = draw_divider;
    s_app_header_page = page;
    s_app_header_page_count = page_count;
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, 0, screen_w, APP_HEADER_H, BLACK);

    const int status_right = getAppStatusRightX(screen_w);
    const bool wifi = isWifiStaConnected();
    const bool ble = isBleStackReady();
    const bool charging = isBatteryCharging();
    const int status_left =
        headerStatusLeftX(status_right, s_app_header_include_battery, wifi, ble, charging);

    // 左上角：设备 indicator，再画设备名
    constexpr int left_pad = 4;
    constexpr int pager_title_gap = 6; // indicator 与名称间隔
    int title_x = left_pad;
    const int pager_w = mijiaDevicePagerWidth(s_app_header_pager_count);
    const int pager_h = mijiaDevicePagerHeight(s_app_header_pager_count);
    if (pager_w > 0) {
        // 相对 header 垂直居中再上移 2px
        drawMijiaDevicePager(left_pad, headerStatusIconY(pager_h) - 2, s_app_header_pager_idx,
                             s_app_header_pager_count);
        title_x = left_pad + pager_w + pager_title_gap;
    }
    // 标题与状态区之间留 6px，超长则截断
    const int title_max_w = max(0, status_left - title_x - 6);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(title_x, (APP_HEADER_H - 16) / 2);
    if (title != nullptr && title[0] != '\0') {
        char clipped[48];
        strncpy(clipped, title, sizeof(clipped) - 1);
        clipped[sizeof(clipped) - 1] = '\0';
        while (clipped[0] != '\0' && M5Cardputer.Display.textWidth(clipped) > title_max_w) {
            clipped[strlen(clipped) - 1] = '\0';
        }
        M5Cardputer.Display.print(clipped);
        if (accent != nullptr && accent[0] != '\0') {
            // 后缀也截到剩余宽度
            const int used = M5Cardputer.Display.textWidth(clipped);
            const int accent_max = max(0, title_max_w - used);
            char accent_clipped[24];
            strncpy(accent_clipped, accent, sizeof(accent_clipped) - 1);
            accent_clipped[sizeof(accent_clipped) - 1] = '\0';
            while (accent_clipped[0] != '\0' &&
                   M5Cardputer.Display.textWidth(accent_clipped) > accent_max) {
                accent_clipped[strlen(accent_clipped) - 1] = '\0';
            }
            M5Cardputer.Display.setTextColor(accent_color, BLACK);
            M5Cardputer.Display.print(accent_clipped);
        }
    }

    if (page_count > 1) {
        drawIconPageDots(getAppPageDotsX(screen_w), APP_HEADER_H / 2, page, page_count);
    }
    drawHeaderStatusIcons(status_right, s_app_header_include_battery);
    if (draw_divider) {
        drawHeaderDivider(screen_w);
    }
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void clearAppHeaderDevicePager() {
    s_app_header_pager_idx = 0;
    s_app_header_pager_count = 0;
}

static void setAppHeaderDevicePager(const int device_idx, const int device_count) {
    s_app_header_pager_idx = device_idx;
    s_app_header_pager_count = device_count > 0 ? device_count : 0;
}

void drawAppScreenHeader(const char* title, const bool draw_divider) {
    clearAppHeaderDevicePager();
    drawAppScreenHeaderAccent(title, nullptr, WHITE, draw_divider);
}

void drawAppScreenHeaderAccent(const char* title, const char* accent, const uint16_t accent_color,
                               const bool draw_divider) {
    clearAppHeaderDevicePager();
    drawAppHeaderCore(title, accent, accent_color, draw_divider, 0, 1);
}

void drawAppScreenHeaderWithDevicePager(const char* title, const int device_idx,
                                        const int device_count, const bool draw_divider) {
    setAppHeaderDevicePager(device_idx, device_count);
    drawAppHeaderCore(title, nullptr, WHITE, draw_divider, 0, 1);
}

void beginAppScreenWithDevicePager(const char* title, const int device_idx, const int device_count,
                                   const bool draw_divider) {
    s_app_header_include_battery = false;
    M5Cardputer.Display.clear();
    drawAppScreenHeaderWithDevicePager(title, device_idx, device_count, draw_divider);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void drawMenuScreenHeader(const char* app_name, const int page, const int page_count) {
    s_app_header_status_refresh = true;
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, 0, screen_w, APP_HEADER_H, BLACK);

    const int logo_y = (APP_HEADER_H - MENU_LOGO_SIZE) / 2;
    drawAppLogo(2, logo_y, MENU_LOGO_SIZE);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(2 + MENU_LOGO_SIZE + 4, logo_y + 4);
    M5Cardputer.Display.print(app_name);

    const int status_right = getMenuStatusRightX(screen_w, page_count);
    drawHeaderStatusIcons(status_right, true);

    if (page_count > 1) {
        const int dot_x = screen_w - headerPageDotsWidth(page_count) - HEADER_RIGHT_PAD;
        drawIconPageDots(dot_x, APP_HEADER_H / 2, page, page_count);
    }
    // 主菜单 header 不画下边框
}

// 仅重绘分页圆点，避免翻页时擦黑扫过电池
void updateMenuPageDots(const int page, const int page_count) {
    if (page_count <= 1) {
        return;
    }
    const int screen_w = M5Cardputer.Display.width();
    const int dots_w = headerPageDotsWidth(page_count);
    const int dot_x = screen_w - dots_w - HEADER_RIGHT_PAD;
    // 只清圆点区域
    M5Cardputer.Display.fillRect(dot_x - 1, 0, dots_w + 2, APP_HEADER_H, BLACK);
    drawIconPageDots(dot_x, APP_HEADER_H / 2, page, page_count);
}

void updateMenuHeaderStatus(const int page_count) {
    static int prev_clear_left = -1;
    const int screen_w = M5Cardputer.Display.width();
    const int status_right = getMenuStatusRightX(screen_w, page_count);
    const bool wifi = isWifiStaConnected();
    const bool ble = isBleStackReady();
    const bool charging = isBatteryCharging();
    const int left_x = headerStatusLeftX(status_right, true, wifi, ble, charging);
    int clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (clear_left < 0) {
        clear_left = 0;
    }
    if (prev_clear_left >= 0 && prev_clear_left < clear_left) {
        clear_left = prev_clear_left;
    }
    clearHeaderStatusArea(clear_left, status_right);
    drawHeaderStatusIcons(status_right, true);
    prev_clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (prev_clear_left < 0) {
        prev_clear_left = 0;
    }
}

void clearAppHeaderStatusRefresh() {
    s_app_header_status_refresh = false;
}

bool isAppHeaderStatusRefreshEnabled() {
    if (!s_app_header_status_refresh) {
        return false;
    }
    // 同 app 内临时全屏（Help 外遥控 / 快捷键编辑 / Config UI / 灭屏等）
    if (hidKeyboardSuppressesHeader() || mijiaAppSuppressesHeader() || webAppSuppressesHeader() ||
        irAppSuppressesHeader() || isCursorDisplayBlanked()) {
        return false;
    }
    return true;
}

void updateAppHeaderStatus() {
    if (!isAppHeaderStatusRefreshEnabled()) {
        return;
    }
    static int prev_clear_left = -1;
    const int screen_w = M5Cardputer.Display.width();
    const int status_right = getAppStatusRightX(screen_w);
    const bool wifi = isWifiStaConnected();
    const bool ble = isBleStackReady();
    const bool charging = isBatteryCharging();
    const int left_x =
        headerStatusLeftX(status_right, s_app_header_include_battery, wifi, ble, charging);
    int clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (clear_left < 0) {
        clear_left = 0;
    }
    if (prev_clear_left >= 0 && prev_clear_left < clear_left) {
        clear_left = prev_clear_left;
    }
    clearHeaderStatusArea(clear_left, status_right);
    drawHeaderStatusIcons(status_right, s_app_header_include_battery);
    if (s_app_header_draw_divider) {
        drawHeaderDivider(screen_w);
    }
    prev_clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (prev_clear_left < 0) {
        prev_clear_left = 0;
    }
}

void updateMenuScreenBattery(const int page_count) {
    updateMenuHeaderStatus(page_count);
}

void beginAppScreen(const char* title, const bool draw_divider) {
    s_app_header_include_battery = false;
    M5Cardputer.Display.clear();
    drawAppScreenHeader(title, draw_divider);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void beginAppScreenWithBattery(const char* title, const bool draw_divider) {
    s_app_header_include_battery = true;
    M5Cardputer.Display.clear();
    drawAppScreenHeader(title, draw_divider);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void beginAppScreenAccentWithBattery(const char* title, const char* accent,
                                     const uint16_t accent_color, const bool draw_divider) {
    s_app_header_include_battery = true;
    M5Cardputer.Display.clear();
    drawAppScreenHeaderAccent(title, accent, accent_color, draw_divider);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void drawAppScreenHeaderWithBattery(const char* title, const bool draw_divider) {
    s_app_header_include_battery = true;
    drawAppScreenHeader(title, draw_divider);
}

void drawAppScreenHeaderAccentWithBattery(const char* title, const char* accent,
                                          const uint16_t accent_color, const bool draw_divider) {
    s_app_header_include_battery = true;
    drawAppScreenHeaderAccent(title, accent, accent_color, draw_divider);
}

void beginAppScreenAccent(const char* title, const char* accent, const uint16_t accent_color,
                          const bool draw_divider) {
    s_app_header_include_battery = false;
    M5Cardputer.Display.clear();
    drawAppScreenHeaderAccent(title, accent, accent_color, draw_divider);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void clearAppContentArea() {
    fillAppContentArea(BLACK);
}

void fillAppContentArea(const uint16_t color) {
    const int screen_w = M5Cardputer.Display.width();
    const int screen_h = M5Cardputer.Display.height();
    const int h = screen_h - APP_CONTENT_Y;
    if (h > 0) {
        M5Cardputer.Display.fillRect(0, APP_CONTENT_Y, screen_w, h, color);
    }
}

void beginAppHubScreen(const char* title, const uint16_t content_bg, const int page,
                       const int page_count) {
    s_app_header_include_battery = false;
    clearAppHeaderDevicePager();
    M5Cardputer.Display.clear();
    drawAppHeaderCore(title, nullptr, WHITE, false, page, page_count);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    fillAppContentArea(content_bg);
}
