#include "app_i2c_scan.h"
#include "app_common.h"
#include "app_header.h"
#include "app_radio.h"
#include <cstring>

namespace {

struct I2cDevHint {
    uint8_t addr;
    const char* chip;
    const char* role;
};

static constexpr I2cDevHint kI2cInDevs[] = {
    {0x18, "ES8311", "codec"},
    {0x34, "TCA8418", "keyboard"},
    {0x68, "BMI270", "IMU"},
    {0x69, "BMI270", "IMU"},
};

static constexpr I2cDevHint kI2cExDevs[] = {
    {0x10, "RDA5807M", "radio"},
    {0x11, "RDA5807M", "radio"},
    {0x18, "ES8311", "codec"},
    {0x23, "BH1750", "light"},
    {0x26, "MiniScale", "weight"},
    {0x29, "VL53L0X", "ToF"},
    {0x34, "TCA8418", "keyboard"},
    {0x3C, "SSD1306", "OLED"},
    {0x3D, "SSD1306", "OLED"},
    {0x41, "8Encoder", "encoder"},
    {0x43, "8Angle", "angle"},
    {0x44, "SHT3x", "ENV"},
    {0x48, "ADS1115", "ADC"},
    {0x50, "UnitNFC", "ST25R3916"},
    {0x51, "BM8563", "RTC"},
    {0x57, "UnitUS", "sonar"},
    {0x5A, "MLX90614", "NCIR"},
    {0x5F, "CardKB", "keyboard"},
    {0x60, "TEA5767", "radio"},
    {0x61, "PbHub", "hub"},
    {0x68, "BMI270", "IMU"},
    {0x69, "BMI270", "IMU"},
    {0x70, "QMP6988", "ENV"},
    {0x76, "BMP280", "ENV"},
    {0x77, "BMP280", "ENV"},
};

static bool g_i2c_help_visible = false;

static const I2cDevHint* findI2cDevHint(const uint8_t addr, const bool internal_bus) {
    const I2cDevHint* table = internal_bus ? kI2cInDevs : kI2cExDevs;
    const int n = internal_bus ? static_cast<int>(sizeof(kI2cInDevs) / sizeof(kI2cInDevs[0]))
                               : static_cast<int>(sizeof(kI2cExDevs) / sizeof(kI2cExDevs[0]));
    for (int i = 0; i < n; ++i) {
        if (table[i].addr == addr) {
            return &table[i];
        }
    }
    return nullptr;
}

static void drawI2cHelpPage(const bool internal_bus) {
    int y = drawAppHelpBegin(internal_bus ? "InI2" : "ExI2");
    constexpr int x = APP_HELP_CONTENT_X;
    y = drawAppHelpKey(x, y, 'r', "rescan bus");
    if (internal_bus) {
        y = drawAppHelpText(x, y, "onboard chips, confirmed:");
        y = drawAppHelpLabelText(x, y, "0x18", APP_COLOR_LABEL, " ES8311  codec");
        y = drawAppHelpLabelText(x, y, "0x34", APP_COLOR_LABEL, " TCA8418 keyboard");
        y = drawAppHelpLabelText(x, y, "0x68", APP_COLOR_LABEL, " BMI270  IMU");
        y = drawAppHelpText(x, y, "EXT14 top-down: 5V red");
        y = drawAppHelpText(x, y, "SDA cyan  SCL yellow");
    } else {
        y = drawAppHelpText(x, y, "left Grove: GND 5V G2 G1");
        y = drawAppHelpText(x, y, "G2=SDA  G1=SCL");
        y = drawAppHelpText(x, y, "names are likely matches");
        y = drawAppHelpLabelText(x, y, "10/11", APP_COLOR_LABEL, " RDA5807M radio");
        y = drawAppHelpLabelText(x, y, "0x50", APP_COLOR_LABEL, " UnitNFC ST25R3916");
        y = drawAppHelpLabelText(x, y, "0x60", APP_COLOR_LABEL, " TEA5767 radio");
        y = drawAppHelpText(x, y, "unknown addr shows as --");
    }
    drawHelpHintRight("close");
}

static constexpr int I2C_PIN_CELL = 16;
static constexpr int I2C_PIN_SOCK = 8;
static constexpr int I2C_PIN_FONT_H = 8;
static constexpr int I2C_PIN_LEAD_CLEAR = 12;
static constexpr int I2C_PIN_LEAD_STUB = 4;
static constexpr int I2C_PIN_LABEL_GAP = 6;
static constexpr int I2C_PIN_LEAD_GAP = 3;
static constexpr int I2C_SCREEN_W = 240;
static constexpr int I2C_SCREEN_H = 135;
static constexpr uint16_t I2C_COLOR_GRAY = 0xC618;
static constexpr uint16_t I2C_COLOR_PAD_MUTED = 0x8410;
static constexpr int I2C_PAD_GAP = 2;

static void drawI2cPinPad(const int px, const int py, const uint16_t color) {
    auto& d = M5Cardputer.Display;
    constexpr int off = (I2C_PIN_CELL - I2C_PIN_SOCK) / 2;
    d.drawRect(px, py, I2C_PIN_CELL, I2C_PIN_CELL, color);
    d.fillRect(px + off, py + off, I2C_PIN_SOCK, I2C_PIN_SOCK, color);
}

static void drawI2cPinLead(const int pad_x, const int pad_y, const int label_x, const int label_y,
                           const uint16_t color) {
    auto& d = M5Cardputer.Display;
    const int dx = label_x - pad_x;
    const int dy = label_y - pad_y;
    if (dx == 0 && dy == 0) {
        return;
    }
    int stub_x = pad_x;
    int stub_y = pad_y;
    if (abs(dy) >= abs(dx)) {
        stub_y += (dy > 0) ? I2C_PIN_LEAD_STUB : -I2C_PIN_LEAD_STUB;
    } else {
        stub_x += (dx > 0) ? I2C_PIN_LEAD_STUB : -I2C_PIN_LEAD_STUB;
    }
    int end_x = label_x;
    int end_y = label_y;
    if (abs(dy) >= abs(dx)) {
        end_y -= (dy > 0) ? I2C_PIN_LEAD_GAP : -I2C_PIN_LEAD_GAP;
    } else {
        end_x -= (dx > 0) ? I2C_PIN_LEAD_GAP : -I2C_PIN_LEAD_GAP;
    }
    d.drawLine(pad_x, pad_y, stub_x, stub_y, color);
    d.drawLine(stub_x, stub_y, end_x, end_y, color);
}

static int drawI2cGrovePinout(const int x, const int y) {
    auto& d = M5Cardputer.Display;
    constexpr int rows = 4;
    struct GrovePin {
        const char* gpio;
        const char* func;
        uint16_t pad;
        uint16_t func_color;
    };
    static constexpr GrovePin kPins[rows] = {
        {"GND", nullptr, I2C_COLOR_PAD_MUTED, WHITE},
        {"5V", nullptr, APP_COLOR_ERROR, WHITE},
        {"G2", "SDA", CYAN, CYAN},
        {"G1", "SCL", YELLOW, YELLOW},
    };

    d.setTextSize(1);
    int max_tw = 0;
    for (int i = 0; i < rows; ++i) {
        int tw = d.textWidth(kPins[i].gpio);
        if (kPins[i].func != nullptr) {
            tw += d.textWidth(" ") + d.textWidth(kPins[i].func);
        }
        if (tw > max_tw) {
            max_tw = tw;
        }
    }

    const int label_x = x + I2C_PIN_CELL + I2C_PIN_LEAD_CLEAR;
    const int stride = I2C_PIN_CELL + I2C_PAD_GAP;
    for (int i = 0; i < rows; ++i) {
        const int py = y + i * stride;
        const int pad_cy = py + I2C_PIN_CELL / 2;
        const int text_y = pad_cy - I2C_PIN_FONT_H / 2;
        const uint16_t c = kPins[i].pad;

        drawI2cPinPad(x, py, c);
        drawI2cPinLead(x + I2C_PIN_CELL - 1, pad_cy, label_x, text_y + I2C_PIN_FONT_H / 2, c);
        d.setTextColor(WHITE, BLACK);
        d.setCursor(label_x, text_y);
        d.print(kPins[i].gpio);
        if (kPins[i].func != nullptr) {
            d.print(" ");
            d.setTextColor(kPins[i].func_color, BLACK);
            d.print(kPins[i].func);
        }
    }
    return I2C_PIN_CELL + I2C_PIN_LEAD_CLEAR + max_tw;
}

static int i2cGrovePinoutHeight() {
    return 4 * I2C_PIN_CELL + 3 * I2C_PAD_GAP;
}

static int drawI2cExt14Pinout(const int y) {
    auto& d = M5Cardputer.Display;
    constexpr int cols = 7;

    static constexpr const char* kTop[cols] = {"5VIN", "GND", "5VOUT", "SDA", "SCL", "G13", "G15"};
    static constexpr const char* kBot[cols] = {"G3", "G4", "G6", "G40", "G14", "G39", "G5"};

    auto pinColor = [](const char* name) -> uint16_t {
        if (name[0] == '5') {
            return APP_COLOR_ERROR;
        }
        if (strcmp(name, "SDA") == 0) {
            return CYAN;
        }
        if (strcmp(name, "SCL") == 0) {
            return YELLOW;
        }
        return I2C_COLOR_PAD_MUTED;
    };

    d.setTextSize(1);
    const int stride = I2C_PIN_CELL + I2C_PAD_GAP;
    const int grid_w = cols * I2C_PIN_CELL + (cols - 1) * I2C_PAD_GAP;
    const int x = (I2C_SCREEN_W - grid_w) / 2;

    struct PinLabel {
        int pad_cx;
        int text_x;
        int tw;
    };
    PinLabel top_lbl[cols]{};
    PinLabel bot_lbl[cols]{};

    for (int i = 0; i < cols; ++i) {
        const int pad_cx = x + (cols - 1 - i) * stride + I2C_PIN_CELL / 2;
        top_lbl[i].pad_cx = pad_cx;
        top_lbl[i].tw = d.textWidth(kTop[i]);
        bot_lbl[i].pad_cx = pad_cx;
        bot_lbl[i].tw = d.textWidth(kBot[i]);
    }

    auto packRow = [&](PinLabel* lbls) {
        int total = 0;
        for (int i = 0; i < cols; ++i) {
            total += lbls[i].tw + I2C_PIN_LABEL_GAP;
        }
        total -= I2C_PIN_LABEL_GAP;
        int cursor = x + (grid_w - total) / 2;
        if (cursor < 0) {
            cursor = 0;
        }
        if (cursor + total > I2C_SCREEN_W - 1) {
            cursor = I2C_SCREEN_W - 1 - total;
            if (cursor < 0) {
                cursor = 0;
            }
        }
        for (int vis = 0; vis < cols; ++vis) {
            const int i = cols - 1 - vis;
            lbls[i].text_x = cursor;
            cursor += lbls[i].tw + I2C_PIN_LABEL_GAP;
        }
    };
    packRow(top_lbl);
    packRow(bot_lbl);

    const int top_text_y = y;
    const int top_pad_y = top_text_y + I2C_PIN_FONT_H + I2C_PIN_LEAD_CLEAR;
    const int bot_pad_y = top_pad_y + I2C_PIN_CELL + I2C_PAD_GAP;
    const int bot_text_y = bot_pad_y + I2C_PIN_CELL + I2C_PIN_LEAD_CLEAR;

    for (int i = 0; i < cols; ++i) {
        const int px = x + (cols - 1 - i) * stride;
        const uint16_t top_c = pinColor(kTop[i]);
        const uint16_t bot_c = pinColor(kBot[i]);
        const int top_lcx = top_lbl[i].text_x + top_lbl[i].tw / 2;
        const int bot_lcx = bot_lbl[i].text_x + bot_lbl[i].tw / 2;

        drawI2cPinLead(top_lbl[i].pad_cx, top_pad_y, top_lcx, top_text_y + I2C_PIN_FONT_H, top_c);
        drawI2cPinPad(px, top_pad_y, top_c);
        drawI2cPinPad(px, bot_pad_y, bot_c);
        drawI2cPinLead(bot_lbl[i].pad_cx, bot_pad_y + I2C_PIN_CELL - 1, bot_lcx, bot_text_y, bot_c);

        d.setTextColor(WHITE, BLACK);
        d.setCursor(top_lbl[i].text_x, top_text_y);
        d.print(kTop[i]);
        d.setTextColor(WHITE, BLACK);
        d.setCursor(bot_lbl[i].text_x, bot_text_y);
        d.print(kBot[i]);
    }

    return bot_text_y + I2C_PIN_FONT_H - y;
}

} // namespace

void drawI2cScanApp(m5::I2C_Class& bus, const char* title, const bool internal_bus) {
    bool found[120]{};
    if (bus.isEnabled()) {
        bus.begin();
        bus.scanID(found);
        silenceFmRadioOnBus(bus);
    }

    clearAppHeaderStatusRefresh();
    M5Cardputer.Display.fillScreen(BLACK);

    constexpr int edge = APP_HELP_EDGE;
    constexpr int chart_list_gap = 10;
    constexpr int title_h = 8;

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(edge, edge);
    M5Cardputer.Display.print(title);

    int x = edge;
    int y = edge + title_h + 2;
    int list_max = 7;

    if (internal_bus) {
        const int chart_y = y;
        y = chart_y + drawI2cExt14Pinout(chart_y) + chart_list_gap;
        list_max = 3;
    } else {
        const int grove_h = i2cGrovePinoutHeight();
        const int grove_y = (I2C_SCREEN_H - grove_h) / 2;
        const int grove_w = drawI2cGrovePinout(edge, grove_y);
        x = edge + grove_w + chart_list_gap;
        y = edge + title_h + 2;
        list_max = 8;
    }
    M5Cardputer.Display.setTextSize(1);

    if (!bus.isEnabled()) {
        M5Cardputer.Display.setTextColor(APP_COLOR_ERROR, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("bus disabled");
        return;
    }

    constexpr int row_h = 12;
    constexpr int dot_size = 4;
    const int text_x = x + dot_size + 4;
    constexpr int addr_w = 36;
    const int list_right = I2C_SCREEN_W - edge;
    int count = 0;
    int shown = 0;
    for (int addr = 8; addr < 0x78; ++addr) {
        if (!found[addr]) {
            continue;
        }
        ++count;
        if (shown >= list_max) {
            continue;
        }
        const I2cDevHint* hint = findI2cDevHint(static_cast<uint8_t>(addr), internal_bus);
        const uint16_t dot_color = hint != nullptr ? APP_COLOR_OK : I2C_COLOR_GRAY;
        M5Cardputer.Display.fillCircle(x + dot_size / 2, y + 3, dot_size / 2, dot_color);

        char addr_text[8];
        snprintf(addr_text, sizeof(addr_text), "0x%02X", addr);
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(text_x, y);
        M5Cardputer.Display.print(addr_text);

        const char* chip = hint != nullptr ? hint->chip : "--";
        const char* role = hint != nullptr ? hint->role : "unknown";
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.setCursor(text_x + addr_w, y);
        M5Cardputer.Display.print(chip);

        const int role_x = text_x + addr_w + M5Cardputer.Display.textWidth(chip) + 6;
        if (role_x + M5Cardputer.Display.textWidth(role) <= list_right) {
            M5Cardputer.Display.setTextColor(I2C_COLOR_GRAY, BLACK);
            M5Cardputer.Display.setCursor(role_x, y);
            M5Cardputer.Display.print(role);
        }
        y += row_h;
        ++shown;
    }
    if (count == 0) {
        M5Cardputer.Display.setTextColor(I2C_COLOR_GRAY, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("no device");
    } else if (count > list_max) {
        char more[16];
        snprintf(more, sizeof(more), "+%d more", count - list_max);
        M5Cardputer.Display.setTextColor(I2C_COLOR_GRAY, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(more);
    }
}

void handleI2cScanApp(const String& key, m5::I2C_Class& bus, const char* title,
                      const bool internal_bus) {
    if (key == "r" || key == "R") {
        if (g_i2c_help_visible) {
            return;
        }
        drawI2cScanApp(bus, title, internal_bus);
        return;
    }
    if (key != "h" && key != "H") {
        return;
    }
    g_i2c_help_visible = !g_i2c_help_visible;
    if (g_i2c_help_visible) {
        drawI2cHelpPage(internal_bus);
    } else {
        drawI2cScanApp(bus, title, internal_bus);
    }
}

bool isI2cScanHelpVisible() {
    return g_i2c_help_visible;
}

bool closeI2cScanHelp(m5::I2C_Class& bus, const char* title, const bool internal_bus) {
    if (!g_i2c_help_visible) {
        return false;
    }
    g_i2c_help_visible = false;
    drawI2cScanApp(bus, title, internal_bus);
    return true;
}

void resetI2cScanHelp() {
    g_i2c_help_visible = false;
}
