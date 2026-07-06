#include "M5Cardputer.h"
#include "logo_png.h"
#include <BLEDevice.h>
#include <WiFi.h>
#include <esp_chip_info.h>
#include <esp_sleep.h>
#include <esp_system.h>

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
    BTNA,
    POWER,
    SPEAKER,
    RTC,
    IN_I2C,
    EX_I2C,
    WIFI,
    BLE,
    DISP,
    SLEEP,
};

struct MenuItem {
    char key;
    const char* name;
    AppState state;
};

// Cardputer 键盘布局（* = 已占用 app 入口，见下方映射）
//
// 行0: `  1  2  3  4  5  6  7  8  9  0  -  =  Bksp
// 行1: Tab q  w* e* r  t* y  u  i* o* p* [  ]  \
// 行2: Fn  Sh  a* s* d* f  g* h  j  k* l* ;  '  Ent
// 行3: Ctrl Opt Alt z  x  c  v* b* n* m* ,  .  /  Spc
//
// 占用: a=BtnA  b=BLE  d=Disp  e=ExI2  g=BMI  i=Info
//       k=Key  l=Spk  m=Mic  n=InI2  o=Set  p=Pwr
//       s=Sleep t=Time v=Ver  w=WiFi
// 空闲: c f h j q r u x y z

// Cardputer 技能 → 字母入口
static const MenuItem MENU_ITEMS[] = {
    {'v', "Ver", AppState::VERSION},
    {'k', "Key", AppState::KEYBOARD},
    {'g', "BMI", AppState::BMI},
    {'i', "Info", AppState::INFO},
    {'m', "Mic", AppState::MIC},
    {'o', "Set", AppState::SETTINGS},
    {'a', "BtnA", AppState::BTNA},
    {'p', "Pwr", AppState::POWER},
    {'l', "Spk", AppState::SPEAKER},
    {'s', "Slp", AppState::SLEEP},
    {'t', "Time", AppState::RTC},
    {'n', "InI2", AppState::IN_I2C},
    {'e', "ExI2", AppState::EX_I2C},
    {'w', "WiFi", AppState::WIFI},
    {'b', "BLE", AppState::BLE},
    {'d', "Disp", AppState::DISP},
};

static const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

AppState currentState = AppState::MENU;
static uint32_t btnTestCount = 0;

void enterApp(const AppState state);

// 获取当前按下的可打印字符
String getPressedKey() {
    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    String key;
    for (const char c : status.word) {
        key += c;
    }
    return key;
}

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

// 绘制主菜单（字体 2，格式：V.Ver）
void showMenu() {
    currentState = AppState::MENU;
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.setTextSize(2);

    for (int i = 0; i < MENU_ITEM_COUNT; i += 3) {
        M5Cardputer.Display.printf("%c.%s",
                                   toupper(MENU_ITEMS[i].key),
                                   MENU_ITEMS[i].name);
        if (i + 1 < MENU_ITEM_COUNT) {
            M5Cardputer.Display.printf(" %c.%s",
                                       toupper(MENU_ITEMS[i + 1].key),
                                       MENU_ITEMS[i + 1].name);
        }
        if (i + 2 < MENU_ITEM_COUNT) {
            M5Cardputer.Display.printf(" %c.%s",
                                       toupper(MENU_ITEMS[i + 2].key),
                                       MENU_ITEMS[i + 2].name);
        }
        M5Cardputer.Display.println();
    }
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
        M5Cardputer.Display.clear();
        M5Cardputer.Display.setCursor(5, 5);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.printf("No app: %c\n", toupper(c));
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.println("BtnA -> menu");
    }
}

// ===== VERSION =====

// 返回固件版本信息
VersionInfo getVersionInfo() {
    return VersionInfo{
        "0.0.1",
        "2026-07-06",
        "KyleBing",
        "kylebing@163.com",
        "kylebing.cn"
    };
}

// 绘制 Version 页面（logo 上 + ver 下，居中）
void drawVersionApp() {
    const VersionInfo info = getVersionInfo();
    M5Cardputer.Display.clear();

    constexpr int logoSrcW = 65;
    constexpr int logoSrcH = 65;
    constexpr int logoBox = 64;

    const float scaleW = static_cast<float>(logoBox) / logoSrcW;
    const float scaleH = static_cast<float>(logoBox) / logoSrcH;
    const float scale = scaleW < scaleH ? scaleW : scaleH;
    const int logoDrawH = static_cast<int>(logoSrcH * scale);

    const int logoX = (M5Cardputer.Display.width() - logoBox) / 2;
    const int logoY = 8;

    if (!M5Cardputer.Display.drawPng(
            logo_png, logo_png_len, logoX, logoY, logoBox, logoBox, 0, 0, 0.0f, 0.0f)) {
        M5Cardputer.Display.setCursor(5, 5);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.println("logo missing");
        return;
    }

    const int textY = logoY + logoDrawH + 10;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.drawCenterString(
        ("ver: " + info.version).c_str(),
        M5Cardputer.Display.width() / 2,
        textY);
}

// ===== KEYBOARD =====

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
    if (status.fn || status.shift || status.ctrl || status.opt || status.alt) {
        return "MOD";
    }
    return "-";
}

void drawKeyboardApp(const Keyboard_Class::KeysState& status) {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("KEYBOARD");

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.printf("Fn:%s Sh:%s Ct:%s\n",
                               status.fn ? "ON" : "--",
                               status.shift ? "ON" : "--",
                               status.ctrl ? "ON" : "--");
    M5Cardputer.Display.printf("Op:%s Al:%s Tb:%s\n",
                               status.opt ? "ON" : "--",
                               status.alt ? "ON" : "--",
                               status.tab ? "ON" : "--");
    M5Cardputer.Display.printf("Dl:%s En:%s Sp:%s\n",
                               status.del ? "ON" : "--",
                               status.enter ? "ON" : "--",
                               status.space ? "ON" : "--");

    const String label = getKeyLabel(status);
    M5Cardputer.Display.setTextSize(4);
    M5Cardputer.Display.printf("%s\n", label.c_str());
    Serial.println(label);
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

// BMI（IMU）页面
void drawBmiApp() {
    // 保持屏幕与 CPU 活跃，避免休眠影响 IMU 刷新
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();

    M5.Imu.update();

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);

    if (!M5.Imu.isEnabled()) {
        M5Cardputer.Display.println("BMI");
        M5Cardputer.Display.println("IMU not found");
        return;
    }

    M5Cardputer.Display.printf("BMI %s\n", getImuTypeName(M5.Imu.getType()));

    float ax = 0;
    float ay = 0;
    float az = 0;
    float gx = 0;
    float gy = 0;
    float gz = 0;
    float mx = 0;
    float my = 0;
    float mz = 0;
    float temp = 0;

    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    M5Cardputer.Display.printf("X→ %+.2f\n", ax);
    M5Cardputer.Display.printf("Y→ %+.2f\n", ay);
    M5Cardputer.Display.printf("Z→ %+.2f\n", az);
    M5Cardputer.Display.printf("Gx→ %+.0f\n", gx);
    M5Cardputer.Display.printf("Gy→ %+.0f\n", gy);
    M5Cardputer.Display.printf("Gz→ %+.0f\n", gz);

    if (M5.Imu.getMag(&mx, &my, &mz)) {
        M5Cardputer.Display.printf("Mx→ %+.0f\n", mx);
        M5Cardputer.Display.printf("My→ %+.0f\n", my);
        M5Cardputer.Display.printf("Mz→ %+.0f\n", mz);
    }
    if (M5.Imu.getTemp(&temp)) {
        M5Cardputer.Display.printf("T→ %+.1fC\n", temp);
    }
}

// ===== INFO =====

// 绘制板级 Info 页面
void drawInfoApp() {
    const esp_chip_info_t chipInfo = []() {
        esp_chip_info_t info{};
        esp_chip_info(&info);
        return info;
    }();

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("INFO");
    M5Cardputer.Display.printf("model: %s\n", ESP.getChipModel());
    M5Cardputer.Display.printf("cores: %d\n", chipInfo.cores);
    M5Cardputer.Display.printf("freq: %d MHz\n", ESP.getCpuFreqMHz());
    M5Cardputer.Display.printf("flash: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    M5Cardputer.Display.printf("heap: %d KB\n", ESP.getFreeHeap() / 1024);
    M5Cardputer.Display.printf("sdk: %s\n", ESP.getSdkVersion());
}

// ===== MIC =====

// Mic 实时波形
void drawMicApp() {
    constexpr int waveTop = 22;
    const int waveW = M5Cardputer.Display.width();
    const int waveH = M5Cardputer.Display.height() - waveTop - 5;
    const int centerY = waveTop + waveH / 2;

    static int16_t samples[240];

    if (!M5Cardputer.Mic.isEnabled()) {
        M5Cardputer.Display.clear();
        M5Cardputer.Display.setCursor(5, 5);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.println("MIC");
        M5Cardputer.Display.println("not found");
        return;
    }

    const size_t sampleCount = waveW < 240 ? waveW : 240;
    if (!M5Cardputer.Mic.record(samples, sampleCount)) {
        return;
    }

    M5Cardputer.Display.fillRect(0, waveTop, waveW, waveH, BLACK);
    M5Cardputer.Display.drawFastHLine(0, centerY, waveW, DARKGREY);

    constexpr int micGain = 2;
    int prevY = centerY;
    for (int x = 0; x < static_cast<int>(sampleCount); x++) {
        int y = centerY - static_cast<int>(samples[x] * micGain * (waveH / 2 - 2) / 32768);
        y = constrain(y, waveTop, waveTop + waveH - 1);
        if (x > 0) {
            M5Cardputer.Display.drawLine(x - 1, prevY, x, y, GREEN);
        }
        prevY = y;
    }
}

// ===== SETTINGS =====

void drawSettingsApp() {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("SETTINGS");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.println("0-9  brightness");
    M5Cardputer.Display.println("b    show level");
    M5Cardputer.Display.println("r    invert screen");
    M5Cardputer.Display.printf("\ninvert: %s\n",
                               M5Cardputer.Display.getInvert() ? "ON" : "OFF");
    M5Cardputer.Display.printf("level:  %d\n", M5Cardputer.Display.getBrightness());
}

void handleSettingsApp(const String& key) {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("SETTINGS");

    if (key == "b") {
        M5Cardputer.Display.printf("brightness: %d\n", M5Cardputer.Display.getBrightness());
    } else if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
        const int level = key[0] - '0';
        const uint8_t brightness = level * 255 / 9;
        M5Cardputer.Display.setBrightness(brightness);
        M5Cardputer.Display.printf("level %d -> %d\n", level, brightness);
    } else if (key == "r") {
        const bool inverted = M5Cardputer.Display.getInvert();
        M5Cardputer.Display.invertDisplay(!inverted);
        M5Cardputer.Display.printf("invert: %s\n", !inverted ? "ON" : "OFF");
    }
}

// ===== BTNA =====

// BtnA 侧键测试（短按计数，长按返回菜单）
void drawBtnAApp() {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("BTNA");
    M5Cardputer.Display.printf("count: %lu\n", btnTestCount);
    M5Cardputer.Display.printf("press: %s\n", M5Cardputer.BtnA.isPressed() ? "ON" : "--");
    M5Cardputer.Display.printf("hold:  %s\n", M5Cardputer.BtnA.isHolding() ? "ON" : "--");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.println("tap=count");
    M5Cardputer.Display.println("hold=back");
}

// ===== POWER =====

// 电源信息
void drawPowerApp() {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("POWER");
    M5Cardputer.Display.printf("bat: %d%%\n", M5Cardputer.Power.getBatteryLevel());
    M5Cardputer.Display.printf("volt: %dmV\n", M5Cardputer.Power.getBatteryVoltage());
    M5Cardputer.Display.printf("curr: %dmA\n", M5Cardputer.Power.getBatteryCurrent());
    M5Cardputer.Display.printf("chg: %s\n", M5Cardputer.Power.isCharging() ? "ON" : "OFF");
    M5Cardputer.Display.printf("vbus: %dmV\n", M5Cardputer.Power.getVBUSVoltage());
}

// ===== SPEAKER =====

void drawSpeakerApp(const String& key) {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("SPEAKER");
    M5Cardputer.Display.setTextSize(1);
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

// RTC 时钟
void drawRtcApp() {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("TIME");

    if (!M5.Rtc.isEnabled()) {
        M5Cardputer.Display.println("RTC N/A");
        return;
    }

    const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
    M5Cardputer.Display.printf("%04d-%02d-%02d\n", dt.date.year, dt.date.month, dt.date.date);
    M5Cardputer.Display.printf("%02d:%02d:%02d\n", dt.time.hours, dt.time.minutes, dt.time.seconds);
}

// ===== IN I2C =====

// 绘制 I2C 扫描结果（IN I2C / EX I2C 共用）
void drawI2cScanApp(m5::I2C_Class& bus, const char* title) {
    bool found[120]{};
    if (bus.isEnabled()) {
        bus.scanID(found);
    }

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println(title);

    if (!bus.isEnabled()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.println("bus disabled");
        return;
    }

    M5Cardputer.Display.setTextSize(1);
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

// ===== WIFI =====

// WiFi 状态
void drawWifiApp() {
    static bool wifiReady = false;
    if (!wifiReady) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        wifiReady = true;
    }

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("WIFI");

    if (WiFi.status() == WL_CONNECTED) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.printf("SSID: %s\n", WiFi.SSID().c_str());
        M5Cardputer.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        M5Cardputer.Display.printf("RSSI: %d dBm\n", WiFi.RSSI());
    } else {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.println("not connected");
        M5Cardputer.Display.println("s = scan");
    }
}

void handleWifiApp(const String& key) {
    if (key != "s") {
        return;
    }

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("WIFI SCAN");

    const int count = WiFi.scanNetworks();
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.printf("found: %d\n", count);
    const int show = count < 4 ? count : 4;
    for (int i = 0; i < show; i++) {
        M5Cardputer.Display.printf("%s %d\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
}

// ===== BLE =====

// BLE 状态
void drawBleApp() {
    static bool bleReady = false;
    if (!bleReady) {
        BLEDevice::init("Cardputer");
        bleReady = true;
    }

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("BLE");
    M5Cardputer.Display.setTextSize(1);
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
    M5Cardputer.Display.setTextColor(BLACK, colors[idx]);
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("DISPLAY");
    M5Cardputer.Display.printf("color: %s\n", names[idx]);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.println("1-7 switch");
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

void handleDisplayApp(const String& key) {
    if (key.length() != 1 || key[0] < '1' || key[0] > '7') {
        return;
    }
    drawDisplayApp(key[0] - '1');
}

// ===== SLEEP =====

static bool displayAsleep = false;

// 屏幕休眠演示页
void drawSleepApp() {
    M5Cardputer.Display.wakeup();
    displayAsleep = false;

    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("SLEEP");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.println("d  screen off");
    M5Cardputer.Display.println("l  light 5s");
    M5Cardputer.Display.println("BtnA -> menu");
}

// 从屏幕休眠唤醒并刷新界面
void wakeSleepDisplay() {
    if (!displayAsleep) {
        return;
    }
    M5Cardputer.Display.wakeup();
    displayAsleep = false;
    drawSleepApp();
}

void handleSleepApp(const String& key) {
    if (key == "d") {
        M5Cardputer.Display.sleep();
        displayAsleep = true;
        return;
    }
    if (key == "l") {
        M5Cardputer.Display.clear();
        M5Cardputer.Display.setCursor(5, 5);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.println("SLEEP");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.println("light 5s...");

        esp_sleep_enable_timer_wakeup(5ULL * 1000000ULL);
        esp_light_sleep_start();
        drawSleepApp();
    }
}

// ===== MAIN =====

void enterApp(const AppState state) {
    currentState = state;
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(5, 5);

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
            drawBmiApp();
            break;
        case AppState::INFO:
            drawInfoApp();
            break;
        case AppState::MIC:
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.println("MIC");
            break;
        case AppState::BTNA:
            drawBtnAApp();
            break;
        case AppState::POWER:
            drawPowerApp();
            break;
        case AppState::SPEAKER:
            drawSpeakerApp("");
            break;
        case AppState::RTC:
            drawRtcApp();
            break;
        case AppState::IN_I2C:
            drawI2cScanApp(M5Cardputer.In_I2C, "IN I2C");
            break;
        case AppState::EX_I2C:
            drawI2cScanApp(M5Cardputer.Ex_I2C, "EX I2C");
            break;
        case AppState::WIFI:
            drawWifiApp();
            break;
        case AppState::BLE:
            drawBleApp();
            break;
        case AppState::DISP:
            drawDisplayApp(0);
            break;
        case AppState::SETTINGS:
            drawSettingsApp();
            break;
        case AppState::SLEEP:
            drawSleepApp();
            break;
        default:
            break;
    }
}

void setup() {
    const auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(30);
    showMenu();
}

void loop() {
    M5Cardputer.update();

    // BtnA：BtnA app 内短按计数，长按返回；Sleep 屏灭时先唤醒；其它 app 短按返回
    if (M5Cardputer.BtnA.wasPressed()) {
        if (currentState == AppState::SLEEP && displayAsleep) {
            wakeSleepDisplay();
        } else if (currentState == AppState::BTNA) {
            btnTestCount++;
            drawBtnAApp();
        } else if (currentState != AppState::MENU) {
            showMenu();
        }
    }
    if (currentState == AppState::BTNA && M5Cardputer.BtnA.wasHold()) {
        showMenu();
    }

    const uint32_t now = millis();

    // BMI 实时刷新，不使用 delay 以免触发 idle sleep
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
    }

    if (currentState == AppState::BTNA) {
        static uint32_t lastBtnUpdateMs = 0;
        if (now - lastBtnUpdateMs >= 80) {
            lastBtnUpdateMs = now;
            drawBtnAApp();
        }
    }

    if (currentState == AppState::POWER) {
        static uint32_t lastPowerUpdateMs = 0;
        if (now - lastPowerUpdateMs >= 500) {
            lastPowerUpdateMs = now;
            drawPowerApp();
        }
    }

    if (currentState == AppState::RTC) {
        static uint32_t lastRtcUpdateMs = 0;
        if (now - lastRtcUpdateMs >= 1000) {
            lastRtcUpdateMs = now;
            drawRtcApp();
        }
    }

    if (M5Cardputer.Keyboard.isChange()) {
        if (currentState == AppState::SLEEP && displayAsleep) {
            wakeSleepDisplay();
        }

        switch (currentState) {
            case AppState::MENU:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleMenuKey(getPressedKey());
                }
                break;
            case AppState::KEYBOARD:
                drawKeyboardApp(M5Cardputer.Keyboard.keysState());
                break;
            case AppState::SETTINGS:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleSettingsApp(getPressedKey());
                }
                break;
            case AppState::SPEAKER:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleSpeakerApp(getPressedKey());
                }
                break;
            case AppState::WIFI:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleWifiApp(getPressedKey());
                }
                break;
            case AppState::DISP:
                if (M5Cardputer.Keyboard.isPressed()) {
                    handleDisplayApp(getPressedKey());
                }
                break;
            case AppState::SLEEP:
                if (M5Cardputer.Keyboard.isPressed() && !displayAsleep) {
                    handleSleepApp(getPressedKey());
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
