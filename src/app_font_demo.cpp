#include "app_font_demo.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include <cstdio>

struct FontDemoItem {
    const char* name;
    const lgfx::IFont* font;
    uint8_t text_size;  // font 为 null 时生效
    const char* sample;
};

// M5GFX / LovyanGFX 当前可用的内置字体清单（引用即链接进固件）
static const FontDemoItem FONT_DEMO_ITEMS[] = {
    {"Default 1x", nullptr, 1, "Hello Cardputer! 0123"},
    {"Default 2x", nullptr, 2, "Hello Cardputer!"},
    {"Font0", &fonts::Font0, 1, "GLCD Font0 ABC 123"},
    {"Font2", &fonts::Font2, 1, "Font2 abc XYZ 456"},
    {"Font4", &fonts::Font4, 1, "Font4 abc XYZ 456"},
    {"Font6", &fonts::Font6, 1, "apm.:-0369"},
    {"Font7", &fonts::Font7, 1, "12:45.:-147"},
    {"Font8", &fonts::Font8, 1, "0123456789"},
    {"Font8x8C64", &fonts::Font8x8C64, 1, "C64 8x8 font"},
    {"Ascii8x16", &fonts::AsciiFont8x16, 1, "ASCII 8x16"},
    {"TomThumb", &fonts::TomThumb, 1, "Tiny TomThumb"},
    {"FreeMono9", nullptr, 1, "listed: FreeMono 9pt"},
    {"FreeMono12", &fonts::FreeMono12pt7b, 1, "FreeMono 12pt"},
    {"FreeMono18", nullptr, 1, "listed: FreeMono 18pt"},
    {"FreeMono24", nullptr, 1, "listed: FreeMono 24pt"},
    {"FreeMonoBold9", nullptr, 1, "listed: Mono Bold 9"},
    {"FreeMonoBold12", nullptr, 1, "listed: Mono Bold 12"},
    {"FreeMonoBold18", nullptr, 1, "listed: Mono Bold 18"},
    {"FreeMonoBold24", nullptr, 1, "listed: Mono Bold 24"},
    {"FreeMonoObl9", nullptr, 1, "listed: Mono Oblique 9"},
    {"FreeMonoObl12", nullptr, 1, "listed: Mono Oblique 12"},
    {"FreeMonoObl18", nullptr, 1, "listed: Mono Oblique 18"},
    {"FreeMonoObl24", nullptr, 1, "listed: Mono Oblique 24"},
    {"FreeMonoBoldObl9", nullptr, 1, "listed: Mono Bold Oblique 9"},
    {"FreeMonoBoldObl12", nullptr, 1, "listed: Mono Bold Oblique 12"},
    {"FreeMonoBoldObl18", nullptr, 1, "listed: Mono Bold Oblique 18"},
    {"FreeMonoBoldObl24", nullptr, 1, "listed: Mono Bold Oblique 24"},
    {"FreeSans9", nullptr, 1, "listed: FreeSans 9pt"},
    {"FreeSans12", &fonts::FreeSans12pt7b, 1, "FreeSans 12pt"},
    {"FreeSans18", nullptr, 1, "listed: FreeSans 18pt"},
    {"FreeSans24", nullptr, 1, "listed: FreeSans 24pt"},
    {"FreeSansBold9", nullptr, 1, "listed: Sans Bold 9"},
    {"FreeSansBold12", nullptr, 1, "listed: Sans Bold 12"},
    {"FreeSansBold18", nullptr, 1, "listed: Sans Bold 18"},
    {"FreeSansBold24", nullptr, 1, "listed: Sans Bold 24"},
    {"FreeSansObl9", nullptr, 1, "listed: Sans Oblique 9"},
    {"FreeSansObl12", nullptr, 1, "listed: Sans Oblique 12"},
    {"FreeSansObl18", nullptr, 1, "listed: Sans Oblique 18"},
    {"FreeSansObl24", nullptr, 1, "listed: Sans Oblique 24"},
    {"FreeSansBoldObl9", nullptr, 1, "listed: Sans Bold Oblique 9"},
    {"FreeSansBoldObl12", nullptr, 1, "listed: Sans Bold Oblique 12"},
    {"FreeSansBoldObl18", nullptr, 1, "listed: Sans Bold Oblique 18"},
    {"FreeSansBoldObl24", nullptr, 1, "listed: Sans Bold Oblique 24"},
    {"FreeSerif9", nullptr, 1, "listed: FreeSerif 9pt"},
    {"FreeSerif12", &fonts::FreeSerif12pt7b, 1, "FreeSerif 12pt"},
    {"FreeSerif18", nullptr, 1, "listed: FreeSerif 18pt"},
    {"FreeSerif24", nullptr, 1, "listed: FreeSerif 24pt"},
    {"FreeSerifBold9", nullptr, 1, "listed: Serif Bold 9"},
    {"FreeSerifBold12", nullptr, 1, "listed: Serif Bold 12"},
    {"FreeSerifBold18", nullptr, 1, "listed: Serif Bold 18"},
    {"FreeSerifBold24", nullptr, 1, "listed: Serif Bold 24"},
    {"FreeSerifItalic9", nullptr, 1, "listed: Serif Italic 9"},
    {"FreeSerifItalic12", nullptr, 1, "listed: Serif Italic 12"},
    {"FreeSerifItalic18", nullptr, 1, "listed: Serif Italic 18"},
    {"FreeSerifItalic24", nullptr, 1, "listed: Serif Italic 24"},
    {"FreeSerifBoldItalic9", nullptr, 1, "listed: Serif Bold Italic 9"},
    {"FreeSerifBoldItalic12", nullptr, 1, "listed: Serif Bold Italic 12"},
    {"FreeSerifBoldItalic18", nullptr, 1, "listed: Serif Bold Italic 18"},
    {"FreeSerifBoldItalic24", nullptr, 1, "listed: Serif Bold Italic 24"},
    {"Orbitron24", &fonts::Orbitron_Light_24, 1, "Orbitron Light"},
    {"Roboto24", &fonts::Roboto_Thin_24, 1, "Roboto Thin"},
    {"Satisfy24", &fonts::Satisfy_24, 1, "Satisfy"},
    {"DejaVu12", &fonts::DejaVu12, 1, "DejaVu 12"},
    {"DejaVu18", &fonts::DejaVu18, 1, "DejaVu 18"},
};

static constexpr int FONT_DEMO_COUNT =
    static_cast<int>(sizeof(FONT_DEMO_ITEMS) / sizeof(FONT_DEMO_ITEMS[0]));

static int fontDemoIndex = 0;

static int getFontDemoPageCount() {
    return FONT_DEMO_COUNT;
}

// 应用字体设置
static void applyFontDemoItem(const FontDemoItem& item) {
    if (item.font != nullptr) {
        M5Cardputer.Display.setFont(item.font);
        M5Cardputer.Display.setTextSize(1);
    } else {
        M5Cardputer.Display.setFont(nullptr);
        M5Cardputer.Display.setTextSize(item.text_size);
    }
}

void drawFontDemoApp() {
    beginAppScreen("Font");

    const FontDemoItem& item = FONT_DEMO_ITEMS[fontDemoIndex];
    int y = APP_CONTENT_Y;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d", fontDemoIndex + 1, FONT_DEMO_COUNT);
    drawInfoLineAt(APP_CONTENT_X, y, buf, item.name, 1);
    y += INFO_LINE_H + 2;

    const int hint_h = 12;
    const int sample_y = y + 4;
    const int sample_h = M5Cardputer.Display.height() - sample_y - hint_h - 2;

    M5Cardputer.Display.fillRect(APP_CONTENT_X, sample_y, M5Cardputer.Display.width() - APP_CONTENT_X * 2,
                                 sample_h, BLACK);

    applyFontDemoItem(item);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setTextWrap(true, false);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, sample_y + 2);
    M5Cardputer.Display.println(item.sample);

    // 数字与符号补充行（Font6/7/8 以外也展示）
    if (item.font != &fonts::Font6 && item.font != &fonts::Font7 && item.font != &fonts::Font8) {
        M5Cardputer.Display.println("AaBb 09 +-*/");
    }

    M5Cardputer.Display.setTextWrap(false, false);
    M5Cardputer.Display.setFont(nullptr);
    M5Cardputer.Display.setTextSize(1);

    const int hint_y = M5Cardputer.Display.height() - hint_h;
    int cx = APP_CONTENT_X;
    cx += drawArrowBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("page");
}

void enterFontDemoApp() {
    fontDemoIndex = 0;
    drawFontDemoApp();
}

void handleFontDemoNav(const Keyboard_Class::KeysState& status) {
    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return;
    }
    const int count = getFontDemoPageCount();
    fontDemoIndex = (fontDemoIndex + delta + count) % count;
    drawFontDemoApp();
}
