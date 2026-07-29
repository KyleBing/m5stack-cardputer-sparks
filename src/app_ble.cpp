#include "app_ble.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_common.h"
#include "app_colors.h"
#include <BLEDevice.h>
#include <M5GFX.h>
#include <cstring>

static constexpr int BLE_SCAN_SECONDS = 4;
static constexpr int BLE_SCAN_MAX_ITEMS = 24;
static constexpr int BLE_SCAN_PAGE_SIZE = 4;
static constexpr int BLE_LINE_H = 10;  // Font0 x1 行高（收紧）
static constexpr int BLE_ITEM_GAP = 1; // 条目间分隔后的空隙
static constexpr int BLE_HINT_H = 14;  // 底栏 tip 预留高度
static constexpr int BLE_LIST_LINE_START_X = APP_CONTENT_X + 18;
static constexpr int BLE_LIST_LINE_END_X = APP_CONTENT_X + 236;
static constexpr uint16_t BLE_INDEX_COLOR = ORANGE; // 列表序号特殊色

struct BleScanItem {
    char name[24];
    char addr[20];
    int rssi;
    char category[12];
};

static BleScanItem bleScanItems[BLE_SCAN_MAX_ITEMS];

static bool bleScreenReady = false;
static bool bleScanning = false;
static int bleScanCount = 0;
static int bleScanPage = 0;
static bool bleHelpVisible = false;
static bool bleListDirty = true;
static bool bleLastScanning = false;
static int bleLastScanCount = -1;
static int bleLastScanPage = -1;

// 蓝牙界面统一使用系统默认 Font0
static void setBleFont(const int text_size = 1) {
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(text_size == 2 ? 2 : 1);
}

static void resetBleFont() {
    M5Cardputer.Display.setFont(nullptr);
}

// 列表条目：序号特殊色 + 名称；下一行地址/RSSI + 黄色类别
static void drawBleDeviceListItem(const int x, const int y, const int index, const BleScanItem& item) {
    char num[8];
    snprintf(num, sizeof(num), "%02d", index);

    setBleFont(1);
    // 序号
    M5Cardputer.Display.setTextColor(BLE_INDEX_COLOR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(num);
    const int name_x = x + M5Cardputer.Display.textWidth(num) + 4;
    // 设备名
    M5Cardputer.Display.setTextColor(INFO_VALUE_COLOR, BLACK);
    M5Cardputer.Display.setCursor(name_x, y);
    M5Cardputer.Display.print(item.name);

    // 第二行：地址 + RSSI + 类别（紧贴上一行）
    const int y2 = y + BLE_LINE_H;
    char sig[36];
    snprintf(sig, sizeof(sig), "%s %ddBm ", item.addr, item.rssi);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(name_x, y2);
    M5Cardputer.Display.print(sig);
    // 类别黄色标记
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.print(item.category);
    resetBleFont();
}

static void resetBleListCache() {
    bleListDirty = true;
    bleLastScanning = false;
    bleLastScanCount = -1;
    bleLastScanPage = -1;
}

// 列表模式下仅在状态变化时重绘，减少频繁闪烁
static bool shouldRedrawBleList() {
    if (bleListDirty) {
        return true;
    }
    if (bleLastScanning != bleScanning) {
        return true;
    }
    if (bleLastScanCount != bleScanCount) {
        return true;
    }
    if (bleLastScanPage != bleScanPage) {
        return true;
    }
    return false;
}

static void markBleListDrawn() {
    bleListDirty = false;
    bleLastScanning = bleScanning;
    bleLastScanCount = bleScanCount;
    bleLastScanPage = bleScanPage;
}

static int getBlePageCount() {
    if (bleScanCount <= 0) {
        return 1;
    }
    return (bleScanCount + BLE_SCAN_PAGE_SIZE - 1) / BLE_SCAN_PAGE_SIZE;
}

// Help 分栏标题
static int drawBleHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    setBleFont(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    resetBleFont();
    return y + 13;
}

// Help 按键说明；徽章后恢复说明文字颜色
static int drawBleHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    setBleFont(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    resetBleFont();
    return y + 11;
}

static int drawBleHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    setBleFont(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    resetBleFont();
    return y + 11;
}

static int drawBleHelpText(const int x, const int y, const char* text) {
    setBleFont(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    resetBleFont();
    return y + 11;
}

// 黄色类别名 + 用途说明
static int drawBleHelpType(const int x, const int y, const char* cat, const char* use) {
    setBleFont(1);
    M5Cardputer.Display.setTextColor(YELLOW, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(cat);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(" ");
    M5Cardputer.Display.print(use);
    resetBleFont();
    return y + 11;
}

static void drawBleHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int types_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                      M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawBleHelpColHeader(0, col_y, col_w, "keymap");
    y = drawBleHelpKey(2, y, 's', "scan nearby");
    y = drawBleHelpBadge(2, y, ",.", "page");
    y = drawBleHelpBadge(2, y, "[]", "page too");
    y = drawBleHelpKey(2, y, 'h', "help / close");

    y = drawBleHelpColHeader(types_x, col_y, screen_w - types_x, "types");
    y = drawBleHelpText(types_x + 2, y, "scan nearby BLE");
    y = drawBleHelpType(types_x + 2, y, "normal", "phone/buds");
    y = drawBleHelpText(types_x + 2, y, " generic peripheral");
    y = drawBleHelpType(types_x + 2, y, "beacon", "locate/ads");
    y = drawBleHelpText(types_x + 2, y, " iBeacon/Eddystone");
    y = drawBleHelpType(types_x + 2, y, "ble-svc", "sensor/IoT");
    y = drawBleHelpText(types_x + 2, y, " advertises GATT");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

// 底栏 tip：scan + 分页页码；右侧 h help（[] 翻页只写在 Help）
static void drawBleActionHints() {
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_CONTENT_X;
    static const KeyHintItem items[] = {
        {'s', "scan"},
    };
    drawKeyHintsRow(cx, hint_y, items, 1, 1, APP_COLOR_HINT);

    // 有列表时显示翻页箭头与页码（不提示 []）
    if (bleScanCount > 0 && !bleScanning) {
        setBleFont(1);
        cx = APP_CONTENT_X + 56;
        cx += drawArrowBadge(cx, hint_y, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复 tip 色
        M5Cardputer.Display.setCursor(cx, hint_y + 1);
        M5Cardputer.Display.print("page ");
        cx += M5Cardputer.Display.textWidth("page ");
        char buf[12];
        snprintf(buf, sizeof(buf), "%d/%d", bleScanPage + 1, getBlePageCount());
        M5Cardputer.Display.print(buf);
        resetBleFont();
    }

    drawHelpHintRight("help");
}

// 入口空列表提示：x2 + 按键徽章
static void drawBleEmptyPrompt(const int y) {
    int cx = APP_CONTENT_X;
    cx += drawKeyBadge(cx, y, 's', 2);
    setBleFont(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("scan");
    resetBleFont();
}

// 蓝牙设备类型：普通 BLE / Beacon / BLE 服务设备
static const char* classifyBleCategory(BLEAdvertisedDevice& dev) {
    if (dev.haveManufacturerData() && !dev.haveServiceUUID()) {
        return "beacon";
    }
    if (dev.haveServiceUUID()) {
        return "ble-svc";
    }
    return "normal";
}

static void copySafe(char* dest, const size_t size, const char* src) {
    if (size == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

static void scanNearbyBleDevices() {
    if (!beginBleScanSession()) {
        return;
    }

    bleScanning = true;
    bleListDirty = true;
    drawBleApp(false);

    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    BLEScanResults results = scan->start(BLE_SCAN_SECONDS, false);

    const int count = results.getCount();
    bleScanCount = count > BLE_SCAN_MAX_ITEMS ? BLE_SCAN_MAX_ITEMS : count;
    for (int i = 0; i < bleScanCount; i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        if (dev.haveName()) {
            copySafe(bleScanItems[i].name, sizeof(bleScanItems[i].name), dev.getName().c_str());
        } else {
            copySafe(bleScanItems[i].name, sizeof(bleScanItems[i].name), "(no name)");
        }
        copySafe(bleScanItems[i].addr, sizeof(bleScanItems[i].addr),
                 dev.getAddress().toString().c_str());
        bleScanItems[i].rssi = dev.getRSSI();
        copySafe(bleScanItems[i].category, sizeof(bleScanItems[i].category),
                 classifyBleCategory(dev));
    }
    bleScanPage = 0;
    scan->clearResults();
    bleScanning = false;
    bleListDirty = true;
    endBleScanSession(true);
}

// 扫描列表（内容区），底栏 tip；支持翻页
static void drawBleScanListFull() {
    const int y_start = APP_CONTENT_INSET_Y;
    const int content_h = M5Cardputer.Display.height() - y_start - BLE_HINT_H;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, y_start, 236, content_h + BLE_HINT_H, BLACK);
    int y = y_start;

    if (bleScanning) {
        setBleFont(2);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("scanning...");
        resetBleFont();
        drawBleActionHints();
        return;
    }

    if (bleScanPage >= getBlePageCount()) {
        bleScanPage = getBlePageCount() - 1;
    }
    if (bleScanPage < 0) {
        bleScanPage = 0;
    }
    if (bleScanCount == 0) {
        drawBleEmptyPrompt(y);
        drawBleActionHints();
        return;
    }

    const int start = bleScanPage * BLE_SCAN_PAGE_SIZE;
    const int end =
        (start + BLE_SCAN_PAGE_SIZE < bleScanCount) ? start + BLE_SCAN_PAGE_SIZE : bleScanCount;
    for (int i = start; i < end; i++) {
        const BleScanItem& item = bleScanItems[i];
        drawBleDeviceListItem(APP_CONTENT_X, y, i + 1, item);
        y += BLE_LINE_H * 2;
        // 分隔线
        M5Cardputer.Display.drawFastHLine(BLE_LIST_LINE_START_X, y,
                                          BLE_LIST_LINE_END_X - BLE_LIST_LINE_START_X,
                                          APP_COLOR_MUTED);
        y += BLE_ITEM_GAP + 1;
    }

    drawBleActionHints();
}

void drawBleApp(const bool full_init) {
    if (bleHelpVisible) {
        drawBleHelpPage();
        return;
    }

    if (full_init || !bleScreenReady) {
        beginAppScreen("BLE");
        bleScreenReady = true;
        resetBleListCache();
    }

    if (!full_init && !shouldRedrawBleList()) {
        return;
    }
    drawBleScanListFull();
    markBleListDrawn();
}

void enterBleApp() {
    bleScreenReady = false;
    bleHelpVisible = false;
    drawBleApp(true);
}

void updateBleApp() {
    if (!bleScreenReady || bleHelpVisible) {
        return;
    }
    drawBleApp(false);
}

void handleBleApp(const String& key) {
    if (key == "h") {
        bleHelpVisible = !bleHelpVisible;
        if (bleHelpVisible) {
            drawBleHelpPage();
        } else {
            // Help 清过全屏，关闭后强制重绘主界面
            bleScreenReady = false;
            bleListDirty = true;
            drawBleApp(true);
        }
        updateAppHeaderStatus();
        return;
    }
    if (bleHelpVisible) {
        return;
    }

    if (key == "s") {
        scanNearbyBleDevices();
    } else {
        return;
    }
    drawBleApp(false);
    updateAppHeaderStatus();
}

bool handleBlePageNav(const Keyboard_Class::KeysState& status) {
    if (bleHelpVisible || bleScanning) {
        return false;
    }

    // 方向键 / ;,. / 以及 [] 翻页
    int delta = getMenuNavDelta(status);
    if (delta == 0) {
        for (const char c : status.word) {
            if (c == '[') {
                delta = -1;
                break;
            }
            if (c == ']') {
                delta = 1;
                break;
            }
        }
    }
    if (delta == 0) {
        return false;
    }

    const int page_count = getBlePageCount();
    if (page_count <= 1) {
        return false;
    }
    bleScanPage += delta;
    if (bleScanPage < 0) {
        bleScanPage = 0;
    }
    if (bleScanPage >= page_count) {
        bleScanPage = page_count - 1;
    }
    drawBleApp(false);
    return true;
}
