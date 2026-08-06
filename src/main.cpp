#include "M5Cardputer.h"
#include "app_config.h"
#include "app_version.h"
#include "app_icons.h"
#include "app_device_icons.h"
#include "app_header.h"
#include "app_common.h"
#include "app_web.h"
#include "app_wifi.h"
#include "app_mijia.h"
#include "app_mijia_ui.h"
#include "mijia_control.h"
#include "app_ble.h"
#include "app_connectivity.h"
#include "app_rtc.h"
#include "app_countdown.h"
#include "app_icon_demo.h"
#include "app_cursor.h"
#include "app_morse.h"
#include "app_ir.h"
#include "app_font_demo.h"
#include "app_mic.h"
#include "app_neon_fx.h"
#include "app_dice.h"
#include "app_newton_cradle.h"
#include "app_games.h"
#include "app_battery.h"
#include "app_info.h"
#include "app_hid_keyboard.h"
#include "app_calendar.h"
#include "app_screenshot.h"
#include "app_ac_auto.h"
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <driver/rtc_io.h>
#include <esp_system.h>
#include <cmath>



// ===== COMMON =====

struct VersionInfo {
    const String version;
    const String update_time;
    const String author;
    const String github;
    const String email;
    const String website;
};

// 应用状态
enum class AppState {
    MENU,
    VERSION,
    KEYBOARD,
    BMI,
    MIC,
    NEON_FX,
    DICE,
    NEWTON_CRADLE,
    GAMES,
    HARDWARE_TESTS,
    SETTINGS,
    RTC,
    IN_I2C,
    EX_I2C,
    WIFI,
    BLE,
    DISP,
    ICONS,
    SLEEP,
    MIJIA,
    WEB,
    CURSOR,
    MORSE,
    IR,
    FONT_DEMO,
    LED,
    BATTERY,
    HID_KEYBOARD,
    INFO, // 系统信息 / 内存（字母 i）
    CALENDAR,
    AC_AUTO, // 空调自动化
};

enum class HardwareTestMode {
    HUB,
    SCREEN,
    IMU,
    FONT,
    ICONS,
    LED,
    BLE,
    IN_I2C,
    EX_I2C,
    MIC, // 麦克风波形（原主菜单 r）
};

struct MenuItem {
    char key;
    const char* name;
    const char* name_full;
    AppState state;
};


// Cardputer 技能 → 字母入口
static const MenuItem MENU_ITEMS[] = {
    // 常用 app（菜单显示 name_full，全大写）
    {'m', "Mij", "MIJIA", AppState::MIJIA},
    {'u', "Cfg", "CONFIG", AppState::WEB},
    {'w', "WiFi", "WIFI", AppState::WIFI},
    {'t', "Time", "TIME", AppState::RTC},
    {'s', "Slp", "SLEEP", AppState::SLEEP},
    {'o', "Opt", "OPTIONS", AppState::SETTINGS},
    {'i', "Inf", "INFO", AppState::INFO},
    {'p', "Bat", "BATTERY", AppState::BATTERY},
    {'c', "Cur", "CURSOR", AppState::CURSOR},
    {'a', "Cal", "CALENDAR", AppState::CALENDAR},
    {'v', "Ver", "VERSION", AppState::VERSION},
    {'j', "Mor", "MORSE", AppState::MORSE},
    {'x', "IR", "INFRARED", AppState::IR},
    {'n', "AC", "AC AUTO", AppState::AC_AUTO},

    // 系统功能测试
    {'k', "KB", "KEYBOARD", AppState::HID_KEYBOARD},
    {'g', "Game", "MINI GAMES", AppState::GAMES},
    {'h', "Test", "HARDWARE TEST", AppState::HARDWARE_TESTS},
};

static const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

AppState currentState = AppState::MENU;
static HardwareTestMode hardwareTestMode = HardwareTestMode::HUB;
static int hardwareTestHubPage = 0;
static bool bmiScreenReady = false;
static int bmiPrevDotX[2] = {-1, -1};
static int bmiPrevDotY[2] = {-1, -1};

void enterApp(const AppState state);

// 根据字母查找 app（支持大小写）
bool enterAppByKey(const char key) {
    const char keyLower = (key >= 'A' && key <= 'Z') ? static_cast<char>(key - 'A' + 'a') : key;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (MENU_ITEMS[i].key == keyLower) {
            enterApp(MENU_ITEMS[i].state);
            return true;
        }
    }
    return false;
}

// ===== MENU =====

static constexpr const char* APP_NAME = "Sparks";

// 按 AppState 取菜单长名（用于子界面 header）
const char* getMenuItemNameFull(const AppState state) {
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (MENU_ITEMS[i].state == state) {
            return MENU_ITEMS[i].name_full;
        }
    }
    return "?";
}

// 截图文件名短名：app_<slug>_NNN.bmp
const char* getCurrentAppShotSlug() {
    switch (currentState) {
        case AppState::MENU:
            return "menu";
        case AppState::VERSION:
            return "version";
        case AppState::KEYBOARD:
            return "keyboard";
        case AppState::BMI:
            return "imu";
        case AppState::MIC:
            return "mic";
        case AppState::NEON_FX:
            return "neonfx";
        case AppState::DICE:
            return "dice";
        case AppState::NEWTON_CRADLE:
            return "newton";
        case AppState::GAMES:
            return "games";
        case AppState::HARDWARE_TESTS:
            // Mic 在 Test 子项里时用 mic 截图名
            if (hardwareTestMode == HardwareTestMode::MIC) {
                return "mic";
            }
            return "hardware";
        case AppState::SETTINGS:
            return "options";
        case AppState::RTC:
            return "time";
        case AppState::IN_I2C:
            return "ini2c";
        case AppState::EX_I2C:
            return "exi2c";
        case AppState::WIFI:
            return "wifi";
        case AppState::BLE:
            return "ble";
        case AppState::DISP:
            return "display";
        case AppState::ICONS:
            return "icons";
        case AppState::SLEEP:
            return "sleep";
        case AppState::MIJIA:
            return "mijia";
        case AppState::WEB:
            return "config";
        case AppState::CURSOR:
            return "cursor";
        case AppState::MORSE:
            return "morse";
        case AppState::IR:
            return "ir";
        case AppState::FONT_DEMO:
            return "font";
        case AppState::LED:
            return "led";
        case AppState::BATTERY:
            return "battery";
        case AppState::HID_KEYBOARD:
            return "hidkeyboard";
        case AppState::INFO:
            return "info";
        case AppState::CALENDAR:
            return "calendar";
        case AppState::AC_AUTO:
            return "acauto";
        default:
            return "unknown";
    }
}
static constexpr int MENU_COLS = APP_HUB_CARD_COLS;
static constexpr int MENU_ROWS_PER_PAGE = 4;
static constexpr int MENU_ITEMS_PER_PAGE = MENU_COLS * MENU_ROWS_PER_PAGE;

static int menuPage = 0;
static bool menuNoAppPrompt = false;

// 计算菜单总页数
int getMenuPageCount() {
    return (MENU_ITEM_COUNT + MENU_ITEMS_PER_PAGE - 1) / MENU_ITEMS_PER_PAGE;
}

// 主页菜单五彩强调色（按全局序号循环，翻页颜色稳定）
static uint16_t menuAccentColor(const int index) {
    static const uint8_t kColors[][3] = {
        {0xE9, 0xC4, 0x6A}, // gold
        {0xB0, 0x6C, 0xFF}, // purple
        {0xFF, 0x9D, 0x3F}, // orange
        {0xFF, 0x5E, 0x68}, // coral
        {0x56, 0xA8, 0xFF}, // blue
        {0x42, 0xD3, 0x92}, // green
        {0xFF, 0x7A, 0xC8}, // pink
        {0x3D, 0xC4, 0xBF}, // teal
    };
    constexpr int n = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
    const uint8_t* c = kColors[((index % n) + n) % n];
    return M5Cardputer.Display.color565(c[0], c[1], c[2]);
}

static uint16_t menuHubBg() {
    return M5Cardputer.Display.color565(0x05, 0x08, 0x0D);
}

static uint16_t menuCardBg() {
    return M5Cardputer.Display.color565(0x0D, 0x16, 0x22);
}

static uint16_t menuTitleColor() {
    return M5Cardputer.Display.color565(0xF4, 0xF1, 0xE8);
}

// 无对应 app 时在菜单态居中提示（保留主菜单 header，不显示子界面返回键）
static void showMenuNoAppPrompt(const char key) {
    menuNoAppPrompt = true;
    currentState = AppState::MENU;

    const int page_count = getMenuPageCount();
    M5Cardputer.Display.clear();
    drawMenuScreenHeader(APP_NAME, menuPage, page_count);

    char msg[20];
    snprintf(msg, sizeof(msg), "No app: %c", static_cast<char>(toupper(static_cast<unsigned char>(key))));

    const int center_x = M5Cardputer.Display.width() / 2;
    const int content_h = M5Cardputer.Display.height() - APP_CONTENT_Y;
    constexpr int line_h = INFO_LINE_H_2X;
    constexpr int hint_line_h = INFO_LINE_H;
    const int block_h = line_h + 4 + hint_line_h;
    const int text_y = APP_CONTENT_Y + (content_h - block_h) / 2;

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(RED, BLACK);
    M5Cardputer.Display.drawCenterString(msg, center_x, text_y);

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
    // btngo：返回主菜单提示
    char hint[24];
    snprintf(hint, sizeof(hint), "%s: menu", btnGoHintLabel());
    M5Cardputer.Display.drawCenterString(hint, center_x, text_y + line_h + 4);
}

// Games/Test 风格卡片：彩色字母徽章 + 全名
static void drawMenuItemAt(const int x, const int y, const MenuItem& item, const int index) {
    const uint16_t accent = menuAccentColor(index);
    const uint16_t card_bg = menuCardBg();
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(item.key)));

    M5Cardputer.Display.fillRoundRect(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, 4, accent);
    M5Cardputer.Display.fillRoundRect(x + 3, y + 3, 18, 16, 3, accent); // 整块序号徽章左移 1px
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, accent);
    M5Cardputer.Display.setCursor(x + 9, y + 7);
    M5Cardputer.Display.print(letter);
    M5Cardputer.Display.setTextColor(menuTitleColor(), card_bg);
    M5Cardputer.Display.setCursor(x + 28, y + 7);
    M5Cardputer.Display.print(item.name_full);
}

// 绘制主菜单当前页（2 列卡片，五彩强调色）
void drawMenuPage() {
    const int startIdx = menuPage * MENU_ITEMS_PER_PAGE;
    const int endIdx = (startIdx + MENU_ITEMS_PER_PAGE < MENU_ITEM_COUNT)
                           ? startIdx + MENU_ITEMS_PER_PAGE
                           : MENU_ITEM_COUNT;

    // 背景从 header 下沿铺满（含 padding 带）
    fillAppContentArea(menuHubBg());

    int row = 0;
    for (int i = startIdx; i < endIdx; i += MENU_COLS) {
        const int y = APP_HUB_CARD_ORIGIN_Y + row * (APP_HUB_CARD_H + APP_HUB_CARD_GAP_Y);
        for (int col = 0; col < MENU_COLS; ++col) {
            const int idx = i + col;
            if (idx >= endIdx) {
                break;
            }
            const int x = APP_HUB_CARD_ORIGIN_X + col * (APP_HUB_CARD_W + APP_HUB_CARD_GAP_X);
            drawMenuItemAt(x, y, MENU_ITEMS[idx], idx);
        }
        row++;
    }
}

// 离开 LED 测试页时关灯并恢复背光
static void leaveLedApp();

// 绘制主菜单（header + 可翻页菜单区）
void showMenu() {
    flushSpeakerVolumeSave(); // 离开 Options 等界面时落盘未写完的音量
    menuNoAppPrompt = false;
    leaveCursorApp();
    leaveNeonFxApp();
    leaveDiceApp();
    leaveNewtonCradleApp();
    leaveGamesApp();
    // Morse leave 会卸喇叭；仅真正离开 Morse 时调用，避免 Test/Game 回主页破音
    if (currentState == AppState::MORSE) {
        leaveMorseApp();
    }
    leaveLedApp();
    leaveHidKeyboardApp();
    leaveIrApp(); // 释放红外图标 RAM 缓存
    leaveAcAutoApp();
    // leaveCountdownApp 不再停后台计时；到期由 poll 弹窗
    leaveMijiaApp();
    stopConfigWebServer();
    releaseConfigWifi();
    currentState = AppState::MENU;
    const int pageCount = getMenuPageCount();
    if (menuPage >= pageCount) {
        menuPage = 0;
    }

    M5Cardputer.Display.clear();
    drawMenuScreenHeader(APP_NAME, menuPage, getMenuPageCount());
    drawMenuPage();
}

// 方向键 / [ ] 翻页，返回 true 表示已处理
bool handleMenuPageNav(const Keyboard_Class::KeysState& status) {
    int delta = getMenuNavDelta(status);
    if (delta == 0) {
        delta = getBracketNavDelta(status);
    }
    if (delta == 0) {
        return false;
    }

    const int pageCount = getMenuPageCount();
    menuPage = (menuPage + delta + pageCount) % pageCount;
    // 局部刷新：不清整屏，避免 header 擦黑扫过电池时闪出竖线
    menuNoAppPrompt = false;
    clearAppContentArea();
    drawMenuPage();
    updateMenuPageDots(menuPage, pageCount);
    return true;
}

// 菜单按键
void handleMenuKey(const String& key) {
    // 休眠唤醒后可能残留鬼键导致多字符；取第一个字母
    char c = '\0';
    for (unsigned i = 0; i < key.length(); i++) {
        const char ch = key[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            c = ch;
            break;
        }
    }
    if (c == '\0') {
        return;
    }

    if (!enterAppByKey(c)) {
        showMenuNoAppPrompt(c);
    }
}

// ===== VERSION =====

// 返回固件版本信息（常量见 app_version.h）
VersionInfo getVersionInfo() {
    return VersionInfo{
        APP_VERSION,
        APP_UPDATE_TIME,
        APP_AUTHOR,
        APP_GITHUB,
        APP_EMAIL,
        APP_WEBSITE,
    };
}

// RGB888 转 RGB565
static uint16_t versionColor565(const uint32_t rgb) {
    return M5Cardputer.Display.color565(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                                       static_cast<uint8_t>((rgb >> 8) & 0xFF),
                                       static_cast<uint8_t>(rgb & 0xFF));
}

// 单朵烟花：放射线 + 拖尾火花 + 内层短芒
static void drawFireworkBurst(const int cx, const int cy, const uint16_t color,
                              const uint16_t glow_color) {
    const int core_r = random(1, 3);
    M5Cardputer.Display.fillCircle(cx, cy, core_r, color);

    const int ray_count = random(10, 17);
    for (int r = 0; r < ray_count; r++) {
        const float angle =
            (static_cast<float>(r) * 6.2831853f / ray_count) + random(-25, 26) / 100.0f;
        const int len = random(10, 24);
        const int ex = cx + static_cast<int>(cosf(angle) * len);
        const int ey = cy + static_cast<int>(sinf(angle) * len);
        M5Cardputer.Display.drawLine(cx, cy, ex, ey, color);

        const int spark_t = random(len / 4, len * 3 / 4);
        const int sx = cx + static_cast<int>(cosf(angle) * spark_t);
        const int sy = cy + static_cast<int>(sinf(angle) * spark_t);
        M5Cardputer.Display.fillCircle(sx, sy, 1, color);

        if (random(3) != 0) {
            const int tail = spark_t + random(2, 6);
            const int tx = cx + static_cast<int>(cosf(angle) * tail);
            const int ty = cy + static_cast<int>(sinf(angle) * tail);
            M5Cardputer.Display.drawPixel(tx, ty, glow_color);
            if (tail + 2 < len) {
                const int tx2 = cx + static_cast<int>(cosf(angle) * (tail + 2));
                const int ty2 = cy + static_cast<int>(sinf(angle) * (tail + 2));
                M5Cardputer.Display.drawPixel(tx2, ty2, glow_color);
            }
        }
    }

    const int inner_rays = ray_count / 2 + 1;
    for (int r = 0; r < inner_rays; r++) {
        const float angle =
            ((static_cast<float>(r) + 0.5f) * 6.2831853f / inner_rays) + random(-20, 21) / 100.0f;
        const int len = random(4, 11);
        const int ex = cx + static_cast<int>(cosf(angle) * len);
        const int ey = cy + static_cast<int>(sinf(angle) * len);
        M5Cardputer.Display.drawLine(cx, cy, ex, ey, glow_color);
    }
}

// Version 页 UI 避让区（logo 圆 + 文字矩形，与 drawVersionOverlay 布局一致）
struct VersionPageLayout {
    int logo_cx;
    int logo_cy;
    int logo_avoid_r;
    int text_x;
    int text_y;
    int text_w;
    int text_h;
};

static VersionPageLayout getVersionPageLayout() {
    const VersionInfo info = getVersionInfo();
    const int screen_w = M5Cardputer.Display.width();

    constexpr int logo_px = APP_LOGO_60_PX;
    const int logo_y = APP_CONTENT_INSET_Y - 8; // logo 上移 8px
    const int logo_bottom = logo_y + logo_px;
    const int text_y = logo_bottom + 5; // 与文字区间隔 5px
    constexpr int line_h = 12;
    constexpr int text_line_h = 8;

    M5Cardputer.Display.setTextSize(1);
    const String line0 = "v" + info.version;
    const String line1 = "date: " + info.update_time;
    const String line2 = "github: " + info.github;
    const int text_w =
        max(M5Cardputer.Display.textWidth(line0.c_str()),
            max(M5Cardputer.Display.textWidth(line1.c_str()),
                M5Cardputer.Display.textWidth(line2.c_str()))) +
        16;

    return VersionPageLayout{
        screen_w / 2,
        logo_y + logo_px / 2,
        logo_px / 2 + 14,
        (screen_w - text_w) / 2,
        text_y - 4,
        text_w,
        line_h * 2 + text_line_h + 8,
    };
}

// 烟花落点是否避开 logo / 文字（含烟花半径余量）
static bool versionFireworkSpotOk(const int cx, const int cy, const VersionPageLayout& layout,
                                  const int burst_margin) {
    const int dx = cx - layout.logo_cx;
    const int dy = cy - layout.logo_cy;
    const int logo_r = layout.logo_avoid_r + burst_margin;
    if (dx * dx + dy * dy < logo_r * logo_r) {
        return false;
    }

    return cx < layout.text_x - burst_margin || cx >= layout.text_x + layout.text_w + burst_margin ||
           cy < layout.text_y - burst_margin || cy >= layout.text_y + layout.text_h + burst_margin;
}

// 随机选取避开 UI 的烟花落点
static bool pickVersionFireworkSpot(const int y_min, const int y_max, const VersionPageLayout& layout,
                                    const int burst_margin, int& cx, int& cy) {
    const int screen_w = M5Cardputer.Display.width();
    for (int attempt = 0; attempt < 24; attempt++) {
        cx = random(screen_w);
        cy = random(y_min, y_max);
        if (versionFireworkSpotOk(cx, cy, layout, burst_margin)) {
            return true;
        }
    }
    return false;
}

// Version 页背景烟花（logo 四色 + 白，避开 logo / 文字区域）
static void drawVersionFireworks() {
    const int screen_h = M5Cardputer.Display.height();
    const int y_min = APP_CONTENT_INSET_Y;
    const VersionPageLayout layout = getVersionPageLayout();
    constexpr int burst_margin = 26;

    static const uint32_t palette[] = {0x30D158, 0x3CD3FE, 0xFF4245, 0xFFD600, 0xFFFFFF};

    const int burst_count = random(6, 10);
    for (int i = 0; i < burst_count; i++) {
        int cx = 0;
        int cy = 0;
        if (!pickVersionFireworkSpot(y_min, screen_h, layout, burst_margin, cx, cy)) {
            continue;
        }
        const int ci = random(5);
        const uint16_t color = versionColor565(palette[ci]);
        const uint16_t glow = versionColor565(palette[(ci + 1) % 4]);
        drawFireworkBurst(cx, cy, color, glow);
    }

    // constexpr int spark_margin = 8;
    // for (int i = 0; i < random(8, 14); i++) {
    //     int cx = 0;
    //     int cy = 0;
    //     if (!pickVersionFireworkSpot(y_min, screen_h, layout, spark_margin, cx, cy)) {
    //         continue;
    //     }
    //     const uint16_t color = versionColor565(palette[random(5)]);
    //     const int rays = random(3, 6);
    //     for (int r = 0; r < rays; r++) {
    //         const float angle = random(0, 628) / 100.0f;
    //         const int len = random(2, 6);
    //         M5Cardputer.Display.drawLine(cx, cy, cx + static_cast<int>(cosf(angle) * len),
    //                                      cy + static_cast<int>(sinf(angle) * len), color);
    //     }
    //     M5Cardputer.Display.drawPixel(cx, cy, color);
    // }
}

// Version 页 logo + 版本信息（叠在烟花背景上）
static void drawVersionOverlay() {
    const VersionInfo info = getVersionInfo();

    constexpr int logo_px = APP_LOGO_60_PX;
    const int logoX = (M5Cardputer.Display.width() - logo_px) / 2;
    const int logoY = APP_CONTENT_INSET_Y - 8; // logo 上移 8px
    int logo_bottom = logoY + logo_px;
    if (!drawAppLogo60(logoX, logoY, 1.0f)) {
        constexpr int fallback_size = APP_LOGO_DESIGN_SIZE;
        const int fallback_x = (M5Cardputer.Display.width() - fallback_size) / 2;
        drawAppLogo(fallback_x, logoY, fallback_size);
        logo_bottom = logoY + fallback_size;
    }

    const int textY = logo_bottom + 5; // 与文字区间隔 5px
    const int centerX = M5Cardputer.Display.width() / 2;
    constexpr int lineH = 12;

    M5Cardputer.Display.setTextSize(1);
    // 文字区最上：版本号
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.drawCenterString(("v" + info.version).c_str(), centerX, textY);
    M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
    M5Cardputer.Display.drawCenterString(
        ("date: " + info.update_time).c_str(), centerX, textY + lineH);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.drawCenterString(
        ("github: " + info.github).c_str(), centerX, textY + lineH * 2);
}

// 全屏重绘 Version 页（header + 烟花 + 前景）
static void refreshVersionFireworks() {
    beginAppScreen(APP_NAME, false);
    drawVersionFireworks();
    drawVersionOverlay();
}

// 绘制 Version 页面
void drawVersionApp() {
    refreshVersionFireworks();
}

// R 键刷新背景烟花
void handleVersionApp(const Keyboard_Class::KeysState& status) {
    String key;
    for (const char c : status.word) {
        key += c;
    }
    if (key == "r" || key == "R") {
        refreshVersionFireworks();
    }
}

// ===== KEYBOARD =====

static char lastKeyLabel[16] = "-";
static char keyboardDisplayedKey[16] = "";
static bool keyboardScreenReady = false;
static bool keyboardLastFn = false;
static bool keyboardLastShift = false;
static bool keyboardLastOpt = false;
static bool keyboardLastCtrl = false;
static bool keyboardLastAlt = false;

static constexpr int KEY_MOD_LINE_H = 18;
static constexpr int KEY_MOD_COL_W = 88;
static constexpr int KEY_PANEL_X = 96;

String getKeyLabel(const Keyboard_Class::KeysState& status) {
    String key;
    for (const char c : status.word) {
        key += c;
    }
    if (key.length() > 0) {
        return key;
    }
    if (status.del) {
        return "DEL";
    }
    if (status.enter) {
        return "ENT";
    }
    if (status.space) {
        return "SPC";
    }
    if (status.tab) {
        return "TAB";
    }
    return "-";
}

// 修饰键：仅字体颜色，2 倍字，无底色
static void drawModLabelAt(const int x, const int y, const char* label, const bool active,
                           const uint16_t activeColor) {
    M5Cardputer.Display.fillRect(x, y, KEY_MOD_COL_W, KEY_MOD_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(active ? activeColor : DARKGREY, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
}

// 仅修饰键状态变化时重绘对应行
static void updateModLabelIfChanged(const int x, const int y, const char* label, const bool active,
                                    bool& cache, const uint16_t activeColor) {
    if (keyboardScreenReady && cache == active) {
        return;
    }
    cache = active;
    drawModLabelAt(x, y, label, active, activeColor);
}

// 仅重绘右侧按键内容区
static void updateKeyboardKeyPanel() {
    const int keyPanelY = APP_CONTENT_INSET_Y;
    const int keyPanelW = M5Cardputer.Display.width() - KEY_PANEL_X - 4;
    const int keyPanelH = M5Cardputer.Display.height() - keyPanelY;

    if (keyboardScreenReady && strcmp(keyboardDisplayedKey, lastKeyLabel) == 0) {
        return;
    }

    M5Cardputer.Display.fillRect(KEY_PANEL_X, keyPanelY, keyPanelW, keyPanelH, BLACK);

    const size_t len = strlen(lastKeyLabel);
    const int textSize = len <= 2 ? 4 : (len <= 4 ? 3 : 2);
    M5Cardputer.Display.setTextSize(textSize);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    const int textH = 8 * textSize;
    M5Cardputer.Display.drawCenterString(lastKeyLabel, KEY_PANEL_X + keyPanelW / 2,
                                         keyPanelY + (keyPanelH - textH) / 2);
    strncpy(keyboardDisplayedKey, lastKeyLabel, sizeof(keyboardDisplayedKey) - 1);
    keyboardDisplayedKey[sizeof(keyboardDisplayedKey) - 1] = '\0';
}

void drawKeyboardApp(const Keyboard_Class::KeysState& status, const bool full_init) {
    if (full_init || !keyboardScreenReady) {
        beginAppScreen("Key");
        keyboardScreenReady = true;
        keyboardLastFn = !status.fn;
        keyboardLastShift = !status.shift;
        keyboardLastOpt = !status.opt;
        keyboardLastCtrl = !status.ctrl;
        keyboardLastAlt = !status.alt;
        keyboardDisplayedKey[0] = '\0';
    }

    const String label = getKeyLabel(status);
    if (label != "-") {
        strncpy(lastKeyLabel, label.c_str(), sizeof(lastKeyLabel) - 1);
        lastKeyLabel[sizeof(lastKeyLabel) - 1] = '\0';
        Serial.println(label);
    }

    constexpr int modX = APP_CONTENT_X;
    int modY = APP_CONTENT_INSET_Y;
    updateModLabelIfChanged(modX, modY, "Fn", status.fn, keyboardLastFn, ORANGE);
    modY += KEY_MOD_LINE_H;
    updateModLabelIfChanged(modX, modY, "Aa", status.shift, keyboardLastShift, BLUE);
    modY += KEY_MOD_LINE_H;
    updateModLabelIfChanged(modX, modY, "opt", status.opt, keyboardLastOpt, GREEN);
    modY += KEY_MOD_LINE_H;
    updateModLabelIfChanged(modX, modY, "ctrl", status.ctrl, keyboardLastCtrl, WHITE);
    modY += KEY_MOD_LINE_H;
    updateModLabelIfChanged(modX, modY, "alt", status.alt, keyboardLastAlt, WHITE);

    updateKeyboardKeyPanel();
}

void enterKeyboardApp() {
    keyboardScreenReady = false;
    lastKeyLabel[0] = '-';
    lastKeyLabel[1] = '\0';
    keyboardDisplayedKey[0] = '\0';
    Keyboard_Class::KeysState status{};
    drawKeyboardApp(status, true);
}

// ===== BMI =====

const char* getImuTypeName(const m5::imu_t type) {
    switch (type) {
        case m5::imu_bmi270:
            return "BMI270";
        case m5::imu_mpu6886:
            return "MPU6886";
        case m5::imu_mpu6050:
            return "MPU6050";
        case m5::imu_mpu9250:
            return "MPU9250";
        case m5::imu_sh200q:
            return "SH200Q";
        case m5::imu_unknown:
            return "Unknown";
        default:
            return "N/A";
    }
}

// 绘制加速度十字线（左栏 XY 用）
static void drawBmiCrosshair(const int panelX, const int panelW, const int contentTop,
                             const int contentH) {
    const int crossCx = panelX + panelW / 2;
    const int crossCy = contentTop + contentH / 2;
    constexpr int crossLen = 38;
    // 参考圆线条比十字线更浅
    constexpr uint16_t ringColor = 0x3186;
    static constexpr int ringRadii[] = {12, 24, 36};

    for (const int r : ringRadii) {
        M5Cardputer.Display.drawCircle(crossCx, crossCy, r, ringColor);
    }

    M5Cardputer.Display.drawFastHLine(crossCx - crossLen, crossCy, crossLen * 2 + 1, DARKGREY);
    M5Cardputer.Display.drawFastVLine(crossCx, crossCy - crossLen, crossLen * 2 + 1, DARKGREY);
}

// 绘制 Z 轴竖线（右栏用）
static void drawBmiZAxis(const int panelX, const int panelW, const int contentTop,
                         const int contentH) {
    const int axisCx = panelX + panelW / 2;
    const int axisCy = contentTop + contentH / 2;
    constexpr int axisLen = 38;

    M5Cardputer.Display.drawFastVLine(axisCx, axisCy - axisLen, axisLen * 2 + 1, DARKGREY);
    M5Cardputer.Display.drawFastHLine(axisCx - 10, axisCy, 21, DARKGREY);
}

// 左栏：XY 十字图 + 数值靠左
static void updateBmiXYPanel(const int panelX, const int panelW, const int contentTop,
                             const int contentH, const float ax, const float ay) {
    const int crossCx = panelX + panelW / 2;
    const int crossCy = contentTop + contentH / 2;
    constexpr float accelScale = 34.0f;

    int dotX = crossCx + static_cast<int>(ax * accelScale);
    int dotY = crossCy - static_cast<int>(ay * accelScale);
    dotX = constrain(dotX, panelX + 2, panelX + panelW - 3);
    dotY = constrain(dotY, contentTop + 2, contentTop + contentH - 3);

    if (bmiPrevDotX[0] >= 0) {
        M5Cardputer.Display.fillCircle(bmiPrevDotX[0], bmiPrevDotY[0], 5, BLACK);
        drawBmiCrosshair(panelX, panelW, contentTop, contentH);
    }

    M5Cardputer.Display.fillCircle(dotX, dotY, 4, GREEN);
    bmiPrevDotX[0] = dotX;
    bmiPrevDotY[0] = dotY;

    // X/Y 贴左栏顶边左右两侧，避免与中部参考圆重叠
    M5Cardputer.Display.fillRect(panelX + 2, contentTop, panelW - 4, 8, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    char xyBuf[16];
    snprintf(xyBuf, sizeof(xyBuf), "X %+.2f", ax);
    M5Cardputer.Display.setCursor(panelX + 2, contentTop);
    M5Cardputer.Display.print(xyBuf);
    snprintf(xyBuf, sizeof(xyBuf), "Y %+.2f", ay);
    M5Cardputer.Display.drawRightString(xyBuf, panelX + panelW - 2, contentTop);
}

// 右栏：Z 竖轴指示 + 数值靠右
static void updateBmiZPanel(const int panelX, const int panelW, const int contentTop,
                            const int contentH, const float az) {
    const int axisCx = panelX + panelW / 2;
    const int axisCy = contentTop + contentH / 2;
    constexpr float zScale = 34.0f;

    int dotY = axisCy - static_cast<int>(az * zScale);
    dotY = constrain(dotY, contentTop + 2, contentTop + contentH - 3);

    if (bmiPrevDotX[1] >= 0) {
        M5Cardputer.Display.fillCircle(bmiPrevDotX[1], bmiPrevDotY[1], 5, BLACK);
        drawBmiZAxis(panelX, panelW, contentTop, contentH);
    }

    M5Cardputer.Display.fillCircle(axisCx, dotY, 4, GREEN);
    bmiPrevDotX[1] = axisCx;
    bmiPrevDotY[1] = dotY;

    // Z 贴右栏顶边右侧
    M5Cardputer.Display.fillRect(panelX + 2, contentTop, panelW - 4, 8, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    char zBuf[16];
    snprintf(zBuf, sizeof(zBuf), "Z %+.2f", az);
    M5Cardputer.Display.drawRightString(zBuf, panelX + panelW - 2, contentTop);
}

// BMI（IMU）页面：左 XY、右 Z，型号显示在 header
void drawBmiApp() {
    // 保持屏幕与 CPU 活跃，避免休眠影响 IMU 刷新
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();

    M5.Imu.update();

    if (!M5.Imu.isEnabled()) {
        bmiScreenReady = false;
        bmiPrevDotX[0] = bmiPrevDotX[1] = -1;
        beginAppScreen("IMU");
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_INSET_Y);
        M5Cardputer.Display.println("IMU not found");
        return;
    }

    const int screenW = M5Cardputer.Display.width();
    const int screenH = M5Cardputer.Display.height();
    const int panelW = screenW / 2;
    const int contentTop = APP_CONTENT_INSET_Y;
    const int contentH = screenH - contentTop;

    // 首帧才全屏初始化，避免每帧 clear 导致闪烁
    if (!bmiScreenReady) {
        beginAppScreen(getImuTypeName(M5.Imu.getType()));
        drawBmiCrosshair(0, panelW, contentTop, contentH);
        drawBmiZAxis(panelW, panelW, contentTop, contentH);
        M5Cardputer.Display.drawFastVLine(panelW, contentTop, contentH, DARKGREY);
        bmiPrevDotX[0] = bmiPrevDotX[1] = -1;
        bmiScreenReady = true;
    }

    float ax = 0;
    float ay = 0;
    float az = 0;
    M5.Imu.getAccel(&ax, &ay, &az);

    updateBmiXYPanel(0, panelW, contentTop, contentH, ax, ay);
    updateBmiZPanel(panelW, panelW, contentTop, contentH, az);
}

// ===== SETTINGS（L1 模块列表 → L2 详情 → L3 选择页）=====

enum class SettingsModule : uint8_t {
    Screen = 0,
    Sound = 1,
    Time = 2,
    Calendar = 3,
    Infrared = 4,
    AcAuto = 5,
    Count = 6,
};

enum class SettingsLayer : uint8_t { List = 0, Detail = 1, Picker = 2 };

enum class SettingsPickerKind : uint8_t {
    None = 0,
    TimeDefault,
    Timezone,
    IrDefault,
    IrTvBrand,
    IrAcBrand,
    AcAutoSensor,
    AcAutoBrand,
    AcAutoMode,
    AcAutoFan,
};

static SettingsModule g_settings_module = SettingsModule::Screen;
// List=侧栏焦点 / Detail=右侧字段焦点 / Picker=右侧选择页
static SettingsLayer g_settings_layer = SettingsLayer::List;
static int g_settings_row = 0; // 右侧字段选中
static SettingsPickerKind g_picker_kind = SettingsPickerKind::None;
static int g_picker_index = 0;
static int g_picker_scroll = 0;
static constexpr int SETTINGS_HINT_H = 12;
static constexpr int SETTINGS_ROW_H = 12;     // 右侧字段 / picker 行高
static constexpr int SETTINGS_SIDE_ROW_H = 14; // 侧栏分类行高
static constexpr int SETTINGS_SIDEBAR_W = 74;
static constexpr int SETTINGS_PAD_X = 3;
static constexpr int SETTINGS_PAD_Y = 2;
static constexpr int SETTINGS_SIDE_RADIUS = 2; // 侧栏选中圆角（略小）

// 侧栏选中 / 进度条：teal（与设计稿一致）
static uint16_t settingsAccentColor() {
    return M5Cardputer.Display.color565(0x3D, 0xC4, 0xBF);
}
// 右侧内容区底色
static uint16_t settingsContentBg() {
    return M5Cardputer.Display.color565(0x1C, 0x1C, 0x1C);
}
// 侧栏焦点弱化时的选中底
static uint16_t settingsAccentDim() {
    return M5Cardputer.Display.color565(0x1A, 0x4A, 0x48);
}

// 与 cycleAppTimezonePreset 预设表保持一致
static const char* const kSettingsTzPresets[] = {
    "CST-8", "JST-9", "KST-9", "UTC", "GMT0", "CET-1", "EST5", "PST8",
};
static constexpr int kSettingsTzPresetCount =
    static_cast<int>(sizeof(kSettingsTzPresets) / sizeof(kSettingsTzPresets[0]));

static const char* settingsModuleName(const SettingsModule mod) {
    switch (mod) {
        case SettingsModule::Screen:
            return "screen";
        case SettingsModule::Sound:
            return "sound";
        case SettingsModule::Time:
            return "clock";
        case SettingsModule::Calendar:
            return "calendar";
        case SettingsModule::Infrared:
            return "infrared";
        case SettingsModule::AcAuto:
            return "ac auto";
        default:
            return "?";
    }
}

static int settingsPanelRowCount(const SettingsModule mod) {
    switch (mod) {
        case SettingsModule::Screen:
            return 2; // brightness / invert
        case SettingsModule::Sound:
            return 3; // volume / time key / mijia on/off
        case SettingsModule::Time:
            return 2; // default / timezone
        case SettingsModule::Calendar:
            return 1; // week start
        case SettingsModule::Infrared:
            return 3; // category / tv / ac
        case SettingsModule::AcAuto:
            return 8; // sensor / on / off / filter / brand / mode / temp / fan
        default:
            return 0;
    }
}

static void clampSettingsRow() {
    const int n = settingsPanelRowCount(g_settings_module);
    if (n <= 0) {
        g_settings_row = 0;
        return;
    }
    if (g_settings_row < 0) {
        g_settings_row = 0;
    }
    if (g_settings_row >= n) {
        g_settings_row = n - 1;
    }
}

// 亮度待写盘：先改显示响应，UI 刷新后再 flushBrightnessSave
static uint8_t g_brightness_to_save = 0;
static bool g_brightness_dirty = false;

static void flushBrightnessSave() {
    // 亮度 RMW 会 loadAppConfig：先落盘音量，避免把未写入的 volume 打回旧值
    flushSpeakerVolumeSave();
    if (!g_brightness_dirty) {
        return;
    }
    g_brightness_dirty = false;
    saveAppConfigBrightness(g_brightness_to_save);
}

// 亮度加减并限制在 0-100（显示用）；只改背光，写盘延后到 flushBrightnessSave
void adjustBrightness(const int delta) {
    const int pct =
        constrain(static_cast<int>(brightnessHwToPercent(M5Cardputer.Display.getBrightness())) + delta,
                  0, 100);
    const uint8_t value = static_cast<uint8_t>(pct);
    M5Cardputer.Display.setBrightness(brightnessPercentToHw(value));
    g_brightness_to_save = value;
    g_brightness_dirty = true;
}

// 上下键（; . / HID）
static int getSettingsUpDownDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            return -1; // Up / ;
        }
        if (hid == 0x51 || hid == 0x37) {
            return 1; // Down / .
        }
    }
    for (const char c : status.word) {
        if (c == ';') {
            return -1;
        }
        if (c == '.') {
            return 1;
        }
    }
    return 0;
}

// 左右键：侧栏→进字段 / 字段·picker←返回
static int getSettingsLeftRightDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x50 || hid == 0x36) {
            return -1; // Left / ,
        }
        if (hid == 0x4F || hid == 0x38) {
            return 1; // Right / /
        }
    }
    for (const char c : status.word) {
        if (c == ',') {
            return -1;
        }
        if (c == '/') {
            return 1;
        }
    }
    return 0;
}

// -= 键：数值增减（返回 -1 / +1 / 0）
static int getSettingsValueDelta(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '-' || c == '_') {
            return -1;
        }
        if (c == '=' || c == '+') {
            return 1;
        }
    }
    return 0;
}

static const char* timeDefaultModeLabel(const TimeDefaultMode mode) {
    switch (mode) {
        case TimeDefaultMode::Ntp:
            return "Clock";
        case TimeDefaultMode::Countdown:
            return "Countdown";
        case TimeDefaultMode::Stopwatch:
            return "Stopwatch";
        case TimeDefaultMode::Up:
        default:
            return "Uptime";
    }
}

static TimeDefaultMode cycleTimeDefaultMode(const TimeDefaultMode cur, const int delta) {
    int idx = static_cast<int>(cur) + delta;
    constexpr int n = 4;
    idx = (idx % n + n) % n;
    return static_cast<TimeDefaultMode>(idx);
}

static int findSettingsTzPresetIndex(const char* tz) {
    if (tz == nullptr || tz[0] == '\0') {
        return 0;
    }
    for (int i = 0; i < kSettingsTzPresetCount; i++) {
        if (strcmp(tz, kSettingsTzPresets[i]) == 0) {
            return i;
        }
    }
    return 0;
}

static int settingsAcAutoSensorCount() {
    const AppConfig& cfg = getAppConfig();
    int n = 0;
    for (int i = 0; i < cfg.device_count; i++) {
        if (mijiaBleCanScan(cfg.devices[i]) &&
            mijiaClassifyModel(cfg.devices[i].model) == MijiaDevKind::SENSOR_HT) {
            n++;
        }
    }
    return n;
}

static int settingsAcAutoSensorDeviceIndex(const int picker_index) {
    const AppConfig& cfg = getAppConfig();
    int n = 0;
    for (int i = 0; i < cfg.device_count; i++) {
        if (mijiaBleCanScan(cfg.devices[i]) &&
            mijiaClassifyModel(cfg.devices[i].model) == MijiaDevKind::SENSOR_HT) {
            if (n == picker_index) {
                return i;
            }
            n++;
        }
    }
    return -1;
}

static int settingsAcAutoSensorPickerIndex(const char* sensor_id) {
    if (sensor_id == nullptr || sensor_id[0] == '\0') {
        return 0;
    }
    const AppConfig& cfg = getAppConfig();
    int n = 0;
    for (int i = 0; i < cfg.device_count; i++) {
        if (mijiaBleCanScan(cfg.devices[i]) &&
            mijiaClassifyModel(cfg.devices[i].model) == MijiaDevKind::SENSOR_HT) {
            if (strcmp(cfg.devices[i].id, sensor_id) == 0) {
                return n;
            }
            n++;
        }
    }
    return 0;
}

static const char* settingsAcAutoSensorLabel(const int picker_index) {
    const int dev_i = settingsAcAutoSensorDeviceIndex(picker_index);
    if (dev_i < 0) {
        return "(none)";
    }
    return mijiaDeviceDisplayName(getAppConfig().devices[dev_i]);
}

static int settingsPickerCount(const SettingsPickerKind kind) {
    switch (kind) {
        case SettingsPickerKind::TimeDefault:
            return 4;
        case SettingsPickerKind::Timezone:
            return kSettingsTzPresetCount;
        case SettingsPickerKind::IrDefault:
            return 2;
        case SettingsPickerKind::IrTvBrand:
            return IR_TV_BRAND_COUNT;
        case SettingsPickerKind::IrAcBrand:
        case SettingsPickerKind::AcAutoBrand:
            return IR_AC_BRAND_COUNT;
        case SettingsPickerKind::AcAutoSensor: {
            const int n = settingsAcAutoSensorCount();
            return n > 0 ? n : 1; // 至少占位一行
        }
        case SettingsPickerKind::AcAutoMode:
            return AC_AUTO_MODE_COUNT;
        case SettingsPickerKind::AcAutoFan:
            return AC_AUTO_FAN_COUNT;
        default:
            return 0;
    }
}

static const char* settingsPickerLabel(const SettingsPickerKind kind, const int index) {
    switch (kind) {
        case SettingsPickerKind::TimeDefault:
            return timeDefaultModeLabel(static_cast<TimeDefaultMode>(index));
        case SettingsPickerKind::Timezone:
            if (index >= 0 && index < kSettingsTzPresetCount) {
                return kSettingsTzPresets[index];
            }
            return "?";
        case SettingsPickerKind::IrDefault:
            return index == 1 ? "AC" : "TV";
        case SettingsPickerKind::IrTvBrand:
            return irTvBrandDisplayName(static_cast<uint8_t>(index));
        case SettingsPickerKind::IrAcBrand:
        case SettingsPickerKind::AcAutoBrand:
            return irAcBrandDisplayName(static_cast<uint8_t>(index));
        case SettingsPickerKind::AcAutoSensor:
            return settingsAcAutoSensorLabel(index);
        case SettingsPickerKind::AcAutoMode:
            return acAutoModeDisplayName(static_cast<uint8_t>(index));
        case SettingsPickerKind::AcAutoFan:
            return acAutoFanDisplayName(static_cast<uint8_t>(index));
        default:
            return "?";
    }
}

static const char* settingsPickerTitle(const SettingsPickerKind kind) {
    switch (kind) {
        case SettingsPickerKind::TimeDefault:
            return "default";
        case SettingsPickerKind::Timezone:
            return "timezone";
        case SettingsPickerKind::IrDefault:
            return "default";
        case SettingsPickerKind::IrTvBrand:
            return "tv brand";
        case SettingsPickerKind::IrAcBrand:
            return "ac brand";
        case SettingsPickerKind::AcAutoSensor:
            return "sensor";
        case SettingsPickerKind::AcAutoBrand:
            return "ac brand";
        case SettingsPickerKind::AcAutoMode:
            return "mode";
        case SettingsPickerKind::AcAutoFan:
            return "fan";
        default:
            return "pick";
    }
}

// 当前行是否进 L3 选择页（开关类不进）
static bool settingsRowOpensPicker(const SettingsModule mod, const int row) {
    switch (mod) {
        case SettingsModule::Time:
            return row == 0 || row == 1;
        case SettingsModule::Infrared:
            return row >= 0 && row <= 2;
        case SettingsModule::AcAuto:
            // sensor / brand / mode / fan 进选择页；温度与过滤用 -=
            return row == 0 || row == 4 || row == 5 || row == 7;
        default:
            return false;
    }
}

static void openSettingsPickerForCurrentRow() {
    g_picker_kind = SettingsPickerKind::None;
    g_picker_index = 0;
    g_picker_scroll = 0;
    const AppConfig& cfg = getAppConfig();
    if (g_settings_module == SettingsModule::Time) {
        if (g_settings_row == 0) {
            g_picker_kind = SettingsPickerKind::TimeDefault;
            g_picker_index = static_cast<int>(cfg.time_default_mode);
        } else if (g_settings_row == 1) {
            g_picker_kind = SettingsPickerKind::Timezone;
            g_picker_index = findSettingsTzPresetIndex(getAppTimezone());
        }
    } else if (g_settings_module == SettingsModule::Infrared) {
        if (g_settings_row == 0) {
            g_picker_kind = SettingsPickerKind::IrDefault;
            g_picker_index = cfg.infrared_default == IrDefaultCategory::Ac ? 1 : 0;
        } else if (g_settings_row == 1) {
            g_picker_kind = SettingsPickerKind::IrTvBrand;
            g_picker_index = cfg.infrared_tv_brand;
        } else if (g_settings_row == 2) {
            g_picker_kind = SettingsPickerKind::IrAcBrand;
            g_picker_index = cfg.infrared_ac_brand;
        }
    } else if (g_settings_module == SettingsModule::AcAuto) {
        const AcAutoConfig& ac = cfg.ac_auto;
        if (g_settings_row == 0) {
            g_picker_kind = SettingsPickerKind::AcAutoSensor;
            g_picker_index = settingsAcAutoSensorPickerIndex(ac.sensor_id);
        } else if (g_settings_row == 4) {
            g_picker_kind = SettingsPickerKind::AcAutoBrand;
            g_picker_index = ac.ac_brand;
        } else if (g_settings_row == 5) {
            g_picker_kind = SettingsPickerKind::AcAutoMode;
            g_picker_index = ac.ac_mode;
        } else if (g_settings_row == 7) {
            g_picker_kind = SettingsPickerKind::AcAutoFan;
            g_picker_index = ac.ac_fan;
        }
    }
    if (g_picker_kind == SettingsPickerKind::None) {
        return;
    }
    const int n = settingsPickerCount(g_picker_kind);
    if (g_picker_index < 0) {
        g_picker_index = 0;
    }
    if (n > 0 && g_picker_index >= n) {
        g_picker_index = n - 1;
    }
    g_settings_layer = SettingsLayer::Picker;
}

static void applySettingsPickerSelection() {
    switch (g_picker_kind) {
        case SettingsPickerKind::TimeDefault:
            saveAppConfigTimeDefaultMode(static_cast<TimeDefaultMode>(g_picker_index));
            break;
        case SettingsPickerKind::Timezone:
            if (g_picker_index >= 0 && g_picker_index < kSettingsTzPresetCount) {
                if (saveAppConfigTimezone(kSettingsTzPresets[g_picker_index])) {
                    applyLocalTimezone();
                }
            }
            break;
        case SettingsPickerKind::IrDefault:
        case SettingsPickerKind::IrTvBrand:
        case SettingsPickerKind::IrAcBrand: {
            const AppConfig& cfg = getAppConfig();
            IrDefaultCategory cat = cfg.infrared_default;
            uint8_t tv = cfg.infrared_tv_brand;
            uint8_t ac = cfg.infrared_ac_brand;
            if (g_picker_kind == SettingsPickerKind::IrDefault) {
                cat = g_picker_index == 1 ? IrDefaultCategory::Ac : IrDefaultCategory::Tv;
            } else if (g_picker_kind == SettingsPickerKind::IrTvBrand) {
                tv = static_cast<uint8_t>(g_picker_index);
            } else {
                ac = static_cast<uint8_t>(g_picker_index);
            }
            saveAppConfigInfrared(cat, tv, ac);
            break;
        }
        case SettingsPickerKind::AcAutoSensor:
        case SettingsPickerKind::AcAutoBrand:
        case SettingsPickerKind::AcAutoMode:
        case SettingsPickerKind::AcAutoFan: {
            AcAutoConfig ac = getAppConfig().ac_auto;
            if (g_picker_kind == SettingsPickerKind::AcAutoSensor) {
                const int dev_i = settingsAcAutoSensorDeviceIndex(g_picker_index);
                if (dev_i >= 0) {
                    strncpy(ac.sensor_id, getAppConfig().devices[dev_i].id, sizeof(ac.sensor_id) - 1);
                    ac.sensor_id[sizeof(ac.sensor_id) - 1] = '\0';
                } else {
                    ac.sensor_id[0] = '\0';
                }
            } else if (g_picker_kind == SettingsPickerKind::AcAutoBrand) {
                ac.ac_brand = static_cast<uint8_t>(g_picker_index);
            } else if (g_picker_kind == SettingsPickerKind::AcAutoMode) {
                ac.ac_mode = static_cast<uint8_t>(g_picker_index);
            } else {
                ac.ac_fan = static_cast<uint8_t>(g_picker_index);
            }
            saveAppConfigAcAuto(ac);
            break;
        }
        default:
            break;
    }
    g_picker_kind = SettingsPickerKind::None;
    g_settings_layer = SettingsLayer::Detail;
}

static void settingsEnsureScroll(int& scroll, const int selected, const int count,
                                 const int visible) {
    if (visible <= 0 || count <= 0) {
        scroll = 0;
        return;
    }
    if (selected < scroll) {
        scroll = selected;
    }
    if (selected >= scroll + visible) {
        scroll = selected - visible + 1;
    }
    const int max_scroll = count > visible ? count - visible : 0;
    if (scroll < 0) {
        scroll = 0;
    }
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
}

// fill_color：未选中用 teal，右侧选中黄底时用黑色
static void drawSettingsBrightBar(const int x, const int y, const int w, const int h,
                                  const int percent, const uint16_t fill_color) {
    const int pct = constrain(percent, 0, 100);
    M5Cardputer.Display.drawRect(x, y, w, h, APP_COLOR_MUTED);
    const int fill_w = (w - 2) * pct / 100;
    if (fill_w > 0) {
        M5Cardputer.Display.fillRect(x + 1, y + 1, fill_w, h - 2, fill_color);
    }
}

// 选中行：selected 时用 sel_bg；否则 label 用 label_color，底色 bg；text_pad_x 为文字左边距
static void drawSettingsSelectRow(const int x, const int y, const int w, const int row_h,
                                  const char* label, const char* value, const uint16_t value_color,
                                  const bool selected, const uint16_t sel_bg, const uint16_t bg,
                                  const uint16_t label_color, const bool round_sel,
                                  const int text_pad_x = 2) {
    M5Cardputer.Display.setTextSize(1);
    const uint16_t row_bg = selected ? sel_bg : bg;
    if (selected) {
        if (round_sel) {
            M5Cardputer.Display.fillRoundRect(x, y, w, row_h, SETTINGS_SIDE_RADIUS, sel_bg);
        } else {
            M5Cardputer.Display.fillRect(x, y, w, row_h, sel_bg);
        }
        // 亮起时内容用黑色
        M5Cardputer.Display.setTextColor(BLACK, sel_bg);
    } else {
        M5Cardputer.Display.setTextColor(label_color, bg);
    }
    const int text_y = y + (row_h - 8) / 2;
    M5Cardputer.Display.setCursor(x + text_pad_x, text_y);
    M5Cardputer.Display.print(label);
    if (value != nullptr && value[0] != '\0') {
        const uint16_t fg = selected ? BLACK : value_color;
        M5Cardputer.Display.setTextColor(fg, row_bg);
        M5Cardputer.Display.setCursor(x + w - M5Cardputer.Display.textWidth(value) - 2, text_y);
        M5Cardputer.Display.print(value);
    }
}

// 左侧分类栏：当前模块始终标记；侧栏焦点时亮 teal，右侧编辑时弱化
static void drawSettingsSidebar(const int content_y, const int content_h) {
    const int x = SETTINGS_PAD_X;
    const int w = SETTINGS_SIDEBAR_W - SETTINGS_PAD_X * 2;
    const bool side_focus = g_settings_layer == SettingsLayer::List;
    const int count = static_cast<int>(SettingsModule::Count);
    const int visible = content_h / SETTINGS_SIDE_ROW_H;
    int scroll = 0;
    settingsEnsureScroll(scroll, static_cast<int>(g_settings_module), count, visible);

    for (int i = 0; i < visible; i++) {
        const int idx = scroll + i;
        if (idx >= count) {
            break;
        }
        const SettingsModule mod = static_cast<SettingsModule>(idx);
        const bool selected = mod == g_settings_module;
        const int y = content_y + SETTINGS_PAD_Y + i * SETTINGS_SIDE_ROW_H;
        const uint16_t sel_bg = side_focus ? settingsAccentColor() : settingsAccentDim();
        // 侧栏文字距背景左缘 6px
        drawSettingsSelectRow(x, y, w, SETTINGS_SIDE_ROW_H, settingsModuleName(mod), nullptr,
                              APP_COLOR_VALUE, selected, sel_bg, BLACK, APP_COLOR_TEXT, true, 6);
    }
}

// 右侧字段区：焦点在字段时黄底标记当前行
static void drawSettingsDetailLayer(const int content_x, const int content_y, const int content_w,
                                    const int content_h) {
    (void)content_h;
    const uint16_t bg = settingsContentBg();
    const bool field_focus = g_settings_layer == SettingsLayer::Detail;
    int y = content_y + SETTINGS_PAD_Y;

    auto draw_row = [&](const int row, const char* label, const char* value,
                        const uint16_t value_color) {
        const bool selected = field_focus && g_settings_row == row;
        drawSettingsSelectRow(content_x, y, content_w, SETTINGS_ROW_H, label, value, value_color,
                              selected, APP_COLOR_MENU_KEY, bg, APP_COLOR_TEXT, false);
        y += SETTINGS_ROW_H;
    };

    // volume / brightness：标签行 + 进度条行；选中时整块黄底，文字/进度用黑色
    auto draw_bar_field = [&](const int row, const char* label, const int pct) {
        const bool selected = field_focus && g_settings_row == row;
        char pct_buf[8];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
        constexpr int bar_h = 6;
        constexpr int block_h = SETTINGS_ROW_H + bar_h + 3;
        if (selected) {
            M5Cardputer.Display.fillRect(content_x, y, content_w, block_h, APP_COLOR_MENU_KEY);
        }
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(selected ? BLACK : APP_COLOR_TEXT,
                                         selected ? APP_COLOR_MENU_KEY : bg);
        M5Cardputer.Display.setCursor(content_x + 2, y + 2);
        M5Cardputer.Display.print(label);
        const int bar_y = y + SETTINGS_ROW_H;
        const int bar_w = content_w - M5Cardputer.Display.textWidth(pct_buf) - 10;
        drawSettingsBrightBar(content_x + 2, bar_y, bar_w, bar_h, pct,
                              selected ? BLACK : settingsAccentColor());
        M5Cardputer.Display.setTextColor(selected ? BLACK : APP_COLOR_VALUE,
                                         selected ? APP_COLOR_MENU_KEY : bg);
        M5Cardputer.Display.setCursor(content_x + 2 + bar_w + 4, bar_y - 1);
        M5Cardputer.Display.print(pct_buf);
        y += block_h;
    };

    switch (g_settings_module) {
        case SettingsModule::Screen: {
            const int pct = brightnessHwToPercent(M5Cardputer.Display.getBrightness());
            draw_bar_field(0, "brightness", pct);
            const bool inverted = M5Cardputer.Display.getInvert();
            draw_row(1, "invert", inverted ? "ON" : "OFF",
                     inverted ? APP_COLOR_OK : APP_COLOR_HINT);
            break;
        }
        case SettingsModule::Sound: {
            draw_bar_field(0, "volume", getAppSpeakerVolumePercent());
            const bool time_on = isTimeKeySoundEnabled();
            draw_row(1, "time key", time_on ? "ON" : "OFF",
                     time_on ? APP_COLOR_OK : APP_COLOR_HINT);
            const bool mijia_on = isMijiaOnOffSoundEnabled();
            draw_row(2, "mijia on/off", mijia_on ? "ON" : "OFF",
                     mijia_on ? APP_COLOR_OK : APP_COLOR_HINT);
            break;
        }
        case SettingsModule::Time: {
            const AppConfig& cfg = getAppConfig();
            draw_row(0, "default", timeDefaultModeLabel(cfg.time_default_mode), APP_COLOR_VALUE);
            draw_row(1, "timezone", getAppTimezone(), APP_COLOR_VALUE);
            break;
        }
        case SettingsModule::Calendar: {
            const AppConfig& cfg = getAppConfig();
            const char* week_start = cfg.week_start == WeekStartDay::Monday ? "Mon" : "Sun";
            draw_row(0, "week start", week_start, APP_COLOR_VALUE);
            break;
        }
        case SettingsModule::Infrared: {
            const AppConfig& cfg = getAppConfig();
            const char* cat = cfg.infrared_default == IrDefaultCategory::Ac ? "AC" : "TV";
            draw_row(0, "default", cat, APP_COLOR_VALUE);
            draw_row(1, "tv brand", irTvBrandDisplayName(cfg.infrared_tv_brand), APP_COLOR_VALUE);
            draw_row(2, "ac brand", irAcBrandDisplayName(cfg.infrared_ac_brand), APP_COLOR_VALUE);
            break;
        }
        case SettingsModule::AcAuto: {
            const AcAutoConfig& ac = getAppConfig().ac_auto;
            char buf[16];
            const char* sensor = "(none)";
            if (ac.sensor_id[0] != '\0') {
                const int di = mijiaFindDeviceIndexById(ac.sensor_id);
                sensor = di >= 0 ? mijiaDeviceDisplayName(getAppConfig().devices[di]) : "(missing)";
            }
            draw_row(0, "sensor", sensor, APP_COLOR_VALUE);
            snprintf(buf, sizeof(buf), "%uC", static_cast<unsigned>(ac.on_temp_c));
            draw_row(1, "on temp", buf, APP_COLOR_VALUE);
            snprintf(buf, sizeof(buf), "%uC", static_cast<unsigned>(ac.off_temp_c));
            draw_row(2, "off temp", buf, APP_COLOR_VALUE);
            snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(ac.filter_count));
            draw_row(3, "filter", buf, APP_COLOR_VALUE);
            draw_row(4, "brand", irAcBrandDisplayName(ac.ac_brand), APP_COLOR_VALUE);
            draw_row(5, "mode", acAutoModeDisplayName(ac.ac_mode), APP_COLOR_VALUE);
            snprintf(buf, sizeof(buf), "%uC", static_cast<unsigned>(ac.ac_temp_c));
            draw_row(6, "ac temp", buf, APP_COLOR_VALUE);
            draw_row(7, "fan", acAutoFanDisplayName(ac.ac_fan), APP_COLOR_VALUE);
            break;
        }
        default:
            break;
    }
}

static void drawSettingsPickerLayer(const int content_x, const int content_y, const int content_w,
                                    const int content_h) {
    const uint16_t bg = settingsContentBg();
    // 右侧小标题
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(settingsAccentColor(), bg);
    M5Cardputer.Display.setCursor(content_x + 2, content_y + 1);
    M5Cardputer.Display.print(settingsPickerTitle(g_picker_kind));

    const int list_y = content_y + 11;
    const int list_h = content_h - 11;
    const int visible = list_h / SETTINGS_ROW_H;
    const int count = settingsPickerCount(g_picker_kind);
    settingsEnsureScroll(g_picker_scroll, g_picker_index, count, visible);

    for (int i = 0; i < visible; i++) {
        const int idx = g_picker_scroll + i;
        if (idx >= count) {
            break;
        }
        const int y = list_y + i * SETTINGS_ROW_H;
        drawSettingsSelectRow(content_x, y, content_w, SETTINGS_ROW_H,
                              settingsPickerLabel(g_picker_kind, idx), nullptr, APP_COLOR_VALUE,
                              idx == g_picker_index, APP_COLOR_MENU_KEY, bg, APP_COLOR_TEXT, false);
    }
}

static void drawSettingsHints() {
    const int hint_y = M5Cardputer.Display.height() - SETTINGS_HINT_H;
    const int badge_y = hint_y + 1; // tip 徽章下移 1px
    const int text_y = hint_y + 2;  // tip 文字下移 2px
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, screen_w - APP_CONTENT_X * 2,
                                 SETTINGS_HINT_H, BLACK);

    int cx = APP_CONTENT_X;
    auto print_hint = [&](const char* text) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, text_y);
        M5Cardputer.Display.print(text);
        cx += M5Cardputer.Display.textWidth(text);
    };

    cx += drawArrowUpBadge(cx, badge_y, 1);
    cx += drawArrowDownBadge(cx, badge_y, 1);
    if (g_settings_layer == SettingsLayer::List) {
        print_hint("cat ");
        cx += drawTextBadge(cx, badge_y, "Ent", 1);
        print_hint("edit");
    } else if (g_settings_layer == SettingsLayer::Detail) {
        print_hint("field ");
        cx += drawTextBadge(cx, badge_y, "-=", 1);
        print_hint("val ");
        cx += drawTextBadge(cx, badge_y, "`", 1);
        print_hint("back");
    } else {
        print_hint("pick ");
        cx += drawTextBadge(cx, badge_y, "Ent", 1);
        print_hint("ok ");
        cx += drawTextBadge(cx, badge_y, "`", 1);
        print_hint("back");
    }
}

// 局部刷新标志：避免 beginAppScreen 整页清屏闪烁
enum SettingsDirty : uint8_t {
    SettingsDirtySidebar = 1 << 0,
    SettingsDirtyPane = 1 << 1,
    SettingsDirtyHints = 1 << 2,
    SettingsDirtyBody = SettingsDirtySidebar | SettingsDirtyPane | SettingsDirtyHints,
};

static void settingsContentMetrics(int& content_y, int& content_h, int& right_x, int& right_w) {
    const int screen_w = M5Cardputer.Display.width();
    const int screen_h = M5Cardputer.Display.height();
    content_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    content_h = screen_h - content_y - SETTINGS_HINT_H;
    right_x = SETTINGS_SIDEBAR_W;
    right_w = screen_w - right_x;
}

// 只重绘脏区；不清 header / 不清整屏
static void redrawSettings(const uint8_t dirty) {
    int content_y = 0;
    int content_h = 0;
    int right_x = 0;
    int right_w = 0;
    settingsContentMetrics(content_y, content_h, right_x, right_w);

    if (dirty & SettingsDirtySidebar) {
        // 先清侧栏再画，去掉上一选中圆角残影
        M5Cardputer.Display.fillRect(0, content_y, SETTINGS_SIDEBAR_W, content_h, BLACK);
        drawSettingsSidebar(content_y, content_h);
    }
    if (dirty & SettingsDirtyPane) {
        M5Cardputer.Display.fillRect(right_x, content_y, right_w, content_h, settingsContentBg());
        const int pane_x = right_x + SETTINGS_PAD_X;
        const int pane_w = right_w - SETTINGS_PAD_X * 2;
        if (g_settings_layer == SettingsLayer::Picker) {
            drawSettingsPickerLayer(pane_x, content_y, pane_w, content_h);
        } else {
            drawSettingsDetailLayer(pane_x, content_y, pane_w, content_h);
        }
    }
    if (dirty & SettingsDirtyHints) {
        drawSettingsHints();
    }
}

void drawSettingsApp() {
    beginAppScreen("Options");
    redrawSettings(SettingsDirtyBody);
}

void enterSettingsApp() {
    g_settings_module = SettingsModule::Screen;
    g_settings_layer = SettingsLayer::List;
    g_settings_row = 0;
    g_picker_kind = SettingsPickerKind::None;
    g_picker_index = 0;
    g_picker_scroll = 0;
    drawSettingsApp();
}

// `：picker→字段 / 字段→侧栏；侧栏交给全局回主菜单
static bool handleSettingsBack() {
    if (g_settings_layer == SettingsLayer::Picker) {
        g_picker_kind = SettingsPickerKind::None;
        g_settings_layer = SettingsLayer::Detail;
        // picker↔字段：侧栏不变，只刷右栏与 tip
        redrawSettings(SettingsDirtyPane | SettingsDirtyHints);
        return true;
    }
    if (g_settings_layer == SettingsLayer::Detail) {
        g_settings_layer = SettingsLayer::List;
        // 回侧栏：焦点色与 tip 都变
        redrawSettings(SettingsDirtyBody);
        return true;
    }
    return false;
}

static void applySettingsValueDelta(const int val_delta) {
    if (val_delta == 0) {
        return;
    }
    switch (g_settings_module) {
        case SettingsModule::Screen:
            if (g_settings_row == 0) {
                adjustBrightness(val_delta * 5);
            } else if (g_settings_row == 1) {
                const bool next = !M5Cardputer.Display.getInvert();
                M5Cardputer.Display.invertDisplay(next);
                saveAppConfigScreenInvert(next);
            }
            break;
        case SettingsModule::Sound:
            if (g_settings_row == 0) {
                adjustAppSpeakerVolume(val_delta * 5);
            } else if (g_settings_row == 1) {
                flushSpeakerVolumeSave(); // RMW 前先落盘音量
                saveAppConfigTimeKeySound(!isTimeKeySoundEnabled());
            } else if (g_settings_row == 2) {
                flushSpeakerVolumeSave();
                saveAppConfigMijiaOnOffSound(!isMijiaOnOffSoundEnabled());
            }
            break;
        case SettingsModule::Time:
            if (g_settings_row == 0) {
                saveAppConfigTimeDefaultMode(
                    cycleTimeDefaultMode(getAppConfig().time_default_mode, val_delta));
            } else if (g_settings_row == 1) {
                const char* next_tz = cycleAppTimezonePreset(getAppTimezone(), val_delta);
                if (saveAppConfigTimezone(next_tz)) {
                    applyLocalTimezone();
                }
            }
            break;
        case SettingsModule::Calendar:
            if (g_settings_row == 0) {
                const WeekStartDay next = getAppConfig().week_start == WeekStartDay::Monday
                                              ? WeekStartDay::Sunday
                                              : WeekStartDay::Monday;
                saveAppConfigWeekStart(next);
            }
            break;
        case SettingsModule::Infrared: {
            const AppConfig& cfg = getAppConfig();
            IrDefaultCategory cat = cfg.infrared_default;
            uint8_t tv = cfg.infrared_tv_brand;
            uint8_t ac = cfg.infrared_ac_brand;
            if (g_settings_row == 0) {
                cat = cycleIrDefaultCategory(cat, val_delta);
            } else if (g_settings_row == 1) {
                tv = cycleIrTvBrand(tv, val_delta);
            } else if (g_settings_row == 2) {
                ac = cycleIrAcBrand(ac, val_delta);
            }
            saveAppConfigInfrared(cat, tv, ac);
            break;
        }
        case SettingsModule::AcAuto: {
            AcAutoConfig ac = getAppConfig().ac_auto;
            if (g_settings_row == 0) {
                const int n = settingsAcAutoSensorCount();
                if (n > 0) {
                    int cur = settingsAcAutoSensorPickerIndex(ac.sensor_id);
                    cur = (cur + val_delta + n) % n;
                    const int di = settingsAcAutoSensorDeviceIndex(cur);
                    if (di >= 0) {
                        strncpy(ac.sensor_id, getAppConfig().devices[di].id,
                                sizeof(ac.sensor_id) - 1);
                        ac.sensor_id[sizeof(ac.sensor_id) - 1] = '\0';
                    }
                }
            } else if (g_settings_row == 1) {
                ac.on_temp_c = static_cast<uint8_t>(
                    constrain(static_cast<int>(ac.on_temp_c) + val_delta, 16, 40));
            } else if (g_settings_row == 2) {
                ac.off_temp_c = static_cast<uint8_t>(
                    constrain(static_cast<int>(ac.off_temp_c) + val_delta, 10, 35));
            } else if (g_settings_row == 3) {
                ac.filter_count = static_cast<uint8_t>(
                    constrain(static_cast<int>(ac.filter_count) + val_delta, 1, 10));
            } else if (g_settings_row == 4) {
                ac.ac_brand = cycleIrAcBrand(ac.ac_brand, val_delta);
            } else if (g_settings_row == 5) {
                ac.ac_mode = cycleAcAutoMode(ac.ac_mode, val_delta);
            } else if (g_settings_row == 6) {
                ac.ac_temp_c = static_cast<uint8_t>(
                    constrain(static_cast<int>(ac.ac_temp_c) + val_delta, 16, 30));
            } else if (g_settings_row == 7) {
                ac.ac_fan = cycleAcAutoFan(ac.ac_fan, val_delta);
            }
            saveAppConfigAcAuto(ac);
            break;
        }
        default:
            break;
    }
}

void handleSettingsApp(const Keyboard_Class::KeysState& status) {
    const int lr = getSettingsLeftRightDelta(status);
    if (lr != 0) {
        if (g_settings_layer == SettingsLayer::List) {
            if (lr > 0) {
                g_settings_layer = SettingsLayer::Detail;
                clampSettingsRow();
                redrawSettings(SettingsDirtyBody);
            }
            return;
        }
        // 字段 / picker：← 返回上一层
        if (lr < 0) {
            (void)handleSettingsBack();
        }
        return;
    }

    const int ud = getSettingsUpDownDelta(status);
    if (ud != 0) {
        if (g_settings_layer == SettingsLayer::List) {
            // 侧栏切换分类，右侧内容同步刷新；tip 不变
            int next = static_cast<int>(g_settings_module) + ud;
            const int count = static_cast<int>(SettingsModule::Count);
            if (next < 0) {
                next = count - 1;
            } else if (next >= count) {
                next = 0;
            }
            g_settings_module = static_cast<SettingsModule>(next);
            g_settings_row = 0;
            redrawSettings(SettingsDirtySidebar | SettingsDirtyPane);
        } else if (g_settings_layer == SettingsLayer::Detail) {
            const int n = settingsPanelRowCount(g_settings_module);
            if (n > 0) {
                g_settings_row = (g_settings_row + ud + n) % n;
            }
            redrawSettings(SettingsDirtyPane);
        } else {
            const int n = settingsPickerCount(g_picker_kind);
            if (n > 0) {
                g_picker_index = (g_picker_index + ud + n) % n;
            }
            redrawSettings(SettingsDirtyPane);
        }
        return;
    }

    // 字段焦点：-= 改值
    if (g_settings_layer == SettingsLayer::Detail) {
        const int val_delta = getSettingsValueDelta(status);
        if (val_delta != 0) {
            applySettingsValueDelta(val_delta);
            redrawSettings(SettingsDirtyPane);
            flushBrightnessSave();
            return;
        }
    }

    if (status.enter) {
        if (g_settings_layer == SettingsLayer::List) {
            g_settings_layer = SettingsLayer::Detail;
            clampSettingsRow();
            redrawSettings(SettingsDirtyBody);
        } else if (g_settings_layer == SettingsLayer::Detail) {
            if (settingsRowOpensPicker(g_settings_module, g_settings_row)) {
                openSettingsPickerForCurrentRow();
                redrawSettings(SettingsDirtyPane | SettingsDirtyHints);
            } else {
                applySettingsValueDelta(1);
                redrawSettings(SettingsDirtyPane);
            }
        } else {
            applySettingsPickerSelection();
            redrawSettings(SettingsDirtyPane | SettingsDirtyHints);
        }
        flushBrightnessSave();
        return;
    }

    // 亮度数字快捷 / invert r / mijia m：仅字段焦点下 screen·sound
    if (g_settings_layer != SettingsLayer::Detail) {
        return;
    }
    String key;
    for (const char c : status.word) {
        key += c;
    }
    if (g_settings_module == SettingsModule::Screen) {
        if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
            const int level = key[0] - '0';
            const uint8_t pct = static_cast<uint8_t>(level * 100 / 9);
            M5Cardputer.Display.setBrightness(brightnessPercentToHw(pct));
            g_brightness_to_save = pct;
            g_brightness_dirty = true;
            redrawSettings(SettingsDirtyPane);
            flushBrightnessSave();
            return;
        }
        if (key == "r") {
            const bool next = !M5Cardputer.Display.getInvert();
            M5Cardputer.Display.invertDisplay(next);
            saveAppConfigScreenInvert(next);
            redrawSettings(SettingsDirtyPane);
        }
    }
    if (g_settings_module == SettingsModule::Sound && key == "m") {
        flushSpeakerVolumeSave();
        saveAppConfigMijiaOnOffSound(!isMijiaOnOffSoundEnabled());
        redrawSettings(SettingsDirtyPane);
    }
}

// ===== RGB LED（Stamp-S3A 板载 WS2812，GPIO21；与背光共电）=====

static constexpr int LED_PIN_FALLBACK = 21;
static bool g_led_app_active = false;
static bool g_led_help_visible = false;
static bool g_led_on = false;
static uint8_t g_led_r = 255;
static uint8_t g_led_g = 255;
static uint8_t g_led_b = 255;
static uint8_t g_led_saved_brightness = 30;
static bool g_i2c_help_visible = false;

// 简单 Help 页的分栏标题
static int drawSimpleHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// 简单 Help 页的按键说明；徽章后恢复说明文字颜色
static int drawSimpleHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawSimpleHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// 简单 Help 页的功能说明
static int drawSimpleHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static void drawLedHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawSimpleHelpColHeader(0, col_y, col_w, "keymap");
    y = drawSimpleHelpKey(2, y, 't', "toggle");
    y = drawSimpleHelpKey(2, y, '0', "off");
    y = drawSimpleHelpBadge(2, y, "1-7", "select color");
    y = drawSimpleHelpBadge(2, y, "-=", "brightness");

    y = drawSimpleHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawSimpleHelpText(manual_x + 2, y, "test onboard RGB");
    y = drawSimpleHelpText(manual_x + 2, y, "1-7: R G B Y C M W");
    y = drawSimpleHelpText(manual_x + 2, y, "shares LCD power");
    y = drawSimpleHelpText(manual_x + 2, y, "-= change bright");
    y = drawSimpleHelpText(manual_x + 2, y, "exit restores level");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

// 取板载 RGB 数据线脚位
static int getRgbLedPin() {
    const int pin = M5.getPin(m5::pin_name_t::rgb_led);
    return pin >= 0 ? pin : LED_PIN_FALLBACK;
}

// 写入板载 WS2812（neopixelWrite 按 RGB 字节序发送）
static void writeRgbLed(const uint8_t r, const uint8_t g, const uint8_t b) {
    neopixelWrite(static_cast<uint8_t>(getRgbLedPin()), r, g, b);
}

static void applyRgbLed() {
    if (g_led_on) {
        writeRgbLed(g_led_r, g_led_g, g_led_b);
    } else {
        writeRgbLed(0, 0, 0);
    }
}

static void leaveLedApp() {
    if (!g_led_app_active) {
        return;
    }
    writeRgbLed(0, 0, 0);
    g_led_on = false;
    g_led_app_active = false;
    M5Cardputer.Display.setBrightness(g_led_saved_brightness);
}

static const char* ledColorName() {
    if (!g_led_on) {
        return "OFF";
    }
    if (g_led_r == 255 && g_led_g == 0 && g_led_b == 0) {
        return "RED";
    }
    if (g_led_r == 0 && g_led_g == 255 && g_led_b == 0) {
        return "GREEN";
    }
    if (g_led_r == 0 && g_led_g == 0 && g_led_b == 255) {
        return "BLUE";
    }
    if (g_led_r == 255 && g_led_g == 255 && g_led_b == 0) {
        return "YELLOW";
    }
    if (g_led_r == 0 && g_led_g == 255 && g_led_b == 255) {
        return "CYAN";
    }
    if (g_led_r == 255 && g_led_g == 0 && g_led_b == 255) {
        return "MAGENTA";
    }
    if (g_led_r == 255 && g_led_g == 255 && g_led_b == 255) {
        return "WHITE";
    }
    return "RGB";
}

// 状态 wrap 背景色（与当前 LED 颜色对应）
static uint16_t ledStateBgColor() {
    if (!g_led_on) {
        return DARKGREY;
    }
    if (g_led_r == 255 && g_led_g == 0 && g_led_b == 0) {
        return RED;
    }
    if (g_led_r == 0 && g_led_g == 255 && g_led_b == 0) {
        return GREEN;
    }
    if (g_led_r == 0 && g_led_g == 0 && g_led_b == 255) {
        return BLUE;
    }
    if (g_led_r == 255 && g_led_g == 255 && g_led_b == 0) {
        return YELLOW;
    }
    if (g_led_r == 0 && g_led_g == 255 && g_led_b == 255) {
        return CYAN;
    }
    if (g_led_r == 255 && g_led_g == 0 && g_led_b == 255) {
        return MAGENTA;
    }
    if (g_led_r == 255 && g_led_g == 255 && g_led_b == 255) {
        return WHITE;
    }
    return M5Cardputer.Display.color565(g_led_r, g_led_g, g_led_b);
}

// 亮色底用黑字，暗色底用白字
static uint16_t ledStateFgColor() {
    if (!g_led_on) {
        return LIGHTGREY;
    }
    const int lum = (g_led_r * 299 + g_led_g * 587 + g_led_b * 114) / 1000;
    return lum >= 140 ? BLACK : WHITE;
}

void drawLedApp() {
    beginAppScreen("RGB LED");

    int y = APP_CONTENT_INSET_Y;

    // state 标签 + 对应颜色 wrap
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y + 2);
    M5Cardputer.Display.print("state ");

    const char* name = ledColorName();
    const uint16_t bg = ledStateBgColor();
    const uint16_t fg = ledStateFgColor();
    const int label_w = M5Cardputer.Display.textWidth("state ");
    const int tw = M5Cardputer.Display.textWidth(name);
    constexpr int pad_x = 6;
    constexpr int pad_y = 2;
    const int bx = APP_CONTENT_X + label_w;
    const int bw = tw + pad_x * 2;
    const int bh = 16 + pad_y * 2;
    M5Cardputer.Display.fillRoundRect(bx, y, bw, bh, 4, bg);
    M5Cardputer.Display.setTextColor(fg, bg);
    M5Cardputer.Display.setCursor(bx + pad_x, y + pad_y);
    M5Cardputer.Display.print(name);

    y += bh + 6;

    char pin_buf[16];
    snprintf(pin_buf, sizeof(pin_buf), "GPIO%d", getRgbLedPin());
    drawInfoLineAt(APP_CONTENT_X, y, "pin", pin_buf, 2);
    y += INFO_LINE_H_2X + 2;

    // 背光亮度（与 RGB 共电）
    char bright_buf[12];
    const int bright_pct = brightnessHwToPercent(M5Cardputer.Display.getBrightness());
    snprintf(bright_buf, sizeof(bright_buf), "%d", bright_pct);
    drawInfoLineAt(APP_CONTENT_X, y, "bright", bright_buf, 2);

    // 底栏 tip：t/1/2/3 + -= bright（0 off 不占 tip）
    const int hint_y = M5Cardputer.Display.height() - 12;
    const int text_y = hint_y + 1;
    int cx = APP_CONTENT_X;
    const KeyHintItem hints[] = {
        {'t', "tog"},
        {'1', "R"},
        {'2', "G"},
        {'3', "B"},
    };
    M5Cardputer.Display.setTextSize(1);
    for (int i = 0; i < 4; i++) {
        cx += drawKeyBadge(cx, hint_y, hints[i].key, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, text_y);
        M5Cardputer.Display.print(hints[i].text);
        cx += M5Cardputer.Display.textWidth(hints[i].text);
        M5Cardputer.Display.setCursor(cx, text_y);
        M5Cardputer.Display.print(" ");
        cx += M5Cardputer.Display.textWidth(" ");
    }
    cx += drawTextBadge(cx, hint_y, "-=", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("bright");
    drawHelpHintRight("help");
    updateAppHeaderStatus();
}

void enterLedApp() {
    // 背光与 RGB 共电：进入时拉满亮度，离开时恢复
    g_led_saved_brightness = M5Cardputer.Display.getBrightness();
    M5Cardputer.Display.setBrightness(255);
    g_led_app_active = true;
    g_led_help_visible = false;
    g_led_on = true;
    g_led_r = 255;
    g_led_g = 255;
    g_led_b = 255;
    applyRgbLed();
    drawLedApp();
}

void handleLedApp(const String& key) {
    if (key.length() == 0) {
        return;
    }
    const char c = key[0];
    if (c == 'h' || c == 'H') {
        g_led_help_visible = !g_led_help_visible;
        if (g_led_help_visible) {
            drawLedHelpPage();
        } else {
            drawLedApp();
        }
        return;
    }
    if (g_led_help_visible) {
        return;
    }
    // -= 调节背光亮度（与 RGB 共电；离开时仍恢复进入前亮度）
    if (c == '-' || c == '_' || c == '=' || c == '+') {
        const int delta = (c == '=' || c == '+') ? 5 : -5;
        const int pct = constrain(
            brightnessHwToPercent(M5Cardputer.Display.getBrightness()) + delta, 0, 100);
        M5Cardputer.Display.setBrightness(brightnessPercentToHw(static_cast<uint8_t>(pct)));
        drawLedApp();
        return;
    }
    if (c == 't' || c == 'T') {
        g_led_on = !g_led_on;
    } else if (c == '0') {
        g_led_on = false;
    } else if (c == '1') {
        g_led_on = true;
        g_led_r = 255;
        g_led_g = 0;
        g_led_b = 0;
    } else if (c == '2') {
        g_led_on = true;
        g_led_r = 0;
        g_led_g = 255;
        g_led_b = 0;
    } else if (c == '3') {
        g_led_on = true;
        g_led_r = 0;
        g_led_g = 0;
        g_led_b = 255;
    } else if (c == '4') {
        g_led_on = true;
        g_led_r = 255;
        g_led_g = 255;
        g_led_b = 0;
    } else if (c == '5') {
        g_led_on = true;
        g_led_r = 0;
        g_led_g = 255;
        g_led_b = 255;
    } else if (c == '6') {
        g_led_on = true;
        g_led_r = 255;
        g_led_g = 0;
        g_led_b = 255;
    } else if (c == '7') {
        g_led_on = true;
        g_led_r = 255;
        g_led_g = 255;
        g_led_b = 255;
    } else {
        return;
    }
    applyRgbLed();
    drawLedApp();
}

// ===== IN I2C =====

static void drawI2cHelpPage(const bool internal_bus) {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawSimpleHelpColHeader(0, col_y, col_w, "keymap");
    y = drawSimpleHelpKey(2, y, 'h', "help / close");

    y = drawSimpleHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawSimpleHelpText(manual_x + 2, y,
                           internal_bus ? "scan internal I2C" : "scan external I2C");
    y = drawSimpleHelpText(manual_x + 2, y,
                           internal_bus ? "internal bus debug" : "HY2.0 Port A bus");
    y = drawSimpleHelpText(manual_x + 2, y, "show SDA/SCL pins");
    y = drawSimpleHelpText(manual_x + 2, y, "scan address 1-119");
    y = drawSimpleHelpText(manual_x + 2, y, "list found devices");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

// 绘制 I2C 扫描结果（IN I2C / EX I2C 共用）
void drawI2cScanApp(m5::I2C_Class& bus, const char* title) {
    bool found[120]{};
    if (bus.isEnabled()) {
        bus.scanID(found);
    }

    M5Cardputer.Display.clear();
    drawAppScreenHeader(title);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_INSET_Y);

    if (!bus.isEnabled()) {
        M5Cardputer.Display.println("bus disabled");
        drawHelpHintRight("help");
        updateAppHeaderStatus();
        return;
    }

    M5Cardputer.Display.printf("SDA:%d SCL:%d\n", bus.getSDA(), bus.getSCL());

    int count = 0;
    for (int addr = 1; addr < 120; addr++) {
        if (!found[addr]) {
            continue;
        }
        M5Cardputer.Display.printf("0x%02X ", addr);
        count++;
        if (count % 5 == 0) {
            M5Cardputer.Display.println();
        }
    }
    if (count == 0) {
        M5Cardputer.Display.println("no device");
    }
    drawHelpHintRight("help");
    updateAppHeaderStatus();
}

static void handleI2cScanApp(const String& key, m5::I2C_Class& bus, const char* title,
                             const bool internal_bus) {
    if (key != "h" && key != "H") {
        return;
    }
    g_i2c_help_visible = !g_i2c_help_visible;
    if (g_i2c_help_visible) {
        drawI2cHelpPage(internal_bus);
    } else {
        drawI2cScanApp(bus, title);
    }
}

// ===== EX I2C =====
// 使用 drawI2cScanApp(M5Cardputer.Ex_I2C, "EX I2C")

// ===== MIJIA =====
// 见 app_mijia.cpp

// ===== WEB CONFIG =====
// 见 app_web.cpp

// ===== WIFI =====
// 见 app_wifi.cpp

// ===== BLE =====
// 见 app_ble.cpp

// ===== DISP =====

static int dispPatternIndex = 0;

enum class DispPattern {
    RED,
    GREEN,
    BLUE,
    YELLOW,
    CYAN,
    MAGENTA,
    WHITE,
    CHK_2X2,
    CHK_1X1,
    H_LINE_1PX,
    V_LINE_1PX,
    COUNT,
};

static const char* dispPatternName(const DispPattern p) {
    switch (p) {
        case DispPattern::RED:
            return "RED";
        case DispPattern::GREEN:
            return "GREEN";
        case DispPattern::BLUE:
            return "BLUE";
        case DispPattern::YELLOW:
            return "YEL";
        case DispPattern::CYAN:
            return "CYAN";
        case DispPattern::MAGENTA:
            return "MAG";
        case DispPattern::WHITE:
            return "WHT";
        case DispPattern::CHK_2X2:
            return "chk 2x2";
        case DispPattern::CHK_1X1:
            return "chk 1x1";
        case DispPattern::H_LINE_1PX:
            return "h 1px";
        case DispPattern::V_LINE_1PX:
            return "v 1px";
        default:
            return "?";
    }
}

// 2x2 黑白相间格
static void drawDispChecker2x2(const int x, const int y, const int w, const int h) {
    M5Cardputer.Display.fillScreen(BLACK);
    for (int py = y; py < y + h; py += 2) {
        for (int px = x; px < x + w; px += 2) {
            const bool white = (((px - x) / 2) + ((py - y) / 2)) % 2 == 0;
            const int rw = (px + 2 <= x + w) ? 2 : (x + w - px);
            const int rh = (py + 2 <= y + h) ? 2 : (y + h - py);
            M5Cardputer.Display.fillRect(px, py, rw, rh, white ? WHITE : BLACK);
        }
    }
}

// 1x1 黑白相间格
static void drawDispChecker1x1(const int x, const int y, const int w, const int h) {
    M5Cardputer.Display.fillScreen(BLACK);
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            const bool white = ((px - x) + (py - y)) % 2 == 0;
            M5Cardputer.Display.drawPixel(px, py, white ? WHITE : BLACK);
        }
    }
}

// 横向 1 像素间隔线
static void drawDispHLines1px(const int x, const int y, const int w, const int h) {
    M5Cardputer.Display.fillScreen(BLACK);
    for (int py = y; py < y + h; py += 2) {
        M5Cardputer.Display.drawFastHLine(x, py, w, WHITE);
    }
}

// 纵向 1 像素间隔线
static void drawDispVLines1px(const int x, const int y, const int w, const int h) {
    M5Cardputer.Display.fillScreen(BLACK);
    for (int px = x; px < x + w; px += 2) {
        M5Cardputer.Display.drawFastVLine(px, y, h, WHITE);
    }
}

// 屏幕验证图案（无 header，全屏图案 + 底部说明）
void drawDisplayApp(const int patternIndex) {
    static const uint16_t solid_colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE};
    const int count = static_cast<int>(DispPattern::COUNT);
    dispPatternIndex = ((patternIndex % count) + count) % count;
    const DispPattern pattern = static_cast<DispPattern>(dispPatternIndex);

    const int screen_w = M5Cardputer.Display.width();
    const int screen_h = M5Cardputer.Display.height();
    constexpr int hint_h = 12;
    const int hint_y = screen_h - hint_h + 2;  // 贴底再下移 1px
    const int area_h = screen_h - hint_h;

    if (static_cast<int>(pattern) < 7) {
        M5Cardputer.Display.fillScreen(solid_colors[static_cast<int>(pattern)]);
    } else {
        switch (pattern) {
            case DispPattern::CHK_2X2:
                drawDispChecker2x2(0, 0, screen_w, area_h);
                break;
            case DispPattern::CHK_1X1:
                drawDispChecker1x1(0, 0, screen_w, area_h);
                break;
            case DispPattern::H_LINE_1PX:
                drawDispHLines1px(0, 0, screen_w, area_h);
                break;
            case DispPattern::V_LINE_1PX:
                drawDispVLines1px(0, 0, screen_w, area_h);
                break;
            default:
                M5Cardputer.Display.fillScreen(BLACK);
                break;
        }
    }

    M5Cardputer.Display.fillRect(0, screen_h - hint_h, screen_w, hint_h, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(4, hint_y);
    M5Cardputer.Display.printf("%s  ", dispPatternName(pattern));
    int cx = M5Cardputer.Display.getCursorX();
    cx += drawArrowBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("page");
}

void handleDisplayApp(const Keyboard_Class::KeysState& status) {
    int delta = getMenuNavDelta(status);
    if (delta == 0) {
        delta = getBracketNavDelta(status);
    }
    if (delta == 0) {
        return;
    }
    drawDisplayApp(dispPatternIndex + delta);
}

// ===== HARDWARE TEST 二层入口 =====
static constexpr int HW_HUB_ITEMS_PER_PAGE = 8;

struct HardwareTestHubItem {
    const char* title;
    HardwareTestMode mode;
};

static constexpr HardwareTestHubItem HW_HUB_ITEMS[] = {
    {"DISPLAY", HardwareTestMode::SCREEN},
    {"IMU", HardwareTestMode::IMU},
    {"FONT", HardwareTestMode::FONT},
    {"ICONS", HardwareTestMode::ICONS},
    {"RGB LED", HardwareTestMode::LED},
    {"BLE", HardwareTestMode::BLE},
    {"INI2", HardwareTestMode::IN_I2C},
    {"EXI2", HardwareTestMode::EX_I2C},
    {"MIC", HardwareTestMode::MIC},
};
static constexpr int HW_HUB_ITEM_COUNT =
    static_cast<int>(sizeof(HW_HUB_ITEMS) / sizeof(HW_HUB_ITEMS[0]));

// 独有冷青主题（与 Games 暖金区分）
static uint16_t hwHubRgb(const uint8_t r, const uint8_t g, const uint8_t b) {
    return M5Cardputer.Display.color565(r, g, b);
}

// 顶栏由 beginAppScreen 绘制，此处仅保留 hub 配色
static uint16_t hwHubBg() {
    return hwHubRgb(0x04, 0x0A, 0x10);
}

static uint16_t hwHubCardBg() {
    return hwHubRgb(0x0A, 0x18, 0x22);
}

static uint16_t hwHubAccent() {
    return hwHubRgb(0x4E, 0xC8, 0xE8); // 冷青主色
}

// 边框用浅冷青，弱于徽章主色
static uint16_t hwHubBorder() {
    return hwHubRgb(0x36, 0x8C, 0xA0);
}

static uint16_t hwHubTitle() {
    return hwHubRgb(0xD0, 0xEC, 0xF4);
}

// Test hub 卡片：尺寸与主菜单一致
static void drawHardwareTestHubCard(const int x, const int y, const int card_w, const int card_h,
                                    const char key, const char* title) {
    const uint16_t card_bg = hwHubCardBg();
    const uint16_t accent = hwHubAccent();
    M5Cardputer.Display.fillRoundRect(x, y, card_w, card_h, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(x, y, card_w, card_h, 4, hwHubBorder());
    M5Cardputer.Display.fillRoundRect(x + 3, y + 3, 18, 16, 3, accent); // 整块序号徽章左移 1px
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, accent);
    M5Cardputer.Display.setCursor(x + 9, y + 7);
    M5Cardputer.Display.print(key);
    M5Cardputer.Display.setTextColor(hwHubTitle(), card_bg);
    M5Cardputer.Display.setCursor(x + 28, y + 7);
    M5Cardputer.Display.print(title);
}

static void drawHardwareTestHubCardAt(const int index, const char key, const char* title) {
    const int row = index / APP_HUB_CARD_COLS;
    const int col = index % APP_HUB_CARD_COLS;
    const int x = APP_HUB_CARD_ORIGIN_X + col * (APP_HUB_CARD_W + APP_HUB_CARD_GAP_X);
    const int y = APP_HUB_CARD_ORIGIN_Y + row * (APP_HUB_CARD_H + APP_HUB_CARD_GAP_Y);
    drawHardwareTestHubCard(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, key, title);
}

static int getHardwareTestHubPageCount() {
    return (HW_HUB_ITEM_COUNT + HW_HUB_ITEMS_PER_PAGE - 1) / HW_HUB_ITEMS_PER_PAGE;
}

static void drawHardwareTestHubCards() {
    // 每页最多四行八项，数字键按当前页从 1 重新编号。
    const int start = hardwareTestHubPage * HW_HUB_ITEMS_PER_PAGE;
    const int end = min(start + HW_HUB_ITEMS_PER_PAGE, HW_HUB_ITEM_COUNT);
    for (int item = start; item < end; ++item) {
        const int slot = item - start;
        drawHardwareTestHubCardAt(slot, static_cast<char>('1' + slot), HW_HUB_ITEMS[item].title);
    }
}

static void showHardwareTestsHubScreen() {
    // 回到 hub 时必须清子模式，否则按键/刷新仍走子 app 并盖住主菜单
    hardwareTestMode = HardwareTestMode::HUB;
    beginAppHubScreen(getMenuItemNameFull(AppState::HARDWARE_TESTS), hwHubBg(),
                      hardwareTestHubPage, getHardwareTestHubPageCount());
    drawHardwareTestHubCards();
}

static void leaveHardwareTestChild(const HardwareTestMode mode) {
    if (mode == HardwareTestMode::LED) {
        leaveLedApp();
    } else if (mode == HardwareTestMode::MIC) {
        leaveMicApp();
    }
    g_i2c_help_visible = false;
}

static void enterHardwareTestsApp() {
    hardwareTestHubPage = 0;
    showHardwareTestsHubScreen();
}

static void selectHardwareTest(const HardwareTestMode mode) {
    hardwareTestMode = mode;
    if (mode == HardwareTestMode::SCREEN) {
        dispPatternIndex = 0;
        drawDisplayApp(0);
    } else if (mode == HardwareTestMode::IMU) {
        bmiScreenReady = false;
        drawBmiApp();
    } else if (mode == HardwareTestMode::FONT) {
        enterFontDemoApp();
    } else if (mode == HardwareTestMode::ICONS) {
        enterIconDemoApp();
    } else if (mode == HardwareTestMode::LED) {
        enterLedApp();
    } else if (mode == HardwareTestMode::BLE) {
        enterBleApp();
    } else if (mode == HardwareTestMode::IN_I2C) {
        g_i2c_help_visible = false;
        drawI2cScanApp(M5Cardputer.In_I2C, "InI2");
    } else if (mode == HardwareTestMode::EX_I2C) {
        g_i2c_help_visible = false;
        drawI2cScanApp(M5Cardputer.Ex_I2C, "ExI2");
    } else if (mode == HardwareTestMode::MIC) {
        enterMicApp();
    }
}

static bool handleHardwareTestsBack() {
    if (hardwareTestMode == HardwareTestMode::HUB) {
        return false;
    }
    leaveHardwareTestChild(hardwareTestMode);
    showHardwareTestsHubScreen();
    return true;
}

static void handleHardwareTestsApp(const Keyboard_Class::KeysState& status) {
    if (hardwareTestMode == HardwareTestMode::HUB) {
        int delta = getMenuNavDelta(status);
        if (delta == 0) {
            delta = getBracketNavDelta(status);
        }
        const int page_count = getHardwareTestHubPageCount();
        if (delta != 0 && page_count > 1) {
            hardwareTestHubPage =
                (hardwareTestHubPage + delta + page_count) % page_count;
            showHardwareTestsHubScreen();
            return;
        }
        for (const char c : status.word) {
            if (c >= '1' && c <= '8') {
                const int item = hardwareTestHubPage * HW_HUB_ITEMS_PER_PAGE + (c - '1');
                if (item < HW_HUB_ITEM_COUNT) {
                    selectHardwareTest(HW_HUB_ITEMS[item].mode);
                    return;
                }
            }
        }
        return;
    }
    if (hardwareTestMode == HardwareTestMode::SCREEN) {
        handleDisplayApp(status);
    } else if (hardwareTestMode == HardwareTestMode::FONT) {
        handleFontDemoNav(status);
    } else if (hardwareTestMode == HardwareTestMode::ICONS) {
        handleIconDemoNav(status);
    } else if (hardwareTestMode == HardwareTestMode::LED) {
        handleLedApp(getPressedKey());
    } else if (hardwareTestMode == HardwareTestMode::BLE) {
        if (!handleBlePageNav(status)) {
            handleBleApp(getPressedKey());
        }
    } else if (hardwareTestMode == HardwareTestMode::IN_I2C) {
        handleI2cScanApp(getPressedKey(), M5Cardputer.In_I2C, "InI2", true);
    } else if (hardwareTestMode == HardwareTestMode::EX_I2C) {
        handleI2cScanApp(getPressedKey(), M5Cardputer.Ex_I2C, "ExI2", false);
    } else if (hardwareTestMode == HardwareTestMode::MIC) {
        handleMicApp(status);
    }
}

// ===== SLEEP =====

enum class SleepPhase {
    NONE,
    PROMPT_LIGHT,
    PROMPT_DEEP,
};

static SleepPhase sleepPhase = SleepPhase::NONE;
static uint32_t sleepPromptMs = 0;
static int sleepPromptLastSec = -1;
static uint8_t sleepSavedBrightness = 30;
// 倒计时数字区布局（局部刷新用）
static int sleepCountX = 0;
static int sleepCountY = 0;
static int sleepCountW = 0;
static int sleepCountH = 0;

// Cardputer BtnA (GO) = GPIO0，RTC 引脚，支持 ext0 唤醒
static constexpr gpio_num_t SLEEP_WAKE_PIN = GPIO_NUM_0;
static constexpr uint32_t SLEEP_PROMPT_MS = 5000;

// 入睡前断开无线
static void shutdownRadiosForSleep() {
    stopConfigWebServer();
    forceShutdownStaWifi();
    stopBleStack();
}

// 等 BtnA 松开并配置低电平唤醒
static void prepareBtnAWake() {
    pinMode(SLEEP_WAKE_PIN, INPUT_PULLUP);
    while (digitalRead(SLEEP_WAKE_PIN) == LOW) {
        delay(10);
    }

    rtc_gpio_init(SLEEP_WAKE_PIN);
    rtc_gpio_set_direction(SLEEP_WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(SLEEP_WAKE_PIN);
    rtc_gpio_pulldown_dis(SLEEP_WAKE_PIN);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup(SLEEP_WAKE_PIN, 0);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
}

// 浅休眠：关屏后 CPU 暂停，BtnA 唤醒并回到主菜单（不重启）
static void enterLightSleep() {
    batteryLogPrepareSleep(BatterySleepMode::Light);
    sleepSavedBrightness = M5Cardputer.Display.getBrightness();
    M5Cardputer.Display.sleep();
    M5Cardputer.Display.waitDisplay();
    M5Cardputer.Display.setBrightness(0);
    shutdownRadiosForSleep();
    flushCardputerInput(true);
    prepareBtnAWake();
    esp_light_sleep_start();

    // 先亮屏再清输入：避免等 BtnA 松开时黑屏卡住数秒
    sleepPhase = SleepPhase::NONE;
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.setBrightness(sleepSavedBrightness);
    flushCardputerInput(false);
    batteryLogAfterWake();
    showMenu();
}

// 深度休眠：关屏关无线后 CPU 断电，仅 BtnA 可唤醒（唤醒后重启）
static void enterDeepSleep() {
    batteryLogPrepareSleep(BatterySleepMode::Deep);
    M5Cardputer.Display.sleep();
    M5Cardputer.Display.waitDisplay();
    M5Cardputer.Display.setBrightness(0);
    shutdownRadiosForSleep();
    prepareBtnAWake();
    esp_deep_sleep_start();
}

// 仅刷新倒计时数字
static void drawSleepCountdownOnly(const int seconds_left) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", seconds_left);
    M5Cardputer.Display.setTextSize(3);
    if (sleepCountW > 0) {
        M5Cardputer.Display.fillRect(sleepCountX, sleepCountY, sleepCountW, sleepCountH, BLACK);
    }
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, sleepCountY);
    M5Cardputer.Display.print(buf);
    sleepCountW = M5Cardputer.Display.textWidth(buf);
    sleepCountH = 24;
    sleepCountX = APP_CONTENT_X;
}

// 浅休眠提示：默认路径，倒计时内按 s 可切到深度休眠
static void drawLightSleepPrompt(const int seconds_left) {
    // Header：Sleep + Light（次要色）
    beginAppScreenAccent("Sleep ", "Light", APP_COLOR_LABEL);

    int y = APP_CONTENT_INSET_Y;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("Will enter in");
    y += INFO_LINE_H_2X + 4;

    sleepCountY = y;
    sleepCountX = APP_CONTENT_X;
    sleepCountW = 0;
    drawSleepCountdownOnly(seconds_left);
    y += 30;

    // 两行 2x 提示：BtnGO wake / S Deep
    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, y, "BtnGO", 2);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("wake");
    y += INFO_LINE_H_2X + 4;

    cx = APP_CONTENT_X;
    cx += drawKeyBadge(cx, y, 's', 2);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("Deep");
}

// 深度休眠提示
static void drawDeepSleepPrompt(const int seconds_left) {
    // Header：Sleep + Deep（次要色）
    beginAppScreenAccent("Sleep ", "Deep", APP_COLOR_LABEL);

    int y = APP_CONTENT_INSET_Y;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("Will enter in");
    y += INFO_LINE_H_2X + 4;

    sleepCountY = y;
    sleepCountX = APP_CONTENT_X;
    sleepCountW = 0;
    drawSleepCountdownOnly(seconds_left);
    y += 30;

    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, y, "BtnGO", 2);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("wake");
    y += INFO_LINE_H_2X + 4;

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("reboot on wake");
}

// 进入浅休眠提示流程（5 秒后进 light sleep）
static void enterSleepApp() {
    currentState = AppState::SLEEP;
    sleepPhase = SleepPhase::PROMPT_LIGHT;
    sleepPromptMs = millis();
    sleepPromptLastSec = -1;
    M5Cardputer.Display.clear();
    drawLightSleepPrompt(5);
}

// 浅休眠提示中按 s：切换为深度休眠倒计时
static void switchToDeepSleepPrompt() {
    sleepPhase = SleepPhase::PROMPT_DEEP;
    sleepPromptMs = millis();
    sleepPromptLastSec = -1;
    drawDeepSleepPrompt(5);
}

// 倒计时结束后进入对应休眠（light sleep 唤醒后会返回）
static void updateSleepPrompt() {
    if (sleepPhase != SleepPhase::PROMPT_LIGHT && sleepPhase != SleepPhase::PROMPT_DEEP) {
        return;
    }

    const uint32_t elapsed = millis() - sleepPromptMs;
    if (elapsed >= SLEEP_PROMPT_MS) {
        if (sleepPhase == SleepPhase::PROMPT_DEEP) {
            enterDeepSleep();
        } else {
            enterLightSleep();
        }
        return;
    }

    const int sec_left = 5 - static_cast<int>(elapsed / 1000);
    if (sec_left != sleepPromptLastSec) {
        sleepPromptLastSec = sec_left;
        drawSleepCountdownOnly(sec_left);
    }
}

// ===== MAIN =====

void enterApp(const AppState state) {
    menuNoAppPrompt = false;
    if (currentState == AppState::MIC && state != AppState::MIC) {
        leaveMicApp();
    }
    if (currentState == AppState::NEON_FX && state != AppState::NEON_FX) {
        leaveNeonFxApp();
    }
    if (currentState == AppState::DICE && state != AppState::DICE) {
        leaveDiceApp();
    }
    if (currentState == AppState::NEWTON_CRADLE && state != AppState::NEWTON_CRADLE) {
        leaveNewtonCradleApp();
    }
    if (currentState == AppState::GAMES && state != AppState::GAMES) {
        leaveGamesApp();
    }
    if (currentState == AppState::HARDWARE_TESTS && state != AppState::HARDWARE_TESTS) {
        leaveHardwareTestChild(hardwareTestMode);
    }
    if (currentState == AppState::MORSE && state != AppState::MORSE) {
        leaveMorseApp();
    }
    if (currentState == AppState::HID_KEYBOARD && state != AppState::HID_KEYBOARD) {
        leaveHidKeyboardApp();
    }
    // 防御：离开 Cursor 时务必停 fetch / 放 WiFi，避免与 Config 抢射频
    if (currentState == AppState::CURSOR && state != AppState::CURSOR) {
        leaveCursorApp();
    }
    if (currentState == AppState::AC_AUTO && state != AppState::AC_AUTO) {
        leaveAcAutoApp();
    }
    if (currentState == AppState::MIJIA && state != AppState::MIJIA) {
        leaveMijiaApp();
    }
    if (currentState == AppState::IR && state != AppState::IR) {
        leaveIrApp();
    }
    currentState = state;

    // Sleep 先显示 5 秒提示，再关屏
    if (state == AppState::SLEEP) {
        enterSleepApp();
        return;
    }

    M5Cardputer.Display.clear();

    switch (state) {
        case AppState::VERSION:
            drawVersionApp();
            break;
        case AppState::KEYBOARD:
            enterKeyboardApp();
            break;
        case AppState::BMI:
            bmiScreenReady = false;
            drawBmiApp();
            break;
        case AppState::MIC:
            enterMicApp();
            break;
        case AppState::NEON_FX:
            enterNeonFxApp();
            break;
        case AppState::DICE:
            enterDiceApp();
            break;
        case AppState::NEWTON_CRADLE:
            enterNewtonCradleApp();
            break;
        case AppState::GAMES:
            enterGamesApp();
            break;
        case AppState::HARDWARE_TESTS:
            enterHardwareTestsApp();
            break;
        case AppState::RTC:
            enterRtcApp();
            break;
        case AppState::IN_I2C:
            g_i2c_help_visible = false;
            drawI2cScanApp(M5Cardputer.In_I2C, "InI2");
            break;
        case AppState::EX_I2C:
            g_i2c_help_visible = false;
            drawI2cScanApp(M5Cardputer.Ex_I2C, "ExI2");
            break;
        case AppState::WIFI:
            enterWifiApp();
            break;
        case AppState::BLE:
            enterBleApp();
            break;
        case AppState::DISP:
            dispPatternIndex = 0;
            drawDisplayApp(0);
            break;
        case AppState::ICONS:
            enterIconDemoApp();
            break;
        case AppState::SETTINGS:
            enterSettingsApp();
            break;
        case AppState::MIJIA:
            enterMijiaApp();
            break;
        case AppState::WEB:
            enterWebApp();
            break;
        case AppState::CURSOR:
            enterCursorApp();
            break;
        case AppState::MORSE:
            enterMorseApp();
            break;
        case AppState::IR:
            enterIrApp();
            break;
        case AppState::FONT_DEMO:
            enterFontDemoApp();
            break;
        case AppState::LED:
            enterLedApp();
            break;
        case AppState::BATTERY:
            enterBatteryApp();
            break;
        case AppState::HID_KEYBOARD:
            enterHidKeyboardApp();
            break;
        case AppState::INFO:
            enterInfoApp();
            break;
        case AppState::CALENDAR:
            enterCalendarApp();
            break;
        case AppState::AC_AUTO:
            // 与米家抢 BLE 扫描会话，进入前先释放
            leaveMijiaApp();
            leaveIrApp();
            enterAcAutoApp();
            break;
        default:
            break;
    }
}

void setup() {
    const uint32_t t0 = millis();
    const auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    const uint32_t t_begin = millis();
    // 开机拉低喇叭 I2S 脚，避免 NS4168 悬空嗡嗡；需要出声时再 begin
    releaseSpeakerQuiet();
    Serial.begin(115200);
    // USB-JTAG 控制台用 esp_rom_printf（CDC_ON_BOOT=0 时 Serial 可能不可见）
    esp_rom_printf("[boot] begin=%lums\n", static_cast<unsigned long>(t_begin - t0));
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("wake: BtnA from deep sleep");
    }
    const uint32_t t_fs0 = millis();
    if (initAppConfigFs()) {
        // 启动失败/空间不足时删最后一张截图，避免 Flash 撑死起不来
        const uint32_t t_shot0 = millis();
        recoverScreenshotsOnBoot();
        esp_rom_printf("[boot] littlefs+shot=%lums (shot=%lums)\n",
                       static_cast<unsigned long>(millis() - t_fs0),
                       static_cast<unsigned long>(millis() - t_shot0));
        const uint32_t t_cfg0 = millis();
        if (loadAppConfig()) {
            Serial.printf("config: %d mijia device(s)\n", getAppConfig().device_count);
            esp_rom_printf("[boot] config ok devices=%d json=%lums\n",
                           getAppConfig().device_count,
                           static_cast<unsigned long>(millis() - t_cfg0));
        } else {
            Serial.println("config: /config.json missing or invalid");
            esp_rom_printf("[boot] config fail json=%lums\n",
                           static_cast<unsigned long>(millis() - t_cfg0));
        }
    } else {
        Serial.println("config: LittleFS mount failed");
        esp_rom_printf("[boot] littlefs_fail=%lums\n",
                       static_cast<unsigned long>(millis() - t_fs0));
    }
    // config 加载后再设时区（deep sleep 唤醒后时钟可能已是 UTC）
    applyLocalTimezone();
    forceShutdownStaWifi();
    M5Cardputer.Display.setRotation(1);
    uint8_t brightness = 30;
    bool screen_invert = false;
    if (getAppConfig().loaded) {
        brightness = getAppConfig().brightness;
        screen_invert = getAppConfig().screen_invert;
    }
    M5Cardputer.Display.setBrightness(brightnessPercentToHw(brightness));
    M5Cardputer.Display.invertDisplay(screen_invert);
    const uint32_t t_flush0 = millis();
    // 冷启动轻量清输入：完整 flush 固定约 230ms（12+6 次 delay）
    for (int i = 0; i < 3; i++) {
        M5Cardputer.update();
        (void)M5Cardputer.Keyboard.isChange();
        (void)M5Cardputer.BtnA.wasPressed();
        (void)M5Cardputer.BtnA.wasReleased();
    }
    resetBtnGoEdge();
    esp_rom_printf("[boot] flush_input=%lums\n",
                   static_cast<unsigned long>(millis() - t_flush0));
    const uint32_t t_menu0 = millis();
    showMenu();
    esp_rom_printf("[boot] show_menu=%lums\n",
                   static_cast<unsigned long>(millis() - t_menu0));
    // 正常进主菜单后清除 boot_pending
    markScreenshotBootOk();
    // 电池日志放到首屏之后，缩短「黑屏→菜单」等待
    const uint32_t t_bat0 = millis();
    initBatteryLog();
    esp_rom_printf("[boot] battery_log=%lums total=%lums heap=%u\n",
                   static_cast<unsigned long>(millis() - t_bat0),
                   static_cast<unsigned long>(millis() - t0),
                   static_cast<unsigned>(ESP.getFreeHeap()));
}

void loop() {
    M5Cardputer.update();
    // 提示音播完后关功放+拉低脚；音量防抖写盘
    pollSpeakerQuietRelease();
    pollSpeakerVolumeSave();

    // 休眠提示倒计时
    if (sleepPhase == SleepPhase::PROMPT_LIGHT || sleepPhase == SleepPhase::PROMPT_DEEP) {
        updateSleepPrompt();
        // btngo：取消休眠倒计时并回主菜单（入睡唤醒仍用侧边 BtnA）
        if (wasBtnGoPressed()) {
            sleepPhase = SleepPhase::NONE;
            showMenu();
            return;
        }
        if (M5Cardputer.Keyboard.isChange()) {
            // 与其它界面一致：Fn+s 截图优先，不交给 sleep 的 s→深睡
            if (tryHandleScreenshotHotkey()) {
                return;
            }
            if (M5Cardputer.Keyboard.isPressed()) {
                const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                // Fn 按下时不消费字母键（留给全局热键）
                if (!status.fn) {
                    const String key = getPressedKey();
                    if (key == "s" && sleepPhase == SleepPhase::PROMPT_LIGHT) {
                        switchToDeepSleepPrompt();
                    }
                }
            }
        }
        return;
    }

    // btngo：无 app 提示页 / 子界面返回主菜单
    // HID 键盘占用全部按键（含 ESC），改由侧边 BtnA 退出
    if (currentState != AppState::HID_KEYBOARD && wasBtnGoPressed()) {
        if (menuNoAppPrompt || currentState != AppState::MENU) {
            if (currentState == AppState::GAMES && handleGamesBack()) {
                return;
            }
            if (currentState == AppState::HARDWARE_TESTS && handleHardwareTestsBack()) {
                return;
            }
            if (currentState == AppState::SETTINGS && handleSettingsBack()) {
                return;
            }
            if (currentState == AppState::MIC) {
                leaveMicApp();
            }
            if (currentState == AppState::NEON_FX) {
                leaveNeonFxApp();
            }
            if (currentState == AppState::DICE) {
                leaveDiceApp();
            }
            if (currentState == AppState::NEWTON_CRADLE) {
                leaveNewtonCradleApp();
            }
            if (currentState == AppState::GAMES) {
                leaveGamesApp();
            }
            if (currentState == AppState::MORSE) {
                leaveMorseApp();
            }
            showMenu();
            return;
        }
    }

    const uint32_t now = millis();

    // 开机期间按整点记录电池（sleep 中不跑 loop）
    batteryLogTick();

    // 主菜单 / 子界面 header 状态定时刷新
    static uint32_t lastHeaderStatusMs = 0;
    if (now - lastHeaderStatusMs >= 2000) {
        lastHeaderStatusMs = now;
        if (currentState == AppState::MENU) {
            updateMenuHeaderStatus(getMenuPageCount());
        } else if (currentState != AppState::SLEEP && currentState != AppState::DISP &&
                   currentState != AppState::NEON_FX && currentState != AppState::DICE &&
                   currentState != AppState::NEWTON_CRADLE && currentState != AppState::GAMES &&
                   currentState != AppState::HARDWARE_TESTS &&
                   // Time 全屏无 header（含 Help），不刷状态图标
                   !(currentState == AppState::RTC && isTimePureMode()) &&
                   !(currentState == AppState::CURSOR && isCursorDisplayBlanked()) &&
                   !(currentState == AppState::MIJIA && mijiaAppSuppressesHeader()) &&
                   !(currentState == AppState::IR && irAppSuppressesHeader()) &&
                   !(currentState == AppState::AC_AUTO && acAutoAppSuppressesHeader()) &&
                   !(currentState == AppState::WIFI && wifiAppSuppressesHeader()) &&
                   !(currentState == AppState::HID_KEYBOARD && hidKeyboardSuppressesHeader())) {
            updateAppHeaderStatus();
        }
    }

    if (currentState == AppState::BMI) {
        static uint32_t lastBmiUpdateMs = 0;
        if (now - lastBmiUpdateMs >= 100) {
            lastBmiUpdateMs = now;
            drawBmiApp();
        }
    } else if (currentState == AppState::MIC) {
        // 每帧拉取：Mic.record 异步双槽，40ms 节流会造成断流破音
        updateMicApp();
    } else if (currentState == AppState::NEON_FX) {
        pollNeonFxBtnA();
        // 调色板动画不节流，用屏幕实际吞吐量跑满刷新率。
        updateNeonFxApp();
    } else if (currentState == AppState::DICE) {
        updateDiceApp();
    } else if (currentState == AppState::NEWTON_CRADLE) {
        pollNewtonCradleBtnA();
        updateNewtonCradleApp();
    } else if (currentState == AppState::GAMES) {
        pollGamesBtnA();
        updateGamesApp();
    } else if (currentState == AppState::HARDWARE_TESTS &&
               hardwareTestMode == HardwareTestMode::IMU) {
        static uint32_t lastHardwareImuUpdateMs = 0;
        if (now - lastHardwareImuUpdateMs >= 100) {
            lastHardwareImuUpdateMs = now;
            drawBmiApp();
        }
    } else if (currentState == AppState::HARDWARE_TESTS &&
               hardwareTestMode == HardwareTestMode::BLE) {
        static uint32_t lastHardwareBleUpdateMs = 0;
        if (now - lastHardwareBleUpdateMs >= 500) {
            lastHardwareBleUpdateMs = now;
            updateBleApp();
        }
    } else if (currentState == AppState::HARDWARE_TESTS &&
               hardwareTestMode == HardwareTestMode::MIC) {
        // 与独立 Mic 相同：每帧拉取，避免断流
        updateMicApp();
    } else if (currentState == AppState::BATTERY) {
        static uint32_t lastBatUpdateMs = 0;
        // 后台 NTP 时 250ms 轮询；平时 1s 刷新电量
        const uint32_t bat_iv = batteryAppSyncBusy() ? 250 : 1000;
        if (now - lastBatUpdateMs >= bat_iv) {
            lastBatUpdateMs = now;
            updateBatteryApp();
        }
    } else if (currentState == AppState::INFO) {
        updateInfoApp();
    } else if (currentState == AppState::CALENDAR) {
        updateCalendarApp();
    } else if (currentState == AppState::WIFI) {
        updateWifiApp();
    } else if (currentState == AppState::BLE) {
        static uint32_t lastBleUpdateMs = 0;
        if (now - lastBleUpdateMs >= 500) {
            lastBleUpdateMs = now;
            updateBleApp();
        }
    } else if (currentState == AppState::MIJIA) {
        // BtnA 边沿只在当帧有效，需每帧轮询
        pollMijiaBtnA();
        updateMijiaApp();
    } else if (currentState == AppState::WEB) {
        updateWebApp();
    } else if (currentState == AppState::CURSOR) {
        // BtnA 边沿只在当帧有效，需每帧轮询
        pollCursorBtnA();
        updateCursorApp();
    } else if (currentState == AppState::MORSE) {
        updateMorseApp();
    } else if (currentState == AppState::IR) {
        pollIrBtnA();
        updateIrApp();
    } else if (currentState == AppState::AC_AUTO) {
        pollAcAutoBtnA();
        updateAcAutoApp();
    } else if (currentState == AppState::HID_KEYBOARD) {
        if (pollHidKeyboardBtnAExit()) {
            showMenu();
            return;
        }
        updateHidKeyboardApp();
    }

    // 倒计时后台：到期响铃并强制切入 CD 界面
    {
        const bool just_expired = pollCountdownBackground();
        const bool on_cd = currentState == AppState::RTC && isTimeCountdownUiActive();
        if (just_expired || isCountdownAlarmRinging()) {
            if (!on_cd) {
                currentState = AppState::RTC;
                presentCountdownAlarmUi();
            } else if (just_expired) {
                // 已在 CD：刷新到 FINISHED 页
                redrawCountdownApp();
            }
        }
    }

    if (currentState == AppState::RTC) {
        // BtnA 边沿只在当帧有效，不能跟刷新节流绑在一起
        pollTimeAppBtnA();
        static uint32_t lastRtcUpdateMs = 0;
        // Clock-like 空闲 1s 一拍时不必 30ms 轮询；CD/SW 仍 30ms
        const uint32_t rtc_poll_ms = isTimeIdleSlowLoop() ? 1000 : 30;
        if (now - lastRtcUpdateMs >= rtc_poll_ms) {
            lastRtcUpdateMs = now;
            updateRtcApp();
        }
    }

    if (M5Cardputer.Keyboard.isChange()) {
        // 任意界面 Fn+s：优先 TF，否则 Flash（进 Config → /shots 下载）
        if (tryHandleScreenshotHotkey()) {
            return;
        }
        switch (currentState) {
            case AppState::MENU:
                if (M5Cardputer.Keyboard.isPressed()) {
                    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                    // Fn 按下时仍允许方向键翻菜单页（不拦截字母菜单）
                    if (status.fn) {
                        (void)handleMenuPageNav(status);
                        break;
                    }
                    if (!handleMenuPageNav(status)) {
                        handleMenuKey(getPressedKey());
                    }
                }
                break;
            case AppState::KEYBOARD:
                drawKeyboardApp(M5Cardputer.Keyboard.keysState(), false);
                break;
            case AppState::SETTINGS:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleSettingsApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::VERSION:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleVersionApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::MIC:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleMicApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::NEON_FX:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleNeonFxApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::DICE:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleDiceApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::NEWTON_CRADLE:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleNewtonCradleApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::GAMES:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleGamesApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::HARDWARE_TESTS:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleHardwareTestsApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::WIFI:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleWifiApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::DISP:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleDisplayApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::MIJIA:
                if (M5Cardputer.Keyboard.isPressed()) {
                    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                    if (handleMijiaHotkeyUi(status)) {
                        break;
                    }
                    if (!handleMijiaOverviewPageNav(status) && !handleMijiaDeviceNav(status)) {
                        handleMijiaApp(getPressedKey());
                    }
                }
                break;
            case AppState::BLE:
                if (M5Cardputer.Keyboard.isPressed()) {
                    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                    if (!handleBlePageNav(status)) {
                        handleBleApp(getPressedKey());
                    }
                }
                break;
            case AppState::ICONS:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleIconDemoNav(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::IN_I2C:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleI2cScanApp(getPressedKey(), M5Cardputer.In_I2C, "InI2", true);
                }
                break;
            case AppState::EX_I2C:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleI2cScanApp(getPressedKey(), M5Cardputer.Ex_I2C, "ExI2", false);
                }
                break;
            case AppState::WEB:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleWebApp(getPressedKey());
                }
                break;
            case AppState::RTC:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleTimeApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::CURSOR:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleCursorApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::MORSE:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleMorseApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::IR:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleIrApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::AC_AUTO:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleAcAutoApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::FONT_DEMO:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleFontDemoNav(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::LED:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleLedApp(getPressedKey());
                }
                break;
            case AppState::BATTERY:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleBatteryApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::HID_KEYBOARD:
                // 按下与松开都要处理，避免主机卡键
                handleHidKeyboardApp(M5Cardputer.Keyboard.keysState());
                break;
            case AppState::INFO:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleInfoApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::CALENDAR:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleCalendarApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            default:
                break;
        }
    }

    // 实时 app 不休眠；Cursor / Time(Uptime·Clock) 空闲后 1s 一拍；其它状态 yield 10ms
    if (currentState == AppState::CURSOR && isCursorIdleSlowLoop()) {
        delay(1000);
    } else if (currentState == AppState::RTC && isTimeIdleSlowLoop()) {
        // 对齐下一整秒，秒位刷新不跳秒；按键响应约 1s 内
        const uint32_t rem = millis() % 1000;
        delay(rem == 0 ? 1000 : (1000 - rem));
    } else if (currentState == AppState::RTC && isTimeClockLikeMode()) {
        // Uptime / Clock 有操作时 ~30ms，避免空转费电
        delay(30);
    } else if ((currentState == AppState::NEON_FX && isNeonFxHelpVisible()) ||
               (currentState == AppState::DICE && isDiceHelpVisible()) ||
               (currentState == AppState::GAMES && isGamesHelpVisible())) {
        // Help 页静态展示，节流到 ~30ms 节省 CPU
        delay(30);
    } else if (currentState == AppState::HID_KEYBOARD) {
        // HID 键盘：更密采样 + 排空 BLE 发送队列
        delay(2);
    } else if (currentState != AppState::BMI && currentState != AppState::MIC &&
               currentState != AppState::NEON_FX && currentState != AppState::DICE &&
               currentState != AppState::NEWTON_CRADLE && currentState != AppState::GAMES &&
               currentState != AppState::RTC) {
        delay(10);
    }
}
