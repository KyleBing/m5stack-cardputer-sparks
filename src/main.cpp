#include "M5Cardputer.h"
#include "app_config.h"
#include "app_icons.h"
#include "app_header.h"
#include "app_common.h"
#include "app_web.h"
#include "app_wifi.h"
#include "app_mijia.h"
#include "app_mijia_ui.h"
#include "app_device_icons.h"
#include "app_ble.h"
#include "app_connectivity.h"
#include "app_countdown.h"
#include "app_stopwatch.h"
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <time.h>



// ===== COMMON =====

struct VersionInfo {
    const String version;
    const String update_time;
    const String author;
    const String email;
    const String website;
};

// 应用状态
enum class AppState {
    MENU,
    VERSION,
    KEYBOARD,
    BMI,
    INFO,
    MIC,
    SETTINGS,
    POWER,
    SPEAKER,
    RTC,
    IN_I2C,
    EX_I2C,
    WIFI,
    BLE,
    DISP,
    CIRCLE,
    ICONS,
    SLEEP,
    MIJIA,
    WEB,
    COUNTDOWN,
    STOPWATCH,
};

struct MenuItem {
    char key;
    const char* name;
    const char* name_full;
    AppState state;
};


// Cardputer 技能 → 字母入口
static const MenuItem MENU_ITEMS[] = {
    {'v', "Ver", "Version", AppState::VERSION},
    {'k', "Key", "Keyboard", AppState::KEYBOARD},
    {'g', "BMI", "BMI", AppState::BMI},
    {'i', "Info", "Info", AppState::INFO},
    {'r', "Mic", "Mic", AppState::MIC},
    {'o', "Set", "Settings", AppState::SETTINGS},
    {'p', "Pwr", "Power", AppState::POWER},
    {'l', "Spk", "Speaker", AppState::SPEAKER},
    {'s', "Slp", "Sleep", AppState::SLEEP},
    {'t', "Time", "Time", AppState::RTC},
    {'n', "InI2", "InI2", AppState::IN_I2C},
    {'e', "ExI2", "ExI2", AppState::EX_I2C},
    {'w', "WiFi", "WiFi", AppState::WIFI},
    {'m', "Mij", "Mijia", AppState::MIJIA},
    {'u', "Cfg", "Config Setup", AppState::WEB},
    {'b', "BLE", "BLE", AppState::BLE},
    {'d', "Disp", "Display", AppState::DISP},
    {'c', "Circ", "Circle", AppState::CIRCLE},
    {'a', "Icn", "Icons", AppState::ICONS},
    {'q', "Cd", "Countdown", AppState::COUNTDOWN},
    {'f', "Sw", "Stopwatch", AppState::STOPWATCH},
};

static const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
const int GAP_VERTICAL = 3;

AppState currentState = AppState::MENU;
static bool micHeaderReady = false;
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
static constexpr int MENU_COLS = 3;
static constexpr int MENU_ROWS_PER_PAGE = 5;
static constexpr int MENU_ITEMS_PER_PAGE = MENU_COLS * MENU_ROWS_PER_PAGE;
static constexpr int MENU_KEY_TEXT_SIZE = 2;
static constexpr int MENU_LINE_H = 18; // 与 2 倍按键块高度一致

static int menuPage = 0;

// 计算菜单总页数
int getMenuPageCount() {
    return (MENU_ITEM_COUNT + MENU_ITEMS_PER_PAGE - 1) / MENU_ITEMS_PER_PAGE;
}

// 绘制单个菜单项：按键块 + 名称
static void drawMenuItemAt(const int x, const int y, const MenuItem& item) {
    const int badge_w = drawKeyBadge(x, y, item.key, MENU_KEY_TEXT_SIZE);
    M5Cardputer.Display.setTextSize(MENU_KEY_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(APP_COLOR_TEXT, BLACK);
    M5Cardputer.Display.setCursor(x + badge_w, y + 1);
    M5Cardputer.Display.print(item.name);
}

// 绘制主菜单当前页
void drawMenuPage() {
    const int startIdx = menuPage * MENU_ITEMS_PER_PAGE;
    const int endIdx = (startIdx + MENU_ITEMS_PER_PAGE < MENU_ITEM_COUNT)
                           ? startIdx + MENU_ITEMS_PER_PAGE
                           : MENU_ITEM_COUNT;

    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - APP_CONTENT_X * 2) / MENU_COLS;

    int row = 0;
    for (int i = startIdx; i < endIdx; i += MENU_COLS) {
        const int y = APP_CONTENT_Y + row * (MENU_LINE_H + GAP_VERTICAL);
        for (int col = 0; col < MENU_COLS; ++col) {
            const int idx = i + col;
            if (idx >= endIdx) {
                break;
            }
            drawMenuItemAt(APP_CONTENT_X + col * col_w, y, MENU_ITEMS[idx]);
        }
        row++;
    }
}

// 绘制主菜单（header + 可翻页菜单区）
void showMenu() {
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

// 方向键翻页，返回 true 表示已处理
bool handleMenuPageNav(const Keyboard_Class::KeysState& status) {
    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return false;
    }

    const int pageCount = getMenuPageCount();
    menuPage = (menuPage + delta + pageCount) % pageCount;
    showMenu();
    return true;
}

// 菜单按键
void handleMenuKey(const String& key) {
    if (key.length() != 1) {
        return;
    }
    const char c = key[0];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        return;
    }

    if (!enterAppByKey(c)) {
        beginAppScreen("Menu");
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.printf("No app: %c\n", toupper(c));
    }
}

// ===== VERSION =====

// 返回固件版本信息
VersionInfo getVersionInfo() {
    return VersionInfo{
        "0.0.1",
        "2026.07.06",
        "KyleBing",
        "kylebing@163.com",
        "kylebing.cn"
    };
}

// 绘制 Version 页面
void drawVersionApp() {
    const VersionInfo info = getVersionInfo();
    beginAppScreen(("v" + info.version).c_str());

    constexpr int logoSize = APP_LOGO_DESIGN_SIZE;
    const int logoX = (M5Cardputer.Display.width() - logoSize) / 2;
    const int logoY = APP_CONTENT_Y + 4;
    drawAppLogo(logoX, logoY, logoSize);

    const int textY = logoY + logoSize + 10;
    const int centerX = M5Cardputer.Display.width() / 2;
    constexpr int lineH = 12;

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
    M5Cardputer.Display.drawCenterString(
        ("date: " + info.update_time).c_str(), centerX, textY);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.drawCenterString(
        ("auth: " + info.author).c_str(), centerX, textY + lineH);
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
    const int keyPanelY = APP_CONTENT_Y;
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
    int modY = APP_CONTENT_Y;
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

    M5Cardputer.Display.fillRect(panelX + 2, contentTop + 2, 58, 18, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(panelX + 2, contentTop + 2);
    M5Cardputer.Display.printf("X %+.2f\n", ax);
    M5Cardputer.Display.printf("Y %+.2f", ay);
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

    M5Cardputer.Display.fillRect(panelX + panelW - 54, contentTop + 2, 52, 10, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    char zBuf[16];
    snprintf(zBuf, sizeof(zBuf), "Z %+.2f", az);
    M5Cardputer.Display.drawRightString(zBuf, panelX + panelW - 2, contentTop + 2);
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
        beginAppScreen("BMI");
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
        M5Cardputer.Display.println("IMU not found");
        return;
    }

    const int screenW = M5Cardputer.Display.width();
    const int screenH = M5Cardputer.Display.height();
    const int panelW = screenW / 2;
    const int contentTop = APP_CONTENT_Y;
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

// ===== INFO =====

static constexpr int INFO_ICON_SIZE = ICON_INFO_H;
static constexpr int INFO_CARD_LINE_H = 8;
static constexpr int INFO_CARD_GAP = 6;
static int infoScrollIdx = 0;

struct InfoListItem {
    void (*draw_icon)(int x, int y, uint16_t color);
    const char* l1_label;
    const char* l1_value;
    const char* l2_label;
    const char* l2_value;
    const char* l3_label;
    const char* l3_value;
};

// 绘制卡片右侧单行（行高 8px，与 24px 图标对齐）
static void drawInfoCardLine(const int x, const int y, const char* label, const char* value) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.print(": ");
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.print(value);
}

// 绘制单项：左图标 + 右三行
static void drawInfoCardItem(const InfoListItem& item, const int x, const int y) {
    item.draw_icon(x, y, APP_COLOR_VALUE);
    const int text_x = x + INFO_ICON_SIZE + 6;
    drawInfoCardLine(text_x, y, item.l1_label, item.l1_value);
    drawInfoCardLine(text_x, y + INFO_CARD_LINE_H, item.l2_label, item.l2_value);
    drawInfoCardLine(text_x, y + INFO_CARD_LINE_H * 2, item.l3_label, item.l3_value);
}

// 组装 Info 列表数据
static int buildInfoListItems(InfoListItem* items, const int max_items) {
    const esp_chip_info_t chipInfo = []() {
        esp_chip_info_t info{};
        esp_chip_info(&info);
        return info;
    }();

    static char buf_model[24];
    static char buf_cores[8];
    static char buf_freq[16];
    static char buf_flash[16];
    static char buf_heap[16];
    static char buf_sdk[20];
    static char buf_bat[8];
    static char buf_volt[12];
    static char buf_chg[8];

    snprintf(buf_model, sizeof(buf_model), "%s", ESP.getChipModel());
    snprintf(buf_cores, sizeof(buf_cores), "%d", chipInfo.cores);
    snprintf(buf_freq, sizeof(buf_freq), "%d MHz", ESP.getCpuFreqMHz());
    snprintf(buf_flash, sizeof(buf_flash), "%d MB", ESP.getFlashChipSize() / (1024 * 1024));
    snprintf(buf_heap, sizeof(buf_heap), "%d KB", ESP.getFreeHeap() / 1024);
    snprintf(buf_sdk, sizeof(buf_sdk), "%s", ESP.getSdkVersion());
    snprintf(buf_bat, sizeof(buf_bat), "%d%%", M5Cardputer.Power.getBatteryLevel());
    snprintf(buf_volt, sizeof(buf_volt), "%dmV", M5Cardputer.Power.getBatteryVoltage());
    strncpy(buf_chg, getChargingStatusText(), sizeof(buf_chg));
    buf_chg[sizeof(buf_chg) - 1] = '\0';

    int count = 0;
    if (count < max_items) {
        items[count++] = {drawIconInfoChip, "model", buf_model, "cores", buf_cores, "freq",
                          buf_freq};
    }
    if (count < max_items) {
        items[count++] = {drawIconInfoStorage, "flash", buf_flash, "heap", buf_heap, "sdk", buf_sdk};
    }
    if (count < max_items) {
        items[count++] = {drawIconInfoBattery, "bat", buf_bat, "volt", buf_volt, "chg", buf_chg};
    }
    return count;
}

static int getInfoVisibleCount() {
    const int avail_h = M5Cardputer.Display.height() - APP_CONTENT_Y - 12;
    return avail_h / (INFO_ICON_SIZE + INFO_CARD_GAP);
}

// 绘制 Info 列表（支持滚动）
void drawInfoApp() {
    InfoListItem items[6];
    const int item_count = buildInfoListItems(items, 6);
    const int visible = getInfoVisibleCount();
    const int max_scroll = item_count > visible ? item_count - visible : 0;
    if (infoScrollIdx > max_scroll) {
        infoScrollIdx = max_scroll;
    }
    if (infoScrollIdx < 0) {
        infoScrollIdx = 0;
    }

    beginAppScreen("Info");
    int y = APP_CONTENT_Y;
    for (int i = 0; i < visible; i++) {
        const int idx = infoScrollIdx + i;
        if (idx >= item_count) {
            break;
        }
        drawInfoCardItem(items[idx], APP_CONTENT_X, y);
        y += INFO_ICON_SIZE + INFO_CARD_GAP;
    }

    if (item_count > visible) {
        char hint[24];
        snprintf(hint, sizeof(hint), "%d/%d  , . [ ] scroll", infoScrollIdx + 1, max_scroll + 1);
        drawHintText(APP_CONTENT_X, M5Cardputer.Display.height() - 12, hint);
    }
}

// Info 列表滚动
bool handleInfoPageNav(const Keyboard_Class::KeysState& status) {
    InfoListItem items[6];
    const int item_count = buildInfoListItems(items, 6);
    const int visible = getInfoVisibleCount();
    if (item_count <= visible) {
        return false;
    }

    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return false;
    }

    const int max_scroll = item_count - visible;
    infoScrollIdx = constrain(infoScrollIdx + delta, 0, max_scroll);
    drawInfoApp();
    return true;
}

void enterInfoApp() {
    infoScrollIdx = 0;
    drawInfoApp();
}

// ===== MIC =====

// Mic 实时波形 + 电平条
void drawMicApp() {
    constexpr int meterW = 12;
    const int waveTop = APP_CONTENT_Y;
    const int screenW = M5Cardputer.Display.width();
    const int waveW = screenW - meterW - 2;
    const int waveH = M5Cardputer.Display.height() - waveTop - 2;
    const int centerY = waveTop + waveH / 2;
    const int meterX = waveW + 2;

    static int16_t samples[240];
    static uint32_t peakHold = 4000;

    if (!M5Cardputer.Mic.isEnabled()) {
        beginAppScreen("Mic");
        micHeaderReady = false;
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
        M5Cardputer.Display.println("not found");
        return;
    }

    if (!micHeaderReady) {
        beginAppScreen("Mic");
        micHeaderReady = true;
    }

    const size_t sampleCount = waveW < 240 ? static_cast<size_t>(waveW) : 240;
    if (!M5Cardputer.Mic.record(samples, sampleCount)) {
        return;
    }

    // 本帧峰值 + 平滑峰值（快升慢降，用于自适应增益）
    int32_t framePeak = 0;
    for (size_t i = 0; i < sampleCount; i++) {
        const int32_t absVal = abs(samples[i]);
        if (absVal > framePeak) {
            framePeak = absVal;
        }
    }
    if (framePeak > static_cast<int32_t>(peakHold)) {
        peakHold = static_cast<uint32_t>(framePeak);
    } else {
        peakHold = peakHold * 7 / 8 + static_cast<uint32_t>(framePeak) / 8;
    }
    if (peakHold < 500) {
        peakHold = 500;
    }

    constexpr int minGain = 6;
    constexpr int maxGain = 18;
    const int micGain = constrain(
        (waveH / 2 - 4) * 32768 / static_cast<int>(peakHold), minGain, maxGain);

    M5Cardputer.Display.fillRect(0, waveTop, screenW, waveH, BLACK);
    M5Cardputer.Display.drawFastHLine(0, centerY, waveW, WHITE);

    // 中心对称填充波形，比单线更易辨认
    for (int x = 0; x < static_cast<int>(sampleCount); x++) {
        int y = centerY - static_cast<int>(samples[x] * micGain * (waveH / 2 - 2) / 32768);
        y = constrain(y, waveTop, waveTop + waveH - 1);
        const int yTop = min(centerY, y);
        const int yBot = max(centerY, y);
        M5Cardputer.Display.drawFastVLine(x, yTop, yBot - yTop + 1, CYAN);
    }

    // 右侧电平条：RMS 近似用峰值，黄色横线标瞬时峰值
    const int peakBarH = constrain(
        static_cast<int>(static_cast<int64_t>(framePeak) * waveH / 32768), 0, waveH);
    const int barX = meterX + 2;
    const int barInnerW = meterW - 4;
    const int barBottom = waveTop + waveH;

    M5Cardputer.Display.drawRect(meterX, waveTop, meterW, waveH, DARKGREY);
    if (peakBarH > 0) {
        M5Cardputer.Display.fillRect(barX, barBottom - peakBarH, barInnerW, peakBarH, GREEN);
    }
    const int peakLineY = barBottom - peakBarH;
    if (peakBarH > 0) {
        M5Cardputer.Display.drawFastHLine(meterX, peakLineY, meterW, YELLOW);
    }

    // 左上角显示峰值百分比
    const int peakPct = constrain(framePeak * 100 / 32768, 0, 100);
    M5Cardputer.Display.fillRect(0, waveTop, 52, 10, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.setCursor(2, waveTop + 1);
    M5Cardputer.Display.printf("Lv%3d%%", peakPct);
}

// ===== SETTINGS =====

static constexpr int BRIGHTNESS_BAR_MARGIN_X = 8;

// 亮度加减并限制在 0-255
void adjustBrightness(const int delta) {
    const int next = constrain(static_cast<int>(M5Cardputer.Display.getBrightness()) + delta, 0, 255);
    M5Cardputer.Display.setBrightness(static_cast<uint8_t>(next));
}

// 方向键步进：左右 ±1，上下 ±10（Cardputer 方向键为 ; , . /）
int getSettingsArrowDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        switch (hid) {
            case 0x50:
            case 0x36:
                return -1;
            case 0x4F:
            case 0x38:
                return 1;
            case 0x52:
            case 0x33:
                return 10;
            case 0x51:
            case 0x37:
                return -10;
            default:
                break;
        }
    }
    for (const char c : status.word) {
        switch (c) {
            case ',':
                return -1;
            case '/':
                return 1;
            case ';':
                return 10;
            case '.':
                return -10;
            default:
                break;
        }
    }
    return 0;
}

// 方向键：左右 ±1，上下 ±10；返回 true 表示已处理
bool handleSettingsArrowKeys(const Keyboard_Class::KeysState& status) {
    const int delta = getSettingsArrowDelta(status);
    if (delta == 0) {
        return false;
    }
    adjustBrightness(delta);
    return true;
}

// 绘制亮度条（左右留 margin）
void drawBrightnessBar(const int x, const int y, const int barW, const int barH,
                     const uint8_t brightness) {
    const int innerW = barW - 2;

    M5Cardputer.Display.drawRect(x, y, barW, barH, DARKGREY);

    // 刻度：0 / 64 / 128 / 192 / 255
    for (int i = 0; i <= 4; i++) {
        const int tickX = x + 1 + i * innerW / 4;
        M5Cardputer.Display.drawFastVLine(tickX, y + barH, 3, DARKGREY);
    }

    const int fillW = static_cast<int>(static_cast<int64_t>(brightness) * innerW / 255);
    if (fillW > 0) {
        M5Cardputer.Display.fillRect(x + 1, y + 1, fillW, barH - 2, GREEN);
    }
}

void drawSettingsApp() {
    beginAppScreen(getMenuItemNameFull(AppState::SETTINGS));

    const int screenW = M5Cardputer.Display.width();
    const uint8_t brightness = M5Cardputer.Display.getBrightness();

    // 标题 2 倍字，亮度值 1 倍字跟在后面
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.print("Brightness");
    const int valueX = M5Cardputer.Display.getCursorX() + 4;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(valueX, APP_CONTENT_Y + 5);
    M5Cardputer.Display.printf("%d", brightness);

    const int barY = APP_CONTENT_Y + 20;
    const int barX = BRIGHTNESS_BAR_MARGIN_X;
    const int barW = screenW - BRIGHTNESS_BAR_MARGIN_X * 2;
    drawBrightnessBar(barX, barY, barW, 12, brightness);

    // 说明区：小字 + 边框
    constexpr int pad = 4;
    constexpr int lineH = 10;
    constexpr int hintLines = 5;
    const int boxX = APP_CONTENT_X;
    const int boxY = barY + 18;
    const int boxW = screenW - APP_CONTENT_X * 2;
    const int boxH = pad * 2 + hintLines * lineH;

    M5Cardputer.Display.drawRect(boxX, boxY, boxW, boxH, DARKGREY);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);

    int ty = boxY + pad;
    M5Cardputer.Display.setCursor(boxX + pad, ty);
    M5Cardputer.Display.println("0-9  preset");
    ty += lineH;
    M5Cardputer.Display.setCursor(boxX + pad, ty);
    M5Cardputer.Display.println("<>   step 1");
    ty += lineH;
    M5Cardputer.Display.setCursor(boxX + pad, ty);
    M5Cardputer.Display.println("^v   step 10");
    ty += lineH;
    M5Cardputer.Display.setCursor(boxX + pad, ty);
    M5Cardputer.Display.println("r    invert");
    ty += lineH;
    M5Cardputer.Display.setCursor(boxX + pad, ty);
    M5Cardputer.Display.printf("inv: %s", M5Cardputer.Display.getInvert() ? "ON" : "OFF");
}

void handleSettingsApp(const Keyboard_Class::KeysState& status) {
    if (handleSettingsArrowKeys(status)) {
        drawSettingsApp();
        return;
    }

    String key;
    for (const char c : status.word) {
        key += c;
    }

    if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
        const int level = key[0] - '0';
        M5Cardputer.Display.setBrightness(static_cast<uint8_t>(level * 255 / 9));
    } else if (key == "-" || key == "_") {
        adjustBrightness(-16);
    } else if (key == "+" || key == "=") {
        adjustBrightness(16);
    } else if (key == "r") {
        const bool inverted = M5Cardputer.Display.getInvert();
        M5Cardputer.Display.invertDisplay(!inverted);
    } else {
        return;
    }
    drawSettingsApp();
}

// ===== POWER =====

static bool powerScreenReady = false;
static char powerLastBat[8] = "";
static char powerLastVolt[12] = "";
static char powerLastCurr[12] = "";
static char powerLastChg[8] = "";
static char powerLastVbus[12] = "";
static constexpr int POWER_TEXT_SIZE = 2;

// 仅重绘变化的电源信息行
static void updatePowerLine(const int y, const char* label, const char* value, char* cache,
                            const size_t cache_size) {
    if (strncmp(cache, value, cache_size) == 0 && cache[0] != '\0') {
        return;
    }

    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, 220, INFO_LINE_H_2X, BLACK);
    drawInfoLineAt(APP_CONTENT_X, y, label, value, POWER_TEXT_SIZE);
    strncpy(cache, value, cache_size - 1);
    cache[cache_size - 1] = '\0';
}

// 电源信息：首帧全屏，之后只刷新内容区
void drawPowerApp(const bool full_init) {
    char bat[8];
    char volt[12];
    char curr[12];
    char chg[8];
    char vbus[12];

    snprintf(bat, sizeof(bat), "%d%%", M5Cardputer.Power.getBatteryLevel());
    snprintf(volt, sizeof(volt), "%dmV", M5Cardputer.Power.getBatteryVoltage());

    const int32_t curr_ma = M5Cardputer.Power.getBatteryCurrent();
    const int16_t vbus_mv = M5Cardputer.Power.getVBUSVoltage();
    const bool pwr_detail_supported = vbus_mv >= 0;

    if (pwr_detail_supported) {
        snprintf(curr, sizeof(curr), "%dmA", static_cast<int>(curr_ma));
        snprintf(vbus, sizeof(vbus), "%dmV", vbus_mv);
    } else {
        strncpy(curr, "N/A", sizeof(curr));
        strncpy(vbus, "N/A", sizeof(vbus));
    }

    strncpy(chg, getChargingStatusText(), sizeof(chg));
    chg[sizeof(chg) - 1] = '\0';

    if (full_init || !powerScreenReady) {
        beginAppScreen("Pwr");
        powerScreenReady = true;
        powerLastBat[0] = '\0';
        powerLastVolt[0] = '\0';
        powerLastCurr[0] = '\0';
        powerLastChg[0] = '\0';
        powerLastVbus[0] = '\0';
    }

    int y = APP_CONTENT_Y;
    updatePowerLine(y, "bat", bat, powerLastBat, sizeof(powerLastBat));
    y += INFO_LINE_H_2X;
    updatePowerLine(y, "volt", volt, powerLastVolt, sizeof(powerLastVolt));
    y += INFO_LINE_H_2X;
    updatePowerLine(y, "curr", curr, powerLastCurr, sizeof(powerLastCurr));
    y += INFO_LINE_H_2X;
    updatePowerLine(y, "chg", chg, powerLastChg, sizeof(powerLastChg));
    y += INFO_LINE_H_2X;
    updatePowerLine(y, "vbus", vbus, powerLastVbus, sizeof(powerLastVbus));
}

void enterPowerApp() {
    powerScreenReady = false;
    powerLastBat[0] = '\0';
    powerLastVolt[0] = '\0';
    powerLastCurr[0] = '\0';
    powerLastChg[0] = '\0';
    powerLastVbus[0] = '\0';
    drawPowerApp(true);
}

// ===== SPEAKER =====

void drawSpeakerApp(const String& key) {
    beginAppScreen("Spk");
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    if (key.length() == 1 && key[0] >= '1' && key[0] <= '9') {
        M5Cardputer.Display.printf("tone: %d Hz\n", 440 + (key[0] - '1') * 110);
    } else {
        M5Cardputer.Display.println("1-9 tone");
        M5Cardputer.Display.println("0 stop");
    }
}

void handleSpeakerApp(const String& key) {
    if (key.length() == 1 && key[0] >= '1' && key[0] <= '9') {
        const int n = key[0] - '1';
        const int freq = 440 + n * 110;
        M5Cardputer.Speaker.tone(freq, 300);
    } else if (key == "0") {
        M5Cardputer.Speaker.stop();
    }
    drawSpeakerApp(key);
}

// ===== RTC =====

static bool rtcScreenReady = false;
static bool rtcSyncTimedOut = false;
static char rtcLastTime[16] = "";
static char rtcLastDate[20] = "";
static char rtcLastSrc[8] = "";
static constexpr int RTC_TIME_TEXT_SIZE = 3;
static constexpr int RTC_DATE_TEXT_SIZE = 2;
static constexpr int RTC_TIME_LINE_H = 8 * RTC_TIME_TEXT_SIZE;
static constexpr int RTC_DATE_LINE_H = 8 * RTC_DATE_TEXT_SIZE;
static constexpr int RTC_TIME_DATE_GAP = 6;
static constexpr int RTC_SRC_TEXT_SIZE = 1;
static constexpr int RTC_SRC_LINE_H = INFO_LINE_H;
static constexpr int RTC_FAIL_TEXT_SIZE = 2;
static constexpr uint32_t RTC_SYNC_TIMEOUT_MS = 5000;
static constexpr uint16_t RTC_SRC_COLOR = APP_COLOR_LABEL; // 来源标识专用色

// 内容区高度（不含顶栏）
static int rtcContentHeight() {
    return M5Cardputer.Display.height() - APP_CONTENT_Y;
}

// 主时间垂直起始 y（时间+日期块在内容区居中，底部留给来源行）
static int rtcTimeY() {
    const int block_h = RTC_TIME_LINE_H + RTC_TIME_DATE_GAP + RTC_DATE_LINE_H;
    const int avail_h = rtcContentHeight() - RTC_SRC_LINE_H - 4;
    return APP_CONTENT_Y + (avail_h - block_h) / 2;
}

static int rtcDateY() {
    return rtcTimeY() + RTC_TIME_LINE_H + RTC_TIME_DATE_GAP;
}

// 来源行贴近内容区左下角
static int rtcSrcY() {
    return M5Cardputer.Display.height() - RTC_SRC_LINE_H - 2;
}

// 按字号水平居中
static int rtcCenteredX(const char* text, const int text_size) {
    M5Cardputer.Display.setTextSize(text_size);
    const int tw = M5Cardputer.Display.textWidth(text);
    return (M5Cardputer.Display.width() - tw) / 2;
}

// 仅重绘居中时分秒
void updateRtcTimeText(const char* time_buf) {
    if (strcmp(time_buf, rtcLastTime) == 0) {
        return;
    }

    const int y = rtcTimeY();
    M5Cardputer.Display.fillRect(0, y, M5Cardputer.Display.width(), RTC_TIME_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(RTC_TIME_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(rtcCenteredX(time_buf, RTC_TIME_TEXT_SIZE), y);
    M5Cardputer.Display.print(time_buf);
    strncpy(rtcLastTime, time_buf, sizeof(rtcLastTime) - 1);
    rtcLastTime[sizeof(rtcLastTime) - 1] = '\0';
}

// 仅重绘居中日期
void updateRtcDateText(const char* date_buf) {
    if (strcmp(date_buf, rtcLastDate) == 0) {
        return;
    }

    const int y = rtcDateY();
    M5Cardputer.Display.fillRect(0, y, M5Cardputer.Display.width(), RTC_DATE_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(RTC_DATE_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(rtcCenteredX(date_buf, RTC_DATE_TEXT_SIZE), y);
    M5Cardputer.Display.print(date_buf);
    strncpy(rtcLastDate, date_buf, sizeof(rtcLastDate) - 1);
    rtcLastDate[sizeof(rtcLastDate) - 1] = '\0';
}

// 左下角 1x 来源标识（NTP / RTC）
void updateRtcSourceText(const char* source) {
    if (strcmp(source, rtcLastSrc) == 0) {
        return;
    }

    const int y = rtcSrcY();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, 48, RTC_SRC_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(RTC_SRC_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(RTC_SRC_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(source);
    strncpy(rtcLastSrc, source, sizeof(rtcLastSrc) - 1);
    rtcLastSrc[sizeof(rtcLastSrc) - 1] = '\0';
}

// 时间界面中间状态（连接 WiFi / NTP 同步）
static void drawRtcBusyScreen(const char* msg) {
    beginAppScreen("Time");
    rtcScreenReady = true;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.println(msg);
}

// 通过 NTP 同步系统时间，并写回硬件 RTC（调用前须已连 WiFi，deadline_ms 为总截止时间）
static bool trySyncNtpTime(const uint32_t deadline_ms) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");

    struct tm timeinfo{};
    while (static_cast<int32_t>(millis() - deadline_ms) < 0) {
        if (getLocalTime(&timeinfo, 200)) {
            if (M5.Rtc.isEnabled()) {
                M5.Rtc.setDateTime(&timeinfo);
                M5.Rtc.setSystemTimeFromRtc();
            }
            return true;
        }
        delay(100);
    }
    return false;
}

// 读取当前时间：优先硬件 RTC，其次系统时间
static bool readCurrentTime(struct tm& out, const char*& source) {
    if (M5.Rtc.isEnabled()) {
        const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
        if (dt.date.year >= 2020) {
            out.tm_year = dt.date.year - 1900;
            out.tm_mon = dt.date.month - 1;
            out.tm_mday = dt.date.date;
            out.tm_hour = dt.time.hours;
            out.tm_min = dt.time.minutes;
            out.tm_sec = dt.time.seconds;
            out.tm_wday = dt.date.weekDay;
            source = "RTC";
            return true;
        }
    }

    const time_t now = time(nullptr);
    if (now > 1600000000) {
        localtime_r(&now, &out);
        source = "NTP";
        return true;
    }

    source = "none";
    return false;
}

// RTC / NTP 时钟：首帧全屏，之后只刷新变化的时间文字
void drawRtcApp(const bool full_init) {
    struct tm timeinfo{};
    const char* source = "none";
    if (!readCurrentTime(timeinfo, source)) {
        if (!full_init && rtcScreenReady) {
            return;
        }
        beginAppScreen("Time");
        rtcScreenReady = true;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
        int y = APP_CONTENT_Y;
        drawInfoLineAt(APP_CONTENT_X, y, "time", "not set", RTC_FAIL_TEXT_SIZE);
        y += INFO_LINE_H_2X;
        const AppConfig& cfg = getAppConfig();
        if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
            drawInfoLineAt(APP_CONTENT_X, y, "hint", "set WiFi cfg", RTC_FAIL_TEXT_SIZE);
        } else if (rtcSyncTimedOut) {
            drawInfoLineAt(APP_CONTENT_X, y, "hint", "timeout", RTC_FAIL_TEXT_SIZE);
        } else {
            drawInfoLineAt(APP_CONTENT_X, y, "hint", "wifi/ntp fail", RTC_FAIL_TEXT_SIZE);
        }
        return;
    }

    char time_buf[16];
    char date_buf[20];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

    if (full_init || !rtcScreenReady) {
        beginAppScreen("Time");
        rtcScreenReady = true;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
    }
    updateRtcTimeText(time_buf);
    updateRtcDateText(date_buf);
    updateRtcSourceText(source);
}

void enterRtcApp() {
    rtcScreenReady = false;
    rtcSyncTimedOut = false;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';

    const AppConfig& cfg = getAppConfig();
    if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
        const uint32_t deadline = millis() + RTC_SYNC_TIMEOUT_MS;
        drawRtcBusyScreen("wifi connecting...");

        uint32_t remain = deadline - millis();
        const bool wifi_ok = remain > 0 && ensureConfigWifi(remain);
        if (!wifi_ok) {
            rtcSyncTimedOut = static_cast<int32_t>(millis() - deadline) >= 0;
        } else if (static_cast<int32_t>(millis() - deadline) < 0) {
            drawRtcBusyScreen("ntp syncing...");
            if (!trySyncNtpTime(deadline)) {
                rtcSyncTimedOut = static_cast<int32_t>(millis() - deadline) >= 0;
            }
        } else {
            rtcSyncTimedOut = true;
        }
        releaseConfigWifi();
    }

    drawRtcApp(true);
}

// ===== IN I2C =====

// 绘制 I2C 扫描结果（IN I2C / EX I2C 共用）
void drawI2cScanApp(m5::I2C_Class& bus, const char* title) {
    bool found[120]{};
    if (bus.isEnabled()) {
        bus.scanID(found);
    }

    M5Cardputer.Display.clear();
    drawAppScreenHeader(title);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);

    if (!bus.isEnabled()) {
        M5Cardputer.Display.println("bus disabled");
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

// 屏幕色彩测试
void drawDisplayApp(const int colorIndex) {
    static const uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE};
    static const char* names[] = {"RED", "GREEN", "BLUE", "YEL", "CYAN", "MAG", "WHT"};
    static const int colorCount = 7;

    const int idx = colorIndex % colorCount;
    M5Cardputer.Display.fillScreen(colors[idx]);
    drawAppScreenHeader("Disp");
    M5Cardputer.Display.setTextColor(BLACK, colors[idx]);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.printf("color: %s\n", names[idx]);
    M5Cardputer.Display.println("1-7 switch");
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void handleDisplayApp(const String& key) {
    if (key.length() != 1 || key[0] < '1' || key[0] > '7') {
        return;
    }
    drawDisplayApp(key[0] - '1');
}

// ===== CIRCLE =====

static int circleRadius = 30;

// 像素比例测试：正圆 + 十字线，+/- 调整半径
void drawCircleTestApp() {
    M5Cardputer.Display.clear();

    const int cx = M5Cardputer.Display.width() / 2;
    const int cy = (M5Cardputer.Display.height() + APP_HEADER_H) / 2;

    M5Cardputer.Display.drawCircle(cx, cy, circleRadius, WHITE);
    M5Cardputer.Display.drawFastHLine(cx - circleRadius, cy, circleRadius * 2 + 1, DARKGREY);
    M5Cardputer.Display.drawFastVLine(cx, cy - circleRadius, circleRadius * 2 + 1, DARKGREY);

    drawAppScreenHeader("Circ");
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.printf("r=%d\n", circleRadius);
    M5Cardputer.Display.println("+/- size");
}

// +/- 调整圆半径
void handleCircleApp(const String& key) {
    if (key == "+" || key == "=") {
        circleRadius = min(circleRadius + 2, 55);
        drawCircleTestApp();
    } else if (key == "-") {
        circleRadius = max(circleRadius - 2, 5);
        drawCircleTestApp();
    }
}

// ===== ICONS =====

struct IconDemoItem {
    const char* name;
    int width;
    int height;
    void (*draw)(int x, int y);
};

static int iconDemoPage = 0;
static constexpr int ICON_DEMO_ITEMS_PER_PAGE = 1;
static constexpr int ICON_DEMO_SIZE = 64;

// 将小图标居中绘制在 ICON_DEMO_SIZE 方框内
static void drawDemoInBox(const int x, const int y, const int w, const int h,
                          void (*draw)(int, int)) {
    draw(x + (ICON_DEMO_SIZE - w) / 2, y + (ICON_DEMO_SIZE - h) / 2);
}

static void drawDemoLogo(const int x, const int y) { drawAppLogo(x, y, ICON_DEMO_SIZE); }
static void drawDemoArrowLeft(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowLeft(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowRight(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowRight(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowUp(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowUp(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowDown(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowDown(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowLeftRight(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_LR_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowLeftRight(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowUpDown(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_UD_H, [](const int bx, const int by) {
        drawIconArrowUpDown(bx, by + 7, WHITE);
    });
}
static void drawDemoSignalBars(const int x, const int y) {
    drawDemoInBox(x, y, ICON_SIGNAL_W, ICON_SIGNAL_H, [](const int bx, const int by) {
        drawSignalBars(bx, by, -56, WHITE);
    });
}
static void drawDemoWifi(const int x, const int y) {
    drawDemoInBox(x, y, ICON_WIFI_W, ICON_WIFI_H, [](const int bx, const int by) {
        drawIconWifi(bx, by, -56, WHITE);
    });
}
static void drawDemoBle(const int x, const int y) {
    drawDemoInBox(x, y, ICON_BLE_W, ICON_BLE_H,
                  [](const int bx, const int by) { drawIconBle(bx, by, WHITE); });
}
static void drawDemoChargingBolt(const int x, const int y) {
    drawDemoInBox(x, y, 6, 12, [](const int bx, const int by) { drawIconChargingBolt(bx, by, 12); });
}
static void drawDemoBattery(const int x, const int y) {
    const int bw = getIconBatteryDisplayWidth(false);
    const int bh = getIconBatteryBodyHeight();
    drawDemoInBox(x, y, bw, bh,
                  [](const int bx, const int by) { drawIconBattery(bx, by, 75, false); });
}
static void drawDemoPageDots(const int x, const int y) {
    drawDemoInBox(x, y, 22, 4, [](const int bx, const int by) { drawIconPageDots(bx, by + 2, 1, 4); });
}
static void drawDemoInfoChip(const int x, const int y) {
    drawIconInfoChipSized(x, y, WHITE, ICON_DEMO_SIZE);
}
static void drawDemoInfoStorage(const int x, const int y) {
    drawIconInfoStorageSized(x, y, WHITE, ICON_DEMO_SIZE);
}
static void drawDemoInfoBattery(const int x, const int y) {
    drawIconInfoBatterySized(x, y, WHITE, ICON_DEMO_SIZE);
}
static void drawDemoMijiaLight(const int x, const int y) {
    drawMijiaDeviceIcon(MijiaDevKind::LIGHT, x, y, WHITE, ICON_DEMO_SIZE / MIJIA_ICON_BASE);
}
static void drawDemoMijiaFan(const int x, const int y) {
    drawMijiaDeviceIcon(MijiaDevKind::FAN_GENERIC, x, y, WHITE, ICON_DEMO_SIZE / MIJIA_ICON_BASE);
}
static void drawDemoMijiaAirFryer(const int x, const int y) {
    drawMijiaDeviceIcon(MijiaDevKind::AIR_FRYER, x, y, WHITE, ICON_DEMO_SIZE / MIJIA_ICON_BASE);
}
static void drawDemoPngFan(const int x, const int y) {
    drawDevicePngFile("/img/fan@2x.png", x, y, ICON_DEMO_SIZE);
}
static void drawDemoPngPurifier(const int x, const int y) {
    drawDevicePngFile("/img/air_normal@2x.png", x, y, ICON_DEMO_SIZE);
}
static void drawDemoPngPlug(const int x, const int y) {
    drawDevicePngFile("/img/switch_on@2x.png", x, y, ICON_DEMO_SIZE);
}
static void drawDemoPngDefault(const int x, const int y) {
    drawDevicePngFile("/img/default@2x.png", x, y, ICON_DEMO_SIZE);
}
// 米家设备原生 PNG（52×52，不缩放，居中展示）
static void drawDemoNativeLamp(const int x, const int y) {
    drawDemoInBox(x, y, DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, [](const int bx, const int by) {
        drawDevicePngNative("/icon/device/lamp.png", bx, by);
    });
}
static void drawDemoNativeBedlight(const int x, const int y) {
    drawDemoInBox(x, y, DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, [](const int bx, const int by) {
        drawDevicePngNative("/icon/device/bedlight.png", bx, by);
    });
}
static void drawDemoNativeBlumb(const int x, const int y) {
    drawDemoInBox(x, y, DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, [](const int bx, const int by) {
        drawDevicePngNative("/icon/device/blumb.png", bx, by);
    });
}
static void drawDemoPowerOn(const int x, const int y) {
    drawIconPower(x, y, APP_COLOR_OK, ICON_DEMO_SIZE);
}
static void drawDemoPowerOff(const int x, const int y) {
    drawIconPower(x, y, APP_COLOR_HINT, ICON_DEMO_SIZE);
}

static const IconDemoItem ICON_DEMO_ITEMS[] = {
    {"app logo", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoLogo},
    {"arrow left", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowLeft},
    {"arrow right", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowRight},
    {"arrow up", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowUp},
    {"arrow down", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowDown},
    {"arrow left-right", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowLeftRight},
    {"arrow up-down", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowUpDown},
    {"signal bars", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoSignalBars},
    {"wifi", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoWifi},
    {"ble", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoBle},
    {"charging bolt", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoChargingBolt},
    {"battery", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoBattery},
    {"page dots", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPageDots},
    {"info chip", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoInfoChip},
    {"info storage", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoInfoStorage},
    {"info battery", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoInfoBattery},
    {"mijia light", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoMijiaLight},
    {"mijia fan", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoMijiaFan},
    {"mijia fryer", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoMijiaAirFryer},
    {"device lamp", DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, drawDemoNativeLamp},
    {"device bedlight", DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, drawDemoNativeBedlight},
    {"device blumb", DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, drawDemoNativeBlumb},
    {"png fan", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPngFan},
    {"png purifier", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPngPurifier},
    {"png plug", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPngPlug},
    {"png default", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPngDefault},
    {"power on", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPowerOn},
    {"power off", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPowerOff},
};

static int getIconDemoItemCount() {
    return sizeof(ICON_DEMO_ITEMS) / sizeof(ICON_DEMO_ITEMS[0]);
}

static int getIconDemoPageCount() {
    const int total = getIconDemoItemCount();
    return (total + ICON_DEMO_ITEMS_PER_PAGE - 1) / ICON_DEMO_ITEMS_PER_PAGE;
}

static void drawIconDemoApp() {
    beginAppScreen("Icons");
    const int page_count = getIconDemoPageCount();
    if (iconDemoPage >= page_count) {
        iconDemoPage = page_count - 1;
    }
    if (iconDemoPage < 0) {
        iconDemoPage = 0;
    }

    int y = APP_CONTENT_Y;
    char buf[32];
    static const KeyHintItem nav_items[] = {
        {'[', "prev"},
        {']', "next"},
    };
    drawKeyHintsRow(APP_CONTENT_X, y, nav_items, sizeof(nav_items) / sizeof(nav_items[0]), 1,
                    APP_COLOR_HINT);
    snprintf(buf, sizeof(buf), "page %d/%d", iconDemoPage + 1, page_count);
    M5Cardputer.Display.setCursor(APP_CONTENT_X + 168, y);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(buf);
    y += INFO_LINE_H + 4;

    const int start = iconDemoPage * ICON_DEMO_ITEMS_PER_PAGE;
    int end = start + ICON_DEMO_ITEMS_PER_PAGE;
    const int total = getIconDemoItemCount();
    if (end > total) {
        end = total;
    }

    for (int i = start; i < end; i++) {
        const IconDemoItem& item = ICON_DEMO_ITEMS[i];
        const int row_y = y;

        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, row_y);
        M5Cardputer.Display.printf("%02d %s", i + 1, item.name);

        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, row_y + INFO_LINE_H_2X);
        M5Cardputer.Display.printf("size %dx%d", item.width, item.height);

        const int icon_x = M5Cardputer.Display.width() - APP_CONTENT_X - ICON_DEMO_SIZE;
        const int label_bottom = row_y + INFO_LINE_H_2X + INFO_LINE_H + 4;
        const int avail_h = M5Cardputer.Display.height() - label_bottom - 4;
        const int icon_y = label_bottom + (avail_h - ICON_DEMO_SIZE) / 2;
        item.draw(icon_x, icon_y);

        y += ICON_DEMO_SIZE + INFO_LINE_H_2X + INFO_LINE_H + 16;
    }
}

static void enterIconDemoApp() {
    iconDemoPage = 0;
    drawIconDemoApp();
}

static void handleIconDemoNav(const Keyboard_Class::KeysState& status) {
    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return;
    }
    const int page_count = getIconDemoPageCount();
    iconDemoPage = (iconDemoPage + delta + page_count) % page_count;
    drawIconDemoApp();
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

// Cardputer BtnA (GO) = GPIO0，RTC 引脚，支持 ext0 唤醒
static constexpr gpio_num_t SLEEP_WAKE_PIN = GPIO_NUM_0;
static constexpr uint32_t SLEEP_PROMPT_MS = 5000;

// 入睡前断开无线
static void shutdownRadiosForSleep() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
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
    sleepSavedBrightness = M5Cardputer.Display.getBrightness();
    M5Cardputer.Display.sleep();
    M5Cardputer.Display.waitDisplay();
    M5Cardputer.Display.setBrightness(0);
    shutdownRadiosForSleep();
    flushCardputerInput();
    prepareBtnAWake();
    esp_light_sleep_start();

    flushCardputerInput();
    sleepPhase = SleepPhase::NONE;
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.setBrightness(sleepSavedBrightness);
    showMenu();
}

// 深度休眠：关屏关无线后 CPU 断电，仅 BtnA 可唤醒（唤醒后重启）
static void enterDeepSleep() {
    M5Cardputer.Display.sleep();
    M5Cardputer.Display.waitDisplay();
    M5Cardputer.Display.setBrightness(0);
    shutdownRadiosForSleep();
    prepareBtnAWake();
    esp_deep_sleep_start();
}

// 浅休眠提示：默认路径，倒计时内按 s 可切到深度休眠
static void drawLightSleepPrompt(const int seconds_left) {
    beginAppScreen("Sleep");

    int y = APP_CONTENT_Y + 8;
    drawInfoLineAt(APP_CONTENT_X, y, "LIGHT", "SLEEP", 2);
    y += INFO_LINE_H_2X + 4;

    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", seconds_left);
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(buf);
    y += 30;

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("wake: BtnA (GO)");
    y += INFO_LINE_H_2X + 2;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("press s again for deep sleep");
}

// 深度休眠提示
static void drawDeepSleepPrompt(const int seconds_left) {
    beginAppScreen("Sleep");

    int y = APP_CONTENT_Y + 8;
    drawInfoLineAt(APP_CONTENT_X, y, "DEEP", "SLEEP", 2);
    y += INFO_LINE_H_2X + 4;

    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", seconds_left);
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(buf);
    y += 30;

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("wake: BtnA (GO)");
    y += INFO_LINE_H_2X + 2;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("reboot on wake");
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
        if (sleepPhase == SleepPhase::PROMPT_DEEP) {
            drawDeepSleepPrompt(sec_left);
        } else {
            drawLightSleepPrompt(sec_left);
        }
    }
}

// ===== MAIN =====

void enterApp(const AppState state) {
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
        case AppState::INFO:
            enterInfoApp();
            break;
        case AppState::MIC:
            micHeaderReady = false;
            drawMicApp();
            break;
        case AppState::POWER:
            enterPowerApp();
            break;
        case AppState::SPEAKER:
            drawSpeakerApp("");
            break;
        case AppState::RTC:
            enterRtcApp();
            break;
        case AppState::IN_I2C:
            drawI2cScanApp(M5Cardputer.In_I2C, "InI2");
            break;
        case AppState::EX_I2C:
            drawI2cScanApp(M5Cardputer.Ex_I2C, "ExI2");
            break;
        case AppState::WIFI:
            enterWifiApp();
            break;
        case AppState::BLE:
            enterBleApp();
            break;
        case AppState::DISP:
            drawDisplayApp(0);
            break;
        case AppState::CIRCLE:
            drawCircleTestApp();
            break;
        case AppState::ICONS:
            enterIconDemoApp();
            break;
        case AppState::SETTINGS:
            drawSettingsApp();
            break;
        case AppState::MIJIA:
            enterMijiaApp();
            break;
        case AppState::WEB:
            enterWebApp();
            break;
        case AppState::COUNTDOWN:
            enterCountdownApp();
            break;
        case AppState::STOPWATCH:
            enterStopwatchApp();
            break;
        default:
            break;
    }
}

void setup() {
    const auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    Serial.begin(115200);
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("wake: BtnA from deep sleep");
    }
    if (initAppConfigFs()) {
        if (loadAppConfig()) {
            Serial.printf("config: %d mijia device(s)\n", getAppConfig().device_count);
        } else {
            Serial.println("config: /config.json missing or invalid");
        }
    } else {
        Serial.println("config: LittleFS mount failed");
    }
    WiFi.mode(WIFI_OFF);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(30);
    flushCardputerInput();
    showMenu();
}

void loop() {
    M5Cardputer.update();

    // 休眠提示倒计时
    if (sleepPhase == SleepPhase::PROMPT_LIGHT || sleepPhase == SleepPhase::PROMPT_DEEP) {
        updateSleepPrompt();
        if (M5Cardputer.BtnA.wasPressed()) {
            sleepPhase = SleepPhase::NONE;
            showMenu();
            return;
        }
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const String key = getPressedKey();
            if (key == "s" && sleepPhase == SleepPhase::PROMPT_LIGHT) {
                switchToDeepSleepPrompt();
            }
        }
        return;
    }

    // BtnA：非菜单界面短按返回菜单
    if (M5Cardputer.BtnA.wasPressed()) {
        if (currentState != AppState::MENU) {
            showMenu();
        }
    }

    const uint32_t now = millis();

    // 主菜单 / 子界面 header 状态定时刷新
    static uint32_t lastHeaderStatusMs = 0;
    if (now - lastHeaderStatusMs >= 2000) {
        lastHeaderStatusMs = now;
        if (currentState == AppState::MENU) {
            updateMenuHeaderStatus(getMenuPageCount());
        } else if (currentState != AppState::SLEEP) {
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
        static uint32_t lastMicUpdateMs = 0;
        if (now - lastMicUpdateMs >= 40) {
            lastMicUpdateMs = now;
            drawMicApp();
        }
    } else if (currentState == AppState::POWER) {
        static uint32_t lastPowerUpdateMs = 0;
        if (now - lastPowerUpdateMs >= 500) {
            lastPowerUpdateMs = now;
            drawPowerApp(false);
        }
    } else if (currentState == AppState::WIFI) {
        updateWifiApp();
    } else if (currentState == AppState::BLE) {
        static uint32_t lastBleUpdateMs = 0;
        if (now - lastBleUpdateMs >= 500) {
            lastBleUpdateMs = now;
            updateBleApp();
        }
    } else if (currentState == AppState::MIJIA) {
        updateMijiaApp();
    } else if (currentState == AppState::WEB) {
        updateWebApp();
    } else if (currentState == AppState::COUNTDOWN) {
        updateCountdownApp();
    } else if (currentState == AppState::STOPWATCH) {
        updateStopwatchApp();
    }

    if (currentState == AppState::RTC) {
        static uint32_t lastRtcUpdateMs = 0;
        if (now - lastRtcUpdateMs >= 1000) {
            lastRtcUpdateMs = now;
            drawRtcApp(false);
        }
    }

    if (M5Cardputer.Keyboard.isChange()) {
        switch (currentState) {
            case AppState::MENU:
                if (M5Cardputer.Keyboard.isPressed()) {
                    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
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
            case AppState::SPEAKER:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleSpeakerApp(getPressedKey());
                }
                break;
            case AppState::WIFI:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleWifiApp(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::DISP:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleDisplayApp(getPressedKey());
                }
                break;
            case AppState::CIRCLE:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleCircleApp(getPressedKey());
                }
                break;
            case AppState::INFO:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleInfoPageNav(M5Cardputer.Keyboard.keysState());
                }
                break;
            case AppState::MIJIA:
                if (M5Cardputer.Keyboard.isPressed()) {
                    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
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
            case AppState::WEB:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleWebApp(getPressedKey());
                }
                break;
            case AppState::COUNTDOWN:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleCountdownApp(getPressedKey());
                }
                break;
            case AppState::STOPWATCH:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleStopwatchApp(getPressedKey());
                }
                break;
            default:
                break;
        }
    }

    // 实时 app 不休眠；其它状态 yield 10ms
    if (currentState != AppState::BMI && currentState != AppState::MIC &&
        currentState != AppState::STOPWATCH) {
        delay(10);
    }
}
