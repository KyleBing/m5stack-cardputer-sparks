#include "app_rtc.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_countdown.h"
#include "app_header.h"
#include "app_stopwatch.h"
#include "app_time_ui.h"
#include <WiFi.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <time.h>

enum class TimeMode {
    UPTIME,
    CLOCK,
    COUNTDOWN,
    STOPWATCH,
};

static TimeMode timeMode = TimeMode::UPTIME;
static bool timeHelpVisible = false;
static bool rtcPureLargeClockVisible = false;
static bool rtcSyncTimedOut = false;
static bool rtcScreenReady = false;
static bool clockSyncedOnce = false;
static char pureLastDate[24] = "";
static char pureLargeClockLastTime[8] = "";
static BigTimeState uptimePureTimeState{};
static BigTimeState pureTimeState{};
static uint32_t timeModeLabelUntilMs = 0; // 左上角模式提示截止时间；0=未显示
static bool timeModeLabelVisible = false;
static uint32_t timeLastActivityMs = 0; // 最近按键/切模式，用于空闲降频
static constexpr int RTC_PURE_DATE_TEXT_SIZE = 2;
static constexpr int RTC_PURE_DATE_LINE_H = 8 * RTC_PURE_DATE_TEXT_SIZE;
static constexpr int RTC_PURE_TIME_DATE_GAP = 4;
static constexpr int RTC_FAIL_TEXT_SIZE = 2;
static constexpr uint32_t RTC_SYNC_TIMEOUT_MS = 10000;  // WiFi 连接超时
static constexpr uint32_t RTC_NTP_TIMEOUT_MS = 8000;    // NTP 单独超时（WiFi 成功后）
static constexpr uint32_t RTC_WIFI_RETRY_MS = 3500;     // 与 ensureStaWifi 一致：主动重发 begin
static constexpr uint32_t UPTIME_UPDATE_MS = 1000;       // 更新间隔 1 second
static constexpr uint32_t BIG_CLOCK_UPDATE_MS = 15000;    // big time 仅 HH:MM，活跃时 15s 检查一次
static constexpr uint32_t TIME_IDLE_SLOW_MS = 60000;    // 无操作 1 分钟后主循环 1s 一拍
static constexpr uint32_t TIME_MODE_LABEL_MS = 3000;    // 切换模式提示显示时长
static constexpr int TIME_MODE_LABEL_X = 2;
static constexpr int TIME_MODE_LABEL_Y = 2;
static constexpr int TIME_MODE_LABEL_H = 8; // text size 1
// 忙屏 / 错误页正文起点：避开左上角模式小字
static constexpr int TIME_TOP_CONTENT_Y = TIME_MODE_LABEL_Y + TIME_MODE_LABEL_H + 6;
// tm_wday: 0=Sun .. 6=Sat
static constexpr const char* RTC_WEEKDAY_ABBR[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// 后台 WiFi + NTP（不阻塞主循环，同步中可切模块 / 回菜单）
enum class RtcTimeSync : uint8_t {
    Idle = 0,
    BeginWifi,
    WaitWifi,
    BeginNtp,
    WaitNtp,
};
static RtcTimeSync g_rtc_sync = RtcTimeSync::Idle;
static uint32_t g_rtc_sync_deadline_ms = 0;
static uint32_t g_rtc_wifi_retry_ms = 0;
static uint32_t g_rtc_header_ms = 0; // 同步中刷新 header WiFi 图标
static const char* g_rtc_busy_msg = nullptr; // 同步中提示文案

static void drawRtcApp(const bool full_init);
static void drawRtcPureApp(const bool full_init);
static void drawRtcPureLargeClockApp(const bool full_init);
static void drawUptimePureApp(const bool full_init);
static void drawTimePureApp(const bool full_init);
static void drawTimeModeLabelOverlay();
static void showTimeModeLabel();
static void noteTimeActivity();
static bool rtcSyncBusy();
static void abortClockSync();
static void finishClockSync(bool ok, bool timed_out);
static void startClockSync(bool force);
static void updateClockSync();
static bool applyNtpResultToRtc();

static void noteTimeActivity() {
    timeLastActivityMs = millis();
}

static char timePressedLetter(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c >= 'a' && c <= 'z') {
            return c;
        }
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c - 'A' + 'a');
        }
    }
    return '\0';
}

static int rtcCenteredX(const char* text, const int text_size) {
    M5Cardputer.Display.setTextSize(text_size);
    const int tw = M5Cardputer.Display.textWidth(text);
    return (M5Cardputer.Display.width() - tw) / 2;
}

// Help 行高：徽章高度 10px 的 1.3 倍（见 APP_HELP_LINE_H）

// 彩色按键徽章（模块入口行用）
static int drawTimeHelpColoredKey(const int x, const int y, const char key, const uint16_t bg) {
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(key)));
    const char str[2] = {letter, '\0'};
    M5Cardputer.Display.setTextSize(1);
    const int tw = M5Cardputer.Display.textWidth(str);
    constexpr int pad_x = 2;
    constexpr int pad_y = 1;
    const int bw = tw + pad_x * 2;
    const int bh = 8 + pad_y * 2;
    M5Cardputer.Display.fillRoundRect(x, y, bw, bh, 2, bg);
    M5Cardputer.Display.setTextColor(APP_COLOR_KEY_TEXT, bg);
    M5Cardputer.Display.setCursor(x + pad_x, y + pad_y);
    M5Cardputer.Display.print(str);
    return bw + 3;
}

// 一排模块入口：彩色首字母键 + 剩余字母（如 T + ime）
static void drawTimeHelpModulesRow(const int y) {
    struct Entry {
        char key;
        const char* rest; // 名称去掉首字母后的部分
        uint16_t color;
    };
    // u/t/c/s 与切模式热键一致；键底分色便于扫读
    const Entry entries[] = {
        {'u', "ptime", APP_COLOR_LABEL},
        {'t', "ime", APP_COLOR_VALUE},
        {'c', "ountdown", APP_COLOR_WARN},
        {'s', "topwatch", APP_COLOR_OK},
    };
    int cx = APP_HELP_CONTENT_X;
    for (const Entry& e : entries) {
        cx += drawTimeHelpColoredKey(cx, y, e.key, e.color);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复说明色
        M5Cardputer.Display.setCursor(cx, y + 1);
        M5Cardputer.Display.print(e.rest);
        cx = M5Cardputer.Display.getCursorX() + 6;
    }
}

// 模式切换时左上角小字提示用的名称
static const char* timeModeLabelName() {
    switch (timeMode) {
        case TimeMode::UPTIME:
            return "uptime";
        case TimeMode::CLOCK:
            return "clock";
        case TimeMode::COUNTDOWN:
            return "countdown";
        case TimeMode::STOPWATCH:
            return "stopwatch";
    }
    return "";
}

// 绘制/清除左上角模式提示（超时后擦掉）
static void drawTimeModeLabelOverlay() {
    if (timeHelpVisible) {
        return;
    }
    if (timeModeLabelUntilMs == 0) {
        return;
    }
    if (static_cast<int32_t>(millis() - timeModeLabelUntilMs) >= 0) {
        if (timeModeLabelVisible) {
            M5Cardputer.Display.fillRect(TIME_MODE_LABEL_X, TIME_MODE_LABEL_Y,
                                         M5Cardputer.Display.width() / 2, TIME_MODE_LABEL_H,
                                         BLACK);
            timeModeLabelVisible = false;
        }
        timeModeLabelUntilMs = 0;
        return;
    }

    const char* name = timeModeLabelName();
    M5Cardputer.Display.setTextSize(1);
    // 主题蓝（与 Help / header accent 同色）
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(TIME_MODE_LABEL_X, TIME_MODE_LABEL_Y);
    M5Cardputer.Display.print(name);
    timeModeLabelVisible = true;
}

static void showTimeModeLabel() {
    timeModeLabelUntilMs = millis() + TIME_MODE_LABEL_MS;
    timeModeLabelVisible = false;
    drawTimeModeLabelOverlay();
}

static void drawTimeHelpScreen() {
    // 无 header：全屏黑底，风格对齐全局 Time Help
    int y = drawAppHelpBegin(timeModeLabelName());
    constexpr int x = APP_HELP_CONTENT_X;

    // 当前模块功能说明
    switch (timeMode) {
        case TimeMode::UPTIME:
            y = drawAppHelpText(x, y, "Shows time since device boot.");
            break;
        case TimeMode::CLOCK:
            y = drawAppHelpKey(x, y, 'r', "sync time over WiFi");
            y = drawAppHelpKey(x, y, 'b', "big clock");
            y = drawAppHelpText(x, y, "Uses RTC; sync source is NTP.");
            break;
        case TimeMode::COUNTDOWN:
            y = drawAppHelpBadge(x, y, "Arrows", "select / adjust field");
            y = drawAppHelpBadge(x, y, "0-9", "enter duration");
            y = drawAppHelpBadge(x, y, "BtnGO", "start / pause / resume");
            y = drawAppHelpKey(x, y, 'r', "reset countdown");
            y = drawAppHelpText(x, y, "Keeps running in background.");
            break;
        case TimeMode::STOPWATCH:
            y = drawAppHelpBadge(x, y, "BtnGO", "start / pause / resume");
            y = drawAppHelpKey(x, y, 'r', "reset stopwatch");
            y = drawAppHelpText(x, y, "1 ms display; runs in background.");
            break;
    }

    // 所有模块入口一排（彩色首字母 + 剩余字母），贴在底栏 close 上方
    const int modules_y = M5Cardputer.Display.height() - 12 - APP_HELP_LINE_H - 2;
    drawTimeHelpModulesRow(modules_y);

    drawHelpHintRight("close");
}

static void redrawCurrentTimeMode() {
    timeHelpVisible = false;
    drawTimePureApp(true);
    drawTimeModeLabelOverlay();
}

static void drawRtcBusyScreen(const char* msg) {
    // 同步中用共享 header，便于显示 WiFi 连接状态
    beginAppScreen("Clock");
    rtcScreenReady = true;
    g_rtc_busy_msg = msg;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_INSET_Y);
    M5Cardputer.Display.println(msg);
}

static bool rtcSyncBusy() {
    return g_rtc_sync == RtcTimeSync::BeginWifi || g_rtc_sync == RtcTimeSync::WaitWifi ||
           g_rtc_sync == RtcTimeSync::BeginNtp || g_rtc_sync == RtcTimeSync::WaitNtp;
}

static void abortClockSync() {
    if (!rtcSyncBusy()) {
        g_rtc_sync = RtcTimeSync::Idle;
        g_rtc_busy_msg = nullptr;
        return;
    }
    releaseConfigWifi();
    g_rtc_sync = RtcTimeSync::Idle;
    g_rtc_busy_msg = nullptr;
    clearAppHeaderStatusRefresh();
}

static bool readCurrentTime(struct tm& out, const char*& source) {
    // 保证 TZ 有效（deep sleep 重启后系统时钟可能仍在，但 TZ 会丢）
    applyLocalTimezone();

    if (M5.Rtc.isEnabled()) {
        const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
        if (dt.date.year >= 2020) {
            // RTC 存 UTC：临时用 GMT0 做 mktime，再按本地时区显示
            struct tm utc{};
            utc.tm_year = dt.date.year - 1900;
            utc.tm_mon = dt.date.month - 1;
            utc.tm_mday = dt.date.date;
            utc.tm_hour = dt.time.hours;
            utc.tm_min = dt.time.minutes;
            utc.tm_sec = dt.time.seconds;
            utc.tm_isdst = 0;
            setenv("TZ", "GMT0", 1);
            tzset();
            const time_t epoch = mktime(&utc);
            applyLocalTimezone();
            if (epoch > 1600000000 && localtime_r(&epoch, &out) != nullptr) {
                source = "RTC";
                return true;
            }
        }
    }

    const time_t now = time(nullptr);
    if (now > 1600000000) {
        localtime_r(&now, &out);
        source = "NTP";
        return true;
    }

    source = "none";
    return false;
}

static bool hasValidClockTime() {
    struct tm timeinfo{};
    const char* source = "none";
    return readCurrentTime(timeinfo, source);
}

// 将已完成的 SNTP 结果写入硬件 RTC / 时区配置
static bool applyNtpResultToRtc() {
    struct tm timeinfo{};
    if (!getLocalTime(&timeinfo, 0)) {
        return false;
    }
    const char* tz = getAppTimezone();
    if (M5.Rtc.isEnabled()) {
        // 硬件 RTC 按 UTC 存：与 M5 setSystemTimeFromRtc 约定一致
        const time_t now = time(nullptr);
        struct tm utc{};
        gmtime_r(&now, &utc);
        M5.Rtc.setDateTime(&utc);
        M5.Rtc.setSystemTimeFromRtc();
        // setSystemTimeFromRtc 会冲掉改 TZ，写回本地时区
        applyLocalTimezone();
    }
    saveAppConfigTimezone(tz);
    return true;
}

static void finishClockSync(const bool ok, const bool timed_out) {
    releaseConfigWifi();
    g_rtc_sync = RtcTimeSync::Idle;
    g_rtc_busy_msg = nullptr;
    // 回到 Pure 全屏时钟，停止刷 header 状态图标
    clearAppHeaderStatusRefresh();
    if (ok) {
        clockSyncedOnce = true;
        rtcSyncTimedOut = false;
    } else {
        rtcSyncTimedOut = timed_out;
    }
    // 仍在 Clock 页时刷新结果；已切走则只收尾 WiFi
    if (timeMode == TimeMode::CLOCK && !timeHelpVisible) {
        drawRtcPureApp(true);
        drawTimeModeLabelOverlay();
    }
}

// 启动后台同步；立刻返回，主循环可继续响应切模块
static void startClockSync(const bool force) {
    if (!force && clockSyncedOnce) {
        return;
    }
    if (!force && hasValidClockTime()) {
        clockSyncedOnce = true;
        return;
    }
    if (rtcSyncBusy()) {
        // 已在同步中：强制刷新则重启；否则沿用当前请求
        if (!force) {
            return;
        }
        abortClockSync();
    }

    rtcSyncTimedOut = false;
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        if (timeMode == TimeMode::CLOCK) {
            drawRtcApp(true);
        }
        return;
    }

    drawRtcBusyScreen("wifi connecting...");
    g_rtc_sync = RtcTimeSync::BeginWifi;
}

static void updateClockSync() {
    if (!rtcSyncBusy()) {
        return;
    }
    // 已离开 Clock：取消同步，避免后台占 WiFi
    if (timeMode != TimeMode::CLOCK) {
        abortClockSync();
        return;
    }

    const AppConfig& cfg = getAppConfig();
    switch (g_rtc_sync) {
        case RtcTimeSync::Idle:
            return;
        case RtcTimeSync::BeginWifi: {
            if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
                finishClockSync(false, false);
                return;
            }
            if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid) {
                claimStaWifi();
                g_rtc_sync = RtcTimeSync::BeginNtp;
                return;
            }
            claimStaWifi();
            if (WiFi.status() == WL_CONNECTED) {
                WiFi.disconnect(true);
                delay(50);
            }
            WiFi.mode(WIFI_STA);
            applyWifiRadioSleepPolicy();
            WiFi.setAutoReconnect(true);
            WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
            g_rtc_sync_deadline_ms = millis() + RTC_SYNC_TIMEOUT_MS;
            g_rtc_wifi_retry_ms = millis() + RTC_WIFI_RETRY_MS;
            g_rtc_sync = RtcTimeSync::WaitWifi;
            break;
        }
        case RtcTimeSync::WaitWifi:
            if (WiFi.status() == WL_CONNECTED) {
                g_rtc_sync = RtcTimeSync::BeginNtp;
            } else if (static_cast<int32_t>(millis() - g_rtc_sync_deadline_ms) >= 0) {
                finishClockSync(hasValidClockTime(), true);
            } else if (static_cast<int32_t>(millis() - g_rtc_wifi_retry_ms) >= 0) {
                // Arduino 自动重连不可靠，主动重发 begin
                WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
                g_rtc_wifi_retry_ms = millis() + RTC_WIFI_RETRY_MS;
            }
            break;
        case RtcTimeSync::BeginNtp: {
            // 已有系统/RTC 时间时 getLocalTime 会立刻成功，必须等 SNTP COMPLETED
            // SMOOTH 对 ≤35min 误差只渐调，强制 IMMED 立刻跳到线上时间
            const char* tz = getAppTimezone();
            sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
            configTzTime(tz, "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");
            g_rtc_busy_msg = "ntp syncing...";
            if (!timeHelpVisible) {
                drawRtcBusyScreen(g_rtc_busy_msg);
            }
            g_rtc_sync_deadline_ms = millis() + RTC_NTP_TIMEOUT_MS;
            g_rtc_sync = RtcTimeSync::WaitNtp;
            break;
        }
        case RtcTimeSync::WaitNtp:
            // get 读到 COMPLETED 后会清回 RESET，只判断一次即可
            if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                finishClockSync(applyNtpResultToRtc(), false);
            } else if (static_cast<int32_t>(millis() - g_rtc_sync_deadline_ms) >= 0) {
                finishClockSync(hasValidClockTime(), true);
            }
            break;
    }

    // 联网期间刷新 header WiFi 图标
    if (g_rtc_sync == RtcTimeSync::WaitWifi || g_rtc_sync == RtcTimeSync::WaitNtp) {
        if (millis() - g_rtc_header_ms >= 500) {
            g_rtc_header_ms = millis();
            updateAppHeaderStatus();
        }
    }
}

static void drawUptimePureApp(const bool full_init) {
    if (full_init) {
        M5Cardputer.Display.fillScreen(BLACK);
        uptimePureTimeState = BigTimeState{};
    }

    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frac = 0;
    // esp_timer 在 light sleep 期间继续计时，millis() 会停表
    splitTimeMs(static_cast<uint64_t>(esp_timer_get_time() / 1000LL), hours, minutes, seconds,
                frac);

    int area_y = 0;
    int area_h = 0;
    getTimePureDisplayArea(area_y, area_h);
    drawBigTimeDisplay(uptimePureTimeState, area_y, area_h, hours, minutes, seconds, frac, false,
                       full_init || uptimePureTimeState.ts <= 0);
}

static void drawTimePureApp(const bool full_init) {
    switch (timeMode) {
        case TimeMode::UPTIME:
            drawUptimePureApp(full_init);
            break;
        case TimeMode::CLOCK:
            drawRtcPureApp(full_init);
            break;
        case TimeMode::COUNTDOWN:
            redrawCountdownApp();
            break;
        case TimeMode::STOPWATCH:
            redrawStopwatchApp();
            break;
    }
}

static void drawRtcPureLargeClockApp(const bool full_init) {
    struct tm timeinfo{};
    const char* source = "none";
    if (!readCurrentTime(timeinfo, source)) {
        rtcPureLargeClockVisible = false;
        drawRtcApp(true);
        return;
    }

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    if (full_init) {
        M5Cardputer.Display.fillScreen(BLACK);
        pureLargeClockLastTime[0] = '\0';
    }

    if (full_init || strcmp(time_buf, pureLargeClockLastTime) != 0) {
        const int screen_w = M5Cardputer.Display.width();
        const int screen_h = M5Cardputer.Display.height();
        const int margin = APP_CONTENT_X;
        const int avail_w = screen_w - margin * 2;
        // 原字体放大：取能放下 HH:MM 的最大 textSize
        int ts = 1;
        for (int candidate = 12; candidate >= 1; candidate--) {
            M5Cardputer.Display.setTextSize(candidate);
            if (M5Cardputer.Display.textWidth(time_buf) <= avail_w &&
                8 * candidate <= screen_h) {
                ts = candidate;
                break;
            }
        }
        M5Cardputer.Display.setTextSize(ts);
        const int time_w = M5Cardputer.Display.textWidth(time_buf);
        const int time_h = 8 * ts;
        const int time_x = (screen_w - time_w) / 2;
        const int time_y = (screen_h - time_h) / 2;
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(time_x, time_y);
        M5Cardputer.Display.print(time_buf);
        strncpy(pureLargeClockLastTime, time_buf, sizeof(pureLargeClockLastTime) - 1);
        pureLargeClockLastTime[sizeof(pureLargeClockLastTime) - 1] = '\0';
    }
}

static void drawRtcPureApp(const bool full_init) {
    if (rtcPureLargeClockVisible) {
        drawRtcPureLargeClockApp(full_init);
        return;
    }

    // 同步进行中：保留忙屏，勿被 fail 页盖住
    if (rtcSyncBusy()) {
        if (full_init || g_rtc_busy_msg == nullptr) {
            drawRtcBusyScreen(g_rtc_busy_msg != nullptr ? g_rtc_busy_msg : "syncing...");
        }
        return;
    }

    struct tm timeinfo{};
    const char* source = "none";
    if (!readCurrentTime(timeinfo, source)) {
        drawRtcApp(true);
        return;
    }

    char date_buf[24];
    const int wday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday <= 6) ? timeinfo.tm_wday : 0;
    // 日期 + 星期简写
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d %s", timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1, timeinfo.tm_mday, RTC_WEEKDAY_ABBR[wday]);

    if (full_init) {
        M5Cardputer.Display.fillScreen(BLACK);
        pureTimeState = BigTimeState{};
        pureLastDate[0] = '\0';
    }

    const int screen_h = M5Cardputer.Display.height();
    const int date_block_h = RTC_PURE_DATE_LINE_H + RTC_PURE_TIME_DATE_GAP;
    const int area_y = 0;
    const int area_h = screen_h - date_block_h;
    drawBigTimeDisplay(pureTimeState, area_y, area_h, timeinfo.tm_hour, timeinfo.tm_min,
                       timeinfo.tm_sec, 0, false, full_init || pureTimeState.ts <= 0);

    if (full_init || strcmp(date_buf, pureLastDate) != 0) {
        const int date_y = pureTimeState.main_y + pureTimeState.main_h + RTC_PURE_TIME_DATE_GAP;
        M5Cardputer.Display.fillRect(0, date_y, M5Cardputer.Display.width(), RTC_PURE_DATE_LINE_H,
                                     BLACK);
        M5Cardputer.Display.setTextSize(RTC_PURE_DATE_TEXT_SIZE);
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.setCursor(rtcCenteredX(date_buf, RTC_PURE_DATE_TEXT_SIZE), date_y);
        M5Cardputer.Display.print(date_buf);
        strncpy(pureLastDate, date_buf, sizeof(pureLastDate) - 1);
        pureLastDate[sizeof(pureLastDate) - 1] = '\0';
    }
}

// Clock 未校时时的错误页（无 header）
static void drawRtcApp(const bool full_init) {
    if (!full_init && rtcScreenReady) {
        return;
    }
    M5Cardputer.Display.fillScreen(BLACK);
    rtcScreenReady = true;
    int y = TIME_TOP_CONTENT_Y; // 避开左上角模式小字
    drawInfoLineAt(APP_CONTENT_X, y, "time", "not set", RTC_FAIL_TEXT_SIZE);
    y += INFO_LINE_H_2X;
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        drawInfoLineAt(APP_CONTENT_X, y, "hint", "set WiFi cfg", RTC_FAIL_TEXT_SIZE);
    } else if (rtcSyncTimedOut) {
        drawInfoLineAt(APP_CONTENT_X, y, "hint", "timeout", RTC_FAIL_TEXT_SIZE);
    } else {
        drawInfoLineAt(APP_CONTENT_X, y, "hint", "wifi/ntp fail", RTC_FAIL_TEXT_SIZE);
    }
}

static void enterTimeMode(const TimeMode mode) {
    noteTimeActivity();
    timeHelpVisible = false;
    if (mode != TimeMode::CLOCK) {
        rtcPureLargeClockVisible = false;
        abortClockSync(); // 切走 Clock 时立刻停 NTP，不挡其它模块
    }
    if (mode == TimeMode::CLOCK) {
        timeMode = TimeMode::CLOCK;
        rtcScreenReady = false;
        startClockSync(false);
        // 未进同步忙屏时再画时钟 / 失败页
        if (!rtcSyncBusy()) {
            drawRtcPureApp(true);
        }
        showTimeModeLabel();
        return;
    }

    timeMode = mode;
    if (mode == TimeMode::UPTIME) {
        drawUptimePureApp(true);
    } else if (mode == TimeMode::COUNTDOWN) {
        enterCountdownApp();
    } else if (mode == TimeMode::STOPWATCH) {
        enterStopwatchApp();
    }
    showTimeModeLabel();
}

void enterRtcApp() {
    rtcPureLargeClockVisible = false;
    rtcScreenReady = false;
    timeModeLabelUntilMs = 0;
    timeModeLabelVisible = false;
    abortClockSync();
    noteTimeActivity();

    // 按配置进入默认模块
    switch (getAppConfig().time_default_mode) {
        case TimeDefaultMode::Ntp:
            enterTimeMode(TimeMode::CLOCK);
            break;
        case TimeDefaultMode::Countdown:
            enterTimeMode(TimeMode::COUNTDOWN);
            break;
        case TimeDefaultMode::Stopwatch:
            enterTimeMode(TimeMode::STOPWATCH);
            break;
        case TimeDefaultMode::Up:
        default:
            enterTimeMode(TimeMode::UPTIME);
            break;
    }
}

void leaveRtcApp() {
    abortClockSync();
}

void updateRtcApp() {
    // 后台推进 WiFi/NTP；同步中主循环仍可处理按键
    updateClockSync();
    if (timeHelpVisible) {
        return;
    }
    switch (timeMode) {
        case TimeMode::UPTIME: {
            static uint32_t last_pure_uptime_ms = 0;
            if (millis() - last_pure_uptime_ms >= UPTIME_UPDATE_MS) {
                last_pure_uptime_ms = millis();
                drawUptimePureApp(false);
            }
            break;
        }
        case TimeMode::CLOCK: {
            if (rtcSyncBusy()) {
                break; // 忙屏由 start/updateClockSync 维护
            }
            static uint32_t last_pure_clock_ms = 0;
            // big time 仅 HH:MM：活跃时长间隔检查；空闲 1s 一拍时改为每秒，整分切换及时
            const uint32_t interval =
                rtcPureLargeClockVisible
                    ? (isTimeIdleSlowLoop() ? UPTIME_UPDATE_MS : BIG_CLOCK_UPDATE_MS)
                    : UPTIME_UPDATE_MS;
            if (millis() - last_pure_clock_ms >= interval) {
                last_pure_clock_ms = millis();
                drawRtcPureApp(false);
            }
            break;
        }
        case TimeMode::COUNTDOWN:
            updateCountdownApp();
            break;
        case TimeMode::STOPWATCH:
            updateStopwatchApp();
            break;
    }
    // 模式提示可能被刷新覆盖，每帧补画；到期则擦除
    drawTimeModeLabelOverlay();
}

// BtnA 须每帧轮询：wasPressed 仅在按下边沿当帧为 true
void pollTimeAppBtnA() {
    if (timeHelpVisible) {
        return;
    }
    if (timeMode == TimeMode::COUNTDOWN) {
        pollCountdownBtnA();
    } else if (timeMode == TimeMode::STOPWATCH) {
        pollStopwatchBtnA();
    }
}

void handleTimeApp(const Keyboard_Class::KeysState& status) {
    noteTimeActivity();
    const char key = timePressedLetter(status);

    if (key == 'h') {
        if (timeHelpVisible) {
            redrawCurrentTimeMode();
        } else {
            rtcPureLargeClockVisible = false;
            timeHelpVisible = true;
            timeModeLabelUntilMs = 0;
            timeModeLabelVisible = false;
            drawTimeHelpScreen();
        }
        return;
    }
    if (timeHelpVisible) {
        if (key != 'u' && key != 't' && key != 'c' && key != 's') {
            return;
        }
        timeHelpVisible = false;
    }
    if (key == 'b' && timeMode == TimeMode::CLOCK) {
        // Clock 内切换点阵大字 HH:MM（无秒）
        rtcPureLargeClockVisible = !rtcPureLargeClockVisible;
        drawRtcPureApp(true);
        drawTimeModeLabelOverlay();
        return;
    }
    if (key == 'u') {
        enterTimeMode(TimeMode::UPTIME);
        return;
    }
    if (key == 't') {
        enterTimeMode(TimeMode::CLOCK);
        return;
    }
    if (key == 'c') {
        enterTimeMode(TimeMode::COUNTDOWN);
        return;
    }
    if (key == 's') {
        enterTimeMode(TimeMode::STOPWATCH);
        return;
    }
    if (key == 'r' && timeMode == TimeMode::CLOCK) {
        startClockSync(true);
        if (!rtcSyncBusy()) {
            drawRtcPureApp(true);
        }
        drawTimeModeLabelOverlay();
        return;
    }

    if (timeMode == TimeMode::COUNTDOWN) {
        handleCountdownApp(status);
    } else if (timeMode == TimeMode::STOPWATCH) {
        handleStopwatchApp(status);
    }
}

bool closeRtcHelp() {
    if (!timeHelpVisible) {
        return false;
    }
    redrawCurrentTimeMode();
    return true;
}

bool isTimeClockLikeMode() {
    // Uptime / Clock（含 big time）可降频；CD/SW 需更高刷新
    return !timeHelpVisible &&
           (timeMode == TimeMode::UPTIME || timeMode == TimeMode::CLOCK);
}

bool isTimeIdleSlowLoop() {
    // 同步中保持较快轮询，便于超时与重连
    if (rtcSyncBusy()) {
        return false;
    }
    return isTimeClockLikeMode() && (millis() - timeLastActivityMs) >= TIME_IDLE_SLOW_MS;
}

void presentCountdownAlarmUi() {
    timeHelpVisible = false;
    timeMode = TimeMode::COUNTDOWN;
    enterCountdownApp();
}

bool isTimeCountdownUiActive() {
    return timeMode == TimeMode::COUNTDOWN && !timeHelpVisible;
}
