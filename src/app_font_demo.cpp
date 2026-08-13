#include "app_font_demo.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"

#include <cstdio>
#include <cstring>

struct FontDemoItem {
    const char* name;
    const lgfx::IFont* font;
    uint8_t text_size;  // font 为 null 时生效
};

// 仅保留实测可用的内置字体（引用即链接进固件）
static const FontDemoItem FONT_DEMO_ITEMS[] = {
    {"Default 1x", nullptr, 1},
    {"Default 2x", nullptr, 2},
    {"Font0", &fonts::Font0, 1},
    {"Font2", &fonts::Font2, 1},
    {"Font4", &fonts::Font4, 1},
    {"Font6", &fonts::Font6, 1},
    {"Font7", &fonts::Font7, 1},
    {"Font8", &fonts::Font8, 1},
    {"Font8x8C64", &fonts::Font8x8C64, 1},
    {"Ascii8x16", &fonts::AsciiFont8x16, 1},
    {"TomThumb", &fonts::TomThumb, 1},
    {"FreeMono9", nullptr, 1},
    {"FreeMono12", &fonts::FreeMono12pt7b, 1},
    {"FreeSans12", &fonts::FreeSans12pt7b, 1},
    {"FreeSerif12", &fonts::FreeSerif12pt7b, 1},
    {"Orbitron24", &fonts::Orbitron_Light_24, 1},
    {"Roboto24", &fonts::Roboto_Thin_24, 1},
    {"Satisfy24", &fonts::Satisfy_24, 1},
    {"DejaVu12", &fonts::DejaVu12, 1},
    {"DejaVu18", &fonts::DejaVu18, 1},
};

static constexpr int FONT_DEMO_COUNT =
    static_cast<int>(sizeof(FONT_DEMO_ITEMS) / sizeof(FONT_DEMO_ITEMS[0]));
static constexpr int FONT_DEMO_EDGE = APP_HELP_EDGE;
static constexpr int FONT_DEMO_MAX_LINES = 32;
static constexpr int FONT_DEMO_LINE_CHARS = 48;
static const char* FONT_DEMO_DIGITS = "0123456789";
static const char* FONT_DEMO_PANGRAM = "The quick brown fox jumps over the lazy dog";

static int fontDemoIndex = 0;
static int fontDemoScrollY = 0;
static bool fontDemoHelpVisible = false;

// Font6/7/8 几乎只有数字与少数符号
static bool isDigitsOnlyFont(const lgfx::IFont* font) {
    return font == &fonts::Font6 || font == &fonts::Font7 || font == &fonts::Font8;
}

// 应用当前条目的字体与字号
static void applyFontDemoItem(const FontDemoItem& item) {
    if (item.font != nullptr) {
        M5Cardputer.Display.setFont(item.font);
        M5Cardputer.Display.setTextSize(1);
    } else {
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextSize(item.text_size);
    }
}

// 恢复 Font0 1x，避免后续 UI 继承演示字体
static void resetUiFont() {
    M5Cardputer.Display.setTextDatum(textdatum_t::top_left);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextWrap(false, false);
}

// 左右箭头 / , / 与 [ ] 切换字体（Cardputer：,=左 /=右）
static int getFontDemoPageDelta(const Keyboard_Class::KeysState& status) {
    const int bracket = getBracketNavDelta(status);
    if (bracket != 0) {
        return bracket;
    }
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x50 || hid == 0x36) {
            return -1;  // Left / ,
        }
        if (hid == 0x4F || hid == 0x38) {
            return 1;  // Right / /
        }
    }
    for (const char c : status.word) {
        if (c == ',') {
            return -1;
        }
        if (c == '/') {
            return 1;
        }
    }
    return 0;
}

// 上下箭头 / ; . 滚动样本（Cardputer：;=上 .=下）
static int getFontDemoScrollDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            return -1;  // Up / ;
        }
        if (hid == 0x51 || hid == 0x37) {
            return 1;  // Down / .
        }
    }
    for (const char c : status.word) {
        if (c == ';') {
            return -1;
        }
        if (c == '.') {
            return 1;
        }
    }
    return 0;
}

// 按当前字体宽度折行；优先在空格断开，单字超宽则按字符断开
static int appendWrappedLines(char lines[][FONT_DEMO_LINE_CHARS], int count, const int max_lines,
                              const char* text, const int max_w) {
    if (text == nullptr || text[0] == '\0' || max_w <= 0) {
        return count;
    }
    auto& d = M5Cardputer.Display;
    const char* p = text;
    while (*p != '\0' && count < max_lines) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == '\n') {
            p++;
            continue;
        }

        char buf[FONT_DEMO_LINE_CHARS];
        int len = 0;
        int last_space = -1;
        buf[0] = '\0';

        while (*p != '\0' && *p != '\n' && len < FONT_DEMO_LINE_CHARS - 1) {
            buf[len] = *p;
            buf[len + 1] = '\0';
            if (len > 0 && d.textWidth(buf) > max_w) {
                buf[len] = '\0';
                if (last_space >= 0) {
                    buf[last_space] = '\0';
                    p -= (len - last_space - 1);
                    len = last_space;
                }
                break;
            }
            if (*p == ' ') {
                last_space = len;
            }
            len++;
            p++;
        }

        while (len > 0 && buf[len - 1] == ' ') {
            buf[--len] = '\0';
        }
        if (len > 0) {
            memcpy(lines[count], buf, static_cast<size_t>(len) + 1);
            count++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return count;
}

// 过长标题截到 max_w（Font2 已选中）
static void printClipped(const int x, const int y, const char* text, const int max_w,
                         const uint16_t color) {
    auto& d = M5Cardputer.Display;
    char buf[40];
    strncpy(buf, text != nullptr ? text : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    while (buf[0] != '\0' && d.textWidth(buf) > max_w) {
        buf[strlen(buf) - 1] = '\0';
    }
    d.setTextColor(color, BLACK);
    d.setCursor(x, y);
    d.print(buf);
}

// Help：Time 风格单栏；翻页用中括号
static void drawFontHelpPage() {
    clearAppHeaderStatusRefresh();
    int y = drawAppHelpBegin("Font");
    constexpr int x = APP_HELP_CONTENT_X;
    y = drawAppHelpArrows(x, y, "previous / next font");
    y = drawAppHelpBadge(x, y, "[ ]", "previous / next font");
    const int cx = x + drawArrowUpDownFlatBadge(x, y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("scroll sample");
    y += APP_HELP_LINE_H;
    y = drawAppHelpText(x, y, "Top-left: index and name.");
    y = drawAppHelpText(x, y, "Red box: one glyph cell.");
    (void)drawAppHelpText(x, y, "Digits + pangram preview.");
    drawHelpHintRight("close");
}

static void drawFontDemoApp() {
    clearAppHeaderStatusRefresh();
    auto& d = M5Cardputer.Display;
    d.fillScreen(BLACK);
    d.setTextWrap(false, false);

    const FontDemoItem& item = FONT_DEMO_ITEMS[fontDemoIndex];
    const int screen_w = d.width();
    const int screen_h = d.height();

    // 量单字占用：宽 x 高
    applyFontDemoItem(item);
    d.setTextDatum(textdatum_t::top_left);
    char glyph = isDigitsOnlyFont(item.font) ? '8' : 'A';
    char glyph_str[2] = {glyph, '\0'};
    int glyph_w = d.textWidth(glyph_str);
    int glyph_h = d.fontHeight();
    if (glyph_w <= 0) {
        glyph = '0';
        glyph_str[0] = glyph;
        glyph_w = d.textWidth(glyph_str);
    }
    if (glyph_w <= 0) {
        glyph_w = glyph_h > 0 ? glyph_h : 8;
    }
    if (glyph_h <= 0) {
        glyph_h = 8;
    }

    const int frame_w = glyph_w + 2;
    const int frame_h = glyph_h + 2;
    int box_x = screen_w - FONT_DEMO_EDGE - frame_w;
    const int box_y = FONT_DEMO_EDGE;
    if (box_x < FONT_DEMO_EDGE) {
        box_x = FONT_DEMO_EDGE;
    }

    // 左上角：Font2 显示 index + 名称
    resetUiFont();
    d.setFont(&fonts::Font2);
    d.setTextSize(1);
    char idx_buf[8];
    snprintf(idx_buf, sizeof(idx_buf), "%02d", fontDemoIndex + 1);
    const int title_y = FONT_DEMO_EDGE;
    const int title_max_w = max(24, box_x - FONT_DEMO_EDGE - 4);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(FONT_DEMO_EDGE, title_y);
    d.print(idx_buf);
    const int name_x = FONT_DEMO_EDGE + d.textWidth(idx_buf) + 6;
    const int name_max_w = max(8, title_max_w - (name_x - FONT_DEMO_EDGE));
    printClipped(name_x, title_y, item.name, name_max_w, APP_COLOR_VALUE);
    const int title_bottom = title_y + d.fontHeight();

    // 右上角：红色单字占用框 + 示例字
    applyFontDemoItem(item);
    d.setTextDatum(textdatum_t::top_left);
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.drawRect(box_x, box_y, frame_w, frame_h, APP_COLOR_ERROR);
    d.drawString(glyph_str, box_x + 1, box_y + 1);

    // 占用尺寸画在红框正下方 5px
    resetUiFont();
    char size_buf[20];
    snprintf(size_buf, sizeof(size_buf), "%dx%d", glyph_w, glyph_h);
    const int size_w = d.textWidth(size_buf);
    int size_x = box_x + frame_w - size_w;
    if (size_x < FONT_DEMO_EDGE) {
        size_x = FONT_DEMO_EDGE;
    }
    const int size_y = box_y + frame_h + 5;
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(size_x, size_y);
    d.print(size_buf);
    const int right_bottom = size_y + INFO_LINE_H;
    const int right_left = min(box_x, size_x);

    const int sample_y = title_bottom + 4;
    const int sample_h = screen_h - FONT_DEMO_EDGE - sample_y;
    int wrap_w = screen_w - FONT_DEMO_EDGE * 2;
    if (sample_y < right_bottom) {
        const int beside = right_left - FONT_DEMO_EDGE - 4;
        if (beside >= 24) {
            wrap_w = beside;
        }
    }

    applyFontDemoItem(item);
    d.setTextDatum(textdatum_t::top_left);
    char lines[FONT_DEMO_MAX_LINES][FONT_DEMO_LINE_CHARS];
    int line_count = appendWrappedLines(lines, 0, FONT_DEMO_MAX_LINES, FONT_DEMO_DIGITS, wrap_w);
    if (!isDigitsOnlyFont(item.font)) {
        line_count =
            appendWrappedLines(lines, line_count, FONT_DEMO_MAX_LINES, FONT_DEMO_PANGRAM, wrap_w);
    }

    const int line_h = max(1, d.fontHeight());
    const int content_h = line_count * line_h;
    const int max_scroll = max(0, content_h - max(0, sample_h));
    if (fontDemoScrollY > max_scroll) {
        fontDemoScrollY = max_scroll;
    }
    if (fontDemoScrollY < 0) {
        fontDemoScrollY = 0;
    }

    if (sample_h > 0 && line_count > 0) {
        d.setClipRect(FONT_DEMO_EDGE, sample_y, screen_w - FONT_DEMO_EDGE * 2, sample_h);
        d.setTextColor(APP_COLOR_VALUE, BLACK);
        int y = sample_y - fontDemoScrollY;
        for (int i = 0; i < line_count; i++) {
            if (y + line_h > sample_y && y < sample_y + sample_h) {
                d.drawString(lines[i], FONT_DEMO_EDGE, y);
            }
            y += line_h;
        }
        d.clearClipRect();
    }

    resetUiFont();
}

void enterFontDemoApp() {
    fontDemoIndex = 0;
    fontDemoScrollY = 0;
    fontDemoHelpVisible = false;
    drawFontDemoApp();
}

bool closeFontDemoHelp() {
    if (!fontDemoHelpVisible) {
        return false;
    }
    fontDemoHelpVisible = false;
    drawFontDemoApp();
    return true;
}

void handleFontDemoNav(const Keyboard_Class::KeysState& status) {
    const String key = getPressedKey();
    if (key == "h") {
        if (fontDemoHelpVisible) {
            closeFontDemoHelp();
        } else {
            fontDemoHelpVisible = true;
            drawFontHelpPage();
        }
        return;
    }
    if (fontDemoHelpVisible) {
        return;
    }

    const int page_delta = getFontDemoPageDelta(status);
    const int scroll_delta = getFontDemoScrollDelta(status);
    if (page_delta == 0 && scroll_delta == 0) {
        return;
    }
    // isPressed 每帧都会进：短冷却，一次按键只走一步
    static uint32_t last_nav_ms = 0;
    const uint32_t now = millis();
    if (now - last_nav_ms < 160) {
        return;
    }
    last_nav_ms = now;

    if (page_delta != 0) {
        fontDemoIndex = (fontDemoIndex + page_delta + FONT_DEMO_COUNT) % FONT_DEMO_COUNT;
        fontDemoScrollY = 0;
        drawFontDemoApp();
        return;
    }

    applyFontDemoItem(FONT_DEMO_ITEMS[fontDemoIndex]);
    int step = M5Cardputer.Display.fontHeight();
    if (step < 8) {
        step = 8;
    }
    resetUiFont();
    fontDemoScrollY += scroll_delta * step;
    drawFontDemoApp();
}
