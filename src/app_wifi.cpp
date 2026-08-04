#include "app_wifi.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include "app_icons.h"
#include "app_common.h"
#include "app_colors.h"
#include <WiFi.h>
#include <cstring>

static constexpr int WIFI_SAVED_PAGE_SIZE = 3; // 已保存：一页 3 条，均分行高
static constexpr int WIFI_SCAN_PAGE_SIZE = 4;  // 扫网：一页 4 条
static constexpr int WIFI_CONTENT_TOP = APP_CONTENT_Y_NO_TAP_TO_HEADER + 2; // 贴 header 下沿
static constexpr int WIFI_CONTENT_BOTTOM_PAD = 2;
static constexpr int WIFI_SCAN_FOOTER_H = 12; // 扫网底栏：条数 + 页码
static constexpr int WIFI_LIST_LINE_H = 24; // 状态页行距
static constexpr int WIFI_CARD_GAP = 2; // 已保存卡片间距（越小卡片越高）
static constexpr int WIFI_PASS_MAX = 64;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;  // 已保存档案后台连接
static constexpr uint32_t WIFI_JOIN_TIMEOUT_MS = 10000;    // 输入密码后的手动连接
static constexpr uint32_t WIFI_FAIL_GRACE_MS = 1500;       // 刚 begin 时忽略瞬时错误状态
static constexpr int WIFI_SSID_CARD_H = 26;
static constexpr int WIFI_PROGRESS_H = 10;
static constexpr int WIFI_PROGRESS_SEG_W = 56;
static constexpr int WIFI_PROGRESS_STEP_PX = 5;
static constexpr uint32_t WIFI_PROGRESS_TICK_MS = 50;

// 与主页菜单卡片同色
static uint16_t wifiCardBg() {
    return M5Cardputer.Display.color565(0x0D, 0x16, 0x22);
}
static uint16_t wifiCardTitleColor() {
    return M5Cardputer.Display.color565(0xF4, 0xF1, 0xE8);
}
static uint16_t wifiCardAccentGold() {
    return M5Cardputer.Display.color565(0xE9, 0xC4, 0x6A);
}
// 扫网列表条目间的暗色分隔线
static uint16_t wifiListSeparatorColor() {
    return M5Cardputer.Display.color565(0x2A, 0x2A, 0x2A);
}

enum class WifiAppPhase {
    STATUS,
    SAVED, // 已保存网络列表，切换 wifi_active
    SCANNING,
    LIST,
    PASSWORD,
    CONNECTING,
    FAILED, // 手动连接失败，提示后回扫网列表
};

static WifiAppPhase wifiPhase = WifiAppPhase::STATUS;
static int wifiScanCount = 0;
static int wifiListPage = 0;
static int wifiSavedPage = 0;
static int wifiSavedSel = 0; // 已保存列表选中项（全局下标，跨页）
static int wifiSelectedIdx = -1;
static char wifiPassword[WIFI_PASS_MAX + 1] = "";
static char wifiStatus[48] = "";
static uint32_t wifiConnectDeadline = 0;
static bool wifiConnectFromConfig = false;
static bool wifiHelpVisible = false;
// 连接目标（脱离扫描索引，失败页仍可显示）
static char wifiTargetSsid[33] = "";
static int wifiTargetRssi = -100;
static uint32_t wifiConnectStartMs = 0;
static uint32_t wifiConnectAnimMs = 0;
static int wifiConnectAnimPos = 0;
static int wifiConnectLeftSec = -1;
// 失败原因（失败页需手动按键返回）
static char wifiFailReason[40] = "";

// 各阶段的 header 标题
static const char* wifiPageName() {
    switch (wifiPhase) {
        case WifiAppPhase::STATUS:
            return "WiFi Status";
        case WifiAppPhase::SAVED:
            return "WiFi Saved";
        case WifiAppPhase::SCANNING:
        case WifiAppPhase::LIST:
            return "WiFi Scanner";
        case WifiAppPhase::PASSWORD:
            return "WiFi Password";
        case WifiAppPhase::CONNECTING:
            return "WiFi Connect";
        case WifiAppPhase::FAILED:
            return "WiFi Failed";
    }
    return "WiFi";
}

// 清屏 + header（标题随阶段变化；switcher 等列表页不画下边框）
static void beginWifiScreen(const bool draw_divider = true) {
    beginAppScreen(wifiPageName(), draw_divider);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
}

// 是否已连上 config 中当前 active WiFi
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
    return (wifiScanCount + WIFI_SCAN_PAGE_SIZE - 1) / WIFI_SCAN_PAGE_SIZE;
}

static int getWifiSavedPageCount() {
    const AppConfig& cfg = getAppConfig();
    if (cfg.wifi_count <= 0) {
        return 1;
    }
    return (cfg.wifi_count + WIFI_SAVED_PAGE_SIZE - 1) / WIFI_SAVED_PAGE_SIZE;
}

// 选中项夹到有效范围，并让分页跟随选中项
static void clampWifiSavedSel() {
    const AppConfig& cfg = getAppConfig();
    if (cfg.wifi_count <= 0) {
        wifiSavedSel = 0;
        wifiSavedPage = 0;
        return;
    }
    if (wifiSavedSel < 0) {
        wifiSavedSel = 0;
    } else if (wifiSavedSel >= cfg.wifi_count) {
        wifiSavedSel = cfg.wifi_count - 1;
    }
    wifiSavedPage = wifiSavedSel / WIFI_SAVED_PAGE_SIZE;
}

// 上下键移动选中项（跨页循环）
static void moveWifiSavedSel(const int delta) {
    const AppConfig& cfg = getAppConfig();
    if (cfg.wifi_count <= 0) {
        return;
    }
    wifiSavedSel = (wifiSavedSel + delta + cfg.wifi_count) % cfg.wifi_count;
    wifiSavedPage = wifiSavedSel / WIFI_SAVED_PAGE_SIZE;
}

// 内容区高度（header 下沿到屏幕底部）
static int wifiContentHeight() {
    return M5Cardputer.Display.height() - WIFI_CONTENT_BOTTOM_PAD - WIFI_CONTENT_TOP;
}

// 已保存列表：内容区均分给每页条数（含卡片间距）
static int wifiSavedSlotHeight() {
    return wifiContentHeight() / WIFI_SAVED_PAGE_SIZE;
}

// 扫网列表：扣除底栏后，剩余高度均分给每页条数
static int wifiScanListHeight() {
    return wifiContentHeight() - WIFI_SCAN_FOOTER_H;
}

static int wifiScanSlotHeight() {
    return wifiScanListHeight() / WIFI_SCAN_PAGE_SIZE;
}

// 已保存卡片：选中项才铺底色；可选描边；序号 x2 垂直居中，返回 label 起始 x
static int drawWifiItemCard(const int x, const int y, const int w, const int h, const int num,
                            const uint16_t accent, const uint16_t border, const bool draw_border,
                            const bool draw_bg) {
    const uint16_t card_bg = draw_bg ? wifiCardBg() : BLACK;
    if (draw_bg) {
        M5Cardputer.Display.fillRoundRect(x, y, w, h, 4, card_bg);
    }
    if (draw_border) {
        M5Cardputer.Display.drawRoundRect(x, y, w, h, 4, border);
    }

    constexpr int NUM_PAD_L = 10; // 序号左边 padding
    constexpr int NUM_LABEL_GAP = 10;
    char num_buf[4];
    snprintf(num_buf, sizeof(num_buf), "%d", num);
    const int num_h = infoLineHeight(2);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(accent, card_bg);
    M5Cardputer.Display.setCursor(x + NUM_PAD_L, y + (h - num_h) / 2);
    M5Cardputer.Display.print(num_buf);
    return x + NUM_PAD_L + M5Cardputer.Display.textWidth(num_buf) + NUM_LABEL_GAP;
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

// Help 按键说明；徽章后恢复说明文字颜色
static int drawWifiHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 12;
}

static int drawWifiHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 12;
}

// Help 功能说明
static int drawWifiHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// Help：单栏按键表
static void drawWifiHelpPage() {
    beginAppScreen("Help");
    int y = APP_CONTENT_Y_NO_TAP_TO_HEADER + 3;
    y = drawWifiHelpBadge(APP_CONTENT_X, y, "s/w", "saved <-> scan");
    y = drawWifiHelpKey(APP_CONTENT_X, y, 'r', "connect / retry");
    y = drawWifiHelpKey(APP_CONTENT_X, y, 'p', "edit pass on fail");
    y = drawWifiHelpBadge(APP_CONTENT_X, y, "1-4", "pick saved / scan");
    y = drawWifiHelpBadge(APP_CONTENT_X, y, "Enter", "connect / list");
    y = drawWifiHelpBadge(APP_CONTENT_X, y, "Bksp", "del saved");
    y = drawWifiHelpBadge(APP_CONTENT_X, y, "Fn+Q", "leave password");
    // 一行放两个徽章：方向键移动光标 / 括号翻页
    {
        int x = APP_CONTENT_X;
        x += drawTextBadge(x, y, ";,./", 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("move  ");
        x = M5Cardputer.Display.getCursorX();
        x += drawTextBadge(x, y, "[]", 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("page");
        y += 12;
    }

    drawHelpHintRight("close");
}

static void drawWifiStatusScreen() {
    beginWifiScreen();
    M5Cardputer.Display.setTextSize(2);

    int y = WIFI_CONTENT_TOP;
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
}

// 已保存网络列表：一页 3 条主页风格卡片
static void drawWifiSavedScreen() {
    // Hub 式 header：无下边框 + 右上角分页圆点
    beginAppHubScreen(wifiPageName(), BLACK, wifiSavedPage, getWifiSavedPageCount());

    const AppConfig& cfg = getAppConfig();
    const int slot_h = wifiSavedSlotHeight();
    const int card_h = slot_h - WIFI_CARD_GAP;
    const int start = wifiSavedPage * WIFI_SAVED_PAGE_SIZE;
    const int end = start + WIFI_SAVED_PAGE_SIZE < cfg.wifi_count ? start + WIFI_SAVED_PAGE_SIZE
                                                                  : cfg.wifi_count;
    const int screen_w = M5Cardputer.Display.width();
    const int card_x = APP_HUB_CARD_ORIGIN_X;
    const int card_w = screen_w - card_x * 2;
    constexpr int ROW_RIGHT_GAP = 6;
    constexpr int SSID_STATUS_GAP = 4;
    const int ssid_h = infoLineHeight(2);
    const int ip_h = infoLineHeight(1);
    constexpr int SSID_IP_GAP = 2;

    // 当前 STA 已连上的 SSID（用于匹配列表项）
    const bool sta_up = WiFi.status() == WL_CONNECTED;
    char connected_ssid[33] = "";
    if (sta_up) {
        strncpy(connected_ssid, WiFi.SSID().c_str(), sizeof(connected_ssid) - 1);
        connected_ssid[sizeof(connected_ssid) - 1] = '\0';
    }

    for (int i = start; i < end; i++) {
        const int row = i - start;
        const int card_y = WIFI_CONTENT_TOP + row * slot_h;
        const bool is_active = strcmp(cfg.wifis[i].ssid, cfg.wifi_active) == 0;
        const bool is_connected = sta_up && strcmp(connected_ssid, cfg.wifis[i].ssid) == 0;
        const bool is_connecting = wifiPhase == WifiAppPhase::CONNECTING && wifiConnectFromConfig &&
                                   is_active && !is_connected;

        const bool is_sel = i == wifiSavedSel;

        const uint16_t accent =
            is_connected ? APP_COLOR_OK : (is_connecting ? APP_COLOR_WARN : wifiCardAccentGold());
        // 仅选中项铺底色；active / 已连上绿框，光标项黄框优先
        const bool show_border = is_sel || is_active || is_connected;
        const uint16_t border =
            is_sel ? YELLOW : ((is_active || is_connected) ? APP_COLOR_OK : accent);
        // 序号 x2 居中，返回与 label 间距 10px 后的起始 x
        const int name_x = drawWifiItemCard(card_x, card_y, card_w, card_h, row + 1, accent, border,
                                            show_border, is_sel);

        const uint16_t card_bg = is_sel ? wifiCardBg() : BLACK;
        int name_max_w = card_x + card_w - ROW_RIGHT_GAP - name_x;

        if (is_connected) {
            name_max_w -= ICON_WIFI_W + 4;
        } else if (is_connecting) {
            M5Cardputer.Display.setTextSize(1);
            name_max_w -= M5Cardputer.Display.textWidth("connecting") + SSID_STATUS_GAP;
        } else if (wifiStatus[0] != '\0' && is_active && !sta_up) {
            M5Cardputer.Display.setTextSize(1);
            name_max_w -= M5Cardputer.Display.textWidth(wifiStatus) + SSID_STATUS_GAP;
        }

        // 内容块垂直居中：单行 SSID，或 SSID + IP
        const int block_h = is_connected ? (ssid_h + SSID_IP_GAP + ip_h) : ssid_h;
        const int block_y = card_y + (card_h - block_h) / 2;
        const int ssid_y = block_y;
        char ssid[33];
        M5Cardputer.Display.setTextSize(2);
        truncateTextToWidth(cfg.wifis[i].ssid, ssid, sizeof(ssid), name_max_w);
        M5Cardputer.Display.setTextColor(
            is_active || is_connected ? APP_COLOR_OK : wifiCardTitleColor(), card_bg);
        M5Cardputer.Display.setCursor(name_x, ssid_y);
        M5Cardputer.Display.print(ssid);
        const int ssid_w = M5Cardputer.Display.textWidth(ssid);

        if (is_connecting) {
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_WARN, card_bg);
            M5Cardputer.Display.setCursor(name_x + ssid_w + SSID_STATUS_GAP, ssid_y + 4);
            M5Cardputer.Display.print("connecting");
        } else if (is_connected) {
            const int signal_x = card_x + card_w - ROW_RIGHT_GAP - ICON_WIFI_W;
            drawIconWifi(signal_x, card_y + (card_h - ICON_WIFI_H) / 2, WiFi.RSSI(), WHITE);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, card_bg);
            M5Cardputer.Display.setCursor(name_x, ssid_y + ssid_h + SSID_IP_GAP);
            M5Cardputer.Display.print(WiFi.localIP().toString().c_str());
        } else if (wifiStatus[0] != '\0' && is_active && !sta_up) {
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_WARN, card_bg);
            M5Cardputer.Display.setCursor(name_x + ssid_w + SSID_STATUS_GAP, ssid_y + 4);
            M5Cardputer.Display.print(wifiStatus);
        }
    }

    if (cfg.wifi_count == 0) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, WIFI_CONTENT_TOP);
        M5Cardputer.Display.println("no saved WiFi");
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, WIFI_CONTENT_TOP + 14);
        M5Cardputer.Display.println("press s to scan");
    }
}

// 扫网列表：纯文本行（黄字序号 x2 + SSID + 右侧 RSSI/信号）；底栏条数/页码
static void drawWifiListScreen() {
    beginWifiScreen();

    const int start = wifiListPage * WIFI_SCAN_PAGE_SIZE;
    const int end = start + WIFI_SCAN_PAGE_SIZE < wifiScanCount ? start + WIFI_SCAN_PAGE_SIZE
                                                                : wifiScanCount;
    const int content_right = M5Cardputer.Display.width() - APP_CONTENT_X;
    constexpr int ROW_RIGHT_GAP = 2;
    constexpr int RSSI_SIGNAL_GAP = 4;
    const int num_h = infoLineHeight(2);
    const int list_h = wifiScanListHeight();
    const int slot_h = wifiScanSlotHeight();

    for (int i = start; i < end; i++) {
        const int row = i - start;
        const int row_y = WIFI_CONTENT_TOP + row * slot_h;
        const int rssi = WiFi.RSSI(i);
        const bool locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        // 右侧：信号图标贴边，RSSI 在其左
        const int signal_x = content_right - ICON_WIFI_W;
        drawIconWifi(signal_x, row_y + (slot_h - ICON_WIFI_H) / 2, rssi, WHITE);

        char rssi_buf[8];
        snprintf(rssi_buf, sizeof(rssi_buf), "%d", rssi);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
        const int rssi_w = M5Cardputer.Display.textWidth(rssi_buf);
        const int rssi_x = signal_x - RSSI_SIGNAL_GAP - rssi_w;
        M5Cardputer.Display.setCursor(rssi_x, row_y + (slot_h - 8) / 2);
        M5Cardputer.Display.print(rssi_buf);

        // 左侧序号 x2（如 1.）
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(YELLOW, BLACK);
        const int text_y = row_y + (slot_h - num_h) / 2;
        M5Cardputer.Display.setCursor(APP_CONTENT_X, text_y);
        char num_buf[4];
        snprintf(num_buf, sizeof(num_buf), "%d.", row + 1);
        M5Cardputer.Display.print(num_buf);
        const int num_w = M5Cardputer.Display.textWidth(num_buf);

        // SSID：序号与 RSSI 之间
        const int name_x = APP_CONTENT_X + num_w + ROW_RIGHT_GAP;
        const int lock_w = locked ? M5Cardputer.Display.textWidth("*") : 0;
        const int name_max_w = rssi_x - ROW_RIGHT_GAP - name_x - lock_w;
        char ssid[33];
        truncateTextToWidth(WiFi.SSID(i).c_str(), ssid, sizeof(ssid), name_max_w);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(name_x, text_y);
        M5Cardputer.Display.print(ssid);
        if (locked) {
            M5Cardputer.Display.print("*");
        }

        // 条目间分隔线（最后一条不画）
        if (i + 1 < end) {
            M5Cardputer.Display.drawFastHLine(APP_CONTENT_X, row_y + slot_h - 1,
                                              content_right - APP_CONTENT_X,
                                              wifiListSeparatorColor());
        }
    }

    if (wifiScanCount == 0) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(ORANGE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, WIFI_CONTENT_TOP);
        M5Cardputer.Display.println("no network");
    }

    // 底栏：列表总数 + 页码（列表区已均分剩余高度）
    const int footer_y = WIFI_CONTENT_TOP + list_h + (WIFI_SCAN_FOOTER_H - 8) / 2;
    const int page_count = getWifiListPageCount();
    char footer_buf[24];
    snprintf(footer_buf, sizeof(footer_buf), "%d  %d/%d", wifiScanCount, wifiListPage + 1,
             page_count);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, footer_y);
    M5Cardputer.Display.print(footer_buf);
}

// 按键提示项：徽章 + 说明（徽章后恢复说明文字颜色），返回下一项起始 x
static int drawWifiKeyHintItem(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(text);
    return cx + M5Cardputer.Display.textWidth(text) + 6;
}

static int drawWifiBadgeHintItem(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(text);
    return cx + M5Cardputer.Display.textWidth(text) + 6;
}

// 内容区卡片 x / 宽（密码、连接、失败页共用）
static int wifiCardX() {
    return APP_HUB_CARD_ORIGIN_X;
}
static int wifiCardW() {
    return M5Cardputer.Display.width() - wifiCardX() * 2;
}

// SSID 卡片：信号图标 + 名称（密码 / 连接 / 失败页共用），返回卡片下沿 y
static int drawWifiSsidCard(const int y, const char* ssid_text, const int rssi,
                            const uint16_t border) {
    const int card_x = wifiCardX();
    const int card_w = wifiCardW();
    const uint16_t card_bg = wifiCardBg();
    M5Cardputer.Display.fillRoundRect(card_x, y, card_w, WIFI_SSID_CARD_H, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(card_x, y, card_w, WIFI_SSID_CARD_H, 4, border);

    const int icon_x = card_x + 8;
    drawIconWifi(icon_x, y + (WIFI_SSID_CARD_H - ICON_WIFI_H) / 2, rssi, WHITE);

    const int name_x = icon_x + ICON_WIFI_W + 8;
    char ssid[33];
    M5Cardputer.Display.setTextSize(2);
    truncateTextToWidth(ssid_text != nullptr && ssid_text[0] != '\0' ? ssid_text : "?", ssid,
                        sizeof(ssid), card_x + card_w - 8 - name_x);
    M5Cardputer.Display.setTextColor(wifiCardTitleColor(), card_bg);
    M5Cardputer.Display.setCursor(name_x, y + (WIFI_SSID_CARD_H - infoLineHeight(2)) / 2);
    M5Cardputer.Display.print(ssid);
    return y + WIFI_SSID_CARD_H;
}

// 密码页编辑区布局（局部刷新需要固定位置）
static constexpr int WIFI_PASS_INPUT_H = 28;
static constexpr int WIFI_PASS_INPUT_PAD_X = 8;
static constexpr int WIFI_PASS_CARET_W = 3;

static int wifiPassLabelY() {
    return WIFI_CONTENT_TOP + WIFI_SSID_CARD_H + 6;
}
static int wifiPassInputY() {
    return wifiPassLabelY() + 12;
}

// 密码编辑区局部重绘：标签行 + 输入框 + 状态行
// 每按一个键只刷这一块，不动 header / SSID 卡片 / 底栏提示
static void drawWifiPasswordEditArea() {
    const int card_x = wifiCardX();
    const int card_w = wifiCardW();
    const uint16_t card_bg = wifiCardBg();
    const uint16_t gold = wifiCardAccentGold();
    const int label_y = wifiPassLabelY();
    const int input_y = wifiPassInputY();
    const size_t pass_len = strlen(wifiPassword);

    const int area_h = input_y + WIFI_PASS_INPUT_H + 14 - label_y;
    M5Cardputer.Display.fillRect(0, label_y, M5Cardputer.Display.width(), area_h, BLACK);

    // 标签行：password + 已输入字符数
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(card_x + 2, label_y);
    M5Cardputer.Display.print("password");
    char count_buf[8];
    snprintf(count_buf, sizeof(count_buf), "%u", static_cast<unsigned>(pass_len));
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(card_x + card_w - 2 - M5Cardputer.Display.textWidth(count_buf),
                                  label_y);
    M5Cardputer.Display.print(count_buf);

    // 输入框：明文 + 光标，超宽时跟随尾部滚动
    M5Cardputer.Display.fillRoundRect(card_x, input_y, card_w, WIFI_PASS_INPUT_H, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(card_x, input_y, card_w, WIFI_PASS_INPUT_H, 4,
                                      pass_len > 0 ? APP_COLOR_OK : gold);

    const int text_x = card_x + WIFI_PASS_INPUT_PAD_X;
    const int text_max_w = card_w - WIFI_PASS_INPUT_PAD_X * 2 - WIFI_PASS_CARET_W;
    M5Cardputer.Display.setTextSize(2);
    const char* tail = wifiPassword;
    while (*tail != '\0' && M5Cardputer.Display.textWidth(tail) > text_max_w) {
        tail++;
    }
    const int text_y = input_y + (WIFI_PASS_INPUT_H - infoLineHeight(2)) / 2;
    M5Cardputer.Display.setTextColor(WHITE, card_bg);
    M5Cardputer.Display.setCursor(text_x, text_y);
    M5Cardputer.Display.print(tail);
    M5Cardputer.Display.fillRect(text_x + M5Cardputer.Display.textWidth(tail) + 1, text_y,
                                 WIFI_PASS_CARET_W, infoLineHeight(2), gold);

    if (wifiStatus[0] != '\0') {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_WARN, BLACK);
        M5Cardputer.Display.setCursor(card_x + 2, input_y + WIFI_PASS_INPUT_H + 5);
        M5Cardputer.Display.print(wifiStatus);
    }
}

// 密码页：SSID 卡片 + 输入框 + 按键提示（整页重绘，进入 / 返回时用）
static void drawWifiPasswordScreen() {
    beginWifiScreen(false);

    const bool has_pick = wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount;
    drawWifiSsidCard(WIFI_CONTENT_TOP, has_pick ? WiFi.SSID(wifiSelectedIdx).c_str() : "?",
                     has_pick ? WiFi.RSSI(wifiSelectedIdx) : -100, wifiCardAccentGold());

    drawWifiPasswordEditArea();

    // 底栏按键提示：字母键会被当成密码字符，返回用 Fn+Q
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_CONTENT_X;
    cx = drawWifiBadgeHintItem(cx, hint_y, "Ent", "join");
    cx = drawWifiBadgeHintItem(cx, hint_y, "Del", "bk");
    cx += drawTextBadge(cx, hint_y, "Fn", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y + 1);
    M5Cardputer.Display.print("+");
    cx += M5Cardputer.Display.textWidth("+") + 1;
    drawWifiKeyHintItem(cx, hint_y, 'q', "back");
}

// 连接页各行 y（进度条动画需要局部重绘，位置集中在此）
static int wifiConnectLabelY() {
    return WIFI_CONTENT_TOP + WIFI_SSID_CARD_H + 8;
}
static int wifiConnectTrackY() {
    return wifiConnectLabelY() + 12;
}

// 进度条内部：来回滑动的高亮段（仅重绘轨道内部，避免整屏闪烁）
static void drawWifiConnectProgress() {
    const int card_x = wifiCardX();
    const int card_w = wifiCardW();
    const int track_y = wifiConnectTrackY();
    M5Cardputer.Display.fillRect(card_x + 1, track_y + 1, card_w - 2, WIFI_PROGRESS_H - 2,
                                 wifiCardBg());

    const int travel = card_w - 2 - WIFI_PROGRESS_SEG_W;
    int pos = travel > 0 ? wifiConnectAnimPos % (travel * 2) : 0;
    if (pos > travel) {
        pos = travel * 2 - pos; // 折返
    }
    M5Cardputer.Display.fillRect(card_x + 1 + pos, track_y + 1, WIFI_PROGRESS_SEG_W,
                                 WIFI_PROGRESS_H - 2, wifiCardAccentGold());
}

// 剩余秒数（右上角），仅重绘自身区域
static void drawWifiConnectCountdown() {
    const int card_x = wifiCardX();
    const int card_w = wifiCardW();
    const int label_y = wifiConnectLabelY();
    const int32_t left_ms = static_cast<int32_t>(wifiConnectDeadline - millis());
    wifiConnectLeftSec = left_ms > 0 ? (left_ms + 999) / 1000 : 0;

    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", wifiConnectLeftSec);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillRect(card_x + card_w - 26, label_y, 26, 8, BLACK);
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(card_x + card_w - M5Cardputer.Display.textWidth(buf), label_y);
    M5Cardputer.Display.print(buf);
}

// 扫描项加密方式短名
static const char* wifiAuthName(const int enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/2";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/3";
        default:
            return "ENT";
    }
}

// 连接页：SSID 卡片 + 状态行 + 滑动进度条 + 按键提示
static void drawWifiConnectingScreen() {
    beginWifiScreen(false);

    const int card_x = wifiCardX();
    const int card_w = wifiCardW();
    drawWifiSsidCard(WIFI_CONTENT_TOP, wifiTargetSsid, wifiTargetRssi, wifiCardAccentGold());

    const int label_y = wifiConnectLabelY();
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(card_x, label_y);
    M5Cardputer.Display.print("connecting");

    const int track_y = wifiConnectTrackY();
    M5Cardputer.Display.drawRect(card_x, track_y, card_w, WIFI_PROGRESS_H, wifiCardAccentGold());
    drawWifiConnectProgress();
    drawWifiConnectCountdown();

    // 目标热点详情：信号 + 加密 + 信道
    if (wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount) {
        const int detail_y = track_y + WIFI_PROGRESS_H + 8;
        char buf[24];
        snprintf(buf, sizeof(buf), "%d dBm", wifiTargetRssi);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
        M5Cardputer.Display.setCursor(card_x, detail_y);
        M5Cardputer.Display.print(buf);

        snprintf(buf, sizeof(buf), "%s  ch%d", wifiAuthName(WiFi.encryptionType(wifiSelectedIdx)),
                 static_cast<int>(WiFi.channel(wifiSelectedIdx)));
        M5Cardputer.Display.setCursor(card_x + card_w - M5Cardputer.Display.textWidth(buf),
                                      detail_y);
        M5Cardputer.Display.print(buf);
    }

    // 底栏按键提示
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_CONTENT_X;
    cx = drawWifiKeyHintItem(cx, hint_y, 'c', "cancel");
    drawWifiKeyHintItem(cx, hint_y, 'r', "retry");
}

// 失败页：SSID 卡片 + 红框错误卡片 + 重试/返回提示
static void drawWifiFailedScreen() {
    beginWifiScreen(false);

    const int card_x = wifiCardX();
    const int card_w = wifiCardW();
    const uint16_t card_bg = wifiCardBg();
    drawWifiSsidCard(WIFI_CONTENT_TOP, wifiTargetSsid, wifiTargetRssi, APP_COLOR_ERROR);

    constexpr int ERR_CARD_H = 40;
    const int err_y = WIFI_CONTENT_TOP + WIFI_SSID_CARD_H + 6;
    M5Cardputer.Display.fillRoundRect(card_x, err_y, card_w, ERR_CARD_H, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(card_x, err_y, card_w, ERR_CARD_H, 4, APP_COLOR_ERROR);

    // 左侧圆形叹号：手绘竖线 + 圆点，保证在圆内居中
    constexpr int BADGE_R = 9;
    const int badge_cx = card_x + 4 + BADGE_R + 2;
    const int badge_cy = err_y + ERR_CARD_H / 2;
    M5Cardputer.Display.fillCircle(badge_cx, badge_cy, BADGE_R, APP_COLOR_ERROR);
    M5Cardputer.Display.fillRect(badge_cx - 1, badge_cy - 6, 2, 7, WHITE);
    M5Cardputer.Display.fillRect(badge_cx - 1, badge_cy + 3, 2, 2, WHITE);

    const int text_x = badge_cx + BADGE_R + 8;
    const int text_max_w = card_x + card_w - 6 - text_x;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_ERROR, card_bg);
    M5Cardputer.Display.setCursor(text_x, err_y + 10);
    M5Cardputer.Display.print("connect failed");

    char reason[40];
    truncateTextToWidth(wifiFailReason, reason, sizeof(reason), text_max_w);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, card_bg);
    M5Cardputer.Display.setCursor(text_x, err_y + 22);
    M5Cardputer.Display.print(reason);

    // 底栏：重试 / 改密码 / 回列表（需手动按键，不自动返回）
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_CONTENT_X;
    cx = drawWifiKeyHintItem(cx, hint_y, 'r', "retry");
    cx = drawWifiKeyHintItem(cx, hint_y, 'p', "pass");
    drawWifiBadgeHintItem(cx, hint_y, "Ent", "list");
}

static void showWifiSavedList() {
    clampWifiSavedSel();
    wifiPhase = WifiAppPhase::SAVED;
    drawWifiSavedScreen();
}

// 使用 config 中当前 active WiFi 发起连接
static void startWifiConfigConnect() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        strncpy(wifiStatus, "no config", sizeof(wifiStatus));
        wifiPhase = WifiAppPhase::SAVED;
        drawWifiSavedScreen();
        return;
    }

    if (isWifiConfigConnected()) {
        claimStaWifi();
        wifiConnectFromConfig = false;
        wifiPhase = WifiAppPhase::SAVED;
        wifiStatus[0] = '\0';
        drawWifiSavedScreen();
        return;
    }

    claimStaWifi();
    WiFi.mode(WIFI_STA);
    applyWifiRadioSleepPolicy();
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    wifiSelectedIdx = -1;
    wifiConnectFromConfig = true;
    wifiPhase = WifiAppPhase::CONNECTING;
    wifiConnectStartMs = millis();
    wifiConnectDeadline = wifiConnectStartMs + WIFI_CONNECT_TIMEOUT_MS;
    strncpy(wifiTargetSsid, cfg.wifi_ssid, sizeof(wifiTargetSsid) - 1);
    wifiTargetSsid[sizeof(wifiTargetSsid) - 1] = '\0';
    wifiStatus[0] = '\0';
    // 停留在已保存列表，对应项显示 connecting
    drawWifiSavedScreen();
}

// 连接已保存项（全局下标）：设为 active 并连接
static void selectSavedWifiAt(const int idx) {
    const AppConfig& cfg = getAppConfig();
    if (idx < 0 || idx >= cfg.wifi_count) {
        return;
    }

    wifiSavedSel = idx;
    wifiSavedPage = idx / WIFI_SAVED_PAGE_SIZE;
    const char* ssid = cfg.wifis[idx].ssid;
    if (!setAppConfigWifiActive(ssid)) {
        strncpy(wifiStatus, "switch fail", sizeof(wifiStatus));
        wifiPhase = WifiAppPhase::SAVED;
        drawWifiSavedScreen();
        return;
    }
    startWifiConfigConnect();
}

// 数字键选当前页第 list_index 项
static void selectSavedWifi(const int list_index) {
    selectSavedWifiAt(wifiSavedPage * WIFI_SAVED_PAGE_SIZE + list_index);
}

// backspace：删除选中的已保存档案
static void deleteSelectedSavedWifi() {
    const AppConfig& cfg = getAppConfig();
    if (wifiSavedSel < 0 || wifiSavedSel >= cfg.wifi_count) {
        return;
    }

    char ssid[33];
    strncpy(ssid, cfg.wifis[wifiSavedSel].ssid, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    // 删的就是当前连着的网络：断开，避免列表状态与实连不一致
    const bool drop_link = WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid;

    if (!removeAppConfigWifi(ssid)) {
        strncpy(wifiStatus, "del fail", sizeof(wifiStatus));
        drawWifiSavedScreen();
        return;
    }
    if (drop_link) {
        WiFi.disconnect();
    }
    wifiStatus[0] = '\0';
    clampWifiSavedSel();
    drawWifiSavedScreen();
}

static void startWifiScan() {
    wifiPhase = WifiAppPhase::SCANNING;
    wifiListPage = 0;
    wifiSelectedIdx = -1;
    wifiStatus[0] = '\0';
    wifiFailReason[0] = '\0';

    beginWifiScreen();
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, WIFI_CONTENT_TOP);
    M5Cardputer.Display.println("scanning");

    claimStaWifi();
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
    claimStaWifi();
    WiFi.mode(WIFI_STA);
    applyWifiRadioSleepPolicy();
    // 切换 SSID 时才断开，避免频繁 disconnect 导致连接超时
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() != ssid) {
        WiFi.disconnect();
    }
    WiFi.begin(ssid.c_str(), password);

    wifiConnectFromConfig = false;
    wifiPhase = WifiAppPhase::CONNECTING;
    wifiConnectStartMs = millis();
    wifiConnectDeadline = wifiConnectStartMs + WIFI_JOIN_TIMEOUT_MS;
    wifiConnectAnimMs = wifiConnectStartMs;
    wifiConnectAnimPos = 0;
    wifiConnectLeftSec = -1;
    strncpy(wifiTargetSsid, ssid.c_str(), sizeof(wifiTargetSsid) - 1);
    wifiTargetSsid[sizeof(wifiTargetSsid) - 1] = '\0';
    wifiTargetRssi = WiFi.RSSI(wifiSelectedIdx);
    wifiStatus[0] = '\0';
    wifiFailReason[0] = '\0';
    drawWifiConnectingScreen();
}

// 手动连接失败：停在失败页提示，不污染 switcher 列表状态
static void failWifiConnect(const char* reason) {
    WiFi.disconnect();
    strncpy(wifiFailReason, reason, sizeof(wifiFailReason) - 1);
    wifiFailReason[sizeof(wifiFailReason) - 1] = '\0';
    wifiStatus[0] = '\0';
    wifiConnectFromConfig = false;
    wifiPhase = WifiAppPhase::FAILED;
    drawWifiFailedScreen();
}

// 失败/取消后回扫网列表（无扫描结果则回已保存列表）
static void backToWifiScanList() {
    wifiFailReason[0] = '\0';
    wifiStatus[0] = '\0';
    if (wifiScanCount > 0) {
        wifiPhase = WifiAppPhase::LIST;
        drawWifiListScreen();
        return;
    }
    wifiPhase = WifiAppPhase::SAVED;
    drawWifiSavedScreen();
}

static void selectWifiNetwork(const int list_index) {
    const int idx = wifiListPage * WIFI_SCAN_PAGE_SIZE + list_index;
    if (idx < 0 || idx >= wifiScanCount) {
        return;
    }

    wifiSelectedIdx = idx;
    wifiPassword[0] = '\0';
    wifiStatus[0] = '\0';
    wifiConnectFromConfig = false;

    // 若已保存过该 SSID，预填密码
    {
        const AppConfig& cfg = getAppConfig();
        const String ssid = WiFi.SSID(idx);
        for (int i = 0; i < cfg.wifi_count; i++) {
            if (ssid == cfg.wifis[i].ssid) {
                strncpy(wifiPassword, cfg.wifis[i].password, WIFI_PASS_MAX);
                wifiPassword[WIFI_PASS_MAX] = '\0';
                break;
            }
        }
    }

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
    wifiSavedPage = 0;
    wifiSelectedIdx = -1;
    wifiPassword[0] = '\0';
    wifiStatus[0] = '\0';
    wifiConnectFromConfig = false;
    wifiHelpVisible = false;
    wifiFailReason[0] = '\0';
    wifiTargetSsid[0] = '\0';
    wifiTargetRssi = -100;

    const AppConfig& cfg = getAppConfig();

    // 光标默认落在当前 active 档案上
    wifiSavedSel = 0;
    for (int i = 0; i < cfg.wifi_count; i++) {
        if (strcmp(cfg.wifis[i].ssid, cfg.wifi_active) == 0) {
            wifiSavedSel = i;
            break;
        }
    }
    clampWifiSavedSel();

    // 优先显示已保存列表；有 active 且未连上则后台连接
    wifiPhase = WifiAppPhase::SAVED;
    if (cfg.loaded && cfg.wifi_ssid[0] != '\0' && !isWifiConfigConnected()) {
        startWifiConfigConnect();
        return;
    }
    drawWifiApp();
}

// 各页都带 header，定时状态刷新照常
bool wifiAppSuppressesHeader() {
    return false;
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
        case WifiAppPhase::SAVED:
            drawWifiSavedScreen();
            break;
        case WifiAppPhase::SCANNING:
            beginWifiScreen();
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(APP_CONTENT_X, WIFI_CONTENT_TOP);
            M5Cardputer.Display.println("scanning");
            break;
        case WifiAppPhase::LIST:
            drawWifiListScreen();
            break;
        case WifiAppPhase::PASSWORD:
            drawWifiPasswordScreen();
            break;
        case WifiAppPhase::CONNECTING:
            // 从已保存列表发起的连接：留在列表页显示 connecting
            if (wifiConnectFromConfig) {
                drawWifiSavedScreen();
            } else {
                drawWifiConnectingScreen();
            }
            break;
        case WifiAppPhase::FAILED:
            drawWifiFailedScreen();
            break;
    }
}

void updateWifiApp() {
    if (wifiPhase != WifiAppPhase::CONNECTING) {
        return;
    }

    // 手动连接页：滑动进度条 + 剩余秒数
    if (!wifiConnectFromConfig && !wifiHelpVisible) {
        const uint32_t now = millis();
        if (now - wifiConnectAnimMs >= WIFI_PROGRESS_TICK_MS) {
            wifiConnectAnimMs = now;
            wifiConnectAnimPos += WIFI_PROGRESS_STEP_PX;
            drawWifiConnectProgress();
            const int32_t left_ms = static_cast<int32_t>(wifiConnectDeadline - now);
            const int left_sec = left_ms > 0 ? (left_ms + 999) / 1000 : 0;
            if (left_sec != wifiConnectLeftSec) {
                drawWifiConnectCountdown();
            }
        }
    }

    if (WiFi.status() == WL_CONNECTED &&
        (!wifiConnectFromConfig || isWifiConfigConnected())) {
        if (!wifiConnectFromConfig) {
            // 扫网连上：upsert 到 wifis[] 并设为 active
            if (saveAppConfigWifi(WiFi.SSID().c_str(), wifiPassword)) {
                wifiStatus[0] = '\0';
            } else {
                strncpy(wifiStatus, "full/fail", sizeof(wifiStatus));
            }
        } else {
            wifiStatus[0] = '\0';
        }
        wifiConnectFromConfig = false;
        wifiPhase = WifiAppPhase::SAVED;
        drawWifiApp();
        return;
    }

    // 手动连接：认证/找不到 AP 可提前判失败，跳过 begin 初期的瞬时状态
    if (!wifiConnectFromConfig && millis() - wifiConnectStartMs >= WIFI_FAIL_GRACE_MS) {
        const wl_status_t st = WiFi.status();
        if (st == WL_CONNECT_FAILED) {
            failWifiConnect("wrong password?");
            return;
        }
        if (st == WL_NO_SSID_AVAIL) {
            failWifiConnect("AP not found");
            return;
        }
    }

    if (static_cast<int32_t>(millis() - wifiConnectDeadline) >= 0) {
        // 手动连接失败留在失败页；档案连接才在列表项上标 timeout
        if (!wifiConnectFromConfig) {
            failWifiConnect("timed out, AP no response");
            return;
        }
        strncpy(wifiStatus, "timeout", sizeof(wifiStatus));
        wifiConnectFromConfig = false;
        wifiPhase = WifiAppPhase::SAVED;
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

    if (wifiPhase == WifiAppPhase::FAILED) {
        if (status.enter) {
            backToWifiScanList();
            return;
        }
        for (const char c : status.word) {
            if (c == 'r' || c == 'R') {
                if (wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount) {
                    startWifiConnect(wifiPassword);
                }
                return;
            }
            // 改密码重来
            if (c == 'p' || c == 'P') {
                if (wifiSelectedIdx >= 0 && wifiSelectedIdx < wifiScanCount) {
                    wifiFailReason[0] = '\0';
                    wifiStatus[0] = '\0';
                    wifiPhase = WifiAppPhase::PASSWORD;
                    drawWifiPasswordScreen();
                }
                return;
            }
            if (c == 0x1B || c == 'w' || c == 'W') {
                backToWifiScanList();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::CONNECTING) {
        for (const char c : status.word) {
            // 连接中仍可用 1-3 改连其它档案
            if (wifiConnectFromConfig && c >= '1' && c <= '3') {
                WiFi.disconnect();
                selectSavedWifi(c - '1');
                return;
            }
            if (c == 'c' || c == 'C') {
                WiFi.disconnect();
                const bool from_config = wifiConnectFromConfig;
                wifiConnectFromConfig = false;
                wifiStatus[0] = '\0';
                // 手动连接取消后回扫网列表，档案连接回已保存列表
                if (!from_config) {
                    backToWifiScanList();
                    return;
                }
                wifiPhase = WifiAppPhase::SAVED;
                drawWifiSavedScreen();
                return;
            }
            if (c == 'r' || c == 'R') {
                if (wifiConnectFromConfig) {
                    startWifiConfigConnect();
                } else if (wifiSelectedIdx >= 0) {
                    startWifiConnect(wifiPassword);
                }
            }
            if (wifiConnectFromConfig && (c == 's' || c == 'S')) {
                WiFi.disconnect();
                wifiConnectFromConfig = false;
                wifiStatus[0] = '\0';
                startWifiScan();
                return;
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
                    showWifiSavedList();
                }
                return;
            }
            if (c == 'c' || c == 'C') {
                showWifiSavedList();
                return;
            }
            if (c == 's' || c == 'S') {
                startWifiScan();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::SAVED) {
        // 方向键移动光标（跨页循环），[ ] 仍整页翻
        const int nav = getMenuNavDelta(status);
        if (nav != 0) {
            moveWifiSavedSel(nav);
            drawWifiSavedScreen();
            return;
        }

        const int page_nav = getBracketNavDelta(status);
        if (page_nav != 0) {
            const int page_count = getWifiSavedPageCount();
            wifiSavedPage = (wifiSavedPage + page_nav + page_count) % page_count;
            wifiSavedSel = wifiSavedPage * WIFI_SAVED_PAGE_SIZE;
            clampWifiSavedSel();
            drawWifiSavedScreen();
            return;
        }

        if (status.enter) {
            selectSavedWifiAt(wifiSavedSel);
            return;
        }

        if (status.del) {
            deleteSelectedSavedWifi();
            return;
        }

        for (const char c : status.word) {
            if (c == '\b') {
                deleteSelectedSavedWifi();
                return;
            }
            if (c >= '1' && c <= '3') {
                selectSavedWifi(c - '1');
                return;
            }
            if (c == 'r' || c == 'R') {
                const AppConfig& cfg = getAppConfig();
                if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
                    startWifiConfigConnect();
                }
                return;
            }
            if (c == 's' || c == 'S') {
                startWifiScan();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::LIST) {
        int nav = getMenuNavDelta(status);
        if (nav == 0) {
            nav = getBracketNavDelta(status);
        }
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
            // w / Esc：回到已保存列表
            if (c == 0x1B || c == 'w' || c == 'W') {
                wifiPhase = WifiAppPhase::SAVED;
                drawWifiSavedScreen();
                return;
            }
        }
        return;
    }

    if (wifiPhase == WifiAppPhase::PASSWORD) {
        // 字母都要留给密码，返回上一层用 Fn+Q
        if (status.fn) {
            for (const char c : status.word) {
                if (c == 'q' || c == 'Q') {
                    wifiPhase = WifiAppPhase::LIST;
                    drawWifiListScreen();
                    return;
                }
            }
            return;
        }

        // 输入过程只刷编辑区，避免整屏重绘闪烁
        if (status.del) {
            backspaceWifiPassword();
            drawWifiPasswordEditArea();
            return;
        }

        if (status.space) {
            appendWifiPasswordChar(' ');
            drawWifiPasswordEditArea();
            return;
        }

        if (status.enter) {
            startWifiConnect(wifiPassword);
            return;
        }

        for (const char c : status.word) {
            if (c == '\b') {
                backspaceWifiPassword();
                drawWifiPasswordEditArea();
                return;
            }
            if (c == 0x1B) {
                wifiPhase = WifiAppPhase::LIST;
                drawWifiListScreen();
                return;
            }
            appendWifiPasswordChar(c);
        }

        if (!status.word.empty()) {
            drawWifiPasswordEditArea();
        }
    }
}
