#include "app_ir.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>

#include <cstring>

// Cardputer / Adv 板载红外发射管
static constexpr uint16_t IR_TX_PIN = 44;

static IRsend g_irsend(IR_TX_PIN);
static IRac g_irac(IR_TX_PIN);
static bool g_ir_ready = false;

enum class IrCategory : uint8_t { TV = 0, AC = 1 };

enum class IrTvBrand : uint8_t {
    Samsung = 0,
    Sony,
    Lg,
    Panasonic,
    Nec,
    Count,
};

enum class IrAcBrand : uint8_t {
    Midea = 0,
    Gree,
    Haier,
    Aux,
    Hisense,
    Xiaomi,
    Count,
};

enum class IrTvAction : uint8_t {
    Power = 0,
    VolUp,
    VolDown,
    Mute,
    ChUp,
    ChDown,
    Input,
    Count,
};

// 空调可调字段
enum class IrAcField : uint8_t {
    Power = 0,
    Mode,
    Temp,
    Fan,
    Count,
};

static IrCategory g_category = IrCategory::TV;
static int g_tv_brand = 0;
static int g_ac_brand = 0;
static int g_tv_action = 0;
static int g_ac_field = 0;

static bool g_ac_power = true;
static stdAc::opmode_t g_ac_mode = stdAc::opmode_t::kCool;
static uint8_t g_ac_temp = 26;
static stdAc::fanspeed_t g_ac_fan = stdAc::fanspeed_t::kAuto;

static bool g_help_visible = false;
static int g_help_page = 0;
static constexpr int IR_HELP_PAGE_COUNT = 2;

static const char* g_tx_status = "";
static uint32_t g_tx_status_until_ms = 0;
static bool g_screen_ready = false;

// 按键反馈：AC / TV 各自一套按钮 id，None=-1
enum class IrAcBtn : int8_t {
    None = -1,
    Power = 0,
    Mode,
    Fan,
    TempDown,
    TempUp,
    Send,
};
enum class IrTvBtn : int8_t {
    None = -1,
    Power = 0,
    VolUp,
    VolDown,
    Mute,
    ChUp,
    ChDown,
    Input,
    Send,
};
static IrAcBtn g_press_ac = IrAcBtn::None;
static IrTvBtn g_press_tv = IrTvBtn::None;
static uint32_t g_press_until_ms = 0;
static constexpr uint32_t IR_PRESS_MS = 160;

static const char* tvBrandName(const int idx) {
    static const char* names[] = {"Samsung", "Sony", "LG", "Panasonic", "NEC"};
    if (idx < 0 || idx >= static_cast<int>(IrTvBrand::Count)) {
        return "?";
    }
    return names[idx];
}

static const char* acBrandName(const int idx) {
    static const char* names[] = {"Midea", "Gree", "Haier", "AUX", "Hisense", "Xiaomi"};
    if (idx < 0 || idx >= static_cast<int>(IrAcBrand::Count)) {
        return "?";
    }
    return names[idx];
}

static const char* tvActionName(const int idx) {
    static const char* names[] = {"Power", "Vol+", "Vol-", "Mute", "Ch+", "Ch-", "Input"};
    if (idx < 0 || idx >= static_cast<int>(IrTvAction::Count)) {
        return "?";
    }
    return names[idx];
}

static const char* acModeName(const stdAc::opmode_t mode) {
    switch (mode) {
        case stdAc::opmode_t::kCool:
            return "Cool";
        case stdAc::opmode_t::kHeat:
            return "Heat";
        case stdAc::opmode_t::kDry:
            return "Dry";
        case stdAc::opmode_t::kFan:
            return "Fan";
        case stdAc::opmode_t::kAuto:
            return "Auto";
        default:
            return "?";
    }
}

static const char* acFanName(const stdAc::fanspeed_t fan) {
    switch (fan) {
        case stdAc::fanspeed_t::kAuto:
            return "Auto";
        case stdAc::fanspeed_t::kMin:
            return "Min";
        case stdAc::fanspeed_t::kLow:
            return "Low";
        case stdAc::fanspeed_t::kMedium:
            return "Med";
        case stdAc::fanspeed_t::kHigh:
            return "High";
        case stdAc::fanspeed_t::kMax:
            return "Max";
        default:
            return "?";
    }
}

static decode_type_t acProtocol(const int brand) {
    switch (static_cast<IrAcBrand>(brand)) {
        case IrAcBrand::Midea:
            return decode_type_t::MIDEA;
        case IrAcBrand::Gree:
            return decode_type_t::GREE;
        case IrAcBrand::Haier:
            return decode_type_t::HAIER_AC176;
        case IrAcBrand::Aux:
            return decode_type_t::ELECTRA_AC;
        case IrAcBrand::Hisense:
            return decode_type_t::KELON;
        case IrAcBrand::Xiaomi:
            // 多数小米/酷批机用 Coolix；壁挂 OEM 可再试 Midea
            return decode_type_t::COOLIX;
        default:
            return decode_type_t::MIDEA;
    }
}

static void ensureIrReady() {
    if (g_ir_ready) {
        return;
    }
    g_irsend.begin();
    g_ir_ready = true;
}

static void setTxStatus(const char* text) {
    g_tx_status = text;
    g_tx_status_until_ms = millis() + 1500;
}

// 常用电视红外码（公开遥控码表，机型可能有差异）
static void sendTvAction() {
    ensureIrReady();
    const auto brand = static_cast<IrTvBrand>(g_tv_brand);
    const auto action = static_cast<IrTvAction>(g_tv_action);

    switch (brand) {
        case IrTvBrand::Samsung: {
            uint32_t code = 0;
            switch (action) {
                case IrTvAction::Power:
                    code = 0xE0E040BF;
                    break;
                case IrTvAction::VolUp:
                    code = 0xE0E0E01F;
                    break;
                case IrTvAction::VolDown:
                    code = 0xE0E0D02F;
                    break;
                case IrTvAction::Mute:
                    code = 0xE0E0F00F;
                    break;
                case IrTvAction::ChUp:
                    code = 0xE0E048B7;
                    break;
                case IrTvAction::ChDown:
                    code = 0xE0E008F7;
                    break;
                case IrTvAction::Input:
                    code = 0xE0E0807F;
                    break;
                default:
                    break;
            }
            g_irsend.sendSAMSUNG(code);
            break;
        }
        case IrTvBrand::Sony: {
            uint16_t code = 0;
            switch (action) {
                case IrTvAction::Power:
                    code = 0xA90;
                    break;
                case IrTvAction::VolUp:
                    code = 0x490;
                    break;
                case IrTvAction::VolDown:
                    code = 0xC90;
                    break;
                case IrTvAction::Mute:
                    code = 0x290;
                    break;
                case IrTvAction::ChUp:
                    code = 0x090;
                    break;
                case IrTvAction::ChDown:
                    code = 0x890;
                    break;
                case IrTvAction::Input:
                    code = 0xA50;
                    break;
                default:
                    break;
            }
            g_irsend.sendSony(code, 12, 2);
            break;
        }
        case IrTvBrand::Lg: {
            uint32_t code = 0;
            switch (action) {
                case IrTvAction::Power:
                    code = 0x20DF10EF;
                    break;
                case IrTvAction::VolUp:
                    code = 0x20DF40BF;
                    break;
                case IrTvAction::VolDown:
                    code = 0x20DFC03F;
                    break;
                case IrTvAction::Mute:
                    code = 0x20DF906F;
                    break;
                case IrTvAction::ChUp:
                    code = 0x20DF00FF;
                    break;
                case IrTvAction::ChDown:
                    code = 0x20DF807F;
                    break;
                case IrTvAction::Input:
                    code = 0x20DFD02F;
                    break;
                default:
                    break;
            }
            g_irsend.sendLG(code);
            break;
        }
        case IrTvBrand::Panasonic: {
            uint32_t data = 0;
            switch (action) {
                case IrTvAction::Power:
                    data = 0x100BCBD;
                    break;
                case IrTvAction::VolUp:
                    data = 0x1000405;
                    break;
                case IrTvAction::VolDown:
                    data = 0x1008485;
                    break;
                case IrTvAction::Mute:
                    data = 0x1004C4D;
                    break;
                case IrTvAction::ChUp:
                    data = 0x1002C2D;
                    break;
                case IrTvAction::ChDown:
                    data = 0x100ACAD;
                    break;
                case IrTvAction::Input:
                    data = 0x100A0A1;
                    break;
                default:
                    break;
            }
            g_irsend.sendPanasonic(0x4004, data);
            break;
        }
        case IrTvBrand::Nec: {
            uint64_t code = 0;
            switch (action) {
                case IrTvAction::Power:
                    code = 0x00FF02FD;
                    break;
                case IrTvAction::VolUp:
                    code = 0x00FFA857;
                    break;
                case IrTvAction::VolDown:
                    code = 0x00FFE01F;
                    break;
                case IrTvAction::Mute:
                    code = 0x00FF906F;
                    break;
                case IrTvAction::ChUp:
                    code = 0x00FFE21D;
                    break;
                case IrTvAction::ChDown:
                    code = 0x00FF629D;
                    break;
                case IrTvAction::Input:
                    code = 0x00FF22DD;
                    break;
                default:
                    break;
            }
            g_irsend.sendNEC(code);
            break;
        }
        default:
            setTxStatus("fail");
            return;
    }
    setTxStatus("sent");
}

static void sendAcState() {
    ensureIrReady();
    stdAc::state_t s = {};
    s.protocol = acProtocol(g_ac_brand);
    s.model = -1;
    s.power = g_ac_power;
    s.mode = g_ac_mode;
    s.degrees = g_ac_temp;
    s.celsius = true;
    s.fanspeed = g_ac_fan;
    s.swingv = stdAc::swingv_t::kOff;
    s.swingh = stdAc::swingh_t::kOff;
    s.quiet = false;
    s.turbo = false;
    s.econo = false;
    s.light = true;
    s.filter = false;
    s.clean = false;
    s.beep = false;
    s.sleep = -1;
    s.clock = -1;
    g_irac.sendAc(s, nullptr);
    setTxStatus("sent");
}

static void sendCurrent() {
    if (g_category == IrCategory::TV) {
        sendTvAction();
    } else {
        sendAcState();
    }
}

static int getHorizontalDelta(const Keyboard_Class::KeysState& status) {
    int delta = 0;
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x50 || hid == 0x36) {
            delta = -1;
        } else if (hid == 0x4F || hid == 0x38) {
            delta = 1;
        }
    }
    for (const char c : status.word) {
        if (c == ',') {
            delta = -1;
        } else if (c == '/') {
            delta = 1;
        }
    }
    return delta;
}

static int getVerticalDelta(const Keyboard_Class::KeysState& status) {
    int delta = 0;
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            delta = -1;
        } else if (hid == 0x51 || hid == 0x37) {
            delta = 1;
        }
    }
    for (const char c : status.word) {
        if (c == ';') {
            delta = -1;
        } else if (c == '.') {
            delta = 1;
        }
    }
    return delta;
}

static void cycleAcMode(const int delta) {
    static const stdAc::opmode_t modes[] = {
        stdAc::opmode_t::kCool, stdAc::opmode_t::kHeat, stdAc::opmode_t::kDry,
        stdAc::opmode_t::kFan,  stdAc::opmode_t::kAuto,
    };
    constexpr int n = 5;
    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (modes[i] == g_ac_mode) {
            idx = i;
            break;
        }
    }
    idx = (idx + delta + n) % n;
    g_ac_mode = modes[idx];
}

static void cycleAcFan(const int delta) {
    static const stdAc::fanspeed_t fans[] = {
        stdAc::fanspeed_t::kAuto, stdAc::fanspeed_t::kMin, stdAc::fanspeed_t::kLow,
        stdAc::fanspeed_t::kMedium, stdAc::fanspeed_t::kHigh, stdAc::fanspeed_t::kMax,
    };
    constexpr int n = 6;
    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (fans[i] == g_ac_fan) {
            idx = i;
            break;
        }
    }
    idx = (idx + delta + n) % n;
    g_ac_fan = fans[idx];
}

static void adjustAcField(const int delta) {
    switch (static_cast<IrAcField>(g_ac_field)) {
        case IrAcField::Power:
            g_ac_power = !g_ac_power;
            break;
        case IrAcField::Mode:
            cycleAcMode(delta == 0 ? 1 : delta);
            break;
        case IrAcField::Temp: {
            int t = static_cast<int>(g_ac_temp) + (delta == 0 ? 1 : delta);
            if (t < 16) {
                t = 16;
            }
            if (t > 30) {
                t = 30;
            }
            g_ac_temp = static_cast<uint8_t>(t);
            break;
        }
        case IrAcField::Fan:
            cycleAcFan(delta == 0 ? 1 : delta);
            break;
        default:
            break;
    }
}

static int drawHelpKey(const int y, const char key, const char* text) {
    int cx = APP_CONTENT_X + drawKeyBadge(APP_CONTENT_X, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(text);
    return y + 12;
}

static int drawHelpArrows(const int y, const char* text) {
    int cx = APP_CONTENT_X + drawArrowBadge(APP_CONTENT_X, y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(text);
    return y + 12;
}

static int drawHelpTitle(const int y, const char* title) {
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(title);
    return y + INFO_LINE_H_2X;
}

static int drawHelpText(const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(text);
    return y + 12;
}

static void flashAcBtn(const IrAcBtn btn) {
    g_press_ac = btn;
    g_press_tv = IrTvBtn::None;
    g_press_until_ms = millis() + IR_PRESS_MS;
}

static void flashTvBtn(const IrTvBtn btn) {
    g_press_tv = btn;
    g_press_ac = IrAcBtn::None;
    g_press_until_ms = millis() + IR_PRESS_MS;
}

static void drawIrHelpPage() {
    beginAppScreen("Help");
    int y = APP_CONTENT_Y;
    if (g_help_page == 0) {
        y = drawHelpTitle(y, "Keys");
        y = drawHelpKey(y, 't', "TV / AC tab");
        y = drawHelpArrows(y, "brand / field");
        y = drawHelpKey(y, '1', "TV keys 1-7");
        y = drawHelpKey(y, 'p', "AC p/m/f -/= ");
        y = drawHelpKey(y, ' ', "ent/spc send");
        y = drawHelpKey(y, 'h', "help / close");
    } else {
        y = drawHelpTitle(y, "Notes");
        y = drawHelpText(y, "TX only GPIO44");
        y = drawHelpText(y, "TV/AC: remote pad");
        y = drawHelpText(y, "Xiaomi: Coolix");
        y = drawHelpText(y, "OEM? try Midea");
        y = drawHelpText(y, "aim IR window");
    }

    const int hint_y = M5Cardputer.Display.height() - 12;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);
    int cx = APP_CONTENT_X;
    cx += drawArrowBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("page ");
    cx += M5Cardputer.Display.textWidth("page ");
    char buf[8];
    snprintf(buf, sizeof(buf), "%d/%d", g_help_page + 1, IR_HELP_PAGE_COUNT);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print(buf);
    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

// 遥控器垫按钮：黄框/灰底，按下反色
static void drawIrPadBtn(const int x, const int y, const int w, const int h, const bool pressed,
                         const char key, const char* label, const bool selected) {
    const uint16_t fill = pressed ? APP_COLOR_MENU_KEY : (selected ? 0x4208 : BLACK);
    const uint16_t border = selected || pressed ? APP_COLOR_MENU_KEY : APP_COLOR_MUTED;
    M5Cardputer.Display.fillRoundRect(x, y, w, h, 3, fill);
    M5Cardputer.Display.drawRoundRect(x, y, w, h, 3, border);

    const int badge_y = y + (h - 10) / 2;
    int cx = x + 4;
    if (key == ' ') {
        cx += drawTextBadge(cx, badge_y, "SP", 1);
    } else {
        cx += drawKeyBadge(cx, badge_y, key, 1);
    }
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(pressed ? APP_COLOR_KEY_TEXT : APP_COLOR_HINT, fill);
    M5Cardputer.Display.setCursor(cx, badge_y + 1);
    M5Cardputer.Display.print(label);
}

static bool isAcBtnPressed(const IrAcBtn btn) {
    return g_press_ac == btn && static_cast<int32_t>(millis() - g_press_until_ms) < 0;
}

static bool isTvBtnPressed(const IrTvBtn btn) {
    return g_press_tv == btn && static_cast<int32_t>(millis() - g_press_until_ms) < 0;
}

// 顶部 TV / AC tab；返回内容区起始 y
static int drawIrTabs() {
    const int x0 = APP_CONTENT_X;
    const int y = APP_CONTENT_Y;
    constexpr int tab_w = 36;
    constexpr int tab_h = 12;
    constexpr int gap = 4;

    const bool tv_on = g_category == IrCategory::TV;
    M5Cardputer.Display.fillRoundRect(x0, y, tab_w, tab_h, 3, tv_on ? APP_COLOR_MENU_KEY : BLACK);
    M5Cardputer.Display.drawRoundRect(x0, y, tab_w, tab_h, 3,
                                      tv_on ? APP_COLOR_MENU_KEY : APP_COLOR_MUTED);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(tv_on ? APP_COLOR_KEY_TEXT : APP_COLOR_HINT,
                                     tv_on ? APP_COLOR_MENU_KEY : BLACK);
    M5Cardputer.Display.setCursor(x0 + 10, y + 2);
    M5Cardputer.Display.print("TV");

    const int ax = x0 + tab_w + gap;
    const bool ac_on = !tv_on;
    M5Cardputer.Display.fillRoundRect(ax, y, tab_w, tab_h, 3, ac_on ? APP_COLOR_MENU_KEY : BLACK);
    M5Cardputer.Display.drawRoundRect(ax, y, tab_w, tab_h, 3,
                                      ac_on ? APP_COLOR_MENU_KEY : APP_COLOR_MUTED);
    M5Cardputer.Display.setTextColor(ac_on ? APP_COLOR_KEY_TEXT : APP_COLOR_HINT,
                                     ac_on ? APP_COLOR_MENU_KEY : BLACK);
    M5Cardputer.Display.setCursor(ax + 10, y + 2);
    M5Cardputer.Display.print("AC");

    // tab 旁提示 t
    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(ax + tab_w + 6, y + 2);
    M5Cardputer.Display.print("t");

    return y + tab_h + 2;
}

static void drawAcRemotePad(const int content_y) {
    const int x0 = APP_CONTENT_X;
    int y = content_y;

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x0, y);
    M5Cardputer.Display.print(acBrandName(g_ac_brand));
    M5Cardputer.Display.setTextColor(g_ac_power ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x0 + 72, y);
    M5Cardputer.Display.print(g_ac_power ? "ON" : "OFF");
    if (g_tx_status[0] != '\0' && static_cast<int32_t>(millis() - g_tx_status_until_ms) < 0) {
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(x0 + 160, y);
        M5Cardputer.Display.print(g_tx_status);
    }
    y += 11;

    // 紧凑温度区（为 tab 腾高度）
    char tbuf[8];
    snprintf(tbuf, sizeof(tbuf), "%u", static_cast<unsigned>(g_ac_temp));
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(x0, y);
    M5Cardputer.Display.print(tbuf);
    const int temp_w = M5Cardputer.Display.textWidth(tbuf);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(x0 + temp_w + 2, y + 4);
    M5Cardputer.Display.print("C");

    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x0 + 56, y);
    M5Cardputer.Display.print(acModeName(g_ac_mode));
    M5Cardputer.Display.setCursor(x0 + 56, y + 10);
    M5Cardputer.Display.print(acFanName(g_ac_fan));
    y += 22;

    constexpr int btn_w = 72;
    constexpr int btn_h = 16;
    constexpr int gap = 3;
    const int row1 = y;
    const int row2 = y + btn_h + gap;
    drawIrPadBtn(x0, row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Power), 'p', "pwr",
                 g_ac_field == static_cast<int>(IrAcField::Power));
    drawIrPadBtn(x0 + btn_w + gap, row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Mode), 'm', "mode",
                 g_ac_field == static_cast<int>(IrAcField::Mode));
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Fan), 'f',
                 "fan", g_ac_field == static_cast<int>(IrAcField::Fan));
    drawIrPadBtn(x0, row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::TempDown), '-', "temp",
                 g_ac_field == static_cast<int>(IrAcField::Temp));
    drawIrPadBtn(x0 + btn_w + gap, row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::TempUp), '=', "temp",
                 g_ac_field == static_cast<int>(IrAcField::Temp));
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Send), ' ',
                 "send", false);
}

static IrTvBtn tvActionToBtn(const int action) {
    switch (static_cast<IrTvAction>(action)) {
        case IrTvAction::Power:
            return IrTvBtn::Power;
        case IrTvAction::VolUp:
            return IrTvBtn::VolUp;
        case IrTvAction::VolDown:
            return IrTvBtn::VolDown;
        case IrTvAction::Mute:
            return IrTvBtn::Mute;
        case IrTvAction::ChUp:
            return IrTvBtn::ChUp;
        case IrTvAction::ChDown:
            return IrTvBtn::ChDown;
        case IrTvAction::Input:
            return IrTvBtn::Input;
        default:
            return IrTvBtn::None;
    }
}

static void drawTvRemotePad(const int content_y) {
    const int x0 = APP_CONTENT_X;
    int y = content_y;

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x0, y);
    M5Cardputer.Display.print(tvBrandName(g_tv_brand));
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(x0 + 72, y);
    M5Cardputer.Display.print(tvActionName(g_tv_action));
    if (g_tx_status[0] != '\0' && static_cast<int32_t>(millis() - g_tx_status_until_ms) < 0) {
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(x0 + 160, y);
        M5Cardputer.Display.print(g_tx_status);
    }
    y += 12;

    constexpr int btn_w = 72;
    constexpr int btn_h = 16;
    constexpr int gap = 3;
    const int row1 = y;
    const int row2 = y + btn_h + gap;
    const int row3 = y + 2 * (btn_h + gap);

    drawIrPadBtn(x0, row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Power), '1', "pwr",
                 g_tv_action == static_cast<int>(IrTvAction::Power));
    drawIrPadBtn(x0 + btn_w + gap, row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::VolUp), '2', "vol+",
                 g_tv_action == static_cast<int>(IrTvAction::VolUp));
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::VolDown), '3',
                 "vol-", g_tv_action == static_cast<int>(IrTvAction::VolDown));

    drawIrPadBtn(x0, row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Mute), '4', "mute",
                 g_tv_action == static_cast<int>(IrTvAction::Mute));
    drawIrPadBtn(x0 + btn_w + gap, row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::ChUp), '5', "ch+",
                 g_tv_action == static_cast<int>(IrTvAction::ChUp));
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::ChDown), '6',
                 "ch-", g_tv_action == static_cast<int>(IrTvAction::ChDown));

    drawIrPadBtn(x0, row3, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Input), '7', "in",
                 g_tv_action == static_cast<int>(IrTvAction::Input));
    drawIrPadBtn(x0 + btn_w + gap, row3, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Send), ' ', "send",
                 false);
}

static void drawIrMain() {
    if (!g_screen_ready) {
        beginAppScreen("Infrared");
        g_screen_ready = true;
    } else {
        clearAppContentArea();
    }

    const int content_y = drawIrTabs();
    if (g_category == IrCategory::TV) {
        drawTvRemotePad(content_y);
    } else {
        drawAcRemotePad(content_y);
    }

    const int hint_y = M5Cardputer.Display.height() - 12;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);
    int cx = APP_CONTENT_X;
    cx += drawArrowBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("brand ");
    cx += M5Cardputer.Display.textWidth("brand ");
    cx += drawKeyBadge(cx, hint_y, 't', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("tab");
    drawHelpHintRight("help");
}

static void redrawIr() {
    if (g_help_visible) {
        drawIrHelpPage();
    } else {
        drawIrMain();
    }
}

void enterIrApp() {
    g_screen_ready = false;
    g_help_visible = false;
    g_help_page = 0;
    g_tx_status = "";
    g_press_ac = IrAcBtn::None;
    g_press_tv = IrTvBtn::None;
    ensureIrReady();
    redrawIr();
}

void updateIrApp() {
    if (g_help_visible) {
        return;
    }
    bool need_redraw = false;
    if ((g_press_ac != IrAcBtn::None || g_press_tv != IrTvBtn::None) &&
        static_cast<int32_t>(millis() - g_press_until_ms) >= 0) {
        g_press_ac = IrAcBtn::None;
        g_press_tv = IrTvBtn::None;
        need_redraw = true;
    }
    if (g_tx_status[0] != '\0' && static_cast<int32_t>(millis() - g_tx_status_until_ms) >= 0) {
        g_tx_status = "";
        need_redraw = true;
    }
    if (need_redraw) {
        drawIrMain();
    }
}

void handleIrApp(const Keyboard_Class::KeysState& status) {
    if (!status.word.empty() || !status.hid_keys.empty() || status.enter || status.space ||
        status.del) {
        // continue
    } else {
        return;
    }

    for (const char c : status.word) {
        if (c == 'h' || c == 'H') {
            g_help_visible = !g_help_visible;
            if (g_help_visible) {
                g_help_page = 0;
            }
            g_screen_ready = false;
            redrawIr();
            return;
        }
    }

    if (g_help_visible) {
        const int hdelta = getHorizontalDelta(status);
        if (hdelta != 0) {
            g_help_page = (g_help_page + hdelta + IR_HELP_PAGE_COUNT) % IR_HELP_PAGE_COUNT;
            drawIrHelpPage();
        }
        return;
    }

    for (const char c : status.word) {
        if (c == 't' || c == 'T') {
            g_category = (g_category == IrCategory::TV) ? IrCategory::AC : IrCategory::TV;
            drawIrMain();
            return;
        }
    }

    // TV 快捷数字键：选中并立即发送
    if (g_category == IrCategory::TV) {
        for (const char c : status.word) {
            int action = -1;
            if (c == '1') {
                action = static_cast<int>(IrTvAction::Power);
            } else if (c == '2') {
                action = static_cast<int>(IrTvAction::VolUp);
            } else if (c == '3') {
                action = static_cast<int>(IrTvAction::VolDown);
            } else if (c == '4') {
                action = static_cast<int>(IrTvAction::Mute);
            } else if (c == '5') {
                action = static_cast<int>(IrTvAction::ChUp);
            } else if (c == '6') {
                action = static_cast<int>(IrTvAction::ChDown);
            } else if (c == '7') {
                action = static_cast<int>(IrTvAction::Input);
            }
            if (action >= 0) {
                g_tv_action = action;
                flashTvBtn(tvActionToBtn(action));
                sendCurrent();
                drawIrMain();
                return;
            }
        }
        if (status.enter || status.space) {
            flashTvBtn(IrTvBtn::Send);
            sendCurrent();
            drawIrMain();
            return;
        }
    }

    // AC 遥控快捷键
    if (g_category == IrCategory::AC) {
        for (const char c : status.word) {
            if (c == 'p' || c == 'P') {
                g_ac_field = static_cast<int>(IrAcField::Power);
                g_ac_power = !g_ac_power;
                flashAcBtn(IrAcBtn::Power);
                drawIrMain();
                return;
            }
            if (c == 'm' || c == 'M') {
                g_ac_field = static_cast<int>(IrAcField::Mode);
                cycleAcMode(1);
                flashAcBtn(IrAcBtn::Mode);
                drawIrMain();
                return;
            }
            if (c == 'f' || c == 'F') {
                g_ac_field = static_cast<int>(IrAcField::Fan);
                cycleAcFan(1);
                flashAcBtn(IrAcBtn::Fan);
                drawIrMain();
                return;
            }
            if (c == '-' || c == '_') {
                g_ac_field = static_cast<int>(IrAcField::Temp);
                adjustAcField(-1);
                flashAcBtn(IrAcBtn::TempDown);
                drawIrMain();
                return;
            }
            if (c == '=' || c == '+') {
                g_ac_field = static_cast<int>(IrAcField::Temp);
                adjustAcField(1);
                flashAcBtn(IrAcBtn::TempUp);
                drawIrMain();
                return;
            }
        }
        if (status.enter || status.space) {
            flashAcBtn(IrAcBtn::Send);
            sendCurrent();
            drawIrMain();
            return;
        }
    }

    const int hdelta = getHorizontalDelta(status);
    if (hdelta != 0) {
        if (g_category == IrCategory::TV) {
            const int n = static_cast<int>(IrTvBrand::Count);
            g_tv_brand = (g_tv_brand + hdelta + n) % n;
        } else {
            const int n = static_cast<int>(IrAcBrand::Count);
            g_ac_brand = (g_ac_brand + hdelta + n) % n;
        }
        drawIrMain();
        return;
    }

    const int vdelta = getVerticalDelta(status);
    if (vdelta != 0) {
        if (g_category == IrCategory::TV) {
            const int n = static_cast<int>(IrTvAction::Count);
            g_tv_action = (g_tv_action + vdelta + n) % n;
        } else {
            const int n = static_cast<int>(IrAcField::Count);
            g_ac_field = (g_ac_field + vdelta + n) % n;
        }
        drawIrMain();
        return;
    }

    if (status.enter || status.space) {
        sendCurrent();
        drawIrMain();
        return;
    }
}
