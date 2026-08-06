#include "app_ac_auto.h"

#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include "app_ir.h"
#include "mijia_ble.h"
#include "mijia_control.h"

#include <cstdio>
#include <cstring>

// ===== 常量 =====
static constexpr uint32_t AC_AUTO_COUNTDOWN_S = 15;          // 进入自动态前倒计时
static constexpr uint32_t AC_AUTO_IDLE_BLANK_S = 15;         // 亮屏空闲后再息屏
static constexpr uint32_t AC_AUTO_BLE_SCAN_S = 20;           // BLE 一轮扫描窗口（秒）
static constexpr uint32_t AC_AUTO_HIST_INTERVAL_MS = 60000;  // 每分钟记一点
static constexpr int AC_AUTO_HIST_LEN = 12 * 60;             // 12 小时
static constexpr int AC_AUTO_HINT_H = 12;
static constexpr int AC_AUTO_CFG_ROWS = 8;

enum class AcAutoPage : uint8_t { Display = 0, Config = 1 };

// ===== 运行态 =====
static AcAutoPage g_page = AcAutoPage::Display;
static bool g_help_visible = false;
static bool g_display_blanked = false;
static uint8_t g_saved_brightness = 30;

static bool g_auto_active = false;
static uint32_t g_enter_ms = 0;
static uint32_t g_last_input_ms = 0;
static int g_countdown_shown = -1;

static AcAutoConfig g_cfg = {};
static bool g_cfg_dirty = false;

static bool g_has_reading = false;
static float g_temp_c = 0.f;
static float g_hum = 0.f;

static uint8_t g_on_streak = 0;
static uint8_t g_off_streak = 0;
static uint16_t g_on_times = 0;
static uint16_t g_off_times = 0;
static bool g_ac_power = false;

static int16_t g_hist_temp10[AC_AUTO_HIST_LEN];
static uint8_t g_hist_hum[AC_AUTO_HIST_LEN];
static bool g_hist_valid[AC_AUTO_HIST_LEN];
static int g_hist_head = 0;
static int g_hist_count = 0;
static uint32_t g_last_hist_ms = 0;

static MijiaDevice g_watch_dev = {};
static bool g_watch_valid = false;

static int g_cfg_row = 0;

static void drawAcAutoApp();
static void blankAcAutoDisplay();
static void wakeAcAutoDisplay(bool redraw);
static void ensureBleWatch();
static void stopBleWatch();
static void pushHistorySample();
static void applyAutomation(float temp_c);
static int listHtSensors(int* out_indices, int max_n);
static const char* sensorLabel();
static void reloadWatchDevice();

static void blankAcAutoDisplay() {
    if (g_display_blanked) {
        return;
    }
    g_saved_brightness = M5Cardputer.Display.getBrightness();
    M5Cardputer.Display.sleep();
    M5Cardputer.Display.setBrightness(0);
    g_display_blanked = true;
}

static void wakeAcAutoDisplay(const bool redraw) {
    if (!g_display_blanked) {
        return;
    }
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.setBrightness(g_saved_brightness);
    g_display_blanked = false;
    g_last_input_ms = millis();
    if (redraw) {
        drawAcAutoApp();
    }
}

bool acAutoAppSuppressesHeader() {
    return g_display_blanked || g_help_visible;
}

static int listHtSensors(int* out_indices, const int max_n) {
    const AppConfig& cfg = getAppConfig();
    int n = 0;
    for (int i = 0; i < cfg.device_count && n < max_n; i++) {
        if (mijiaBleCanScan(cfg.devices[i]) &&
            mijiaClassifyModel(cfg.devices[i].model) == MijiaDevKind::SENSOR_HT) {
            if (out_indices != nullptr) {
                out_indices[n] = i;
            }
            n++;
        }
    }
    return n;
}

static const char* sensorLabel() {
    if (g_cfg.sensor_id[0] == '\0') {
        return "(none)";
    }
    const int idx = mijiaFindDeviceIndexById(g_cfg.sensor_id);
    if (idx < 0) {
        return "(missing)";
    }
    return mijiaDeviceDisplayName(getAppConfig().devices[idx]);
}

static void reloadWatchDevice() {
    g_watch_valid = false;
    g_watch_dev = {};
    if (g_cfg.sensor_id[0] == '\0') {
        return;
    }
    const int idx = mijiaFindDeviceIndexById(g_cfg.sensor_id);
    if (idx < 0) {
        return;
    }
    const AppConfig& cfg = getAppConfig();
    if (!mijiaBleCanScan(cfg.devices[idx])) {
        return;
    }
    g_watch_dev = cfg.devices[idx];
    g_watch_valid = true;
}

static void stopBleWatch() {
    if (mijiaBleScanIsRunning()) {
        mijiaBleScanAbort();
    }
}

static void ensureBleWatch() {
    if (!g_watch_valid) {
        return;
    }
    if (mijiaBleScanIsRunning()) {
        return;
    }
    // 单设备后台监听：扫满窗口，期间可多次上报（约数秒一条广播）
    (void)mijiaBleWatchStart(&g_watch_dev, 1, AC_AUTO_BLE_SCAN_S);
}

static void pushHistorySample() {
    if (!g_has_reading) {
        return;
    }
    const int16_t t10 = static_cast<int16_t>(g_temp_c * 10.0f);
    const uint8_t h = static_cast<uint8_t>(constrain(static_cast<int>(g_hum + 0.5f), 0, 100));
    g_hist_temp10[g_hist_head] = t10;
    g_hist_hum[g_hist_head] = h;
    g_hist_valid[g_hist_head] = true;
    g_hist_head = (g_hist_head + 1) % AC_AUTO_HIST_LEN;
    if (g_hist_count < AC_AUTO_HIST_LEN) {
        g_hist_count++;
    }
}

static void applyAutomation(const float temp_c) {
    if (!g_auto_active) {
        return;
    }
    const uint8_t need = g_cfg.filter_count < 1 ? 1 : g_cfg.filter_count;

    if (temp_c > static_cast<float>(g_cfg.on_temp_c)) {
        g_on_streak++;
        g_off_streak = 0;
        if (g_on_streak >= need && !g_ac_power) {
            if (irSendAc(g_cfg.ac_brand, true, g_cfg.ac_mode, g_cfg.ac_temp_c, g_cfg.ac_fan)) {
                g_ac_power = true;
                g_on_times++;
            }
            g_on_streak = 0;
        }
    } else if (temp_c < static_cast<float>(g_cfg.off_temp_c)) {
        g_off_streak++;
        g_on_streak = 0;
        if (g_off_streak >= need && g_ac_power) {
            if (irSendAc(g_cfg.ac_brand, false, g_cfg.ac_mode, g_cfg.ac_temp_c, g_cfg.ac_fan)) {
                g_ac_power = false;
                g_off_times++;
            }
            g_off_streak = 0;
        }
    } else {
        g_on_streak = 0;
        g_off_streak = 0;
    }
}

static void flushConfigIfDirty() {
    if (!g_cfg_dirty) {
        return;
    }
    normalizeAcAutoConfig(g_cfg);
    if (saveAppConfigAcAuto(g_cfg)) {
        g_cfg = getAppConfig().ac_auto;
        g_cfg_dirty = false;
        reloadWatchDevice();
    }
}

static void markCfgDirty() {
    normalizeAcAutoConfig(g_cfg);
    g_cfg_dirty = true;
}

static void drawHelpPage() {
    beginAppScreen("AC Auto");
    int y = APP_CONTENT_INSET_Y;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("AC Auto help");
    y += 12;

    auto line = [&](const char key, const char* text) {
        int cx = APP_CONTENT_X;
        cx += drawKeyBadge(cx, y, key, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(text);
        y += 11;
    };

    line('c', "config / display");
    line('h', "help");
    line('r', "reset counts");
    y += 2;
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("15s -> auto; any key wake");
    y += 11;
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("BLE watch ~20s window");
}

static void drawHistoryChart(const int x, const int y, const int w, const int h) {
    M5Cardputer.Display.drawRect(x, y, w, h, APP_COLOR_MUTED);
    if (g_hist_count < 2) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
        M5Cardputer.Display.setCursor(x + 4, y + h / 2 - 4);
        M5Cardputer.Display.print("no history yet");
        return;
    }

    int16_t t_min = 32767;
    int16_t t_max = -32768;
    for (int i = 0; i < g_hist_count; i++) {
        const int idx = (g_hist_head - g_hist_count + i + AC_AUTO_HIST_LEN * 2) % AC_AUTO_HIST_LEN;
        if (!g_hist_valid[idx]) {
            continue;
        }
        if (g_hist_temp10[idx] < t_min) {
            t_min = g_hist_temp10[idx];
        }
        if (g_hist_temp10[idx] > t_max) {
            t_max = g_hist_temp10[idx];
        }
    }
    if (t_min > t_max) {
        return;
    }
    if (t_max - t_min < 50) {
        const int16_t mid = static_cast<int16_t>((t_min + t_max) / 2);
        t_min = static_cast<int16_t>(mid - 25);
        t_max = static_cast<int16_t>(mid + 25);
    }

    const int inner_w = w - 2;
    const int inner_h = h - 2;
    auto map_y_temp = [&](const int16_t t10) -> int {
        const float n = static_cast<float>(t10 - t_min) / static_cast<float>(t_max - t_min);
        return y + 1 + inner_h - 1 - static_cast<int>(n * (inner_h - 1));
    };
    auto map_y_hum = [&](const uint8_t hum) -> int {
        return y + 1 + inner_h - 1 - static_cast<int>((hum / 100.f) * (inner_h - 1));
    };

    int prev_tx = -1;
    int prev_ty = -1;
    int prev_hx = -1;
    int prev_hy = -1;
    for (int i = 0; i < g_hist_count; i++) {
        const int idx = (g_hist_head - g_hist_count + i + AC_AUTO_HIST_LEN * 2) % AC_AUTO_HIST_LEN;
        if (!g_hist_valid[idx]) {
            continue;
        }
        const int px = x + 1 + (i * (inner_w - 1)) / (g_hist_count - 1);
        const int ty = map_y_temp(g_hist_temp10[idx]);
        const int hy = map_y_hum(g_hist_hum[idx]);
        if (prev_tx >= 0) {
            M5Cardputer.Display.drawLine(prev_tx, prev_ty, px, ty, APP_COLOR_LABEL);
            M5Cardputer.Display.drawLine(prev_hx, prev_hy, px, hy, APP_COLOR_HINT);
        }
        prev_tx = px;
        prev_ty = ty;
        prev_hx = px;
        prev_hy = hy;
    }
}

static void drawDisplayPage() {
    beginAppScreen("AC Auto");
    const int content_bottom = M5Cardputer.Display.height() - AC_AUTO_HINT_H;
    int y = APP_CONTENT_INSET_Y;

    char line[48];
    M5Cardputer.Display.setTextSize(1);
    if (g_has_reading) {
        snprintf(line, sizeof(line), "%.1fC  %.0f%%", static_cast<double>(g_temp_c),
                 static_cast<double>(g_hum));
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    } else {
        snprintf(line, sizeof(line), "--.-C  --%%");
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    }
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(line);

    const char* ac_state = g_ac_power ? "ON" : "OFF";
    M5Cardputer.Display.setTextColor(g_ac_power ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
    const int ac_w = M5Cardputer.Display.textWidth(ac_state);
    M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - ac_w - 4, y);
    M5Cardputer.Display.print(ac_state);
    y += 11;

    snprintf(line, sizeof(line), "on:%u off:%u  f:%u/%u", static_cast<unsigned>(g_on_times),
             static_cast<unsigned>(g_off_times),
             static_cast<unsigned>(g_on_streak > g_off_streak ? g_on_streak : g_off_streak),
             static_cast<unsigned>(g_cfg.filter_count));
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(line);
    y += 11;

    snprintf(line, sizeof(line), ">%u on  <%u off", static_cast<unsigned>(g_cfg.on_temp_c),
             static_cast<unsigned>(g_cfg.off_temp_c));
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(line);
    y += 12;

    const int chart_h = content_bottom - y - 2;
    const int chart_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    if (chart_h >= 28) {
        drawHistoryChart(APP_CONTENT_X, y, chart_w, chart_h);
    }

    const int hint_y = M5Cardputer.Display.height() - AC_AUTO_HINT_H + 1;
    int cx = APP_CONTENT_X;
    if (!g_auto_active) {
        const int left = static_cast<int>(AC_AUTO_COUNTDOWN_S) -
                         static_cast<int>((millis() - g_enter_ms) / 1000);
        const int sec = left < 0 ? 0 : left;
        char cd[16];
        snprintf(cd, sizeof(cd), "auto %ds", sec);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_WARN, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print(cd);
        cx += M5Cardputer.Display.textWidth(cd) + 6;
    } else {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print("AUTO");
        cx += M5Cardputer.Display.textWidth("AUTO") + 6;
    }
    cx += drawKeyBadge(cx, hint_y, 'c', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("cfg");
    drawHelpHintRight("help", 1);
}

static void drawConfigPage() {
    beginAppScreenAccent("AC Auto", "cfg", APP_COLOR_LABEL);

    char val[40];
    const char* labels[AC_AUTO_CFG_ROWS] = {"sensor", "on temp", "off temp", "filter",
                                            "brand",  "mode",    "ac temp",  "fan"};
    auto value_of = [&](const int row) -> const char* {
        switch (row) {
            case 0:
                return sensorLabel();
            case 1:
                snprintf(val, sizeof(val), "%uC", static_cast<unsigned>(g_cfg.on_temp_c));
                return val;
            case 2:
                snprintf(val, sizeof(val), "%uC", static_cast<unsigned>(g_cfg.off_temp_c));
                return val;
            case 3:
                snprintf(val, sizeof(val), "%u", static_cast<unsigned>(g_cfg.filter_count));
                return val;
            case 4:
                return irAcBrandDisplayName(g_cfg.ac_brand);
            case 5:
                return acAutoModeDisplayName(g_cfg.ac_mode);
            case 6:
                snprintf(val, sizeof(val), "%uC", static_cast<unsigned>(g_cfg.ac_temp_c));
                return val;
            case 7:
                return acAutoFanDisplayName(g_cfg.ac_fan);
            default:
                return "?";
        }
    };

    int y = APP_CONTENT_INSET_Y;
    constexpr int row_h = 11;
    for (int i = 0; i < AC_AUTO_CFG_ROWS; i++) {
        const bool sel = (i == g_cfg_row);
        if (sel) {
            M5Cardputer.Display.fillRect(0, y - 1, M5Cardputer.Display.width(), row_h,
                                         APP_COLOR_MENU_KEY);
        }
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(sel ? BLACK : APP_COLOR_TEXT,
                                         sel ? APP_COLOR_MENU_KEY : BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print(labels[i]);

        const char* v = value_of(i);
        char shown[28];
        strncpy(shown, v, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = '\0';
        const int vw = M5Cardputer.Display.textWidth(shown);
        M5Cardputer.Display.setTextColor(sel ? BLACK : APP_COLOR_VALUE,
                                         sel ? APP_COLOR_MENU_KEY : BLACK);
        M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - vw - 4, y);
        M5Cardputer.Display.print(shown);
        y += row_h;
    }

    const int hint_y = M5Cardputer.Display.height() - AC_AUTO_HINT_H + 1;
    int cx = APP_CONTENT_X;
    cx += drawArrowUpDownBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("row ");
    cx = M5Cardputer.Display.getCursorX();
    cx += drawTextBadge(cx, hint_y, "-=", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("val");
}

static void drawAcAutoApp() {
    if (g_display_blanked) {
        return;
    }
    if (g_help_visible) {
        drawHelpPage();
        return;
    }
    if (g_page == AcAutoPage::Config) {
        drawConfigPage();
    } else {
        drawDisplayPage();
    }
}

static void applyConfigDelta(const int delta) {
    if (delta == 0) {
        return;
    }
    switch (g_cfg_row) {
        case 0: {
            int indices[MIJIA_DEVICE_MAX];
            const int n = listHtSensors(indices, MIJIA_DEVICE_MAX);
            if (n <= 0) {
                g_cfg.sensor_id[0] = '\0';
                break;
            }
            int cur = 0;
            for (int i = 0; i < n; i++) {
                if (strcmp(getAppConfig().devices[indices[i]].id, g_cfg.sensor_id) == 0) {
                    cur = i;
                    break;
                }
            }
            cur = (cur + delta + n) % n;
            strncpy(g_cfg.sensor_id, getAppConfig().devices[indices[cur]].id,
                    sizeof(g_cfg.sensor_id) - 1);
            g_cfg.sensor_id[sizeof(g_cfg.sensor_id) - 1] = '\0';
            reloadWatchDevice();
            stopBleWatch();
            ensureBleWatch();
            break;
        }
        case 1:
            g_cfg.on_temp_c = static_cast<uint8_t>(
                constrain(static_cast<int>(g_cfg.on_temp_c) + delta, 16, 40));
            break;
        case 2:
            g_cfg.off_temp_c = static_cast<uint8_t>(
                constrain(static_cast<int>(g_cfg.off_temp_c) + delta, 10, 35));
            break;
        case 3:
            g_cfg.filter_count = static_cast<uint8_t>(
                constrain(static_cast<int>(g_cfg.filter_count) + delta, 1, 10));
            break;
        case 4:
            g_cfg.ac_brand = cycleIrAcBrand(g_cfg.ac_brand, delta);
            break;
        case 5:
            g_cfg.ac_mode = cycleAcAutoMode(g_cfg.ac_mode, delta);
            break;
        case 6:
            g_cfg.ac_temp_c = static_cast<uint8_t>(
                constrain(static_cast<int>(g_cfg.ac_temp_c) + delta, 16, 30));
            break;
        case 7:
            g_cfg.ac_fan = cycleAcAutoFan(g_cfg.ac_fan, delta);
            break;
        default:
            break;
    }
    markCfgDirty();
}

static int getUpDownDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            return -1;
        }
        if (hid == 0x51 || hid == 0x37) {
            return 1;
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

static int getValueDelta(const Keyboard_Class::KeysState& status) {
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

void enterAcAutoApp() {
    g_page = AcAutoPage::Display;
    g_help_visible = false;
    g_display_blanked = false;
    g_auto_active = false;
    g_enter_ms = millis();
    g_last_input_ms = g_enter_ms;
    g_countdown_shown = -1;
    g_cfg = getAppConfig().ac_auto;
    normalizeAcAutoConfig(g_cfg);
    g_cfg_dirty = false;
    g_cfg_row = 0;

    // 若未选传感器，自动选第一台温湿度计
    if (g_cfg.sensor_id[0] == '\0') {
        int indices[1];
        if (listHtSensors(indices, 1) > 0) {
            strncpy(g_cfg.sensor_id, getAppConfig().devices[indices[0]].id,
                    sizeof(g_cfg.sensor_id) - 1);
            g_cfg.sensor_id[sizeof(g_cfg.sensor_id) - 1] = '\0';
            g_cfg_dirty = true;
        }
    }

    g_has_reading = false;
    g_on_streak = 0;
    g_off_streak = 0;
    g_on_times = 0;
    g_off_times = 0;
    g_ac_power = false;
    memset(g_hist_valid, 0, sizeof(g_hist_valid));
    g_hist_head = 0;
    g_hist_count = 0;
    g_last_hist_ms = millis();

    reloadWatchDevice();
    ensureBleWatch();
    drawAcAutoApp();
}

void leaveAcAutoApp() {
    flushConfigIfDirty();
    stopBleWatch();
    if (g_display_blanked) {
        wakeAcAutoDisplay(false);
    }
    g_auto_active = false;
    g_help_visible = false;
}

void pollAcAutoBtnA() {
    if (!M5Cardputer.BtnA.wasPressed()) {
        return;
    }
    g_last_input_ms = millis();
    if (g_display_blanked) {
        wakeAcAutoDisplay(true);
        return;
    }
    if (g_auto_active && g_page == AcAutoPage::Display && !g_help_visible) {
        blankAcAutoDisplay();
    }
}

void updateAcAutoApp() {
    const uint32_t now = millis();

    // BLE 读数
    if (g_watch_valid) {
        MijiaBleReading reading = {};
        const bool done = mijiaBleScanPoll(reading, nullptr);
        if (reading.ok && reading.has_temp) {
            g_temp_c = reading.temperature;
            if (reading.has_humidity) {
                g_hum = reading.humidity;
            }
            g_has_reading = true;
            applyAutomation(g_temp_c);
            if (!g_display_blanked && g_page == AcAutoPage::Display && !g_help_visible) {
                drawAcAutoApp();
            }
        }
        if (done) {
            ensureBleWatch(); // 一轮结束立刻再开
        } else {
            ensureBleWatch();
        }
    }

    // 每分钟记历史
    if (g_has_reading && (now - g_last_hist_ms) >= AC_AUTO_HIST_INTERVAL_MS) {
        g_last_hist_ms = now;
        pushHistorySample();
        if (!g_display_blanked && g_page == AcAutoPage::Display && !g_help_visible) {
            drawAcAutoApp();
        }
    }

    // 倒计时 → 自动工作 + 息屏
    if (!g_auto_active && g_page == AcAutoPage::Display && !g_help_visible) {
        const uint32_t elapsed_s = (now - g_enter_ms) / 1000;
        const int left = static_cast<int>(AC_AUTO_COUNTDOWN_S) - static_cast<int>(elapsed_s);
        if (left != g_countdown_shown && !g_display_blanked) {
            g_countdown_shown = left;
            drawAcAutoApp();
        }
        if (elapsed_s >= AC_AUTO_COUNTDOWN_S) {
            g_auto_active = true;
            blankAcAutoDisplay();
        }
    }

    // 自动态亮屏空闲后再息屏
    if (g_auto_active && !g_display_blanked && g_page == AcAutoPage::Display && !g_help_visible) {
        if ((now - g_last_input_ms) / 1000 >= AC_AUTO_IDLE_BLANK_S) {
            blankAcAutoDisplay();
        }
    }
}

void handleAcAutoApp(const Keyboard_Class::KeysState& status) {
    g_last_input_ms = millis();

    // 息屏：任意键亮屏
    if (g_display_blanked) {
        wakeAcAutoDisplay(true);
        return;
    }

    if (g_help_visible) {
        for (const char c : status.word) {
            if (c == 'h' || c == 'H') {
                g_help_visible = false;
                drawAcAutoApp();
                return;
            }
        }
        if (status.enter) {
            g_help_visible = false;
            drawAcAutoApp();
        }
        return;
    }

    for (const char c : status.word) {
        if (c == 'h' || c == 'H') {
            g_help_visible = true;
            drawAcAutoApp();
            return;
        }
        if (c == 'c' || c == 'C') {
            if (g_page == AcAutoPage::Config) {
                flushConfigIfDirty();
                g_page = AcAutoPage::Display;
                // 回到展示页重置倒计时，避免配置完立刻息屏
                g_enter_ms = millis();
                g_countdown_shown = -1;
                if (!g_auto_active) {
                    // 保持未自动；若已自动则继续
                }
            } else {
                g_page = AcAutoPage::Config;
            }
            drawAcAutoApp();
            return;
        }
        if ((c == 'r' || c == 'R') && g_page == AcAutoPage::Display) {
            g_on_times = 0;
            g_off_times = 0;
            g_on_streak = 0;
            g_off_streak = 0;
            drawAcAutoApp();
            return;
        }
    }

    if (g_page == AcAutoPage::Config) {
        const int ud = getUpDownDelta(status);
        if (ud != 0) {
            g_cfg_row = (g_cfg_row + ud + AC_AUTO_CFG_ROWS) % AC_AUTO_CFG_ROWS;
            drawAcAutoApp();
            return;
        }
        const int vd = getValueDelta(status);
        if (vd != 0) {
            applyConfigDelta(vd);
            drawAcAutoApp();
            return;
        }
    }
}
