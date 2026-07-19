#include "app_ir.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
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

// Tab：切换品牌
static bool isIrTabKey(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x2B) {
            return true;
        }
    }
    for (const char c : status.word) {
        if (c == '\t') {
            return true;
        }
    }
    return false;
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

// Help 分栏标题（与其它 app 一致：蓝底黑字）
static int drawHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

static int drawHelpKeyAt(const int x, const int y, const char key, const char* text) {
    int cx = x + drawKeyBadge(x, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawHelpBadgeAt(const int x, const int y, const char* badge, const char* text) {
    int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y + 1);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawHelpTextAt(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
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
    const int screen_w = M5Cardputer.Display.width();
    constexpr int col_gap = 2;
    const int col_w = (screen_w - col_gap) / 2;
    const int keys_x = 0;
    const int notes_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    const int content_h = M5Cardputer.Display.height() - col_y;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y, content_h, DARKGREY);

    int y = drawHelpColHeader(keys_x, col_y, col_w, "keymap");
    const int kx = keys_x + 2;
    y = drawHelpBadgeAt(kx, y, "Tab", "brand");
    y = drawHelpKeyAt(kx, y, 't', "TV / AC");
    y = drawHelpKeyAt(kx, y, 'p', "power");
    y = drawHelpKeyAt(kx, y, '-', "vol / temp");
    y = drawHelpKeyAt(kx, y, '[', "TV ch");
    y = drawHelpBadgeAt(kx, y, "BtnA", "send");

    y = drawHelpColHeader(notes_x, col_y, screen_w - notes_x, "manual");
    const int nx = notes_x + 2;
    y = drawHelpTextAt(nx, y, "TX GPIO44");
    y = drawHelpTextAt(nx, y, "aim IR window");
    y = drawHelpTextAt(nx, y, "Xiaomi: Coolix");
    y = drawHelpTextAt(nx, y, "OEM? try Midea");
    y = drawHelpTextAt(nx, y, "TV m/i mute/in");
    y = drawHelpTextAt(nx, y, "AC m/f mode/fan");
    y = drawHelpTextAt(nx, y, "SPC/ENT send");

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

static void drawAcRemotePad(const int content_y) {
    const int x0 = APP_CONTENT_X;
    int y = content_y;

    // 第一排：品牌 / 电源状态（二倍字体）
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x0, y);
    const char* ac_brand = acBrandName(g_ac_brand);
    M5Cardputer.Display.print(ac_brand);
    int cx = x0 + M5Cardputer.Display.textWidth(ac_brand) + 8;
    const char* ac_pwr = g_ac_power ? "ON" : "OFF";
    M5Cardputer.Display.setTextColor(g_ac_power ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(ac_pwr);
    if (g_tx_status[0] != '\0' && static_cast<int32_t>(millis() - g_tx_status_until_ms) < 0) {
        cx += M5Cardputer.Display.textWidth(ac_pwr) + 8;
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(g_tx_status);
    }
    y += INFO_LINE_H_2X + 3; // 与下方内容间隔

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
    // 无上下导航选中，仅按键闪烁反馈
    drawIrPadBtn(x0, row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Power), 'p', "pwr", false);
    drawIrPadBtn(x0 + btn_w + gap, row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Mode), 'm', "mode",
                 false);
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Fan), 'f',
                 "fan", false);
    drawIrPadBtn(x0, row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::TempDown), '-', "temp", false);
    drawIrPadBtn(x0 + btn_w + gap, row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::TempUp), '=', "temp",
                 false);
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

    // 第一排：品牌 / 当前动作（二倍字体）
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x0, y);
    const char* tv_brand = tvBrandName(g_tv_brand);
    M5Cardputer.Display.print(tv_brand);
    int cx = x0 + M5Cardputer.Display.textWidth(tv_brand) + 8;
    const char* tv_action = tvActionName(g_tv_action);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(tv_action);
    if (g_tx_status[0] != '\0' && static_cast<int32_t>(millis() - g_tx_status_until_ms) < 0) {
        cx += M5Cardputer.Display.textWidth(tv_action) + 8;
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(cx, y);
        M5Cardputer.Display.print(g_tx_status);
    }
    y += INFO_LINE_H_2X + 5; // 与下方按键间隔

    constexpr int btn_w = 72;
    constexpr int btn_h = 16;
    constexpr int gap = 3;
    const int row1 = y;
    const int row2 = y + btn_h + gap;
    const int row3 = y + 2 * (btn_h + gap);

    // 无上下导航选中；音量/频道按 -/+ 左到右排列
    drawIrPadBtn(x0, row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Power), 'p', "pwr", false);
    drawIrPadBtn(x0 + btn_w + gap, row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::VolDown), '-',
                 "vol-", false);
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::VolUp), '=',
                 "vol+", false);

    drawIrPadBtn(x0, row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Mute), 'm', "mute", false);
    drawIrPadBtn(x0 + btn_w + gap, row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::ChDown), '[', "ch-",
                 false);
    drawIrPadBtn(x0 + 2 * (btn_w + gap), row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::ChUp), ']',
                 "ch+", false);

    drawIrPadBtn(x0, row3, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Input), 'i', "in", false);
    drawIrPadBtn(x0 + btn_w + gap, row3, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Send), ' ', "send",
                 false);
}

static void drawIrMain() {
    if (!g_screen_ready) {
        // Header：Infrared + TV/AC（次要色）
        beginAppScreenAccent("Infrared ", g_category == IrCategory::TV ? "TV" : "AC",
                             APP_COLOR_LABEL);
        g_screen_ready = true;
    } else {
        clearAppContentArea();
    }

    const int content_y = APP_CONTENT_Y;
    if (g_category == IrCategory::TV) {
        drawTvRemotePad(content_y);
    } else {
        drawAcRemotePad(content_y);
    }

    const int hint_y = M5Cardputer.Display.height() - 12;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, 236, 12, BLACK);
    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, hint_y, "Tab", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("brand ");
    cx += M5Cardputer.Display.textWidth("brand ");
    cx += drawKeyBadge(cx, hint_y, 't', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("mode");
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
    g_tx_status = "";
    g_press_ac = IrAcBtn::None;
    g_press_tv = IrTvBtn::None;

    // 按配置应用默认功能块与品牌
    const AppConfig& cfg = getAppConfig();
    g_category =
        cfg.infrared_default == IrDefaultCategory::Ac ? IrCategory::AC : IrCategory::TV;
    g_tv_brand = constrain(static_cast<int>(cfg.infrared_tv_brand), 0,
                           static_cast<int>(IrTvBrand::Count) - 1);
    g_ac_brand = constrain(static_cast<int>(cfg.infrared_ac_brand), 0,
                           static_cast<int>(IrAcBrand::Count) - 1);

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
            g_screen_ready = false;
            redrawIr();
            return;
        }
    }

    if (g_help_visible) {
        return;
    }

    // Tab：循环切换当前类别下的品牌
    if (isIrTabKey(status)) {
        if (g_category == IrCategory::TV) {
            const int n = static_cast<int>(IrTvBrand::Count);
            g_tv_brand = (g_tv_brand + 1) % n;
        } else {
            const int n = static_cast<int>(IrAcBrand::Count);
            g_ac_brand = (g_ac_brand + 1) % n;
        }
        drawIrMain();
        return;
    }

    for (const char c : status.word) {
        if (c == 't' || c == 'T') {
            g_category = (g_category == IrCategory::TV) ? IrCategory::AC : IrCategory::TV;
            g_screen_ready = false; // 刷新 header 中的 TV/AC
            drawIrMain();
            return;
        }
    }

    // TV：快捷键选中并立即发送
    if (g_category == IrCategory::TV) {
        for (const char c : status.word) {
            int action = -1;
            if (c == 'p' || c == 'P') {
                action = static_cast<int>(IrTvAction::Power);
            } else if (c == '=' || c == '+') {
                action = static_cast<int>(IrTvAction::VolUp);
            } else if (c == '-' || c == '_') {
                action = static_cast<int>(IrTvAction::VolDown);
            } else if (c == 'm' || c == 'M') {
                action = static_cast<int>(IrTvAction::Mute);
            } else if (c == ']') {
                action = static_cast<int>(IrTvAction::ChUp);
            } else if (c == '[') {
                action = static_cast<int>(IrTvAction::ChDown);
            } else if (c == 'i' || c == 'I') {
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

    // 电视/空调均无上下键导航，对应物理键直接操作
}

// BtnA：发送当前红外指令
void pollIrBtnA() {
    if (g_help_visible) {
        return;
    }
    if (!M5Cardputer.BtnA.wasPressed()) {
        return;
    }
    if (g_category == IrCategory::TV) {
        flashTvBtn(IrTvBtn::Send);
    } else {
        flashAcBtn(IrAcBtn::Send);
    }
    sendCurrent();
    drawIrMain();
}
