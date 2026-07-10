#include "app_mijia.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include "app_colors.h"
#include "app_icons.h"
#include "app_mijia_ui.h"
#include "app_device_icons.h"
#include "mijia_control.h"
#include <WiFi.h>
#include <cctype>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static int mijiaDeviceIdx = 0;
static bool mijiaOverviewMode = false;
static bool mijiaHelpVisible = false;
static int mijiaOverviewScrollIdx = 0;
static int mijiaOverviewEntryDeviceIdx = 0;
static MijiaUiState mijiaUi{};
static int mijiaRefreshGen = 0;
static volatile bool mijiaRefreshTaskRunning = false;
static volatile bool mijiaRefreshTimedOut = false;
static volatile bool mijiaNeedRedraw = false;
static uint32_t mijiaRefreshDeadlineMs = 0;

static constexpr uint32_t MIJIA_REFRESH_TIMEOUT_MS = 1000;
static constexpr uint32_t MIJIA_WIFI_TIMEOUT_MS = 12000;

enum class MijiaWifiPhase : uint8_t {
    IDLE,
    CONNECTING, // 联网中，不计入设备查询超时
    READY,
    FAILED,
};

static MijiaWifiPhase mijiaWifiPhase = MijiaWifiPhase::IDLE;
static uint32_t mijiaWifiDeadlineMs = 0;
static char mijiaNetStatus[32] = "";

struct MijiaRefreshJob {
    int gen;
    int device_idx;
    uint32_t deadline_ms;
    MijiaDevice device;
};

static void scheduleMijiaRefresh();
static void requestMijiaRefresh();
static void drawMijiaHelpPage();

// 是否已连上 config 中的 WiFi
static bool isMijiaConfigWifiConnected() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        return false;
    }
    return WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid;
}

// 联网阶段在 UI 上显示的网络状态（nullptr 表示不显示）
static const char* getMijiaNetworkStatusForUi() {
    if (mijiaUi.power_known || mijiaNetStatus[0] == '\0') {
        return nullptr;
    }
    return mijiaNetStatus;
}

// 进入米家后先非阻塞联网，成功后再启动设备查询
static void startMijiaWifiConnect() {
    const AppConfig& cfg = getAppConfig();
    mijiaNetStatus[0] = '\0';

    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        mijiaWifiPhase = MijiaWifiPhase::FAILED;
        strncpy(mijiaUi.status, "未配置WiFi", sizeof(mijiaUi.status));
        strncpy(mijiaNetStatus, "未配置WiFi", sizeof(mijiaNetStatus));
        mijiaNeedRedraw = true;
        return;
    }

    if (isMijiaConfigWifiConnected()) {
        mijiaWifiPhase = MijiaWifiPhase::READY;
        strncpy(mijiaNetStatus, "已连接", sizeof(mijiaNetStatus));
        requestMijiaRefresh();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
    mijiaWifiPhase = MijiaWifiPhase::CONNECTING;
    mijiaWifiDeadlineMs = millis() + MIJIA_WIFI_TIMEOUT_MS;
    strncpy(mijiaUi.status, "正在连接网络", sizeof(mijiaUi.status));
    strncpy(mijiaNetStatus, "正在连接网络", sizeof(mijiaNetStatus));
    mijiaRefreshDeadlineMs = 0;
    mijiaNeedRedraw = true;
}

// 主循环轮询 WiFi，联网成功后再开始设备查询（不计入设备超时）
static void updateMijiaWifiConnect() {
    if (mijiaWifiPhase != MijiaWifiPhase::CONNECTING) {
        return;
    }

    if (isMijiaConfigWifiConnected()) {
        mijiaWifiPhase = MijiaWifiPhase::READY;
        strncpy(mijiaNetStatus, "已连接", sizeof(mijiaNetStatus));
        requestMijiaRefresh();
        return;
    }

    if (static_cast<int32_t>(millis() - mijiaWifiDeadlineMs) >= 0) {
        mijiaWifiPhase = MijiaWifiPhase::FAILED;
        strncpy(mijiaUi.status, "连接失败", sizeof(mijiaUi.status));
        strncpy(mijiaNetStatus, "连接失败", sizeof(mijiaNetStatus));
        mijiaRefreshDeadlineMs = 0;
        mijiaNeedRedraw = true;
    }
}

// 按当前模式重绘控制页或帮助页
static void redrawMijiaScreen() {
    if (mijiaHelpVisible && !mijiaOverviewMode) {
        drawMijiaHelpPage();
    } else {
        drawMijiaApp();
    }
}

static const MijiaDevice* getCurrentMijiaDevice() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count == 0) {
        return nullptr;
    }
    if (mijiaDeviceIdx < 0) {
        mijiaDeviceIdx = 0;
    }
    if (mijiaDeviceIdx >= cfg.device_count) {
        mijiaDeviceIdx = cfg.device_count - 1;
    }
    return &cfg.devices[mijiaDeviceIdx];
}

// 后台任务结束后按需继续拉取最新设备
static void finishMijiaRefreshTask(const int job_gen) {
    mijiaRefreshTaskRunning = false;
    if (job_gen != mijiaRefreshGen && !mijiaRefreshTimedOut) {
        scheduleMijiaRefresh();
    }
}

// 后台任务：查询设备状态，结果仅在与当前 gen/索引一致时写回
static void mijiaRefreshTaskFn(void* arg) {
    MijiaRefreshJob* job = static_cast<MijiaRefreshJob*>(arg);
    const int job_gen = job->gen;
    const int job_idx = job->device_idx;
    const uint32_t job_deadline_ms = job->deadline_ms;
    const MijiaDevice device = job->device;
    delete job;

    if (job_gen != mijiaRefreshGen || mijiaRefreshTimedOut) {
        finishMijiaRefreshTask(job_gen);
        vTaskDelete(nullptr);
        return;
    }

    if (!isMijiaConfigWifiConnected()) {
        if (job_gen == mijiaRefreshGen && job_idx == mijiaDeviceIdx) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
            mijiaUi.power_known = false;
            mijiaUi.extra_known = false;
            mijiaRefreshDeadlineMs = 0;
            mijiaNeedRedraw = true;
        }
        finishMijiaRefreshTask(job_gen);
        vTaskDelete(nullptr);
        return;
    }

    if (job_gen != mijiaRefreshGen || mijiaRefreshTimedOut) {
        finishMijiaRefreshTask(job_gen);
        vTaskDelete(nullptr);
        return;
    }

    MijiaUiState temp{};
    mijiaResetUiState(temp);
    mijiaRefreshDevice(&device, temp);

    const bool job_timed_out =
        job_deadline_ms != 0 && static_cast<int32_t>(millis() - job_deadline_ms) >= 0;
    if (mijiaRefreshTimedOut) {
        // UI 层已先判定超时，丢弃晚到结果
    } else if (job_timed_out && job_gen == mijiaRefreshGen && job_idx == mijiaDeviceIdx) {
        mijiaRefreshTimedOut = true;
        mijiaRefreshGen++;
        mijiaRefreshDeadlineMs = 0;
        strncpy(mijiaUi.status, "timeout", sizeof(mijiaUi.status));
        mijiaUi.power_known = false;
        mijiaUi.extra_known = false;
        mijiaNeedRedraw = true;
    } else if (!job_timed_out && job_gen == mijiaRefreshGen && job_idx == mijiaDeviceIdx) {
        mijiaUi = temp;
        mijiaRefreshDeadlineMs = 0;
        mijiaNetStatus[0] = '\0';
        mijiaNeedRedraw = true;
    }

    finishMijiaRefreshTask(job_gen);
    vTaskDelete(nullptr);
}

// 启动一次异步状态查询（若已有任务在跑则等其结束后链式触发）
static void scheduleMijiaRefresh() {
    if (mijiaRefreshTaskRunning) {
        return;
    }

    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        strncpy(mijiaUi.status, "no device", sizeof(mijiaUi.status));
        mijiaUi.power_known = false;
        mijiaRefreshDeadlineMs = 0;
        mijiaNeedRedraw = true;
        return;
    }

    auto* job = new MijiaRefreshJob{};
    job->gen = mijiaRefreshGen;
    job->device_idx = mijiaDeviceIdx;
    job->device = *dev;
    job->deadline_ms = mijiaRefreshDeadlineMs;

    mijiaRefreshTaskRunning = true;
    if (xTaskCreate(mijiaRefreshTaskFn, "mijia_ref", 8192, job, 1, nullptr) != pdPASS) {
        delete job;
        mijiaRefreshTaskRunning = false;
        mijiaRefreshDeadlineMs = 0;
        strncpy(mijiaUi.status, "task fail", sizeof(mijiaUi.status));
        mijiaUi.power_known = false;
        mijiaNeedRedraw = true;
    }
}

// 请求刷新当前设备（不阻塞按键处理；需 WiFi 已就绪）
static void requestMijiaRefresh() {
    if (mijiaWifiPhase != MijiaWifiPhase::READY && !isMijiaConfigWifiConnected()) {
        return;
    }
    mijiaWifiPhase = MijiaWifiPhase::READY;
    mijiaRefreshTimedOut = false;
    mijiaRefreshGen++;
    mijiaRefreshDeadlineMs = millis() + MIJIA_REFRESH_TIMEOUT_MS;
    strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
    mijiaUi.power_known = false;
    mijiaUi.extra_known = false;
    mijiaNeedRedraw = true;
    scheduleMijiaRefresh();
}

// 状态查询超过 1s 就判定超时，晚到结果会被 gen 丢弃
static void updateMijiaRefreshTimeout() {
    if (mijiaRefreshTimedOut || mijiaRefreshDeadlineMs == 0) {
        return;
    }
    if (static_cast<int32_t>(millis() - mijiaRefreshDeadlineMs) < 0) {
        return;
    }

    mijiaRefreshTimedOut = true;
    mijiaRefreshGen++;
    mijiaRefreshDeadlineMs = 0;
    strncpy(mijiaUi.status, "timeout", sizeof(mijiaUi.status));
    mijiaUi.power_known = false;
    mijiaUi.extra_known = false;
    mijiaNeedRedraw = true;
}

// 立即切换设备并异步拉状态
static void switchMijiaDevice(const int delta, const int device_count) {
    mijiaDeviceIdx = (mijiaDeviceIdx + delta + device_count) % device_count;
    mijiaResetUiState(mijiaUi);
    strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
    redrawMijiaScreen();
    requestMijiaRefresh();
}

// 设置当前设备开关
static void setMijiaPower(const bool on) {
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        strncpy(mijiaUi.status, "no device", sizeof(mijiaUi.status));
        return;
    }

    if (!ensureConfigWifi()) {
        strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        return;
    }

    redrawMijiaScreen();
    mijiaSetDevicePower(dev, mijiaUi, on);
}

// 概览列表每屏固定显示 2 项
static int getMijiaOverviewVisibleCount() {
    return MIJIA_LIST_VISIBLE_COUNT;
}

// 概览列表每项高度：两格均分内容区（扣除底栏提示）
static int getMijiaOverviewItemHeight() {
    constexpr int hint_h = 12;
    constexpr int gap = 6;
    const int avail = M5Cardputer.Display.height() - APP_CONTENT_Y - hint_h;
    return (avail - gap) / MIJIA_LIST_VISIBLE_COUNT;
}

static int getMijiaOverviewPageCount(const int device_count) {
    const int visible = getMijiaOverviewVisibleCount();
    if (device_count <= 0) {
        return 1;
    }
    return (device_count + visible - 1) / visible;
}

static int getMijiaOverviewPage(const int device_count) {
    const int visible = getMijiaOverviewVisibleCount();
    if (device_count <= 0) {
        return 0;
    }
    return mijiaOverviewScrollIdx / visible;
}

// 概览列表翻页
static bool handleMijiaOverviewNav(const int delta) {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 1) {
        return false;
    }

    const int visible = getMijiaOverviewVisibleCount();
    const int page_count = getMijiaOverviewPageCount(cfg.device_count);
    if (page_count <= 1) {
        mijiaDeviceIdx = (mijiaDeviceIdx + delta + cfg.device_count) % cfg.device_count;
        mijiaOverviewScrollIdx = (mijiaDeviceIdx / visible) * visible;
        redrawMijiaScreen();
        return true;
    }

    int page = getMijiaOverviewPage(cfg.device_count);
    page = (page + delta + page_count) % page_count;
    mijiaOverviewScrollIdx = page * visible;
    mijiaDeviceIdx = mijiaOverviewScrollIdx;
    if (mijiaDeviceIdx >= cfg.device_count) {
        mijiaDeviceIdx = cfg.device_count - 1;
    }
    redrawMijiaScreen();
    return true;
}

// 绘制概览底栏按键提示（drawKeyBadge）
static void drawMijiaOverviewHints(const AppConfig& cfg) {
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_CONTENT_X;

    if (cfg.loaded && cfg.device_count > 1) {
        char pos_buf[20];
        const int page_count = getMijiaOverviewPageCount(cfg.device_count);
        if (page_count > 1) {
            snprintf(pos_buf, sizeof(pos_buf), "p%d/%d %d/%d",
                     getMijiaOverviewPage(cfg.device_count) + 1, page_count,
                     mijiaDeviceIdx + 1, cfg.device_count);
        } else {
            snprintf(pos_buf, sizeof(pos_buf), "%d/%d", mijiaDeviceIdx + 1, cfg.device_count);
        }
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y);
        M5Cardputer.Display.print(pos_buf);
        cx += M5Cardputer.Display.textWidth(pos_buf) + 6;
    }

    static const KeyHintItem nav_items[] = {
        {',', "page-"},
        {'.', "page+"},
        {'1', "pick1"},
        {'2', "pick2"},
        {'i', "back"},
    };
    drawKeyHintsRow(cx, hint_y, nav_items, 5, 1, APP_COLOR_HINT);
}

// 绘制单项：左图标（缩放）+ 右名称/IP/型号
static void drawMijiaOverviewItem(const MijiaDevice& entry, const int x, const int y,
                                  const int item_h, const bool selected) {
    const MijiaDevKind kind = mijiaClassifyModel(entry.model);
    const uint16_t name_color = selected ? APP_COLOR_OK : APP_COLOR_VALUE;
    const int icon_px = MIJIA_LIST_ICON_PX;
    const float png_scale = static_cast<float>(MIJIA_LIST_ICON_PX) / DEVICE_ICON_NATIVE_PX;
    const int icon_y = y + (item_h - icon_px) / 2;
    drawMijiaDeviceIconFor(&entry, kind, x, icon_y, selected ? APP_COLOR_OK : APP_COLOR_HINT,
                           false, MIJIA_ICON_SCALE_LIST, png_scale);

    const int text_x = x + icon_px + 6;
    const int text_block_h = INFO_LINE_H_2X + INFO_LINE_H * 2;
    const int text_y = y + (item_h - text_block_h) / 2;

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(name_color, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y);
    if (entry.name[0] != '\0') {
        M5Cardputer.Display.print(entry.name);
    } else {
        M5Cardputer.Display.print("device");
    }

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y + INFO_LINE_H_2X);
    M5Cardputer.Display.print(entry.ip);

    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y + INFO_LINE_H_2X + INFO_LINE_H);
    if (entry.model[0] != '\0') {
        M5Cardputer.Display.print(entry.model);
    } else {
        M5Cardputer.Display.print("-");
    }
}

static void drawMijiaOverview(int& y) {
    const AppConfig& cfg = getAppConfig();

    if (!cfg.loaded || cfg.device_count == 0) {
        drawInfoLine(APP_CONTENT_X, y, "total", "0");
        drawInfoLine(APP_CONTENT_X, y, "hint", "press u web");
        static const KeyHintItem empty_items[] = {
            {'u', "web"},
            {'i', "back"},
        };
        drawKeyHintsRow(APP_CONTENT_X, M5Cardputer.Display.height() - 12, empty_items, 2, 1,
                        APP_COLOR_HINT);
        return;
    }

    const int visible = getMijiaOverviewVisibleCount();
    const int max_scroll = cfg.device_count > visible ? cfg.device_count - visible : 0;
    if (mijiaOverviewScrollIdx > max_scroll) {
        mijiaOverviewScrollIdx = max_scroll;
    }
    if (mijiaOverviewScrollIdx < 0) {
        mijiaOverviewScrollIdx = 0;
    }

    const int item_h = getMijiaOverviewItemHeight();
    constexpr int item_gap = 6;
    int item_y = APP_CONTENT_Y;
    for (int i = 0; i < visible; i++) {
        const int idx = mijiaOverviewScrollIdx + i;
        if (idx >= cfg.device_count) {
            break;
        }
        drawMijiaOverviewItem(cfg.devices[idx], APP_CONTENT_X, item_y, item_h,
                              idx == mijiaDeviceIdx);
        item_y += item_h + item_gap;
    }
    y = item_y;
    drawMijiaOverviewHints(cfg);
}

bool handleMijiaOverviewPageNav(const Keyboard_Class::KeysState& status) {
    if (!mijiaOverviewMode) {
        return false;
    }
    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return false;
    }
    return handleMijiaOverviewNav(delta);
}

// 控制页切换设备
bool handleMijiaDeviceNav(const Keyboard_Class::KeysState& status) {
    if (mijiaOverviewMode || mijiaHelpVisible) {
        return false;
    }

    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 1) {
        return false;
    }

    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return false;
    }

    switchMijiaDevice(delta, cfg.device_count);
    return true;
}

static int mijiaHintLineStep(const int text_size) {
    return text_size == 2 ? INFO_LINE_H_2X : INFO_LINE_H;
}

// 帮助页：按总行数均分垂直空间，返回第 row 行绘制 y
static int mijiaHelpRowY(const int row, const int total_rows, const int text_size) {
    const int content_h = M5Cardputer.Display.height() - APP_CONTENT_Y;
    const int line_h = mijiaHintLineStep(text_size);
    if (total_rows <= 0) {
        return APP_CONTENT_Y;
    }
    const int slot_h = content_h / total_rows;
    return APP_CONTENT_Y + row * slot_h + (slot_h - line_h) / 2;
}

// 估算按键徽章 + 文案占用宽度
static int mijiaMeasureKeyHintItem(const KeyHintItem& item, const int text_size) {
    const int size = (text_size == 2) ? 2 : 1;
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(item.key)));
    const char str[2] = {letter, '\0'};
    M5Cardputer.Display.setTextSize(size);
    const int badge_w = M5Cardputer.Display.textWidth(str) + 4 + 3;
    M5Cardputer.Display.setTextSize(text_size);
    return badge_w + M5Cardputer.Display.textWidth(item.text);
}

// 估算换行后的行数
static int mijiaCountWrappedRows(const KeyHintItem* items, const int item_count,
                                 const int text_size, const int max_w) {
    if (items == nullptr || item_count <= 0) {
        return 0;
    }

    int rows = 1;
    int cx = 0;
    M5Cardputer.Display.setTextSize(text_size);
    const int space_w = M5Cardputer.Display.textWidth(" ");
    for (int i = 0; i < item_count; i++) {
        const int item_w = mijiaMeasureKeyHintItem(items[i], text_size);
        if (cx > 0 && cx + item_w > max_w) {
            rows++;
            cx = item_w;
        } else {
            if (cx > 0) {
                cx += space_w;
            }
            cx += item_w;
        }
    }
    return rows;
}

static int mijiaCountHelpRows(const MijiaDevKind kind, const int max_w, const int text_size) {
    static const KeyHintItem action_items[] = {
        {'o', "on"},
        {'f', "off"},
        {'t', "toggle"},
        {'i', "info"},
        {'h', "help"},
    };

    int rows = mijiaCountWrappedRows(action_items, 5, text_size, max_w);
    rows += 1; // refresh + switch

    switch (kind) {
        case MijiaDevKind::LIGHT: {
            static const KeyHintItem bright_items[] = {{'-', "bright-"}, {'=', "bright+"}};
            static const KeyHintItem percent_items[] = {{'1', "10%"}, {'9', "90%"}, {'0', "100%"}};
            static const KeyHintItem ct_items[] = {{'[', "ct-"}, {']', "ct+"}};
            rows += mijiaCountWrappedRows(bright_items, 2, text_size, max_w);
            rows += mijiaCountWrappedRows(percent_items, 3, text_size, max_w);
            rows += mijiaCountWrappedRows(ct_items, 2, text_size, max_w);
            break;
        }
        case MijiaDevKind::FAN_P5: {
            static const KeyHintItem fan_items[] = {
                {'-', "spd-"},
                {'=', "spd+"},
                {'w', "roll"},
                {'m', "mode"},
            };
            rows += mijiaCountWrappedRows(fan_items, 4, text_size, max_w);
            break;
        }
        case MijiaDevKind::FAN_GENERIC: {
            static const KeyHintItem speed_items[] = {
                {'1', "lv1"},
                {'2', "lv2"},
                {'3', "lv3"},
                {'4', "lv4"},
            };
            rows += mijiaCountWrappedRows(speed_items, 4, text_size, max_w);
            break;
        }
        case MijiaDevKind::AIR_PURIFIER_F20: {
            static const KeyHintItem mode_items[] = {
                {'1', "mode1"},
                {'2', "mode2"},
                {'3', "mode3"},
                {'4', "mode4"},
                {'5', "mode5"},
            };
            static const KeyHintItem fan_items[] = {{'-', "fan-"}, {'=', "fan+"}};
            rows += mijiaCountWrappedRows(mode_items, 5, text_size, max_w);
            rows += mijiaCountWrappedRows(fan_items, 2, text_size, max_w);
            break;
        }
        default:
            break;
    }
    return rows;
}

// 绘制单个按键提示，返回占用宽度
static int mijiaDrawKeyHintItem(const int x, const int y, const KeyHintItem& item,
                                const int text_size, const uint16_t color) {
    int cx = x + drawKeyBadge(x, y, item.key, text_size);
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(item.text);
    return cx + M5Cardputer.Display.textWidth(item.text) - x;
}

// 按屏宽换行绘制按键提示，返回下一行索引
static int drawKeyHintsWrapped(const int x, const int start_row, const int total_rows,
                               const KeyHintItem* items, const int item_count,
                               const int text_size, const uint16_t color, const int max_w) {
    if (items == nullptr || item_count <= 0) {
        return start_row;
    }

    int row = start_row;
    int y = mijiaHelpRowY(row, total_rows, text_size);
    int cx = x;
    M5Cardputer.Display.setTextSize(text_size);
    const int space_w = M5Cardputer.Display.textWidth(" ");

    for (int i = 0; i < item_count; i++) {
        const int item_w = mijiaMeasureKeyHintItem(items[i], text_size);
        if (cx > x && cx + item_w > x + max_w) {
            row++;
            y = mijiaHelpRowY(row, total_rows, text_size);
            cx = x;
        }
        cx += mijiaDrawKeyHintItem(cx, y, items[i], text_size, color);
        if (i != item_count - 1) {
            M5Cardputer.Display.setCursor(cx, y);
            M5Cardputer.Display.print(" ");
            cx += space_w;
        }
    }
    return row + 1;
}

// refresh 与 switch 同一行
static void drawMijiaRefreshHelpRow(const int x, const int row, const int total_rows,
                                    const int text_size) {
    const int y = mijiaHelpRowY(row, total_rows, text_size);
    static const KeyHintItem refresh_item = {'r', "refresh"};
    int cx = x + mijiaDrawKeyHintItem(x, y, refresh_item, text_size, APP_COLOR_HINT);

    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("  ");
    cx += M5Cardputer.Display.textWidth("  ");

    const int arrow_cy = y + 4 * text_size;
    drawIconArrowLeft(cx, arrow_cy, APP_COLOR_HINT);
    cx += ICON_ARROW_W + 4;
    drawIconArrowRight(cx, arrow_cy, APP_COLOR_HINT);
    cx += ICON_ARROW_W + 4;
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("switch");
}

// 灯：亮度 + 色温调节说明
static int drawMijiaLightHelpRows(const MijiaDevice* dev, const int start_row, const int total_rows,
                                  const int text_size, const int max_w) {
    static const KeyHintItem bright_items[] = {{'-', "bright-"}, {'=', "bright+"}};
    static const KeyHintItem percent_items[] = {{'1', "10%"}, {'9', "90%"}, {'0', "100%"}};
    static const KeyHintItem ct_items[] = {{'[', "ct-"}, {']', "ct+"}};
    int row = drawKeyHintsWrapped(APP_CONTENT_X, start_row, total_rows, bright_items, 2, text_size,
                                  APP_COLOR_HINT, max_w);
    row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, percent_items, 3, text_size,
                              APP_COLOR_HINT, max_w);
    if (dev != nullptr && mijiaLightSupportsCt(dev->model)) {
        row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, ct_items, 2, text_size,
                                  APP_COLOR_HINT, max_w);
    }
    return row;
}

// 按设备类型绘制操作帮助（text_size=2，垂直空间均分）
static void drawMijiaHelpContent(const MijiaDevice* dev, const int text_size) {
    const MijiaDevKind kind =
        dev != nullptr ? mijiaClassifyModel(dev->model) : MijiaDevKind::GENERIC;
    const int max_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    const int total_rows = mijiaCountHelpRows(kind, max_w, text_size);

    static const KeyHintItem action_items[] = {
        {'o', "on"},
        {'f', "off"},
        {'t', "toggle"},
        {'i', "info"},
        {'h', "help"},
    };

    int row = 0;
    row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, action_items, 5, text_size,
                              APP_COLOR_HINT, max_w);
    drawMijiaRefreshHelpRow(APP_CONTENT_X, row, total_rows, text_size);
    row++;

    switch (kind) {
        case MijiaDevKind::LIGHT:
            row = drawMijiaLightHelpRows(dev, row, total_rows, text_size, max_w);
            break;
        case MijiaDevKind::FAN_P5: {
            static const KeyHintItem fan_items[] = {
                {'-', "spd-"},
                {'=', "spd+"},
                {'w', "roll"},
                {'m', "mode"},
            };
            row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, fan_items, 4, text_size,
                                      APP_COLOR_HINT, max_w);
            break;
        }
        case MijiaDevKind::FAN_GENERIC: {
            static const KeyHintItem speed_items[] = {
                {'1', "lv1"},
                {'2', "lv2"},
                {'3', "lv3"},
                {'4', "lv4"},
            };
            row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, speed_items, 4, text_size,
                                      APP_COLOR_HINT, max_w);
            break;
        }
        case MijiaDevKind::AIR_PURIFIER_F20: {
            static const KeyHintItem mode_items[] = {
                {'1', "mode1"},
                {'2', "mode2"},
                {'3', "mode3"},
                {'4', "mode4"},
                {'5', "mode5"},
            };
            static const KeyHintItem fan_items[] = {{'-', "fan-"}, {'=', "fan+"}};
            row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, mode_items, 5, text_size,
                                      APP_COLOR_HINT, max_w);
            row = drawKeyHintsWrapped(APP_CONTENT_X, row, total_rows, fan_items, 2, text_size,
                                      APP_COLOR_HINT, max_w);
            break;
        }
        default:
            break;
    }
}

// 按 H 切换显示的帮助页
static void drawMijiaHelpPage() {
    beginAppScreen("Help");
    const MijiaDevice* dev = getCurrentMijiaDevice();
    drawMijiaHelpContent(dev, 2);
}

void drawMijiaApp() {
    beginAppScreen("Mijia");
    M5Cardputer.Display.setTextSize(1);

    int y = APP_CONTENT_Y;
    const AppConfig& cfg = getAppConfig();
    const MijiaDevice* dev = getCurrentMijiaDevice();

    if (mijiaOverviewMode) {
        drawMijiaOverview(y);
        return;
    }

    if (!cfg.loaded || dev == nullptr) {
        drawInfoLine(APP_CONTENT_X, y, "cfg", "none");
        drawInfoLine(APP_CONTENT_X, y, "hint", "press u web");
        return;
    }

    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    drawMijiaDevicePanel(dev, kind, mijiaDeviceIdx, cfg.device_count, mijiaUi, APP_CONTENT_X, y,
                         getMijiaNetworkStatusForUi());
}

void enterMijiaApp() {
    mijiaDeviceIdx = 0;
    mijiaOverviewMode = false;
    mijiaHelpVisible = false;
    mijiaOverviewScrollIdx = 0;
    mijiaWifiPhase = MijiaWifiPhase::IDLE;
    mijiaWifiDeadlineMs = 0;
    mijiaRefreshDeadlineMs = 0;
    mijiaNetStatus[0] = '\0';
    mijiaResetUiState(mijiaUi);
    drawMijiaApp();
    startMijiaWifiConnect();
}

void updateMijiaApp() {
    updateMijiaWifiConnect();
    updateMijiaRefreshTimeout();
    if (mijiaNeedRedraw) {
        mijiaNeedRedraw = false;
        redrawMijiaScreen();
    }
}

void handleMijiaApp(const String& key) {
    if (key == "h") {
        if (!mijiaOverviewMode) {
            mijiaHelpVisible = !mijiaHelpVisible;
            redrawMijiaScreen();
        }
        return;
    }
    if (mijiaHelpVisible) {
        return;
    }

    if (key == "i") {
        const bool was_overview = mijiaOverviewMode;
        mijiaOverviewMode = !mijiaOverviewMode;
        if (mijiaOverviewMode) {
            mijiaOverviewEntryDeviceIdx = mijiaDeviceIdx;
            const int visible = getMijiaOverviewVisibleCount();
            mijiaOverviewScrollIdx = (mijiaDeviceIdx / visible) * visible;
        } else if (was_overview && mijiaDeviceIdx != mijiaOverviewEntryDeviceIdx) {
            mijiaResetUiState(mijiaUi);
            strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
            requestMijiaRefresh();
        }
        redrawMijiaScreen();
        return;
    }
    if (mijiaOverviewMode) {
        return;
    }

    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count == 0) {
        return;
    }

    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        return;
    }

    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    bool handled = true;

    if (key == "o") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            setMijiaPower(true);
        }
    } else if (key == "f") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            setMijiaPower(false);
        }
    } else if (key == "t") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            setMijiaPower(!mijiaUi.power_on);
        }
    } else if (key == "r") {
        if (mijiaWifiPhase == MijiaWifiPhase::FAILED || mijiaWifiPhase == MijiaWifiPhase::IDLE) {
            startMijiaWifiConnect();
        } else {
            redrawMijiaScreen();
            requestMijiaRefresh();
        }
    } else if (key == "," || key == ";") {
        switchMijiaDevice(-1, cfg.device_count);
        return;
    } else if (key == "." || key == "/") {
        switchMijiaDevice(1, cfg.device_count);
        return;
    } else if (kind == MijiaDevKind::LIGHT && key == "-") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaAdjustBright(dev, mijiaUi, -10);
        }
    } else if (kind == MijiaDevKind::LIGHT && (key == "=" || key == "+")) {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaAdjustBright(dev, mijiaUi, 10);
        }
    } else if (kind == MijiaDevKind::LIGHT && key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            const int percent = key[0] == '0' ? 100 : (key[0] - '0') * 10;
            mijiaSetBrightPercent(dev, mijiaUi, percent);
        }
    } else if (kind == MijiaDevKind::LIGHT && mijiaLightSupportsCt(dev->model) && key == "[") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaAdjustColorTemp(dev, mijiaUi, -100);
        }
    } else if (kind == MijiaDevKind::LIGHT && mijiaLightSupportsCt(dev->model) && key == "]") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaAdjustColorTemp(dev, mijiaUi, 100);
        }
    } else if (mijiaOverviewMode && (key == "1" || key == "2")) {
        const int visible = getMijiaOverviewVisibleCount();
        const int idx = mijiaOverviewScrollIdx + (key[0] - '1');
        if (idx < cfg.device_count) {
            mijiaDeviceIdx = idx;
            redrawMijiaScreen();
        }
        return;
    } else if (kind == MijiaDevKind::FAN_P5) {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else if (key == "-") {
            mijiaAdjustFanP5Speed(dev, mijiaUi, -10);
        } else if (key == "=" || key == "+") {
            mijiaAdjustFanP5Speed(dev, mijiaUi, 10);
        } else if (key == "w") {
            mijiaToggleFanP5Roll(dev, mijiaUi);
        } else if (key == "m") {
            mijiaToggleFanP5Mode(dev, mijiaUi);
        } else {
            handled = false;
        }
    } else if (kind == MijiaDevKind::FAN_GENERIC && key.length() == 1 && key[0] >= '1' &&
               key[0] <= '4') {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaSetFanSpeedLevel(dev, mijiaUi, key[0] - '0');
        }
    } else if (kind == MijiaDevKind::AIR_PURIFIER_F20) {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else if (key.length() == 1 && key[0] >= '1' && key[0] <= '5') {
            mijiaSetPurifierMode(dev, mijiaUi, key[0] - '1');
        } else if (key == "-") {
            mijiaAdjustPurifierFanLevel(dev, mijiaUi, -1);
        } else if (key == "=" || key == "+") {
            mijiaAdjustPurifierFanLevel(dev, mijiaUi, 1);
        } else {
            handled = false;
        }
    } else {
        handled = false;
    }

    if (handled) {
        redrawMijiaScreen();
    }
}
