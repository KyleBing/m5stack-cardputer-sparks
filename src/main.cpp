#include "M5Cardputer.h"
#include "app_config.h"
#include "app_logo.h"
#include "app_header.h"
#include "app_common.h"
#include "app_web.h"
#include "app_wifi.h"
#include "app_mijia.h"
#include <BLEDevice.h>
#include <WiFi.h>
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
    SLEEP,
    MIJIA,
    WEB,
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
    {'u', "Web", "Config Web", AppState::WEB},
    {'b', "BLE", "BLE", AppState::BLE},
    {'d', "Disp", "Display", AppState::DISP},
    {'c', "Circ", "Circle", AppState::CIRCLE},
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

static constexpr const char* APP_NAME = "Cardputer";

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
static constexpr int MENU_LINE_H = 16;

static int menuPage = 0;

// 计算菜单总页数
int getMenuPageCount() {
    return (MENU_ITEM_COUNT + MENU_ITEMS_PER_PAGE - 1) / MENU_ITEMS_PER_PAGE;
}

// 检测翻页键：-1 上一页，0 无，1 下一页（直接按 ; , . /，无需 Fn）
int getMenuNavDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x50 || hid == 0x33 || hid == 0x36) {
            return -1;  // Up / Left / ; ,
        }
        if (hid == 0x51 || hid == 0x4F || hid == 0x37 || hid == 0x38) {
            return 1;   // Down / Right / . /
        }
    }
    for (const char c : status.word) {
        if (c == ';' || c == ',') {
            return -1;
        }
        if (c == '.' || c == '/') {
            return 1;
        }
    }
    return 0;
}

// 绘制单个菜单项：触发字母与菜单名各用一种固定颜色
void drawMenuItem(const MenuItem& item) {
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.printf("%c.", toupper(item.key));
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.printf("%s", item.name);
}

// 绘制主菜单当前页
void drawMenuPage() {
    const int startIdx = menuPage * MENU_ITEMS_PER_PAGE;
    const int endIdx = (startIdx + MENU_ITEMS_PER_PAGE < MENU_ITEM_COUNT)
                           ? startIdx + MENU_ITEMS_PER_PAGE
                           : MENU_ITEM_COUNT;

    M5Cardputer.Display.setTextSize(2);
    int row = 0;
    for (int i = startIdx; i < endIdx; i += MENU_COLS) {
        const int y = APP_CONTENT_Y + row * (MENU_LINE_H + GAP_VERTICAL );
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        drawMenuItem(MENU_ITEMS[i]);
        if (i + 1 < endIdx) {
            M5Cardputer.Display.print(" ");
            drawMenuItem(MENU_ITEMS[i + 1]);
        }
        if (i + 2 < endIdx) {
            M5Cardputer.Display.print(" ");
            drawMenuItem(MENU_ITEMS[i + 2]);
        }
        row++;
    }
}

// 绘制主菜单（header + 可翻页菜单区）
void showMenu() {
    stopConfigWebServer();
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
static void drawModLabel(const int x, int& y, const char* label, const bool active,
                         const uint16_t activeColor) {
    constexpr int lineH = 18;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(active ? activeColor : DARKGREY, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
    y += lineH;
}

void drawKeyboardApp(const Keyboard_Class::KeysState& status) {
    beginAppScreen("Key");

    constexpr int modX = APP_CONTENT_X;
    int modY = APP_CONTENT_Y;

    drawModLabel(modX, modY, "Fn", status.fn, ORANGE);
    drawModLabel(modX, modY, "Aa", status.shift, BLUE);
    drawModLabel(modX, modY, "opt", status.opt, GREEN);
    drawModLabel(modX, modY, "ctrl", status.ctrl, WHITE);
    drawModLabel(modX, modY, "alt", status.alt, WHITE);

    const String label = getKeyLabel(status);
    if (label != "-") {
        strncpy(lastKeyLabel, label.c_str(), sizeof(lastKeyLabel) - 1);
        lastKeyLabel[sizeof(lastKeyLabel) - 1] = '\0';
        Serial.println(label);
    }

    constexpr int keyPanelX = 96;
    const int keyPanelY = APP_CONTENT_Y;
    const int keyPanelW = M5Cardputer.Display.width() - keyPanelX - 4;
    const int keyPanelH = M5Cardputer.Display.height() - keyPanelY;
    M5Cardputer.Display.fillRect(keyPanelX, keyPanelY, keyPanelW, keyPanelH, BLACK);

    const size_t len = strlen(lastKeyLabel);
    const int textSize = len <= 2 ? 4 : (len <= 4 ? 3 : 2);
    M5Cardputer.Display.setTextSize(textSize);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    const int textH = 8 * textSize;
    M5Cardputer.Display.drawCenterString(lastKeyLabel, keyPanelX + keyPanelW / 2,
                                         keyPanelY + (keyPanelH - textH) / 2);
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

// 绘制板级 Info 页面
void drawInfoApp() {
    const esp_chip_info_t chipInfo = []() {
        esp_chip_info_t info{};
        esp_chip_info(&info);
        return info;
    }();

    beginAppScreen("Info");
    M5Cardputer.Display.setTextSize(1);

    int y = APP_CONTENT_Y;
    char buf[32];

    drawInfoLine(APP_CONTENT_X, y, "model", ESP.getChipModel());
    drawInfoLineInt(APP_CONTENT_X, y, "cores", chipInfo.cores);
    snprintf(buf, sizeof(buf), "%d MHz", ESP.getCpuFreqMHz());
    drawInfoLine(APP_CONTENT_X, y, "freq", buf);
    snprintf(buf, sizeof(buf), "%d MB", ESP.getFlashChipSize() / (1024 * 1024));
    drawInfoLine(APP_CONTENT_X, y, "flash", buf);
    snprintf(buf, sizeof(buf), "%d KB", ESP.getFreeHeap() / 1024);
    drawInfoLine(APP_CONTENT_X, y, "heap", buf);
    drawInfoLine(APP_CONTENT_X, y, "sdk", ESP.getSdkVersion());

    const AppConfig& cfg = getAppConfig();
    if (cfg.loaded) {
        snprintf(buf, sizeof(buf), "%d", cfg.device_count);
        drawInfoLine(APP_CONTENT_X, y, "cfg", buf);
        if (cfg.device_count > 0) {
            drawInfoLine(APP_CONTENT_X, y, "dev0", cfg.devices[0].name);
            drawInfoLine(APP_CONTENT_X, y, "ip0", cfg.devices[0].ip);
        }
    } else {
        drawInfoLine(APP_CONTENT_X, y, "cfg", "none");
    }
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

// 电源信息
void drawPowerApp() {
    beginAppScreen("Pwr");
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.printf("bat: %d%%\n", M5Cardputer.Power.getBatteryLevel());
    M5Cardputer.Display.printf("volt: %dmV\n", M5Cardputer.Power.getBatteryVoltage());
    M5Cardputer.Display.printf("curr: %dmA\n", M5Cardputer.Power.getBatteryCurrent());
    M5Cardputer.Display.printf("chg: %s\n", M5Cardputer.Power.isCharging() ? "ON" : "OFF");
    M5Cardputer.Display.printf("vbus: %dmV\n", M5Cardputer.Power.getVBUSVoltage());
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
static char rtcLastTime[16] = "";
static char rtcLastDate[20] = "";
static char rtcLastSrc[8] = "";
static constexpr int RTC_TIME_Y = APP_CONTENT_Y + 8;
static constexpr int RTC_TIME_H = 26;
static constexpr int RTC_DETAIL_Y = APP_CONTENT_Y + 38;
static constexpr int RTC_DETAIL_LINE_H = 16;

// Time 页小字（2 号字）
void drawRtcDetailLine(const int x, int y, const char* label, const char* value) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.print(": ");
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.println(value);
}

// 仅重绘时分秒区域
void updateRtcTimeText(const char* time_buf) {
    if (strcmp(time_buf, rtcLastTime) == 0) {
        return;
    }

    M5Cardputer.Display.fillRect(APP_CONTENT_X, RTC_TIME_Y, 220, RTC_TIME_H, BLACK);
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, RTC_TIME_Y);
    M5Cardputer.Display.print(time_buf);
    strncpy(rtcLastTime, time_buf, sizeof(rtcLastTime) - 1);
    rtcLastTime[sizeof(rtcLastTime) - 1] = '\0';
}

// 日期或来源变化时重绘对应行
void updateRtcDetailLine(const int y, const char* label, const char* value, char* cache,
                         const size_t cache_size) {
    if (strncmp(cache, value, cache_size) == 0 && cache[0] != '\0') {
        return;
    }

    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, 220, RTC_DETAIL_LINE_H, BLACK);
    drawRtcDetailLine(APP_CONTENT_X, y, label, value);
    strncpy(cache, value, cache_size - 1);
    cache[cache_size - 1] = '\0';
}

// 通过 NTP 同步系统时间，并写回硬件 RTC
static bool trySyncNtpTime() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        return false;
    }
    if (!ensureConfigWifi()) {
        return false;
    }

    configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");

    struct tm timeinfo{};
    for (int i = 0; i < 10; i++) {
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
        int y = APP_CONTENT_Y;
        drawInfoLine(APP_CONTENT_X, y, "time", "not set");
        drawInfoLine(APP_CONTENT_X, y, "hint", "need WiFi sync");
        M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println("enter again after WiFi");
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
        updateRtcTimeText(time_buf);
        drawRtcDetailLine(APP_CONTENT_X, RTC_DETAIL_Y, "date", date_buf);
        strncpy(rtcLastDate, date_buf, sizeof(rtcLastDate) - 1);
        rtcLastDate[sizeof(rtcLastDate) - 1] = '\0';
        drawRtcDetailLine(APP_CONTENT_X, RTC_DETAIL_Y + RTC_DETAIL_LINE_H, "src", source);
        strncpy(rtcLastSrc, source, sizeof(rtcLastSrc) - 1);
        rtcLastSrc[sizeof(rtcLastSrc) - 1] = '\0';
    } else {
        updateRtcTimeText(time_buf);
        updateRtcDetailLine(RTC_DETAIL_Y, "date", date_buf, rtcLastDate, sizeof(rtcLastDate));
        updateRtcDetailLine(RTC_DETAIL_Y + RTC_DETAIL_LINE_H, "src", source,
                            rtcLastSrc, sizeof(rtcLastSrc));
    }
}

void enterRtcApp() {
    rtcScreenReady = false;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';
    trySyncNtpTime();
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

// BLE 状态
void drawBleApp() {
    static bool bleReady = false;
    if (!bleReady) {
        BLEDevice::init("Cardputer");
        bleReady = true;
    }

    beginAppScreen("BLE");
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.printf("addr:\n%s\n", BLEDevice::getAddress().toString().c_str());
    M5Cardputer.Display.println("name: Cardputer");
}

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

// ===== SLEEP =====

static bool displayAsleep = false;

// s 入口：直接关屏，loop 内等 BtnA 唤醒
void enterSleep() {
    displayAsleep = true;
    M5Cardputer.Display.sleep();
}

// ===== MAIN =====

void enterApp(const AppState state) {
    currentState = state;

    // Sleep 直接关屏，不刷新界面
    if (state == AppState::SLEEP) {
        enterSleep();
        return;
    }

    M5Cardputer.Display.clear();

    switch (state) {
        case AppState::VERSION:
            drawVersionApp();
            break;
        case AppState::KEYBOARD: {
            Keyboard_Class::KeysState status{};
            drawKeyboardApp(status);
            break;
        }
        case AppState::BMI:
            bmiScreenReady = false;
            drawBmiApp();
            break;
        case AppState::INFO:
            drawInfoApp();
            break;
        case AppState::MIC:
            micHeaderReady = false;
            drawMicApp();
            break;
        case AppState::POWER:
            drawPowerApp();
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
            drawBleApp();
            break;
        case AppState::DISP:
            drawDisplayApp(0);
            break;
        case AppState::CIRCLE:
            drawCircleTestApp();
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
        default:
            break;
    }
}

void setup() {
    const auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    Serial.begin(115200);
    if (initAppConfigFs()) {
        if (loadAppConfig()) {
            Serial.printf("config: %d mijia device(s)\n", getAppConfig().device_count);
        } else {
            Serial.println("config: /config.json missing or invalid");
        }
    } else {
        Serial.println("config: LittleFS mount failed");
    }
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(30);
    showMenu();
}

void loop() {
    M5Cardputer.update();

    // 休眠中只处理 BtnA 唤醒
    if (displayAsleep) {
        if (M5Cardputer.BtnA.wasPressed()) {
            M5Cardputer.Display.wakeup();
            displayAsleep = false;
            showMenu();
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

    // BMI 实时刷新，不使用 delay 以免触发 idle sleep
    if (currentState == AppState::MENU) {
        static uint32_t lastMenuBatMs = 0;
        if (now - lastMenuBatMs >= 2000) {
            lastMenuBatMs = now;
            updateMenuScreenBattery(getMenuPageCount());
        }
    } else if (currentState == AppState::BMI) {
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
            drawPowerApp();
        }
    } else if (currentState == AppState::WIFI) {
        updateWifiApp();
    }

    if (currentState == AppState::RTC) {
        static uint32_t lastRtcUpdateMs = 0;
        if (now - lastRtcUpdateMs >= 1000) {
            lastRtcUpdateMs = now;
            drawRtcApp(false);
        }
    }

    if (currentState == AppState::WEB) {
        handleConfigWebServer();
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
                drawKeyboardApp(M5Cardputer.Keyboard.keysState());
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
            case AppState::MIJIA:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleMijiaApp(getPressedKey());
                }
                break;
            default:
                break;
        }
    }

    // 实时 app 不休眠；其它状态 yield 10ms
    if (currentState != AppState::BMI && currentState != AppState::MIC) {
        delay(10);
    }
}
