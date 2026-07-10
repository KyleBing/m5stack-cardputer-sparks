#include "app_countdown.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include <cstring>

enum class CountdownPhase {
    SETUP,
    RUNNING,
    PAUSED,
    FINISHED,
};

static CountdownPhase cdPhase = CountdownPhase::SETUP;
static int cdHours = 0;
static int cdMinutes = 5;
static int cdSeconds = 0;
static int cdField = 0;  // 0=h 1=m 2=s
static uint32_t cdEndMs = 0;
static uint32_t cdRemainMs = 0;
static bool cdScreenReady = false;

// 时间区布局缓存（进入/全量重绘时计算）
static int cdTs = 1;
static int cdMainX = 0;
static int cdMainY = 0;
static int cdMainH = 0;
static int cdDigitW = 0;
static int cdColonW = 0;
static int cdHx = 0;
static int cdMx = 0;
static int cdSx = 0;
static int cdLastH = -1;
static int cdLastM = -1;
static int cdLastS = -1;
static int cdLastField = -1;

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

static uint32_t cdSetupTotalMs() {
    const uint32_t total_sec =
        static_cast<uint32_t>(cdHours) * 3600u + static_cast<uint32_t>(cdMinutes) * 60u +
        static_cast<uint32_t>(cdSeconds);
    return total_sec * 1000u;
}

static void cdGetDisplayHms(int& hours, int& minutes, int& seconds) {
    if (cdPhase == CountdownPhase::SETUP) {
        hours = cdHours;
        minutes = cdMinutes;
        seconds = cdSeconds;
        return;
    }

    uint32_t remain_ms = 0;
    if (cdPhase == CountdownPhase::RUNNING) {
        const int32_t left = static_cast<int32_t>(cdEndMs - millis());
        remain_ms = left > 0 ? static_cast<uint32_t>(left) : 0;
    } else {
        remain_ms = cdRemainMs;
    }

    const uint32_t total_sec = remain_ms / 1000u;
    seconds = static_cast<int>(total_sec % 60u);
    minutes = static_cast<int>((total_sec / 60u) % 60u);
    hours = static_cast<int>((total_sec / 3600u) % 100u);
}

// 左右切换编辑栏
static int getCountdownFieldDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x50 || hid == 0x36) {
            return -1;
        }
        if (hid == 0x4F || hid == 0x38) {
            return 1;
        }
    }
    for (const char c : status.word) {
        if (c == ',' || c == '[') {
            return -1;
        }
        if (c == '/' || c == ']') {
            return 1;
        }
    }
    return 0;
}

// 上增下减
static int getCountdownAdjustDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            return 1;
        }
        if (hid == 0x51 || hid == 0x37) {
            return -1;
        }
    }
    for (const char c : status.word) {
        if (c == ';') {
            return 1;
        }
        if (c == '.') {
            return -1;
        }
    }
    return 0;
}

static char cdPressedLetter(const Keyboard_Class::KeysState& status) {
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

static void drawCdDigitPair(const int x, const int y, const int value, const bool highlight) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", value);
    M5Cardputer.Display.fillRect(x, y, cdDigitW, cdMainH, BLACK);
    M5Cardputer.Display.setTextSize(cdTs);
    M5Cardputer.Display.setTextColor(highlight ? YELLOW : WHITE, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(buf);
}

static void drawCdColon(const int x, const int y) {
    M5Cardputer.Display.setTextSize(cdTs);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(":");
}

static void drawCountdownTime(const int y, const int h, const bool force) {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    cdGetDisplayHms(hours, minutes, seconds);

    const bool highlight_field = cdPhase == CountdownPhase::SETUP;
    if (!force && hours == cdLastH && minutes == cdLastM && seconds == cdLastS &&
        (!highlight_field || cdField == cdLastField)) {
        return;
    }

    const int screen_w = M5Cardputer.Display.width();
    const int margin = APP_CONTENT_X;
    const int avail_w = screen_w - margin * 2;

    if (force || cdTs <= 0) {
        const char* sample = "00:00:00";
        cdTs = calcTextSizeForWidth(sample, avail_w);
        M5Cardputer.Display.setTextSize(cdTs);
        cdDigitW = M5Cardputer.Display.textWidth("00");
        cdColonW = M5Cardputer.Display.textWidth(":");
        const int main_w = cdDigitW * 3 + cdColonW * 2;
        cdMainH = 8 * cdTs;
        cdMainX = margin + (avail_w - main_w) / 2;
        cdMainY = y + (h - cdMainH) / 2;
        cdHx = cdMainX;
        cdMx = cdMainX + cdDigitW + cdColonW;
        cdSx = cdMainX + cdDigitW * 2 + cdColonW * 2;

        M5Cardputer.Display.fillRect(margin, y, avail_w, h, BLACK);
        drawCdDigitPair(cdHx, cdMainY, hours, highlight_field && cdField == 0);
        drawCdColon(cdHx + cdDigitW, cdMainY);
        drawCdDigitPair(cdMx, cdMainY, minutes, highlight_field && cdField == 1);
        drawCdColon(cdMx + cdDigitW, cdMainY);
        drawCdDigitPair(cdSx, cdMainY, seconds, highlight_field && cdField == 2);
    } else {
        if (hours != cdLastH || (highlight_field && cdField == 0) ||
            (highlight_field && cdLastField == 0 && cdField != 0)) {
            drawCdDigitPair(cdHx, cdMainY, hours, highlight_field && cdField == 0);
        }
        if (minutes != cdLastM || (highlight_field && cdField == 1) ||
            (highlight_field && cdLastField == 1 && cdField != 1)) {
            drawCdDigitPair(cdMx, cdMainY, minutes, highlight_field && cdField == 1);
        }
        if (seconds != cdLastS || (highlight_field && cdField == 2) ||
            (highlight_field && cdLastField == 2 && cdField != 2)) {
            drawCdDigitPair(cdSx, cdMainY, seconds, highlight_field && cdField == 2);
        }
    }

    cdLastH = hours;
    cdLastM = minutes;
    cdLastS = seconds;
    cdLastField = cdField;
}

static void drawCountdownStatusTag() {
    M5Cardputer.Display.fillRect(APP_CONTENT_X, APP_CONTENT_Y, 72, 10, BLACK);
    if (cdPhase == CountdownPhase::RUNNING) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 2);
        M5Cardputer.Display.print("RUN");
    } else if (cdPhase == CountdownPhase::PAUSED) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 2);
        M5Cardputer.Display.print("PAUSED");
    } else if (cdPhase == CountdownPhase::FINISHED) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 2);
        M5Cardputer.Display.print("DONE");
    }
}

static void drawCountdownBanner() {
    const int screen_h = M5Cardputer.Display.height();
    const int hint_h = 12;
    const int status_h = 12;
    const int area_y = APP_CONTENT_Y + status_h;
    const int area_h = screen_h - area_y - hint_h;

    M5Cardputer.Display.fillRect(APP_CONTENT_X, area_y + area_h - 10,
                                 M5Cardputer.Display.width() - APP_CONTENT_X * 2, 10, BLACK);
    if (cdPhase == CountdownPhase::FINISHED) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, area_y + area_h - 10);
        M5Cardputer.Display.print("Time's up!");
    }
}

static void drawCountdownHints() {
    const int hint_y = M5Cardputer.Display.height() - 12;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);

    if (cdPhase == CountdownPhase::SETUP) {
        const KeyHintItem items[] = {
            {',', "field"},
            {';', "up"},
            {'.', "down"},
            {'g', "start"},
        };
        drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 4, 1, APP_COLOR_HINT);
        return;
    }

    const char* g_text = "start";
    if (cdPhase == CountdownPhase::RUNNING) {
        g_text = "pause";
    } else if (cdPhase == CountdownPhase::PAUSED) {
        g_text = "resume";
    } else if (cdPhase == CountdownPhase::FINISHED) {
        g_text = "again";
    }

    const KeyHintItem items[] = {
        {'g', g_text},
        {'r', "reset"},
    };
    drawKeyHintsRow(APP_CONTENT_X, hint_y, items, 2, 1, APP_COLOR_HINT);
}

// 状态/提示只在阶段变化时刷新；时间可局部刷新
static void drawCountdownChrome() {
    drawCountdownStatusTag();
    drawCountdownBanner();
    drawCountdownHints();
}

static void cdInvalidateTimeCache() {
    cdLastH = -1;
    cdLastM = -1;
    cdLastS = -1;
    cdLastField = -1;
}

static void drawCountdownApp(const bool full_init) {
    const int screen_h = M5Cardputer.Display.height();
    const int hint_h = 12;
    const int status_h = 12;
    const int area_y = APP_CONTENT_Y + status_h;
    const int area_h = screen_h - area_y - hint_h;

    if (full_init || !cdScreenReady) {
        beginAppScreen("Countdown");
        cdScreenReady = true;
        cdInvalidateTimeCache();
        drawCountdownChrome();
        drawCountdownTime(area_y, area_h, true);
        return;
    }

    drawCountdownTime(area_y, area_h, false);
}

static void cdAdjustField(const int delta) {
    if (cdPhase != CountdownPhase::SETUP || delta == 0) {
        return;
    }
    switch (cdField) {
        case 0:
            cdHours = constrain(cdHours + delta, 0, 99);
            break;
        case 1:
            cdMinutes = constrain(cdMinutes + delta, 0, 59);
            break;
        default:
            cdSeconds = constrain(cdSeconds + delta, 0, 59);
            break;
    }
}

static void cdStart() {
    const uint32_t total = cdSetupTotalMs();
    if (total == 0) {
        return;
    }
    cdRemainMs = total;
    cdEndMs = millis() + total;
    cdPhase = CountdownPhase::RUNNING;
    cdInvalidateTimeCache();
    drawCountdownChrome();
    drawCountdownApp(true);
}

static void cdToggleRun() {
    if (cdPhase == CountdownPhase::SETUP) {
        cdStart();
        return;
    }
    if (cdPhase == CountdownPhase::RUNNING) {
        const int32_t left = static_cast<int32_t>(cdEndMs - millis());
        cdRemainMs = left > 0 ? static_cast<uint32_t>(left) : 0;
        cdPhase = CountdownPhase::PAUSED;
        drawCountdownChrome();
        drawCountdownApp(false);
        return;
    }
    if (cdPhase == CountdownPhase::PAUSED) {
        if (cdRemainMs == 0) {
            return;
        }
        cdEndMs = millis() + cdRemainMs;
        cdPhase = CountdownPhase::RUNNING;
        drawCountdownChrome();
        drawCountdownApp(false);
        return;
    }
    if (cdPhase == CountdownPhase::FINISHED) {
        cdInvalidateTimeCache();
        drawCountdownChrome();
        cdStart();
    }
}

static void cdResetToSetup() {
    cdPhase = CountdownPhase::SETUP;
    cdField = 0;
    cdRemainMs = 0;
    cdInvalidateTimeCache();
    drawCountdownChrome();
    const int screen_h = M5Cardputer.Display.height();
    const int hint_h = 12;
    const int status_h = 12;
    const int area_y = APP_CONTENT_Y + status_h;
    const int area_h = screen_h - area_y - hint_h;
    drawCountdownTime(area_y, area_h, true);
}

void enterCountdownApp() {
    cdPhase = CountdownPhase::SETUP;
    cdHours = 0;
    cdMinutes = 5;
    cdSeconds = 0;
    cdField = 0;
    cdRemainMs = 0;
    cdScreenReady = false;
    cdInvalidateTimeCache();
    drawCountdownApp(true);
}

void updateCountdownApp() {
    if (cdPhase == CountdownPhase::RUNNING) {
        const int32_t left = static_cast<int32_t>(cdEndMs - millis());
        if (left <= 0) {
            cdRemainMs = 0;
            cdPhase = CountdownPhase::FINISHED;
            M5Cardputer.Speaker.tone(880, 400);
            cdInvalidateTimeCache();
            drawCountdownChrome();
            drawCountdownApp(true);
            return;
        }

        static uint32_t last_tick_ms = 0;
        if (millis() - last_tick_ms >= 200) {
            last_tick_ms = millis();
            drawCountdownApp(false);
        }
    }
}

void handleCountdownApp(const Keyboard_Class::KeysState& status) {
    if (cdPhase == CountdownPhase::SETUP) {
        const int field_delta = getCountdownFieldDelta(status);
        if (field_delta != 0) {
            cdField = (cdField + field_delta + 3) % 3;
            drawCountdownApp(false);
            return;
        }

        const int adjust_delta = getCountdownAdjustDelta(status);
        if (adjust_delta != 0) {
            cdAdjustField(adjust_delta);
            drawCountdownApp(false);
            return;
        }
    }

    const char key = cdPressedLetter(status);
    if (key == 'g') {
        cdToggleRun();
    } else if (key == 'r') {
        cdResetToSetup();
    }
}
