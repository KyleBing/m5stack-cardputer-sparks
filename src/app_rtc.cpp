#include "app_rtc.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_countdown.h"
#include "app_header.h"
#include "app_stopwatch.h"
#include "app_time_ui.h"
#include <WiFi.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>

enum class TimeMode {
    UPTIME,
    CLOCK,
    COUNTDOWN,
    STOPWATCH,
};

static TimeMode timeMode = TimeMode::UPTIME;
static bool timeHelpVisible = false;
static bool timePureVisible = false;
static bool rtcSyncTimedOut = false;
static bool rtcScreenReady = false;
static bool uptimeScreenReady = false;
static bool clockSyncedOnce = false;
static char rtcLastTime[16] = "";
static char rtcLastDate[20] = "";
static char rtcLastSrc[8] = "";
static char pureLastDate[20] = "";
static BigTimeState uptimeTimeState{};
static BigTimeState uptimePureTimeState{};
static BigTimeState pureTimeState{};
static constexpr int RTC_TIME_TEXT_SIZE = 4;
static constexpr int RTC_DATE_TEXT_SIZE = 2;
static constexpr int RTC_PURE_DATE_TEXT_SIZE = 2;
static constexpr int RTC_PURE_DATE_LINE_H = 8 * RTC_PURE_DATE_TEXT_SIZE;
static constexpr int RTC_PURE_TIME_DATE_GAP = 4;
static constexpr int RTC_TIME_LINE_H = 8 * RTC_TIME_TEXT_SIZE;
static constexpr int RTC_DATE_LINE_H = 8 * RTC_DATE_TEXT_SIZE;
static constexpr int RTC_TIME_BOTTOM_MARGIN = 5;
static constexpr int RTC_FAIL_TEXT_SIZE = 2;
static constexpr uint32_t RTC_SYNC_TIMEOUT_MS = 10000;  // 超时时间 10 seconds
static constexpr uint32_t UPTIME_UPDATE_MS = 1000;       // 更新间隔 1 second

static void drawUptimeApp(const bool full_init);
static void drawRtcApp(const bool full_init);
static void drawRtcPureApp(const bool full_init);
static void drawUptimePureApp(const bool full_init);
static void drawTimePureApp(const bool full_init);

static int clockContentHeight() {
    int area_y = 0;
    int area_h = 0;
    getTimeDisplayArea(area_y, area_h);
    return area_h;
}

static int rtcTimeY() {
    const int block_h = RTC_TIME_LINE_H + RTC_TIME_BOTTOM_MARGIN + RTC_DATE_LINE_H;
    const int avail_h = clockContentHeight();
    return APP_CONTENT_Y + TIME_TAG_H + (avail_h - block_h) / 2;
}

static int rtcDateY() {
    return rtcTimeY() + RTC_TIME_LINE_H + RTC_TIME_BOTTOM_MARGIN;
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

static void updateRtcTimeText(const char* time_buf) {
    if (strcmp(time_buf, rtcLastTime) == 0) {
        return;
    }
    const int y = rtcTimeY();
    M5Cardputer.Display.fillRect(0, y, M5Cardputer.Display.width(), RTC_TIME_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(RTC_TIME_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(rtcCenteredX(time_buf, RTC_TIME_TEXT_SIZE), y);
    M5Cardputer.Display.print(time_buf);
    strncpy(rtcLastTime, time_buf, sizeof(rtcLastTime) - 1);
    rtcLastTime[sizeof(rtcLastTime) - 1] = '\0';
}

static void updateRtcDateText(const char* date_buf) {
    if (strcmp(date_buf, rtcLastDate) == 0) {
        return;
    }
    const int y = rtcDateY();
    M5Cardputer.Display.fillRect(0, y, M5Cardputer.Display.width(), RTC_DATE_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(RTC_DATE_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(rtcCenteredX(date_buf, RTC_DATE_TEXT_SIZE), y);
    M5Cardputer.Display.print(date_buf);
    strncpy(rtcLastDate, date_buf, sizeof(rtcLastDate) - 1);
    rtcLastDate[sizeof(rtcLastDate) - 1] = '\0';
}

static void updateRtcSourceTag(const char* source) {
    if (strcmp(source, rtcLastSrc) == 0) {
        return;
    }
    drawTimeModeTag(source);
    strncpy(rtcLastSrc, source, sizeof(rtcLastSrc) - 1);
    rtcLastSrc[sizeof(rtcLastSrc) - 1] = '\0';
}

static void drawClockBottomHints() {
    const KeyHintItem items[] = {{'r', "sync"}, {'p', "pure"}};
    drawTimeBottomHints(items, 2);
}

// 估算按键提示项宽度
static int timeMeasureKeyHintItem(const KeyHintItem& item, const int text_size) {
    const int size = (text_size == 2) ? 2 : 1;
    const char letter = static_cast<char>(toupper(static_cast<unsigned char>(item.key)));
    const char str[2] = {letter, '\0'};
    M5Cardputer.Display.setTextSize(size);
    const int badge_w = M5Cardputer.Display.textWidth(str) + 4 + 3;
    M5Cardputer.Display.setTextSize(text_size);
    return badge_w + M5Cardputer.Display.textWidth(item.text);
}

// 绘制单个按键提示，返回占用宽度
static int timeDrawKeyHintItem(const int x, const int y, const KeyHintItem& item,
                               const int text_size, const uint16_t color) {
    int cx = x + drawKeyBadge(x, y, item.key, text_size);
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(item.text);
    return cx + M5Cardputer.Display.textWidth(item.text) - x;
}

// Help 页内容区高度
static int timeHelpContentHeight() {
    return M5Cardputer.Display.height() - TIME_HINT_ROW_H - APP_CONTENT_Y;
}

// x2 组：在上方区域均分行 y
static int timeHelpX2RowY(const int row, const int total_x2_rows) {
    constexpr int x1_rows = 2;
    constexpr int x1_gap = 4;
    constexpr int group_gap = 6;
    const int x1_block_h = x1_rows * INFO_LINE_H + (x1_rows - 1) * x1_gap;
    const int x2_block_h = timeHelpContentHeight() - x1_block_h - group_gap;
    const int slot_h = x2_block_h / total_x2_rows;
    return APP_CONTENT_Y + row * slot_h + (slot_h - INFO_LINE_H_2X) / 2;
}

// x1 组：在剩余区域紧凑排列
static int timeHelpX1RowY(const int row) {
    constexpr int x1_rows = 2;
    constexpr int x1_gap = 4;
    constexpr int group_gap = 6;
    const int x1_block_h = x1_rows * INFO_LINE_H + (x1_rows - 1) * x1_gap;
    const int x2_block_h = timeHelpContentHeight() - x1_block_h - group_gap;
    const int x1_top = APP_CONTENT_Y + x2_block_h + group_gap;
    return x1_top + row * (INFO_LINE_H + x1_gap);
}

// x2 组：按屏宽换行绘制按键提示，返回下一行索引
static int drawTimeHelpX2KeyHints(const int x, int row, const int total_x2_rows,
                                  const KeyHintItem* items, const int item_count,
                                  const int text_size, const uint16_t color, const int max_w) {
    if (items == nullptr || item_count <= 0) {
        return row;
    }

    int cx = x;
    int y = timeHelpX2RowY(row, total_x2_rows);
    M5Cardputer.Display.setTextSize(text_size);
    const int space_w = M5Cardputer.Display.textWidth(" ");

    for (int i = 0; i < item_count; i++) {
        const int item_w = timeMeasureKeyHintItem(items[i], text_size);
        if (cx > x && cx + item_w > x + max_w) {
            row++;
            y = timeHelpX2RowY(row, total_x2_rows);
            cx = x;
        }
        cx += timeDrawKeyHintItem(cx, y, items[i], text_size, color);
        if (i != item_count - 1) {
            M5Cardputer.Display.setCursor(cx, y);
            M5Cardputer.Display.print(" ");
            cx += space_w;
        }
    }
    return row + 1;
}

static void drawTimeHelpScreen() {
    beginAppScreen("Help");
    constexpr int ts = 2;
    constexpr int x2_rows = 3;
    const int max_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;

    static const KeyHintItem modes[] = {
        {'u', "uptime"},
        {'t', "clock"},
        {'c', "cd"},
        {'s', "sw"},
        {'r', "sync"},
    };
    int row = drawTimeHelpX2KeyHints(APP_CONTENT_X, 0, x2_rows, modes, 5, ts, APP_COLOR_HINT,
                                     max_w);

    static const KeyHintItem pure_items[] = {{'p', "pure"}};
    drawTimeHelpX2KeyHints(APP_CONTENT_X, row, x2_rows, pure_items, 1, ts, APP_COLOR_HINT,
                           max_w);

    constexpr int hint_ts = 1;
    int y = timeHelpX1RowY(0);
    int cx = APP_CONTENT_X;
    M5Cardputer.Display.setTextSize(hint_ts);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("cd/sw ");
    cx += M5Cardputer.Display.textWidth("cd/sw ");
    cx += drawTextBadge(cx, y, "BtnA", hint_ts);
    cx += drawTextBadge(cx, y, "sp", hint_ts);
    cx += drawTextBadge(cx, y, "ent", hint_ts);

    y = timeHelpX1RowY(1);
    M5Cardputer.Display.setTextSize(hint_ts);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print("start/pause");

    drawTimeBottomHints(nullptr, 0, "back");
}

static void redrawCurrentTimeMode() {
    timeHelpVisible = false;
    if (timePureVisible) {
        drawTimePureApp(true);
        return;
    }
    switch (timeMode) {
        case TimeMode::UPTIME:
            drawUptimeApp(true);
            break;
        case TimeMode::CLOCK:
            drawRtcApp(true);
            break;
        case TimeMode::COUNTDOWN:
            redrawCountdownApp();
            break;
        case TimeMode::STOPWATCH:
            redrawStopwatchApp();
            break;
    }
}

static void drawRtcBusyScreen(const char* msg) {
    beginAppScreen("Time");
    rtcScreenReady = true;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';
    drawTimeModeTag("CLK");
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + TIME_TAG_H + 4);
    M5Cardputer.Display.println(msg);
    drawTimeBottomHints(nullptr, 0);
}

static bool trySyncNtpTime(const uint32_t deadline_ms) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // NTP 只返回 UTC；显示时区来自 config（缺省 CST-8）
    const char* tz = getAppTimezone();
    configTzTime(tz, "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");

    struct tm timeinfo{};
    while (static_cast<int32_t>(millis() - deadline_ms) < 0) {
        if (getLocalTime(&timeinfo, 200)) {
            if (M5.Rtc.isEnabled()) {
                // 硬件 RTC 按 UTC 存：与 M5 setSystemTimeFromRtc 约定一致
                const time_t now = time(nullptr);
                struct tm utc{};
                gmtime_r(&now, &utc);
                M5.Rtc.setDateTime(&utc);
                M5.Rtc.setSystemTimeFromRtc();
                // setSystemTimeFromRtc 会临时改 TZ，写回本地时区
                applyLocalTimezone();
            }
            // 同步成功后把时区写入 config.json
            saveAppConfigTimezone(tz);
            return true;
        }
        delay(100);
    }
    return false;
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

// 首次有效时间或首次 NTP 成功后不再自动同步；按 r 强制刷新
static bool syncClockTimeIfNeeded(const bool force) {
    if (!force && clockSyncedOnce) {
        return hasValidClockTime();
    }
    if (!force && hasValidClockTime()) {
        clockSyncedOnce = true;
        return true;
    }

    rtcSyncTimedOut = false;
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
        return hasValidClockTime();
    }

    const uint32_t deadline = millis() + RTC_SYNC_TIMEOUT_MS;
    drawRtcBusyScreen("wifi connecting...");

    const uint32_t remain = deadline - millis();
    const bool wifi_ok = remain > 0 && ensureConfigWifi(remain);
    if (!wifi_ok) {
        rtcSyncTimedOut = static_cast<int32_t>(millis() - deadline) >= 0;
        releaseConfigWifi();
        return hasValidClockTime();
    }
    if (static_cast<int32_t>(millis() - deadline) < 0) {
        drawRtcBusyScreen("ntp syncing...");
        if (trySyncNtpTime(deadline)) {
            clockSyncedOnce = true;
        } else {
            rtcSyncTimedOut = static_cast<int32_t>(millis() - deadline) >= 0;
        }
    } else {
        rtcSyncTimedOut = true;
    }
    releaseConfigWifi();
    return hasValidClockTime();
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
    splitTimeMs(millis(), hours, minutes, seconds, frac);

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

static void drawRtcPureApp(const bool full_init) {
    struct tm timeinfo{};
    const char* source = "none";
    if (!readCurrentTime(timeinfo, source)) {
        timePureVisible = false;
        drawRtcApp(true);
        return;
    }

    char date_buf[20];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1, timeinfo.tm_mday);

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

static void drawRtcApp(const bool full_init) {
    struct tm timeinfo{};
    const char* source = "none";
    if (!readCurrentTime(timeinfo, source)) {
        if (!full_init && rtcScreenReady) {
            return;
        }
        beginAppScreen("Time");
        rtcScreenReady = true;
        uptimeScreenReady = false;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
        drawTimeModeTag("CLK");
        int y = APP_CONTENT_Y + TIME_TAG_H;
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
        drawClockBottomHints();
        return;
    }

    char time_buf[16];
    char date_buf[20];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min,
             timeinfo.tm_sec);
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1, timeinfo.tm_mday);

    if (full_init || !rtcScreenReady) {
        beginAppScreen("Time");
        rtcScreenReady = true;
        uptimeScreenReady = false;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
        drawClockBottomHints();
    }
    updateRtcTimeText(time_buf);
    updateRtcDateText(date_buf);
    updateRtcSourceTag(source);
}

static void drawUptimeApp(const bool full_init) {
    int area_y = 0;
    int area_h = 0;
    getTimeDisplayArea(area_y, area_h);

    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frac = 0;
    splitTimeMs(millis(), hours, minutes, seconds, frac);

    if (full_init || !uptimeScreenReady) {
        beginAppScreen("Time");
        uptimeScreenReady = true;
        rtcScreenReady = false;
        uptimeTimeState = BigTimeState{};
        drawTimeModeTag("UP");
        const KeyHintItem items[] = {{'p', "pure"}};
        drawTimeBottomHints(items, 1);
    }

    drawBigTimeDisplay(uptimeTimeState, area_y, area_h, hours, minutes, seconds, frac, false,
                       full_init || uptimeTimeState.ts <= 0);
}

static void enterClockMode(const bool force_sync) {
    timeMode = TimeMode::CLOCK;
    rtcScreenReady = false;
    uptimeScreenReady = false;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';
    syncClockTimeIfNeeded(force_sync);
    if (timePureVisible) {
        drawRtcPureApp(true);
    } else {
        drawRtcApp(true);
    }
}

static void enterTimeMode(const TimeMode mode) {
    timeHelpVisible = false;
    if (mode == TimeMode::CLOCK) {
        timeMode = TimeMode::CLOCK;
        rtcScreenReady = false;
        uptimeScreenReady = false;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
        syncClockTimeIfNeeded(false);
        if (timePureVisible) {
            drawRtcPureApp(true);
        } else {
            drawRtcApp(true);
        }
        return;
    }

    timeMode = mode;
    if (mode == TimeMode::UPTIME) {
        uptimeScreenReady = false;
        uptimeTimeState = BigTimeState{};
        if (timePureVisible) {
            drawUptimePureApp(true);
        } else {
            drawUptimeApp(true);
        }
    } else if (mode == TimeMode::COUNTDOWN) {
        enterCountdownApp();
    } else if (mode == TimeMode::STOPWATCH) {
        enterStopwatchApp();
    }
}

void enterRtcApp() {
    timePureVisible = getAppConfig().time_pure;
    uptimeScreenReady = false;
    uptimeTimeState = BigTimeState{};
    rtcScreenReady = false;

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

void updateRtcApp() {
    if (timeHelpVisible) {
        return;
    }
    if (timePureVisible) {
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
                static uint32_t last_pure_clock_ms = 0;
                if (millis() - last_pure_clock_ms >= UPTIME_UPDATE_MS) {
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
        return;
    }
    switch (timeMode) {
        case TimeMode::UPTIME: {
            static uint32_t last_uptime_ms = 0;
            if (millis() - last_uptime_ms >= UPTIME_UPDATE_MS) {
                last_uptime_ms = millis();
                drawUptimeApp(false);
            }
            break;
        }
        case TimeMode::CLOCK: {
            static uint32_t last_clock_ms = 0;
            if (millis() - last_clock_ms >= UPTIME_UPDATE_MS) {
                last_clock_ms = millis();
                drawRtcApp(false);
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
    const char key = timePressedLetter(status);

    if (timePureVisible) {
        if (key == 'p') {
            timePureVisible = false;
            saveAppConfigTimePure(false);
            redrawCurrentTimeMode();
            return;
        }
        if (key == 'h') {
            timePureVisible = false;
            timeHelpVisible = true;
            drawTimeHelpScreen();
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
            syncClockTimeIfNeeded(true);
            drawRtcPureApp(true);
            return;
        }
        if (timeMode == TimeMode::COUNTDOWN) {
            handleCountdownApp(status);
        } else if (timeMode == TimeMode::STOPWATCH) {
            handleStopwatchApp(status);
        }
        return;
    }

    if (key == 'h') {
        if (timeHelpVisible) {
            redrawCurrentTimeMode();
        } else {
            timeHelpVisible = true;
            drawTimeHelpScreen();
        }
        return;
    }
    if (timeHelpVisible) {
        if (key == 'h') {
            redrawCurrentTimeMode();
            return;
        }
        if (key != 'u' && key != 't' && key != 'c' && key != 's') {
            return;
        }
        timeHelpVisible = false;
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
        enterClockMode(true);
        return;
    }
    if (key == 'p') {
        timePureVisible = true;
        saveAppConfigTimePure(true);
        drawTimePureApp(true);
        return;
    }

    if (timeMode == TimeMode::COUNTDOWN) {
        handleCountdownApp(status);
    } else if (timeMode == TimeMode::STOPWATCH) {
        handleStopwatchApp(status);
    }
}

bool isTimePureMode() {
    return timePureVisible;
}

void presentCountdownAlarmUi() {
    timeHelpVisible = false;
    timeMode = TimeMode::COUNTDOWN;
    // 到期页需看到取消提示；跟随已保存的 pure 偏好
    timePureVisible = getAppConfig().time_pure;
    enterCountdownApp();
}

bool isTimeCountdownUiActive() {
    return timeMode == TimeMode::COUNTDOWN && !timeHelpVisible;
}
