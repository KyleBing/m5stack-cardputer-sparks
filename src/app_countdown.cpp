#include "app_countdown.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include "app_rtc.h"
#include "app_time_ui.h"
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

// 电子闹钟滴滴：哔-哔-歇，最多响 30s，x 取消
static constexpr float CD_ALARM_HZ = 1200.0f;
static constexpr uint32_t CD_ALARM_BEEP_MS = 120;
static constexpr uint32_t CD_ALARM_GAP_MS = 100;
static constexpr uint32_t CD_ALARM_REST_MS = 500;
static constexpr uint32_t CD_ALARM_MAX_MS = 30000;
static constexpr char CD_ALARM_CANCEL_KEY = 'x';
static constexpr int CD_FINISH_HINT_H = 20; // size2 徽章高度

static bool cdAlarmActive = false;
static uint32_t cdAlarmStartMs = 0;
static uint32_t cdAlarmNextMs = 0;
static uint8_t cdAlarmStep = 0; // 0=哔1 1=间隙 2=哔2 3=长歇

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

// 结束提示贴在大字下方，不占底栏
static void getCountdownDisplayArea(int& area_y, int& area_h) {
    if (isTimePureMode()) {
        getTimePureDisplayArea(area_y, area_h);
        return;
    }
    if (cdPhase == CountdownPhase::FINISHED) {
        const int screen_h = M5Cardputer.Display.height();
        area_y = APP_CONTENT_INSET_Y;
        area_h = screen_h - area_y;
        return;
    }
    getTimeDisplayArea(area_y, area_h);
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

// 0-9 数字键（word 或 HID 数字行）
static int getCountdownDigit(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
    }
    for (const uint8_t hid : status.hid_keys) {
        if (hid >= 0x1E && hid <= 0x27) {
            return (hid == 0x27) ? 0 : (hid - 0x1E + 1);
        }
    }
    return -1;
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

// 结束态数字色：响铃时红/白闪烁，停铃后保持红色
static uint16_t cdFinishedDigitColor() {
    if (!cdAlarmActive) {
        return APP_COLOR_ERROR;
    }
    return ((millis() / 400) & 1) ? APP_COLOR_ERROR : WHITE;
}

static void drawCdDigitPair(const int x, const int y, const int value, const bool highlight) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", value);
    M5Cardputer.Display.fillRect(x, y, cdDigitW, cdMainH, BLACK);
    M5Cardputer.Display.setTextSize(cdTs);
    uint16_t color = WHITE;
    if (highlight) {
        color = YELLOW;
    } else if (cdPhase == CountdownPhase::FINISHED) {
        color = cdFinishedDigitColor();
    }
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(buf);
}

static void drawCdColon(const int x, const int y) {
    M5Cardputer.Display.setTextSize(cdTs);
    const uint16_t color =
        cdPhase == CountdownPhase::FINISHED ? cdFinishedDigitColor() : WHITE;
    M5Cardputer.Display.setTextColor(color, BLACK);
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
        // 到点：为大字下方「Time up / x OK」留出 8px + 提示行
        if (cdPhase == CountdownPhase::FINISHED) {
            const int below = 8 + CD_FINISH_HINT_H;
            cdMainY = y + (h - cdMainH - below) / 2;
            if (cdMainY < y) {
                cdMainY = y;
            }
        } else {
            cdMainY = y + (h - cdMainH) / 2 - 2;
        }
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

static void drawCountdownStateBanner() {
    if (cdPhase == CountdownPhase::SETUP) {
        return;
    }

    int area_y = 0;
    int area_h = 0;
    getCountdownDisplayArea(area_y, area_h);

    M5Cardputer.Display.fillRect(APP_CONTENT_X, area_y + area_h - 10,
                                 M5Cardputer.Display.width() - APP_CONTENT_X * 2, 10, BLACK);
    if (cdPhase == CountdownPhase::RUNNING) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, area_y + area_h - 10);
        M5Cardputer.Display.print("RUN");
    } else if (cdPhase == CountdownPhase::PAUSED) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, area_y + area_h - 10);
        M5Cardputer.Display.print("PAUSED");
    }
    // FINISHED：不画左下角 UP!，提示贴在大字下方
}

// 结束页：大字下方 8px，「Time up」靠左、「x OK」靠右，与时间同宽对齐
static void drawCountdownFinishedCancelHint() {
    if (cdTs <= 0 || cdMainH <= 0) {
        return;
    }
    const int main_w = cdDigitW * 3 + cdColonW * 2;
    const int y = cdMainY + cdMainH + 8;
    M5Cardputer.Display.fillRect(cdMainX, y, main_w, CD_FINISH_HINT_H, BLACK);

    const char* up_label = "Time up";
    const char* label = "OK";
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_ERROR, BLACK);
    M5Cardputer.Display.setCursor(cdMainX, y + 1);
    M5Cardputer.Display.print(up_label);

    // 右侧：x 徽章 + OK，右缘与大字右缘对齐
    constexpr int pad_x = 2;
    constexpr int gap = 3;
    const int letter_tw = M5Cardputer.Display.textWidth("X");
    const int badge_w = letter_tw + pad_x * 2;
    const int label_tw = M5Cardputer.Display.textWidth(label);
    const int right_w = badge_w + gap + label_tw;
    int cx = cdMainX + main_w - right_w;
    if (cx < cdMainX) {
        cx = cdMainX;
    }
    cx += drawKeyBadge(cx, y + 1, CD_ALARM_CANCEL_KEY, 2);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复说明色
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(label);
}

static void drawCountdownChrome() {
    if (cdPhase == CountdownPhase::FINISHED) {
        // 到点：只画大字旁取消提示（响铃需可见操作）
        drawCountdownFinishedCancelHint();
        return;
    }
    if (isTimePureMode()) {
        if (cdPhase != CountdownPhase::SETUP) {
            drawCountdownStateBanner();
        }
        return;
    }
    // 按键 tip 已迁到 Help，主界面只保留 RUN / PAUSED 状态
    drawCountdownStateBanner();
}

static void cdInvalidateTimeCache() {
    cdLastH = -1;
    cdLastM = -1;
    cdLastS = -1;
    cdLastField = -1;
}

static void drawCountdownApp(const bool full_init) {
    int area_y = 0;
    int area_h = 0;
    getCountdownDisplayArea(area_y, area_h);

    if (full_init || !cdScreenReady) {
        if (isTimePureMode()) {
            if (full_init) {
                M5Cardputer.Display.fillScreen(BLACK);
            }
            cdScreenReady = true;
            cdInvalidateTimeCache();
            cdTs = 0;
            // 先画时间再画 chrome：force fillRect 会盖住左下角 RUN/PAUSED
            drawCountdownTime(area_y, area_h, true);
            drawCountdownChrome();
            return;
        }
        // 到点页：标题强调 UP
        if (cdPhase == CountdownPhase::FINISHED) {
            beginAppScreenAccent("Time ", "UP", APP_COLOR_ERROR);
        } else {
            beginAppScreenAccent("Time ", "CD", APP_COLOR_LABEL);
        }
        cdScreenReady = true;
        cdInvalidateTimeCache();
        drawCountdownTime(area_y, area_h, true);
        drawCountdownChrome();
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

// 数字键堆栈输入：保留末位左移再填新位（45→2→52→7→27）
static void cdShiftInputDigit(int& value, const int digit, const int max_value) {
    value = (value % 10) * 10 + digit;
    if (value > max_value) {
        value = digit;
    }
}

static void cdInputDigit(const int digit) {
    if (cdPhase != CountdownPhase::SETUP || digit < 0 || digit > 9) {
        return;
    }
    switch (cdField) {
        case 0:
            cdShiftInputDigit(cdHours, digit, 99);
            break;
        case 1:
            // 分钟允许输入 72 等，启动时再进位到小时
            cdShiftInputDigit(cdMinutes, digit, 99);
            break;
        default:
            cdShiftInputDigit(cdSeconds, digit, 99);
            break;
    }
}

// 启动前归一化：秒/分溢出进位（如 0:72:0 → 1:12:0）
static void cdNormalizeSetupTime() {
    if (cdSeconds >= 60) {
        cdMinutes += cdSeconds / 60;
        cdSeconds %= 60;
    }
    if (cdMinutes >= 60) {
        cdHours += cdMinutes / 60;
        cdMinutes %= 60;
    }
    cdHours = constrain(cdHours, 0, 99);
}

static void cdStart() {
    cdNormalizeSetupTime();
    const uint32_t total = cdSetupTotalMs();
    if (total == 0) {
        return;
    }
    playTimeKeyTone(1200, 50); // start / resume，与 stopwatch 一致
    cdRemainMs = total;
    cdEndMs = millis() + total;
    cdPhase = CountdownPhase::RUNNING;
    cdInvalidateTimeCache();
    drawCountdownApp(true);
}

// 停止电子闹钟滴滴声
static void cdStopAlarm() {
    cdAlarmActive = false;
    cdAlarmStep = 0;
    releaseSpeakerQuiet();
}

// 开始电子闹钟：哔-哔-歇，最长 30s（响铃期间保持功放，避免每声冷启动破音）
static void cdStartAlarm() {
    cancelSpeakerQuietRelease();
    warmUpSpeakerIfNeeded();
    cdAlarmActive = true;
    cdAlarmStartMs = millis();
    cdAlarmNextMs = cdAlarmStartMs;
    cdAlarmStep = 0;
}

// 非阻塞推进滴滴节奏
static void cdUpdateAlarm() {
    if (!cdAlarmActive) {
        return;
    }
    const uint32_t now = millis();
    if (now - cdAlarmStartMs >= CD_ALARM_MAX_MS) {
        cdStopAlarm();
        return;
    }
    if (now < cdAlarmNextMs) {
        return;
    }

    switch (cdAlarmStep) {
        case 0: // 第一声
            cancelSpeakerQuietRelease();
            playUiTone(CD_ALARM_HZ, CD_ALARM_BEEP_MS, false);
            // 用播完后的时刻排程，避免 warmUp/调度误差叠掉第二声
            cdAlarmNextMs = millis() + CD_ALARM_BEEP_MS + CD_ALARM_GAP_MS;
            cdAlarmStep = 1;
            break;
        case 1: // 间隙结束 → 第二声
            cancelSpeakerQuietRelease();
            playUiTone(CD_ALARM_HZ, CD_ALARM_BEEP_MS, false);
            cdAlarmNextMs = millis() + CD_ALARM_BEEP_MS + CD_ALARM_REST_MS;
            cdAlarmStep = 2;
            break;
        default: // 长歇结束 → 下一轮
            cdAlarmStep = 0;
            cdAlarmNextMs = millis();
            break;
    }
}

// 取消闹钟并回到设置页
static void cdDismissFinished() {
    playTimeKeyTone(880, 35);
    delay(75);
    playTimeKeyTone(880, 35);
    cdStopAlarm();
    cdPhase = CountdownPhase::SETUP;
    cdField = 0;
    cdRemainMs = 0;
    cdInvalidateTimeCache();
    drawCountdownApp(true);
}

static void cdToggleRun() {
    if (cdPhase == CountdownPhase::SETUP) {
        cdStart();
        return;
    }
    if (cdPhase == CountdownPhase::RUNNING) {
        playTimeKeyTone(1000, 50); // pause
        const int32_t left = static_cast<int32_t>(cdEndMs - millis());
        cdRemainMs = left > 0 ? static_cast<uint32_t>(left) : 0;
        cdPhase = CountdownPhase::PAUSED;
        drawCountdownChrome();
        return;
    }
    if (cdPhase == CountdownPhase::PAUSED) {
        if (cdRemainMs == 0) {
            return;
        }
        playTimeKeyTone(1200, 50); // resume
        cdEndMs = millis() + cdRemainMs;
        cdPhase = CountdownPhase::RUNNING;
        drawCountdownChrome();
        return;
    }
    if (cdPhase == CountdownPhase::FINISHED) {
        // BtnA：停闹钟后重新开始
        cdStopAlarm();
        cdInvalidateTimeCache();
        cdStart();
    }
}

static void cdResetToSetup() {
    playTimeKeyTone(880, 35);
    delay(75); // 双击间隔，与 stopwatch 一致
    playTimeKeyTone(880, 35);
    cdStopAlarm();
    cdPhase = CountdownPhase::SETUP;
    cdField = 0;
    cdRemainMs = 0;
    cdInvalidateTimeCache();
    drawCountdownApp(true);
}

void redrawCountdownApp() {
    drawCountdownApp(true);
}

void enterCountdownApp() {
    // 保留 phase / endMs / 编辑值；仅重绘
    cdScreenReady = false;
    cdInvalidateTimeCache();
    if (isTimeKeySoundEnabled() || cdAlarmActive) {
        warmUpSpeakerIfNeeded();
    }
    drawCountdownApp(true);
}

void leaveCountdownApp() {
    // 后台继续跑：不改 phase / endMs；响铃由 dismiss 或 reset 停
}

// 检测 RUNNING 到期；返回是否刚进入 FINISHED（并启动闹钟）
bool pollCountdownBackground() {
    bool just_expired = false;
    if (cdPhase == CountdownPhase::RUNNING) {
        const int32_t left = static_cast<int32_t>(cdEndMs - millis());
        if (left <= 0) {
            cdRemainMs = 0;
            cdPhase = CountdownPhase::FINISHED;
            if (!cdAlarmActive) {
                cdStartAlarm();
            }
            just_expired = true;
        }
    }

    // 不在 CD 页时也要推进滴滴
    if (cdAlarmActive) {
        cdUpdateAlarm();
    }
    return just_expired;
}

bool isCountdownAlarmRinging() {
    return cdPhase == CountdownPhase::FINISHED && cdAlarmActive;
}

void updateCountdownApp() {
    // 到期由 main 里 pollCountdownBackground 统一处理；此处只刷新剩余时间 / 结束闪烁
    if (cdPhase == CountdownPhase::RUNNING) {
        static uint32_t last_tick_ms = 0;
        if (millis() - last_tick_ms >= 200) {
            last_tick_ms = millis();
            drawCountdownApp(false);
        }
    } else if (cdPhase == CountdownPhase::FINISHED && cdAlarmActive) {
        // 响铃期间红白闪烁：强制重绘数字与下方提示
        static uint32_t last_blink_ms = 0;
        static bool last_blink_on = false;
        const bool blink_on = ((millis() / 400) & 1) != 0;
        if (blink_on != last_blink_on || millis() - last_blink_ms >= 400) {
            last_blink_on = blink_on;
            last_blink_ms = millis();
            cdInvalidateTimeCache();
            int area_y = 0;
            int area_h = 0;
            getCountdownDisplayArea(area_y, area_h);
            drawCountdownTime(area_y, area_h, true);
            drawCountdownFinishedCancelHint();
        }
    }

    cdUpdateAlarm();
}

void pollCountdownBtnA() {
    // wasPressed 只在按下当帧为 true，须每帧调用
    if (M5Cardputer.BtnA.wasPressed()) {
        cdToggleRun();
    }
}

void handleCountdownApp(const Keyboard_Class::KeysState& status) {
    if (cdPhase == CountdownPhase::FINISHED) {
        const char key = cdPressedLetter(status);
        if (key == CD_ALARM_CANCEL_KEY) {
            cdDismissFinished();
            return;
        }
        if (key == 'r') {
            cdResetToSetup();
            return;
        }
        if (status.space || status.enter) {
            cdToggleRun();
            return;
        }
        return;
    }

    if (cdPhase == CountdownPhase::SETUP) {
        const int field_delta = getCountdownFieldDelta(status);
        if (field_delta != 0) {
            cdField = (cdField + field_delta + 3) % 3;
            drawCountdownApp(false);
            return;
        }

        const int digit = getCountdownDigit(status);
        if (digit >= 0) {
            cdInputDigit(digit);
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

    if (status.space || status.enter) {
        cdToggleRun();
        return;
    }

    const char key = cdPressedLetter(status);
    if (key == 'r') {
        cdResetToSetup();
    }
}
