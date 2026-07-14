#include "app_stopwatch.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include "app_rtc.h"
#include "app_time_ui.h"
#include <cstdio>

static bool swRunning = false;
static uint32_t swAccumMs = 0;
static uint32_t swStartMs = 0;
static bool swScreenReady = false;
static BigTimeState swTimeState{};

static uint32_t swElapsedMs() {
    if (swRunning) {
        return swAccumMs + (millis() - swStartMs);
    }
    return swAccumMs;
}

// 底栏：BtnA（侧边唤醒键）开始/暂停，替代原 g 键
static void drawStopwatchActionHints() {
    const char* go_text = "start";
    if (swRunning) {
        go_text = "pause";
    } else if (swAccumMs > 0) {
        go_text = "resume";
    }

    const int y = M5Cardputer.Display.height() - TIME_HINT_ROW_H;
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, screen_w - APP_CONTENT_X * 2, TIME_HINT_ROW_H,
                                 BLACK);

    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, y, "BtnA", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(go_text);
    cx += M5Cardputer.Display.textWidth(go_text);
    M5Cardputer.Display.print(" ");
    cx += M5Cardputer.Display.textWidth(" ");

    const KeyHintItem extras[] = {{'r', "reset"}, {'p', "pure"}};
    for (int i = 0; i < 2; i++) {
        cx += drawKeyBadge(cx, y, extras[i].key, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(extras[i].text);
        cx += M5Cardputer.Display.textWidth(extras[i].text);
        if (i != 1) {
            M5Cardputer.Display.print(" ");
            cx += M5Cardputer.Display.textWidth(" ");
        }
    }

    drawTimeHelpHintRight("help");
}

// 与 countdown 相同的 RUN / PAUSED 提示
static void drawStopwatchStateBanner() {
    int area_y = 0;
    int area_h = 0;
    if (isTimePureMode()) {
        getTimePureDisplayArea(area_y, area_h);
    } else {
        getTimeDisplayArea(area_y, area_h);
    }

    M5Cardputer.Display.fillRect(APP_CONTENT_X, area_y + area_h - 10,
                                 M5Cardputer.Display.width() - APP_CONTENT_X * 2, 10, BLACK);
    if (swRunning) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, area_y + area_h - 10);
        M5Cardputer.Display.print("RUN");
    } else if (swAccumMs > 0) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, area_y + area_h - 10);
        M5Cardputer.Display.print("PAUSED");
    }
}

static void drawStopwatchChrome() {
    if (isTimePureMode()) {
        drawStopwatchStateBanner();
        return;
    }
    drawTimeModeTag("SW");
    drawStopwatchStateBanner();
    drawStopwatchActionHints();
}

static void drawStopwatchTimeArea(const int area_y, const int area_h, const bool force) {
    const uint32_t elapsed = swElapsedMs();

    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frac = 0;
    splitTimeMs(elapsed, hours, minutes, seconds, frac);
    drawBigTimeDisplay(swTimeState, area_y, area_h, hours, minutes, seconds, frac, true, force);
}

static void drawStopwatchApp(const bool full_init) {
    int area_y = 0;
    int area_h = 0;
    if (isTimePureMode()) {
        getTimePureDisplayArea(area_y, area_h);
    } else {
        getTimeDisplayArea(area_y, area_h);
    }

    if (full_init || !swScreenReady) {
        if (isTimePureMode()) {
            if (full_init) {
                M5Cardputer.Display.fillScreen(BLACK);
            }
            swScreenReady = true;
            swTimeState = BigTimeState{};
            drawStopwatchChrome();
            drawStopwatchTimeArea(area_y, area_h, true);
            return;
        }
        beginAppScreen("Time");
        swScreenReady = true;
        swTimeState = BigTimeState{};
        drawStopwatchChrome();
        drawStopwatchTimeArea(area_y, area_h, true);
        return;
    }

    drawStopwatchTimeArea(area_y, area_h, false);
}

static void swReset() {
    playTimeKeyTone(880, 35);
    delay(75); // 双击间隔
    playTimeKeyTone(880, 35);
    swRunning = false;
    swAccumMs = 0;
    swStartMs = 0;
    swTimeState = BigTimeState{};
    drawStopwatchChrome();
    int area_y = 0;
    int area_h = 0;
    if (isTimePureMode()) {
        getTimePureDisplayArea(area_y, area_h);
    } else {
        getTimeDisplayArea(area_y, area_h);
    }
    drawStopwatchTimeArea(area_y, area_h, true);
}

void redrawStopwatchApp() {
    drawStopwatchApp(true);
}

void enterStopwatchApp() {
    // 保留运行态：离开后再进入按 millis 还原已计时长
    swScreenReady = false;
    swTimeState = BigTimeState{};
    if (isTimeKeySoundEnabled()) {
        warmUpSpeakerIfNeeded();
    }
    drawStopwatchApp(true);
}

static void swToggleRun() {
    if (swRunning) {
        playTimeKeyTone(1000, 50); // pause（喇叭 <800Hz 几乎听不见）
        swAccumMs += millis() - swStartMs;
        swRunning = false;
    } else {
        playTimeKeyTone(1200, 50); // start / resume
        swStartMs = millis();
        swRunning = true;
    }
    drawStopwatchChrome();
}

void updateStopwatchApp() {
    if (!swRunning) {
        return;
    }

    static uint32_t last_draw_ms = 0;
    if (millis() - last_draw_ms >= 30) {
        last_draw_ms = millis();
        drawStopwatchApp(false);
    }
}

void pollStopwatchBtnA() {
    // wasPressed 只在按下当帧为 true，须每帧调用
    if (M5Cardputer.BtnA.wasPressed()) {
        swToggleRun();
    }
}

void handleStopwatchApp(const Keyboard_Class::KeysState& status) {
    if (status.space || status.enter) {
        swToggleRun();
        return;
    }
    const String key = getPressedKey();
    if (key == "r") {
        swReset();
    }
}
