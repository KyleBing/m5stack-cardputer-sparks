#include "app_ir.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_device_icons.h"
#include "app_header.h"

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <LittleFS.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Cardputer / Adv 板载红外发射管
static constexpr uint16_t IR_TX_PIN = 44;
// data/icon/ir：优先 bake 的 .rgb565，缺失时回退 PNG
static constexpr const char* AC_ICON_DIR = "/icon/ir";
static constexpr int AC_MODE_ICON_PX = 30;
static constexpr int AC_MODE_ICON_PIXELS = AC_MODE_ICON_PX * AC_MODE_ICON_PX;
static constexpr int AC_MODE_ICON_BYTES = AC_MODE_ICON_PIXELS * 2; // RGB565
static constexpr int AC_MODE_ICON_GAP = 1; // 3 列时收紧，给右侧按键留缝
static constexpr int AC_MODE_TOP_COLS = 3; // 上排 Cool/Heat/Dry
static constexpr int AC_MODE_ICON_X = APP_CONTENT_X;
// 模式/按键相对顶栏（品牌行）下方间距
static constexpr int AC_MODE_ROW_GAP = 6;
// 右栏按键贴右边距
static constexpr int AC_PAD_RIGHT = 8;
// 左栏模式区：上 3 下 2（宽约 92px，右侧按键从 ~100 起）

// 风速图标（顶栏与温度并排，仅显示当前档）
static constexpr int AC_FAN_ICON_PX = 24;
static constexpr int AC_FAN_ICON_PIXELS = AC_FAN_ICON_PX * AC_FAN_ICON_PX;
static constexpr int AC_FAN_ICON_BYTES = AC_FAN_ICON_PIXELS * 2;
static constexpr int AC_FAN_COUNT = 6;

// 进入 IR 时 malloc；离开 free（不常驻 BSS）
static constexpr int AC_MODE_COUNT = 5;
static constexpr int AC_ICON_CACHE_SLOTS = AC_MODE_COUNT * 2; // 5 模式 × normal/active
static uint16_t* s_ac_icon_px = nullptr;
static bool s_ac_icon_ready[AC_ICON_CACHE_SLOTS] = {};
static uint16_t* s_ac_fan_icon_px = nullptr;
static bool s_ac_fan_icon_ready[AC_FAN_COUNT] = {};

// 模式槽像素起点
static uint16_t* acModeIconPx(const int slot) {
    return s_ac_icon_px + static_cast<size_t>(slot) * AC_MODE_ICON_PIXELS;
}

// 风速槽像素起点
static uint16_t* acFanIconPx(const int slot) {
    return s_ac_fan_icon_px + static_cast<size_t>(slot) * AC_FAN_ICON_PIXELS;
}

// 进入时分配缓存；失败则保持空指针（绘制走 PNG 回退）
static bool ensureAcIconCache() {
    if (s_ac_icon_px == nullptr) {
        s_ac_icon_px = static_cast<uint16_t*>(
            malloc(static_cast<size_t>(AC_ICON_CACHE_SLOTS) * AC_MODE_ICON_BYTES));
        memset(s_ac_icon_ready, 0, sizeof(s_ac_icon_ready));
    }
    if (s_ac_fan_icon_px == nullptr) {
        s_ac_fan_icon_px = static_cast<uint16_t*>(
            malloc(static_cast<size_t>(AC_FAN_COUNT) * AC_FAN_ICON_BYTES));
        memset(s_ac_fan_icon_ready, 0, sizeof(s_ac_fan_icon_ready));
    }
    return s_ac_icon_px != nullptr && s_ac_fan_icon_px != nullptr;
}

// 离开 IR 释放缓存
static void freeAcIconCache() {
    free(s_ac_icon_px);
    s_ac_icon_px = nullptr;
    free(s_ac_fan_icon_px);
    s_ac_fan_icon_px = nullptr;
    memset(s_ac_icon_ready, 0, sizeof(s_ac_icon_ready));
    memset(s_ac_fan_icon_ready, 0, sizeof(s_ac_fan_icon_ready));
}

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

// 有图标的五种模式
static const char* acModeIconStem(const stdAc::opmode_t mode) {
    switch (mode) {
        case stdAc::opmode_t::kCool:
            return "ac_cool";
        case stdAc::opmode_t::kHeat:
            return "ac_heat";
        case stdAc::opmode_t::kDry:
            return "ac_dry";
        case stdAc::opmode_t::kFan:
            return "ac_fan";
        case stdAc::opmode_t::kAuto:
            return "ac_auto";
        default:
            return nullptr;
    }
}

// cool/heat/dry/fan/auto → 0..4；active 占后 5 槽
static int acModeIconCacheSlot(const char* stem, const bool active) {
    static const char* kStems[] = {"ac_cool", "ac_heat", "ac_dry", "ac_fan", "ac_auto"};
    for (int i = 0; i < AC_MODE_COUNT; i++) {
        if (strcmp(stem, kStems[i]) == 0) {
            return i + (active ? AC_MODE_COUNT : 0);
        }
    }
    return -1;
}

// 风速档 → 缓存槽 / 文件 stem
static int acFanIconSlot(const stdAc::fanspeed_t fan) {
    switch (fan) {
        case stdAc::fanspeed_t::kAuto:
            return 0;
        case stdAc::fanspeed_t::kMin:
            return 1;
        case stdAc::fanspeed_t::kLow:
            return 2;
        case stdAc::fanspeed_t::kMedium:
            return 3;
        case stdAc::fanspeed_t::kHigh:
            return 4;
        case stdAc::fanspeed_t::kMax:
            return 5;
        default:
            return -1;
    }
}

static const char* acFanIconStem(const stdAc::fanspeed_t fan) {
    static const char* kStems[] = {
        "ac_fan_auto", "ac_fan_min", "ac_fan_low", "ac_fan_med", "ac_fan_high", "ac_fan_max",
    };
    const int slot = acFanIconSlot(fan);
    if (slot < 0 || slot >= AC_FAN_COUNT) {
        return nullptr;
    }
    return kStems[slot];
}

// 从 LittleFS 读入 bake 的 RGB565 到模式缓存槽
static bool loadAcRgb565ToSlot(const char* path, const int slot) {
    if (s_ac_icon_px == nullptr || slot < 0 || slot >= AC_ICON_CACHE_SLOTS || path == nullptr) {
        return false;
    }
    if (!LittleFS.exists(path)) {
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }
    const size_t n =
        f.read(reinterpret_cast<uint8_t*>(acModeIconPx(slot)), AC_MODE_ICON_BYTES);
    f.close();
    if (n != static_cast<size_t>(AC_MODE_ICON_BYTES)) {
        return false;
    }
    s_ac_icon_ready[slot] = true;
    return true;
}

// 从 LittleFS 读入风速 RGB565
static bool loadAcFanRgb565ToSlot(const char* path, const int slot) {
    if (s_ac_fan_icon_px == nullptr || slot < 0 || slot >= AC_FAN_COUNT || path == nullptr) {
        return false;
    }
    if (!LittleFS.exists(path)) {
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }
    const size_t n =
        f.read(reinterpret_cast<uint8_t*>(acFanIconPx(slot)), AC_FAN_ICON_BYTES);
    f.close();
    if (n != static_cast<size_t>(AC_FAN_ICON_BYTES)) {
        return false;
    }
    s_ac_fan_icon_ready[slot] = true;
    return true;
}

// 1:1 绘制模式图标；优先 RAM 缓存 → .rgb565 → PNG
static bool drawAcModeIconAt(const char* stem, const int x, const int y, const bool active) {
    if (stem == nullptr) {
        return false;
    }
    const int slot = acModeIconCacheSlot(stem, active);
    if (slot >= 0 && s_ac_icon_px != nullptr && s_ac_icon_ready[slot]) {
        M5Cardputer.Display.pushImage(x, y, AC_MODE_ICON_PX, AC_MODE_ICON_PX, acModeIconPx(slot));
        return true;
    }

    if (slot >= 0 && s_ac_icon_px != nullptr) {
        char path[56];
        if (active) {
            snprintf(path, sizeof(path), "%s/%s_active.rgb565", AC_ICON_DIR, stem);
        } else {
            snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, stem);
        }
        bool ok = loadAcRgb565ToSlot(path, slot);
        if (!ok && active) {
            snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, stem);
            ok = loadAcRgb565ToSlot(path, slot);
        }
        if (ok) {
            M5Cardputer.Display.pushImage(x, y, AC_MODE_ICON_PX, AC_MODE_ICON_PX,
                                          acModeIconPx(slot));
            return true;
        }
    }

    // 回退：drawLittleFsPng（内部仍优先 565，再 PNG）
    char png_path[48];
    if (active) {
        snprintf(png_path, sizeof(png_path), "%s/%s_active.png", AC_ICON_DIR, stem);
    } else {
        snprintf(png_path, sizeof(png_path), "%s/%s.png", AC_ICON_DIR, stem);
    }
    if (drawLittleFsPng(png_path, x, y, 1.0f)) {
        return true;
    }
    if (active) {
        snprintf(png_path, sizeof(png_path), "%s/%s.png", AC_ICON_DIR, stem);
        return drawLittleFsPng(png_path, x, y, 1.0f);
    }
    return false;
}

// 绘制当前风速图标（顶栏）
static bool drawAcFanIconAt(const int x, const int y) {
    const char* stem = acFanIconStem(g_ac_fan);
    const int slot = acFanIconSlot(g_ac_fan);
    if (stem == nullptr || slot < 0) {
        return false;
    }
    if (s_ac_fan_icon_px != nullptr && s_ac_fan_icon_ready[slot]) {
        M5Cardputer.Display.pushImage(x, y, AC_FAN_ICON_PX, AC_FAN_ICON_PX, acFanIconPx(slot));
        return true;
    }

    if (s_ac_fan_icon_px != nullptr) {
        char path[56];
        snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, stem);
        if (loadAcFanRgb565ToSlot(path, slot)) {
            M5Cardputer.Display.pushImage(x, y, AC_FAN_ICON_PX, AC_FAN_ICON_PX, acFanIconPx(slot));
            return true;
        }
    }

    char png_path[48];
    snprintf(png_path, sizeof(png_path), "%s/%s.png", AC_ICON_DIR, stem);
    return drawLittleFsPng(png_path, x, y, 1.0f);
}

// 进入 IR 时预读全部模式图标（normal + active），切模式时不再触 Flash
static void preloadAcModeIcons() {
    if (s_ac_icon_px == nullptr) {
        return;
    }
    static const char* kStems[] = {"ac_cool", "ac_heat", "ac_dry", "ac_fan", "ac_auto"};
    for (int i = 0; i < AC_MODE_COUNT; i++) {
        const char* stem = kStems[i];
        for (int active = 0; active < 2; active++) {
            const int slot = i + (active ? AC_MODE_COUNT : 0);
            if (s_ac_icon_ready[slot]) {
                continue;
            }
            char path[56];
            if (active) {
                snprintf(path, sizeof(path), "%s/%s_active.rgb565", AC_ICON_DIR, stem);
            } else {
                snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, stem);
            }
            if (!loadAcRgb565ToSlot(path, slot) && active) {
                // active 缺失时用普通态顶上
                snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, stem);
                loadAcRgb565ToSlot(path, slot);
            }
        }
    }
}

// 预读全部风速图标
static void preloadAcFanIcons() {
    if (s_ac_fan_icon_px == nullptr) {
        return;
    }
    static const char* kStems[] = {
        "ac_fan_auto", "ac_fan_min", "ac_fan_low", "ac_fan_med", "ac_fan_high", "ac_fan_max",
    };
    for (int i = 0; i < AC_FAN_COUNT; i++) {
        if (s_ac_fan_icon_ready[i]) {
            continue;
        }
        char path[56];
        snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, kStems[i]);
        loadAcFanRgb565ToSlot(path, i);
    }
}

// 左栏上 3 下 2 模式图标；当前模式用 _active
static void drawAcModeIcons(const int x, const int y) {
    static const stdAc::opmode_t kModes[] = {
        stdAc::opmode_t::kCool, stdAc::opmode_t::kHeat, stdAc::opmode_t::kDry,
        stdAc::opmode_t::kFan,  stdAc::opmode_t::kAuto,
    };
    for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); i++) {
        int col;
        int row;
        if (i < AC_MODE_TOP_COLS) {
            col = static_cast<int>(i);
            row = 0;
        } else {
            col = static_cast<int>(i - AC_MODE_TOP_COLS);
            row = 1;
        }
        const int ix = x + col * (AC_MODE_ICON_PX + AC_MODE_ICON_GAP);
        const int iy = y + row * (AC_MODE_ICON_PX + AC_MODE_ICON_GAP);
        const char* stem = acModeIconStem(kModes[i]);
        const bool active = (g_ac_mode == kModes[i]);
        if (!drawAcModeIconAt(stem, ix, iy, active)) {
            // 缺图时退回文字缩写
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(active ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(ix + 10, iy + 10);
            M5Cardputer.Display.print(acModeName(kModes[i])[0]);
        }
    }
}

static int acModeIconY() {
    return APP_CONTENT_INSET_Y + INFO_LINE_H_2X + AC_MODE_ROW_GAP;
}

// 模式图标下方：当前模式名
static int acModeNameY() {
    return acModeIconY() + 2 * AC_MODE_ICON_PX + AC_MODE_ICON_GAP + 2;
}

// 顶栏风速图标 X：紧挨温度左侧
static int acFanIconX(const int screen_w, const int temp_total_w) {
    return screen_w - APP_CONTENT_X - temp_total_w - 4 - AC_FAN_ICON_PX;
}

// 绘制当前模式名（模式图标下方小字）
static void drawAcModeNameLabel(const int x, const int y) {
    // 清掉旧名，避免 Cool→Heat 等长度变化残留
    M5Cardputer.Display.fillRect(x, y, 92, 10, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(acModeName(g_ac_mode));
}

// 仅刷新模式图标与模式名（切模式时不整页重绘）
static void redrawAcModeIconsOnly() {
    const int icon_y = acModeIconY();
    drawAcModeIcons(AC_MODE_ICON_X, icon_y);
    drawAcModeNameLabel(AC_MODE_ICON_X, acModeNameY());
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
    y = drawHelpBadgeAt(kx, y, "BtnGO", "send");

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

// 遥控器垫按钮：2x 键名在上、小字说明在下（AC / TV 共用）
static void drawIrStackPadBtn(const int x, const int y, const int w, const int h,
                              const bool pressed, const char key, const char* label) {
    const uint16_t fill = pressed ? APP_COLOR_MENU_KEY : BLACK;
    const uint16_t border = pressed ? APP_COLOR_MENU_KEY : APP_COLOR_MUTED;
    M5Cardputer.Display.fillRoundRect(x, y, w, h, 3, fill);
    M5Cardputer.Display.drawRoundRect(x, y, w, h, 3, border);

    constexpr int badge_size = 2;
    constexpr int pad_x = 2;
    // 高按钮多留白；矮按钮略收，避免裁切
    const int pad_y = (h >= 34) ? 2 : 1;
    const int stack_gap = (h >= 34) ? 2 : 1;
    const int badge_h = 8 * badge_size + pad_y * 2;
    constexpr int label_h = 8;
    const int stack_h = badge_h + stack_gap + label_h;
    // 徽章+说明在外框内垂直居中
    const int sy = y + (h - stack_h) / 2;

    M5Cardputer.Display.setTextSize(badge_size);
    int badge_w = 0;
    const char* badge_text = nullptr;
    char letter_buf[2] = {0, 0};
    if (key == ' ') {
        badge_text = "SP";
        badge_w = M5Cardputer.Display.textWidth(badge_text) + pad_x * 2;
    } else {
        letter_buf[0] = static_cast<char>(toupper(static_cast<unsigned char>(key)));
        badge_text = letter_buf;
        badge_w = M5Cardputer.Display.textWidth(badge_text) + pad_x * 2;
    }
    const int bx = x + (w - badge_w) / 2;
    // 自绘徽章，pad_y 比通用 drawKeyBadge 更大，字不贴顶
    M5Cardputer.Display.fillRoundRect(bx, sy, badge_w, badge_h, 2, APP_COLOR_MENU_KEY);
    M5Cardputer.Display.setTextColor(APP_COLOR_KEY_TEXT, APP_COLOR_MENU_KEY);
    M5Cardputer.Display.setCursor(bx + pad_x, sy + pad_y);
    M5Cardputer.Display.print(badge_text);

    M5Cardputer.Display.setTextSize(1);
    const int label_w = M5Cardputer.Display.textWidth(label);
    const int lx = x + (w - label_w) / 2;
    M5Cardputer.Display.setTextColor(pressed ? APP_COLOR_KEY_TEXT : APP_COLOR_HINT, fill);
    M5Cardputer.Display.setCursor(lx, sy + badge_h + stack_gap);
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
    const int screen_w = M5Cardputer.Display.width();
    const int y0 = content_y;

    // 顶栏以风速图标高度为基准，文字纵向居中对齐
    const int fan_y = y0 - 2;
    constexpr int kText2H = 16; // setTextSize(2) 字高
    const int text_y = fan_y + (AC_FAN_ICON_PX - kText2H) / 2;

    // 顶栏左：品牌 / 电源
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x0, text_y);
    const char* ac_brand = acBrandName(g_ac_brand);
    M5Cardputer.Display.print(ac_brand);
    int cx = x0 + M5Cardputer.Display.textWidth(ac_brand) + 8;
    const char* ac_pwr = g_ac_power ? "ON" : "OFF";
    M5Cardputer.Display.setTextColor(g_ac_power ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, text_y);
    M5Cardputer.Display.print(ac_pwr);
    if (g_tx_status[0] != '\0' && static_cast<int32_t>(millis() - g_tx_status_until_ms) < 0) {
        cx += M5Cardputer.Display.textWidth(ac_pwr) + 8;
        M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
        M5Cardputer.Display.setCursor(cx, text_y);
        M5Cardputer.Display.print(g_tx_status);
    }

    // 右上角：风速图标 + 温度（同纵向居中）
    char tbuf[8];
    snprintf(tbuf, sizeof(tbuf), "%u", static_cast<unsigned>(g_ac_temp));
    M5Cardputer.Display.setTextSize(2);
    const int temp_w = M5Cardputer.Display.textWidth(tbuf);
    M5Cardputer.Display.setTextSize(1);
    const int unit_w = M5Cardputer.Display.textWidth("C");
    const int temp_total_w = temp_w + 2 + unit_w;
    const int temp_x = screen_w - APP_CONTENT_X - temp_total_w;
    const int fan_x = acFanIconX(screen_w, temp_total_w);
    if (!drawAcFanIconAt(fan_x, fan_y)) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(fan_x, fan_y + 8);
        M5Cardputer.Display.print(acFanName(g_ac_fan)[0]);
    }
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(temp_x, text_y);
    M5Cardputer.Display.print(tbuf);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(temp_x + temp_w + 2, text_y + 4);
    M5Cardputer.Display.print("C");

    // 左：上 3 下 2 模式 + 模式名；右：按键垫贴右边 8px
    const int icon_y = acModeIconY();
    drawAcModeIcons(AC_MODE_ICON_X, icon_y);
    drawAcModeNameLabel(AC_MODE_ICON_X, acModeNameY());
    constexpr int gap = 3;
    constexpr int btn_w = 42;
    constexpr int btn_h = 36;
    constexpr int cols = 3;
    const int pad_w = cols * btn_w + (cols - 1) * gap;
    const int pad_x = screen_w - AC_PAD_RIGHT - pad_w;
    const int row1 = icon_y;
    const int row2 = icon_y + btn_h + gap;
    drawIrStackPadBtn(pad_x, row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Power), 'p', "pwr");
    drawIrStackPadBtn(pad_x + btn_w + gap, row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Mode), 'm',
                      "mode");
    drawIrStackPadBtn(pad_x + 2 * (btn_w + gap), row1, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Fan),
                      'f', "fan");
    drawIrStackPadBtn(pad_x, row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::TempDown), '-', "temp");
    drawIrStackPadBtn(pad_x + btn_w + gap, row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::TempUp),
                      '=', "temp");
    drawIrStackPadBtn(pad_x + 2 * (btn_w + gap), row2, btn_w, btn_h, isAcBtnPressed(IrAcBtn::Send),
                      ' ', "send");
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
    const int screen_w = M5Cardputer.Display.width();
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
    y += 16 + 6; // 2x 字高 + 与按键间距 6px

    // 4x2：音量成组后电源，频道/输入/发送
    constexpr int cols = 4;
    constexpr int gap = 3;
    constexpr int btn_h = 36; // 外框再高 2px，内容居中
    const int pad_w = screen_w - x0 - 8; // 右边留 8px
    const int btn_w = (pad_w - (cols - 1) * gap) / cols;
    const int row1 = y;
    const int row2 = y + btn_h + gap;

    // 第一排：-= 后接 P
    drawIrStackPadBtn(x0, row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::VolDown), '-', "vol-");
    drawIrStackPadBtn(x0 + btn_w + gap, row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::VolUp), '=',
                      "vol+");
    drawIrStackPadBtn(x0 + 2 * (btn_w + gap), row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Power),
                      'p', "pwr");
    drawIrStackPadBtn(x0 + 3 * (btn_w + gap), row1, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Mute),
                      'm', "mute");

    drawIrStackPadBtn(x0, row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::ChDown), '[', "ch-");
    drawIrStackPadBtn(x0 + btn_w + gap, row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::ChUp), ']',
                      "ch+");
    drawIrStackPadBtn(x0 + 2 * (btn_w + gap), row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Input),
                      'i', "in");
    drawIrStackPadBtn(x0 + 3 * (btn_w + gap), row2, btn_w, btn_h, isTvBtnPressed(IrTvBtn::Send),
                      ' ', "send");
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

    const int content_y = APP_CONTENT_INSET_Y;
    if (g_category == IrCategory::TV) {
        drawTvRemotePad(content_y);
    } else {
        drawAcRemotePad(content_y);
    }
    // 无底栏 tip：Tab/t 等说明见 h 帮助页
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
    // 按需分配缓存再预载；OOM 时绘制走 PNG/逐次读
    (void)ensureAcIconCache();
    preloadAcModeIcons();
    preloadAcFanIcons();
    redrawIr();
}

void leaveIrApp() {
    freeAcIconCache();
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
                // 只刷新模式图标，避免整页闪烁
                redrawAcModeIconsOnly();
                return;
            }
            if (c == 'f' || c == 'F') {
                g_ac_field = static_cast<int>(IrAcField::Fan);
                cycleAcFan(1);
                flashAcBtn(IrAcBtn::Fan);
                // 顶栏风速图标就地切换，按钮高亮仍整页刷一次
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
