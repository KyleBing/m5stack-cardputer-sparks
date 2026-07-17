#include "app_wifi.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_header.h"
#include "app_icons.h"
#include "app_common.h"
#include "app_colors.h"
#include <WiFi.h>
#include <cstring>

static constexpr int WIFI_LIST_PAGE_SIZE = 4;
static constexpr int WIFI_HINT_LINE_H = 10;
static constexpr int WIFI_LIST_LINE_H = 18;
static constexpr int WIFI_PASS_MAX = 64;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;

enum class WifiAppPhase {
    STATUS,
    SCANNING,
    LIST,
    PASSWORD,
    CONNECTING,
};

static WifiAppPhase wifiPhase = WifiAppPhase::STATUS;
static int wifiScanCount = 0;
static int wifiListPage = 0;
static int wifiSelectedIdx = -1;
static char wifiPassword[WIFI_PASS_MAX + 1] = "";
static char wifiStatus[48] = "";
static uint32_t wifiConnectDeadline = 0;
static bool wifiConnectFromConfig = false;
static bool wifiHelpVisible = false;

// 是否已连上 config 中保存的 WiFi
static bool isWifiConfigConnected() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        return false;
    }
    return WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid;
}

static int getWifiListPageCount() {
    if (wifiScanCount <= 0) {
        return 1;
    }
    return (wifiScanCount + WIFI_LIST_PAGE_SIZE - 1) / WIFI_LIST_PAGE_SIZE;
}

// 截断 SSID 以适应行宽（字符数上限，密码页等单行场景）
static void truncateSsid(const char* src, char* out, const size_t out_size,
                         const size_t max_visible = 14) {
    if (out_size == 0) {
        return;
    }
    strncpy(out, src, out_size - 1);
    out[out_size - 1] = '\0';
    if (strlen(out) > max_visible) {
        out[max_visible - 2] = '.';
        out[max_visible - 1] = '.';
        out[max_visible] = '\0';
    }
}

// 按当前 textSize 的像素宽度截断文本
static void truncateTextToWidth(const char* src, char* out, const size_t out_size,
                                const int max_width_px) {
    if (out_size == 0) {
        return;
    }
    if (max_width_px <= 0) {
        out[0] = '\0';
        return;
    }
    strncpy(out, src, out_size - 1);
    out[out_size - 1] = '\0';
    if (M5Cardputer.Display.textWidth(out) <= max_width_px) {
        return;
    }

    const char suffix[] = "..";
    const int suffix_w = M5Cardputer.Display.textWidth(suffix);
    size_t len = strlen(out);
    while (len > 0 && M5Cardputer.Display.textWidth(out) + suffix_w > max_width_px) {
        out[--len] = '\0';
    }
    if (len + 2 < out_size) {
        out[len] = '.';
        out[len + 1] = '.';
        out[len + 2] = '\0';
    }
}

// 绘制说明行：',' 左箭头，'.' 右箭头
static void drawWifiHintText(const int x, const int y, const char* text, const int text_size = 1) {
    drawHintText(x, y, text, text_size);
}

static void drawWifiHints() {
    const int hint_y = M5Cardputer.Display.height() - 12;

    switch (wifiPhase) {
        case WifiAppPhase::STATUS: {
            const AppConfig& cfg = getAppConfig();
            if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
                KeyHintItem items[] = {{'r', "refresh"}, {'c', "change"}};
                drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 2, 1, APP_COLOR_HINT);
            } else {
                KeyHintItem items[] = {{'c', "scan"}};
                drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 1, 1, APP_COLOR_HINT);
            }
            break;
        }
        case WifiAppPhase::LIST: {
            int cx = APP_CONTENT_X;
            for (const char k : {'1', '2', '3', '4'}) {
                cx += drawKeyBadge(cx, hint_y, k, 1);
            }
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(cx, hint_y);
            M5Cardputer.Display.print("pick ");
            cx += M5Cardputer.Display.textWidth("pick ");
            cx += drawArrowBadge(cx, hint_y, 1);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(cx, hint_y);
            M5Cardputer.Display.print("page");
            break;
        }
        case WifiAppPhase::PASSWORD:
            drawWifiHintText(APP_CONTENT_X, hint_y, "ent connect del bk");
            break;
        case WifiAppPhase::CONNECTING: {
            if (wifiConnectFromConfig) {
                KeyHintItem items[] = {{'r', "retry"}, {'c', "change"}};
                drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 2, 1, APP_COLOR_HINT);
            } else {
                KeyHintItem items[] = {{'r', "retry"}};
                drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 1, 1, APP_COLOR_HINT);
            }
            break;
        }
        default:
            break;
    }
    if (wifiPhase != WifiAppPhase::PASSWORD && wifiPhase != WifiAppPhase::SCANNING) {
        drawHelpHintRight("help");
    }
}

// Help 分栏标题
static int drawWifiHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// Help 按键说明；徽章后恢复说明文字颜色
static int drawWifiHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawWifiHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// Help 功能说明
static int drawWifiHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static void drawWifiHelpPage() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawWifiHelpColHeader(0, col_y, col_w, "keymap");
    y = drawWifiHelpKey(2, y, 'r', "connect / retry");
    y = drawWifiHelpKey(2, y, 'c', "change WiFi");
    y = drawWifiHelpBadge(2, y, "1-4", "select SSID");
    y = drawWifiHelpKey(2, y, 's', "scan again");
    y = drawWifiHelpBadge(2, y, "Enter", "connect");

    y = drawWifiHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawWifiHelpText(manual_x + 2, y, "show WiFi status");
    y = drawWifiHelpText(manual_x + 2, y, "auto saved network");
    y = drawWifiHelpText(manual_x + 2, y, "WiFi unavailable?");
    y = drawWifiHelpText(manual_x + 2, y, "press c to switch");
    y = drawWifiHelpText(manual_x + 2, y, "scan and pick SSID");
    y = drawWifiHelpText(manual_x + 2, y, "new WiFi is saved");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

static void drawWifiStatusScreen() {
    beginAppScreen("WiFi");
    M5Cardputer.Display.setTextSize(2);

    int y = APP_CONTENT_Y;
    if (WiFi.status() == WL_CONNECTED) {
        char buf[24];
        M5Cardputer.Display.setTextColor(CYAN, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("ssid: ");
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.println(WiFi.SSID().c_str());
        y += WIFI_LIST_LINE_H;

        M5Cardputer.Display.setTextColor(CYAN, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("ip: ");
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.println(WiFi.localIP().toString().c_str());
        y += WIFI_LIST_LINE_H;

        const int rssi = WiFi.RSSI();
        snprintf(buf, sizeof(buf), "%d dBm", rssi);
        M5Cardputer.Display.setTextColor(CYAN, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("rssi: ");
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.println(buf);
        y += WIFI_LIST_LINE_H + 4;
    } else {
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println("not connected");
        y += WIFI_LIST_LINE_H;
        if (wifiStatus[0] != '\0') {
            M5Cardputer.Display.setTextColor(ORANGE, BLACK);
            M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
            M5Cardputer.Display.println(wifiStatus);
            y += WIFI_LIST_LINE_H;
        }
    }

    drawWifiHints();
}

static void drawWifiListScreen() {
    beginAppScreen("WiFi Scan");
    M5Cardputer.Display.setTextSize(1);

    int y = APP_CONTENT_Y;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d net p%d/%d", wifiScanCount, wifiListPage + 1,
             getWifiListPageCount());
    M5Cardputer.Display.setTextColor(CYAN, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println(buf);
    y += WIFI_HINT_LINE_H + 2;

    const int start = wifiListPage * WIFI_LIST_PAGE_SIZE;
    const int end = start + WIFI_LIST_PAGE_SIZE < wifiScanCount ? start + WIFI_LIST_PAGE_SIZE
                                                                : wifiScanCount;

    const int content_right = M5Cardputer.Display.width() - APP_CONTENT_X;
    constexpr int ROW_RIGHT_GAP = 2;
    constexpr int RSSI_SIGNAL_GAP = 4;

    for (int i = start; i < end; i++) {
        const int row = i - start;
        const int row_y = y + row * WIFI_LIST_LINE_H;
        const int rssi = WiFi.RSSI(i);
        const bool locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        // 右侧：扇形圆信号图标贴边，RSSI 数值在其左侧
        const int signal_x = content_right - ICON_WIFI_W;
        drawIconWifi(signal_x, row_y + 3, rssi, WHITE);

        char rssi_buf[8];
        snprintf(rssi_buf, sizeof(rssi_buf), "%d", rssi);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(DARKGREY, BLACK);
        const int rssi_w = M5Cardputer.Display.textWidth(rssi_buf);
        const int rssi_x = signal_x - RSSI_SIGNAL_GAP - rssi_w;
        M5Cardputer.Display.setCursor(rssi_x, row_y + 5);
        M5Cardputer.Display.print(rssi_buf);

        // 左侧序号
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(YELLOW, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, row_y);
        char num_buf[4];
        snprintf(num_buf, sizeof(num_buf), "%d.", row + 1);
        M5Cardputer.Display.print(num_buf);
        const int num_w = M5Cardputer.Display.textWidth(num_buf);

        // SSID 占用序号与 RSSI 之间的剩余宽度
        const int name_x = APP_CONTENT_X + num_w + ROW_RIGHT_GAP;
        const int lock_w = locked ? M5Cardputer.Display.textWidth("*") : 0;
        const int name_max_w = rssi_x - ROW_RIGHT_GAP - name_x - lock_w;

        char ssid[33];
        truncateTextToWidth(WiFi.SSID(i).c_str(), ssid, sizeof(ssid), name_max_w);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(name_x, row_y);
        M5Cardputer.Display.print(ssid);
        if (locked) {
            M5Cardputer.Display.print("*");
        }
    }

    if (wifiScanCount == 0) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println("no network");
    }

    drawWifiHints();
}

static void drawWifiPasswordScreen() {
    beginAppScreen("WiFi Pass");

    int y = APP_CONTENT_Y;
    const int content_right = M5Cardputer.Display.width() - APP_CONTENT_X;
    M5Cardputer.Display.setTextSize(2);

    if (wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount) {
        char ssid[33];
        M5Cardputer.Display.setTextColor(CYAN, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print("ssid: ");
        const int label_w = M5Cardputer.Display.textWidth("ssid: ");
        truncateTextToWidth(WiFi.SSID(wifiSelectedIdx).c_str(), ssid, sizeof(ssid),
                            content_right - APP_CONTENT_X - label_w);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.println(ssid);
    } else {
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println("ssid: ?");
    }
    y += WIFI_LIST_LINE_H;

    M5Cardputer.Display.setTextColor(CYAN, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("pass: ");
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.println(wifiPassword);
    y += WIFI_LIST_LINE_H;

    if (wifiStatus[0] != '\0') {
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println(wifiStatus);
        y += WIFI_LIST_LINE_H;
    }

    drawWifiHints();
}

static void drawWifiConnectingScreen() {
    beginAppScreen("WiFi");
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.println("connecting...");

    if (wifiConnectFromConfig) {
        const AppConfig& cfg = getAppConfig();
        if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
            char ssid[33];
            truncateSsid(cfg.wifi_ssid, ssid, sizeof(ssid), 18);
            M5Cardputer.Display.println(ssid);
        }
    } else if (wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount) {
        char ssid[33];
        truncateSsid(WiFi.SSID(wifiSelectedIdx).c_str(), ssid, sizeof(ssid), 18);
        M5Cardputer.Display.println(ssid);
    }

    if (wifiStatus[0] != '\0') {
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.println(wifiStatus);
    }

    drawWifiHints();
}

// 使用 config 中已保存的 WiFi 发起连接（与米家应用一致：不反复 disconnect）
static void startWifiConfigConnect() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        strncpy(wifiStatus, "no config", sizeof(wifiStatus));
        wifiPhase = WifiAppPhase::STATUS;
        drawWifiStatusScreen();
        return;
    }

    if (isWifiConfigConnected()) {
        wifiConnectFromConfig = false;
        wifiPhase = WifiAppPhase::STATUS;
        wifiStatus[0] = '\0';
        drawWifiStatusScreen();
        updateAppHeaderStatus();
        return;
    }

    WiFi.mode(WIFI_STA);
    applyWifiRadioSleepPolicy();
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    wifiSelectedIdx = -1;
    wifiConnectFromConfig = true;
    wifiPhase = WifiAppPhase::CONNECTING;
    wifiConnectDeadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
    wifiStatus[0] = '\0';
    drawWifiConnectingScreen();
}

static void startWifiScan() {
    wifiPhase = WifiAppPhase::SCANNING;
    wifiListPage = 0;
    wifiSelectedIdx = -1;
    wifiStatus[0] = '\0';

    beginAppScreen("WiFi Scan");
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.println("scanning...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.scanDelete();
    wifiScanCount = WiFi.scanNetworks();
    wifiPhase = WifiAppPhase::LIST;
    drawWifiListScreen();
}

static void startWifiConnect(const char* password) {
    if (wifiSelectedIdx < 0 || wifiSelectedIdx >= wifiScanCount) {
        return;
    }

    const String ssid = WiFi.SSID(wifiSelectedIdx);
    WiFi.mode(WIFI_STA);
    applyWifiRadioSleepPolicy();
    // 切换 SSID 时才断开，避免频繁 disconnect 导致连接超时
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() != ssid) {
        WiFi.disconnect();
    }
    WiFi.begin(ssid.c_str(), password);

    wifiConnectFromConfig = false;
    wifiPhase = WifiAppPhase::CONNECTING;
    wifiConnectDeadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
    strncpy(wifiStatus, "wait...", sizeof(wifiStatus));
    drawWifiConnectingScreen();
}

static void selectWifiNetwork(const int list_index) {
    const int idx = wifiListPage * WIFI_LIST_PAGE_SIZE + list_index;
    if (idx < 0 || idx >= wifiScanCount) {
        return;
    }

    wifiSelectedIdx = idx;
    wifiPassword[0] = '\0';
    wifiStatus[0] = '\0';
    wifiConnectFromConfig = false;

    if (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN) {
        startWifiConnect("");
        return;
    }

    wifiPhase = WifiAppPhase::PASSWORD;
    drawWifiPasswordScreen();
}

static void appendWifiPasswordChar(const char c) {
    const size_t len = strlen(wifiPassword);
    if (len >= WIFI_PASS_MAX) {
        return;
    }
    wifiPassword[len] = c;
    wifiPassword[len + 1] = '\0';
}

static void backspaceWifiPassword() {
    const size_t len = strlen(wifiPassword);
    if (len == 0) {
        return;
    }
    wifiPassword[len - 1] = '\0';
}

void enterWifiApp() {
    wifiListPage = 0;
    wifiSelectedIdx = -1;
    wifiPassword[0] = '\0';
    wifiStatus[0] = '\0';
    wifiConnectFromConfig = false;
    wifiHelpVisible = false;

    const AppConfig& cfg = getAppConfig();

    // 有已保存 WiFi 时自动连接并显示状态
    if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
        if (isWifiConfigConnected()) {
            wifiPhase = WifiAppPhase::STATUS;
        } else {
            startWifiConfigConnect();
            return;
        }
    } else {
        wifiPhase = WifiAppPhase::STATUS;
    }
    drawWifiApp();
}

void drawWifiApp() {
    if (wifiHelpVisible) {
        drawWifiHelpPage();
        return;
    }
    switch (wifiPhase) {
        case WifiAppPhase::STATUS:
            drawWifiStatusScreen();
            break;
        case WifiAppPhase::SCANNING:
            beginAppScreen("WiFi Scan");
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
            M5Cardputer.Display.println("scanning...");
            break;
        case WifiAppPhase::LIST:
            drawWifiListScreen();
            break;
        case WifiAppPhase::PASSWORD:
            drawWifiPasswordScreen();
            break;
        case WifiAppPhase::CONNECTING:
            drawWifiConnectingScreen();
            break;
    }
}

void updateWifiApp() {
    if (wifiPhase != WifiAppPhase::CONNECTING) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED &&
        (!wifiConnectFromConfig || isWifiConfigConnected())) {
        if (!wifiConnectFromConfig &&
            saveAppConfigWifi(WiFi.SSID().c_str(), wifiPassword)) {
            strncpy(wifiStatus, "saved", sizeof(wifiStatus));
        } else {
            strncpy(wifiStatus, "connected", sizeof(wifiStatus));
        }
        wifiConnectFromConfig = false;
        wifiPhase = WifiAppPhase::STATUS;
        drawWifiApp();
        updateAppHeaderStatus();
        return;
    }

    if (static_cast<int32_t>(millis() - wifiConnectDeadline) >= 0) {
        strncpy(wifiStatus, "timeout", sizeof(wifiStatus));
        wifiConnectFromConfig = false;
        wifiPhase = WifiAppPhase::STATUS;
        drawWifiApp();
    }
}

void handleWifiApp(const Keyboard_Class::KeysState& status) {
    // 密码输入页保留 h 作为普通密码字符，其它页面可打开帮助
    if (wifiPhase != WifiAppPhase::PASSWORD) {
        for (const char c : status.word) {
            if (c == 'h' || c == 'H') {
                wifiHelpVisible = !wifiHelpVisible;
                drawWifiApp();
                return;
            }
        }
    }
    if (wifiHelpVisible) {
        return;
    }

    if (wifiPhase == WifiAppPhase::CONNECTING) {
        for (const char c : status.word) {
            if (c == 'c' || c == 'C') {
                WiFi.disconnect();
                wifiConnectFromConfig = false;
                wifiStatus[0] = '\0';
                startWifiScan();
                return;
            }
            if (c == 'r' || c == 'R') {
                if (wifiConnectFromConfig) {
                    startWifiConfigConnect();
                } else if (wifiSelectedIdx >= 0) {
                    startWifiConnect(wifiPassword);
                }
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::STATUS) {
        for (const char c : status.word) {
            if (c == 'r' || c == 'R') {
                const AppConfig& cfg = getAppConfig();
                if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
                    startWifiConfigConnect();
                } else {
                    wifiStatus[0] = '\0';
                    drawWifiStatusScreen();
                }
                return;
            }
            if (c == 'c' || c == 'C') {
                startWifiScan();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::LIST) {
        const int nav = getMenuNavDelta(status);
        if (nav != 0) {
            const int page_count = getWifiListPageCount();
            wifiListPage = (wifiListPage + nav + page_count) % page_count;
            drawWifiListScreen();
            return;
        }

        for (const char c : status.word) {
            if (c >= '1' && c <= '4') {
                selectWifiNetwork(c - '1');
                return;
            }
            if (c == 's' || c == 'S') {
                startWifiScan();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::PASSWORD) {
        if (status.del) {
            backspaceWifiPassword();
            drawWifiPasswordScreen();
            return;
        }

        if (status.space) {
            appendWifiPasswordChar(' ');
            drawWifiPasswordScreen();
            return;
        }

        if (status.enter) {
            startWifiConnect(wifiPassword);
            return;
        }

        for (const char c : status.word) {
            if (c == '\b') {
                backspaceWifiPassword();
                drawWifiPasswordScreen();
                return;
            }
            if (c == 0x1B || c == 'q' || c == 'Q') {
                wifiPhase = WifiAppPhase::LIST;
                drawWifiListScreen();
                return;
            }
            appendWifiPasswordChar(c);
        }

        if (!status.word.empty()) {
            drawWifiPasswordScreen();
        }
    }
}
