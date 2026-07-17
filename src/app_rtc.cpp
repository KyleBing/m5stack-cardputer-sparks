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
    // 顶部留给 NTP/RTC 来源标签
    const int block_h = RTC_TIME_LINE_H + RTC_TIME_BOTTOM_MARGIN + RTC_DATE_LINE_H;
    const int avail_h = clockContentHeight() - TIME_TAG_H;
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

// Help 分栏标题
static int drawTimeHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// Help 按键说明；徽章后恢复说明文字颜色
static int drawTimeHelpKey(const int x, const int y, const char key, const char* text) {
    const int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawTimeHelpBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// Help 功能说明
static int drawTimeHelpText(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static void drawTimeHelpScreen() {
    beginAppScreen("Help");
    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    int y = drawTimeHelpColHeader(0, col_y, col_w, "keymap");
    y = drawTimeHelpKey(2, y, 'u', "uptime");
    y = drawTimeHelpKey(2, y, 't', "clock");
    y = drawTimeHelpKey(2, y, 'c', "countdown");
    y = drawTimeHelpKey(2, y, 's', "stopwatch");
    y = drawTimeHelpKey(2, y, 'p', "pure mode");
    y = drawTimeHelpKey(2, y, 'r', "sync / reset");
    y = drawTimeHelpBadge(2, y, "BtnA", "start / pause");

    y = drawTimeHelpColHeader(manual_x, col_y, screen_w - manual_x, "manual");
    y = drawTimeHelpText(manual_x + 2, y, "uptime since boot");
    y = drawTimeHelpText(manual_x + 2, y, "clock RTC / NTP");
    y = drawTimeHelpText(manual_x + 2, y, "countdown + alarm");
    y = drawTimeHelpText(manual_x + 2, y, "stopwatch 1 ms");
    y = drawTimeHelpText(manual_x + 2, y, "CD/SW run in bg");
    y = drawTimeHelpText(manual_x + 2, y, "while device awake");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
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
    beginAppScreenAccent("Time ", "CLK", APP_COLOR_LABEL);
    rtcScreenReady = true;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 4);
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
        beginAppScreenAccent("Time ", "CLK", APP_COLOR_LABEL);
        rtcScreenReady = true;
        uptimeScreenReady = false;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
        int y = APP_CONTENT_Y;
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
        beginAppScreenAccent("Time ", "CLK", APP_COLOR_LABEL);
        rtcScreenReady = true;
        uptimeScreenReady = false;
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
        drawClockBottomHints();
    }
    updateRtcTimeText(time_buf);
    updateRtcDateText(date_buf);
    updateRtcSourceTag(source); // 内容区顶部：NTP / RTC
}

static void drawUptimeApp(const bool full_init) {
    int area_y = 0;
    int area_h = 0;
    getTimeDisplayArea(area_y, area_h);

    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frac = 0;
    // esp_timer 在 light sleep 期间继续计时，millis() 会停表
    splitTimeMs(static_cast<uint64_t>(esp_timer_get_time() / 1000LL), hours, minutes, seconds,
                frac);

    if (full_init || !uptimeScreenReady) {
        beginAppScreenAccent("Time ", "UP", APP_COLOR_LABEL);
        uptimeScreenReady = true;
        rtcScreenReady = false;
        uptimeTimeState = BigTimeState{};
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
            // 先退出 Pure 界面，再异步写配置，避免卡在保存上
            timePureVisible = false;
            redrawCurrentTimeMode();
            saveAppConfigTimePure(false);
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
        // 先进入 Pure 界面，再写配置，避免 FS 保存拖慢切换
        timePureVisible = true;
        drawTimePureApp(true);
        saveAppConfigTimePure(true);
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
