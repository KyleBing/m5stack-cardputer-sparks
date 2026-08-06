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
static constexpr uint32_t AC_AUTO_COUNTDOWN_S = 5;           // 按 S 后倒计时再息屏
static constexpr uint32_t AC_AUTO_IDLE_BLANK_S = 15;         // 亮屏空闲后再息屏
// BLE：有的温湿度计约 5 分钟才广播一次；进入 APP 即按此节奏监听（与 AUTO 开/关无关）
static constexpr uint32_t AC_AUTO_BLE_SCAN_S = 360;          // 一轮最长听 6 分钟
static constexpr uint32_t AC_AUTO_BLE_NAP_S = 240;           // 收到读数后歇 4 分钟
static constexpr uint32_t AC_AUTO_BLE_BURST_S = 45;          // 首包后再听一会攒 filter
static constexpr uint32_t AC_AUTO_HIST_INTERVAL_MS = 60000;  // 每分钟记一点
static constexpr int AC_AUTO_HIST_LEN = 12 * 60;             // 12 小时
static constexpr int AC_AUTO_HINT_H = 12;
static constexpr int AC_AUTO_CFG_ROWS = 8;
static constexpr int AC_AUTO_LINE_H = 11;
static constexpr int AC_AUTO_STATS_H = AC_AUTO_LINE_H * 2 + 12; // 温湿度/计数/阈值三行

enum class AcAutoPage : uint8_t { Display = 0, Config = 1 };

// ===== 运行态 =====
static AcAutoPage g_page = AcAutoPage::Display;
static bool g_help_visible = false;
static bool g_display_blanked = false;
static uint8_t g_saved_brightness = 30;

static bool g_auto_active = false;
static bool g_countdown_active = false; // 按 S 后开始倒计时
static uint32_t g_countdown_ms = 0;
static uint32_t g_last_input_ms = 0;
static int g_countdown_shown = -1;
static uint32_t g_ble_next_ms = 0; // 下一轮扫描最早时刻
static bool g_ble_got_reading = false; // 本轮扫描是否已收到温度
static uint32_t g_ble_burst_until_ms = 0; // 首包后的短突发窗口终点
static bool g_ble_napping = false; // 「收到读数后长歇」为 true（UI 显示 NAP）
static int8_t g_ble_ui_shown = -1; // 界面上一次绘制的 BLE 状态（局部刷新用）

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
static void drawDisplayStats();
static void drawDisplayHint();
static void drawDisplayChartArea();
static void refreshDisplayStats();
static void refreshDisplayHint();
static void refreshDisplayChart();
static int displayStatsY();
static int displayChartY();
static int displayChartH();

static void blankAcAutoDisplay() {
    if (g_display_blanked) {
        return;
    }
    g_saved_brightness = M5Cardputer.Display.getBrightness();
    if (g_saved_brightness == 0) {
        g_saved_brightness = 30;
    }
    M5Cardputer.Display.sleep();
    M5Cardputer.Display.waitDisplay();
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
    // 亮屏后若在 nap，立刻再听一轮方便看最新温湿度
    if (g_ble_napping) {
        g_ble_napping = false;
        g_ble_next_ms = 0;
    }
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
    g_ble_next_ms = 0;
    g_ble_got_reading = false;
    g_ble_burst_until_ms = 0;
    g_ble_napping = false;
}

static void ensureBleWatch() {
    if (!g_watch_valid) {
        return;
    }
    if (mijiaBleScanIsRunning()) {
        return;
    }
    if (static_cast<int32_t>(millis() - g_ble_next_ms) < 0) {
        return;
    }
    // 最长连听 6 分钟，覆盖约 5 分钟一发的广播（亮/息屏同一节奏）
    g_ble_got_reading = false;
    g_ble_burst_until_ms = 0;
    g_ble_napping = false;
    (void)mijiaBleWatchStart(&g_watch_dev, 1, AC_AUTO_BLE_SCAN_S);
}

// got_reading：本轮是否收到过温度。未收到则立刻再听，避免错过 5 分钟窗
static void scheduleNextBleWatch(const bool got_reading) {
    if (got_reading) {
        g_ble_napping = true;
        g_ble_next_ms = millis() + AC_AUTO_BLE_NAP_S * 1000;
    } else {
        g_ble_napping = false;
        g_ble_next_ms = 0;
    }
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
    beginAppScreenWithBattery("AC Auto");
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
    line('s', "start auto / blank");
    line('h', "help");
    line('r', "reset counts");
    y += 2;
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("S: 5s blank; again to blank");
    y += 11;
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("BLE: listen then nap 4m");
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

static int displayStatsY() {
    return APP_CONTENT_INSET_Y;
}

static int displayChartY() {
    return displayStatsY() + AC_AUTO_STATS_H;
}

static int displayChartH() {
    const int content_bottom = M5Cardputer.Display.height() - AC_AUTO_HINT_H;
    return content_bottom - displayChartY() - 2;
}

// 0=无传感器 1=正在听 2=收到后长歇 3=空闲（不应长时间停留）
static int bleListenUiState() {
    if (!g_watch_valid) {
        return 0;
    }
    if (mijiaBleScanIsRunning()) {
        return 1;
    }
    // NAP：本轮确有读数后的长歇
    if (g_ble_napping && static_cast<int32_t>(millis() - g_ble_next_ms) < 0) {
        return 2;
    }
    return 3;
}

static const char* bleListenUiLabel(const int state) {
    switch (state) {
        case 1:
            return "LISTEN";
        case 2:
            return "NAP";
        case 3:
            return "IDLE";
        default:
            return "NO BLE";
    }
}

// 温湿度 / 计数 / 阈值（不含 chart、tip）
static void drawDisplayStats() {
    int y = displayStatsY();
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
    y += AC_AUTO_LINE_H;

    snprintf(line, sizeof(line), "on:%u off:%u  f:%u/%u", static_cast<unsigned>(g_on_times),
             static_cast<unsigned>(g_off_times),
             static_cast<unsigned>(g_on_streak > g_off_streak ? g_on_streak : g_off_streak),
             static_cast<unsigned>(g_cfg.filter_count));
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(line);
    y += AC_AUTO_LINE_H;

    snprintf(line, sizeof(line), ">%u on  <%u off", static_cast<unsigned>(g_cfg.on_temp_c),
             static_cast<unsigned>(g_cfg.off_temp_c));
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(line);

    // 阈值行右侧：BLE 监听状态
    const int ble_st = bleListenUiState();
    g_ble_ui_shown = static_cast<int8_t>(ble_st);
    const char* ble_label = bleListenUiLabel(ble_st);
    const uint16_t ble_color =
        ble_st == 1 ? APP_COLOR_OK : (ble_st == 2 ? APP_COLOR_WARN : APP_COLOR_MUTED);
    M5Cardputer.Display.setTextColor(ble_color, BLACK);
    const int ble_w = M5Cardputer.Display.textWidth(ble_label);
    M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - ble_w - 4, y);
    M5Cardputer.Display.print(ble_label);
}

static void drawDisplayChartArea() {
    const int chart_h = displayChartH();
    const int chart_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    if (chart_h >= 28) {
        drawHistoryChart(APP_CONTENT_X, displayChartY(), chart_w, chart_h);
    }
}

// 底栏 tip：倒计时 / AUTO / S 启动
static void drawDisplayHint() {
    const int hint_y = M5Cardputer.Display.height() - AC_AUTO_HINT_H + 1;
    int cx = APP_CONTENT_X;
    if (g_countdown_active && !g_auto_active) {
        const int left = static_cast<int>(AC_AUTO_COUNTDOWN_S) -
                         static_cast<int>((millis() - g_countdown_ms) / 1000);
        const int sec = left < 0 ? 0 : left;
        char cd[16];
        snprintf(cd, sizeof(cd), "auto %ds", sec);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_WARN, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print(cd);
        cx += M5Cardputer.Display.textWidth(cd) + 6;
    } else if (g_auto_active) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print("AUTO ");
        cx = M5Cardputer.Display.getCursorX();
        // 工作态亮屏：S 可再灭屏
        cx += drawKeyBadge(cx, hint_y, 's', 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print("blank ");
        cx = M5Cardputer.Display.getCursorX();
    } else {
        // 未启动：手动按 S 开始 5s 倒计时
        cx += drawKeyBadge(cx, hint_y, 's', 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print("auto ");
        cx = M5Cardputer.Display.getCursorX();
    }
    cx += drawKeyBadge(cx, hint_y, 'c', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("cfg");
    drawHelpHintRight("help", 1);
}

static void refreshDisplayStats() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    M5Cardputer.Display.fillRect(0, displayStatsY(), M5Cardputer.Display.width(), AC_AUTO_STATS_H,
                                 BLACK);
    drawDisplayStats();
}

static void refreshDisplayHint() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    const int hint_top = M5Cardputer.Display.height() - AC_AUTO_HINT_H;
    M5Cardputer.Display.fillRect(0, hint_top, M5Cardputer.Display.width(), AC_AUTO_HINT_H, BLACK);
    drawDisplayHint();
}

static void refreshDisplayChart() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    const int chart_h = displayChartH();
    if (chart_h < 28) {
        return;
    }
    M5Cardputer.Display.fillRect(APP_CONTENT_X, displayChartY(),
                                 M5Cardputer.Display.width() - APP_CONTENT_X * 2, chart_h, BLACK);
    drawDisplayChartArea();
}

static void drawDisplayPage() {
    beginAppScreenWithBattery("AC Auto");
    drawDisplayStats();
    drawDisplayChartArea();
    drawDisplayHint();
}

static void drawConfigPage() {
    beginAppScreenAccentWithBattery("AC Auto ", "cfg", APP_COLOR_LABEL);

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
    // 横向并排上下箭头，避免纵向合成超出 tip 高度
    cx += drawArrowUpDownFlatBadge(cx, hint_y, 1);
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
    g_countdown_active = false;
    g_countdown_ms = 0;
    g_last_input_ms = millis();
    g_countdown_shown = -1;
    g_ble_next_ms = 0;
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
    g_countdown_active = false;
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

    // BLE 读数：只局部刷统计区，避免整屏闪
    if (g_watch_valid) {
        MijiaBleReading reading = {};
        // 仅在本帧结束了一轮扫描时排程；已 idle 时 poll 也会返回 done，不能反复刷新 nap 终点
        const bool was_running = mijiaBleScanIsRunning();
        const bool done = mijiaBleScanPoll(reading, nullptr);
        if (reading.ok && reading.has_temp) {
            g_temp_c = reading.temperature;
            if (reading.has_humidity) {
                g_hum = reading.humidity;
            }
            g_has_reading = true;
            g_ble_got_reading = true;
            applyAutomation(g_temp_c);
            refreshDisplayStats();
            // 首包开启短突发窗口，方便频繁广播攒 filter
            if (g_ble_burst_until_ms == 0) {
                g_ble_burst_until_ms = millis() + AC_AUTO_BLE_BURST_S * 1000;
            }
        }
        // 突发窗口结束：停扫进入 nap（5 分钟一发的传感器本轮通常只有 1 包）
        if (g_ble_burst_until_ms != 0 && mijiaBleScanIsRunning() &&
            static_cast<int32_t>(millis() - g_ble_burst_until_ms) >= 0) {
            mijiaBleScanAbort();
            scheduleNextBleWatch(true);
            g_ble_burst_until_ms = 0;
        } else if (done && was_running) {
            scheduleNextBleWatch(g_ble_got_reading);
        }
        ensureBleWatch();
    }

    // BLE 监听状态变化时局部刷统计行（LISTEN / NAP / IDLE）
    if (!g_display_blanked && g_page == AcAutoPage::Display && !g_help_visible) {
        const int ble_st = bleListenUiState();
        if (ble_st != g_ble_ui_shown) {
            refreshDisplayStats();
        }
    }

    // 每分钟记历史：只刷 chart
    if (g_has_reading && (now - g_last_hist_ms) >= AC_AUTO_HIST_INTERVAL_MS) {
        g_last_hist_ms = now;
        pushHistorySample();
        refreshDisplayChart();
    }

    // 按 S 后倒计时 → 自动工作 + 息屏
    if (g_countdown_active && !g_auto_active && g_page == AcAutoPage::Display && !g_help_visible) {
        const uint32_t elapsed_s = (now - g_countdown_ms) / 1000;
        const int left = static_cast<int>(AC_AUTO_COUNTDOWN_S) - static_cast<int>(elapsed_s);
        if (left != g_countdown_shown && !g_display_blanked) {
            g_countdown_shown = left;
            refreshDisplayHint();
        }
        if (elapsed_s >= AC_AUTO_COUNTDOWN_S) {
            g_countdown_active = false;
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

    // 息屏时降低主循环空转，略省电
    if (g_display_blanked) {
        delay(50);
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
                // 离开配置取消未完成的倒计时，需再按 S
                g_countdown_active = false;
                g_countdown_shown = -1;
            } else {
                // 进配置时暂停倒计时
                g_countdown_active = false;
                g_countdown_shown = -1;
                g_page = AcAutoPage::Config;
            }
            drawAcAutoApp();
            return;
        }
        // S：未启动 → 5s 倒计时进 AUTO；已 AUTO 亮屏 → 立刻灭屏
        if ((c == 's' || c == 'S') && g_page == AcAutoPage::Display) {
            if (g_auto_active) {
                blankAcAutoDisplay();
                return;
            }
            if (!g_countdown_active) {
                g_countdown_active = true;
                g_countdown_ms = millis();
                g_countdown_shown = -1;
                refreshDisplayHint();
            }
            return;
        }
        if ((c == 'r' || c == 'R') && g_page == AcAutoPage::Display) {
            g_on_times = 0;
            g_off_times = 0;
            g_on_streak = 0;
            g_off_streak = 0;
            refreshDisplayStats();
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
