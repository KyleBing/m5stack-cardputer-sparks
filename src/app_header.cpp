#include "app_header.h"
#include "app_icons.h"
#include "app_common.h"
#include "app_connectivity.h"
#include "app_hid_keyboard.h"
#include "M5Cardputer.h"

static constexpr int MENU_LOGO_SIZE = 24;
static constexpr int HEADER_STATUS_GAP = 5;
static constexpr int APP_BACK_BTN_W = ICON_BACK_W;
static constexpr int HEADER_STATUS_CLEAR_PAD = 2;
static bool s_app_header_draw_divider = true;
static bool s_app_header_include_battery = false;
// 子界面 header 分页圆点（hub 页用）；page_count <= 1 表示不显示
static int s_app_header_page = 0;
static int s_app_header_page_count = 1;
static constexpr int HEADER_DOT_R = 2;
static constexpr int HEADER_DOT_GAP = 6;

static int headerStatusIconY(const int icon_h) {
    // 图标几何中心对齐 header 垂直中线（避免奇偶高度差 1px）
    return APP_HEADER_H / 2 - icon_h / 2;
}

static int headerPageDotsWidth(const int page_count) {
    if (page_count <= 1) {
        return 0;
    }
    return page_count * HEADER_DOT_R * 2 + (page_count - 1) * HEADER_DOT_GAP;
}

static int getMenuStatusRightX(const int screen_w, const int page_count) {
    int right = screen_w - 4;
    const int dots_w = headerPageDotsWidth(page_count);
    if (dots_w > 0) {
        right -= dots_w + 6;
    }
    return right;
}

// 子界面分页圆点画在返回图标左侧
static int getAppPageDotsX(const int screen_w) {
    return screen_w - 2 - APP_BACK_BTN_W - 4 - headerPageDotsWidth(s_app_header_page_count);
}

// 子界面状态图标右边界；有分页圆点时为其让位
static int getAppStatusRightX(const int screen_w) {
    int right = screen_w - 2 - APP_BACK_BTN_W - 4;
    const int dots_w = headerPageDotsWidth(s_app_header_page_count);
    if (dots_w > 0) {
        right -= dots_w + 6;
    }
    return right;
}

static int getHeaderStatusWidth(const bool include_battery, const bool wifi, const bool ble,
                                const bool charging) {
    int w = 0;
    if (include_battery) {
        w += getIconBatteryDisplayWidth(charging);
    }
    if (wifi) {
        w += (w > 0 ? HEADER_STATUS_GAP : 0) + ICON_WIFI_W;
    }
    if (ble) {
        w += (w > 0 ? HEADER_STATUS_GAP : 0) + ICON_BLE_W;
    }
    return w;
}

// 计算状态图标区最左 x（与 drawHeaderStatusIcons 布局一致）
static int headerStatusLeftX(const int status_right, const bool include_battery, const bool wifi,
                             const bool ble, const bool charging) {
    int x = status_right;
    if (include_battery) {
        x -= getIconBatteryDisplayWidth(charging);
    }
    if (wifi) {
        x -= HEADER_STATUS_GAP + ICON_WIFI_W;
    }
    if (ble) {
        x -= HEADER_STATUS_GAP + ICON_BLE_W;
    }
    return x;
}

// 从右向左绘制连接状态图标，在 header 内垂直居中
static int drawHeaderStatusIcons(const int right_x, const bool include_battery) {
    const bool wifi = isWifiStaConnected();
    const bool ble = isBleStackReady();
    const bool charging = isBatteryCharging();
    const int body_h = getIconBatteryBodyHeight();

    int x = right_x;
    if (include_battery) {
        x -= getIconBatteryDisplayWidth(charging);
        drawIconBattery(x, headerStatusIconY(body_h), M5Cardputer.Power.getBatteryLevel(),
                        charging);
    }
    if (wifi) {
        x -= HEADER_STATUS_GAP + ICON_WIFI_W;
        drawIconWifi(x, headerStatusIconY(ICON_WIFI_H), getWifiStaRssi(), WHITE);
    }
    if (ble) {
        x -= HEADER_STATUS_GAP + ICON_BLE_W;
        drawIconBle(x, headerStatusIconY(ICON_BLE_H), WHITE);
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

// btngo：绘制右侧返回图标（半个圆角矩形 + 左箭头）
static void drawBackButton(const int screen_w) {
    constexpr int btn_w = APP_BACK_BTN_W;
    constexpr int btn_h = ICON_BACK_H;
    const int btn_x = screen_w - btn_w - 2;
    const int btn_y = (APP_HEADER_H - btn_h) / 2;
    drawIconBack(btn_x, btn_y, WHITE);
}

static void drawHeaderDivider(const int screen_w) {
    // header 底边框 #222222
    M5Cardputer.Display.drawFastHLine(0, APP_HEADER_H - 1, screen_w,
                                      M5Cardputer.Display.color565(0x22, 0x22, 0x22));
}

// page_count <= 1 时不画圆点，同时清掉上个界面残留的分页状态
static void drawAppHeaderCore(const char* title, const char* accent, const uint16_t accent_color,
                              const bool draw_divider, const int page, const int page_count) {
    s_app_header_draw_divider = draw_divider;
    s_app_header_page = page;
    s_app_header_page_count = page_count;
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, 0, screen_w, APP_HEADER_H, BLACK);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(4, (APP_HEADER_H - 16) / 2);
    M5Cardputer.Display.print(title);
    if (accent != nullptr && accent[0] != '\0') {
        M5Cardputer.Display.setTextColor(accent_color, BLACK);
        M5Cardputer.Display.print(accent);
    }

    if (page_count > 1) {
        drawIconPageDots(getAppPageDotsX(screen_w), APP_HEADER_H / 2, page, page_count);
    }
    drawHeaderStatusIcons(getAppStatusRightX(screen_w), s_app_header_include_battery);
    drawBackButton(screen_w);
    if (draw_divider) {
        drawHeaderDivider(screen_w);
    }
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void drawAppScreenHeader(const char* title, const bool draw_divider) {
    drawAppScreenHeaderAccent(title, nullptr, WHITE, draw_divider);
}

void drawAppScreenHeaderAccent(const char* title, const char* accent, const uint16_t accent_color,
                               const bool draw_divider) {
    drawAppHeaderCore(title, accent, accent_color, draw_divider, 0, 1);
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

    const int status_right = getMenuStatusRightX(screen_w, page_count);
    drawHeaderStatusIcons(status_right, true);

    if (page_count > 1) {
        const int dot_x = screen_w - headerPageDotsWidth(page_count) - 4;
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
    const int dot_x = screen_w - dots_w - 4;
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

void updateAppHeaderStatus() {
    // Keyboard 主界面 / Hosts / 退出 Exiting 期间禁止刷蓝牙等图标
    if (hidKeyboardSuppressesHeader()) {
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
    M5Cardputer.Display.clear();
    drawAppHeaderCore(title, nullptr, WHITE, false, page, page_count);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    fillAppContentArea(content_bg);
}
