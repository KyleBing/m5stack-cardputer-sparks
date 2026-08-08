#include "app_calendar.h"

#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"

#include <cstdio>
#include <cstring>
#include <time.h>

static constexpr int CAL_PAD_X = APP_CONTENT_X;
static constexpr int CAL_WD_H = 10;
static constexpr int CAL_GRID_ROWS = 6;
static constexpr int CAL_COLS = 7;

static const char* const CAL_WD[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
static const char* const CAL_MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

enum class CalUiMode : uint8_t {
    Month = 0,
    Help,
};

static CalUiMode g_mode = CalUiMode::Month;
static int g_year = 2026;
static int g_month = 7; // 1–12
static int g_today_y = -1;
static int g_today_m = -1;
static int g_today_d = -1;
static bool g_has_today = false;
static bool g_screen_ready = false;

// header 标题：年月（x2 由 drawAppScreenHeader 绘制）
static void formatYmTitle(char* buf, const size_t n) {
    snprintf(buf, n, "%d %s", g_year, CAL_MON[g_month - 1]);
}

// 当月天数（month 1–12）
static int daysInMonth(const int year, const int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

// 当月 1 号星期（0=Sun … 6=Sat）
static int firstWeekday(const int year, const int month) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    t.tm_hour = 12; // 避开夏令时边界
    t.tm_isdst = -1;
    if (mktime(&t) == static_cast<time_t>(-1)) {
        return 0;
    }
    return t.tm_wday;
}

// 返回 tm_wday 编号：0=Sun，1=Mon
static int weekStartIndex() {
    return getAppConfig().week_start == WeekStartDay::Monday ? 1 : 0;
}

// 从 RTC / 系统时钟读“今天”
static void refreshToday() {
    applyLocalTimezone();
    time_t now = time(nullptr);
    if (now <= 1600000000 && M5.Rtc.isEnabled()) {
        const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
        if (dt.date.year >= 2020) {
            M5.Rtc.setSystemTimeFromRtc();
            applyLocalTimezone();
            now = time(nullptr);
        }
    }
    if (now <= 1600000000) {
        g_has_today = false;
        return;
    }
    struct tm local = {};
    localtime_r(&now, &local);
    g_today_y = local.tm_year + 1900;
    g_today_m = local.tm_mon + 1;
    g_today_d = local.tm_mday;
    g_has_today = true;
}

static void jumpToToday() {
    refreshToday();
    if (g_has_today) {
        g_year = g_today_y;
        g_month = g_today_m;
    }
}

static void shiftMonth(const int delta) {
    int m = g_month + delta;
    int y = g_year;
    while (m < 1) {
        m += 12;
        y--;
    }
    while (m > 12) {
        m -= 12;
        y++;
    }
    if (y < 1970) {
        y = 1970;
        m = 1;
    }
    if (y > 2099) {
        y = 2099;
        m = 12;
    }
    g_year = y;
    g_month = m;
}

static void shiftYear(const int delta) {
    g_year += delta;
    if (g_year < 1970) {
        g_year = 1970;
    }
    if (g_year > 2099) {
        g_year = 2099;
    }
}

static void beginCalHeader() {
    char title[24];
    formatYmTitle(title, sizeof(title));
    beginAppScreen(title);
}

// Help：Time 风格单栏
static void drawCalHelp() {
    int y = drawAppHelpBegin("Calendar");
    constexpr int x = APP_HELP_CONTENT_X;
    y = drawAppHelpKey(x, y, ',', "prev month");
    y = drawAppHelpKey(x, y, '.', "next month");
    y = drawAppHelpKey(x, y, '-', "prev year");
    y = drawAppHelpKey(x, y, '=', "next year");
    y = drawAppHelpKey(x, y, 't', "jump to today");
    y = drawAppHelpKey(x, y, 'h', "close help");
    drawHelpHintRight("close");
}

// 仅画内容区网格（header 已是年月）
static void drawMonthBody() {
    clearAppContentArea();

    const int screen_w = M5Cardputer.Display.width();
    const int screen_h = M5Cardputer.Display.height();
    const int wd_y = APP_CONTENT_INSET_Y;
    const int grid_top = wd_y + CAL_WD_H;
    const int grid_h = screen_h - grid_top;
    const int cell_h = grid_h / CAL_GRID_ROWS;
    const int grid_w = screen_w - CAL_PAD_X * 2;
    const int cell_w = grid_w / CAL_COLS;
    const int grid_x = CAL_PAD_X + (grid_w - cell_w * CAL_COLS) / 2;
    const int week_start = weekStartIndex();

    // 星期表头
    M5Cardputer.Display.setTextSize(1);
    for (int c = 0; c < CAL_COLS; c++) {
        const int x = grid_x + c * cell_w;
        const int weekday = (week_start + c) % CAL_COLS;
        const bool weekend = weekday == 0 || weekday == 6;
        M5Cardputer.Display.setTextColor(weekend ? APP_COLOR_WARN : APP_COLOR_HINT, BLACK);
        const int tw = 12; // "Su" 等两字符
        M5Cardputer.Display.setCursor(x + (cell_w - tw) / 2, wd_y);
        M5Cardputer.Display.print(CAL_WD[weekday]);
    }

    const int dim = daysInMonth(g_year, g_month);
    const int first_weekday = firstWeekday(g_year, g_month);
    const int start = (first_weekday - week_start + CAL_COLS) % CAL_COLS;
    const bool mark_today = g_has_today && g_year == g_today_y && g_month == g_today_m;

    for (int day = 1; day <= dim; day++) {
        const int idx = start + day - 1;
        const int row = idx / CAL_COLS;
        const int col = idx % CAL_COLS;
        if (row >= CAL_GRID_ROWS) {
            break;
        }
        const int x = grid_x + col * cell_w;
        const int y = grid_top + row * cell_h;
        const bool is_today = mark_today && day == g_today_d;
        const int weekday = (first_weekday + day - 1) % CAL_COLS;
        const bool weekend = weekday == 0 || weekday == 6;

        char buf[4];
        snprintf(buf, sizeof(buf), "%d", day);
        const int tw = static_cast<int>(strlen(buf)) * 6;
        const int tx = x + (cell_w - tw) / 2;
        const int ty = y + (cell_h - 8) / 2;

        if (is_today) {
            // 今日高亮块
            const int bw = tw + 4;
            const int bh = 10;
            const int bx = tx - 2;
            const int by = ty - 1;
            M5Cardputer.Display.fillRoundRect(bx, by, bw, bh, 2, APP_COLOR_MENU_KEY);
            M5Cardputer.Display.setTextColor(APP_COLOR_KEY_TEXT, APP_COLOR_MENU_KEY);
        } else {
            M5Cardputer.Display.setTextColor(weekend ? APP_COLOR_WARN : APP_COLOR_VALUE, BLACK);
        }
        M5Cardputer.Display.setCursor(tx, ty);
        M5Cardputer.Display.print(buf);
    }

    if (!g_has_today) {
        M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
        M5Cardputer.Display.setCursor(CAL_PAD_X, screen_h - 10);
        M5Cardputer.Display.print("clock unset");
    }
}

static void drawMonthScreen() {
    beginCalHeader();
    drawMonthBody();
    g_screen_ready = true;
}

static void redraw() {
    if (g_mode == CalUiMode::Help) {
        drawCalHelp();
        return;
    }
    drawMonthScreen();
}

void enterCalendarApp() {
    g_mode = CalUiMode::Month;
    g_screen_ready = false;
    jumpToToday();
    if (!g_has_today) {
        // 无有效时钟时仍展示默认月，可手动翻页
        g_year = 2026;
        g_month = 7;
    }
    redraw();
}

void updateCalendarApp() {
    if (!g_screen_ready || g_mode != CalUiMode::Month) {
        return;
    }
    // 跨日时刷新今日高亮（低频率即可）
    static uint32_t last_ms = 0;
    const uint32_t now = millis();
    if (now - last_ms < 30000) {
        return;
    }
    last_ms = now;
    const int py = g_today_y;
    const int pm = g_today_m;
    const int pd = g_today_d;
    const bool ph = g_has_today;
    refreshToday();
    if (ph != g_has_today || py != g_today_y || pm != g_today_m || pd != g_today_d) {
        drawMonthBody();
    }
}

bool closeCalendarHelp() {
    // Help 未打开则忽略
    if (g_mode != CalUiMode::Help) {
        return false;
    }
    g_mode = CalUiMode::Month;
    redraw();
    return true;
}

void handleCalendarApp(const Keyboard_Class::KeysState& status) {
    if (g_mode == CalUiMode::Help) {
        for (const char c : status.word) {
            if (c == 'h' || c == 'H') {
                closeCalendarHelp();
                return;
            }
        }
        return;
    }

    const int nav = getMenuNavDelta(status);
    if (nav != 0) {
        shiftMonth(nav);
        drawMonthScreen();
        return;
    }

    for (const char c : status.word) {
        if (c == 'h' || c == 'H') {
            g_mode = CalUiMode::Help;
            drawCalHelp();
            return;
        }
        if (c == 't' || c == 'T') {
            jumpToToday();
            drawMonthScreen();
            return;
        }
        if (c == '-') {
            shiftYear(-1);
            drawMonthScreen();
            return;
        }
        if (c == '=') {
            shiftYear(1);
            drawMonthScreen();
            return;
        }
    }
}
