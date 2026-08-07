#include "app_ac_auto.h"

#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_device_icons.h"
#include "app_header.h"
#include "app_icons.h"
#include "app_ir.h"
#include "mijia_ble.h"
#include "mijia_control.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ===== 常量 =====
// BLE：有的温湿度计约 5 分钟才广播一次；进入 APP 即按此节奏监听（与 AUTO 开/关无关）
static constexpr uint32_t AC_AUTO_BLE_SCAN_S = 360;          // 一轮最长听 6 分钟
static constexpr uint32_t AC_AUTO_BLE_NAP_S = 240;           // 收到读数后歇 4 分钟
static constexpr uint32_t AC_AUTO_BLE_BURST_S = 45;          // 首包后再听一会攒 filter
static constexpr uint32_t AC_AUTO_HIST_INTERVAL_MS = 60000;  // 每分钟记一点
static constexpr uint32_t AC_AUTO_TOP_REFRESH_MS = 5000;     // 顶栏电量定期刷新
static constexpr uint32_t AC_AUTO_STATS_DEBOUNCE_MS = 180;   // 合并温湿度分发包，避免连闪
static constexpr int AC_AUTO_HIST_LEN = 12 * 60;             // 12 小时
static constexpr int AC_AUTO_HINT_H = 12;                    // 仅 Config / Help 底栏
static constexpr int AC_AUTO_TOP_H = 18;                     // 运行/空调状态 2x ≈16px
static constexpr int AC_AUTO_CFG_ROWS = 8;
static constexpr int AC_AUTO_LINE_H = 17;                    // 统计行高（数值 2x ≈16px）
static constexpr int AC_AUTO_STATS_H = AC_AUTO_LINE_H * 2;   // 左温湿度 / 右 on·off；最右 AC 电源
static constexpr int AC_AUTO_CHART_Y_LABEL_W = 22;           // 曲线左侧 on/off 两行标注
static constexpr int AC_AUTO_CHART_X_LABEL_H = 10;           // 曲线底部时间轴
static constexpr int AC_AUTO_Y_LABEL_GAP = 2;                // 纵坐标 on/off 与数值间距
static constexpr int AC_AUTO_HELP_PAGES = 3;                 // help 分页：键位 / 参数 / 机制
static constexpr int AC_AUTO_GAP_TOP_STATS = 5;              // 顶栏与温湿度区间距
static constexpr int AC_AUTO_GAP_STATS_CHART = 5;            // 温湿度与图表间距
static constexpr int AC_AUTO_POWER_ICON_PX = 30;             // IR ac_power 原尺寸边长
static constexpr int AC_AUTO_POWER_ICON_PIXELS = AC_AUTO_POWER_ICON_PX * AC_AUTO_POWER_ICON_PX;
static constexpr int AC_AUTO_POWER_ICON_BYTES = AC_AUTO_POWER_ICON_PIXELS * 2; // RGB565
static constexpr int AC_AUTO_POWER_ICON_SLOTS = 2;           // off / on
static constexpr const char* AC_AUTO_POWER_ICON_DIR = "/icon/ir";
// 温度 CYAN；湿度 GREEN（与数值/曲线同色）
static constexpr uint16_t AC_AUTO_COLOR_TEMP = CYAN;
static constexpr uint16_t AC_AUTO_COLOR_HUM = GREEN;

enum class AcAutoPage : uint8_t { Display = 0, Config = 1 };

// ===== 运行态 =====
static AcAutoPage g_page = AcAutoPage::Display;
static bool g_help_visible = false;
static int g_help_page = 0; // 0..AC_AUTO_HELP_PAGES-1
static bool g_display_blanked = false;
static bool g_boot_loading = false; // 进入时 BLE 初始化中：全屏 Loading、禁 header
static uint8_t g_saved_brightness = 30;

static bool g_auto_active = false;
static uint32_t g_last_input_ms = 0;
static uint32_t g_ble_next_ms = 0; // 下一轮扫描最早时刻
static bool g_ble_got_reading = false; // 本轮扫描是否已收到温度
static uint32_t g_ble_burst_until_ms = 0; // 首包后的短突发窗口终点
static bool g_ble_napping = false; // 「收到读数后长歇」为 true（UI 显示 NAP）
static int8_t g_ble_ui_shown = -1; // 界面上一次绘制的 BLE 状态（局部刷新用）
static uint32_t g_last_top_ms = 0;  // 顶栏电量上次刷新

static AcAutoConfig g_cfg = {};
static bool g_cfg_dirty = false;

static bool g_has_reading = false;   // 是否收到过温度（自动化/历史以温度为准）
static bool g_has_humidity = false;  // 湿度常与温度分开发，单独记
static float g_temp_c = 0.f;
static float g_hum = 0.f;

static uint8_t g_on_streak = 0;
static uint8_t g_off_streak = 0;
static uint16_t g_on_times = 0;
static uint16_t g_off_times = 0;
static bool g_ac_power = false;

// 已绘统计快照：突发窗口重复 adv / 温湿度分发包时未变则不刷
static bool g_ui_has_reading = false;
static bool g_ui_has_humidity = false;
static float g_ui_temp_c = 0.f;
static float g_ui_hum = 0.f;
static uint16_t g_ui_on_times = 0;
static uint16_t g_ui_off_times = 0;
static bool g_ui_ac_power = false;
static bool g_ui_auto_active = false;
static bool g_stats_defer = false;           // 统计区待刷（防抖中）
static uint32_t g_stats_defer_until_ms = 0;  // 防抖到期时刻
static bool g_top_defer = false;             // 顶栏待刷（与统计合并同帧）

// 开关机图标 RAM 缓存（与 IR 同：进时预载，离时释放，避免每次从 Flash 解图闪烁）
static uint16_t* s_power_icon_px = nullptr;
static bool s_power_icon_ready[AC_AUTO_POWER_ICON_SLOTS] = {};

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
static void drawDisplayTopBar();
static void drawDisplayStats();
static void drawDisplayStatsData(bool force_power); // 仅数值/图标；force_power=全页首绘
static void drawDisplayChartArea();
static void captureStatsUi();
static bool statsUiChanged();
static void scheduleStatsRefresh();
static void scheduleTopRefresh();
static void flushDeferredDisplayRefresh();
static void refreshDisplayTopBar();
static void refreshDisplayStatsData();
static void refreshDisplayTopAndStats();
static void refreshDisplayChart();
static int displayTopY();
static int displayStatsY();
static int displayChartY();
static int displayChartH();
static bool ensurePowerIconCache();
static void freePowerIconCache();
static void preloadPowerIcons();
static bool drawPowerIconAt(int x, int y, bool on);

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
    // 本 App 全程自绘顶栏，不使用系统 header
    return true;
}

// 无 header 的全屏提示（对齐 IR / Mijia leave）
static void drawAcAutoBusyTip(const char* msg) {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.drawCenterString(msg, M5Cardputer.Display.width() / 2,
                                         M5Cardputer.Display.height() / 2 - 8);
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

// 温度曲线：有读数即记，与 AUTO 开/关无关
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

static void drawHelpKeyLine(int& y, const char key, const char* text) {
    int cx = APP_CONTENT_X;
    cx += drawKeyBadge(cx, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复 tip 色
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    y += 11;
}

// Help 分段着色：打印一段后推进 cx
static void helpPrint(int& cx, const int y, const char* text, const uint16_t color) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    cx = M5Cardputer.Display.getCursorX();
}

static void drawHelpRichLine(int& y, const char* const* parts, const uint16_t* colors, const int n) {
    int cx = APP_CONTENT_X;
    for (int i = 0; i < n; i++) {
        helpPrint(cx, y, parts[i], colors[i]);
    }
    y += 11;
}

static void drawHelpPage() {
    M5Cardputer.Display.fillScreen(BLACK);
    int y = 2;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    char title[24];
    snprintf(title, sizeof(title), "AC Auto help %d/%d", g_help_page + 1, AC_AUTO_HELP_PAGES);
    M5Cardputer.Display.print(title);
    y += 12;

    const uint16_t hum_c = AC_AUTO_COLOR_HUM;

    if (g_help_page == 0) {
        // 键位：动作关键字着色
        {
            const char* p[] = {"start / stop ", "AUTO"};
            const uint16_t c[] = {APP_COLOR_HINT, APP_COLOR_OK};
            int cx = APP_CONTENT_X;
            cx += drawKeyBadge(cx, y, 't', 1);
            M5Cardputer.Display.setTextSize(1);
            for (int i = 0; i < 2; i++) {
                helpPrint(cx, y, p[i], c[i]);
            }
            y += 11;
        }
        drawHelpKeyLine(y, 's', "blank screen");
        {
            const char* p[] = {"config", " / ", "display"};
            const uint16_t c[] = {APP_COLOR_LABEL, APP_COLOR_HINT, APP_COLOR_VALUE};
            int cx = APP_CONTENT_X;
            cx += drawKeyBadge(cx, y, 'c', 1);
            M5Cardputer.Display.setTextSize(1);
            for (int i = 0; i < 3; i++) {
                helpPrint(cx, y, p[i], c[i]);
            }
            y += 11;
        }
        {
            const char* p[] = {"reset ", "on", "/", "off", " counts"};
            const uint16_t c[] = {APP_COLOR_HINT, APP_COLOR_WARN, APP_COLOR_HINT, APP_COLOR_OK,
                                  APP_COLOR_HINT};
            int cx = APP_CONTENT_X;
            cx += drawKeyBadge(cx, y, 'r', 1);
            M5Cardputer.Display.setTextSize(1);
            for (int i = 0; i < 5; i++) {
                helpPrint(cx, y, p[i], c[i]);
            }
            y += 11;
        }
        {
            // 仅对齐内部假定开关，不发红外
            const char* p[] = {"assume ", "AC", " on", "/", "off", " (no IR)"};
            const uint16_t c[] = {APP_COLOR_HINT, APP_COLOR_LABEL, APP_COLOR_OK, APP_COLOR_HINT,
                                  APP_COLOR_HINT, APP_COLOR_HINT};
            int cx = APP_CONTENT_X;
            cx += drawKeyBadge(cx, y, 'p', 1);
            M5Cardputer.Display.setTextSize(1);
            for (int i = 0; i < 6; i++) {
                helpPrint(cx, y, p[i], c[i]);
            }
            y += 11;
        }
        drawHelpKeyLine(y, 'h', "help / close");
        {
            const char* p[] = {"BtnA", ": blank / wake"};
            const uint16_t c[] = {APP_COLOR_LABEL, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 2);
        }
        {
            const char* p[] = {",.[]", "  help page"};
            const uint16_t c[] = {APP_COLOR_MENU_KEY, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 2);
        }
    } else if (g_help_page == 1) {
        // 配置参数含义
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("params (cfg)");
        y += 11;
        {
            const char* p[] = {"sensor", ": ", "BLE", " HT meter"};
            const uint16_t c[] = {APP_COLOR_LABEL, APP_COLOR_HINT, APP_COLOR_LABEL, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 4);
        }
        {
            const char* p[] = {"on temp", ": ", ">N", " => ", "AC ON"};
            const uint16_t c[] = {APP_COLOR_WARN, APP_COLOR_HINT, APP_COLOR_VALUE, APP_COLOR_HINT,
                                  APP_COLOR_OK};
            drawHelpRichLine(y, p, c, 5);
        }
        {
            const char* p[] = {"off temp", ": ", "<N", " => ", "AC OFF"};
            const uint16_t c[] = {APP_COLOR_OK, APP_COLOR_HINT, APP_COLOR_VALUE, APP_COLOR_HINT,
                                  APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 5);
        }
        {
            const char* p[] = {"filter", ": streak readings"};
            const uint16_t c[] = {APP_COLOR_LABEL, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 2);
        }
        {
            const char* p[] = {"brand", "/", "mode", "/", "temp", "/", "fan", ": ", "IR"};
            const uint16_t c[] = {APP_COLOR_LABEL, APP_COLOR_HINT, APP_COLOR_LABEL, APP_COLOR_HINT,
                                  AC_AUTO_COLOR_TEMP, APP_COLOR_HINT, APP_COLOR_LABEL,
                                  APP_COLOR_HINT, APP_COLOR_WARN};
            drawHelpRichLine(y, p, c, 9);
        }
        {
            const char* p[] = {";.", " row  ", "-=", " value"};
            const uint16_t c[] = {APP_COLOR_MENU_KEY, APP_COLOR_HINT, APP_COLOR_MENU_KEY,
                                  APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 4);
        }
    } else {
        // 运行机制 + 主界面元素
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("how it runs");
        y += 11;
        {
            const char* p[] = {"T", ": ", "AUTO", " ok while lit"};
            const uint16_t c[] = {APP_COLOR_MENU_KEY, APP_COLOR_HINT, APP_COLOR_OK, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 4);
        }
        {
            const char* p[] = {"S", "/", "BtnA", " blank, key wake"};
            const uint16_t c[] = {APP_COLOR_MENU_KEY, APP_COLOR_HINT, APP_COLOR_LABEL,
                                  APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 4);
        }
        {
            const char* p[] = {">", "on", " ", "filter", " => ", "IR", " ON"};
            const uint16_t c[] = {APP_COLOR_HINT, APP_COLOR_WARN, APP_COLOR_HINT, APP_COLOR_LABEL,
                                  APP_COLOR_HINT, APP_COLOR_WARN, APP_COLOR_OK};
            drawHelpRichLine(y, p, c, 7);
        }
        {
            // 关机前提：内部假定状态为 ON（可用 p 对齐）
            const char* p[] = {"<", "off", " ", "filter", " + ", "ON", " => ", "IR", " OFF"};
            const uint16_t c[] = {APP_COLOR_HINT, APP_COLOR_OK, APP_COLOR_HINT, APP_COLOR_LABEL,
                                  APP_COLOR_HINT, APP_COLOR_OK, APP_COLOR_HINT, APP_COLOR_WARN,
                                  APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 9);
        }
        {
            const char* p[] = {"BLE", " ", "listen", "<=6m ", "nap", " 4m"};
            const uint16_t c[] = {APP_COLOR_LABEL, APP_COLOR_HINT, APP_COLOR_OK, APP_COLOR_HINT,
                                  APP_COLOR_WARN, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 6);
        }
        {
            const char* p[] = {"chart ", "T", " always; ", "on", "/", "off", " lines"};
            const uint16_t c[] = {APP_COLOR_HINT, AC_AUTO_COLOR_TEMP, APP_COLOR_HINT, APP_COLOR_WARN,
                                  APP_COLOR_HINT, APP_COLOR_OK, APP_COLOR_HINT};
            drawHelpRichLine(y, p, c, 7);
        }
        {
            const char* p[] = {"L:", "Temp", " ", "Hum", "  R:", "on", " ", "off"};
            const uint16_t c[] = {APP_COLOR_HINT, AC_AUTO_COLOR_TEMP, APP_COLOR_HINT, hum_c,
                                  APP_COLOR_HINT, APP_COLOR_WARN, APP_COLOR_HINT, APP_COLOR_OK};
            drawHelpRichLine(y, p, c, 8);
        }
    }

    // 底栏：翻页（横向并排上下箭头）+ 关闭
    const int hint_y = M5Cardputer.Display.height() - AC_AUTO_HINT_H + 1;
    int cx = APP_CONTENT_X;
    cx += drawArrowUpDownFlatBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复 tip 色
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("page");
    drawHelpHintRight("close", 1);
}

// 虚线：水平触发线
static void drawDashedHLine(const int x0, const int x1, const int y, const uint16_t color) {
    for (int x = x0; x < x1; x += 4) {
        const int x_end = x + 2 < x1 ? x + 2 : x1;
        if (x_end > x) {
            M5Cardputer.Display.drawFastHLine(x, y, x_end - x, color);
        }
    }
}

static void drawHistoryChart(const int x, const int y, const int w, const int h) {
    // 绘图区留出左/下坐标标注
    const int plot_x = x + AC_AUTO_CHART_Y_LABEL_W;
    const int plot_y = y;
    const int plot_w = w - AC_AUTO_CHART_Y_LABEL_W;
    const int plot_h = h - AC_AUTO_CHART_X_LABEL_H;
    if (plot_w < 24 || plot_h < 20) {
        return;
    }

    M5Cardputer.Display.drawRect(plot_x, plot_y, plot_w, plot_h, APP_COLOR_MUTED);

    const int16_t on10 = static_cast<int16_t>(g_cfg.on_temp_c) * 10;
    const int16_t off10 = static_cast<int16_t>(g_cfg.off_temp_c) * 10;

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
    // 无数据时仍用触发阈值撑开纵轴，方便看到 on/off 线
    if (t_min > t_max) {
        t_min = off10;
        t_max = on10;
    }
    if (off10 < t_min) {
        t_min = off10;
    }
    if (on10 > t_max) {
        t_max = on10;
    }
    if (t_max - t_min < 50) {
        const int16_t mid = static_cast<int16_t>((t_min + t_max) / 2);
        t_min = static_cast<int16_t>(mid - 25);
        t_max = static_cast<int16_t>(mid + 25);
    }
    // 上下各留一点边距，避免触发线贴边
    t_min = static_cast<int16_t>(t_min - 10);
    t_max = static_cast<int16_t>(t_max + 10);

    const int inner_w = plot_w - 2;
    const int inner_h = plot_h - 2;
    auto map_y_temp = [&](const int16_t t10) -> int {
        const float n = static_cast<float>(t10 - t_min) / static_cast<float>(t_max - t_min);
        return plot_y + 1 + inner_h - 1 - static_cast<int>(n * (inner_h - 1));
    };

    // 纵坐标：on / off 触发线（浅色虚线，避免盖住温度曲线）
    const int on_y = map_y_temp(on10);
    const int off_y = map_y_temp(off10);
    const uint16_t on_line = M5Cardputer.Display.color565(0xB0, 0x70, 0x20);
    const uint16_t off_line = M5Cardputer.Display.color565(0x20, 0x90, 0x30);
    drawDashedHLine(plot_x + 1, plot_x + plot_w - 1, on_y, on_line);
    drawDashedHLine(plot_x + 1, plot_x + plot_w - 1, off_y, off_line);

    M5Cardputer.Display.setTextSize(1);
    char ylab[8];
    constexpr int y_label_block_h = 8 + AC_AUTO_Y_LABEL_GAP + 8; // 标签 + 间距 + 数值
    // 两行标注；线距过近时 on 偏上、off 偏下，避免叠字
    int on_ty = on_y - 8;
    int off_ty = off_y - 8;
    const int y_gap = on_y > off_y ? on_y - off_y : off_y - on_y;
    if (y_gap < y_label_block_h + 4) {
        if (on_y <= off_y) {
            on_ty = on_y - 10;
            off_ty = off_y - 2;
        } else {
            off_ty = off_y - 10;
            on_ty = on_y - 2;
        }
    }
    if (on_ty < y) {
        on_ty = y;
    }
    if (off_ty < y) {
        off_ty = y;
    }
    if (on_ty + y_label_block_h > plot_y + plot_h) {
        on_ty = plot_y + plot_h - y_label_block_h;
    }
    if (off_ty + y_label_block_h > plot_y + plot_h) {
        off_ty = plot_y + plot_h - y_label_block_h;
    }

    M5Cardputer.Display.setTextColor(APP_COLOR_WARN, BLACK);
    M5Cardputer.Display.setCursor(x, on_ty);
    M5Cardputer.Display.print("on");
    snprintf(ylab, sizeof(ylab), "%u", static_cast<unsigned>(g_cfg.on_temp_c));
    M5Cardputer.Display.setCursor(x, on_ty + 8 + AC_AUTO_Y_LABEL_GAP);
    M5Cardputer.Display.print(ylab);

    M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
    M5Cardputer.Display.setCursor(x, off_ty);
    M5Cardputer.Display.print("off");
    snprintf(ylab, sizeof(ylab), "%u", static_cast<unsigned>(g_cfg.off_temp_c));
    M5Cardputer.Display.setCursor(x, off_ty + 8 + AC_AUTO_Y_LABEL_GAP);
    M5Cardputer.Display.print(ylab);

    if (g_hist_count < 2) {
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
        M5Cardputer.Display.setCursor(plot_x + 4, plot_y + plot_h / 2 - 4);
        M5Cardputer.Display.print("no history yet");
        return;
    }

    // 仅温度曲线
    int prev_tx = -1;
    int prev_ty = -1;
    for (int i = 0; i < g_hist_count; i++) {
        const int idx = (g_hist_head - g_hist_count + i + AC_AUTO_HIST_LEN * 2) % AC_AUTO_HIST_LEN;
        if (!g_hist_valid[idx]) {
            continue;
        }
        const int px = plot_x + 1 + (i * (inner_w - 1)) / (g_hist_count - 1);
        const int ty = map_y_temp(g_hist_temp10[idx]);
        if (prev_tx >= 0) {
            M5Cardputer.Display.drawLine(prev_tx, prev_ty, px, ty, AC_AUTO_COLOR_TEMP);
        }
        prev_tx = px;
        prev_ty = ty;
    }

    // 横坐标：短跨度用分钟，长跨度用小时（每点 1 分钟）
    const int span_min = g_hist_count;
    const bool use_hour = span_min > 90;
    const int label_y = plot_y + plot_h + 1;
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    char xlab[10];
    if (use_hour) {
        snprintf(xlab, sizeof(xlab), "0h");
        M5Cardputer.Display.setCursor(plot_x, label_y);
        M5Cardputer.Display.print(xlab);
        const int hours = (span_min + 30) / 60;
        snprintf(xlab, sizeof(xlab), "%dh", hours);
        const int tw = M5Cardputer.Display.textWidth(xlab);
        M5Cardputer.Display.setCursor(plot_x + plot_w - tw - 1, label_y);
        M5Cardputer.Display.print(xlab);
        if (hours >= 2) {
            snprintf(xlab, sizeof(xlab), "%dh", hours / 2);
            const int mid_w = M5Cardputer.Display.textWidth(xlab);
            M5Cardputer.Display.setCursor(plot_x + (plot_w - mid_w) / 2, label_y);
            M5Cardputer.Display.print(xlab);
        }
    } else {
        snprintf(xlab, sizeof(xlab), "0m");
        M5Cardputer.Display.setCursor(plot_x, label_y);
        M5Cardputer.Display.print(xlab);
        snprintf(xlab, sizeof(xlab), "%dm", span_min);
        const int tw = M5Cardputer.Display.textWidth(xlab);
        M5Cardputer.Display.setCursor(plot_x + plot_w - tw - 1, label_y);
        M5Cardputer.Display.print(xlab);
        if (span_min >= 10) {
            snprintf(xlab, sizeof(xlab), "%dm", span_min / 2);
            const int mid_w = M5Cardputer.Display.textWidth(xlab);
            M5Cardputer.Display.setCursor(plot_x + (plot_w - mid_w) / 2, label_y);
            M5Cardputer.Display.print(xlab);
        }
    }
}

// 开关机图标槽像素起点：0=off，1=on
static uint16_t* powerIconPx(const int slot) {
    return s_power_icon_px + static_cast<size_t>(slot) * AC_AUTO_POWER_ICON_PIXELS;
}

// 进入时分配两槽缓存；失败则绘制走 PNG 回退
static bool ensurePowerIconCache() {
    if (s_power_icon_px == nullptr) {
        s_power_icon_px = static_cast<uint16_t*>(
            malloc(static_cast<size_t>(AC_AUTO_POWER_ICON_SLOTS) * AC_AUTO_POWER_ICON_BYTES));
        memset(s_power_icon_ready, 0, sizeof(s_power_icon_ready));
    }
    return s_power_icon_px != nullptr;
}

static void freePowerIconCache() {
    free(s_power_icon_px);
    s_power_icon_px = nullptr;
    memset(s_power_icon_ready, 0, sizeof(s_power_icon_ready));
}

// 优先 .rgb565，缺失回退现场解 PNG
static bool loadPowerIconToSlot(const int slot, const bool on) {
    if (s_power_icon_px == nullptr || slot < 0 || slot >= AC_AUTO_POWER_ICON_SLOTS) {
        return false;
    }
    char path[48];
    if (on) {
        snprintf(path, sizeof(path), "%s/ac_power_active.rgb565", AC_AUTO_POWER_ICON_DIR);
    } else {
        snprintf(path, sizeof(path), "%s/ac_power.rgb565", AC_AUTO_POWER_ICON_DIR);
    }
    if (!loadRgb565File(path, powerIconPx(slot), AC_AUTO_POWER_ICON_PX, AC_AUTO_POWER_ICON_PX)) {
        if (on) {
            snprintf(path, sizeof(path), "%s/ac_power_active.png", AC_AUTO_POWER_ICON_DIR);
        } else {
            snprintf(path, sizeof(path), "%s/ac_power.png", AC_AUTO_POWER_ICON_DIR);
        }
        if (!decodePngToRgb565(path, powerIconPx(slot), AC_AUTO_POWER_ICON_PX,
                               AC_AUTO_POWER_ICON_PX)) {
            return false;
        }
    }
    s_power_icon_ready[slot] = true;
    return true;
}

static void preloadPowerIcons() {
    if (!ensurePowerIconCache()) {
        return;
    }
    if (!s_power_icon_ready[0]) {
        loadPowerIconToSlot(0, false);
    }
    if (!s_power_icon_ready[1]) {
        loadPowerIconToSlot(1, true);
    }
}

// 优先 RAM 缓存 pushImage，失败再读 Flash
static bool drawPowerIconAt(const int x, const int y, const bool on) {
    const int slot = on ? 1 : 0;
    if (s_power_icon_px != nullptr && s_power_icon_ready[slot]) {
        M5Cardputer.Display.pushImage(x, y, AC_AUTO_POWER_ICON_PX, AC_AUTO_POWER_ICON_PX,
                                      powerIconPx(slot));
        return true;
    }
    if (s_power_icon_px != nullptr && loadPowerIconToSlot(slot, on)) {
        M5Cardputer.Display.pushImage(x, y, AC_AUTO_POWER_ICON_PX, AC_AUTO_POWER_ICON_PX,
                                      powerIconPx(slot));
        return true;
    }
    char png_path[40];
    if (on) {
        snprintf(png_path, sizeof(png_path), "%s/ac_power_active.png", AC_AUTO_POWER_ICON_DIR);
    } else {
        snprintf(png_path, sizeof(png_path), "%s/ac_power.png", AC_AUTO_POWER_ICON_DIR);
    }
    return drawLittleFsPng(png_path, x, y, 1.0f);
}

static int displayTopY() {
    return 2;
}

static int displayStatsY() {
    return displayTopY() + AC_AUTO_TOP_H + AC_AUTO_GAP_TOP_STATS;
}

static int displayChartY() {
    return displayStatsY() + AC_AUTO_STATS_H + AC_AUTO_GAP_STATS_CHART;
}

static int displayChartH() {
    // Display 页无 tip；顶栏/统计留出间距后图表自然压矮
    return M5Cardputer.Display.height() - displayChartY() - 1;
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
            return "listening";
        case 2:
            return "nap";
        case 3:
            return "idle";
        default:
            return "no ble";
    }
}

// 温湿度数值列左缘（与顶栏状态名共用对齐）
static int displayHtValueX() {
    M5Cardputer.Display.setTextSize(1);
    return APP_CONTENT_X + M5Cardputer.Display.textWidth("Temp") + 4;
}

// 自绘顶栏：左运行图标 + 状态名（与温湿度数值左对齐），右 BLE + 电池（1x）
static void drawDisplayTopBar() {
    const int y = displayTopY();
    const int screen_w = M5Cardputer.Display.width();
    const int icon_cy = y + AC_AUTO_TOP_H / 2;
    const int text_y = y + (AC_AUTO_TOP_H - 16) / 2; // size2 字高 ≈16，垂直居中
    const int status_x = displayHtValueX(); // 与 Temp/Hum 数值左缘对齐

    // 运行图标：在状态文字左侧空隙水平居中（三角=运行，方块=停止）
    const int icon_slot_w = status_x - APP_CONTENT_X;
    if (g_auto_active) {
        const int icon_x = APP_CONTENT_X + (icon_slot_w - ICON_PLAY_W) / 2;
        drawIconPlay(icon_x, icon_cy, APP_COLOR_OK);
    } else {
        const int icon_x = APP_CONTENT_X + (icon_slot_w - ICON_STOP_W) / 2;
        drawIconStop(icon_x, icon_cy, APP_COLOR_HINT);
    }

    M5Cardputer.Display.setTextSize(2);
    // AUTO 开启时始终绿色，避免「AUTO OFF」被误读成未开启
    if (g_auto_active) {
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(status_x, text_y);
        M5Cardputer.Display.print("AUTO");
    } else {
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(status_x, text_y);
        M5Cardputer.Display.print("STOP");
    }

    const bool charging = isBatteryCharging();
    const int bat_w = getIconBatteryDisplayWidth(charging);
    const int bat_h = getIconBatteryBodyHeight();
    const int bat_x = screen_w - bat_w - 4;
    const int bat_y = y + (AC_AUTO_TOP_H - bat_h) / 2;
    drawIconBattery(bat_x, bat_y < 0 ? 0 : bat_y, M5Cardputer.Power.getBatteryLevel(), charging);

    // 右侧监听状态保持 1x
    const int ble_st = bleListenUiState();
    g_ble_ui_shown = static_cast<int8_t>(ble_st);
    const char* ble_label = bleListenUiLabel(ble_st);
    const uint16_t ble_color =
        ble_st == 1 ? APP_COLOR_OK : (ble_st == 2 ? APP_COLOR_WARN : APP_COLOR_MUTED);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(ble_color, BLACK);
    const int ble_w = M5Cardputer.Display.textWidth(ble_label);
    const int ble_y = y + (AC_AUTO_TOP_H - 8) / 2;
    M5Cardputer.Display.setCursor(bat_x - ble_w - 4, ble_y);
    M5Cardputer.Display.print(ble_label);
}

// 打印 2x 数值（上色）+ 单位（白）；无读数时整段 muted
static void printHtValueUnit(const char* value, const char* unit, const bool has_value,
                             const uint16_t value_color) {
    M5Cardputer.Display.setTextSize(2);
    if (has_value) {
        M5Cardputer.Display.setTextColor(value_color, BLACK);
        M5Cardputer.Display.print(value);
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK); // 单位白
        M5Cardputer.Display.print(unit);
    } else {
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
        M5Cardputer.Display.print(value);
        M5Cardputer.Display.print(unit);
    }
}

static void captureStatsUi() {
    g_ui_has_reading = g_has_reading;
    g_ui_has_humidity = g_has_humidity;
    g_ui_temp_c = g_temp_c;
    g_ui_hum = g_hum;
    g_ui_on_times = g_on_times;
    g_ui_off_times = g_off_times;
    g_ui_ac_power = g_ac_power;
    g_ui_auto_active = g_auto_active;
}

static bool statsUiChanged() {
    if (g_ui_has_reading != g_has_reading || g_ui_has_humidity != g_has_humidity) {
        return true;
    }
    if (g_has_reading && g_ui_temp_c != g_temp_c) {
        return true;
    }
    if (g_has_humidity && g_ui_hum != g_hum) {
        return true;
    }
    return g_ui_on_times != g_on_times || g_ui_off_times != g_off_times ||
           g_ui_ac_power != g_ac_power || g_ui_auto_active != g_auto_active;
}

// 左列温湿度 + 中列 on/off 次数；最右 AC 开关机图标（IR 原尺寸）
static void drawDisplayStats() {
    const int y0 = displayStatsY();
    const int screen_w = M5Cardputer.Display.width();
    const int mid_x = screen_w / 2;
    // size1 标签相对 size2 数值垂直居中
    const int label_dy = (16 - 8) / 2;

    // 左：Temp / Hum — 标签 1x；数值列与顶栏状态名对齐
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y0 + label_dy);
    M5Cardputer.Display.print("Temp");
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y0 + AC_AUTO_LINE_H + label_dy);
    M5Cardputer.Display.print("Hum");

    // 中：on / off 标签
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(mid_x, y0 + label_dy);
    M5Cardputer.Display.print("on");
    M5Cardputer.Display.setCursor(mid_x, y0 + AC_AUTO_LINE_H + label_dy);
    M5Cardputer.Display.print("off");

    drawDisplayStatsData(true);
}

// 只重绘可变数值区（先清固定宽度槽，避免整带 fillRect 闪）
static void drawDisplayStatsData(const bool force_power) {
    const int y0 = displayStatsY();
    const int screen_w = M5Cardputer.Display.width();
    const int mid_x = screen_w / 2;
    const int ht_val_x = displayHtValueX();
    const bool redraw_power = force_power || (g_ui_ac_power != g_ac_power);
    char num[12];

    M5Cardputer.Display.setTextSize(1);
    const int cnt_label_w = M5Cardputer.Display.textWidth("off");
    const int cnt_val_x = mid_x + cnt_label_w + 4;

    M5Cardputer.Display.setTextSize(2);
    // 温度槽：最长 "99.9C"
    const int temp_slot_w = M5Cardputer.Display.textWidth("99.9C");
    M5Cardputer.Display.fillRect(ht_val_x, y0, temp_slot_w, 16, BLACK);
    M5Cardputer.Display.setCursor(ht_val_x, y0);
    if (g_has_reading) {
        snprintf(num, sizeof(num), "%.1f", static_cast<double>(g_temp_c));
        printHtValueUnit(num, "C", true, AC_AUTO_COLOR_TEMP);
    } else {
        printHtValueUnit("--.-", "C", false, AC_AUTO_COLOR_TEMP);
    }

    // 湿度槽：最长 "100%"
    const int hum_slot_w = M5Cardputer.Display.textWidth("100%");
    M5Cardputer.Display.fillRect(ht_val_x, y0 + AC_AUTO_LINE_H, hum_slot_w, 16, BLACK);
    M5Cardputer.Display.setCursor(ht_val_x, y0 + AC_AUTO_LINE_H);
    if (g_has_humidity) {
        snprintf(num, sizeof(num), "%.0f", static_cast<double>(g_hum));
        printHtValueUnit(num, "%", true, AC_AUTO_COLOR_HUM);
    } else {
        printHtValueUnit("--", "%", false, AC_AUTO_COLOR_HUM);
    }

    // on/off 次数槽：预留 5 位
    const int cnt_slot_w = M5Cardputer.Display.textWidth("99999");
    M5Cardputer.Display.fillRect(cnt_val_x, y0, cnt_slot_w, 16, BLACK);
    M5Cardputer.Display.fillRect(cnt_val_x, y0 + AC_AUTO_LINE_H, cnt_slot_w, 16, BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    snprintf(num, sizeof(num), "%u", static_cast<unsigned>(g_on_times));
    M5Cardputer.Display.setCursor(cnt_val_x, y0);
    M5Cardputer.Display.print(num);
    snprintf(num, sizeof(num), "%u", static_cast<unsigned>(g_off_times));
    M5Cardputer.Display.setCursor(cnt_val_x, y0 + AC_AUTO_LINE_H);
    M5Cardputer.Display.print(num);

    // 电源图标仅在开关态变化时重绘，避免温湿度更新时右侧跟着闪
    if (redraw_power) {
        const int power_x = screen_w - APP_CONTENT_X - AC_AUTO_POWER_ICON_PX;
        const int power_y = y0 + (AC_AUTO_STATS_H - AC_AUTO_POWER_ICON_PX) / 2;
        M5Cardputer.Display.fillRect(power_x, power_y, AC_AUTO_POWER_ICON_PX, AC_AUTO_POWER_ICON_PX,
                                     BLACK);
        if (!drawPowerIconAt(power_x, power_y, g_ac_power)) {
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(g_ac_power ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(power_x, y0 + (AC_AUTO_STATS_H - 8) / 2);
            M5Cardputer.Display.print(g_ac_power ? "ON" : "OFF");
        }
    }
}

static void drawDisplayChartArea() {
    const int chart_h = displayChartH();
    const int chart_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    if (chart_h >= 36) {
        drawHistoryChart(APP_CONTENT_X, displayChartY(), chart_w, chart_h);
    }
}

static void scheduleStatsRefresh() {
    // 已在防抖中不延长，避免突发窗口连续 adv 把刷新一直往后推
    if (!g_stats_defer) {
        g_stats_defer = true;
        g_stats_defer_until_ms = millis() + AC_AUTO_STATS_DEBOUNCE_MS;
    }
}

static void scheduleTopRefresh() {
    g_top_defer = true;
}

static void refreshDisplayTopBar() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    // 只清顶栏条，不动统计区
    M5Cardputer.Display.fillRect(0, displayTopY(), M5Cardputer.Display.width(), AC_AUTO_TOP_H,
                                 BLACK);
    drawDisplayTopBar();
    g_ui_auto_active = g_auto_active;
    g_last_top_ms = millis();
}

static void refreshDisplayStatsData() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    drawDisplayStatsData(false);
    captureStatsUi();
}

static void refreshDisplayTopAndStats() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    refreshDisplayTopBar();
    refreshDisplayStatsData();
}

// 合并本轮待刷：温湿度防抖到期后再画，顶栏可同帧一起刷
static void flushDeferredDisplayRefresh() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        g_stats_defer = false;
        g_top_defer = false;
        return;
    }
    const uint32_t now = millis();
    const bool stats_due =
        g_stats_defer && static_cast<int32_t>(now - g_stats_defer_until_ms) >= 0;
    if (!stats_due && !g_top_defer) {
        return;
    }
    // 统计仍在防抖：顶栏若急需可先刷；否则等统计一起刷减少闪
    if (g_top_defer && !stats_due && g_stats_defer) {
        refreshDisplayTopBar();
        g_top_defer = false;
        return;
    }
    if (g_top_defer) {
        refreshDisplayTopBar();
        g_top_defer = false;
    }
    if (stats_due) {
        if (statsUiChanged()) {
            refreshDisplayStatsData();
        }
        g_stats_defer = false;
    }
}

static void drawDisplayPage() {
    M5Cardputer.Display.fillScreen(BLACK);
    drawDisplayTopBar();
    drawDisplayStats();
    drawDisplayChartArea();
    captureStatsUi();
    g_stats_defer = false;
    g_top_defer = false;
    g_last_top_ms = millis();
}

static void refreshDisplayChart() {
    if (g_display_blanked || g_page != AcAutoPage::Display || g_help_visible) {
        return;
    }
    const int chart_h = displayChartH();
    if (chart_h < 36) {
        return;
    }
    M5Cardputer.Display.fillRect(APP_CONTENT_X, displayChartY(),
                                 M5Cardputer.Display.width() - APP_CONTENT_X * 2, chart_h, BLACK);
    drawDisplayChartArea();
}

static void drawConfigPage() {
    M5Cardputer.Display.fillScreen(BLACK);

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

    // 无系统 header：顶行标题 + 配置行
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, 2);
    M5Cardputer.Display.print("AC Auto ");
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print("cfg");

    int y = 14;
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
    g_help_page = 0;
    g_display_blanked = false;
    g_boot_loading = false;
    g_auto_active = false;
    g_last_input_ms = millis();
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
    g_has_humidity = false;
    g_hum = 0.f;
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

    // 需要拉起 BLE 且协议栈尚未就绪时，先全屏 Loading（无 header），避免空顶栏干等
    const bool need_ble_boot = g_watch_valid && !isBleStackReady();
    if (need_ble_boot) {
        g_boot_loading = true;
        drawAcAutoBusyTip("Loading...");
    }

    // 预载开关机图标进 RAM，顶栏/统计局部刷新时不再解图闪烁
    preloadPowerIcons();

    ensureBleWatch();

    g_boot_loading = false;
    drawAcAutoApp();
}

void leaveAcAutoApp() {
    flushConfigIfDirty();
    stopBleWatch();
    freePowerIconCache();
    g_boot_loading = false;
    if (g_display_blanked) {
        wakeAcAutoDisplay(false);
    }
    g_auto_active = false;
    g_help_visible = false;
    g_help_page = 0;
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
    // 亮屏时手动熄屏（与 S 相同）
    if (g_page == AcAutoPage::Display && !g_help_visible) {
        blankAcAutoDisplay();
    }
}

void updateAcAutoApp() {
    const uint32_t now = millis();

    // BLE 读数：数值变更才排队局部刷（防抖合并温湿度分发包）
    if (g_watch_valid) {
        MijiaBleReading reading = {};
        // 仅在本帧结束了一轮扫描时排程；已 idle 时 poll 也会返回 done，不能反复刷新 nap 终点
        const bool was_running = mijiaBleScanIsRunning();
        const bool done = mijiaBleScanPoll(reading, nullptr);
        // 米家温湿度常分开发（0x1004 温度 / 0x1006 湿度），需分别合并，不能只认带温度的包
        if (reading.ok && (reading.has_temp || reading.has_humidity)) {
            if (reading.has_humidity) {
                g_hum = reading.humidity;
                g_has_humidity = true;
            }
            if (reading.has_temp) {
                const bool first_reading = !g_has_reading;
                g_temp_c = reading.temperature;
                g_has_reading = true;
                g_ble_got_reading = true;
                applyAutomation(g_temp_c);
                // 首包立刻记一点（与 AUTO 无关），之后每分钟追加
                if (first_reading) {
                    pushHistorySample();
                    g_last_hist_ms = millis();
                    refreshDisplayChart();
                }
                // 首包开启短突发窗口，方便攒 filter，并等可能稍后到的湿度包
                if (g_ble_burst_until_ms == 0) {
                    g_ble_burst_until_ms = millis() + AC_AUTO_BLE_BURST_S * 1000;
                }
            }
            // 未变（突发窗口重复 adv）不排队，避免顶栏+统计连闪
            if (statsUiChanged()) {
                scheduleStatsRefresh();
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

    // BLE 监听状态 / 电量：只刷顶栏，不连带清统计区
    if (!g_display_blanked && g_page == AcAutoPage::Display && !g_help_visible) {
        const int ble_st = bleListenUiState();
        const bool ble_changed = ble_st != g_ble_ui_shown;
        const bool top_due = (now - g_last_top_ms) >= AC_AUTO_TOP_REFRESH_MS;
        if (ble_changed || top_due) {
            scheduleTopRefresh();
        }
    }

    flushDeferredDisplayRefresh();

    // 每分钟记历史：只刷 chart
    if (g_has_reading && (now - g_last_hist_ms) >= AC_AUTO_HIST_INTERVAL_MS) {
        g_last_hist_ms = now;
        pushHistorySample();
        refreshDisplayChart();
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
            // help 分页：,. 与 []
            if (c == ',' || c == '<' || c == '[') {
                g_help_page = (g_help_page + AC_AUTO_HELP_PAGES - 1) % AC_AUTO_HELP_PAGES;
                drawAcAutoApp();
                return;
            }
            if (c == '.' || c == '>' || c == ']') {
                g_help_page = (g_help_page + 1) % AC_AUTO_HELP_PAGES;
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
            g_help_page = 0;
            drawAcAutoApp();
            return;
        }
        if (c == 'c' || c == 'C') {
            if (g_page == AcAutoPage::Config) {
                flushConfigIfDirty();
                g_page = AcAutoPage::Display;
            } else {
                g_page = AcAutoPage::Config;
            }
            drawAcAutoApp();
            return;
        }
        // T：开/关 AUTO（可亮屏运行）
        if ((c == 't' || c == 'T') && g_page == AcAutoPage::Display) {
            g_auto_active = !g_auto_active;
            if (!g_auto_active) {
                g_on_streak = 0;
                g_off_streak = 0;
            }
            // 按键反馈立即刷顶栏，不走防抖
            g_stats_defer = false;
            g_top_defer = false;
            refreshDisplayTopBar();
            return;
        }
        // S：手动熄屏入口
        if ((c == 's' || c == 'S') && g_page == AcAutoPage::Display) {
            blankAcAutoDisplay();
            return;
        }
        if ((c == 'r' || c == 'R') && g_page == AcAutoPage::Display) {
            g_on_times = 0;
            g_off_times = 0;
            g_on_streak = 0;
            g_off_streak = 0;
            g_stats_defer = false;
            g_top_defer = false;
            refreshDisplayStatsData();
            return;
        }
        // P：仅对齐内部假定开关态，不发红外
        if ((c == 'p' || c == 'P') && g_page == AcAutoPage::Display) {
            g_ac_power = !g_ac_power;
            g_on_streak = 0;
            g_off_streak = 0;
            g_stats_defer = false;
            g_top_defer = false;
            refreshDisplayStatsData();
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
