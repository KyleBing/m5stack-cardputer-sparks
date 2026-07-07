#include "app_wifi.h"
#include "app_header.h"
#include "app_signal.h"
#include <WiFi.h>
#include <cstring>

static constexpr int WIFI_LIST_PAGE_SIZE = 4;
static constexpr int WIFI_HINT_LINE_H = 10;
static constexpr int WIFI_LIST_LINE_H = 18;
static constexpr int WIFI_PASS_MAX = 64;

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

// 翻页：-1 上一页，0 无，1 下一页
static int getWifiPageNavDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x50 || hid == 0x33 || hid == 0x36) {
            return -1;
        }
        if (hid == 0x51 || hid == 0x4F || hid == 0x37 || hid == 0x38) {
            return 1;
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

// 绘制左/右翻页小箭头（说明里替代 , . 键位）
static void drawNavArrowLeft(const int x, const int cy, const uint16_t color) {
    M5Cardputer.Display.fillTriangle(x, cy - 3, x + 5, cy, x, cy + 3, color);
}

static void drawNavArrowRight(const int x, const int cy, const uint16_t color) {
    M5Cardputer.Display.fillTriangle(x + 5, cy - 3, x, cy, x + 5, cy + 3, color);
}

// 绘制说明行：',' 左箭头，'.' 右箭头
static void drawWifiHintText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
    int cx = x;
    const int arrow_cy = y + 4;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == ',') {
            drawNavArrowLeft(cx, arrow_cy, LIGHTGREY);
            cx += 8;
        } else if (*p == '.') {
            drawNavArrowRight(cx, arrow_cy, LIGHTGREY);
            cx += 8;
        } else {
            M5Cardputer.Display.setCursor(cx, y);
            const char ch[2] = {*p, '\0'};
            M5Cardputer.Display.print(ch);
            cx += M5Cardputer.Display.textWidth(ch);
        }
    }
}

static void drawWifiHints(const int y) {
    switch (wifiPhase) {
        case WifiAppPhase::STATUS:
            drawWifiHintText(APP_CONTENT_X, y, "s scan");
            break;
        case WifiAppPhase::LIST:
            drawWifiHintText(APP_CONTENT_X, y, "1-4 pick , . page");
            break;
        case WifiAppPhase::PASSWORD:
            drawWifiHintText(APP_CONTENT_X, y, "ent connect del bk");
            break;
        case WifiAppPhase::CONNECTING:
            drawWifiHintText(APP_CONTENT_X, y, "connecting");
            break;
        default:
            break;
    }
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
        M5Cardputer.Display.print(buf);
        drawSignalBars(APP_CONTENT_X + 72, y - 1, rssi, WHITE);
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

    drawWifiHints(y);
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

        // 右侧：信号图标贴边，RSSI 数值在其左侧
        const int signal_x = content_right - SIGNAL_BAR_ICON_W;
        drawSignalBars(signal_x, row_y + 4, rssi, WHITE);

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

    drawWifiHints(M5Cardputer.Display.height() - 12);
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
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.println(wifiStatus);
        y += WIFI_HINT_LINE_H;
    }

    drawWifiHints(y);
}

static void drawWifiConnectingScreen() {
    beginAppScreen("WiFi");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.println("connecting...");
    if (wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount) {
        M5Cardputer.Display.println(WiFi.SSID(wifiSelectedIdx).c_str());
    }
    if (wifiStatus[0] != '\0') {
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.println(wifiStatus);
    }
}

static void startWifiScan() {
    wifiPhase = WifiAppPhase::SCANNING;
    wifiListPage = 0;
    wifiSelectedIdx = -1;
    wifiStatus[0] = '\0';

    beginAppScreen("WiFi Scan");
    M5Cardputer.Display.setTextSize(1);
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
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password);

    wifiPhase = WifiAppPhase::CONNECTING;
    wifiConnectDeadline = millis() + 15000;
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
    wifiPhase = WifiAppPhase::STATUS;
    wifiListPage = 0;
    wifiSelectedIdx = -1;
    wifiPassword[0] = '\0';
    wifiStatus[0] = '\0';
    WiFi.mode(WIFI_STA);
    drawWifiApp();
}

void drawWifiApp() {
    switch (wifiPhase) {
        case WifiAppPhase::STATUS:
            drawWifiStatusScreen();
            break;
        case WifiAppPhase::SCANNING:
            beginAppScreen("WiFi Scan");
            M5Cardputer.Display.setTextSize(1);
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

    if (WiFi.status() == WL_CONNECTED) {
        strncpy(wifiStatus, "connected", sizeof(wifiStatus));
        wifiPhase = WifiAppPhase::STATUS;
        drawWifiStatusScreen();
        return;
    }

    if (static_cast<int32_t>(millis() - wifiConnectDeadline) >= 0) {
        strncpy(wifiStatus, "timeout", sizeof(wifiStatus));
        wifiPhase = WifiAppPhase::STATUS;
        drawWifiStatusScreen();
    }
}

void handleWifiApp(const Keyboard_Class::KeysState& status) {
    if (wifiPhase == WifiAppPhase::CONNECTING) {
        return;
    }

    if (wifiPhase == WifiAppPhase::STATUS) {
        for (const char c : status.word) {
            if (c == 's' || c == 'S') {
                startWifiScan();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::LIST) {
        const int nav = getWifiPageNavDelta(status);
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
