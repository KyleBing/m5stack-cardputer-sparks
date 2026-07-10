#include "app_header.h"
#include "app_icons.h"
#include "app_common.h"
#include "app_connectivity.h"
#include "M5Cardputer.h"

static constexpr int MENU_LOGO_SIZE = 24;
static constexpr int HEADER_STATUS_GAP = 5;
static constexpr int APP_BACK_BTN_W = ICON_BACK_W;
static constexpr int HEADER_STATUS_CLEAR_PAD = 2;

static int headerStatusIconY(const int icon_h) {
    return (APP_HEADER_H - icon_h) / 2;
}

static int getMenuStatusRightX(const int screen_w, const int page_count) {
    int right = screen_w - 4;
    if (page_count > 1) {
        constexpr int dot_r = 2;
        constexpr int dot_gap = 6;
        const int dots_w = page_count * dot_r * 2 + (page_count - 1) * dot_gap;
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

// 绘制右侧 BtnA 返回图标：半个圆角矩形 + 左箭头
static void drawBackButton(const int screen_w) {
    constexpr int btn_w = APP_BACK_BTN_W;
    constexpr int btn_h = ICON_BACK_H;
    const int btn_x = screen_w - btn_w - 2;
    const int btn_y = (APP_HEADER_H - btn_h) / 2;
    drawIconBack(btn_x, btn_y, WHITE);
}

static void drawHeaderDivider(const int screen_w) {
    M5Cardputer.Display.drawFastHLine(0, APP_HEADER_H - 1, screen_w, DARKGREY);
}

void drawAppScreenHeader(const char* title) {
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, 0, screen_w, APP_HEADER_H, BLACK);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(4, (APP_HEADER_H - 16) / 2);
    M5Cardputer.Display.print(title);

    const int status_right = screen_w - 2 - APP_BACK_BTN_W - 4;
    drawHeaderStatusIcons(status_right, false);
    drawBackButton(screen_w);
    drawHeaderDivider(screen_w);
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

    const int status_right = getMenuStatusRightX(screen_w, page_count);
    drawHeaderStatusIcons(status_right, true);

    if (page_count > 1) {
        constexpr int dot_r = 2;
        constexpr int dot_gap = 6;
        const int dots_w = page_count * dot_r * 2 + (page_count - 1) * dot_gap;
        const int dot_x = screen_w - dots_w - 4;
        drawIconPageDots(dot_x, APP_HEADER_H / 2, page, page_count);
    }

    drawHeaderDivider(screen_w);
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
    drawHeaderDivider(screen_w);
    prev_clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (prev_clear_left < 0) {
        prev_clear_left = 0;
    }
}

void updateAppHeaderStatus() {
    static int prev_clear_left = -1;
    const int screen_w = M5Cardputer.Display.width();
    const int status_right = screen_w - 2 - APP_BACK_BTN_W - 4;
    const bool wifi = isWifiStaConnected();
    const bool ble = isBleStackReady();
    const int left_x = headerStatusLeftX(status_right, false, wifi, ble, false);
    int clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (clear_left < 0) {
        clear_left = 0;
    }
    if (prev_clear_left >= 0 && prev_clear_left < clear_left) {
        clear_left = prev_clear_left;
    }
    clearHeaderStatusArea(clear_left, status_right);
    drawHeaderStatusIcons(status_right, false);
    drawHeaderDivider(screen_w);
    prev_clear_left = left_x - HEADER_STATUS_CLEAR_PAD;
    if (prev_clear_left < 0) {
        prev_clear_left = 0;
    }
}

void updateMenuScreenBattery(const int page_count) {
    updateMenuHeaderStatus(page_count);
}

void beginAppScreen(const char* title) {
    M5Cardputer.Display.clear();
    drawAppScreenHeader(title);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void clearAppContentArea() {
    const int screen_w = M5Cardputer.Display.width();
    const int screen_h = M5Cardputer.Display.height();
    M5Cardputer.Display.fillRect(0, APP_CONTENT_Y, screen_w, screen_h - APP_CONTENT_Y, BLACK);
}
