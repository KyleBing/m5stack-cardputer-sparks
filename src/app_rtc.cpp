#include "app_rtc.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <time.h>

static bool rtcScreenReady = false;
static bool rtcSyncTimedOut = false;
static char rtcLastTime[16] = "";
static char rtcLastDate[20] = "";
static char rtcLastSrc[8] = "";
static constexpr int RTC_TIME_TEXT_SIZE = 4;  // 时间主体 4 倍字体
static constexpr int RTC_DATE_TEXT_SIZE = 2;
static constexpr int RTC_TIME_LINE_H = 8 * RTC_TIME_TEXT_SIZE;
static constexpr int RTC_DATE_LINE_H = 8 * RTC_DATE_TEXT_SIZE;
static constexpr int RTC_TIME_BOTTOM_MARGIN = 5;  // 时间主体下方间距
static constexpr int RTC_SRC_TEXT_SIZE = 1;
static constexpr int RTC_SRC_LINE_H = INFO_LINE_H;
static constexpr int RTC_FAIL_TEXT_SIZE = 2;
static constexpr uint32_t RTC_SYNC_TIMEOUT_MS = 5000;
static constexpr uint16_t RTC_SRC_COLOR = APP_COLOR_LABEL;  // 来源标识专用色

// 内容区高度（不含顶栏）
static int rtcContentHeight() {
    return M5Cardputer.Display.height() - APP_CONTENT_Y;
}

// 主时间垂直起始 y（时间+日期块在内容区居中，底部留给来源行）
static int rtcTimeY() {
    const int block_h = RTC_TIME_LINE_H + RTC_TIME_BOTTOM_MARGIN + RTC_DATE_LINE_H;
    const int avail_h = rtcContentHeight() - RTC_SRC_LINE_H - 4;
    return APP_CONTENT_Y + (avail_h - block_h) / 2;
}

static int rtcDateY() {
    return rtcTimeY() + RTC_TIME_LINE_H + RTC_TIME_BOTTOM_MARGIN;
}

// 来源行贴近内容区左下角
static int rtcSrcY() {
    return M5Cardputer.Display.height() - RTC_SRC_LINE_H - 2;
}

// 按字号水平居中
static int rtcCenteredX(const char* text, const int text_size) {
    M5Cardputer.Display.setTextSize(text_size);
    const int tw = M5Cardputer.Display.textWidth(text);
    return (M5Cardputer.Display.width() - tw) / 2;
}

// 仅重绘居中时分秒
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

// 仅重绘居中日期
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

// 左下角 1x 来源标识（NTP / RTC）
static void updateRtcSourceText(const char* source) {
    if (strcmp(source, rtcLastSrc) == 0) {
        return;
    }

    const int y = rtcSrcY();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, 48, RTC_SRC_LINE_H, BLACK);
    M5Cardputer.Display.setTextSize(RTC_SRC_TEXT_SIZE);
    M5Cardputer.Display.setTextColor(RTC_SRC_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(source);
    strncpy(rtcLastSrc, source, sizeof(rtcLastSrc) - 1);
    rtcLastSrc[sizeof(rtcLastSrc) - 1] = '\0';
}

// 时间界面中间状态（连接 WiFi / NTP 同步）
static void drawRtcBusyScreen(const char* msg) {
    beginAppScreen("Time");
    rtcScreenReady = true;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
    M5Cardputer.Display.println(msg);
}

// 通过 NTP 同步系统时间，并写回硬件 RTC（调用前须已连 WiFi，deadline_ms 为总截止时间）
static bool trySyncNtpTime(const uint32_t deadline_ms) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");

    struct tm timeinfo{};
    while (static_cast<int32_t>(millis() - deadline_ms) < 0) {
        if (getLocalTime(&timeinfo, 200)) {
            if (M5.Rtc.isEnabled()) {
                M5.Rtc.setDateTime(&timeinfo);
                M5.Rtc.setSystemTimeFromRtc();
            }
            return true;
        }
        delay(100);
    }
    return false;
}

// 读取当前时间：优先硬件 RTC，其次系统时间
static bool readCurrentTime(struct tm& out, const char*& source) {
    if (M5.Rtc.isEnabled()) {
        const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
        if (dt.date.year >= 2020) {
            out.tm_year = dt.date.year - 1900;
            out.tm_mon = dt.date.month - 1;
            out.tm_mday = dt.date.date;
            out.tm_hour = dt.time.hours;
            out.tm_min = dt.time.minutes;
            out.tm_sec = dt.time.seconds;
            out.tm_wday = dt.date.weekDay;
            source = "RTC";
            return true;
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

// RTC / NTP 时钟：首帧全屏，之后只刷新变化的时间文字
static void drawRtcApp(const bool full_init) {
    struct tm timeinfo{};
    const char* source = "none";
    if (!readCurrentTime(timeinfo, source)) {
        if (!full_init && rtcScreenReady) {
            return;
        }
        beginAppScreen("Time");
        rtcScreenReady = true;
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
        rtcLastTime[0] = '\0';
        rtcLastDate[0] = '\0';
        rtcLastSrc[0] = '\0';
    }
    updateRtcTimeText(time_buf);
    updateRtcDateText(date_buf);
    updateRtcSourceText(source);
}

void enterRtcApp() {
    rtcScreenReady = false;
    rtcSyncTimedOut = false;
    rtcLastTime[0] = '\0';
    rtcLastDate[0] = '\0';
    rtcLastSrc[0] = '\0';

    const AppConfig& cfg = getAppConfig();
    if (cfg.loaded && cfg.wifi_ssid[0] != '\0') {
        const uint32_t deadline = millis() + RTC_SYNC_TIMEOUT_MS;
        drawRtcBusyScreen("wifi connecting...");

        uint32_t remain = deadline - millis();
        const bool wifi_ok = remain > 0 && ensureConfigWifi(remain);
        if (!wifi_ok) {
            rtcSyncTimedOut = static_cast<int32_t>(millis() - deadline) >= 0;
        } else if (static_cast<int32_t>(millis() - deadline) < 0) {
            drawRtcBusyScreen("ntp syncing...");
            if (!trySyncNtpTime(deadline)) {
                rtcSyncTimedOut = static_cast<int32_t>(millis() - deadline) >= 0;
            }
        } else {
            rtcSyncTimedOut = true;
        }
        releaseConfigWifi();
    }

    drawRtcApp(true);
}

void updateRtcApp() {
    drawRtcApp(false);
}
