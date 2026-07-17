#include "app_mijia.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_colors.h"
#include "app_icons.h"
#include "app_mijia_ui.h"
#include "app_device_icons.h"
#include "mijia_control.h"
#include "mijia_ble.h"
#include "miio_client.h"
#include <WiFi.h>
#include <cctype>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


// 米家状态值定义
static int mijiaDeviceIdx = 0;  // 当前选中的设备索引
static bool mijiaOverviewMode = false; // 是否在概览模式
static bool mijiaOverviewGridMode = false; // 是否在宫格模式
static bool mijiaGroupMode = false; // 是否在编组模式（d 键）
static bool mijiaHelpVisible = false; // 是否在帮助模式
static int mijiaOverviewScrollIdx = 0; // 概览模式下的滚动索引
static int mijiaOverviewEntryDeviceIdx = 0; // 概览模式下的入口设备索引
static int mijiaGroupIdx = 0; // 当前编组
static int mijiaGroupScrollIdx = 0; // 编组内成员页起始下标
static char mijiaGroupStatus[32] = ""; // 编组操作汇总状态
static MijiaUiState mijiaUi{}; // 当前设备的UI状态
static int mijiaRefreshGen = 0; // 刷新生成器
static volatile bool mijiaRefreshTaskRunning = false; // 刷新任务是否正在运行
static volatile bool mijiaRefreshTimedOut = false; // 刷新任务是否超时
static volatile bool mijiaNeedRedraw = false; // 是否需要重绘
static volatile bool mijiaNeedGroupHintsRedraw = false; // 编组底栏局部刷新
static bool mijiaBleScanPending = false;      // 主循环启动聚焦 BLE 扫描
static bool mijiaBleBgEnabled = false;        // 米家前台：后台持续听 BLE
static bool mijiaBleFocusActive = false;      // 正在对当前设备做 r 聚焦扫
static uint32_t mijiaRefreshDeadlineMs = 0; // 刷新任务的截止时间

// 米家状态值常量定义
static constexpr uint32_t MIJIA_REFRESH_TIMEOUT_MS = 2000;  // 刷新任务超时时间
static constexpr uint32_t MIJIA_GRID_REFRESH_TIMEOUT_MS = 2000;  // 宫格刷新任务超时时间
static constexpr uint32_t MIJIA_BLE_FOCUS_SCAN_S = 30;      // r 聚焦扫秒数（脏包可丢弃后继续）
static constexpr uint32_t MIJIA_BLE_BG_SCAN_S = 30;         // 后台一轮监听秒数
static constexpr uint32_t MIJIA_WIFI_TIMEOUT_MS = 12000;  // 联网超时时间

// 按设备缓存最近一次成功的 BLE 读数
struct MijiaBleCache {
    bool valid;
    uint32_t updated_ms;
    bool has_temp;
    bool has_humidity;
    bool has_battery;
    bool has_motion;
    bool has_button;
    float temperature;
    float humidity;
    int battery;
    bool motion;
    bool button;
};
static MijiaBleCache mijiaBleCache[MIJIA_DEVICE_MAX];

enum class MijiaWifiPhase : uint8_t {
    IDLE,
    CONNECTING, // 联网中，不计入设备查询超时
    READY,
    FAILED,
};

static MijiaWifiPhase mijiaWifiPhase = MijiaWifiPhase::IDLE;
static uint32_t mijiaWifiDeadlineMs = 0;
static char mijiaNetStatus[32] = "";
static bool mijiaControlInitialized = false;
static int mijiaRenderedDeviceIdx = -1;
static MijiaUiState mijiaRenderedUi{};
static char mijiaRenderedNetStatus[32] = "";
static MijiaUiState mijiaOverviewUi[MIJIA_DEVICE_MAX];
static int mijiaOverviewRefreshQueue[MIJIA_GRID_PAGE_SIZE];
static int mijiaOverviewRefreshQueueLen = 0;
static int mijiaOverviewRefreshQueuePos = 0;
static int mijiaOverviewPendingCells[MIJIA_GRID_PAGE_SIZE];
static int mijiaOverviewPendingCellCount = 0;
// 编组批量 QUERY / SET_POWER 队列（顺序执行）
static int mijiaGroupJobQueue[MIJIA_GROUP_MEMBER_MAX];
static int mijiaGroupJobQueueLen = 0;
static int mijiaGroupJobQueuePos = 0;
static bool mijiaGroupJobIsSetPower = false;
static bool mijiaGroupJobIsSetBright = false;
static bool mijiaGroupJobPowerOn = false;
static bool mijiaGroupJobBrightAbsolute = true;
static int mijiaGroupJobBrightValue = 50;
static int mijiaGroupJobOk = 0;
static int mijiaGroupJobFail = 0;
static int mijiaGroupJobSkip = 0;

enum class MijiaJobType : uint8_t {
    QUERY,
    SET_POWER,
    SET_BRIGHT,
};

struct MijiaRefreshJob {
    int gen;
    int device_idx;
    uint32_t deadline_ms;
    MijiaDevice device;
    bool overview_cache = false;
    MijiaJobType type = MijiaJobType::QUERY;
    bool power_on = false;
    bool bright_absolute = true;
    int bright_value = 50;
};

static MijiaRefreshJob* mijiaDeferredJob = nullptr;

static void scheduleMijiaRefresh();
static void requestMijiaRefresh();
static void scheduleMijiaOverviewRefreshJob();
static void requestMijiaOverviewPageRefresh();
static void scheduleMijiaGroupJob();
static void requestMijiaGroupPageRefresh();
static void requestMijiaGroupPower(bool on);
static void requestMijiaGroupBright(bool absolute, int value);
static void toggleMijiaGroupPower();
static void cancelMijiaPendingJobs();
static bool scheduleMijiaJob(MijiaRefreshJob* job);
static void queueMijiaGridCellRefresh(int device_idx);
static void applyBleReadingToUi(MijiaUiState& ui, const MijiaBleReading& reading, const char* status);
static void storeBleCache(int device_idx, const MijiaBleReading& reading);
static bool applyBleCacheToUi(int device_idx, MijiaUiState& ui, bool with_age_status);
static void requestMijiaBleBackground();
static void formatBleCacheAgeStatus(uint32_t updated_ms, char* out, size_t out_size);
static void flushMijiaGridCellUpdates();
static void onMijiaGridDeviceChanged(int old_idx, int new_idx);
static void mijiaJobTaskFn(void* arg);
static void drawMijiaHelpPage();
static void drawMijiaGridHelpPage();
static void drawMijiaGroupHelpPage();
static void invalidateMijiaControlSurface();
static void applyMijiaControlRefresh(bool force_full = false);
static void drawMijiaOverview(int& y);
static void drawMijiaGroupView();
static void drawMijiaGroupBottomHints(const AppConfig& cfg);
static void requestMijiaGroupHintsRedraw();
static void refreshMijiaGridCell(int device_idx);
static void refreshMijiaGridSelection(int old_idx, int new_idx);
static void enterMijiaGroupMode();
static void exitMijiaGroupMode();
static void switchMijiaGroup(int delta);
static void switchMijiaGroupPage(int delta);
static void enterMijiaOverview(bool grid_mode);
static void exitMijiaOverview();
static const MijiaDeviceGroup* getCurrentMijiaGroup();
static bool mijiaDeviceIsGroupActuator(const MijiaDevice& dev);
static bool mijiaGroupAllLights();
static void refreshMijiaListSelection(int old_idx, int new_idx);
static bool handleMijiaGridSelectionNav(const Keyboard_Class::KeysState& status);
static bool handleMijiaListSelectionNav(const Keyboard_Class::KeysState& status);
static const MijiaDevice* getCurrentMijiaDevice();

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
    applyWifiRadioSleepPolicy();
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
        mijiaNeedRedraw = true;
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

// 离开控制页后下次需整页初始化
static void invalidateMijiaControlSurface() {
    mijiaControlInitialized = false;
    mijiaRenderedDeviceIdx = -1;
    mijiaRenderedNetStatus[0] = '\0';
    mijiaResetUiState(mijiaRenderedUi);
}

// 记录当前已绘制到屏幕上的控制页状态
static void snapshotMijiaRenderedPanel(const char* net_status) {
    mijiaRenderedDeviceIdx = mijiaDeviceIdx;
    mijiaRenderedUi = mijiaUi;
    if (net_status != nullptr) {
        strncpy(mijiaRenderedNetStatus, net_status, sizeof(mijiaRenderedNetStatus) - 1);
        mijiaRenderedNetStatus[sizeof(mijiaRenderedNetStatus) - 1] = '\0';
    } else {
        mijiaRenderedNetStatus[0] = '\0';
    }
}

// 图标仅随开关态变化
static bool mijiaPanelIconVisualChanged(const MijiaUiState& old_ui, const MijiaUiState& new_ui) {
    const bool old_active = old_ui.power_known && old_ui.power_on;
    const bool new_active = new_ui.power_known && new_ui.power_on;
    return old_ui.power_known != new_ui.power_known || old_active != new_active;
}

// 右栏状态与控制区是否需重绘
static bool mijiaPanelControlsVisualChanged(const MijiaUiState& old_ui, const MijiaUiState& new_ui) {
    return old_ui.extra_known != new_ui.extra_known || old_ui.bright != new_ui.bright ||
           old_ui.color_temp != new_ui.color_temp || old_ui.ct_known != new_ui.ct_known ||
           old_ui.ct_min != new_ui.ct_min || old_ui.ct_max != new_ui.ct_max ||
           old_ui.hue != new_ui.hue || old_ui.hue_known != new_ui.hue_known ||
           old_ui.sat != new_ui.sat || old_ui.speed != new_ui.speed || old_ui.roll != new_ui.roll ||
           old_ui.roll_angle != new_ui.roll_angle || old_ui.mode != new_ui.mode ||
           old_ui.fan_level != new_ui.fan_level || old_ui.aqi != new_ui.aqi ||
           old_ui.fryer_time != new_ui.fryer_time || old_ui.temp_known != new_ui.temp_known ||
           old_ui.humidity_known != new_ui.humidity_known ||
           old_ui.battery_known != new_ui.battery_known ||
           old_ui.temperature != new_ui.temperature || old_ui.humidity != new_ui.humidity ||
           old_ui.battery != new_ui.battery || old_ui.motion_known != new_ui.motion_known ||
           old_ui.motion != new_ui.motion || old_ui.button_known != new_ui.button_known ||
           old_ui.button != new_ui.button;
}

static bool mijiaPanelRightVisualChanged(const MijiaUiState& old_ui, const MijiaUiState& new_ui,
                                         const char* old_net, const char* new_net) {
    // 状态文案变化也要重绘（BLE 有读数后仍会改 listening / Xs ago）
    if (strcmp(old_ui.status, new_ui.status) != 0) {
        return true;
    }
    const bool old_inline = mijiaPanelShowsInlineStatus(old_ui.status, old_ui.power_known);
    const bool new_inline = mijiaPanelShowsInlineStatus(new_ui.status, new_ui.power_known);
    if (old_inline != new_inline) {
        return true;
    }
    if (strcmp(old_net != nullptr ? old_net : "", new_net != nullptr ? new_net : "") != 0) {
        return true;
    }
    if (old_ui.power_known != new_ui.power_known) {
        return true;
    }
    return mijiaPanelControlsVisualChanged(old_ui, new_ui);
}

// 控制页局部刷新：仅重绘变化的图标/右栏区域
static void applyMijiaControlRefresh(const bool force_full) {
    const AppConfig& cfg = getAppConfig();
    const MijiaDevice* dev = getCurrentMijiaDevice();
    const int panel_y = APP_CONTENT_Y;

    if (!cfg.loaded || dev == nullptr) {
        if (!mijiaControlInitialized) {
            beginAppScreen("Mijia");
            mijiaControlInitialized = true;
        } else {
            clearAppContentArea();
        }
        int y = panel_y;
        drawInfoLine(APP_CONTENT_X, y, "cfg", "none");
        drawInfoLine(APP_CONTENT_X, y, "hint", "press u web");
        invalidateMijiaControlSurface();
        mijiaControlInitialized = true;
        return;
    }

    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    const char* net = getMijiaNetworkStatusForUi();
    const MijiaPanelLayout layout = calcMijiaPanelLayout(panel_y, APP_CONTENT_X);
    const bool device_changed = mijiaRenderedDeviceIdx != mijiaDeviceIdx;
    const bool icon_dirty = force_full || !mijiaControlInitialized || device_changed ||
                            mijiaPanelIconVisualChanged(mijiaRenderedUi, mijiaUi);
    const bool right_dirty =
        force_full || !mijiaControlInitialized || device_changed ||
        mijiaPanelRightVisualChanged(mijiaRenderedUi, mijiaUi, mijiaRenderedNetStatus, net);

    if (!icon_dirty && !right_dirty) {
        return;
    }

    if (force_full || !mijiaControlInitialized || device_changed) {
        if (!mijiaControlInitialized) {
            beginAppScreen("Mijia");
            mijiaControlInitialized = true;
        } else {
            clearAppContentArea();
        }
        drawMijiaDevicePanel(dev, kind, mijiaDeviceIdx, cfg.device_count, mijiaUi, APP_CONTENT_X,
                             panel_y, net);
        snapshotMijiaRenderedPanel(net);
        return;
    }

    if (icon_dirty) {
        M5Cardputer.Display.fillRect(layout.icon_x, layout.icon_y, layout.icon_px, layout.icon_px,
                                     BLACK);
        drawMijiaPanelIcon(dev, kind, layout, mijiaUi);
    }
    if (right_dirty) {
        // 留出底边 2px，避免擦掉开启态边框
        const int clear_h = M5Cardputer.Display.height() - layout.right_top_y - 2;
        if (clear_h > 0) {
            M5Cardputer.Display.fillRect(layout.info_x, layout.right_top_y, layout.info_w, clear_h,
                                         BLACK);
        }
        drawMijiaPanelRightColumn(dev, kind, layout, mijiaUi, net);
    }
    // 局部刷新后补画边框（右栏清理可能碰到边缘）
    drawMijiaControlPowerBorder(mijiaUi.power_known && mijiaUi.power_on);
    snapshotMijiaRenderedPanel(net);
}

// 按当前模式重绘控制页、概览、编组或帮助页
static void redrawMijiaScreen() {
    if (mijiaHelpVisible) {
        invalidateMijiaControlSurface();
        if (mijiaGroupMode) {
            drawMijiaGroupHelpPage();
        } else if (mijiaOverviewGridMode) {
            drawMijiaGridHelpPage();
        } else {
            drawMijiaHelpPage();
        }
        return;
    }
    if (mijiaGroupMode) {
        invalidateMijiaControlSurface();
        // Header：Mijia + Group（次要色，对齐 Grid/List）
        beginAppScreenAccent("Mijia ", "Group", APP_COLOR_LABEL);
        drawMijiaGroupView();
        return;
    }
    if (mijiaOverviewMode) {
        invalidateMijiaControlSurface();
        // Header：Mijia + Grid/List（次要色，对齐红外 TV/AC）
        beginAppScreenAccent("Mijia ", mijiaOverviewGridMode ? "Grid" : "List", APP_COLOR_LABEL);
        int y = APP_CONTENT_Y;
        drawMijiaOverview(y);
        return;
    }
    applyMijiaControlRefresh(false);
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

static void formatBleCacheAgeStatus(const uint32_t updated_ms, char* out, const size_t out_size) {
    if (out == nullptr || out_size == 0) {
        return;
    }
    const uint32_t age_s = (millis() - updated_ms) / 1000UL;
    if (age_s < 3) {
        strncpy(out, "ok", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    if (age_s < 120) {
        snprintf(out, out_size, "%lus ago", static_cast<unsigned long>(age_s));
        return;
    }
    snprintf(out, out_size, "%lum ago", static_cast<unsigned long>(age_s / 60UL));
}

static void applyBleReadingToUi(MijiaUiState& ui, const MijiaBleReading& reading, const char* status) {
    if (reading.has_temp) {
        ui.temp_known = true;
        ui.temperature = reading.temperature;
    }
    if (reading.has_humidity) {
        ui.humidity_known = true;
        ui.humidity = reading.humidity;
    }
    if (reading.has_battery) {
        ui.battery_known = true;
        ui.battery = reading.battery;
    }
    if (reading.has_motion) {
        ui.motion_known = true;
        ui.motion = reading.motion;
    }
    if (reading.has_button) {
        ui.button_known = true;
        ui.button = reading.button;
    }
    ui.extra_known = ui.temp_known || ui.humidity_known || ui.battery_known || ui.motion_known ||
                     ui.button_known;
    ui.power_known = ui.extra_known;
    ui.power_on = ui.extra_known;
    if (status != nullptr) {
        strncpy(ui.status, status, sizeof(ui.status) - 1);
        ui.status[sizeof(ui.status) - 1] = '\0';
    }
}

static void storeBleCache(const int device_idx, const MijiaBleReading& reading) {
    if (device_idx < 0 || device_idx >= MIJIA_DEVICE_MAX || !reading.ok) {
        return;
    }
    MijiaBleCache& c = mijiaBleCache[device_idx];
    // 合并字段：温湿度/电量常分开发，保留上次有效值
    if (reading.has_temp) {
        c.has_temp = true;
        c.temperature = reading.temperature;
    }
    if (reading.has_humidity) {
        c.has_humidity = true;
        c.humidity = reading.humidity;
    }
    if (reading.has_battery) {
        c.has_battery = true;
        c.battery = reading.battery;
    }
    if (reading.has_motion) {
        c.has_motion = true;
        c.motion = reading.motion;
    }
    if (reading.has_button) {
        c.has_button = true;
        c.button = reading.button;
    }
    c.valid = c.has_temp || c.has_humidity || c.has_battery || c.has_motion || c.has_button;
    c.updated_ms = millis();
}

static bool applyBleCacheToUi(const int device_idx, MijiaUiState& ui, const bool with_age_status) {
    if (device_idx < 0 || device_idx >= MIJIA_DEVICE_MAX) {
        return false;
    }
    const MijiaBleCache& c = mijiaBleCache[device_idx];
    if (!c.valid) {
        return false;
    }
    if (c.has_temp) {
        ui.temp_known = true;
        ui.temperature = c.temperature;
    }
    if (c.has_humidity) {
        ui.humidity_known = true;
        ui.humidity = c.humidity;
    }
    if (c.has_battery) {
        ui.battery_known = true;
        ui.battery = c.battery;
    }
    if (c.has_motion) {
        ui.motion_known = true;
        ui.motion = c.motion;
    }
    if (c.has_button) {
        ui.button_known = true;
        ui.button = c.button;
    }
    ui.extra_known = ui.temp_known || ui.humidity_known || ui.battery_known || ui.motion_known ||
                     ui.button_known;
    ui.power_known = ui.extra_known;
    ui.power_on = ui.extra_known;
    if (with_age_status) {
        formatBleCacheAgeStatus(c.updated_ms, ui.status, sizeof(ui.status));
    }
    return true;
}

// 米家在前台时开启/重启后台多设备 BLE 监听
static void requestMijiaBleBackground() {
    if (!mijiaBleBgEnabled || mijiaBleFocusActive || mijiaBleScanIsRunning()) {
        return;
    }
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 0) {
        return;
    }
    bool any_ble = false;
    for (int i = 0; i < cfg.device_count; i++) {
        if (mijiaBleCanScan(cfg.devices[i])) {
            any_ble = true;
            break;
        }
    }
    if (!any_ble) {
        return;
    }
    mijiaBleWatchStart(cfg.devices, cfg.device_count, MIJIA_BLE_BG_SCAN_S);
}

// 取消进行中的查询/控制，切换设备或新操作时丢弃旧任务
static void cancelMijiaPendingJobs() {
    mijiaRefreshGen++;
    mijiaRefreshTimedOut = false;
    mijiaRefreshDeadlineMs = 0;
    mijiaOverviewRefreshQueueLen = 0;
    mijiaOverviewRefreshQueuePos = 0;
    mijiaOverviewPendingCellCount = 0;
    mijiaGroupJobQueueLen = 0;
    mijiaGroupJobQueuePos = 0;
    if (mijiaDeferredJob != nullptr) {
        delete mijiaDeferredJob;
        mijiaDeferredJob = nullptr;
    }
}

// 编组可执行电源控制的设备（跳过 BLE / 传感器）
static bool mijiaDeviceIsGroupActuator(const MijiaDevice& dev) {
    if (mijiaDeviceUsesBle(dev)) {
        return false;
    }
    const MijiaDevKind kind = mijiaClassifyModel(dev.model);
    return kind != MijiaDevKind::SENSOR_HT && kind != MijiaDevKind::BLE_EVENT;
}

// 编组内成员是否全是灯（才开放组亮度控制）
static bool mijiaGroupAllLights() {
    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr || group->member_count <= 0) {
        return false;
    }
    const AppConfig& cfg = getAppConfig();
    bool any = false;
    for (int i = 0; i < group->member_count; i++) {
        const int idx = group->member_indices[i];
        if (idx < 0 || idx >= cfg.device_count) {
            continue;
        }
        any = true;
        if (mijiaClassifyModel(cfg.devices[idx].model) != MijiaDevKind::LIGHT) {
            return false;
        }
    }
    return any;
}

// 编组底栏局部刷新（开关/亮度进度不整页闪）
static void requestMijiaGroupHintsRedraw() {
    mijiaNeedGroupHintsRedraw = true;
}

static const MijiaDeviceGroup* getCurrentMijiaGroup() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_group_count <= 0) {
        return nullptr;
    }
    if (mijiaGroupIdx < 0) {
        mijiaGroupIdx = 0;
    }
    if (mijiaGroupIdx >= cfg.device_group_count) {
        mijiaGroupIdx = cfg.device_group_count - 1;
    }
    return &cfg.device_groups[mijiaGroupIdx];
}

// 宫格单格刷新排队，主循环中执行避免在任务里画屏
static void queueMijiaGridCellRefresh(const int device_idx) {
    if (device_idx < 0 || device_idx >= MIJIA_DEVICE_MAX) {
        return;
    }
    for (int i = 0; i < mijiaOverviewPendingCellCount; i++) {
        if (mijiaOverviewPendingCells[i] == device_idx) {
            return;
        }
    }
    if (mijiaOverviewPendingCellCount < MIJIA_GRID_PAGE_SIZE) {
        mijiaOverviewPendingCells[mijiaOverviewPendingCellCount++] = device_idx;
    }
}

static void flushMijiaGridCellUpdates() {
    // Help 打开时不刷格子，避免盖住帮助页
    if (mijiaHelpVisible) {
        mijiaOverviewPendingCellCount = 0;
        return;
    }
    const bool grid_like =
        (mijiaOverviewMode && mijiaOverviewGridMode) || mijiaGroupMode;
    if (!grid_like) {
        mijiaOverviewPendingCellCount = 0;
        return;
    }
    for (int i = 0; i < mijiaOverviewPendingCellCount; i++) {
        refreshMijiaGridCell(mijiaOverviewPendingCells[i]);
    }
    mijiaOverviewPendingCellCount = 0;
}

// 宫格切换选中：取消未完成的操作
static void onMijiaGridDeviceChanged(const int old_idx, const int new_idx) {
    if (old_idx == new_idx) {
        return;
    }
    cancelMijiaPendingJobs();
    refreshMijiaGridSelection(old_idx, new_idx);
}

// 启动后台任务；若已有任务在跑则暂存，结束后链式执行
static bool scheduleMijiaJob(MijiaRefreshJob* job) {
    if (job == nullptr) {
        return false;
    }
    if (mijiaRefreshTaskRunning) {
        delete mijiaDeferredJob;
        mijiaDeferredJob = job;
        return true;
    }

    mijiaRefreshTaskRunning = true;
    if (xTaskCreate(mijiaJobTaskFn, "mijia_job", 8192, job, 1, nullptr) != pdPASS) {
        delete job;
        mijiaRefreshTaskRunning = false;
        return false;
    }
    return true;
}

// 后台任务结束后按需继续拉取最新设备
static void finishMijiaRefreshTask(const int job_gen) {
    mijiaRefreshTaskRunning = false;
    if (mijiaDeferredJob != nullptr) {
        MijiaRefreshJob* job = mijiaDeferredJob;
        mijiaDeferredJob = nullptr;
        scheduleMijiaJob(job);
        return;
    }
    // Help 打开时结束宫格查询链，不再排下一个
    if (mijiaHelpVisible) {
        return;
    }
    // 编组批量队列优先
    if (mijiaGroupMode && mijiaGroupJobQueuePos < mijiaGroupJobQueueLen) {
        scheduleMijiaGroupJob();
        return;
    }
    // 宫格概览队列优先于控制页单设备查询
    if (mijiaOverviewRefreshQueuePos < mijiaOverviewRefreshQueueLen) {
        scheduleMijiaOverviewRefreshJob();
        return;
    }
    if (!mijiaOverviewGridMode && !mijiaGroupMode && job_gen != mijiaRefreshGen &&
        !mijiaRefreshTimedOut) {
        scheduleMijiaRefresh();
    }
}

// 后台任务：查询状态或设置开关/亮度，结果仅在与当前 gen 一致时写回
static void mijiaJobTaskFn(void* arg) {
    MijiaRefreshJob* job = static_cast<MijiaRefreshJob*>(arg);
    const int job_gen = job->gen;
    const int job_idx = job->device_idx;
    const uint32_t job_deadline_ms = job->deadline_ms;
    const bool overview_cache = job->overview_cache;
    const MijiaJobType job_type = job->type;
    const bool power_on = job->power_on;
    const bool bright_absolute = job->bright_absolute;
    const int bright_value = job->bright_value;
    const MijiaDevice device = job->device;
    delete job;

    if (job_gen != mijiaRefreshGen || mijiaRefreshTimedOut) {
        finishMijiaRefreshTask(job_gen);
        vTaskDelete(nullptr);
        return;
    }

    if (mijiaDeviceUsesBle(device)) {
        // 禁止在后台任务初始化/扫描 BLE（会闪退）；交给主循环
        if (overview_cache && job_gen == mijiaRefreshGen) {
            strncpy(mijiaOverviewUi[job_idx].status, "ble",
                    sizeof(mijiaOverviewUi[job_idx].status));
            if (job_type == MijiaJobType::QUERY) {
                mijiaOverviewRefreshQueuePos++;
            }
            if (mijiaGroupMode && mijiaGroupJobQueueLen > 0) {
                mijiaGroupJobSkip++;
                mijiaGroupJobQueuePos++;
            }
            queueMijiaGridCellRefresh(job_idx);
        } else if (!overview_cache && job_gen == mijiaRefreshGen && job_idx == mijiaDeviceIdx) {
            strncpy(mijiaUi.status, mijiaBleCanScan(device) ? "press r" : "ble n/a",
                    sizeof(mijiaUi.status));
            mijiaRefreshDeadlineMs = 0;
            mijiaNeedRedraw = true;
        }
        finishMijiaRefreshTask(job_gen);
        vTaskDelete(nullptr);
        return;
    }

    if (!isMijiaConfigWifiConnected()) {
        if (overview_cache && job_gen == mijiaRefreshGen) {
            strncpy(mijiaOverviewUi[job_idx].status, "wifi fail", sizeof(mijiaOverviewUi[job_idx].status));
            if (job_type == MijiaJobType::QUERY) {
                mijiaOverviewRefreshQueuePos++;
            }
            if (mijiaGroupMode && mijiaGroupJobQueueLen > 0) {
                mijiaGroupJobFail++;
                mijiaGroupJobQueuePos++;
            }
            queueMijiaGridCellRefresh(job_idx);
        } else if (!overview_cache && job_gen == mijiaRefreshGen && job_idx == mijiaDeviceIdx) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
            mijiaRefreshDeadlineMs = 0;
            mijiaNeedRedraw = true;
        }
        finishMijiaRefreshTask(job_gen);
        vTaskDelete(nullptr);
        return;
    }

    MijiaUiState temp{};
    if (job_type == MijiaJobType::SET_POWER || job_type == MijiaJobType::SET_BRIGHT) {
        if (overview_cache) {
            temp = mijiaOverviewUi[job_idx];
        } else if (job_idx == mijiaDeviceIdx) {
            temp = mijiaUi;
        } else {
            finishMijiaRefreshTask(job_gen);
            vTaskDelete(nullptr);
            return;
        }
        if (job_type == MijiaJobType::SET_POWER) {
            mijiaSetDevicePower(&device, temp, power_on);
        } else if (bright_absolute) {
            mijiaSetBrightPercent(&device, temp, bright_value);
        } else {
            mijiaAdjustBright(&device, temp, bright_value);
        }
    } else {
        mijiaResetUiState(temp);
        if (overview_cache) {
            miioSetQueryTimeoutOverride(MIJIA_GRID_REFRESH_TIMEOUT_MS);
        }
        mijiaRefreshDevice(&device, temp);
        if (overview_cache) {
            miioClearQueryTimeoutOverride();
        }
    }

    const bool job_timed_out =
        job_deadline_ms != 0 && static_cast<int32_t>(millis() - job_deadline_ms) >= 0;
    if (mijiaRefreshTimedOut) {
        // UI 层已先判定超时，丢弃晚到结果
    } else if (job_timed_out && job_type == MijiaJobType::QUERY && job_gen == mijiaRefreshGen &&
               overview_cache) {
        strncpy(temp.status, "timeout", sizeof(temp.status));
        temp.power_known = false;
        mijiaOverviewUi[job_idx] = temp;
        mijiaOverviewRefreshQueuePos++;
        if (mijiaGroupMode && mijiaGroupJobQueueLen > 0) {
            mijiaGroupJobFail++;
            mijiaGroupJobQueuePos++;
        }
        queueMijiaGridCellRefresh(job_idx);
    } else if (job_timed_out && overview_cache && mijiaGroupMode && mijiaGroupJobQueueLen > 0 &&
               job_gen == mijiaRefreshGen) {
        // 编组 SET_POWER 超时：记失败并继续下一个
        strncpy(mijiaOverviewUi[job_idx].status, "timeout",
                sizeof(mijiaOverviewUi[job_idx].status));
        mijiaGroupJobFail++;
        mijiaGroupJobQueuePos++;
        queueMijiaGridCellRefresh(job_idx);
    } else if (job_timed_out && job_type == MijiaJobType::QUERY && job_gen == mijiaRefreshGen &&
               job_idx == mijiaDeviceIdx && !overview_cache) {
        mijiaRefreshTimedOut = true;
        mijiaRefreshGen++;
        mijiaRefreshDeadlineMs = 0;
        strncpy(mijiaUi.status, "timeout", sizeof(mijiaUi.status));
        mijiaNeedRedraw = true;
    } else if (!job_timed_out && job_gen == mijiaRefreshGen) {
        if (overview_cache) {
            mijiaOverviewUi[job_idx] = temp;
            if (job_type == MijiaJobType::QUERY) {
                mijiaOverviewRefreshQueuePos++;
            }
            if (mijiaGroupMode && mijiaGroupJobQueueLen > 0) {
                // 编组批量：统计成功/失败并推进队列
                bool ok = false;
                if (job_type == MijiaJobType::SET_POWER) {
                    ok = temp.power_known && temp.power_on == power_on;
                } else if (job_type == MijiaJobType::SET_BRIGHT) {
                    ok = (strcmp(temp.status, "ok") == 0);
                } else {
                    ok = temp.power_known;
                }
                if (ok) {
                    mijiaGroupJobOk++;
                } else {
                    mijiaGroupJobFail++;
                }
                mijiaGroupJobQueuePos++;
            }
            queueMijiaGridCellRefresh(job_idx);
            if (job_idx == mijiaDeviceIdx) {
                mijiaUi = temp;
            }
        } else if (job_idx == mijiaDeviceIdx) {
            mijiaUi = temp;
            mijiaRefreshDeadlineMs = 0;
            mijiaNetStatus[0] = '\0';
            mijiaNeedRedraw = true;
            if (job_idx >= 0 && job_idx < MIJIA_DEVICE_MAX) {
                mijiaOverviewUi[job_idx] = temp;
            }
        }
    }

    finishMijiaRefreshTask(job_gen);
    vTaskDelete(nullptr);
}

// 启动一次异步状态查询（若已有任务在跑则暂存，结束后链式执行）
static void scheduleMijiaRefresh() {
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
    job->type = MijiaJobType::QUERY;

    if (!scheduleMijiaJob(job)) {
        strncpy(mijiaUi.status, "task fail", sizeof(mijiaUi.status));
        mijiaUi.power_known = false;
        mijiaRefreshDeadlineMs = 0;
        mijiaNeedRedraw = true;
    }
}

// 请求刷新当前设备（不阻塞按键处理；WiFi 设备需联网，BLE 传感器走主循环扫描）
static void requestMijiaRefresh() {
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        return;
    }
    if (mijiaDeviceUsesBle(*dev)) {
        cancelMijiaPendingJobs();
        mijiaRefreshTimedOut = false;
        mijiaRefreshDeadlineMs = 0;
        if (mijiaBleCanScan(*dev)) {
            // 聚焦扫：打断后台，拉长窗口；脏包不早停
            mijiaBleScanAbort();
            mijiaBleFocusActive = true;
            strncpy(mijiaUi.status, "listening", sizeof(mijiaUi.status));
            mijiaBleScanPending = true;
        } else {
            strncpy(mijiaUi.status, "ble n/a", sizeof(mijiaUi.status));
            mijiaBleScanPending = false;
            mijiaBleFocusActive = false;
        }
        mijiaNeedRedraw = true;
        return;
    }
    if (mijiaWifiPhase != MijiaWifiPhase::READY && !isMijiaConfigWifiConnected()) {
        return;
    }
    mijiaWifiPhase = MijiaWifiPhase::READY;
    cancelMijiaPendingJobs();
    mijiaRefreshTimedOut = false;
    mijiaRefreshDeadlineMs = millis() + MIJIA_REFRESH_TIMEOUT_MS;
    const bool was_query = strcmp(mijiaUi.status, "query...") == 0;
    strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
    if (!was_query) {
        mijiaNeedRedraw = true;
    }
    scheduleMijiaRefresh();
}

// 状态查询超时，晚到结果会被 gen 丢弃
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
    mijiaNeedRedraw = true;
}

// 立即切换设备并异步拉状态（取消上一台未完成的操作）
static void switchMijiaDevice(const int delta, const int device_count) {
    cancelMijiaPendingJobs();
    // 聚焦扫打断；后台监听保留，继续给各设备灌缓存
    if (mijiaBleFocusActive) {
        mijiaBleScanPending = false;
        mijiaBleFocusActive = false;
        mijiaBleScanAbort();
    }
    mijiaDeviceIdx = (mijiaDeviceIdx + delta + device_count) % device_count;
    mijiaResetUiState(mijiaUi);
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev != nullptr && mijiaDeviceUsesBle(*dev)) {
        if (applyBleCacheToUi(mijiaDeviceIdx, mijiaUi, true)) {
            // 已有缓存：直接显示最近读数
        } else if (mijiaBleCanScan(*dev)) {
            strncpy(mijiaUi.status, "listening", sizeof(mijiaUi.status));
        } else {
            strncpy(mijiaUi.status, "ble n/a", sizeof(mijiaUi.status));
        }
        applyMijiaControlRefresh(true);
        mijiaRefreshTimedOut = false;
        mijiaRefreshDeadlineMs = 0;
        requestMijiaBleBackground();
        return;
    }
    strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
    applyMijiaControlRefresh(true);
    mijiaRefreshTimedOut = false;
    mijiaRefreshDeadlineMs = millis() + MIJIA_REFRESH_TIMEOUT_MS;
    scheduleMijiaRefresh();
}

// 开关提示音：开升调 / 关降调，受 sound.mijia_on_off 控制（喇叭 <800Hz 几乎听不见）
static void playMijiaPowerTone(const bool on) {
    if (!isMijiaOnOffSoundEnabled()) {
        return;
    }
    if (on) {
        playUiTone(1000, 35);
        delay(40);
        playUiTone(1400, 45);
    } else {
        playUiTone(1200, 35);
        delay(40);
        playUiTone(880, 50);
    }
}

// 异步设置当前设备开关
static void requestMijiaPower(const bool on) {
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        strncpy(mijiaUi.status, "no device", sizeof(mijiaUi.status));
        return;
    }
    if (!ensureConfigWifi()) {
        strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        applyMijiaControlRefresh(false);
        return;
    }

    playMijiaPowerTone(on);
    cancelMijiaPendingJobs();
    strncpy(mijiaUi.status, on ? "turn on..." : "turn off...", sizeof(mijiaUi.status));
    applyMijiaControlRefresh(false);

    auto* job = new MijiaRefreshJob{};
    job->gen = mijiaRefreshGen;
    job->device_idx = mijiaDeviceIdx;
    job->device = *dev;
    job->type = MijiaJobType::SET_POWER;
    job->power_on = on;
    if (!scheduleMijiaJob(job)) {
        strncpy(mijiaUi.status, "task fail", sizeof(mijiaUi.status));
        applyMijiaControlRefresh(false);
    }
}

// 设置当前设备开关
static void setMijiaPower(const bool on) {
    requestMijiaPower(on);
}

// 宫格概览：依次刷新当前页设备状态
static void scheduleMijiaOverviewRefreshJob() {
    if (mijiaOverviewRefreshQueuePos >= mijiaOverviewRefreshQueueLen) {
        return;
    }
    if (mijiaRefreshTaskRunning) {
        return;
    }

    const AppConfig& cfg = getAppConfig();
    const int idx = mijiaOverviewRefreshQueue[mijiaOverviewRefreshQueuePos];
    if (!cfg.loaded || idx < 0 || idx >= cfg.device_count) {
        mijiaOverviewRefreshQueuePos++;
        scheduleMijiaOverviewRefreshJob();
        return;
    }

    auto* job = new MijiaRefreshJob{};
    job->gen = mijiaRefreshGen;
    job->device_idx = idx;
    job->device = cfg.devices[idx];
    job->deadline_ms = millis() + MIJIA_GRID_REFRESH_TIMEOUT_MS;
    job->overview_cache = true;
    job->type = MijiaJobType::QUERY;

    mijiaRefreshTaskRunning = true;
    // BLE 扫描栈较大
    const uint32_t stack = 8192;
    if (xTaskCreate(mijiaJobTaskFn, "mijia_job", stack, job, 1, nullptr) != pdPASS) {
        delete job;
        mijiaRefreshTaskRunning = false;
        scheduleMijiaOverviewRefreshJob();
    }
}

static void requestMijiaOverviewPageRefresh() {
    if (!mijiaOverviewMode || !mijiaOverviewGridMode) {
        return;
    }
    // 本页若有 BLE 设备，无需 WiFi；否则需联网
    const AppConfig& cfg_chk = getAppConfig();
    bool page_has_ble = false;
    if (cfg_chk.loaded) {
        const int visible = mijiaOverviewGridMode ? MIJIA_GRID_PAGE_SIZE : MIJIA_LIST_VISIBLE_COUNT;
        for (int i = 0; i < visible; i++) {
            const int idx = mijiaOverviewScrollIdx + i;
            if (idx >= 0 && idx < cfg_chk.device_count &&
                mijiaDeviceUsesBle(cfg_chk.devices[idx])) {
                page_has_ble = true;
                break;
            }
        }
    }
    if (!page_has_ble && mijiaWifiPhase != MijiaWifiPhase::READY &&
        !isMijiaConfigWifiConnected()) {
        return;
    }

    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count == 0) {
        return;
    }

    cancelMijiaPendingJobs();
    mijiaOverviewRefreshQueueLen = 0;
    mijiaOverviewRefreshQueuePos = 0;
    for (int slot = 0; slot < MIJIA_GRID_PAGE_SIZE; slot++) {
        const int idx = mijiaOverviewScrollIdx + slot;
        if (idx >= cfg.device_count) {
            break;
        }
        // 已有状态的设备不重复查询
        if (mijiaOverviewUi[idx].power_known) {
            continue;
        }
        // BLE 设备：用缓存填宫格，不进 miIO 后台队列
        if (mijiaDeviceUsesBle(cfg.devices[idx])) {
            if (applyBleCacheToUi(idx, mijiaOverviewUi[idx], false)) {
                formatBleCacheAgeStatus(mijiaBleCache[idx].updated_ms, mijiaOverviewUi[idx].status,
                                        sizeof(mijiaOverviewUi[idx].status));
            } else {
                strncpy(mijiaOverviewUi[idx].status, "ble", sizeof(mijiaOverviewUi[idx].status));
            }
            queueMijiaGridCellRefresh(idx);
            continue;
        }
        mijiaOverviewRefreshQueue[mijiaOverviewRefreshQueueLen++] = idx;
        mijiaResetUiState(mijiaOverviewUi[idx]);
        strncpy(mijiaOverviewUi[idx].status, "query...", sizeof(mijiaOverviewUi[idx].status));
        queueMijiaGridCellRefresh(idx);
    }
    scheduleMijiaOverviewRefreshJob();
}

// 宫格异步开关
static void requestMijiaOverviewPower(const int device_idx, const bool on) {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || device_idx < 0 || device_idx >= cfg.device_count) {
        return;
    }
    if (!ensureConfigWifi()) {
        return;
    }

    playMijiaPowerTone(on);
    cancelMijiaPendingJobs();
    MijiaUiState& state = mijiaOverviewUi[device_idx];
    strncpy(state.status, on ? "turn on..." : "turn off...", sizeof(state.status));
    queueMijiaGridCellRefresh(device_idx);
    flushMijiaGridCellUpdates();

    auto* job = new MijiaRefreshJob{};
    job->gen = mijiaRefreshGen;
    job->device_idx = device_idx;
    job->device = cfg.devices[device_idx];
    job->type = MijiaJobType::SET_POWER;
    job->power_on = on;
    job->overview_cache = true;
    if (!scheduleMijiaJob(job)) {
        strncpy(state.status, "task fail", sizeof(state.status));
        queueMijiaGridCellRefresh(device_idx);
        flushMijiaGridCellUpdates();
    }
}

// 宫格选中设备后 i/o 快捷开关
static void setMijiaOverviewPower(const int device_idx, const bool on) {
    requestMijiaOverviewPower(device_idx, on);
}

// 宫格切换选中设备开关（状态未知时默认开启）
static void toggleMijiaOverviewPower(const int device_idx) {
    const MijiaUiState& state = mijiaOverviewUi[device_idx];
    const bool on = state.power_known ? !state.power_on : true;
    requestMijiaOverviewPower(device_idx, on);
}

// 更新编组底栏汇总文案
static void updateMijiaGroupStatusSummary() {
    const int done = mijiaGroupJobOk + mijiaGroupJobFail + mijiaGroupJobSkip;
    const int total = mijiaGroupJobQueueLen;
    if (total <= 0) {
        strncpy(mijiaGroupStatus, "", sizeof(mijiaGroupStatus));
        return;
    }
    if (done < total) {
        snprintf(mijiaGroupStatus, sizeof(mijiaGroupStatus), "%d/%d...", done, total);
        return;
    }
    if (mijiaGroupJobFail == 0 && mijiaGroupJobSkip == 0) {
        snprintf(mijiaGroupStatus, sizeof(mijiaGroupStatus), "%d/%d ok", mijiaGroupJobOk, total);
    } else {
        snprintf(mijiaGroupStatus, sizeof(mijiaGroupStatus), "%d/%d ok", mijiaGroupJobOk, total);
    }
}

// 编组：依次 QUERY / SET_POWER / SET_BRIGHT 下一个成员
static void scheduleMijiaGroupJob() {
    if (!mijiaGroupMode) {
        return;
    }
    if (mijiaGroupJobQueuePos >= mijiaGroupJobQueueLen) {
        updateMijiaGroupStatusSummary();
        requestMijiaGroupHintsRedraw();
        return;
    }
    if (mijiaRefreshTaskRunning) {
        return;
    }

    const AppConfig& cfg = getAppConfig();
    const int idx = mijiaGroupJobQueue[mijiaGroupJobQueuePos];
    if (!cfg.loaded || idx < 0 || idx >= cfg.device_count) {
        mijiaGroupJobSkip++;
        mijiaGroupJobQueuePos++;
        scheduleMijiaGroupJob();
        return;
    }

    updateMijiaGroupStatusSummary();
    requestMijiaGroupHintsRedraw();

    auto* job = new MijiaRefreshJob{};
    job->gen = mijiaRefreshGen;
    job->device_idx = idx;
    job->device = cfg.devices[idx];
    job->deadline_ms = millis() + MIJIA_GRID_REFRESH_TIMEOUT_MS;
    job->overview_cache = true;
    if (mijiaGroupJobIsSetBright) {
        job->type = MijiaJobType::SET_BRIGHT;
        job->bright_absolute = mijiaGroupJobBrightAbsolute;
        job->bright_value = mijiaGroupJobBrightValue;
    } else if (mijiaGroupJobIsSetPower) {
        job->type = MijiaJobType::SET_POWER;
        job->power_on = mijiaGroupJobPowerOn;
    } else {
        job->type = MijiaJobType::QUERY;
    }

    if (mijiaGroupJobIsSetBright) {
        strncpy(mijiaOverviewUi[idx].status, "bright...", sizeof(mijiaOverviewUi[idx].status));
        queueMijiaGridCellRefresh(idx);
    } else if (mijiaGroupJobIsSetPower) {
        strncpy(mijiaOverviewUi[idx].status,
                mijiaGroupJobPowerOn ? "turn on..." : "turn off...",
                sizeof(mijiaOverviewUi[idx].status));
        queueMijiaGridCellRefresh(idx);
    }

    mijiaRefreshTaskRunning = true;
    if (xTaskCreate(mijiaJobTaskFn, "mijia_job", 8192, job, 1, nullptr) != pdPASS) {
        delete job;
        mijiaRefreshTaskRunning = false;
        mijiaGroupJobFail++;
        mijiaGroupJobQueuePos++;
        scheduleMijiaGroupJob();
    }
}

// 编组：刷新当前组全部成员状态
static void requestMijiaGroupPageRefresh() {
    if (!mijiaGroupMode) {
        return;
    }
    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr || group->member_count <= 0) {
        strncpy(mijiaGroupStatus, "empty", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }

    const AppConfig& cfg = getAppConfig();
    bool need_wifi = false;
    for (int i = 0; i < group->member_count; i++) {
        const int idx = group->member_indices[i];
        if (idx >= 0 && idx < cfg.device_count && mijiaDeviceIsGroupActuator(cfg.devices[idx])) {
            need_wifi = true;
            break;
        }
    }
    if (need_wifi && mijiaWifiPhase != MijiaWifiPhase::READY && !isMijiaConfigWifiConnected()) {
        strncpy(mijiaGroupStatus, "wifi fail", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }

    cancelMijiaPendingJobs();
    mijiaGroupJobQueueLen = 0;
    mijiaGroupJobQueuePos = 0;
    mijiaGroupJobIsSetPower = false;
    mijiaGroupJobIsSetBright = false;
    mijiaGroupJobOk = 0;
    mijiaGroupJobFail = 0;
    mijiaGroupJobSkip = 0;

    for (int i = 0; i < group->member_count; i++) {
        const int idx = group->member_indices[i];
        if (idx < 0 || idx >= cfg.device_count) {
            continue;
        }
        if (!mijiaDeviceIsGroupActuator(cfg.devices[idx])) {
            // BLE/只读：用缓存或标记 skip，不进队列
            if (mijiaDeviceUsesBle(cfg.devices[idx])) {
                if (applyBleCacheToUi(idx, mijiaOverviewUi[idx], false)) {
                    formatBleCacheAgeStatus(mijiaBleCache[idx].updated_ms,
                                            mijiaOverviewUi[idx].status,
                                            sizeof(mijiaOverviewUi[idx].status));
                } else {
                    strncpy(mijiaOverviewUi[idx].status, "ble",
                            sizeof(mijiaOverviewUi[idx].status));
                }
            } else {
                strncpy(mijiaOverviewUi[idx].status, "skip",
                        sizeof(mijiaOverviewUi[idx].status));
            }
            queueMijiaGridCellRefresh(idx);
            continue;
        }
        if (mijiaOverviewUi[idx].power_known) {
            continue;
        }
        mijiaGroupJobQueue[mijiaGroupJobQueueLen++] = idx;
        mijiaResetUiState(mijiaOverviewUi[idx]);
        strncpy(mijiaOverviewUi[idx].status, "query...", sizeof(mijiaOverviewUi[idx].status));
        queueMijiaGridCellRefresh(idx);
    }

    if (mijiaGroupJobQueueLen == 0) {
        strncpy(mijiaGroupStatus, "ready", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }
    strncpy(mijiaGroupStatus, "query...", sizeof(mijiaGroupStatus));
    requestMijiaGroupHintsRedraw();
    scheduleMijiaGroupJob();
}

// 编组：对全部可控制成员顺序设电源
static void requestMijiaGroupPower(const bool on) {
    if (!mijiaGroupMode) {
        return;
    }
    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr || group->member_count <= 0) {
        strncpy(mijiaGroupStatus, "empty", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }
    if (!ensureConfigWifi()) {
        strncpy(mijiaGroupStatus, "wifi fail", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }

    const AppConfig& cfg = getAppConfig();
    playMijiaPowerTone(on);
    cancelMijiaPendingJobs();
    mijiaGroupJobQueueLen = 0;
    mijiaGroupJobQueuePos = 0;
    mijiaGroupJobIsSetPower = true;
    mijiaGroupJobIsSetBright = false;
    mijiaGroupJobPowerOn = on;
    mijiaGroupJobOk = 0;
    mijiaGroupJobFail = 0;
    mijiaGroupJobSkip = 0;

    for (int i = 0; i < group->member_count; i++) {
        const int idx = group->member_indices[i];
        if (idx < 0 || idx >= cfg.device_count) {
            continue;
        }
        if (!mijiaDeviceIsGroupActuator(cfg.devices[idx])) {
            strncpy(mijiaOverviewUi[idx].status, "skip", sizeof(mijiaOverviewUi[idx].status));
            queueMijiaGridCellRefresh(idx);
            continue;
        }
        mijiaGroupJobQueue[mijiaGroupJobQueueLen++] = idx;
        strncpy(mijiaOverviewUi[idx].status, on ? "turn on..." : "turn off...",
                sizeof(mijiaOverviewUi[idx].status));
        queueMijiaGridCellRefresh(idx);
    }
    flushMijiaGridCellUpdates();

    if (mijiaGroupJobQueueLen == 0) {
        strncpy(mijiaGroupStatus, "no actuator", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }
    updateMijiaGroupStatusSummary();
    requestMijiaGroupHintsRedraw();
    scheduleMijiaGroupJob();
}

// 编组：全灯组设亮度（absolute=true 为 0~9 百分比；false 为 -= 步进）
static void requestMijiaGroupBright(const bool absolute, const int value) {
    if (!mijiaGroupMode || !mijiaGroupAllLights()) {
        return;
    }
    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr || group->member_count <= 0) {
        strncpy(mijiaGroupStatus, "empty", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }
    if (!ensureConfigWifi()) {
        strncpy(mijiaGroupStatus, "wifi fail", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }

    const AppConfig& cfg = getAppConfig();
    cancelMijiaPendingJobs();
    mijiaGroupJobQueueLen = 0;
    mijiaGroupJobQueuePos = 0;
    mijiaGroupJobIsSetPower = false;
    mijiaGroupJobIsSetBright = true;
    mijiaGroupJobBrightAbsolute = absolute;
    mijiaGroupJobBrightValue = value;
    mijiaGroupJobOk = 0;
    mijiaGroupJobFail = 0;
    mijiaGroupJobSkip = 0;

    for (int i = 0; i < group->member_count; i++) {
        const int idx = group->member_indices[i];
        if (idx < 0 || idx >= cfg.device_count) {
            continue;
        }
        mijiaGroupJobQueue[mijiaGroupJobQueueLen++] = idx;
        strncpy(mijiaOverviewUi[idx].status, "bright...", sizeof(mijiaOverviewUi[idx].status));
        queueMijiaGridCellRefresh(idx);
    }
    flushMijiaGridCellUpdates();

    if (mijiaGroupJobQueueLen == 0) {
        strncpy(mijiaGroupStatus, "empty", sizeof(mijiaGroupStatus));
        requestMijiaGroupHintsRedraw();
        return;
    }
    updateMijiaGroupStatusSummary();
    requestMijiaGroupHintsRedraw();
    scheduleMijiaGroupJob();
}

// 编组切换：全开→全关，否则→全开（混合时一键全开）
static void toggleMijiaGroupPower() {
    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr) {
        return;
    }
    const AppConfig& cfg = getAppConfig();
    bool any_known = false;
    bool all_on = true;
    for (int i = 0; i < group->member_count; i++) {
        const int idx = group->member_indices[i];
        if (idx < 0 || idx >= cfg.device_count || !mijiaDeviceIsGroupActuator(cfg.devices[idx])) {
            continue;
        }
        const MijiaUiState& st = mijiaOverviewUi[idx];
        if (!st.power_known) {
            all_on = false;
            continue;
        }
        any_known = true;
        if (!st.power_on) {
            all_on = false;
        }
    }
    const bool turn_on = !(any_known && all_on);
    requestMijiaGroupPower(turn_on);
}

static void enterMijiaGroupMode() {
    if (mijiaOverviewMode) {
        exitMijiaOverview();
    }
    mijiaHelpVisible = false;
    mijiaGroupMode = true;
    const AppConfig& cfg = getAppConfig();
    if (mijiaGroupIdx < 0 || mijiaGroupIdx >= cfg.device_group_count) {
        mijiaGroupIdx = 0;
    }
    mijiaGroupScrollIdx = 0;
    mijiaGroupStatus[0] = '\0';
    requestMijiaGroupPageRefresh();
}

static void exitMijiaGroupMode() {
    if (!mijiaGroupMode) {
        return;
    }
    cancelMijiaPendingJobs();
    mijiaGroupMode = false;
    mijiaGroupJobQueueLen = 0;
    mijiaGroupJobQueuePos = 0;
    mijiaGroupStatus[0] = '\0';
}

// 切换编组（,/.）
static void switchMijiaGroup(const int delta) {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_group_count <= 0) {
        return;
    }
    cancelMijiaPendingJobs();
    mijiaGroupIdx = (mijiaGroupIdx + delta + cfg.device_group_count) % cfg.device_group_count;
    mijiaGroupScrollIdx = 0;
    mijiaGroupStatus[0] = '\0';
    requestMijiaGroupPageRefresh();
    redrawMijiaScreen();
}

// 编组内成员翻页
static void switchMijiaGroupPage(const int delta) {
    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr || group->member_count <= MIJIA_GRID_PAGE_SIZE) {
        return;
    }
    const int page_count = (group->member_count + MIJIA_GRID_PAGE_SIZE - 1) / MIJIA_GRID_PAGE_SIZE;
    int page = mijiaGroupScrollIdx / MIJIA_GRID_PAGE_SIZE;
    page = (page + delta + page_count) % page_count;
    mijiaGroupScrollIdx = page * MIJIA_GRID_PAGE_SIZE;
    redrawMijiaScreen();
}

// 概览每页设备数：列表 3 / 宫格 9
static int getMijiaOverviewVisibleCount() {
    return mijiaOverviewGridMode ? MIJIA_GRID_PAGE_SIZE : MIJIA_LIST_VISIBLE_COUNT;
}

static int getMijiaOverviewPageCount(const int device_count) {
    const int visible = getMijiaOverviewVisibleCount();
    if (device_count <= 0) {
        return 1;
    }
    return (device_count + visible - 1) / visible;
}

// 切换列表/宫格时同步滚动位置到当前选中设备所在页
static void syncMijiaOverviewScroll() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 0) {
        mijiaOverviewScrollIdx = 0;
        return;
    }
    const int visible = getMijiaOverviewVisibleCount();
    mijiaOverviewScrollIdx = (mijiaDeviceIdx / visible) * visible;
    if (mijiaOverviewGridMode) {
        const int page_count = getMijiaOverviewPageCount(cfg.device_count);
        int page = mijiaOverviewScrollIdx / visible;
        if (page < 0) {
            page = 0;
        }
        if (page >= page_count) {
            page = page_count - 1;
        }
        mijiaOverviewScrollIdx = page * visible;
        return;
    }
    const int max_scroll = cfg.device_count > visible ? cfg.device_count - visible : 0;
    if (mijiaOverviewScrollIdx > max_scroll) {
        mijiaOverviewScrollIdx = max_scroll;
    }
    if (mijiaOverviewScrollIdx < 0) {
        mijiaOverviewScrollIdx = 0;
    }
}

// 进入概览（列表或宫格）
static void enterMijiaOverview(const bool grid_mode) {
    mijiaOverviewMode = true;
    mijiaOverviewGridMode = grid_mode;
    mijiaOverviewEntryDeviceIdx = mijiaDeviceIdx;
    syncMijiaOverviewScroll();
    if (mijiaDeviceIdx >= 0 && mijiaDeviceIdx < MIJIA_DEVICE_MAX && mijiaUi.power_known) {
        mijiaOverviewUi[mijiaDeviceIdx] = mijiaUi;
    }
    if (grid_mode) {
        requestMijiaOverviewPageRefresh();
    }
}

// 退出概览回到控制页
static void exitMijiaOverview() {
    const bool device_changed = mijiaDeviceIdx != mijiaOverviewEntryDeviceIdx;
    mijiaOverviewMode = false;
    mijiaOverviewGridMode = false;
    if (device_changed) {
        mijiaResetUiState(mijiaUi);
        strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
        requestMijiaRefresh();
    }
}

// 宫格紧贴 header 顶边
static int getMijiaGridOriginY() {
    return APP_CONTENT_Y_NO_TAP_TO_HEADER;
}

// 概览列表每项高度：均分内容区（扣除底栏提示）
static int getMijiaOverviewItemHeight() {
    constexpr int hint_h = 12;
    constexpr int gap = 4;
    const int avail = M5Cardputer.Display.height() - APP_CONTENT_Y - hint_h;
    const int total_gap = gap * (MIJIA_LIST_VISIBLE_COUNT - 1);
    return (avail - total_gap) / MIJIA_LIST_VISIBLE_COUNT;
}

// 概览列表图标边长：随行高缩放，不超过设计上限
static int getMijiaOverviewIconPx(const int item_h) {
    const int fit = item_h - 2;
    return fit < MIJIA_LIST_ICON_PX ? fit : MIJIA_LIST_ICON_PX;
}

static int getMijiaOverviewPage(const int device_count) {
    const int visible = getMijiaOverviewVisibleCount();
    if (device_count <= 0) {
        return 0;
    }
    return mijiaOverviewScrollIdx / visible;
}

// 概览翻页：宫格按整页跳转，列表支持逐条滚动
static bool handleMijiaOverviewNav(const int delta) {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 1) {
        return false;
    }

    const int visible = getMijiaOverviewVisibleCount();
    const int page_count = getMijiaOverviewPageCount(cfg.device_count);

    if (mijiaOverviewGridMode) {
        if (page_count <= 1) {
            return false;
        }
        int page = mijiaOverviewScrollIdx / visible;
        page = (page + delta + page_count) % page_count;
        mijiaOverviewScrollIdx = page * visible;
        mijiaDeviceIdx = mijiaOverviewScrollIdx;
        if (mijiaDeviceIdx >= cfg.device_count) {
            mijiaDeviceIdx = cfg.device_count - 1;
        }
        if (mijiaOverviewGridMode) {
            requestMijiaOverviewPageRefresh();
        }
        redrawMijiaScreen();
        return true;
    }

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

// 宫格底栏提示高度与分隔线
static constexpr int MIJIA_GRID_HINT_H = 12;
static constexpr int MIJIA_GRID_BOTTOM_DIVIDER = 1;

// 垂直方向：; . 及 HID 上/下（Cardputer 方向键布局）
static int getOverviewVerticalDelta(const Keyboard_Class::KeysState& status) {
    int delta = 0;
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            delta = -1;
        } else if (hid == 0x51 || hid == 0x37) {
            delta = 1;
        }
    }
    for (const char c : status.word) {
        if (c == ';') {
            delta = -1;
        } else if (c == '.') {
            delta = 1;
        }
    }
    return delta;
}

// 水平方向：, / 及 HID 左/右（Cardputer 方向键布局）
static int getOverviewHorizontalDelta(const Keyboard_Class::KeysState& status) {
    int delta = 0;
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x50 || hid == 0x36) {
            delta = -1;
        } else if (hid == 0x4F || hid == 0x38) {
            delta = 1;
        }
    }
    for (const char c : status.word) {
        if (c == ',') {
            delta = -1;
        } else if (c == '/') {
            delta = 1;
        }
    }
    return delta;
}

// 列表上下选中，跨页时同步 scroll
static bool handleMijiaListSelectionNav(const Keyboard_Class::KeysState& status) {
    const int drow = getOverviewVerticalDelta(status);
    if (drow == 0) {
        return false;
    }

    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 1) {
        return false;
    }

    const int new_idx = mijiaDeviceIdx + drow;
    if (new_idx < 0 || new_idx >= cfg.device_count || new_idx == mijiaDeviceIdx) {
        return false;
    }

    const int visible = MIJIA_LIST_VISIBLE_COUNT;
    const int old_idx = mijiaDeviceIdx;
    const int old_scroll = mijiaOverviewScrollIdx;
    mijiaDeviceIdx = new_idx;
    mijiaOverviewScrollIdx = (new_idx / visible) * visible;

    if (mijiaOverviewScrollIdx != old_scroll) {
        redrawMijiaScreen();
    } else {
        refreshMijiaListSelection(old_idx, new_idx);
    }
    return true;
}

// [ ] 翻页：-1 上一页，0 无，1 下一页
static int getMijiaOverviewBracketDelta(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '[') {
            return -1;
        }
        if (c == ']') {
            return 1;
        }
    }
    return 0;
}

// 宫格方向键选中：上下左右 / ; , . /
static bool handleMijiaGridSelectionNav(const Keyboard_Class::KeysState& status) {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count <= 1) {
        return false;
    }

    int dcol = 0;
    int drow = 0;
    for (const uint8_t hid : status.hid_keys) {
        switch (hid) {
            case 0x50:
            case 0x36:
                dcol = -1;
                break;
            case 0x4F:
            case 0x38:
                dcol = 1;
                break;
            case 0x52:
            case 0x33:
                drow = -1;
                break;
            case 0x51:
            case 0x37:
                drow = 1;
                break;
            default:
                break;
        }
    }
    for (const char c : status.word) {
        if (c == ',') {
            dcol = -1;
        } else if (c == '/') {
            dcol = 1;
        } else if (c == ';') {
            drow = -1;
        } else if (c == '.') {
            drow = 1;
        }
    }
    if (dcol == 0 && drow == 0) {
        return false;
    }

    int new_idx = mijiaDeviceIdx;
    if (drow != 0) {
        new_idx += drow * MIJIA_GRID_COLS;
    } else {
        new_idx += dcol;
    }
    if (new_idx < 0 || new_idx >= cfg.device_count || new_idx == mijiaDeviceIdx) {
        return false;
    }

    const int old_idx = mijiaDeviceIdx;
    const int old_scroll = mijiaOverviewScrollIdx;
    mijiaDeviceIdx = new_idx;
    mijiaOverviewScrollIdx = (new_idx / MIJIA_GRID_PAGE_SIZE) * MIJIA_GRID_PAGE_SIZE;

    if (mijiaOverviewScrollIdx != old_scroll) {
        requestMijiaOverviewPageRefresh();
        redrawMijiaScreen();
    } else {
        onMijiaGridDeviceChanged(old_idx, new_idx);
    }
    return true;
}

// 估算底栏按键提示宽度
static int mijiaMeasureHintItem(const KeyHintItem& item) {
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(item.key)));
    const char str[2] = {letter, '\0'};
    M5Cardputer.Display.setTextSize(1);
    const int tw = M5Cardputer.Display.textWidth(str);
    constexpr int pad_x = 2;
    const int badge_w = tw + pad_x * 2 + 3;
    return badge_w + M5Cardputer.Display.textWidth(item.text);
}

// 宫格底栏：左侧 on/off/tog，右侧 h help（整行下移 1px）
static void drawMijiaGridBottomHints(const AppConfig& cfg) {
    const int tip_band_y = M5Cardputer.Display.height() - MIJIA_GRID_HINT_H;
    const int hint_y = tip_band_y + 1; // 宫格 tip 整行下移 1px
    const int text_y = hint_y + 1;     // 普通文字相对徽章再下 1px
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, tip_band_y, screen_w - APP_CONTENT_X * 2,
                                 MIJIA_GRID_HINT_H, BLACK);

    int cx = APP_CONTENT_X;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);

    if (cfg.loaded && cfg.device_count > 1) {
        const int page_count = getMijiaOverviewPageCount(cfg.device_count);
        if (page_count > 1) {
            char pos_buf[12];
            snprintf(pos_buf, sizeof(pos_buf), "p%d/%d",
                     getMijiaOverviewPage(cfg.device_count) + 1, page_count);
            M5Cardputer.Display.setCursor(cx, text_y);
            M5Cardputer.Display.print(pos_buf);
            cx += M5Cardputer.Display.textWidth(pos_buf) + 6;
        }
    }

    cx += drawKeyBadge(cx, hint_y, 'o', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("on ");
    cx += M5Cardputer.Display.textWidth("on ");
    cx += drawKeyBadge(cx, hint_y, 'i', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("off ");
    cx += M5Cardputer.Display.textWidth("off ");
    cx += drawKeyBadge(cx, hint_y, 't', 1);
    // BtnA 也可切换开关
    cx += drawTextBadge(cx, hint_y, "BtnA", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("tog");

    drawHelpHintRight("help", 1);
}

// 绘制概览底栏按键提示
static void drawMijiaOverviewHints(const AppConfig& cfg) {
    if (mijiaOverviewGridMode) {
        drawMijiaGridBottomHints(cfg);
        return;
    }

    const int hint_y = M5Cardputer.Display.height() - 12;
    const int text_y = hint_y + 1; // 普通文字下移 1px，徽章不动
    int cx = APP_CONTENT_X;

    if (cfg.loaded && cfg.device_count > 1) {
        const int page_count = getMijiaOverviewPageCount(cfg.device_count);
        if (page_count > 1) {
            char pos_buf[12];
            snprintf(pos_buf, sizeof(pos_buf), "p%d/%d",
                     getMijiaOverviewPage(cfg.device_count) + 1, page_count);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(cx, text_y);
            M5Cardputer.Display.print(pos_buf);
            cx += M5Cardputer.Display.textWidth(pos_buf) + 6;
        }
    }

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);

    cx += drawArrowBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("page ");
    cx += M5Cardputer.Display.textWidth("page ");

    cx += drawKeyBadge(cx, hint_y, 'l', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("back ");
    cx += M5Cardputer.Display.textWidth("back ");
    cx += drawKeyBadge(cx, hint_y, 'g', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("grid");
}

// 宫格单元：左图标，右侧第一行序号+状态，第二行设备名
static void drawMijiaOverviewGridCell(const MijiaDevice& entry, const int device_idx, const int x,
                                      const int y, const int cell_w, const int cell_h,
                                      const bool selected) {
    const MijiaDevKind kind = mijiaClassifyModel(entry.model);
    const uint16_t num_color = selected ? APP_COLOR_OK : APP_COLOR_HINT;
    const uint16_t name_color = selected ? APP_COLOR_OK : APP_COLOR_TEXT;
    const MijiaUiState& ui = mijiaOverviewUi[device_idx];
    const bool icon_active = ui.power_known && ui.power_on;

    constexpr int pad = 2;
    constexpr int text_gap = 4;
    constexpr int text_line_gap = 1;
    constexpr int num_status_gap = 4;
    int icon_px = cell_h - pad * 2;
    if (icon_px > DEVICE_ICON_LIST_PX) {
        icon_px = DEVICE_ICON_LIST_PX;
    }
    if (icon_px < MIJIA_ICON_BASE) {
        icon_px = MIJIA_ICON_BASE;
    }

    const int icon_x = x + pad;
    const int icon_y = y + (cell_h - icon_px) / 2;
    const float png_scale = static_cast<float>(icon_px) / DEVICE_ICON_LIST_PX;
    const int vector_scale = icon_px / MIJIA_ICON_BASE;
    drawMijiaDeviceIconForList(&entry, kind, icon_x, icon_y,
                               selected ? APP_COLOR_OK : APP_COLOR_HINT, icon_active,
                               vector_scale > 0 ? vector_scale : 1, png_scale);

    const int text_x = icon_x + icon_px + text_gap;
    const int text_w = cell_w - (text_x - x) - pad;
    const int num_h = infoLineHeight(1);
    const int name_h = infoLineHeight(1);
    const int line1_h = num_h > MIJIA_TAG_H ? num_h : MIJIA_TAG_H;
    const int text_block_h = line1_h + text_line_gap + name_h;
    const int text_y = y + (cell_h - text_block_h) / 2;

    M5Cardputer.Display.setTextSize(1);
    char num_buf[8];
    snprintf(num_buf, sizeof(num_buf), "%d", device_idx + 1);
    M5Cardputer.Display.setTextColor(num_color, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y + (line1_h - num_h) / 2);
    M5Cardputer.Display.print(num_buf);
    const int num_w = M5Cardputer.Display.textWidth(num_buf);

    const int status_x = text_x + num_w + num_status_gap;
    const int status_y = text_y + (line1_h - MIJIA_TAG_H) / 2;
    MijiaGridStatusTag status_tag{};
    mijiaFormatGridStatusTag(ui, status_tag);
    char status_buf[12];
    strncpy(status_buf, status_tag.text, sizeof(status_buf) - 1);
    status_buf[sizeof(status_buf) - 1] = '\0';
    const int status_max_w = cell_w - (status_x - x) - pad;
    while (status_buf[0] != '\0' && M5Cardputer.Display.textWidth(status_buf) > status_max_w) {
        status_buf[strlen(status_buf) - 1] = '\0';
    }
    drawMijiaStatusTag(status_x, status_y, status_buf, status_tag.active, status_tag.bg, 1);

    const char* raw_name = entry.name[0] != '\0' ? entry.name : "device";
    char name_buf[24];
    strncpy(name_buf, raw_name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    while (name_buf[0] != '\0' && M5Cardputer.Display.textWidth(name_buf) > text_w) {
        name_buf[strlen(name_buf) - 1] = '\0';
    }
    M5Cardputer.Display.setTextColor(name_color, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y + line1_h + text_line_gap);
    M5Cardputer.Display.print(name_buf);

    if (selected) {
        M5Cardputer.Display.drawRoundRect(x, y, cell_w, cell_h, 2, APP_COLOR_OK);
    }
}

struct MijiaGridLayout {
    int grid_y;
    int content_w;
    int avail_h;
    int cell_w;
    int cell_h;
    int gap;
};

// 计算宫格布局参数
static MijiaGridLayout getMijiaGridLayout() {
    MijiaGridLayout layout{};
    layout.gap = 2;
    layout.grid_y = getMijiaGridOriginY();
    layout.content_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    layout.avail_h =
        M5Cardputer.Display.height() - layout.grid_y - MIJIA_GRID_HINT_H - MIJIA_GRID_BOTTOM_DIVIDER;
    layout.cell_w = (layout.content_w - (MIJIA_GRID_COLS - 1) * layout.gap) / MIJIA_GRID_COLS;
    layout.cell_h = (layout.avail_h - (MIJIA_GRID_ROWS - 1) * layout.gap) / MIJIA_GRID_ROWS;
    return layout;
}

// 宫格末行与底栏提示之间的分隔线
static void drawMijiaGridBottomDivider(const MijiaGridLayout& layout) {
    const int y = layout.grid_y + layout.avail_h;
    M5Cardputer.Display.drawFastHLine(APP_CONTENT_X, y, layout.content_w, MIJIA_DIVIDER_COLOR);
}

// 绘制宫格分割线
static void drawMijiaGridDividers(const MijiaGridLayout& layout) {
    for (int col = 1; col < MIJIA_GRID_COLS; col++) {
        const int vx = APP_CONTENT_X + col * (layout.cell_w + layout.gap) - layout.gap / 2;
        M5Cardputer.Display.drawFastVLine(vx, layout.grid_y, layout.avail_h, MIJIA_DIVIDER_COLOR);
    }
    for (int row = 1; row < MIJIA_GRID_ROWS; row++) {
        const int hy = layout.grid_y + row * (layout.cell_h + layout.gap) - layout.gap / 2;
        M5Cardputer.Display.drawFastHLine(APP_CONTENT_X, hy, layout.content_w, MIJIA_DIVIDER_COLOR);
    }
}

// 设备索引转当前页宫格 slot，不在当前页返回 -1
static int mijiaGridSlotForIdx(const int device_idx) {
    if (mijiaGroupMode) {
        const MijiaDeviceGroup* group = getCurrentMijiaGroup();
        if (group == nullptr) {
            return -1;
        }
        for (int i = 0; i < MIJIA_GRID_PAGE_SIZE; i++) {
            const int mi = mijiaGroupScrollIdx + i;
            if (mi >= group->member_count) {
                break;
            }
            if (group->member_indices[mi] == device_idx) {
                return i;
            }
        }
        return -1;
    }
    if (device_idx < mijiaOverviewScrollIdx ||
        device_idx >= mijiaOverviewScrollIdx + MIJIA_GRID_PAGE_SIZE) {
        return -1;
    }
    return device_idx - mijiaOverviewScrollIdx;
}

// 局部刷新宫格单个格子（状态变更或选中切换）
static void refreshMijiaGridCell(const int device_idx) {
    if (mijiaHelpVisible) {
        return;
    }
    const int slot = mijiaGridSlotForIdx(device_idx);
    if (slot < 0) {
        return;
    }

    const MijiaGridLayout layout = getMijiaGridLayout();
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || device_idx < 0 || device_idx >= cfg.device_count) {
        return;
    }

    const int row = slot / MIJIA_GRID_COLS;
    const int col = slot % MIJIA_GRID_COLS;
    const int cx = APP_CONTENT_X + col * (layout.cell_w + layout.gap);
    const int cy = layout.grid_y + row * (layout.cell_h + layout.gap);
    M5Cardputer.Display.fillRect(cx, cy, layout.cell_w, layout.cell_h, BLACK);
    drawMijiaOverviewGridCell(cfg.devices[device_idx], device_idx, cx, cy, layout.cell_w, layout.cell_h,
                              mijiaGroupMode ? false : (device_idx == mijiaDeviceIdx));
    drawMijiaGridDividers(layout);
    drawMijiaGridBottomDivider(layout);
}

// 局部刷新宫格选中态（仅重绘变更的两个格子）
static void refreshMijiaGridSelection(const int old_idx, const int new_idx) {
    if (old_idx == new_idx) {
        refreshMijiaGridCell(old_idx);
        return;
    }

    const int old_slot = mijiaGridSlotForIdx(old_idx);
    const int new_slot = mijiaGridSlotForIdx(new_idx);
    if (old_slot < 0 && new_slot < 0) {
        redrawMijiaScreen();
        return;
    }

    refreshMijiaGridCell(old_idx);
    refreshMijiaGridCell(new_idx);
}

struct MijiaListLayout {
    int item_h;
    int item_gap;
    int line_w;
};

// 计算列表布局参数
static MijiaListLayout getMijiaListLayout() {
    MijiaListLayout layout{};
    layout.item_h = getMijiaOverviewItemHeight();
    layout.item_gap = 4;
    layout.line_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    return layout;
}

// 设备索引转当前页列表 slot，不在当前页返回 -1
static int mijiaListSlotForIdx(const int device_idx) {
    if (device_idx < mijiaOverviewScrollIdx ||
        device_idx >= mijiaOverviewScrollIdx + MIJIA_LIST_VISIBLE_COUNT) {
        return -1;
    }
    return device_idx - mijiaOverviewScrollIdx;
}

// 绘制列表项间分隔线
static void drawMijiaListDividers(const MijiaListLayout& layout, const int device_count) {
    int item_y = APP_CONTENT_Y;
    for (int i = 0; i < MIJIA_LIST_VISIBLE_COUNT - 1; i++) {
        const int idx = mijiaOverviewScrollIdx + i;
        if (idx + 1 >= device_count) {
            break;
        }
        const int line_y = item_y + layout.item_h + layout.item_gap / 2;
        M5Cardputer.Display.drawFastHLine(APP_CONTENT_X, line_y, layout.line_w, MIJIA_DIVIDER_COLOR);
        item_y += layout.item_h + layout.item_gap;
    }
}

// 宫格概览：3x3 整页分页，末页不足 9 台留空
static void drawMijiaOverviewGrid() {
    const AppConfig& cfg = getAppConfig();
    syncMijiaOverviewScroll();

    const MijiaGridLayout layout = getMijiaGridLayout();

    drawMijiaGridDividers(layout);
    drawMijiaGridBottomDivider(layout);

    for (int row = 0; row < MIJIA_GRID_ROWS; row++) {
        for (int col = 0; col < MIJIA_GRID_COLS; col++) {
            const int slot = row * MIJIA_GRID_COLS + col;
            const int idx = mijiaOverviewScrollIdx + slot;
            const int cx = APP_CONTENT_X + col * (layout.cell_w + layout.gap);
            const int cy = layout.grid_y + row * (layout.cell_h + layout.gap);
            if (idx < cfg.device_count) {
                drawMijiaOverviewGridCell(cfg.devices[idx], idx, cx, cy, layout.cell_w, layout.cell_h,
                                          idx == mijiaDeviceIdx);
            }
        }
    }

    drawMijiaOverviewHints(cfg);
}

// 编组底栏：组名/状态 + t toggle + h help（整行下移 1px；其余键只在 Help）
static void drawMijiaGroupBottomHints(const AppConfig& cfg) {
    const int tip_band_y = M5Cardputer.Display.height() - MIJIA_GRID_HINT_H;
    const int hint_y = tip_band_y + 1; // 宫格 tip 整行下移 1px
    const int text_y = hint_y + 1;     // 普通文字相对徽章再下 1px
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, tip_band_y, screen_w - APP_CONTENT_X * 2,
                                 MIJIA_GRID_HINT_H, BLACK);

    int cx = APP_CONTENT_X;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);

    // 预留 t toggle + h help
    constexpr int keys_reserve = 78;

    if (cfg.loaded && cfg.device_group_count > 0) {
        const MijiaDeviceGroup* group = getCurrentMijiaGroup();
        char pos_buf[28];
        const char* gname = (group != nullptr && group->name[0] != '\0') ? group->name : "group";
        snprintf(pos_buf, sizeof(pos_buf), "%d/%d %s", mijiaGroupIdx + 1, cfg.device_group_count,
                 gname);
        // 截断避免挤掉按键提示
        while (pos_buf[0] != '\0' && M5Cardputer.Display.textWidth(pos_buf) > 100) {
            pos_buf[strlen(pos_buf) - 1] = '\0';
        }
        M5Cardputer.Display.setCursor(cx, text_y);
        M5Cardputer.Display.print(pos_buf);
        cx += M5Cardputer.Display.textWidth(pos_buf) + 4;
    }

    if (mijiaGroupStatus[0] != '\0') {
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.setCursor(cx, text_y);
        // 状态过长则跳过，优先显示按键
        if (cx + M5Cardputer.Display.textWidth(mijiaGroupStatus) + keys_reserve <
            screen_w - APP_CONTENT_X) {
            M5Cardputer.Display.print(mijiaGroupStatus);
            cx += M5Cardputer.Display.textWidth(mijiaGroupStatus) + 4;
        }
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    }

    cx += drawKeyBadge(cx, hint_y, 't', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print("toggle");

    drawHelpHintRight("help", 1);
}

// 编组页：宫格展示当前组员
static void drawMijiaGroupView() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_group_count <= 0) {
        int y = APP_CONTENT_Y;
        drawInfoLine(APP_CONTENT_X, y, "hint", "no groups");
        y += infoLineHeight(1) + 4;
        drawInfoLine(APP_CONTENT_X, y, "hint", "add in web /advanced");
        return;
    }

    const MijiaDeviceGroup* group = getCurrentMijiaGroup();
    if (group == nullptr || group->member_count <= 0) {
        int y = APP_CONTENT_Y;
        drawInfoLine(APP_CONTENT_X, y, "hint", "empty group");
        drawMijiaGroupBottomHints(cfg);
        return;
    }

    if (mijiaGroupScrollIdx >= group->member_count) {
        mijiaGroupScrollIdx = 0;
    }

    const MijiaGridLayout layout = getMijiaGridLayout();
    drawMijiaGridDividers(layout);
    drawMijiaGridBottomDivider(layout);

    for (int row = 0; row < MIJIA_GRID_ROWS; row++) {
        for (int col = 0; col < MIJIA_GRID_COLS; col++) {
            const int slot = row * MIJIA_GRID_COLS + col;
            const int mi = mijiaGroupScrollIdx + slot;
            if (mi >= group->member_count) {
                continue;
            }
            const int idx = group->member_indices[mi];
            if (idx < 0 || idx >= cfg.device_count) {
                continue;
            }
            const int cx = APP_CONTENT_X + col * (layout.cell_w + layout.gap);
            const int cy = layout.grid_y + row * (layout.cell_h + layout.gap);
            drawMijiaOverviewGridCell(cfg.devices[idx], idx, cx, cy, layout.cell_w, layout.cell_h,
                                      false);
        }
    }

    drawMijiaGroupBottomHints(cfg);
}

// 绘制单项：序号 + 左图标（缩放）+ 右名称/型号
static void drawMijiaOverviewItem(const MijiaDevice& entry, const int device_idx, const int x,
                                  const int y, const int item_h, const bool selected) {
    const MijiaDevKind kind = mijiaClassifyModel(entry.model);
    const uint16_t name_color = selected ? APP_COLOR_OK : APP_COLOR_VALUE;
    const int num_h = infoLineHeight(2);
    const int num_y = y + (item_h - num_h) / 2;

    M5Cardputer.Display.setTextSize(2);
    char num_buf[8];
    snprintf(num_buf, sizeof(num_buf), "%d", device_idx + 1);
    M5Cardputer.Display.setTextColor(name_color, BLACK);
    M5Cardputer.Display.setCursor(x, num_y);
    M5Cardputer.Display.print(num_buf);
    const int content_x = x + M5Cardputer.Display.textWidth(num_buf) + MIJIA_LIST_NUM_MARGIN_R;

    const int icon_px = DEVICE_ICON_LIST_PX;
    const int vector_scale = icon_px / MIJIA_ICON_BASE;
    const int icon_y = y + (item_h - icon_px) / 2;
    drawMijiaDeviceIconForList(&entry, kind, content_x, icon_y, selected ? APP_COLOR_OK : APP_COLOR_HINT,
                               false, vector_scale > 0 ? vector_scale : 1, 1.0f);

    const int text_x = content_x + icon_px + 6;
    const int text_block_h = INFO_LINE_H * 2;
    const int text_y = y + (item_h - text_block_h) / 2;

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(name_color, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y);
    if (entry.name[0] != '\0') {
        M5Cardputer.Display.print(entry.name);
    } else {
        M5Cardputer.Display.print("device");
    }

    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(text_x, text_y + INFO_LINE_H);
    if (entry.model[0] != '\0') {
        M5Cardputer.Display.print(entry.model);
    } else {
        M5Cardputer.Display.print("-");
    }
}

// 局部刷新列表选中态（仅重绘变更的两项）
static void refreshMijiaListSelection(const int old_idx, const int new_idx) {
    if (old_idx == new_idx) {
        return;
    }

    const int old_slot = mijiaListSlotForIdx(old_idx);
    const int new_slot = mijiaListSlotForIdx(new_idx);
    if (old_slot < 0 && new_slot < 0) {
        redrawMijiaScreen();
        return;
    }

    const MijiaListLayout layout = getMijiaListLayout();
    const AppConfig& cfg = getAppConfig();

    const auto paint_slot = [&](const int slot) {
        if (slot < 0 || slot >= MIJIA_LIST_VISIBLE_COUNT) {
            return;
        }
        const int idx = mijiaOverviewScrollIdx + slot;
        if (idx >= cfg.device_count) {
            return;
        }
        const int item_y = APP_CONTENT_Y + slot * (layout.item_h + layout.item_gap);
        M5Cardputer.Display.fillRect(APP_CONTENT_X, item_y, layout.line_w, layout.item_h, BLACK);
        drawMijiaOverviewItem(cfg.devices[idx], idx, APP_CONTENT_X, item_y, layout.item_h,
                              idx == mijiaDeviceIdx);
    };

    paint_slot(old_slot);
    paint_slot(new_slot);
    drawMijiaListDividers(layout, cfg.device_count);
}

static void drawMijiaOverview(int& y) {
    const AppConfig& cfg = getAppConfig();

    if (!cfg.loaded || cfg.device_count == 0) {
        drawInfoLine(APP_CONTENT_X, y, "total", "0");
        drawInfoLine(APP_CONTENT_X, y, "hint", "press u web");
        static const KeyHintItem empty_items[] = {
            {'u', "web"},
            {'l', "back"},
        };
        drawKeyHintsRow(APP_CONTENT_X, M5Cardputer.Display.height() - 12, empty_items, 2, 1,
                        APP_COLOR_HINT);
        return;
    }

    if (mijiaOverviewGridMode) {
        drawMijiaOverviewGrid();
        y = M5Cardputer.Display.height() - 12;
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
    constexpr int item_gap = 4;
    const MijiaListLayout layout = getMijiaListLayout();
    int item_y = APP_CONTENT_Y;
    for (int i = 0; i < visible; i++) {
        const int idx = mijiaOverviewScrollIdx + i;
        if (idx >= cfg.device_count) {
            break;
        }
        drawMijiaOverviewItem(cfg.devices[idx], idx, APP_CONTENT_X, item_y, item_h,
                              idx == mijiaDeviceIdx);
        item_y += item_h + item_gap;
    }
    drawMijiaListDividers(layout, cfg.device_count);
    y = item_y;
    drawMijiaOverviewHints(cfg);
}

bool handleMijiaOverviewPageNav(const Keyboard_Class::KeysState& status) {
    if (mijiaGroupMode) {
        if (mijiaHelpVisible) {
            return false;
        }
        // 回车退出编组
        if (status.enter) {
            exitMijiaGroupMode();
            redrawMijiaScreen();
            return true;
        }
        // 左右翻组成员页；上下切组
        const int bracket = getMijiaOverviewBracketDelta(status);
        if (bracket != 0) {
            switchMijiaGroupPage(bracket);
            return true;
        }
        const int hdelta = getOverviewHorizontalDelta(status);
        if (hdelta != 0) {
            switchMijiaGroupPage(hdelta);
            return true;
        }
        const int vdelta = getMenuNavDelta(status);
        if (vdelta != 0) {
            // 垂直：切编组
            switchMijiaGroup(vdelta);
            return true;
        }
        return false;
    }
    if (!mijiaOverviewMode || mijiaHelpVisible) {
        return false;
    }
    // 回车：确认当前选中设备，回到控制页
    if (status.enter) {
        exitMijiaOverview();
        redrawMijiaScreen();
        return true;
    }
    if (mijiaOverviewGridMode) {
        const int bracket = getMijiaOverviewBracketDelta(status);
        if (bracket != 0) {
            return handleMijiaOverviewNav(bracket);
        }
        return handleMijiaGridSelectionNav(status);
    }
    const int hdelta = getOverviewHorizontalDelta(status);
    if (hdelta != 0) {
        return handleMijiaOverviewNav(hdelta);
    }
    return handleMijiaListSelectionNav(status);
}

// 控制页切换设备
bool handleMijiaDeviceNav(const Keyboard_Class::KeysState& status) {
    if (mijiaOverviewMode || mijiaGroupMode || mijiaHelpVisible) {
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

// Help 分栏标题（蓝底黑字，宫格/编组 Help 用）
static int drawMijiaHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// Help 按键说明；徽章后恢复说明文字颜色
static int drawMijiaHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawMijiaHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawMijiaHelpArrows(const int x, const int y, const char* text) {
    const int cx = x + drawArrowBadge(x, y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// Help 功能说明
static int drawMijiaHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// 单设备 Help：估算按键徽章 + 文案占用宽度
static int mijiaMeasureKeyHintItem(const KeyHintItem& item, const int text_size) {
    const int size = (text_size == 2) ? 2 : 1;
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(item.key)));
    const char str[2] = {letter, '\0'};
    M5Cardputer.Display.setTextSize(size);
    const int badge_w = M5Cardputer.Display.textWidth(str) + 4 + 3;
    M5Cardputer.Display.setTextSize(text_size);
    return badge_w + M5Cardputer.Display.textWidth(item.text);
}

// 单设备 Help：绘制单个按键提示，返回占用宽度
static int mijiaDrawKeyHintItem(const int x, const int y, const KeyHintItem& item,
                                const int text_size, const uint16_t color) {
    int cx = x + drawKeyBadge(x, y, item.key, text_size);
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(item.text);
    return cx + M5Cardputer.Display.textWidth(item.text) - x;
}

// 单设备三列 Help 列标题（与宫格/编组一致：蓝底黑字）
static int drawMijiaHelpColumnHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// 三列 help 内部换行，返回下一行 y
static int drawKeyHintsWrappedInColumn(const int x, int y, const int w,
                                       const KeyHintItem* items, const int item_count,
                                       const uint16_t color) {
    if (items == nullptr || item_count <= 0) {
        return y;
    }

    constexpr int text_size = 1;
    constexpr int line_h = INFO_LINE_H;
    constexpr int gap = 1;
    int cx = x;
    M5Cardputer.Display.setTextSize(text_size);
    const int space_w = M5Cardputer.Display.textWidth(" ");

    for (int i = 0; i < item_count; i++) {
        const int item_w = mijiaMeasureKeyHintItem(items[i], text_size);
        if (cx > x && cx + item_w > x + w) {
            y += line_h + gap;
            cx = x;
        }
        cx += mijiaDrawKeyHintItem(cx, y, items[i], text_size, color);
        if (i != item_count - 1) {
            M5Cardputer.Display.setCursor(cx, y);
            M5Cardputer.Display.print(" ");
            cx += space_w;
        }
    }
    return y + line_h + gap;
}

static int drawMijiaSwitchHelpInColumn(const int x, const int y) {
    int cx = x + drawArrowBadge(x, y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("switch");
    return y + INFO_LINE_H + 1;
}

// 灯：亮度 + 色温 + 色相调节说明
static int drawMijiaLightHelpInColumn(const MijiaDevice* dev, const int x, int y, const int w) {
    static const KeyHintItem bright_items[] = {{'-', "bright-"}, {'=', "bright+"}};
    static const KeyHintItem percent_items[] = {{'1', "10%"}, {'9', "90%"}, {'0', "100%"}};
    static const KeyHintItem ct_items[] = {{'[', "ct-"}, {']', "ct+"}};
    static const KeyHintItem hue_items[] = {{'j', "hue-"}, {'k', "hue+"}};
    y = drawKeyHintsWrappedInColumn(x, y, w, bright_items, 2, APP_COLOR_HINT);
    y = drawKeyHintsWrappedInColumn(x, y, w, percent_items, 3, APP_COLOR_HINT);
    if (dev != nullptr && mijiaLightSupportsCt(dev->model)) {
        y = drawKeyHintsWrappedInColumn(x, y, w, ct_items, 2, APP_COLOR_HINT);
    }
    if (dev != nullptr && mijiaLightSupportsHue(dev->model)) {
        y = drawKeyHintsWrappedInColumn(x, y, w, hue_items, 2, APP_COLOR_HINT);
    }
    return y;
}

// 单设备帮助：common / navigation / special 三列
static void drawMijiaHelpContent(const MijiaDevice* dev) {
    const MijiaDevKind kind =
        dev != nullptr ? mijiaClassifyModel(dev->model) : MijiaDevKind::GENERIC;
    const int screen_w = M5Cardputer.Display.width();
    constexpr int col_count = 3;
    constexpr int col_gap = 2;
    const int col_w = (screen_w - col_gap * (col_count - 1)) / col_count;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;

    static const KeyHintItem common_items[] = {
        {'o', "on"},
        {'i', "off"},
        {'t', "tog/BtnA"},
        {'r', "refresh"},
        {'h', "help"},
    };
    static const KeyHintItem nav_items[] = {{'l', "list"}, {'g', "grid"}, {'d', "groups"}};

    const int common_x = 0;
    const int nav_x = col_w + col_gap;
    const int special_x = (col_w + col_gap) * 2;
    const int content_h = M5Cardputer.Display.height() - col_y;
    // 列分隔线沿用 header 底部分隔线颜色
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y, content_h, DARKGREY);
    M5Cardputer.Display.drawFastVLine(special_x - col_gap / 2, col_y, content_h, DARKGREY);

    int y = drawMijiaHelpColumnHeader(common_x, col_y, col_w, "common");
    drawKeyHintsWrappedInColumn(common_x + 2, y, col_w - 4, common_items, 5, APP_COLOR_HINT);

    y = drawMijiaHelpColumnHeader(nav_x, col_y, col_w, "navigation");
    y = drawKeyHintsWrappedInColumn(nav_x + 2, y, col_w - 4, nav_items, 3, APP_COLOR_HINT);
    drawMijiaSwitchHelpInColumn(nav_x + 2, y);

    y = drawMijiaHelpColumnHeader(special_x, col_y, screen_w - special_x, "special");
    const int special_content_x = special_x + 2;
    const int special_content_w = screen_w - special_x - 4;
    switch (kind) {
        case MijiaDevKind::LIGHT:
            drawMijiaLightHelpInColumn(dev, special_content_x, y, special_content_w);
            break;
        case MijiaDevKind::FAN_P5: {
            static const KeyHintItem fan_items[] = {
                {'-', "spd-"},
                {'=', "spd+"},
                {'w', "roll"},
                {'m', "mode"},
                {'a', "angle"},
            };
            static const KeyHintItem percent_items[] = {{'1', "10%"}, {'9', "90%"}, {'0', "100%"}};
            y = drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, fan_items, 5,
                                            APP_COLOR_HINT);
            drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, percent_items, 3,
                                        APP_COLOR_HINT);
            break;
        }
        case MijiaDevKind::FAN_GENERIC: {
            static const KeyHintItem speed_items[] = {
                {'1', "lv1"},
                {'2', "lv2"},
                {'3', "lv3"},
                {'4', "lv4"},
            };
            drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, speed_items, 4,
                                        APP_COLOR_HINT);
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
            y = drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, mode_items, 5,
                                            APP_COLOR_HINT);
            drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, fan_items, 2,
                                        APP_COLOR_HINT);
            break;
        }
        case MijiaDevKind::AIR_FRYER: {
            static const KeyHintItem fryer_items[] = {
                {'-', "temp-"},
                {'=', "temp+"},
                {'[', "time-"},
                {']', "time+"},
            };
            drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, fryer_items, 4,
                                        APP_COLOR_HINT);
            break;
        }
        case MijiaDevKind::SENSOR_HT:
        case MijiaDevKind::BLE_EVENT: {
            static const KeyHintItem ble_items[] = {
                {'r', "scan ble"},
            };
            drawKeyHintsWrappedInColumn(special_content_x, y, special_content_w, ble_items, 1,
                                        APP_COLOR_HINT);
            break;
        }
        default:
            break;
    }
}

// 单设备详情帮助：三列 common / navigation / special
static void drawMijiaHelpPage() {
    beginAppScreen("Help");
    drawMijiaHelpContent(getCurrentMijiaDevice());
    drawHelpHintRight("close help");
    updateAppHeaderStatus();
}

// 宫格概览帮助
static void drawMijiaGridHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawMijiaHelpColHeader(0, col_y, col_w, "keymap");
    y = drawMijiaHelpArrows(2, y, "select");
    y = drawMijiaHelpBadge(2, y, "[ ]", "page");
    y = drawMijiaHelpBadge(2, y, "1-9", "pick cell");
    y = drawMijiaHelpBadge(2, y, "o/i/t", "power");
    y = drawMijiaHelpBadge(2, y, "BtnA", "toggle");
    y = drawMijiaHelpBadge(2, y, "l/g/d", "views");

    y = drawMijiaHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawMijiaHelpText(manual_x + 2, y, "device grid view");
    y = drawMijiaHelpText(manual_x + 2, y, "quick on / off");
    y = drawMijiaHelpText(manual_x + 2, y, "pick cell then act");
    y = drawMijiaHelpText(manual_x + 2, y, "g back to detail");
    y = drawMijiaHelpText(manual_x + 2, y, "l list  d groups");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

// 编组帮助
static void drawMijiaGroupHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawMijiaHelpColHeader(0, col_y, col_w, "keymap");
    y = drawMijiaHelpBadge(2, y, ", .", "group");
    y = drawMijiaHelpBadge(2, y, "o/i/t", "power");
    if (mijiaGroupAllLights()) {
        y = drawMijiaHelpBadge(2, y, "-/= 1/0", "bright");
    }
    y = drawMijiaHelpKey(2, y, 'r', "refresh");
    y = drawMijiaHelpBadge(2, y, "[ ]", "page");
    y = drawMijiaHelpBadge(2, y, "l/g/d", "views");

    y = drawMijiaHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawMijiaHelpText(manual_x + 2, y, "control a group");
    y = drawMijiaHelpText(manual_x + 2, y, "members act together");
    y = drawMijiaHelpText(manual_x + 2, y, "lights can dim");
    y = drawMijiaHelpText(manual_x + 2, y, "d back / groups");
    y = drawMijiaHelpText(manual_x + 2, y, "config via web U");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

void drawMijiaApp() {
    if (mijiaGroupMode) {
        beginAppScreenAccent("Mijia ", "Group", APP_COLOR_LABEL);
        drawMijiaGroupView();
        return;
    }
    if (mijiaOverviewMode) {
        beginAppScreenAccent("Mijia ", mijiaOverviewGridMode ? "Grid" : "List", APP_COLOR_LABEL);
        M5Cardputer.Display.setTextSize(1);
        int y = APP_CONTENT_Y;
        drawMijiaOverview(y);
        return;
    }
    applyMijiaControlRefresh(true);
}

void enterMijiaApp() {
    mijiaDeviceIdx = 0;
    mijiaOverviewMode = false;
    mijiaOverviewGridMode = false;
    mijiaGroupMode = false;
    mijiaHelpVisible = false;
    mijiaOverviewScrollIdx = 0;
    mijiaGroupIdx = 0;
    mijiaGroupScrollIdx = 0;
    mijiaGroupStatus[0] = '\0';
    mijiaOverviewRefreshQueueLen = 0;
    mijiaOverviewRefreshQueuePos = 0;
    mijiaGroupJobQueueLen = 0;
    mijiaGroupJobQueuePos = 0;
    mijiaBleScanPending = false;
    mijiaBleFocusActive = false;
    mijiaBleBgEnabled = true;
    if (mijiaDeferredJob != nullptr) {
        delete mijiaDeferredJob;
        mijiaDeferredJob = nullptr;
    }
    for (int i = 0; i < MIJIA_DEVICE_MAX; i++) {
        mijiaResetUiState(mijiaOverviewUi[i]);
    }
    mijiaWifiPhase = MijiaWifiPhase::IDLE;
    mijiaWifiDeadlineMs = 0;
    mijiaRefreshDeadlineMs = 0;
    mijiaNetStatus[0] = '\0';
    invalidateMijiaControlSurface();
    mijiaResetUiState(mijiaUi);
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev != nullptr && mijiaDeviceUsesBle(*dev)) {
        if (!applyBleCacheToUi(mijiaDeviceIdx, mijiaUi, true)) {
            strncpy(mijiaUi.status, mijiaBleCanScan(*dev) ? "listening" : "ble n/a",
                    sizeof(mijiaUi.status));
        }
    }
    applyMijiaControlRefresh(true);
    startMijiaWifiConnect();
    requestMijiaBleBackground();
}

void leaveMijiaApp() {
    mijiaBleBgEnabled = false;
    mijiaBleScanPending = false;
    mijiaBleFocusActive = false;
    mijiaBleScanAbort();
    cancelMijiaPendingJobs();
}

void updateMijiaApp() {
    updateMijiaWifiConnect();
    updateMijiaRefreshTimeout();
    flushMijiaGridCellUpdates();

    // BLE 聚焦扫（r）：非阻塞，解析失败继续听直到超时
    if (mijiaBleScanPending && !mijiaBleScanIsRunning() && !mijiaHelpVisible) {
        mijiaBleScanPending = false;
        const MijiaDevice* dev = getCurrentMijiaDevice();
        if (dev != nullptr && mijiaBleCanScan(*dev)) {
            strncpy(mijiaUi.status, "listening", sizeof(mijiaUi.status));
            applyMijiaControlRefresh(false);
            if (!mijiaBleScanStart(*dev, MIJIA_BLE_FOCUS_SCAN_S, mijiaDeviceIdx)) {
                strncpy(mijiaUi.status, "ble fail", sizeof(mijiaUi.status));
                mijiaBleFocusActive = false;
                applyMijiaControlRefresh(false);
                requestMijiaBleBackground();
            }
        } else {
            mijiaBleFocusActive = false;
            requestMijiaBleBackground();
        }
    }

    // 无聚焦待启动时，维持后台多设备监听
    if (mijiaBleBgEnabled && !mijiaBleFocusActive && !mijiaBleScanPending &&
        !mijiaBleScanIsRunning()) {
        requestMijiaBleBackground();
    }

    if (mijiaBleScanIsRunning()) {
        MijiaBleReading reading{};
        int device_idx = -1;
        const bool done = mijiaBleScanPoll(reading, &device_idx);
        if (reading.ok) {
            if (device_idx < 0 && mijiaBleFocusActive) {
                device_idx = mijiaDeviceIdx;
            }
            if (device_idx >= 0) {
                storeBleCache(device_idx, reading);
                applyBleReadingToUi(mijiaOverviewUi[device_idx], reading, "ok");
                queueMijiaGridCellRefresh(device_idx);
                if (device_idx == mijiaDeviceIdx && !mijiaOverviewMode && !mijiaGroupMode) {
                    applyBleReadingToUi(mijiaUi, reading, "ok");
                    applyMijiaControlRefresh(false);
                }
            }
        }
        if (done) {
            if (mijiaBleFocusActive) {
                mijiaBleFocusActive = false;
                if (!reading.ok) {
                    // 超时失败：尽量保留缓存数值，状态显示失败原因
                    applyBleCacheToUi(mijiaDeviceIdx, mijiaUi, false);
                    strncpy(mijiaUi.status, reading.message, sizeof(mijiaUi.status) - 1);
                    mijiaUi.status[sizeof(mijiaUi.status) - 1] = '\0';
                    applyMijiaControlRefresh(false);
                }
                requestMijiaBleBackground();
            } else if (mijiaBleBgEnabled) {
                // 一轮后台结束，下一帧再开
                requestMijiaBleBackground();
            }
        }
    }

    // 控制页 BLE：刷新「Xs ago」年龄文案
    if (!mijiaOverviewMode && !mijiaGroupMode && !mijiaHelpVisible && !mijiaBleFocusActive) {
        const MijiaDevice* dev = getCurrentMijiaDevice();
        if (dev != nullptr && mijiaDeviceUsesBle(*dev) && mijiaBleCache[mijiaDeviceIdx].valid) {
            const char* st = mijiaUi.status;
            if (strcmp(st, "ok") == 0 || strstr(st, "ago") != nullptr) {
                char age[48];
                formatBleCacheAgeStatus(mijiaBleCache[mijiaDeviceIdx].updated_ms, age, sizeof(age));
                if (strcmp(st, age) != 0) {
                    strncpy(mijiaUi.status, age, sizeof(mijiaUi.status) - 1);
                    mijiaUi.status[sizeof(mijiaUi.status) - 1] = '\0';
                    applyMijiaControlRefresh(false);
                }
            }
        }
    }

    if (mijiaNeedRedraw) {
        mijiaNeedRedraw = false;
        mijiaNeedGroupHintsRedraw = false;
        redrawMijiaScreen();
    } else if (mijiaNeedGroupHintsRedraw) {
        mijiaNeedGroupHintsRedraw = false;
        if (mijiaGroupMode && !mijiaHelpVisible) {
            drawMijiaGroupBottomHints(getAppConfig());
        }
    }
}

// BtnA：控制页 / Grid 切换当前设备；编组页切换整组
void pollMijiaBtnA() {
    if (!M5Cardputer.BtnA.wasPressed()) {
        return;
    }
    if (mijiaHelpVisible) {
        return;
    }

    if (mijiaGroupMode) {
        toggleMijiaGroupPower();
        return;
    }

    if (mijiaOverviewMode) {
        if (!mijiaOverviewGridMode) {
            return;
        }
        toggleMijiaOverviewPower(mijiaDeviceIdx);
        return;
    }

    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count == 0) {
        return;
    }
    if (!ensureConfigWifi()) {
        strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        applyMijiaControlRefresh(false);
        return;
    }
    setMijiaPower(!mijiaUi.power_on);
}

// 关闭帮助页
static void dismissMijiaHelp() {
    if (!mijiaHelpVisible) {
        return;
    }
    mijiaHelpVisible = false;
    // 关闭 Help：继续查本页尚未拿到状态的设备
    if (mijiaGroupMode) {
        requestMijiaGroupPageRefresh();
    } else if (mijiaOverviewMode && mijiaOverviewGridMode) {
        requestMijiaOverviewPageRefresh();
    }
    redrawMijiaScreen();
}

void handleMijiaApp(const String& key) {
    if (key == "h") {
        if (mijiaGroupMode || mijiaOverviewGridMode || !mijiaOverviewMode) {
            const bool opening = !mijiaHelpVisible;
            if (!opening) {
                dismissMijiaHelp();
                return;
            }
            mijiaHelpVisible = true;
            // 打开 Help：取消查询，避免返回结果盖住帮助页
            if (mijiaGroupMode || mijiaOverviewGridMode) {
                cancelMijiaPendingJobs();
            }
            redrawMijiaScreen();
        }
        return;
    }
    if (mijiaHelpVisible) {
        return;
    }

    // d：进入/退出编组模式
    if (key == "d") {
        if (mijiaGroupMode) {
            exitMijiaGroupMode();
            redrawMijiaScreen();
        } else {
            enterMijiaGroupMode();
            redrawMijiaScreen();
        }
        return;
    }

    if (mijiaGroupMode) {
        if (key == "o") {
            requestMijiaGroupPower(true);
            return;
        }
        if (key == "i") {
            requestMijiaGroupPower(false);
            return;
        }
        if (key == "t") {
            toggleMijiaGroupPower();
            return;
        }
        // 全灯组：0~9 设亮度，-= 步进
        if (mijiaGroupAllLights()) {
            if (key == "-") {
                requestMijiaGroupBright(false, -10);
                return;
            }
            if (key == "=" || key == "+") {
                requestMijiaGroupBright(false, 10);
                return;
            }
            if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
                const int percent = key[0] == '0' ? 100 : (key[0] - '0') * 10;
                requestMijiaGroupBright(true, percent);
                return;
            }
        }
        if (key == "r") {
            // 强制重查：清已知状态
            const MijiaDeviceGroup* group = getCurrentMijiaGroup();
            if (group != nullptr) {
                for (int i = 0; i < group->member_count; i++) {
                    const int idx = group->member_indices[i];
                    if (idx >= 0 && idx < MIJIA_DEVICE_MAX) {
                        mijiaOverviewUi[idx].power_known = false;
                    }
                }
            }
            requestMijiaGroupPageRefresh();
            redrawMijiaScreen();
            return;
        }
        if (key == "," || key == ";") {
            switchMijiaGroup(-1);
            return;
        }
        if (key == "." || key == "/") {
            switchMijiaGroup(1);
            return;
        }
        if (key == "[") {
            switchMijiaGroupPage(-1);
            return;
        }
        if (key == "]") {
            switchMijiaGroupPage(1);
            return;
        }
        if (key == "l") {
            exitMijiaGroupMode();
            enterMijiaOverview(false);
            redrawMijiaScreen();
            return;
        }
        if (key == "g") {
            exitMijiaGroupMode();
            enterMijiaOverview(true);
            redrawMijiaScreen();
            return;
        }
        return;
    }

    if (key == "l") {
        if (!mijiaOverviewMode) {
            enterMijiaOverview(false);
        } else if (mijiaOverviewGridMode) {
            mijiaOverviewGridMode = false;
            mijiaHelpVisible = false;
            syncMijiaOverviewScroll();
        } else {
            exitMijiaOverview();
        }
        redrawMijiaScreen();
        return;
    }
    if (key == "g") {
        if (!mijiaOverviewMode) {
            enterMijiaOverview(true);
        } else if (mijiaOverviewGridMode) {
            mijiaHelpVisible = false;
            exitMijiaOverview();
        } else {
            mijiaOverviewGridMode = true;
            syncMijiaOverviewScroll();
            requestMijiaOverviewPageRefresh();
        }
        redrawMijiaScreen();
        return;
    }
    if (mijiaOverviewMode) {
        if (mijiaOverviewGridMode && (key == "i" || key == "o")) {
            setMijiaOverviewPower(mijiaDeviceIdx, key == "o");
            return;
        }
        if (mijiaOverviewGridMode && key == "t") {
            toggleMijiaOverviewPower(mijiaDeviceIdx);
            return;
        }

        const AppConfig& cfg = getAppConfig();
        // 宫格数字键选中当前页设备
        if (mijiaOverviewGridMode && key.length() == 1 &&
            key[0] >= '1' && key[0] < static_cast<char>('1' + MIJIA_GRID_PAGE_SIZE)) {
            const int pick = key[0] - '1';
            if (pick < MIJIA_GRID_PAGE_SIZE && cfg.loaded) {
                const int idx = mijiaOverviewScrollIdx + pick;
                if (idx < cfg.device_count && idx != mijiaDeviceIdx) {
                    const int old_idx = mijiaDeviceIdx;
                    mijiaDeviceIdx = idx;
                    onMijiaGridDeviceChanged(old_idx, idx);
                }
            }
        }
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
    const bool ble_dev = mijiaDeviceUsesBle(*dev);
    bool handled = true;

    if (key == "o" || key == "i" || key == "t") {
        if (ble_dev || kind == MijiaDevKind::SENSOR_HT || kind == MijiaDevKind::BLE_EVENT) {
            strncpy(mijiaUi.status, "read only", sizeof(mijiaUi.status));
            mijiaNeedRedraw = true;
        } else if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else if (key == "o") {
            setMijiaPower(true);
        } else if (key == "i") {
            setMijiaPower(false);
        } else {
            setMijiaPower(!mijiaUi.power_on);
        }
    } else if (key == "r") {
        if (ble_dev) {
            requestMijiaRefresh();
        } else if (mijiaWifiPhase == MijiaWifiPhase::FAILED || mijiaWifiPhase == MijiaWifiPhase::IDLE) {
            startMijiaWifiConnect();
        } else {
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
    } else if (kind == MijiaDevKind::LIGHT && mijiaLightSupportsHue(dev->model) && key == "j") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaAdjustHue(dev, mijiaUi, -15);
        }
    } else if (kind == MijiaDevKind::LIGHT && mijiaLightSupportsHue(dev->model) && key == "k") {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else {
            mijiaAdjustHue(dev, mijiaUi, 15);
        }
    } else if (kind == MijiaDevKind::FAN_P5) {
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else if (key == "-") {
            mijiaAdjustFanP5Speed(dev, mijiaUi, -10);
        } else if (key == "=" || key == "+") {
            mijiaAdjustFanP5Speed(dev, mijiaUi, 10);
        } else if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
            // 与灯亮度相同：1→10% … 9→90%，0→100%
            const int percent = key[0] == '0' ? 100 : (key[0] - '0') * 10;
            mijiaSetFanP5SpeedPercent(dev, mijiaUi, percent);
        } else if (key == "w") {
            mijiaToggleFanP5Roll(dev, mijiaUi);
        } else if (key == "m") {
            mijiaToggleFanP5Mode(dev, mijiaUi);
        } else if (key == "a") {
            mijiaCycleFanP5Angle(dev, mijiaUi);
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
    } else if (kind == MijiaDevKind::AIR_FRYER) {
        // 手动模式：-/= 温度，[/] 时长；开关仍用 o/i/t/BtnA
        if (!ensureConfigWifi()) {
            strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        } else if (key == "-") {
            mijiaAdjustFryerTemp(dev, mijiaUi, -5);
        } else if (key == "=" || key == "+") {
            mijiaAdjustFryerTemp(dev, mijiaUi, 5);
        } else if (key == "[") {
            mijiaAdjustFryerTime(dev, mijiaUi, -1);
        } else if (key == "]") {
            mijiaAdjustFryerTime(dev, mijiaUi, 1);
        } else {
            handled = false;
        }
    } else {
        handled = false;
    }

    if (handled) {
        applyMijiaControlRefresh(false);
    }
}
