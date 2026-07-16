#include "app_time_ui.h"
#include "app_colors.h"
#include "app_header.h"
#include <cstdio>

void drawTimeModeTag(const char* tag) {
    if (tag == nullptr || tag[0] == '\0') {
        return;
    }
    M5Cardputer.Display.fillRect(APP_CONTENT_X, APP_CONTENT_Y, 72, TIME_TAG_H, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y + 2);
    M5Cardputer.Display.print(tag);
}

static int drawKeyHintItemAt(const int x, const int y, const KeyHintItem& item, const int text_size,
                             const uint16_t color) {
    int cx = x;
    cx += drawKeyBadge(cx, y, item.key, text_size);
    M5Cardputer.Display.setTextSize(text_size);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(item.text);
    cx += M5Cardputer.Display.textWidth(item.text);
    return cx - x;
}

void drawTimeBottomHints(const KeyHintItem* action_items, const int action_count,
                         const char* help_label) {
    const int y = M5Cardputer.Display.height() - TIME_HINT_ROW_H;
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, screen_w - APP_CONTENT_X * 2, TIME_HINT_ROW_H,
                                 BLACK);

    int cx = APP_CONTENT_X;
    if (action_items != nullptr && action_count > 0) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        for (int i = 0; i < action_count; i++) {
            cx += drawKeyHintItemAt(cx, y, action_items[i], 1, APP_COLOR_HINT);
            if (i != action_count - 1) {
                M5Cardputer.Display.setCursor(cx, y);
                M5Cardputer.Display.print(" ");
                cx += M5Cardputer.Display.textWidth(" ");
            }
        }
    }

    const KeyHintItem help_item = {'h', help_label != nullptr ? help_label : "help"};
    drawHelpHintRight(help_item.text);
}

void drawTimeHelpHintRight(const char* help_label) {
    drawHelpHintRight(help_label != nullptr ? help_label : "help");
}

void getTimeDisplayArea(int& area_y, int& area_h) {
    // 模式已迁到 Header accent，内容区从 header 下起算
    const int screen_h = M5Cardputer.Display.height();
    area_y = APP_CONTENT_Y;
    area_h = screen_h - area_y - TIME_HINT_ROW_H;
}

void getTimePureDisplayArea(int& area_y, int& area_h) {
    area_y = 0;
    area_h = M5Cardputer.Display.height();
}

void splitTimeMs(const uint64_t elapsed_ms, int& hours, int& minutes, int& seconds, int& frac) {
    frac = static_cast<int>(elapsed_ms % 1000ULL);
    const uint64_t total_sec = elapsed_ms / 1000ULL;
    seconds = static_cast<int>(total_sec % 60ULL);
    minutes = static_cast<int>((total_sec / 60ULL) % 60ULL);
    hours = static_cast<int>((total_sec / 3600ULL) % 100ULL);
}

static int calcTextSizeForWidth(const char* text, const int max_w) {
    for (int ts = 6; ts >= 1; ts--) {
        M5Cardputer.Display.setTextSize(ts);
        if (M5Cardputer.Display.textWidth(text) <= max_w) {
            return ts;
        }
    }
    return 1;
}

static void drawDigitPair(BigTimeState& state, const int x, const int y, const int value,
                          const uint16_t color) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", value);
    M5Cardputer.Display.fillRect(x, y, state.digit_w, state.main_h, BLACK);
    M5Cardputer.Display.setTextSize(state.ts);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(buf);
}

static void drawColon(BigTimeState& state, const int x, const int y) {
    M5Cardputer.Display.setTextSize(state.ts);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(":");
}

static void drawFracMs(BigTimeState& state, const int frac) {
    char buf[8];
    snprintf(buf, sizeof(buf), ".%03d", frac);
    M5Cardputer.Display.fillRect(state.ms_x, state.ms_y, state.ms_w, 8, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(state.ms_x, state.ms_y);
    M5Cardputer.Display.print(buf);
}

void drawBigTimeDisplay(BigTimeState& state, const int area_y, const int area_h, const int hours,
                        const int minutes, const int seconds, const int frac_ms,
                        const bool show_ms, const bool force) {
    if (!force && hours == state.last_h && minutes == state.last_m && seconds == state.last_s &&
        (!show_ms || frac_ms == state.last_frac)) {
        return;
    }

    const int screen_w = M5Cardputer.Display.width();
    const int margin = APP_CONTENT_X;
    const int avail_w = screen_w - margin * 2;

    if (force || state.ts <= 0) {
        const char* sample = "00:00:00";
        state.ts = calcTextSizeForWidth(sample, avail_w);
        M5Cardputer.Display.setTextSize(state.ts);
        state.digit_w = M5Cardputer.Display.textWidth("00");
        state.colon_w = M5Cardputer.Display.textWidth(":");
        const int main_w = state.digit_w * 3 + state.colon_w * 2;
        state.main_h = 8 * state.ts;
        state.main_x = margin + (avail_w - main_w) / 2;
        const int total_h = state.main_h + (show_ms ? 10 : 0);
        state.main_y = area_y + (area_h - total_h) / 2;
        state.ms_y = state.main_y + state.main_h + 2;
        M5Cardputer.Display.setTextSize(1);
        state.ms_w = M5Cardputer.Display.textWidth(".000");
        state.ms_x = state.main_x + main_w - state.ms_w;

        M5Cardputer.Display.fillRect(margin, area_y, avail_w, area_h, BLACK);
        drawDigitPair(state, state.main_x, state.main_y, hours, WHITE);
        drawColon(state, state.main_x + state.digit_w, state.main_y);
        drawDigitPair(state, state.main_x + state.digit_w + state.colon_w, state.main_y, minutes,
                      WHITE);
        drawColon(state, state.main_x + state.digit_w * 2 + state.colon_w, state.main_y);
        drawDigitPair(state, state.main_x + state.digit_w * 2 + state.colon_w * 2, state.main_y,
                      seconds, WHITE);
        if (show_ms) {
            drawFracMs(state, frac_ms);
        }
    } else {
        if (hours != state.last_h) {
            drawDigitPair(state, state.main_x, state.main_y, hours, WHITE);
        }
        if (minutes != state.last_m) {
            drawDigitPair(state, state.main_x + state.digit_w + state.colon_w, state.main_y, minutes,
                          WHITE);
        }
        if (seconds != state.last_s) {
            drawDigitPair(state, state.main_x + state.digit_w * 2 + state.colon_w * 2, state.main_y,
                          seconds, WHITE);
        }
        if (show_ms && frac_ms != state.last_frac) {
            drawFracMs(state, frac_ms);
        }
    }

    state.last_h = hours;
    state.last_m = minutes;
    state.last_s = seconds;
    state.last_frac = frac_ms;
}
