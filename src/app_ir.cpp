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

// 风速图标（含左侧绿色档位条）：资源 34×30，非正方形，进 RAM 缓存
static constexpr int AC_FAN_ICON_W = 34;
static constexpr int AC_FAN_ICON_H = 30;
static constexpr int AC_FAN_ICON_PIXELS = AC_FAN_ICON_W * AC_FAN_ICON_H;
static constexpr int AC_FAN_ICON_BYTES = AC_FAN_ICON_PIXELS * 2;
static constexpr int AC_FAN_COUNT = 6;

// 红外信号图标：发射中红色 / 空闲灰色（非正方形，进入时解码进 RAM）
static constexpr int IR_SIG_ICON_W = 57;
static constexpr int IR_SIG_ICON_H = 38;
static constexpr int IR_SIG_ICON_BYTES = IR_SIG_ICON_W * IR_SIG_ICON_H * 2;
static constexpr uint32_t IR_SIG_TX_MS = 500; // 一次发送后红色图标保持时长

// 品牌 logo（data/icon/brand/*.png 统一 66x20）
static constexpr const char* IR_BRAND_ICON_DIR = "/icon/brand";
static constexpr int IR_BRAND_ICON_W = 66;
static constexpr int IR_BRAND_ICON_H = 20;

// ===== 全屏遥控页共用几何（无 header，对齐 240×135 设计稿）=====
static constexpr int IR_PAGE_SIG_X = 20;
static constexpr int IR_PAGE_SIG_Y = 12;
// 右侧 3 列按键垫（设计稿量得 38×33、间距 8、左缘 x=102）
static constexpr int IR_PAGE_BTN_W = 38;
static constexpr int IR_PAGE_BTN_H = 33;
static constexpr int IR_PAGE_BTN_GAP = 8;
static constexpr int IR_PAGE_BTN_COLS = 3;
static constexpr int IR_PAGE_PAD_X = 102;
// 键位字母用普通 2x 字体（非点阵），可见高度 7 行 × 2
static constexpr int IR_PAD_GLYPH_SIZE = 2;
static constexpr int IR_PAD_GLYPH_H = 7 * IR_PAD_GLYPH_SIZE;
// 按键底色：比设计稿 #2e2e2e 再压暗一档，描边保持略亮
static constexpr uint16_t IR_PAGE_BTN_FILL = 0x18E3;   // #1e1e1e
static constexpr uint16_t IR_PAGE_BTN_BORDER = 0x4A69; // #4a4a4a
static constexpr uint16_t IR_PAGE_BTN_ACTIVE = 0x3CDF; // #3cd3fe

// AC：两排按键；模式/风速图标居中于第 1/2 列
static constexpr int AC_PAGE_ROW1_Y = 53;
static constexpr int AC_PAGE_ROW2_Y = AC_PAGE_ROW1_Y + IR_PAGE_BTN_H + IR_PAGE_BTN_GAP;
static constexpr int AC_PAGE_ICON_CY = 25;
// 温度大字 / 品牌 logo 独立按左半边排版，不与右侧按键对齐
static constexpr int AC_PAGE_TEMP_X = 22;
static constexpr int AC_PAGE_TEMP_Y = 58;
static constexpr int AC_PAGE_TEMP_SIZE = 5;
static constexpr int AC_PAGE_TEMP_CLEAR_W = 76;
static constexpr int AC_PAGE_BRAND_X = 15;
static constexpr int AC_PAGE_BRAND_Y = 103;

// TV：三排按键（顶排缺左上角）；品牌与动作名在左侧
static constexpr int TV_PAGE_ROW1_Y = 12;
static constexpr int TV_PAGE_ROW2_Y = TV_PAGE_ROW1_Y + IR_PAGE_BTN_H + IR_PAGE_BTN_GAP;
static constexpr int TV_PAGE_ROW3_Y = TV_PAGE_ROW2_Y + IR_PAGE_BTN_H + IR_PAGE_BTN_GAP;
static constexpr int TV_PAGE_BRAND_X = 15;
static constexpr int TV_PAGE_BRAND_Y = 62;
// 动作大字与品牌 logo 同一个盒子（x/宽度一致），文本在盒内居中
static constexpr int TV_PAGE_ACTION_X = TV_PAGE_BRAND_X;
static constexpr int TV_PAGE_ACTION_Y = 97;
static constexpr int TV_PAGE_ACTION_SIZE = 3;
static constexpr int TV_PAGE_ACTION_CLEAR_W = IR_BRAND_ICON_W;
static constexpr int TV_PAGE_ACTION_CLEAR_H = 24;
static constexpr int TV_PAGE_ACTION_MAX_CHARS = 3;

// 进入 IR 时 malloc；离开 free（不常驻 BSS）
static constexpr int AC_MODE_COUNT = 5;
static constexpr int AC_ICON_CACHE_SLOTS = AC_MODE_COUNT * 2; // 5 模式 × normal/active
static uint16_t* s_ac_icon_px = nullptr;
static bool s_ac_icon_ready[AC_ICON_CACHE_SLOTS] = {};
static uint16_t* s_ac_fan_icon_px = nullptr;
static bool s_ac_fan_icon_ready[AC_FAN_COUNT] = {};
// 信号图标非正方形，公共 rgb565 加载器不收；进入时自行解 PNG 存 RAM
// 0 = 空闲（灰） / 1 = 发射中（红）
static uint16_t* s_ac_sig_icon_px[2] = {nullptr, nullptr};

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
    for (uint16_t*& px : s_ac_sig_icon_px) {
        free(px);
        px = nullptr;
    }
    memset(s_ac_icon_ready, 0, sizeof(s_ac_icon_ready));
    memset(s_ac_fan_icon_ready, 0, sizeof(s_ac_fan_icon_ready));
}

// 进入时把两张信号图标读进 RAM：优先 bake 的 .rgb565，缺失回退现场解 PNG
static void preloadAcSignalIcons() {
    static const char* kStems[2] = {
        "/icon/ir/send_inactive",
        "/icon/ir/send_active",
    };
    for (int i = 0; i < 2; i++) {
        if (s_ac_sig_icon_px[i] != nullptr) {
            continue;
        }
        auto* px = static_cast<uint16_t*>(malloc(IR_SIG_ICON_BYTES));
        if (px == nullptr) {
            continue;
        }
        char path[56];
        snprintf(path, sizeof(path), "%s.rgb565", kStems[i]);
        if (!loadRgb565File(path, px, IR_SIG_ICON_W, IR_SIG_ICON_H)) {
            snprintf(path, sizeof(path), "%s.png", kStems[i]);
            if (!decodePngToRgb565(path, px, IR_SIG_ICON_W, IR_SIG_ICON_H)) {
                free(px);
                continue;
            }
        }
        s_ac_sig_icon_px[i] = px;
    }
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
// 品牌/类别先完成重绘，再合并连续切换并延后写 LittleFS
static constexpr uint32_t IR_CONFIG_SAVE_DELAY_MS = 250;
static bool g_config_dirty = false;
static uint32_t g_config_save_due_ms = 0;

static void markIrConfigDirty() {
    g_config_dirty = true;
    g_config_save_due_ms = millis() + IR_CONFIG_SAVE_DELAY_MS;
}

static void flushIrConfigSave(const bool force = false) {
    if (!g_config_dirty ||
        (!force && static_cast<int32_t>(millis() - g_config_save_due_ms) < 0)) {
        return;
    }

    const IrDefaultCategory category =
        g_category == IrCategory::AC ? IrDefaultCategory::Ac : IrDefaultCategory::Tv;
    const uint8_t tv_brand = static_cast<uint8_t>(g_tv_brand);
    const uint8_t ac_brand = static_cast<uint8_t>(g_ac_brand);
    const AppConfig& cfg = getAppConfig();
    g_config_dirty = false;
    if (cfg.infrared_default == category && cfg.infrared_tv_brand == tv_brand &&
        cfg.infrared_ac_brand == ac_brand) {
        return;
    }
    saveAppConfigInfrared(category, tv_brand, ac_brand);
}

static const char* g_tx_status = "";
static uint32_t g_tx_status_until_ms = 0;
static bool g_screen_ready = false;
// AC 页顶部信号图标：发射后短时间保持红色；g_sig_icon_active 记录屏上现态
static uint32_t g_tx_active_until_ms = 0;
static bool g_sig_icon_active = false;

static bool isIrTxActive() {
    return static_cast<int32_t>(millis() - g_tx_active_until_ms) < 0;
}

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

// 品牌 logo 文件名（data/icon/brand）
static const char* acBrandIconStem(const int idx) {
    static const char* stems[] = {"midea", "gree", "haier", "aux", "hisense", "xiaomi"};
    if (idx < 0 || idx >= static_cast<int>(IrAcBrand::Count)) {
        return nullptr;
    }
    return stems[idx];
}

// TV 品牌 logo（NEC 无图）
static const char* tvBrandIconStem(const int idx) {
    static const char* stems[] = {"samsung", "sony", "lg", "panasonic", nullptr};
    if (idx < 0 || idx >= static_cast<int>(IrTvBrand::Count)) {
        return nullptr;
    }
    return stems[idx];
}

static const char* tvActionName(const int idx) {
    // 与设计稿左下大字一致的短名
    static const char* names[] = {"Power", "V+", "V-", "Mute", "CH+", "CH-", "Input"};
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
    if (!loadRgb565File(path, acModeIconPx(slot), AC_MODE_ICON_PX, AC_MODE_ICON_PX)) {
        return false;
    }
    s_ac_icon_ready[slot] = true;
    return true;
}

// 风速图标：优先 bake 的 .rgb565，缺失回退现场解 PNG
static bool loadAcFanPngToSlot(const int slot) {
    if (s_ac_fan_icon_px == nullptr || slot < 0 || slot >= AC_FAN_COUNT) {
        return false;
    }
    const char* stem = nullptr;
    static const char* kStems[] = {
        "ac_fan_auto", "ac_fan_min", "ac_fan_low", "ac_fan_med", "ac_fan_high", "ac_fan_max",
    };
    if (slot < AC_FAN_COUNT) {
        stem = kStems[slot];
    }
    if (stem == nullptr) {
        return false;
    }
    char path[56];
    snprintf(path, sizeof(path), "%s/%s.rgb565", AC_ICON_DIR, stem);
    if (!loadRgb565File(path, acFanIconPx(slot), AC_FAN_ICON_W, AC_FAN_ICON_H)) {
        snprintf(path, sizeof(path), "%s/%s.png", AC_ICON_DIR, stem);
        if (!decodePngToRgb565(path, acFanIconPx(slot), AC_FAN_ICON_W, AC_FAN_ICON_H)) {
            return false;
        }
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

// 绘制当前风速图标（1:1，34×30）
static bool drawAcFanIconAt(const int x, const int y) {
    const char* stem = acFanIconStem(g_ac_fan);
    const int slot = acFanIconSlot(g_ac_fan);
    if (stem == nullptr || slot < 0) {
        return false;
    }
    if (s_ac_fan_icon_px != nullptr && s_ac_fan_icon_ready[slot]) {
        M5Cardputer.Display.pushImage(x, y, AC_FAN_ICON_W, AC_FAN_ICON_H, acFanIconPx(slot));
        return true;
    }
    if (s_ac_fan_icon_px != nullptr && loadAcFanPngToSlot(slot)) {
        M5Cardputer.Display.pushImage(x, y, AC_FAN_ICON_W, AC_FAN_ICON_H, acFanIconPx(slot));
        return true;
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

// 预读全部风速图标（PNG → RAM）
static void preloadAcFanIcons() {
    if (s_ac_fan_icon_px == nullptr) {
        return;
    }
    for (int i = 0; i < AC_FAN_COUNT; i++) {
        if (!s_ac_fan_icon_ready[i]) {
            loadAcFanPngToSlot(i);
        }
    }
}

// 按键垫左边界
static int irPadX() {
    return IR_PAGE_PAD_X;
}

// 第 col 列按键左边界（0..2）
static int irPadColX(const int col) {
    return irPadX() + col * (IR_PAGE_BTN_W + IR_PAGE_BTN_GAP);
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

// Help 单栏行：按键徽章 + 功能名（白）+ 说明（灰）
static constexpr int IR_HELP_BADGE_X = 5;
static constexpr int IR_HELP_NAME_X = 40;
static constexpr int IR_HELP_DESC_X = 82;
static constexpr int IR_HELP_ROW_H = 11;

// badge_kind：0 按键字母 / 1 文字徽章 / 2 上下箭头徽章
enum class IrHelpBadge : uint8_t { Key = 0, Text, ArrowUpDown };

static int drawIrHelpRow(const int y, const IrHelpBadge kind, const char key, const char* badge,
                         const char* name, const char* desc) {
    switch (kind) {
        case IrHelpBadge::Text:
            drawTextBadge(IR_HELP_BADGE_X, y, badge, 1);
            break;
        case IrHelpBadge::ArrowUpDown:
            drawArrowUpDownBadge(IR_HELP_BADGE_X, y, 1);
            break;
        default:
            drawKeyBadge(IR_HELP_BADGE_X, y, key, 1);
            break;
    }
    // 徽章绘制会改 setTextColor，说明文字前必须恢复
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_TEXT, BLACK);
    M5Cardputer.Display.setCursor(IR_HELP_NAME_X, y + 1);
    M5Cardputer.Display.print(name);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(IR_HELP_DESC_X, y + 1);
    M5Cardputer.Display.print(desc);
    return y + IR_HELP_ROW_H;
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

// Help：单栏按键表，内容跟随当前 TV / AC 功能块
static void drawIrHelpPage() {
    const bool is_ac = g_category == IrCategory::AC;
    beginAppScreenAccent("Help ", is_ac ? "AC" : "TV", APP_COLOR_LABEL);

    int y = APP_CONTENT_Y_NO_TAP_TO_HEADER + 3;
    if (is_ac) {
        y = drawIrHelpRow(y, IrHelpBadge::Key, 'p', nullptr, "Power", "on / off");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 'm', nullptr, "Mode", "cool heat dry fan auto");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 'f', nullptr, "Fan", "auto min low med hi max");
        y = drawIrHelpRow(y, IrHelpBadge::ArrowUpDown, 0, nullptr, "Temp", "16 - 30 C  (also - =)");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 's', nullptr, "Send", "or SPC / ENT / BtnGO");
        y = drawIrHelpRow(y, IrHelpBadge::Text, 0, "Tab", "Brand", "next AC brand");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 't', nullptr, "TV", "switch to TV remote");
    } else {
        y = drawIrHelpRow(y, IrHelpBadge::Key, 'p', nullptr, "Power", "sends immediately");
        y = drawIrHelpRow(y, IrHelpBadge::Text, 0, "- =", "Volume", "down / up");
        y = drawIrHelpRow(y, IrHelpBadge::Text, 0, "[ ]", "Chan", "CH+ / CH-");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 'm', nullptr, "Mute", "toggle mute");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 'i', nullptr, "Input", "input source");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 's', nullptr, "Send", "or SPC / ENT / BtnGO");
        y = drawIrHelpRow(y, IrHelpBadge::Text, 0, "Tab", "Brand", "next TV brand");
        y = drawIrHelpRow(y, IrHelpBadge::Key, 't', nullptr, "AC", "switch to AC remote");
    }
    drawIrHelpRow(y, IrHelpBadge::Text, 0, btnGoHintLabel(), "Back", "IR TX on GPIO44");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

static bool isAcBtnPressed(const IrAcBtn btn) {
    return g_press_ac == btn && static_cast<int32_t>(millis() - g_press_until_ms) < 0;
}

static bool isTvBtnPressed(const IrTvBtn btn) {
    return g_press_tv == btn && static_cast<int32_t>(millis() - g_press_until_ms) < 0;
}

// ===== 全屏遥控页共用绘制 =====

enum class IrPadGlyph : uint8_t { Letter = 0, ArrowUp, ArrowDown };

// 点阵风格文字：先 1x 渲染到离屏 sprite，再按 scale 画带 1px 缝隙的方块
static void drawIrDotText(const char* text, const int x, const int y, const int scale,
                          const uint16_t color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    M5Cardputer.Display.setTextSize(1);
    const int w = M5Cardputer.Display.textWidth(text);
    constexpr int h = 8;
    M5Canvas spr(&M5Cardputer.Display);
    spr.setColorDepth(16);
    if (scale < 2 || w <= 0 || !spr.createSprite(w, h)) {
        M5Cardputer.Display.setTextSize(scale);
        M5Cardputer.Display.setTextColor(color, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(text);
        return;
    }
    spr.setFont(&fonts::Font0);
    spr.setTextSize(1);
    spr.fillSprite(BLACK);
    spr.setTextColor(WHITE, BLACK);
    spr.setCursor(0, 0);
    spr.print(text);

    const int block = scale - 1; // 留 1px 缝隙
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            if (spr.readPixel(px, py) != 0) {
                M5Cardputer.Display.fillRect(x + px * scale, y + py * scale, block, block, color);
            }
        }
    }
    spr.deleteSprite();
}

// 单个按键：深灰圆角底 + 点阵键名/三角 + 小字说明；按下时青底
static void drawIrPadBtnAt(const int x, const int y, const bool pressed, const IrPadGlyph glyph,
                           const char letter, const char* label) {
    const int w = IR_PAGE_BTN_W;
    const int h = IR_PAGE_BTN_H;
    const uint16_t fill = pressed ? IR_PAGE_BTN_ACTIVE : IR_PAGE_BTN_FILL;
    const uint16_t border = pressed ? IR_PAGE_BTN_ACTIVE : IR_PAGE_BTN_BORDER;
    const uint16_t fg = pressed ? APP_COLOR_KEY_TEXT : APP_COLOR_TEXT;

    M5Cardputer.Display.fillRoundRect(x, y, w, h, 4, fill);
    M5Cardputer.Display.drawRoundRect(x, y, w, h, 4, border);

    // 说明贴底留 3px，键名在其上方剩余空间居中
    const int label_y = h - 3 - 7;
    const int glyph_y = (label_y - IR_PAD_GLYPH_H) / 2;
    if (glyph == IrPadGlyph::Letter) {
        const char str[2] = {letter, '\0'};
        M5Cardputer.Display.setTextSize(IR_PAD_GLYPH_SIZE);
        M5Cardputer.Display.setTextColor(fg, fill);
        const int gw = M5Cardputer.Display.textWidth(str);
        M5Cardputer.Display.setCursor(x + (w - gw) / 2, y + glyph_y);
        M5Cardputer.Display.print(str);
    } else {
        const uint16_t arrow = pressed ? APP_COLOR_KEY_TEXT : APP_COLOR_HINT;
        const int cx = x + w / 2;
        const int cy = y + glyph_y + IR_PAD_GLYPH_H / 2; // 与字母键名同一竖直中线
        if (glyph == IrPadGlyph::ArrowUp) {
            M5Cardputer.Display.fillTriangle(cx - 6, cy + 5, cx + 6, cy + 5, cx, cy - 5, arrow);
        } else {
            M5Cardputer.Display.fillTriangle(cx - 6, cy - 5, cx + 6, cy - 5, cx, cy + 5, arrow);
        }
    }

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(fg, fill);
    const int label_w = M5Cardputer.Display.textWidth(label);
    M5Cardputer.Display.setCursor(x + (w - label_w) / 2, y + label_y);
    M5Cardputer.Display.print(label);
}

// 顶部红外信号图标：发射中红色，空闲灰色
static void drawIrSignalIcon() {
    const int slot = isIrTxActive() ? 1 : 0;
    g_sig_icon_active = slot != 0;
    if (s_ac_sig_icon_px[slot] != nullptr) {
        M5Cardputer.Display.pushImage(IR_PAGE_SIG_X, IR_PAGE_SIG_Y, IR_SIG_ICON_W, IR_SIG_ICON_H,
                                      s_ac_sig_icon_px[slot]);
        return;
    }
    M5Cardputer.Display.fillRect(IR_PAGE_SIG_X, IR_PAGE_SIG_Y, IR_SIG_ICON_W, IR_SIG_ICON_H, BLACK);
    const uint16_t color = slot != 0 ? APP_COLOR_ERROR : APP_COLOR_MUTED;
    const int cx = IR_PAGE_SIG_X + IR_SIG_ICON_W / 2;
    const int cy = IR_PAGE_SIG_Y + IR_SIG_ICON_H - 6;
    for (int r = 10; r <= 26; r += 8) {
        M5Cardputer.Display.drawArc(cx, cy, r, r + 1, 210, 330, color);
    }
}

// 品牌 logo（AC / TV 共用）；缺图回退文字
static void drawIrBrandLogo(const int x, const int y, const char* stem, const char* fallback) {
    M5Cardputer.Display.fillRect(x, y, IR_BRAND_ICON_W, IR_BRAND_ICON_H, BLACK);
    if (stem != nullptr) {
        char path[56];
        snprintf(path, sizeof(path), "%s/%s.png", IR_BRAND_ICON_DIR, stem);
        if (drawLittleFsPng(path, x, y, 1.0f)) {
            return;
        }
        // Gree 旧资源名 gelee
        if (strcmp(stem, "gree") == 0) {
            snprintf(path, sizeof(path), "%s/gelee.png", IR_BRAND_ICON_DIR);
            if (drawLittleFsPng(path, x, y, 1.0f)) {
                return;
            }
        }
    }
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_TEXT, BLACK);
    M5Cardputer.Display.setCursor(x, y + 2);
    M5Cardputer.Display.print(fallback != nullptr ? fallback : "?");
}

// ===== AC 全屏遥控页 =====

struct AcPadSpec {
    IrAcBtn id;
    IrPadGlyph glyph;
    char letter;
    const char* label;
};
static constexpr int AC_PAD_BTN_COUNT = 6;
static const AcPadSpec kAcPad[AC_PAD_BTN_COUNT] = {
    {IrAcBtn::Power, IrPadGlyph::Letter, 'P', "Power"},
    {IrAcBtn::Fan, IrPadGlyph::Letter, 'F', "Fan"},
    {IrAcBtn::TempUp, IrPadGlyph::ArrowUp, '\0', "Temp"},
    {IrAcBtn::Send, IrPadGlyph::Letter, 'S', "Send"},
    {IrAcBtn::Mode, IrPadGlyph::Letter, 'M', "Mode"},
    {IrAcBtn::TempDown, IrPadGlyph::ArrowDown, '\0', "Temp"},
};

static int acPadIndexOf(const IrAcBtn btn) {
    for (int i = 0; i < AC_PAD_BTN_COUNT; i++) {
        if (kAcPad[i].id == btn) {
            return i;
        }
    }
    return -1;
}

static void drawAcPadBtn(const int idx) {
    if (idx < 0 || idx >= AC_PAD_BTN_COUNT) {
        return;
    }
    const AcPadSpec& spec = kAcPad[idx];
    const int x = irPadColX(idx % IR_PAGE_BTN_COLS);
    const int y = (idx < IR_PAGE_BTN_COLS) ? AC_PAGE_ROW1_Y : AC_PAGE_ROW2_Y;
    drawIrPadBtnAt(x, y, isAcBtnPressed(spec.id), spec.glyph, spec.letter, spec.label);
}

static void drawAcModeIcon() {
    const int x = irPadColX(0) + (IR_PAGE_BTN_W - AC_MODE_ICON_PX) / 2;
    const int y = AC_PAGE_ICON_CY - AC_MODE_ICON_PX / 2;
    M5Cardputer.Display.fillRect(x, y, AC_MODE_ICON_PX, AC_MODE_ICON_PX, BLACK);
    if (drawAcModeIconAt(acModeIconStem(g_ac_mode), x, y, g_ac_power)) {
        return;
    }
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(g_ac_power ? APP_COLOR_LABEL : APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(x + 9, y + 7);
    M5Cardputer.Display.print(acModeName(g_ac_mode)[0]);
}

static void drawAcFanIcon() {
    const int x = irPadColX(1) + (IR_PAGE_BTN_W - AC_FAN_ICON_W) / 2;
    const int y = AC_PAGE_ICON_CY - AC_FAN_ICON_H / 2;
    M5Cardputer.Display.fillRect(x, y, AC_FAN_ICON_W, AC_FAN_ICON_H, BLACK);
    if (drawAcFanIconAt(x, y)) {
        return;
    }
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x + 10, y + 11);
    M5Cardputer.Display.print(acFanName(g_ac_fan)[0]);
}

static void drawAcTemp() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_ac_temp));
    const int glyph_h = 7 * AC_PAGE_TEMP_SIZE;
    M5Cardputer.Display.fillRect(AC_PAGE_TEMP_X, AC_PAGE_TEMP_Y, AC_PAGE_TEMP_CLEAR_W, glyph_h,
                                 BLACK);

    const uint16_t color = g_ac_power ? APP_COLOR_VALUE : APP_COLOR_MUTED;
    drawIrDotText(buf, AC_PAGE_TEMP_X, AC_PAGE_TEMP_Y, AC_PAGE_TEMP_SIZE, color);

    M5Cardputer.Display.setTextSize(1);
    const int unit_x = AC_PAGE_TEMP_X + M5Cardputer.Display.textWidth(buf) * AC_PAGE_TEMP_SIZE - 3;
    const int unit_y = AC_PAGE_TEMP_Y + glyph_h - 8;
    M5Cardputer.Display.drawCircle(unit_x + 2, unit_y + 1, 2, color);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(unit_x + 6, unit_y);
    M5Cardputer.Display.print("C");
}

static void drawAcBrandLogo() {
    drawIrBrandLogo(AC_PAGE_BRAND_X, AC_PAGE_BRAND_Y, acBrandIconStem(g_ac_brand),
                    acBrandName(g_ac_brand));
}

static void drawAcPage() {
    M5Cardputer.Display.fillScreen(BLACK);
    drawIrSignalIcon();
    drawAcModeIcon();
    drawAcFanIcon();
    drawAcTemp();
    drawAcBrandLogo();
    for (int i = 0; i < AC_PAD_BTN_COUNT; i++) {
        drawAcPadBtn(i);
    }
}

// ===== TV 全屏遥控页（对齐设计稿：顶排缺左上角）=====

struct TvPadSpec {
    IrTvBtn id;
    int col; // 0..2
    int row; // 0..2
    IrPadGlyph glyph;
    char letter;
    const char* label;
};
static constexpr int TV_PAD_BTN_COUNT = 8;
static const TvPadSpec kTvPad[TV_PAD_BTN_COUNT] = {
    {IrTvBtn::Power, 1, 0, IrPadGlyph::Letter, 'P', "Power"},
    {IrTvBtn::Mute, 2, 0, IrPadGlyph::Letter, 'M', "Mute"},
    {IrTvBtn::VolUp, 0, 1, IrPadGlyph::ArrowUp, '\0', "V+"},
    {IrTvBtn::VolDown, 1, 1, IrPadGlyph::ArrowDown, '\0', "V-"},
    {IrTvBtn::Input, 2, 1, IrPadGlyph::Letter, 'i', "Input"},
    {IrTvBtn::ChUp, 0, 2, IrPadGlyph::Letter, '[', "CH+"},
    {IrTvBtn::ChDown, 1, 2, IrPadGlyph::Letter, ']', "CH-"},
    {IrTvBtn::Send, 2, 2, IrPadGlyph::Letter, 'S', "Send"},
};

static int tvPadIndexOf(const IrTvBtn btn) {
    for (int i = 0; i < TV_PAD_BTN_COUNT; i++) {
        if (kTvPad[i].id == btn) {
            return i;
        }
    }
    return -1;
}

static int tvPadRowY(const int row) {
    if (row <= 0) {
        return TV_PAGE_ROW1_Y;
    }
    if (row == 1) {
        return TV_PAGE_ROW2_Y;
    }
    return TV_PAGE_ROW3_Y;
}

static void drawTvPadBtn(const int idx) {
    if (idx < 0 || idx >= TV_PAD_BTN_COUNT) {
        return;
    }
    const TvPadSpec& spec = kTvPad[idx];
    drawIrPadBtnAt(irPadColX(spec.col), tvPadRowY(spec.row), isTvBtnPressed(spec.id), spec.glyph,
                   spec.letter, spec.label);
}

static void drawTvBrandLogo() {
    drawIrBrandLogo(TV_PAGE_BRAND_X, TV_PAGE_BRAND_Y, tvBrandIconStem(g_tv_brand),
                    tvBrandName(g_tv_brand));
}

// 左下当前动作大字（点阵）：截到 3 字符，避免长名压到右侧按键
static void drawTvActionText() {
    char buf[TV_PAGE_ACTION_MAX_CHARS + 1];
    snprintf(buf, sizeof(buf), "%s", tvActionName(g_tv_action));
    M5Cardputer.Display.fillRect(TV_PAGE_ACTION_X, TV_PAGE_ACTION_Y, TV_PAGE_ACTION_CLEAR_W,
                                 TV_PAGE_ACTION_CLEAR_H, BLACK);
    M5Cardputer.Display.setTextSize(1);
    // 点阵每个字形末尾带 1 空列，居中时扣掉
    const int text_w = (M5Cardputer.Display.textWidth(buf) - 1) * TV_PAGE_ACTION_SIZE;
    const int x = TV_PAGE_ACTION_X + (TV_PAGE_ACTION_CLEAR_W - text_w) / 2;
    drawIrDotText(buf, x, TV_PAGE_ACTION_Y, TV_PAGE_ACTION_SIZE, APP_COLOR_VALUE);
}

static void drawTvPage() {
    M5Cardputer.Display.fillScreen(BLACK);
    drawIrSignalIcon();
    drawTvBrandLogo();
    drawTvActionText();
    for (int i = 0; i < TV_PAD_BTN_COUNT; i++) {
        drawTvPadBtn(i);
    }
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

static void drawIrMain() {
    // AC / TV 都是无 header 的全屏遥控页
    if (g_category == IrCategory::AC) {
        drawAcPage();
    } else {
        drawTvPage();
    }
    g_screen_ready = true;
}

// AC 页按下反馈：只重绘该按键
static void pressAcBtn(const IrAcBtn btn) {
    flashAcBtn(btn);
    drawAcPadBtn(acPadIndexOf(btn));
}

// TV 页按下反馈：只重绘该按键
static void pressTvBtn(const IrTvBtn btn) {
    flashTvBtn(btn);
    drawTvPadBtn(tvPadIndexOf(btn));
}

// 发送：先点亮红色信号图标再发码（发码阻塞）
static void sendFromAcPage() {
    pressAcBtn(IrAcBtn::Send);
    g_tx_active_until_ms = millis() + IR_SIG_TX_MS;
    drawIrSignalIcon();
    sendAcState();
}

static void sendFromTvPage() {
    pressTvBtn(IrTvBtn::Send);
    g_tx_active_until_ms = millis() + IR_SIG_TX_MS;
    drawIrSignalIcon();
    sendTvAction();
}

// TV 快捷键：更新动作大字 + 按键高亮 + 立即发码
static void fireTvAction(const int action) {
    g_tv_action = action;
    const IrTvBtn btn = tvActionToBtn(action);
    pressTvBtn(btn);
    drawTvActionText();
    g_tx_active_until_ms = millis() + IR_SIG_TX_MS;
    drawIrSignalIcon();
    sendTvAction();
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
    g_config_dirty = false;
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

    g_tx_active_until_ms = 0;
    g_sig_icon_active = false;

    ensureIrReady();
    // 按需分配缓存再预载；OOM 时绘制走 PNG/逐次读
    (void)ensureAcIconCache();
    preloadAcModeIcons();
    preloadAcFanIcons();
    preloadAcSignalIcons();
    redrawIr();
}

void leaveIrApp() {
    flushIrConfigSave(true);
    freeAcIconCache();
}

// 只有 Help 页带 header；遥控主页需要屏蔽定时刷新
bool irAppSuppressesHeader() {
    return !g_help_visible;
}

void updateIrApp() {
    flushIrConfigSave();
    if (g_help_visible) {
        return;
    }

    // AC / TV 全屏页只回收过期的局部状态，不整页重绘
    if (g_press_ac != IrAcBtn::None &&
        static_cast<int32_t>(millis() - g_press_until_ms) >= 0) {
        const int idx = acPadIndexOf(g_press_ac);
        g_press_ac = IrAcBtn::None;
        drawAcPadBtn(idx);
    }
    if (g_press_tv != IrTvBtn::None &&
        static_cast<int32_t>(millis() - g_press_until_ms) >= 0) {
        const int idx = tvPadIndexOf(g_press_tv);
        g_press_tv = IrTvBtn::None;
        drawTvPadBtn(idx);
    }
    if (g_sig_icon_active != isIrTxActive()) {
        drawIrSignalIcon();
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
            drawTvBrandLogo();
        } else {
            const int n = static_cast<int>(IrAcBrand::Count);
            g_ac_brand = (g_ac_brand + 1) % n;
            drawAcBrandLogo();
        }
        markIrConfigDirty();
        return;
    }

    for (const char c : status.word) {
        if (c == 't' || c == 'T') {
            g_category = (g_category == IrCategory::TV) ? IrCategory::AC : IrCategory::TV;
            g_screen_ready = false;
            drawIrMain();
            markIrConfigDirty();
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
            } else if (c == '[') {
                action = static_cast<int>(IrTvAction::ChUp); // 设计稿：[ = CH+
            } else if (c == ']') {
                action = static_cast<int>(IrTvAction::ChDown);
            } else if (c == 'i' || c == 'I') {
                action = static_cast<int>(IrTvAction::Input);
            } else if (c == 's' || c == 'S') {
                sendFromTvPage();
                return;
            }
            if (action >= 0) {
                fireTvAction(action);
                return;
            }
        }
        // 音量也可用上下方向键
        for (const uint8_t hid : status.hid_keys) {
            if (hid == 0x52 || hid == 0x33) { // Up / ;
                fireTvAction(static_cast<int>(IrTvAction::VolUp));
                return;
            }
            if (hid == 0x51 || hid == 0x37) { // Down / .
                fireTvAction(static_cast<int>(IrTvAction::VolDown));
                return;
            }
        }
        for (const char c : status.word) {
            if (c == ';') {
                fireTvAction(static_cast<int>(IrTvAction::VolUp));
                return;
            }
            if (c == '.') {
                fireTvAction(static_cast<int>(IrTvAction::VolDown));
                return;
            }
        }
        if (status.enter || status.space) {
            sendFromTvPage();
            return;
        }
    }

    // AC 遥控快捷键：每个动作只重绘受影响的区域
    if (g_category == IrCategory::AC) {
        for (const char c : status.word) {
            if (c == 'p' || c == 'P') {
                g_ac_field = static_cast<int>(IrAcField::Power);
                g_ac_power = !g_ac_power;
                pressAcBtn(IrAcBtn::Power);
                // 关机态：模式图标与温度转灰
                drawAcModeIcon();
                drawAcTemp();
                return;
            }
            if (c == 'm' || c == 'M') {
                g_ac_field = static_cast<int>(IrAcField::Mode);
                cycleAcMode(1);
                pressAcBtn(IrAcBtn::Mode);
                drawAcModeIcon();
                return;
            }
            if (c == 'f' || c == 'F') {
                g_ac_field = static_cast<int>(IrAcField::Fan);
                cycleAcFan(1);
                pressAcBtn(IrAcBtn::Fan);
                drawAcFanIcon();
                return;
            }
            if (c == 's' || c == 'S') {
                sendFromAcPage();
                return;
            }
        }
        // 温度：上下方向键（; .）与 - = 等价
        int temp_delta = 0;
        for (const uint8_t hid : status.hid_keys) {
            if (hid == 0x52 || hid == 0x33) { // Up / ;
                temp_delta = 1;
            } else if (hid == 0x51 || hid == 0x37) { // Down / .
                temp_delta = -1;
            }
        }
        for (const char c : status.word) {
            if (c == ';' || c == '=' || c == '+') {
                temp_delta = 1;
            } else if (c == '.' || c == '-' || c == '_') {
                temp_delta = -1;
            }
        }
        if (temp_delta != 0) {
            g_ac_field = static_cast<int>(IrAcField::Temp);
            adjustAcField(temp_delta);
            pressAcBtn(temp_delta > 0 ? IrAcBtn::TempUp : IrAcBtn::TempDown);
            drawAcTemp();
            return;
        }
        if (status.enter || status.space) {
            sendFromAcPage();
            return;
        }
    }
}

// BtnA：发送当前红外指令
void pollIrBtnA() {
    if (g_help_visible) {
        return;
    }
    if (!M5Cardputer.BtnA.wasPressed()) {
        return;
    }
    if (g_category == IrCategory::AC) {
        sendFromAcPage();
        return;
    }
    sendFromTvPage();
}
