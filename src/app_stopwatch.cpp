#include "app_stopwatch.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include <cstdio>

static bool swRunning = false;
static uint32_t swAccumMs = 0;
static uint32_t swStartMs = 0;
static bool swScreenReady = false;

// 时间区布局缓存（进入/全量重绘时计算）
static int swTs = 1;
static int swMainX = 0;
static int swMainY = 0;
static int swMainH = 0;
static int swDigitW = 0;
static int swColonW = 0;
static int swMsX = 0;
static int swMsY = 0;
static int swMsW = 0;
static int swLastH = -1;
static int swLastM = -1;
static int swLastS = -1;
static int swLastFrac = -1;

// 按宽度选取最大字号
static int calcTextSizeForWidth(const char* text, const int max_w) {
    for (int ts = 6; ts >= 1; ts--) {
        M5Cardputer.Display.setTextSize(ts);
        if (M5Cardputer.Display.textWidth(text) <= max_w) {
            return ts;
        }
    }
    return 1;
}

static uint32_t swElapsedMs() {
    if (swRunning) {
        return swAccumMs + (millis() - swStartMs);
    }
    return swAccumMs;
}

static void swSplitTime(const uint32_t elapsed_ms, int& hours, int& minutes, int& seconds,
                        int& frac) {
    frac = static_cast<int>(elapsed_ms % 1000u);
    const uint32_t total_sec = elapsed_ms / 1000u;
    seconds = static_cast<int>(total_sec % 60u);
    minutes = static_cast<int>((total_sec / 60u) % 60u);
    hours = static_cast<int>((total_sec / 3600u) % 100u);
}

// 擦除并绘制固定宽度的两位数字
static void drawDigitPair(const int x, const int y, const int value) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", value);
    M5Cardputer.Display.fillRect(x, y, swDigitW, swMainH, BLACK);
    M5Cardputer.Display.setTextSize(swTs);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(buf);
}

static void drawColon(const int x, const int y) {
    M5Cardputer.Display.setTextSize(swTs);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(":");
}

static void drawFracMs(const int frac) {
    char buf[8];
    snprintf(buf, sizeof(buf), ".%03d", frac);
    M5Cardputer.Display.fillRect(swMsX, swMsY, swMsW, 8, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(swMsX, swMsY);
    M5Cardputer.Display.print(buf);
}

// 计算时间区坐标；force 时重画全部，否则只刷新变化的时/分/秒/毫秒
static void drawStopwatchTime(const int y, const int h, const uint32_t elapsed_ms,
                              const bool force) {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frac = 0;
    swSplitTime(elapsed_ms, hours, minutes, seconds, frac);

    if (!force && hours == swLastH && minutes == swLastM && seconds == swLastS &&
        frac == swLastFrac) {
        return;
    }

    const int screen_w = M5Cardputer.Display.width();
    const int margin = APP_CONTENT_X;
    const int avail_w = screen_w - margin * 2;

    if (force || swTs <= 0) {
        const char* sample = "00:00:00";
        swTs = calcTextSizeForWidth(sample, avail_w);
        M5Cardputer.Display.setTextSize(swTs);
        swDigitW = M5Cardputer.Display.textWidth("00");
        swColonW = M5Cardputer.Display.textWidth(":");
        const int main_w = swDigitW * 3 + swColonW * 2;
        swMainH = 8 * swTs;
        swMainX = margin + (avail_w - main_w) / 2;
        const int total_h = swMainH + 2 + 8;
        swMainY = y + (h - total_h) / 2;
        swMsY = swMainY + swMainH + 2;
        M5Cardputer.Display.setTextSize(1);
        swMsW = M5Cardputer.Display.textWidth(".000");
        swMsX = swMainX + main_w - swMsW;

        M5Cardputer.Display.fillRect(margin, y, avail_w, h, BLACK);
        drawDigitPair(swMainX, swMainY, hours);
        drawColon(swMainX + swDigitW, swMainY);
        drawDigitPair(swMainX + swDigitW + swColonW, swMainY, minutes);
        drawColon(swMainX + swDigitW * 2 + swColonW, swMainY);
        drawDigitPair(swMainX + swDigitW * 2 + swColonW * 2, swMainY, seconds);
        drawFracMs(frac);
    } else {
        // 时/分未变则跳过对应位，秒与毫秒按需刷新
        if (hours != swLastH) {
            drawDigitPair(swMainX, swMainY, hours);
        }
        if (minutes != swLastM) {
            drawDigitPair(swMainX + swDigitW + swColonW, swMainY, minutes);
        }
        if (seconds != swLastS) {
            drawDigitPair(swMainX + swDigitW * 2 + swColonW * 2, swMainY, seconds);
        }
        if (frac != swLastFrac) {
            drawFracMs(frac);
        }
    }

    swLastH = hours;
    swLastM = minutes;
    swLastS = seconds;
    swLastFrac = frac;
}

static void drawStopwatchHints() {
    const int hint_y = M5Cardputer.Display.height() - 12;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);

    // g 开始/暂停，r 重置
    const char* g_text = "start";
    if (swRunning) {
        g_text = "pause";
    } else if (swAccumMs > 0) {
        g_text = "resume";
    }
    const KeyHintItem items[] = {
        {'g', g_text},
        {'r', "reset"},
    };
    drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 2, 1, APP_COLOR_HINT);
}

static void drawStopwatchStatusTag() {
    M5Cardputer.Display.fillRect(APP_CONTENT_X, APP_CONTENT_Y, 56, 10, BLACK);
    if (swRunning) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 2);
        M5Cardputer.Display.print("RUN");
    } else if (swAccumMs > 0) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 2);
        M5Cardputer.Display.print("PAUSED");
    }
}

// 状态/提示只在开始、暂停、重置时刷新；时间可局部刷新
static void drawStopwatchChrome() {
    drawStopwatchStatusTag();
    drawStopwatchHints();
}

static void drawStopwatchApp(const bool full_init) {
    const int screen_h = M5Cardputer.Display.height();
    const int hint_h = 12;
    const int status_h = 12;
    const int area_y = APP_CONTENT_Y + status_h;
    const int area_h = screen_h - area_y - hint_h;

    if (full_init || !swScreenReady) {
        beginAppScreen("Stopwatch");
        swScreenReady = true;
        swLastH = -1;
        swLastM = -1;
        swLastS = -1;
        swLastFrac = -1;
        drawStopwatchChrome();
        drawStopwatchTime(area_y, area_h, swElapsedMs(), true);
        return;
    }

    drawStopwatchTime(area_y, area_h, swElapsedMs(), false);
}

static void swReset() {
    swRunning = false;
    swAccumMs = 0;
    swStartMs = 0;
    drawStopwatchChrome();
    const int screen_h = M5Cardputer.Display.height();
    const int hint_h = 12;
    const int status_h = 12;
    const int area_y = APP_CONTENT_Y + status_h;
    const int area_h = screen_h - area_y - hint_h;
    swLastH = -1;
    swLastM = -1;
    swLastS = -1;
    swLastFrac = -1;
    drawStopwatchTime(area_y, area_h, 0, true);
}

void enterStopwatchApp() {
    swRunning = false;
    swAccumMs = 0;
    swStartMs = 0;
    swScreenReady = false;
    swLastH = -1;
    swLastM = -1;
    swLastS = -1;
    swLastFrac = -1;
    drawStopwatchApp(true);
}

void updateStopwatchApp() {
    if (!swRunning) {
        return;
    }

    static uint32_t last_draw_ms = 0;
    if (millis() - last_draw_ms >= 30) {
        last_draw_ms = millis();
        // 运行中只局部刷新时间，不重画 RUN / 按键提示
        drawStopwatchApp(false);
    }
}

void handleStopwatchApp(const String& key) {
    if (key == "g") {
        // g：开始 / 暂停切换
        if (swRunning) {
            swAccumMs += millis() - swStartMs;
            swRunning = false;
        } else {
            swStartMs = millis();
            swRunning = true;
        }
        drawStopwatchChrome();
    } else if (key == "r") {
        swReset();
    }
}
