#include "app_radio.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include "app_icons.h"
#include "fm_tuner.h"
#include <Preferences.h>
#include <cstdio>
#include <cstring>

namespace {

static constexpr int RADIO_HOTKEY_COUNT = 10; // 1-0 数字快捷槽数量
static constexpr int RADIO_STATION_MAX = 64;  // 列表可存任意个（有上限）
static constexpr int RADIO_NAME_MAX = 16;
static constexpr int DIAL_MARGIN = 14; // 两端留空，MHz 数字不被裁切
static constexpr int RADIO_UI_LEFT = APP_CONTENT_X;
static constexpr int RADIO_UI_TOP = APP_CONTENT_INSET_Y;
static constexpr int RADIO_FREQ_Y = 66; // 刻度下方，较原先再下移 10px
static constexpr int RADIO_FREQ_SCALE = 3;
static constexpr int RADIO_PLAY_SIZE = 40;
static constexpr int RADIO_METER_Y = 112;
static constexpr int RADIO_NO_MOD_Y = RADIO_METER_Y - 14; // 未连接提示在信号条上方
static constexpr int RADIO_TITLE_H = 16; // size-2 标题高度
static constexpr int RADIO_TITLE_GAP = 2; // 标题与列表间距（收紧以多显示行）
static constexpr int RADIO_LIST_ROW_H = 11;
static constexpr int RADIO_LIST_TIP_H = 12; // 仅重命名时底栏提示
static constexpr int RADIO_PAGER_SCALE = 2; // 条数/页码点阵字号
static constexpr int RADIO_DIAL_REGION_H = 52; // 刻度+MHz 数字（局部刷新）
static constexpr uint8_t RADIO_DIRTY_DIAL = 1u << 0;
static constexpr uint8_t RADIO_DIRTY_FREQ = 1u << 1;
static constexpr uint8_t RADIO_DIRTY_METER = 1u << 2;
static constexpr uint8_t RADIO_DIRTY_LIVE =
    RADIO_DIRTY_DIAL | RADIO_DIRTY_FREQ | RADIO_DIRTY_METER;

struct RadioPreset {
    uint16_t freq; // 0 = 空
    char name[RADIO_NAME_MAX + 1];
};

enum class RadioView {
    Main,
    Stations,
    Rename,
    Tuner,
    Rds,
};

enum class RadioHelpKind : uint8_t {
    None = 0,
    Main,
    Stations,
    Tuner,
    Rds,
};

enum class TunerItem : uint8_t {
    Band = 0,
    Deemph,
    SeekMode,
    SeekStop,
    Injection,
    SoftMute,
    HighCut,
    Snc,
    ChMute,
    Count,
};

enum class RdaTunerItem : uint8_t {
    Band = 0,
    Spacing,
    Deemph,
    SeekMode,
    SeekThreshold,
    SeekWrap,
    Volume,
    Bass,
    SoftMute,
    SoftBlend,
    Rds,
    Rbds,
    Afc,
    Count,
};

static FmTuner g_radio;
static M5Canvas radioCanvas(&M5Cardputer.Display);
static bool g_canvas_ok = false;
static int g_canvas_w = 0;
static int g_canvas_h = 0;
static bool g_ready = false;
static RadioHelpKind g_help_kind = RadioHelpKind::None;
static int g_help_page = 0;
static RadioView g_view = RadioView::Main;
static bool g_muted = false;
static bool g_mono = false;
static bool g_seeking = false;
static bool g_auto_scanning = false;
static bool g_seek_up = true;
static bool g_seek_hw = false;
static bool g_seek_wrapped = false;
static uint16_t g_freq = 9850; // 98.50 MHz
static uint8_t g_rssi = 0;
static uint8_t g_if_counter = 0;
static uint16_t g_chip_id = 0;
static bool g_stereo = false;
static uint32_t g_seek_poll_ms = 0; // 扫描中轮询真实 RSSI/立体声
static uint32_t g_rssi_kick_ms = 0; // 空闲时 kickAdc 时间戳
static bool g_rssi_pending = false; // 已写 PLL，等待 ADC 稳定
static uint32_t g_rds_poll_ms = 0;
static uint32_t g_seek_settle_ms = 0;
static uint32_t g_seek_hw_start_ms = 0;
static uint16_t g_seek_origin = 0;
static int g_scan_found_count = 0;
static uint16_t g_scan_last_found = 0;
static constexpr uint32_t RADIO_SEEK_SETTLE_MS = 60;
static constexpr uint32_t RADIO_RDA_SEEK_SETTLE_MS = 120; // RDA 需更久才稳 FM_TRUE/RSSI
static constexpr uint32_t RADIO_RSSI_POLL_MS = 250; // 空闲信号刷新周期（kick + 60ms settle）
static constexpr uint32_t RADIO_HW_SEEK_TIMEOUT_MS = 8000;
static bool g_japan = false;
static bool g_deemph75 = false;
static bool g_hw_seek_pref = false; // false=软件步进，true=芯片 SM
static bool g_hlsi_high = true;
static bool g_soft_mute = true;
static bool g_hcc = true;
static bool g_snc = true;
static FmTuner::SeekStop g_ssl = FmTuner::SeekStop::Mid;
static FmTuner::ChannelMute g_ch_mute = FmTuner::ChannelMute::Off;
static uint8_t g_rda_band = 0;
static uint8_t g_rda_spacing = 0; // 默认 100 kHz
static uint8_t g_rda_volume = 8;
static uint8_t g_rda_seek_threshold = 8; // datasheet 默认；停台后再用 FM_TRUE 滤假台
static bool g_rda_hw_seek = true;
static bool g_rda_wrap = true;
static bool g_rda_bass = false;
static bool g_rda_soft_mute = true;
static bool g_rda_soft_blend = true;
static bool g_rda_rds = true;
static bool g_rda_rbds = false;
static bool g_rda_afc = true;
static bool g_rda_deemph75 = false;
static bool g_rda_volume_dirty = false;
static int g_tuner_sel = 0;
static int g_tuner_scroll = 0;
static RadioPreset g_presets[RADIO_STATION_MAX]{};
static int g_station_count = 0;
static int g_sel_slot = 0;
static int g_list_scroll = 0;
static int g_active_slot = -1;
static char g_rename_buf[RADIO_NAME_MAX + 1];
enum class RadioRepeatKind : uint8_t {
    None = 0,
    Tune,
    Volume,
};
static RadioRepeatKind g_repeat_kind = RadioRepeatKind::None;
static int8_t g_tune_repeat_dir = 0;
static uint32_t g_tune_repeat_since_ms = 0;
static uint32_t g_tune_repeat_last_ms = 0;
static constexpr uint32_t RADIO_TUNE_REPEAT_DELAY_MS = 320;
static constexpr uint32_t RADIO_TUNE_REPEAT_RATE_MS = 90;

static uint16_t radioFreqMin() {
    return g_ready ? g_radio.freqMin() : (g_japan ? Tea5767::FREQ_JP_MIN : Tea5767::FREQ_EU_MIN);
}

static uint16_t radioFreqMax() {
    return g_ready ? g_radio.freqMax() : (g_japan ? Tea5767::FREQ_JP_MAX : Tea5767::FREQ_EU_MAX);
}

static uint16_t radioFreqStep() {
    return g_ready ? g_radio.freqStep() : Tea5767::FREQ_STEP;
}

static uint8_t seekRssiThreshold() {
    if (g_radio.isRda()) {
        return constrain(g_rda_seek_threshold, 1, 15);
    }
    switch (g_ssl) {
        case FmTuner::SeekStop::Low:
            return 5;
        case FmTuner::SeekStop::High:
            return 10;
        case FmTuner::SeekStop::Mid:
        default:
            return 7;
    }
}

static bool useHardwareSeek() {
    return g_radio.isRda() ? g_rda_hw_seek : g_hw_seek_pref;
}

static int mapFreqToX(const int x0, const int w, const uint16_t freq_centi) {
    const long in = static_cast<long>(freq_centi);
    const long lo = radioFreqMin();
    const long span = static_cast<long>(radioFreqMax()) - lo;
    if (span <= 0) {
        return x0;
    }
    return x0 + static_cast<int>((in - lo) * w / span);
}

static void formatFreqText(const uint16_t freq_centi, char* out, const size_t out_len) {
    const unsigned whole = freq_centi / 100;
    const unsigned frac = (freq_centi / 10) % 10;
    snprintf(out, out_len, "%u.%u", whole, frac);
}

static bool isPresetValid(const RadioPreset& preset) {
    return preset.freq >= radioFreqMin() && preset.freq <= radioFreqMax();
}

// 存盘用：不依赖当前频段，只要像合法 FM 频点
static bool isPresetStored(const RadioPreset& preset) {
    return preset.freq >= 5000 && preset.freq <= Tea5767::FREQ_EU_MAX;
}

static void presetLabel(const RadioPreset& preset, char* out, const size_t out_len) {
    if (preset.name[0] != '\0') {
        snprintf(out, out_len, "%s", preset.name);
        return;
    }
    if (isPresetValid(preset)) {
        char freq_text[12];
        formatFreqText(preset.freq, freq_text, sizeof(freq_text));
        snprintf(out, out_len, "%s MHz", freq_text);
        return;
    }
    snprintf(out, out_len, "Empty");
}

static void clearAllStations() {
    g_station_count = 0;
    g_sel_slot = 0;
    g_list_scroll = 0;
    g_active_slot = -1;
    for (int i = 0; i < RADIO_STATION_MAX; ++i) {
        g_presets[i].freq = 0;
        g_presets[i].name[0] = '\0';
    }
}

static void saveAllStations() {
    Preferences prefs;
    if (!prefs.begin("radio", false)) {
        return;
    }
    prefs.putUChar("sc", static_cast<uint8_t>(g_station_count));
    if (g_station_count > 0) {
        prefs.putBytes("st", g_presets, sizeof(RadioPreset) * static_cast<size_t>(g_station_count));
    } else {
        prefs.remove("st");
    }
    // 清掉旧版 f0-n9 单槽键，避免下次误读
    for (int i = 0; i < RADIO_HOTKEY_COUNT; ++i) {
        char key[4];
        snprintf(key, sizeof(key), "f%d", i);
        prefs.remove(key);
        snprintf(key, sizeof(key), "n%d", i);
        prefs.remove(key);
    }
    prefs.end();
}

// 清空列表并写入 NVS
static void clearSavedStations() {
    clearAllStations();
    saveAllStations();
}

static void loadRadioPresets() {
    clearAllStations();
    Preferences prefs;
    if (!prefs.begin("radio", true)) {
        return;
    }

    // 新格式：count + blob
    const uint8_t sc = prefs.getUChar("sc", 0xFF);
    if (sc != 0xFF) {
        g_station_count = sc;
        if (g_station_count > RADIO_STATION_MAX) {
            g_station_count = RADIO_STATION_MAX;
        }
        if (g_station_count > 0) {
            const size_t need = sizeof(RadioPreset) * static_cast<size_t>(g_station_count);
            const size_t got = prefs.getBytes("st", g_presets, need);
            if (got < need) {
                g_station_count = static_cast<int>(got / sizeof(RadioPreset));
            }
        }
        prefs.end();
        // 丢掉完全非法的条目；跨频段的留给 isPresetValid 在 UI 里判断
        int w = 0;
        for (int i = 0; i < g_station_count; ++i) {
            if (!isPresetStored(g_presets[i])) {
                continue;
            }
            if (w != i) {
                g_presets[w] = g_presets[i];
            }
            ++w;
        }
        for (int i = w; i < g_station_count; ++i) {
            g_presets[i].freq = 0;
            g_presets[i].name[0] = '\0';
        }
        g_station_count = w;
        return;
    }

    // 旧格式迁移：f0-n9 固定 10 槽
    for (int i = 0; i < RADIO_HOTKEY_COUNT; ++i) {
        char key[4];
        snprintf(key, sizeof(key), "f%d", i);
        const uint16_t freq = prefs.getUShort(key, 0);
        snprintf(key, sizeof(key), "n%d", i);
        const String name = prefs.getString(key, "");
        RadioPreset p{};
        p.freq = freq;
        strncpy(p.name, name.c_str(), RADIO_NAME_MAX);
        p.name[RADIO_NAME_MAX] = '\0';
        if (!isPresetStored(p)) {
            continue;
        }
        g_presets[g_station_count++] = p;
    }
    prefs.end();
    if (g_station_count > 0) {
        saveAllStations(); // 写成新格式
    }
}

static void loadTunerSettings() {
    Preferences prefs;
    if (!prefs.begin("radio", true)) {
        return;
    }
    g_japan = prefs.getBool("jp", false);
    g_deemph75 = prefs.getBool("dtc75", false);
    g_hw_seek_pref = prefs.getBool("hwseek", false);
    g_hlsi_high = prefs.getBool("hlsi", true);
    g_soft_mute = prefs.getBool("smute", true);
    g_hcc = prefs.getBool("hcc", true);
    g_snc = prefs.getBool("snc", true);
    const uint8_t ssl = prefs.getUChar("ssl", 2);
    if (ssl == 1) {
        g_ssl = FmTuner::SeekStop::Low;
    } else if (ssl == 3) {
        g_ssl = FmTuner::SeekStop::High;
    } else {
        g_ssl = FmTuner::SeekStop::Mid;
    }
    const uint8_t ch = prefs.getUChar("chmute", 0);
    if (ch == 1) {
        g_ch_mute = FmTuner::ChannelMute::Left;
    } else if (ch == 2) {
        g_ch_mute = FmTuner::ChannelMute::Right;
    } else {
        g_ch_mute = FmTuner::ChannelMute::Off;
    }
    g_rda_band = prefs.getUChar("rband", 0);
    g_rda_spacing = prefs.getUChar("rstep", 0);
    g_rda_volume = constrain(prefs.getUChar("rvol", 8), 0, 15);
    // 先前曾把默认 SeekTh 8→11；有 FM_TRUE 后改回 8（仅撤销自动升到 11 的）
    const bool seekth_relaxed = prefs.getBool("rsk8", false);
    uint8_t seekth = prefs.getUChar("rseekth", 8);
    if (!seekth_relaxed && prefs.getBool("rsk11", false) && seekth == 11) {
        seekth = 8;
    }
    g_rda_seek_threshold = constrain(seekth, 0, 15);
    g_rda_hw_seek = prefs.getBool("rhwseek", true);
    g_rda_wrap = prefs.getBool("rwrap", true);
    g_rda_bass = prefs.getBool("rbass", false);
    g_rda_soft_mute = prefs.getBool("rsmute", true);
    g_rda_soft_blend = prefs.getBool("rsblend", true);
    g_rda_rds = prefs.getBool("rrds", true);
    g_rda_rbds = prefs.getBool("rrbds", false);
    g_rda_afc = prefs.getBool("rafc", true);
    g_rda_deemph75 = prefs.getBool("rdtc75", false);
    prefs.end();
    if (!seekth_relaxed) {
        Preferences write_prefs;
        if (write_prefs.begin("radio", false)) {
            write_prefs.putBool("rsk8", true);
            write_prefs.putUChar("rseekth", g_rda_seek_threshold);
            write_prefs.end();
        }
    }
}

static void saveTunerSettings() {
    Preferences prefs;
    if (!prefs.begin("radio", false)) {
        return;
    }
    prefs.putBool("jp", g_japan);
    prefs.putBool("dtc75", g_deemph75);
    prefs.putBool("hwseek", g_hw_seek_pref);
    prefs.putBool("hlsi", g_hlsi_high);
    prefs.putBool("smute", g_soft_mute);
    prefs.putBool("hcc", g_hcc);
    prefs.putBool("snc", g_snc);
    prefs.putUChar("ssl", static_cast<uint8_t>(g_ssl));
    prefs.putUChar("chmute", static_cast<uint8_t>(g_ch_mute));
    prefs.putUChar("rband", g_rda_band);
    prefs.putUChar("rstep", g_rda_spacing);
    prefs.putUChar("rvol", g_rda_volume);
    prefs.putUChar("rseekth", g_rda_seek_threshold);
    prefs.putBool("rhwseek", g_rda_hw_seek);
    prefs.putBool("rwrap", g_rda_wrap);
    prefs.putBool("rbass", g_rda_bass);
    prefs.putBool("rsmute", g_rda_soft_mute);
    prefs.putBool("rsblend", g_rda_soft_blend);
    prefs.putBool("rrds", g_rda_rds);
    prefs.putBool("rrbds", g_rda_rbds);
    prefs.putBool("rafc", g_rda_afc);
    prefs.putBool("rdtc75", g_rda_deemph75);
    prefs.end();
}

static void applyTunerToChip() {
    if (!g_ready) {
        return;
    }
    if (g_radio.isRda()) {
        static const Rda5807m::Band kBands[] = {
            Rda5807m::Band::UsEurope, Rda5807m::Band::Japan, Rda5807m::Band::Worldwide,
            Rda5807m::Band::EastEurope, Rda5807m::Band::Low,
        };
        static const Rda5807m::Spacing kSpacings[] = {
            Rda5807m::Spacing::Khz100, Rda5807m::Spacing::Khz200,
            Rda5807m::Spacing::Khz50, Rda5807m::Spacing::Khz25,
        };
        g_rda_band = constrain(g_rda_band, 0, 4);
        g_rda_spacing = constrain(g_rda_spacing, 0, 3);
        Rda5807m& rda = g_radio.rda();
        (void)rda.setBand(kBands[g_rda_band]);
        (void)rda.setSpacing(kSpacings[g_rda_spacing]);
        (void)rda.setDeemphasis50(!g_rda_deemph75);
        (void)rda.setSeekThreshold(g_rda_seek_threshold);
        (void)rda.setSeekWrap(g_rda_wrap);
        (void)rda.setVolume(g_rda_volume);
        (void)rda.setBass(g_rda_bass);
        (void)rda.setSoftMute(g_rda_soft_mute);
        (void)rda.setSoftBlend(g_rda_soft_blend);
        (void)rda.setRds(g_rda_rds);
        (void)rda.setRbds(g_rda_rbds);
        (void)rda.setAfc(g_rda_afc);
        (void)rda.setHighZ(false);
        (void)rda.setMute(g_muted);
        (void)rda.setMono(g_mono);
        return;
    }
    g_radio.setJapanBand(g_japan);
    g_radio.setDeemphasis75(g_deemph75);
    g_radio.setHighSideInjection(g_hlsi_high);
    g_radio.setSoftMute(g_soft_mute);
    g_radio.setHighCut(g_hcc);
    g_radio.setStereoNoiseCancel(g_snc);
    g_radio.setSeekStop(g_ssl);
    g_radio.setChannelMute(g_ch_mute);
    g_radio.setMute(g_muted);
    g_radio.setMono(g_mono);
}

static void clampFreqToBand() {
    const uint16_t lo = radioFreqMin();
    const uint16_t hi = radioFreqMax();
    if (g_freq < lo || g_freq > hi) {
        g_freq = static_cast<uint16_t>((static_cast<uint32_t>(lo) + hi) / 2);
    }
}

// 点阵字绘制到离屏 canvas（与 drawDotText 同风格）
static void drawDotTextOnCanvas(const char* text, const int x, const int y, const int scale,
                                const uint16_t color) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    radioCanvas.setFont(&fonts::Font0);
    radioCanvas.setTextSize(1);
    const int w = radioCanvas.textWidth(text);
    M5Canvas spr(&M5Cardputer.Display);
    spr.setColorDepth(16);
    if (scale < 2 || w <= 0 || !spr.createSprite(w, DOT_TEXT_H_1X)) {
        radioCanvas.setTextSize(scale < 1 ? 1 : scale);
        radioCanvas.setTextColor(color, BLACK);
        radioCanvas.setCursor(x, y);
        radioCanvas.print(text);
        radioCanvas.setTextFont(1);
        radioCanvas.setTextSize(1);
        return;
    }
    spr.setFont(&fonts::Font0);
    spr.setTextSize(1);
    spr.fillSprite(BLACK);
    spr.setTextColor(WHITE, BLACK);
    spr.setCursor(0, 0);
    spr.print(text);

    const int block = scale - 1;
    for (int py = 0; py < DOT_TEXT_H_1X; ++py) {
        for (int px = 0; px < w; ++px) {
            if (spr.readPixel(px, py) != 0) {
                radioCanvas.fillRect(x + px * scale, y + py * scale, block, block, color);
            }
        }
    }
    spr.deleteSprite();
    radioCanvas.setTextFont(1);
    radioCanvas.setTextSize(1);
}

// FM 频段刻度：高 30px、距顶 10px；偶数 MHz 长刻度+数字，奇数 MHz 短刻度
static void drawDial(const uint16_t freq_centi) {
    constexpr int dial_y = 10;
    constexpr int dial_h = 30;
    const int dial_w = g_canvas_w;
    const int inner_x = DIAL_MARGIN;
    const int inner_w = dial_w - DIAL_MARGIN * 2;
    const int dial_bottom = dial_y + dial_h - 1;

    // 上下边框
    radioCanvas.drawFastHLine(0, dial_y, dial_w, APP_COLOR_MUTED);
    radioCanvas.drawFastHLine(0, dial_bottom, dial_w, APP_COLOR_MUTED);

    const int mhz_lo = static_cast<int>(radioFreqMin() / 100);
    const int mhz_hi = static_cast<int>(radioFreqMax() / 100);
    for (int mhz = mhz_lo; mhz <= mhz_hi; ++mhz) {
        const uint16_t tick_freq = static_cast<uint16_t>(mhz * 100);
        if (tick_freq < radioFreqMin() || tick_freq > radioFreqMax()) {
            continue;
        }
        const bool major = (mhz % 2 == 0);
        const int tick_h = major ? 12 : 6;
        const int x = mapFreqToX(inner_x, inner_w, tick_freq);
        radioCanvas.drawFastVLine(x, dial_bottom - tick_h, tick_h, APP_COLOR_HINT);

        if (!major) {
            continue;
        }

        char label[4];
        snprintf(label, sizeof(label), "%d", mhz);
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        const int tw = radioCanvas.textWidth(label);
        int lx = x - tw / 2;
        if (lx < 0) {
            lx = 0;
        }
        if (lx + tw > dial_w) {
            lx = dial_w - tw;
        }
        radioCanvas.setCursor(lx, dial_y + dial_h + 2);
        radioCanvas.print(label);
    }

    // 已保存 / 扫描中已发现的电台：刻度上方 2x2 方块（当前槽位高亮）
    for (int i = 0; i < g_station_count; ++i) {
        if (!isPresetValid(g_presets[i])) {
            continue;
        }
        const int px = mapFreqToX(inner_x, inner_w, g_presets[i].freq);
        const bool active = (i == g_active_slot);
        const uint16_t color = active ? APP_COLOR_LABEL : APP_COLOR_OK;
        const int mx = constrain(px, 0, dial_w - 2);
        radioCanvas.fillRect(mx, dial_y + 2, 2, 2, color);
    }

    const int px = mapFreqToX(inner_x, inner_w, freq_centi);
    const int needle_x = constrain(px, 0, dial_w - 2);
    radioCanvas.fillRect(needle_x, dial_y + 1, 2, dial_h - 2, RED);
}

// 信号条始终按真实 RSSI 绘制（扫描中也不做假动画）
static void drawSignalBars(const int x, const int y, const int bar_w, const int bar_gap, const int max_h,
                           const uint8_t rssi) {
    constexpr int bars = 5;
    for (int i = 0; i < bars; ++i) {
        const int bx = x + i * (bar_w + bar_gap);
        const int bh = 6 + i * 3;
        const int by = y + max_h - bh;

        int level = 0;
        if (g_ready) {
            level = static_cast<int>((rssi * bars + 14) / 15);
        }
        if (i < level) {
            // active：实心占满原含 border 的整块，不再画边框
            uint16_t color = APP_COLOR_OK;
            if (i >= bars - 2) {
                color = APP_COLOR_WARN;
            }
            radioCanvas.fillRect(bx, by, bar_w, bh, color);
        } else {
            radioCanvas.drawRect(bx, by, bar_w, bh, APP_COLOR_MUTED);
        }
    }
}

// RDA 音量 0–15：5 列×3 行，自底从左往右填黄格；高度与信号条一致
static void drawVolumeGrid(const int x, const int y, const int max_h, const uint8_t volume) {
    constexpr int cols = 5;
    constexpr int rows = 3;
    constexpr int cell_w = 5;
    constexpr int h_gap = 2;
    constexpr int v_gap = 1;
    const int cell_h = (max_h - (rows - 1) * v_gap) / rows;
    const int filled = g_ready ? constrain(static_cast<int>(volume), 0, cols * rows) : 0;

    for (int i = 0; i < cols * rows; ++i) {
        const int col = i % cols;
        const int row_from_bottom = i / cols;
        const int row = (rows - 1) - row_from_bottom;
        const int cx = x + col * (cell_w + h_gap);
        const int cy = y + row * (cell_h + v_gap);
        if (i < filled) {
            radioCanvas.fillRect(cx, cy, cell_w, cell_h, YELLOW);
        } else {
            radioCanvas.drawRect(cx, cy, cell_w, cell_h, APP_COLOR_MUTED);
        }
    }
}

// 播放/停止图标：播放=绿色三角，停止=红色圆角方块
static void drawPlayPauseIcon(const int x, const int y) {
    constexpr int size = RADIO_PLAY_SIZE;
    const bool playing = g_ready && !g_muted;
    if (playing) {
        constexpr int pad = 9;
        radioCanvas.fillTriangle(x + pad, y + pad, x + pad, y + size - pad, x + size - pad,
                                 y + size / 2, APP_COLOR_OK);
    } else {
        constexpr int pad = 11;
        radioCanvas.fillRoundRect(x + pad, y + pad, size - pad * 2, size - pad * 2, 3,
                                  APP_COLOR_ERROR);
    }
}

static int drawTextBadgeOnCanvas(const int x, const int y, const char* label, const int text_size) {
    if (label == nullptr || label[0] == '\0') {
        return 0;
    }
    radioCanvas.setTextSize(text_size);
    const int tw = radioCanvas.textWidth(label);
    constexpr int pad_x = 3;
    constexpr int pad_y = 1;
    const int badge_w = tw + pad_x * 2;
    const int badge_h = 8 * text_size + pad_y * 2;
    radioCanvas.fillRoundRect(x, y, badge_w, badge_h, 2, YELLOW);
    radioCanvas.setTextColor(APP_COLOR_KEY_TEXT, YELLOW);
    radioCanvas.setCursor(x + pad_x, y + pad_y);
    radioCanvas.print(label);
    radioCanvas.setTextSize(1);
    return badge_w;
}

static void drawStatusBadges(const int x, const int y) {
    int cx = x;
    if (!g_ready) {
        cx += drawTextBadgeOnCanvas(cx, y, "NO MOD", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
        return;
    }
    if (g_muted) {
        cx += drawTextBadgeOnCanvas(cx, y, "MUTE", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    } else if (g_ch_mute == FmTuner::ChannelMute::Left) {
        cx += drawTextBadgeOnCanvas(cx, y, "ML", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    } else if (g_ch_mute == FmTuner::ChannelMute::Right) {
        cx += drawTextBadgeOnCanvas(cx, y, "MR", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    }

    if (g_stereo && !g_mono) {
        cx += drawTextBadgeOnCanvas(cx, y, "ST", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    } else {
        cx += drawTextBadgeOnCanvas(cx, y, "MONO", 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    }

    const char* band_badge = nullptr;
    if (g_radio.isRda()) {
        static const char* kBandBadges[] = {"", "JP", "WIDE", "EE", "LOW"};
        band_badge = kBandBadges[constrain(g_rda_band, 0, 4)];
    } else if (g_japan) {
        band_badge = "JP";
    }
    if (band_badge != nullptr && band_badge[0] != '\0') {
        cx += drawTextBadgeOnCanvas(cx, y, band_badge, 1) + 2;
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    }

    if (g_seeking) {
        radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
        radioCanvas.setCursor(cx + 4, y + 1);
        radioCanvas.print(g_seek_hw ? "SEEK HW" : "SEEK");
    }
}

static void drawFrequencyDisplay(const uint16_t freq_centi) {
    char freq_text[12];
    formatFreqText(freq_centi, freq_text, sizeof(freq_text));

    constexpr int scale = RADIO_FREQ_SCALE;
    const int text_w = measureDotTextWidth1x(freq_text) * scale;
    const int text_h = DOT_TEXT_H_1X * scale;
    const int freq_x = (g_canvas_w - text_w) / 2;
    constexpr int freq_y = RADIO_FREQ_Y;

    drawDotTextOnCanvas(freq_text, freq_x, freq_y, scale, WHITE);

    radioCanvas.setTextSize(1);
    radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    radioCanvas.setCursor(freq_x + text_w + 4, freq_y + text_h - 10);
    radioCanvas.print("MHz");

    if (g_active_slot >= 0 && g_active_slot < g_station_count) {
        const RadioPreset& preset = g_presets[g_active_slot];
        const bool has_name = preset.name[0] != '\0'; // 未改名则只显示序号
        constexpr int line_h = 8;
        const int block_h = has_name ? (line_h * 2 + 1) : line_h;
        const int block_y = freq_y + (text_h - block_h) / 2;

        char slot_text[8];
        snprintf(slot_text, sizeof(slot_text), "#%d", g_active_slot + 1);
        radioCanvas.setTextSize(1);
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, block_y);
        radioCanvas.print(slot_text);

        if (has_name) {
            // 名称不超过频率数字左缘，过长则截断
            const int max_w = freq_x - RADIO_UI_LEFT - 4;
            char shown[RADIO_NAME_MAX + 1];
            strncpy(shown, preset.name, sizeof(shown) - 1);
            shown[sizeof(shown) - 1] = '\0';
            while (shown[0] != '\0' && radioCanvas.textWidth(shown) > max_w) {
                shown[strlen(shown) - 1] = '\0';
            }
            radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
            radioCanvas.setCursor(RADIO_UI_LEFT, block_y + line_h + 1);
            radioCanvas.print(shown);
        }
    }
}

// 右下角电池；距边 5px（图标 API 不走 canvas，推屏时避开这块）
static int g_bat_shown_level = -1;
static bool g_bat_shown_charging = false;
static bool g_bat_drawn = false;
static uint32_t g_bat_sample_ms = 0;
static constexpr uint32_t RADIO_BAT_SAMPLE_MS = 2000;

static void radioBatteryRect(int* x, int* y, int* w, int* h) {
    *w = getIconBatteryDisplayWidth(g_bat_shown_charging);
    *h = getIconBatteryBodyHeight();
    *x = g_canvas_w - *w - APP_HELP_EDGE;
    *y = g_canvas_h - *h - APP_HELP_EDGE;
}

static int radioBatteryLeftX() {
    return g_canvas_w - getIconBatteryDisplayWidth(g_bat_shown_charging) - APP_HELP_EDGE;
}

static void drawRadioBattery(const bool force_draw) {
    const uint32_t now = millis();
    const bool need_sample =
        (g_bat_shown_level < 0) || (!g_seeking && now - g_bat_sample_ms >= RADIO_BAT_SAMPLE_MS);
    bool changed = false;
    if (need_sample) {
        const int level = M5Cardputer.Power.getBatteryLevel();
        const bool charging = isBatteryCharging();
        changed = (level != g_bat_shown_level) || (charging != g_bat_shown_charging);
        g_bat_shown_level = level;
        g_bat_shown_charging = charging;
        g_bat_sample_ms = now;
    }
    // 已画过且数值没变：不要擦了重画，否则会闪
    if (!force_draw && g_bat_drawn && !changed) {
        return;
    }
    int bat_x = 0;
    int bat_y = 0;
    int bat_w = 0;
    int bat_h = 0;
    radioBatteryRect(&bat_x, &bat_y, &bat_w, &bat_h);
    drawIconBattery(bat_x, bat_y, g_bat_shown_level, g_bat_shown_charging);
    g_bat_drawn = true;
}

static void drawNoModuleHint() {
    if (g_ready) {
        return;
    }
    radioCanvas.setTextSize(1);
    radioCanvas.setTextColor(APP_COLOR_ERROR, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, RADIO_NO_MOD_Y);
    radioCanvas.print("No FM module");
}

static void drawRdaProgramInfo() {
    if (!g_ready || !g_radio.isRda()) {
        return;
    }
    const char* ps = g_radio.rda().programService();
    if (ps == nullptr || ps[0] == '\0') {
        return;
    }
    char shown[9];
    strncpy(shown, ps, sizeof(shown) - 1);
    shown[sizeof(shown) - 1] = '\0';
    radioCanvas.setTextSize(1);
    radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, RADIO_NO_MOD_Y);
    radioCanvas.print(shown);
}

static int stationsListTopY() {
    return APP_HELP_EDGE + RADIO_TITLE_H + RADIO_TITLE_GAP;
}

static int stationsListTipH() {
    // 平时不留底栏，列表可多 1～2 行；重命名才预留提示
    return (g_view == RadioView::Rename) ? RADIO_LIST_TIP_H : 0;
}

static int stationsVisibleRows() {
    const int avail = g_canvas_h - stationsListTopY() - stationsListTipH();
    const int rows = avail / RADIO_LIST_ROW_H;
    return rows < 1 ? 1 : rows;
}

// 列表页码：page 从 0 起；末页 scroll 可能不足一整页
static void stationsListPager(int* page, int* page_count) {
    const int vis = stationsVisibleRows();
    const int count = g_station_count < 0 ? 0 : g_station_count;
    int pages = vis > 0 ? ((count + vis - 1) / vis) : 1;
    if (pages < 1) {
        pages = 1;
    }
    int p = 0;
    if (count > 0 && vis > 0) {
        const int max_scroll = count > vis ? (count - vis) : 0;
        if (g_list_scroll >= max_scroll && pages > 1) {
            p = pages - 1;
        } else {
            p = g_list_scroll / vis;
        }
        if (p >= pages) {
            p = pages - 1;
        }
        if (p < 0) {
            p = 0;
        }
    }
    if (page != nullptr) {
        *page = p;
    }
    if (page_count != nullptr) {
        *page_count = pages;
    }
}

static void ensureStationVisible() {
    if (g_station_count <= 0) {
        g_list_scroll = 0;
        g_sel_slot = 0;
        return;
    }
    if (g_sel_slot < 0) {
        g_sel_slot = 0;
    }
    if (g_sel_slot >= g_station_count) {
        g_sel_slot = g_station_count - 1;
    }
    const int vis = stationsVisibleRows();
    if (g_sel_slot < g_list_scroll) {
        g_list_scroll = g_sel_slot;
    }
    if (g_sel_slot >= g_list_scroll + vis) {
        g_list_scroll = g_sel_slot - vis + 1;
    }
    const int max_scroll = g_station_count > vis ? (g_station_count - vis) : 0;
    if (g_list_scroll < 0) {
        g_list_scroll = 0;
    }
    if (g_list_scroll > max_scroll) {
        g_list_scroll = max_scroll;
    }
}

static void drawStationsList() {
    radioCanvas.fillSprite(BLACK);
    // 列表是全屏子界面，不预留主界面的 header 高度。
    constexpr int title_y = APP_HELP_EDGE;
    radioCanvas.setTextSize(2);
    radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, title_y);
    radioCanvas.print("Stations");

    // 标题右侧点阵字：总条数 + 当前页/总页
    int page = 0;
    int page_count = 1;
    stationsListPager(&page, &page_count);
    char pager[20];
    snprintf(pager, sizeof(pager), "%d  %d/%d", g_station_count, page + 1, page_count);
    radioCanvas.setFont(&fonts::Font0);
    radioCanvas.setTextSize(1);
    const int pager_w = radioCanvas.textWidth(pager) * RADIO_PAGER_SCALE;
    const int pager_x = g_canvas_w - APP_HELP_EDGE - pager_w;
    drawDotTextOnCanvas(pager, pager_x, title_y, RADIO_PAGER_SCALE, APP_COLOR_HINT);

    radioCanvas.setTextSize(1);
    const int list_y = stationsListTopY();
    const int vis = stationsVisibleRows();
    ensureStationVisible();

    if (g_station_count <= 0) {
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, list_y);
        radioCanvas.print("Empty");
    } else {
        for (int row = 0; row < vis; ++row) {
            const int i = g_list_scroll + row;
            if (i >= g_station_count) {
                break;
            }
            const int y = list_y + row * RADIO_LIST_ROW_H;
            const bool sel = (i == g_sel_slot);
            if (sel) {
                radioCanvas.fillRect(0, y - 1, g_canvas_w, RADIO_LIST_ROW_H, APP_COLOR_LABEL);
            }

            char num[6];
            // 快捷键槽位标 1-0，其余用序号
            if (i < RADIO_HOTKEY_COUNT) {
                snprintf(num, sizeof(num), "%d", (i + 1) % 10);
            } else {
                snprintf(num, sizeof(num), "%d", i + 1);
            }
            radioCanvas.setTextColor(sel ? BLACK : APP_COLOR_HINT, sel ? APP_COLOR_LABEL : BLACK);
            radioCanvas.setCursor(RADIO_UI_LEFT, y);
            radioCanvas.print(num);

            radioCanvas.setCursor(RADIO_UI_LEFT + 18, y);
            if (g_view == RadioView::Rename && sel) {
                radioCanvas.print(g_rename_buf);
                radioCanvas.print("_");
            } else {
                char label[RADIO_NAME_MAX + 8];
                presetLabel(g_presets[i], label, sizeof(label));
                radioCanvas.print(label);
            }
        }
        // 超出一屏时在列表右侧画细滚动条
        drawAppScrollbar(radioCanvas, list_y, vis * RADIO_LIST_ROW_H, g_station_count, vis,
                         g_list_scroll);
    }

    if (g_view == RadioView::Rename) {
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, g_canvas_h - 10);
        radioCanvas.print("Enter save  ` cancel");
    }
}

static bool ensureRadioCanvas() {
    if (g_canvas_ok) {
        return true;
    }
    g_canvas_w = M5Cardputer.Display.width();
    g_canvas_h = M5Cardputer.Display.height();
    radioCanvas.setColorDepth(16);
    if (!radioCanvas.createSprite(g_canvas_w, g_canvas_h)) {
        return false;
    }
    g_canvas_ok = true;
    return true;
}

static void pushRadioRect(const int x, const int y, int w, int h) {
    if (!g_canvas_ok || w <= 0 || h <= 0) {
        return;
    }
    int px = x;
    int py = y;
    if (px < 0) {
        w += px;
        px = 0;
    }
    if (py < 0) {
        h += py;
        py = 0;
    }
    if (px + w > g_canvas_w) {
        w = g_canvas_w - px;
    }
    if (py + h > g_canvas_h) {
        h = g_canvas_h - py;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    // clip 后 pushSprite 只传脏矩形，不整屏刷
    M5Cardputer.Display.setClipRect(px, py, w, h);
    radioCanvas.pushSprite(0, 0);
    M5Cardputer.Display.clearClipRect();
}

static void pushRadioFrame() {
    if (g_canvas_ok) {
        radioCanvas.pushSprite(0, 0);
    }
}

// 避开右下角电池：上半整宽 + 电池左侧
static void pushRadioAvoidBattery() {
    if (!g_canvas_ok) {
        return;
    }
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    radioBatteryRect(&bx, &by, &bw, &bh);
    if (by > 0) {
        pushRadioRect(0, 0, g_canvas_w, by);
    }
    if (bx > 0) {
        pushRadioRect(0, by, bx, g_canvas_h - by);
    }
}

static void paintRadioMainCanvas() {
    radioCanvas.fillSprite(BLACK);
    drawDial(g_freq);
    drawFrequencyDisplay(g_freq);
    drawNoModuleHint();
    drawRdaProgramInfo();

    // 播放键与频率数字纵向居中对齐
    constexpr int icon_size = RADIO_PLAY_SIZE;
    const int freq_h = DOT_TEXT_H_1X * RADIO_FREQ_SCALE;
    const int icon_x = g_canvas_w - icon_size - RADIO_UI_LEFT;
    const int icon_y = RADIO_FREQ_Y + (freq_h - icon_size) / 2;
    drawPlayPauseIcon(icon_x, icon_y);

    constexpr int meter_y = RADIO_METER_Y;
    constexpr int bar_w = 5;
    constexpr int bar_gap = 2; // 信号条间距再贴近 1px
    constexpr int bars = 5;
    constexpr int bars_h = 18;
    constexpr int vol_gap = 8; // 信号条与音量格间距
    constexpr int badge_h = 8 + 1 * 2; // text_size1 + pad_y
    const int bars_w = bars * bar_w + (bars - 1) * bar_gap;
    constexpr int vol_w = 5 * 5 + 4 * 2; // 5x3 黄格总宽
    // status 与信号条纵向居中对齐
    const int status_y = meter_y + (bars_h - badge_h) / 2;
    drawSignalBars(RADIO_UI_LEFT, meter_y, bar_w, bar_gap, bars_h, g_rssi);
    int meter_x = RADIO_UI_LEFT + bars_w + vol_gap;
    if (g_radio.isRda()) {
        drawVolumeGrid(meter_x, meter_y, bars_h, g_rda_volume);
        meter_x += vol_w + 10;
    } else {
        meter_x += 2; // TEA 无芯片音量，徽章紧跟信号条
    }
    drawStatusBadges(meter_x, status_y);
}

// 搜台/扫频：canvas 整帧绘制，只把脏区推到屏幕
static void drawRadioMainPartial(const uint8_t dirty) {
    if (!g_canvas_ok || dirty == 0) {
        return;
    }
    paintRadioMainCanvas();

    const int meter_y = RADIO_NO_MOD_Y - 1;
    const bool dial = (dirty & RADIO_DIRTY_DIAL) != 0;
    const bool freq = (dirty & RADIO_DIRTY_FREQ) != 0;
    const bool meter = (dirty & RADIO_DIRTY_METER) != 0;

    if (dial && freq) {
        pushRadioRect(0, 0, g_canvas_w, meter_y);
    } else if (dial) {
        pushRadioRect(0, 0, g_canvas_w, RADIO_DIAL_REGION_H);
    } else if (freq) {
        pushRadioRect(0, RADIO_DIAL_REGION_H, g_canvas_w, meter_y - RADIO_DIAL_REGION_H);
    }

    if (meter) {
        // 信号条在左，右侧留给电池不推
        pushRadioRect(0, meter_y, radioBatteryLeftX(), g_canvas_h - meter_y);
    }
}

static void drawRadioMain(const bool force_battery = false) {
    if (!g_canvas_ok) {
        return;
    }
    paintRadioMainCanvas();
    pushRadioAvoidBattery();
    drawRadioBattery(force_battery);
}

static const char* tunerItemLabel(const TunerItem item) {
    switch (item) {
        case TunerItem::Band:
            return "Band";
        case TunerItem::Deemph:
            return "De-emphasis";
        case TunerItem::SeekMode:
            return "Seek mode";
        case TunerItem::SeekStop:
            return "Seek stop";
        case TunerItem::Injection:
            return "Injection";
        case TunerItem::SoftMute:
            return "Soft mute";
        case TunerItem::HighCut:
            return "High cut";
        case TunerItem::Snc:
            return "Noise cancel";
        case TunerItem::ChMute:
            return "Channel mute";
        default:
            return "";
    }
}

// 返回该设置的全部可选项；*out_sel 为当前选中下标
static const char* const* tunerItemOptions(const TunerItem item, int* out_count, int* out_sel) {
    static const char* kBand[] = {"Europe", "Japan"};
    static const char* kDeemph[] = {"50 us", "75 us"};
    static const char* kSeek[] = {"Software", "Hardware"};
    static const char* kStop[] = {"Low", "Mid", "High"};
    static const char* kInject[] = {"High", "Low"};
    static const char* kOnOff[] = {"On", "Off"};
    static const char* kMute[] = {"Off", "Left", "Right"};

    switch (item) {
        case TunerItem::Band:
            *out_count = 2;
            *out_sel = g_japan ? 1 : 0;
            return kBand;
        case TunerItem::Deemph:
            *out_count = 2;
            *out_sel = g_deemph75 ? 1 : 0;
            return kDeemph;
        case TunerItem::SeekMode:
            *out_count = 2;
            *out_sel = g_hw_seek_pref ? 1 : 0;
            return kSeek;
        case TunerItem::SeekStop:
            *out_count = 3;
            if (g_ssl == FmTuner::SeekStop::Low) {
                *out_sel = 0;
            } else if (g_ssl == FmTuner::SeekStop::High) {
                *out_sel = 2;
            } else {
                *out_sel = 1;
            }
            return kStop;
        case TunerItem::Injection:
            *out_count = 2;
            *out_sel = g_hlsi_high ? 0 : 1;
            return kInject;
        case TunerItem::SoftMute:
            *out_count = 2;
            *out_sel = g_soft_mute ? 0 : 1;
            return kOnOff;
        case TunerItem::HighCut:
            *out_count = 2;
            *out_sel = g_hcc ? 0 : 1;
            return kOnOff;
        case TunerItem::Snc:
            *out_count = 2;
            *out_sel = g_snc ? 0 : 1;
            return kOnOff;
        case TunerItem::ChMute:
            *out_count = 3;
            if (g_ch_mute == FmTuner::ChannelMute::Left) {
                *out_sel = 1;
            } else if (g_ch_mute == FmTuner::ChannelMute::Right) {
                *out_sel = 2;
            } else {
                *out_sel = 0;
            }
            return kMute;
        default:
            *out_count = 0;
            *out_sel = 0;
            return nullptr;
    }
}

static int tunerItemCount() {
    return g_radio.isRda() ? static_cast<int>(RdaTunerItem::Count)
                           : static_cast<int>(TunerItem::Count);
}

static const char* rdaTunerItemLabel(const RdaTunerItem item) {
    switch (item) {
        case RdaTunerItem::Band:
            return "Band";
        case RdaTunerItem::Spacing:
            return "Step";
        case RdaTunerItem::Deemph:
            return "De-emphasis";
        case RdaTunerItem::SeekMode:
            return "Seek mode";
        case RdaTunerItem::SeekThreshold:
            return "Seek threshold";
        case RdaTunerItem::SeekWrap:
            return "Seek wrap";
        case RdaTunerItem::Volume:
            return "Volume";
        case RdaTunerItem::Bass:
            return "Bass boost";
        case RdaTunerItem::SoftMute:
            return "Soft mute";
        case RdaTunerItem::SoftBlend:
            return "Soft blend";
        case RdaTunerItem::Rds:
            return "Radio data";
        case RdaTunerItem::Rbds:
            return "US radio data";
        case RdaTunerItem::Afc:
            return "Auto freq";
        default:
            return "";
    }
}

static const char* const* rdaTunerItemOptions(const RdaTunerItem item, int* out_count,
                                              int* out_sel) {
    // 频段用范围比 EU/JP 缩写更直观
    static const char* kBand[] = {"87-108", "76-91", "76-108", "65-76", "50-76"};
    static const char* kSpacing[] = {"100 kHz", "200 kHz", "50 kHz", "25 kHz"};
    static const char* kDeemph[] = {"50 us", "75 us"};
    static const char* kSeek[] = {"Software", "Hardware"};
    static const char* kOnOff[] = {"On", "Off"};
    static const char* kLevel[] = {"0", "1", "2", "3", "4", "5", "6", "7",
                                  "8", "9", "10", "11", "12", "13", "14", "15"};
    switch (item) {
        case RdaTunerItem::Band:
            *out_count = 5;
            *out_sel = g_rda_band;
            return kBand;
        case RdaTunerItem::Spacing:
            *out_count = 4;
            *out_sel = g_rda_spacing;
            return kSpacing;
        case RdaTunerItem::Deemph:
            *out_count = 2;
            *out_sel = g_rda_deemph75 ? 1 : 0;
            return kDeemph;
        case RdaTunerItem::SeekMode:
            *out_count = 2;
            *out_sel = g_rda_hw_seek ? 1 : 0;
            return kSeek;
        case RdaTunerItem::SeekThreshold:
            *out_count = 16;
            *out_sel = g_rda_seek_threshold;
            return kLevel;
        case RdaTunerItem::SeekWrap:
            *out_count = 2;
            *out_sel = g_rda_wrap ? 0 : 1;
            return kOnOff;
        case RdaTunerItem::Volume:
            *out_count = 16;
            *out_sel = g_rda_volume;
            return kLevel;
        case RdaTunerItem::Bass:
            *out_count = 2;
            *out_sel = g_rda_bass ? 0 : 1;
            return kOnOff;
        case RdaTunerItem::SoftMute:
            *out_count = 2;
            *out_sel = g_rda_soft_mute ? 0 : 1;
            return kOnOff;
        case RdaTunerItem::SoftBlend:
            *out_count = 2;
            *out_sel = g_rda_soft_blend ? 0 : 1;
            return kOnOff;
        case RdaTunerItem::Rds:
            *out_count = 2;
            *out_sel = g_rda_rds ? 0 : 1;
            return kOnOff;
        case RdaTunerItem::Rbds:
            *out_count = 2;
            *out_sel = g_rda_rbds ? 0 : 1;
            return kOnOff;
        case RdaTunerItem::Afc:
            *out_count = 2;
            *out_sel = g_rda_afc ? 0 : 1;
            return kOnOff;
        default:
            *out_count = 0;
            *out_sel = 0;
            return nullptr;
    }
}

static void drawTunerSettings() {
    radioCanvas.fillSprite(BLACK);
    constexpr int title_y = APP_HELP_EDGE;
    radioCanvas.setTextSize(2);
    radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, title_y);
    radioCanvas.print("Tuner");

    radioCanvas.setTextSize(1);
    constexpr int row_h = 11;
    constexpr int list_y = title_y + RADIO_TITLE_H + RADIO_TITLE_GAP;
    constexpr int visible_rows = 9;
    const int n = tunerItemCount();
    if (g_tuner_sel < g_tuner_scroll) {
        g_tuner_scroll = g_tuner_sel;
    } else if (g_tuner_sel >= g_tuner_scroll + visible_rows) {
        g_tuner_scroll = g_tuner_sel - visible_rows + 1;
    }
    for (int row = 0; row < visible_rows; ++row) {
        const int i = g_tuner_scroll + row;
        if (i >= n) {
            break;
        }
        const int y = list_y + row * row_h;
        const bool focus = (i == g_tuner_sel);
        const char* label = g_radio.isRda()
                                ? rdaTunerItemLabel(static_cast<RdaTunerItem>(i))
                                : tunerItemLabel(static_cast<TunerItem>(i));

        // 焦点行：标签高亮；当前取值用成功色，其余选项灰显
        radioCanvas.setTextColor(focus ? APP_COLOR_LABEL : APP_COLOR_HINT, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, y);
        radioCanvas.print(label);

        int opt_count = 0;
        int opt_sel = 0;
        const char* const* opts =
            g_radio.isRda()
                ? rdaTunerItemOptions(static_cast<RdaTunerItem>(i), &opt_count, &opt_sel)
                : tunerItemOptions(static_cast<TunerItem>(i), &opt_count, &opt_sel);
        // 全称标签后接选项；选项多时只显示当前值，避免溢出
        int cx = RADIO_UI_LEFT + radioCanvas.textWidth(label) + 8;
        const bool value_only = opt_count > 4;
        const int first = value_only ? opt_sel : 0;
        const int last = value_only ? (opt_sel + 1) : opt_count;
        for (int o = first; o < last; ++o) {
            const bool on = (o == opt_sel);
            radioCanvas.setTextColor(on ? APP_COLOR_OK : APP_COLOR_MUTED, BLACK);
            radioCanvas.setCursor(cx, y);
            radioCanvas.print(opts[o]);
            cx += radioCanvas.textWidth(opts[o]) + 6;
        }
    }
    // 项数多于可见行时在右侧画细滚动条
    drawAppScrollbar(radioCanvas, list_y, visible_rows * row_h, n, visible_rows, g_tuner_scroll);
}

static void drawRdsInfo() {
    radioCanvas.fillSprite(BLACK);
    radioCanvas.setTextSize(2);
    radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, APP_HELP_EDGE);
    radioCanvas.print("RDS");

    const Rda5807m& rda = g_radio.rda();
    int y = APP_HELP_EDGE + RADIO_TITLE_H + RADIO_TITLE_GAP;
    radioCanvas.setTextSize(1);
    char line[48];
    snprintf(line, sizeof(line), "PS: %-8s  PI: %04X", rda.programService(),
             static_cast<unsigned>(rda.programId()));
    radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, y);
    radioCanvas.print(line);
    y += 11;

    snprintf(line, sizeof(line), "PTY:%u  TP:%s TA:%s  AF:%u",
             static_cast<unsigned>(rda.programType()), rda.trafficProgram() ? "Y" : "N",
             rda.trafficAnnouncement() ? "Y" : "N",
             static_cast<unsigned>(rda.alternativeFrequencyCount()));
    radioCanvas.setCursor(RADIO_UI_LEFT, y);
    radioCanvas.print(line);
    y += 13;

    radioCanvas.setTextColor(APP_COLOR_LABEL, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, y);
    radioCanvas.print("RadioText");
    y += 10;
    const char* rt = rda.radioText();
    for (int row = 0; row < 3 && rt != nullptr && *rt != '\0'; ++row) {
        char part[37];
        size_t n = strlen(rt);
        if (n > sizeof(part) - 1) {
            n = sizeof(part) - 1;
        }
        memcpy(part, rt, n);
        part[n] = '\0';
        radioCanvas.setTextColor(APP_COLOR_HINT, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, y);
        radioCanvas.print(part);
        rt += n;
        y += 10;
    }

    const Rda5807m::RdsClock& ct = rda.clockTime();
    if (ct.valid) {
        snprintf(line, sizeof(line), "UTC %04d-%02u-%02u %02u:%02u",
                 static_cast<int>(ct.year), static_cast<unsigned>(ct.month),
                 static_cast<unsigned>(ct.day), static_cast<unsigned>(ct.hour),
                 static_cast<unsigned>(ct.minute));
        radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
        radioCanvas.setCursor(RADIO_UI_LEFT, g_canvas_h - 18);
        radioCanvas.print(line);
    }
    radioCanvas.setTextColor(APP_COLOR_MUTED, BLACK);
    radioCanvas.setCursor(RADIO_UI_LEFT, g_canvas_h - 9);
    radioCanvas.print("i close  h help");
}

static void drawRadioChrome() {
    if (g_view == RadioView::Stations || g_view == RadioView::Rename) {
        drawStationsList();
        pushRadioFrame();
        return;
    }
    if (g_view == RadioView::Tuner) {
        drawTunerSettings();
        pushRadioFrame();
        return;
    }
    if (g_view == RadioView::Rds) {
        drawRdsInfo();
        pushRadioFrame();
        return;
    }
    // 搜台/扫频只局部推屏，避开电池
    if (g_seeking) {
        drawRadioMainPartial(RADIO_DIRTY_LIVE);
        return;
    }
    drawRadioMain();
}

static constexpr int RADIO_HELP_LINE_CAP = 32;
static AppHelpLine g_help_lines[RADIO_HELP_LINE_CAP];

static void addRadioHelpLine(AppHelpLine* lines, int& n, const AppHelpLine& line) {
    if (n < RADIO_HELP_LINE_CAP) {
        lines[n++] = line;
    }
}

// 全部 Help 行连成一条流，按 APP_HELP_MAX_LINES 切开；章节标题不单独占一页
static int fillRadioHelpLines(AppHelpLine* lines) {
    int n = 0;
    switch (g_help_kind) {
        case RadioHelpKind::Stations:
            addRadioHelpLine(lines, n, appHelpArrows("select station"));
            addRadioHelpLine(lines, n, appHelpBadge("[]", "page up / down"));
            addRadioHelpLine(lines, n, appHelpBadge("Ent", "tune + exit"));
            addRadioHelpLine(lines, n, appHelpKey('r', "rename"));
            addRadioHelpLine(lines, n, appHelpBadge("=", "save freq here"));
            addRadioHelpLine(lines, n, appHelpKey('n', "add current freq"));
            addRadioHelpLine(lines, n, appHelpBadge("d/Bk", "delete"));
            addRadioHelpLine(lines, n, appHelpKey('p', "pin to top"));
            addRadioHelpLine(lines, n, appHelpKey('c', "clear all stations"));
            addRadioHelpLine(lines, n, appHelpKey('l', "close list"));
            addRadioHelpLine(lines, n, appHelpText("1-0 jump hotkey slot"));
            break;
        case RadioHelpKind::Tuner:
            addRadioHelpLine(lines, n, appHelpArrows("or Tab: select"));
            addRadioHelpLine(lines, n, appHelpBadge("Ent/=", "cycle value"));
            addRadioHelpLine(lines, n, appHelpKey('-', "cycle prev"));
            addRadioHelpLine(lines, n, appHelpKey('t', "close tuner"));
            if (g_radio.isRda()) {
                addRadioHelpLine(lines, n, appHelpText("5 bands; step 25/50/100/200 kHz"));
                addRadioHelpLine(lines, n, appHelpText("Seek threshold 0-15 (higher=stricter)"));
                addRadioHelpLine(lines, n, appHelpTextColored("RDA audio / data", APP_COLOR_LABEL));
                addRadioHelpLine(lines, n, appHelpText("Bass boosts low frequencies"));
                addRadioHelpLine(lines, n, appHelpText("Soft blend mixes weak stereo to mono"));
                addRadioHelpLine(lines, n, appHelpText("Radio data Europe; US radio data NA"));
                addRadioHelpLine(lines, n, appHelpText("Auto freq normally stays enabled"));
            } else {
                addRadioHelpLine(lines, n, appHelpText("Band Europe 87.5-108 / Japan 76-91"));
                addRadioHelpLine(lines, n, appHelpText("Seek Software step or Hardware"));
                addRadioHelpLine(lines, n, appHelpText("Seek stop Low/Mid/High"));
            }
            break;
        case RadioHelpKind::Rds:
            addRadioHelpLine(lines, n, appHelpText("PS = station name; RT = radio text"));
            addRadioHelpLine(lines, n, appHelpText("PI/PTY identify program and type"));
            addRadioHelpLine(lines, n, appHelpText("TP/TA = traffic service / alert"));
            addRadioHelpLine(lines, n, appHelpText("AF = alternate-frequency count"));
            addRadioHelpLine(lines, n, appHelpText("CT = station UTC date and time"));
            addRadioHelpLine(lines, n, appHelpKey('i', "close RDS info"));
            break;
        case RadioHelpKind::Main:
            addRadioHelpLine(lines, n, appHelpBadge("< >", "seek; again to stop"));
            addRadioHelpLine(lines, n, appHelpBadge("^ v", "fine tune step"));
            addRadioHelpLine(lines, n, appHelpBadge("[]", "prev / next station"));
            addRadioHelpLine(lines, n,
                             appHelpBadge("-=", g_radio.isRda() ? "volume 0-15" : "tune (legacy)"));
            addRadioHelpLine(lines, n, appHelpKey('a', "auto scan + save"));
            addRadioHelpLine(lines, n, appHelpBadge("m o", "mute / mono"));
            addRadioHelpLine(lines, n, appHelpBadge("l t", "stations / tuner"));
            addRadioHelpLine(lines, n, appHelpTextColored("Badges", APP_COLOR_LABEL));
            addRadioHelpLine(lines, n, appHelpLabelText("ST", APP_COLOR_LABEL, " = stereo signal"));
            addRadioHelpLine(lines, n,
                             appHelpLabelText("MONO", APP_COLOR_LABEL, " = mono (o force)"));
            addRadioHelpLine(lines, n, appHelpLabelText("MUTE", APP_COLOR_LABEL, " = muted (m)"));
            if (g_radio.isRda()) {
                addRadioHelpLine(lines, n,
                                 appHelpLabelText("BAND", APP_COLOR_LABEL, " = selected FM range"));
            } else {
                addRadioHelpLine(lines, n,
                                 appHelpLabelText("ML/MR", APP_COLOR_LABEL, " = mute L / R"));
                addRadioHelpLine(lines, n,
                                 appHelpLabelText("JP", APP_COLOR_LABEL, " = Japan FM band"));
            }
            addRadioHelpLine(lines, n, appHelpLabelText("SEEK", APP_COLOR_LABEL, " = searching"));
            if (g_radio.isRda()) {
                addRadioHelpLine(lines, n, appHelpText("Yellow 5x3 grid = volume 0-15"));
                addRadioHelpLine(lines, n, appHelpKey('i', "RDS station info"));
            }
            addRadioHelpLine(lines, n, appHelpTextColored("Dial", APP_COLOR_LABEL));
            addRadioHelpLine(lines, n, appHelpLabelText("green", APP_COLOR_OK, " = saved station"));
            addRadioHelpLine(lines, n,
                             appHelpLabelText("cyan", APP_COLOR_LABEL, " = active station"));
            addRadioHelpLine(lines, n, appHelpText("Audio: module 3.5mm jack only"));
            addRadioHelpLine(lines, n, appHelpText("No BT/AirPods; cable is often antenna"));
            addRadioHelpLine(lines, n,
                             appHelpText(g_radio.isRda() ? "Native I2C 0x10/0x11"
                                                         : "No volume register"));
            addRadioHelpLine(lines, n, appHelpText("I2C Grove G2/G1 or EXT G8/G9"));
            addRadioHelpLine(lines, n, appHelpText("NO MOD = chip not found"));
            break;
        default:
            break;
    }
    return n;
}

static const char* radioHelpSubtitle() {
    switch (g_help_kind) {
        case RadioHelpKind::Stations:
            return "Stations";
        case RadioHelpKind::Tuner:
            return "Tuner";
        case RadioHelpKind::Rds:
            return "RDS";
        default:
            return "Radio";
    }
}

static int radioHelpPageCount() {
    return appHelpPageCount(fillRadioHelpLines(g_help_lines));
}

static void drawHelpPage() {
    const int n = fillRadioHelpLines(g_help_lines);
    drawAppHelpLines(radioHelpSubtitle(), g_help_lines, n, g_help_page);
}

static void markRssiFresh() {
    g_rssi_pending = false;
    g_rssi_kick_ms = millis();
}

static void applyFrequency(const uint16_t freq_centi) {
    g_freq = freq_centi;
    if (g_ready) {
        g_radio.setFrequency(g_freq);
        FmTuner::Status st{};
        if (g_radio.readStatus(st)) {
            g_rssi = st.rssi;
            g_stereo = st.stereo;
            g_if_counter = st.quality;
            g_chip_id = st.chip_id;
        }
        markRssiFresh(); // 刚写过 PLL，下一轮空闲刷新从现在起算
    }
}

static void tuneBySteps(const int steps) {
    if (steps == 0) {
        return;
    }
    int next = static_cast<int>(g_freq) + steps * static_cast<int>(radioFreqStep());
    if (next < static_cast<int>(radioFreqMin())) {
        next = static_cast<int>(radioFreqMin());
    }
    if (next > static_cast<int>(radioFreqMax())) {
        next = static_cast<int>(radioFreqMax());
    }
    applyFrequency(static_cast<uint16_t>(next));
    drawRadioChrome();
}

static uint16_t nextSeekFreq(const uint16_t freq, const bool up) {
    if (up) {
        if (freq >= radioFreqMax()) {
            return radioFreqMin();
        }
        return static_cast<uint16_t>(freq + radioFreqStep());
    }
    if (freq <= radioFreqMin()) {
        return radioFreqMax();
    }
    return static_cast<uint16_t>(freq - radioFreqStep());
}

static void seekTuneTo(const uint16_t freq) {
    g_freq = freq;
    if (g_ready) {
        g_radio.setFrequency(g_freq, false);
    }
    g_seek_settle_ms = millis();
    drawRadioMainPartial(RADIO_DIRTY_LIVE); // 频率/指针/SEEK
}

static void finishSeek(const bool refresh_status = true) {
    if (g_ready && g_radio.isSearching()) {
        g_radio.abortSearch();
    }
    g_seeking = false;
    g_auto_scanning = false;
    g_seek_hw = false;
    // 搜台结束恢复用户静音状态（搜台中强制静音避免扫空白杂音）
    if (g_ready) {
        g_radio.setMute(g_muted);
    }
    if (refresh_status && g_ready) {
        FmTuner::Status st{};
        if (g_radio.readStatus(st)) {
            g_rssi = st.rssi;
            g_stereo = st.stereo;
            g_if_counter = st.quality;
            g_chip_id = st.chip_id;
            if (st.freq_centi >= radioFreqMin() && st.freq_centi <= radioFreqMax()) {
                g_freq = st.freq_centi;
            }
        }
        markRssiFresh();
    }
    drawRadioChrome();
}

// 自动扫描收下当前频点；满上限返回 true
static bool acceptScanStation(const uint16_t freq) {
    if (g_scan_last_found != 0) {
        const int diff = static_cast<int>(freq) - static_cast<int>(g_scan_last_found);
        if (diff < static_cast<int>(radioFreqStep()) * 2) {
            return false;
        }
    }
    if (g_scan_found_count >= RADIO_STATION_MAX) {
        return true;
    }
    g_presets[g_scan_found_count].freq = freq;
    g_presets[g_scan_found_count].name[0] = '\0';
    g_scan_last_found = freq;
    g_scan_found_count++;
    g_station_count = g_scan_found_count; // 扫到即上刻度
    drawRadioMainPartial(RADIO_DIRTY_DIAL); // 只补刻度方块
    return g_scan_found_count >= RADIO_STATION_MAX;
}

static void persistScanStations() {
    g_station_count = g_scan_found_count;
    for (int i = g_station_count; i < RADIO_STATION_MAX; ++i) {
        g_presets[i].freq = 0;
        g_presets[i].name[0] = '\0';
    }
    saveAllStations();
}

static void completeAutoScan() {
    persistScanStations();
    if (g_scan_found_count > 0) {
        g_active_slot = 0;
        g_sel_slot = 0;
        g_list_scroll = 0;
        applyFrequency(g_presets[0].freq);
        finishSeek(false);
        return;
    }
    g_active_slot = -1;
    g_sel_slot = 0;
    g_list_scroll = 0;
    finishSeek(true);
}

// 与软件搜台同一套门限：RDA 另需芯片 FM_TRUE，避免噪点当台
static bool isAcceptableSeekStation(const FmTuner::Status& st) {
    if (st.rssi < seekRssiThreshold()) {
        return false;
    }
    return !g_radio.isRda() || st.valid_station;
}

// PLL 稳定后读 RSSI；够强则停（自动扫描则记台后继续）
static void onSeekSettled() {
    if (!g_ready) {
        finishSeek(false);
        return;
    }
    FmTuner::Status st{};
    const bool status_ok = g_radio.readStatus(st);
    if (status_ok) {
        g_rssi = st.rssi;
        g_stereo = st.stereo;
    }
    const bool station_found = status_ok && isAcceptableSeekStation(st);

    if (g_auto_scanning) {
        if (station_found) {
            if (acceptScanStation(g_freq)) {
                completeAutoScan();
                return;
            }
        }
        const uint16_t nxt = nextSeekFreq(g_freq, true);
        if (nxt == g_seek_origin) {
            completeAutoScan();
            return;
        }
        seekTuneTo(nxt);
        return;
    }

    if (station_found) {
        finishSeek(false);
        return;
    }
    const uint16_t nxt = nextSeekFreq(g_freq, g_seek_up);
    if (nxt == g_seek_origin) {
        finishSeek(true);
        return;
    }
    seekTuneTo(nxt);
}

static void startHwSearch(const bool up) {
    g_seek_hw = true;
    g_seek_up = up;
    g_seek_hw_start_ms = millis();
    g_seek_settle_ms = g_seek_hw_start_ms;
    if (g_ready) {
        g_radio.startSearch(up);
    }
    drawRadioMainPartial(RADIO_DIRTY_LIVE);
}

// 硬件停台不合格时：跳过当前频点继续 SM（与扫完一圈的终点判断一致）
static void continueHwSeekPastCurrent() {
    const bool up = g_auto_scanning || g_seek_up;
    const uint16_t nxt = nextSeekFreq(g_freq, up);
    if (g_auto_scanning) {
        if (nxt == g_seek_origin || nxt < g_freq) {
            completeAutoScan();
            return;
        }
        g_radio.setFrequency(nxt, g_radio.isRda());
        g_freq = nxt;
        startHwSearch(true);
        return;
    }
    if (nxt == g_seek_origin) {
        finishSeek(true);
        return;
    }
    g_radio.setFrequency(nxt, g_radio.isRda());
    g_freq = nxt;
    startHwSearch(up);
}

static void onHwSeekTick() {
    if (!g_ready) {
        finishSeek(false);
        return;
    }
    FmTuner::Status st{};
    if (!g_radio.readStatus(st)) {
        return;
    }
    uint8_t dirty = 0;
    if (st.freq_centi >= radioFreqMin() && st.freq_centi <= radioFreqMax() &&
        st.freq_centi != g_freq) {
        g_freq = st.freq_centi;
        dirty |= RADIO_DIRTY_DIAL | RADIO_DIRTY_FREQ;
    }
    if (st.rssi != g_rssi || st.stereo != g_stereo || st.quality != g_if_counter) {
        g_rssi = st.rssi;
        g_stereo = st.stereo;
        g_if_counter = st.quality;
        dirty |= RADIO_DIRTY_METER;
    }
    g_chip_id = st.chip_id;
    if (dirty != 0) {
        drawRadioMainPartial(dirty);
    }

    const uint32_t now = millis();
    if (now - g_seek_hw_start_ms < 80) {
        return; // 刚启动时 RF 可能仍是上一次调谐就绪
    }
    const bool timeout = (now - g_seek_hw_start_ms >= RADIO_HW_SEEK_TIMEOUT_MS);
    if (!st.ready && !st.band_limit && !timeout) {
        return;
    }

    g_radio.abortSearch();
    if (timeout) {
        finishSeek(true);
        return;
    }
    if (st.band_limit) {
        if (g_auto_scanning) {
            completeAutoScan();
            return;
        }
        if (!g_seek_wrapped) {
            g_seek_wrapped = true;
            g_freq = g_seek_up ? radioFreqMin() : radioFreqMax();
            g_radio.setFrequency(g_freq, g_radio.isRda());
            startHwSearch(g_seek_up);
            return;
        }
        finishSeek(true);
        return;
    }

    if (st.freq_centi >= radioFreqMin() && st.freq_centi <= radioFreqMax()) {
        g_freq = st.freq_centi;
    }
    // 清 SEEK 后再读一次：用 FM_TRUE + RSSI 过滤 SEEKTH 过松的假台
    if (g_radio.isRda()) {
        FmTuner::Status check{};
        if (g_radio.readStatus(check)) {
            st = check;
            g_rssi = st.rssi;
            g_stereo = st.stereo;
            g_if_counter = st.quality;
        }
        if (!isAcceptableSeekStation(st)) {
            continueHwSeekPastCurrent();
            return;
        }
    }
    if (g_auto_scanning) {
        if (g_scan_last_found != 0 && g_freq <= g_scan_last_found) {
            completeAutoScan(); // RDA 开启硬件回绕时，以频率回退判断扫完一圈。
            return;
        }
        if (acceptScanStation(g_freq)) {
            completeAutoScan();
            return;
        }
        const uint16_t nxt = nextSeekFreq(g_freq, true);
        if (nxt == g_seek_origin || nxt < g_freq) {
            completeAutoScan();
            return;
        }
        g_radio.setFrequency(nxt, g_radio.isRda());
        g_freq = nxt;
        startHwSearch(true);
        return;
    }
    finishSeek(true);
}

static void beginSeek(const bool up) {
    if (!g_ready || g_seeking) {
        return;
    }
    g_seeking = true;
    g_auto_scanning = false;
    g_seek_up = up;
    g_seek_origin = g_freq;
    g_seek_wrapped = false;
    g_seek_poll_ms = millis();
    g_radio.setMute(true); // 扫空白频段时静音，避免耳机里一片沙沙声
    if (useHardwareSeek()) {
        startHwSearch(up);
        return;
    }
    g_seek_hw = false;
    seekTuneTo(nextSeekFreq(g_freq, up));
}

// 搜台中途改方向：重置起点，硬件搜则重启 SM
static void redirectSeek(const bool up) {
    if (!g_ready || !g_seeking || g_auto_scanning) {
        return;
    }
    if (g_seek_up == up) {
        return;
    }
    g_seek_up = up;
    g_seek_origin = g_freq;
    g_seek_wrapped = false;
    g_seek_poll_ms = millis();
    if (g_seek_hw) {
        if (g_ready) {
            g_radio.abortSearch();
        }
        startHwSearch(up);
        return;
    }
    seekTuneTo(nextSeekFreq(g_freq, up));
}

static bool isTuneDirectionStillHeld(const int dir) {
    // Cardputer：;↑ 加频，.↓ 减频
    if (dir > 0) {
        return M5Cardputer.Keyboard.isKeyPressed(';');
    }
    if (dir < 0) {
        return M5Cardputer.Keyboard.isKeyPressed('.');
    }
    return false;
}

static bool isVolumeDirectionStillHeld(const int dir) {
    return dir < 0 ? M5Cardputer.Keyboard.isKeyPressed('-')
                   : (M5Cardputer.Keyboard.isKeyPressed('=') ||
                      M5Cardputer.Keyboard.isKeyPressed('+'));
}

static void adjustRdaVolume(const int dir) {
    if (!g_ready || !g_radio.isRda() || dir == 0) {
        return;
    }
    const int next = constrain(static_cast<int>(g_rda_volume) + (dir < 0 ? -1 : 1), 0, 15);
    if (next == g_rda_volume) {
        return;
    }
    g_rda_volume = static_cast<uint8_t>(next);
    g_radio.rda().setVolume(g_rda_volume);
    g_rda_volume_dirty = true;
    drawRadioChrome();
}

// 自动扫频：从频段底往上搜，结果写入列表（非阻塞，由 update 步进）
static void beginAutoScan() {
    if (!g_ready || g_seeking) {
        return;
    }
    clearAllStations();
    g_seeking = true;
    g_auto_scanning = true;
    g_seek_up = true;
    g_seek_origin = radioFreqMin();
    g_seek_wrapped = false;
    g_scan_found_count = 0;
    g_scan_last_found = 0;
    g_seek_poll_ms = millis();
    g_radio.setMute(true); // 自动扫台同样静音
    if (useHardwareSeek()) {
        g_freq = radioFreqMin();
        g_radio.setFrequency(g_freq, g_radio.isRda());
        startHwSearch(true);
        return;
    }
    g_seek_hw = false;
    seekTuneTo(radioFreqMin());
}

static void toggleMute() {
    if (!g_ready) {
        return;
    }
    g_muted = !g_muted;
    g_radio.setMute(g_muted);
    drawRadioChrome();
}

static void toggleMono() {
    if (!g_ready) {
        return;
    }
    g_mono = !g_mono;
    g_radio.setMono(g_mono);
    g_stereo = g_radio.isStereo();
    drawRadioChrome();
}

static int cycleValue(const int value, const int count, const int dir) {
    return (value + (dir < 0 ? -1 : 1) + count) % count;
}

static void cycleRdaTunerItem(const int dir) {
    const RdaTunerItem item = static_cast<RdaTunerItem>(g_tuner_sel);
    switch (item) {
        case RdaTunerItem::Band:
            g_rda_band = static_cast<uint8_t>(cycleValue(g_rda_band, 5, dir));
            break;
        case RdaTunerItem::Spacing:
            g_rda_spacing = static_cast<uint8_t>(cycleValue(g_rda_spacing, 4, dir));
            break;
        case RdaTunerItem::Deemph:
            g_rda_deemph75 = !g_rda_deemph75;
            break;
        case RdaTunerItem::SeekMode:
            g_rda_hw_seek = !g_rda_hw_seek;
            break;
        case RdaTunerItem::SeekThreshold:
            g_rda_seek_threshold =
                static_cast<uint8_t>(cycleValue(g_rda_seek_threshold, 16, dir));
            break;
        case RdaTunerItem::SeekWrap:
            g_rda_wrap = !g_rda_wrap;
            break;
        case RdaTunerItem::Volume:
            g_rda_volume = static_cast<uint8_t>(cycleValue(g_rda_volume, 16, dir));
            break;
        case RdaTunerItem::Bass:
            g_rda_bass = !g_rda_bass;
            break;
        case RdaTunerItem::SoftMute:
            g_rda_soft_mute = !g_rda_soft_mute;
            break;
        case RdaTunerItem::SoftBlend:
            g_rda_soft_blend = !g_rda_soft_blend;
            break;
        case RdaTunerItem::Rds:
            g_rda_rds = !g_rda_rds;
            break;
        case RdaTunerItem::Rbds:
            g_rda_rbds = !g_rda_rbds;
            break;
        case RdaTunerItem::Afc:
            g_rda_afc = !g_rda_afc;
            break;
        default:
            break;
    }
    applyTunerToChip();
    if (item == RdaTunerItem::Band || item == RdaTunerItem::Spacing) {
        clampFreqToBand();
        if (g_ready) {
            g_radio.setFrequency(g_freq, true);
        }
    }
    saveTunerSettings();
    drawRadioChrome();
}

static void cycleTunerItem(const int dir) {
    if (g_radio.isRda()) {
        cycleRdaTunerItem(dir);
        return;
    }
    const int d = (dir < 0) ? -1 : 1;
    switch (static_cast<TunerItem>(g_tuner_sel)) {
        case TunerItem::Band:
            g_japan = !g_japan;
            clampFreqToBand();
            break;
        case TunerItem::Deemph:
            g_deemph75 = !g_deemph75;
            break;
        case TunerItem::SeekMode:
            g_hw_seek_pref = !g_hw_seek_pref;
            break;
        case TunerItem::SeekStop: {
            int v = static_cast<int>(g_ssl) + d;
            if (v < 1) {
                v = 3;
            }
            if (v > 3) {
                v = 1;
            }
            g_ssl = static_cast<FmTuner::SeekStop>(v);
            break;
        }
        case TunerItem::Injection:
            g_hlsi_high = !g_hlsi_high;
            break;
        case TunerItem::SoftMute:
            g_soft_mute = !g_soft_mute;
            break;
        case TunerItem::HighCut:
            g_hcc = !g_hcc;
            break;
        case TunerItem::Snc:
            g_snc = !g_snc;
            break;
        case TunerItem::ChMute: {
            int v = static_cast<int>(g_ch_mute) + d;
            if (v < 0) {
                v = 2;
            }
            if (v > 2) {
                v = 0;
            }
            g_ch_mute = static_cast<FmTuner::ChannelMute>(v);
            break;
        }
        default:
            break;
    }
    applyTunerToChip();
    if (g_ready && (static_cast<TunerItem>(g_tuner_sel) == TunerItem::Band ||
                    static_cast<TunerItem>(g_tuner_sel) == TunerItem::Injection)) {
        g_radio.setFrequency(g_freq, true);
        FmTuner::Status st{};
        if (g_radio.readStatus(st)) {
            g_rssi = st.rssi;
            g_stereo = st.stereo;
            g_if_counter = st.quality;
            g_chip_id = st.chip_id;
        }
    }
    saveTunerSettings();
    drawRadioChrome();
}

static void openTunerSettings() {
    g_view = RadioView::Tuner;
    g_tuner_scroll = 0;
    if (g_ready) {
        FmTuner::Status st{};
        if (g_radio.readStatus(st)) {
            g_rssi = st.rssi;
            g_stereo = st.stereo;
            g_if_counter = st.quality;
            g_chip_id = st.chip_id;
        }
    }
    drawRadioChrome();
}

static void closeTunerSettingsToMain() {
    g_view = RadioView::Main;
    M5Cardputer.Display.fillScreen(BLACK);
    g_bat_drawn = false;
    drawRadioChrome();
}

static void openRdsInfo() {
    if (!g_ready || !g_radio.isRda()) {
        return;
    }
    g_view = RadioView::Rds;
    drawRadioChrome();
}

static void closeRdsInfoToMain() {
    g_view = RadioView::Main;
    M5Cardputer.Display.fillScreen(BLACK);
    g_bat_drawn = false;
    drawRadioChrome();
}

static bool handleTunerInput(const Keyboard_Class::KeysState& status, const String& key) {
    if (key == "t" || key == "T") {
        closeTunerSettingsToMain();
        return true;
    }
    // Tab / Shift+Tab 与方向键一样上下切项
    const int tab = getTabNavDelta(status);
    const int nav = (tab != 0) ? tab : getMenuNavDelta(status);
    if (nav != 0) {
        const int n = tunerItemCount();
        g_tuner_sel = (g_tuner_sel + nav + n) % n;
        drawRadioChrome();
        return true;
    }
    if (status.enter || status.space) {
        cycleTunerItem(1);
        return true;
    }
    for (const char c : status.word) {
        if (c == '-') {
            cycleTunerItem(-1);
            return true;
        }
        if (c == '=' || c == '+') {
            cycleTunerItem(1);
            return true;
        }
    }
    return true;
}

static void loadPresetSlot(const int idx) {
    if (idx < 0 || idx >= g_station_count || !isPresetValid(g_presets[idx])) {
        return;
    }
    g_active_slot = idx;
    g_sel_slot = idx;
    applyFrequency(g_presets[idx].freq);
    drawRadioChrome();
}

// [] 遍历全部已存电台；跳过空/无效
static void cycleSavedPreset(const int delta) {
    if (delta == 0 || g_station_count <= 0) {
        return;
    }
    const int span = g_station_count;
    int start = g_active_slot;
    if (start < 0 || start >= span) {
        for (int i = 0; i < span; ++i) {
            const int idx = (delta > 0) ? i : (span - 1 - i);
            if (isPresetValid(g_presets[idx])) {
                loadPresetSlot(idx);
                return;
            }
        }
        return;
    }
    for (int step = 1; step <= span; ++step) {
        const int idx = (start + delta * step + span * 2) % span;
        if (isPresetValid(g_presets[idx])) {
            loadPresetSlot(idx);
            return;
        }
    }
}

static int presetIndexFromKey(const char c) {
    if (c >= '1' && c <= '9') {
        return c - '1';
    }
    if (c == '0') {
        return 9;
    }
    return -1;
}

static void openStationsList() {
    g_view = RadioView::Stations;
    g_rename_buf[0] = '\0';
    if (g_active_slot >= 0 && g_active_slot < g_station_count) {
        g_sel_slot = g_active_slot;
    } else if (g_sel_slot >= g_station_count) {
        g_sel_slot = g_station_count > 0 ? g_station_count - 1 : 0;
    }
    ensureStationVisible();
    drawRadioChrome();
}

static void closeStationsListToMain() {
    g_view = RadioView::Main;
    g_rename_buf[0] = '\0';
    M5Cardputer.Display.fillScreen(BLACK);
    g_bat_drawn = false;
    drawRadioChrome();
}

static void beginRenamePreset() {
    if (g_sel_slot < 0 || g_sel_slot >= g_station_count) {
        return;
    }
    g_view = RadioView::Rename;
    if (g_presets[g_sel_slot].name[0] != '\0') {
        strncpy(g_rename_buf, g_presets[g_sel_slot].name, sizeof(g_rename_buf) - 1);
    } else {
        g_rename_buf[0] = '\0';
    }
    g_rename_buf[sizeof(g_rename_buf) - 1] = '\0';
    drawRadioChrome();
}

static void cancelRenamePreset() {
    g_view = RadioView::Stations;
    g_rename_buf[0] = '\0';
    drawRadioChrome();
}

static void commitRenamePreset() {
    if (g_sel_slot < 0 || g_sel_slot >= g_station_count) {
        cancelRenamePreset();
        return;
    }
    size_t start = 0;
    while (g_rename_buf[start] == ' ') {
        start++;
    }
    size_t end = strlen(g_rename_buf);
    while (end > start && g_rename_buf[end - 1] == ' ') {
        end--;
    }
    const size_t n = end - start;
    if (n == 0) {
        g_presets[g_sel_slot].name[0] = '\0';
    } else {
        const size_t copy_n = (n > static_cast<size_t>(RADIO_NAME_MAX)) ? RADIO_NAME_MAX : n;
        memcpy(g_presets[g_sel_slot].name, g_rename_buf + start, copy_n);
        g_presets[g_sel_slot].name[copy_n] = '\0';
    }
    saveAllStations();
    g_view = RadioView::Stations;
    g_rename_buf[0] = '\0';
    drawRadioChrome();
}

static void appendCurrentFreq() {
    if (g_station_count >= RADIO_STATION_MAX) {
        return;
    }
    g_presets[g_station_count].freq = g_freq;
    g_presets[g_station_count].name[0] = '\0';
    g_sel_slot = g_station_count;
    g_active_slot = g_station_count;
    g_station_count++;
    saveAllStations();
    ensureStationVisible();
    drawRadioChrome();
}

static void saveCurrentFreqToSelectedSlot() {
    if (g_station_count <= 0) {
        appendCurrentFreq();
        return;
    }
    if (g_sel_slot < 0 || g_sel_slot >= g_station_count) {
        return;
    }
    g_presets[g_sel_slot].freq = g_freq;
    g_active_slot = g_sel_slot;
    saveAllStations();
    drawRadioChrome();
}

static void deleteSelectedStation() {
    if (g_sel_slot < 0 || g_sel_slot >= g_station_count) {
        return;
    }
    const int del = g_sel_slot;
    if (del < g_station_count - 1) {
        memmove(&g_presets[del], &g_presets[del + 1],
                sizeof(RadioPreset) * static_cast<size_t>(g_station_count - del - 1));
    }
    g_station_count--;
    g_presets[g_station_count].freq = 0;
    g_presets[g_station_count].name[0] = '\0';
    if (g_active_slot == del) {
        g_active_slot = -1;
    } else if (g_active_slot > del) {
        g_active_slot--;
    }
    if (g_station_count <= 0) {
        g_sel_slot = 0;
        g_list_scroll = 0;
    } else if (g_sel_slot >= g_station_count) {
        g_sel_slot = g_station_count - 1;
    }
    saveAllStations();
    ensureStationVisible();
    drawRadioChrome();
}

// 选中项移到列表顶部（影响 1-0 快捷槽顺序）
static void pinSelectedStation() {
    if (g_sel_slot <= 0 || g_sel_slot >= g_station_count) {
        return;
    }
    const RadioPreset moved = g_presets[g_sel_slot];
    memmove(&g_presets[1], &g_presets[0], sizeof(RadioPreset) * static_cast<size_t>(g_sel_slot));
    g_presets[0] = moved;
    if (g_active_slot == g_sel_slot) {
        g_active_slot = 0;
    } else if (g_active_slot >= 0 && g_active_slot < g_sel_slot) {
        g_active_slot++;
    }
    g_sel_slot = 0;
    saveAllStations();
    ensureStationVisible();
    drawRadioChrome();
}

static void loadSelectedPresetAndExit() {
    if (g_sel_slot >= 0 && g_sel_slot < g_station_count && isPresetValid(g_presets[g_sel_slot])) {
        g_active_slot = g_sel_slot;
        applyFrequency(g_presets[g_sel_slot].freq);
    }
    closeStationsListToMain();
}

static bool handleRenameInput(const Keyboard_Class::KeysState& status) {
    if (status.del) {
        const size_t n = strlen(g_rename_buf);
        if (n > 0) {
            g_rename_buf[n - 1] = '\0';
            drawRadioChrome();
        }
        return true;
    }
    if (status.enter) {
        commitRenamePreset();
        return true;
    }
    if (status.space) {
        const size_t n = strlen(g_rename_buf);
        if (n < RADIO_NAME_MAX) {
            g_rename_buf[n] = ' ';
            g_rename_buf[n + 1] = '\0';
            drawRadioChrome();
        }
        return true;
    }
    for (const char c : status.word) {
        if (c == '\b') {
            const size_t n = strlen(g_rename_buf);
            if (n > 0) {
                g_rename_buf[n - 1] = '\0';
                drawRadioChrome();
            }
            return true;
        }
        if (c == 0x1B || c == '`') {
            cancelRenamePreset();
            return true;
        }
        if (c < 32 || c > 126) {
            continue;
        }
        const size_t n = strlen(g_rename_buf);
        if (n < RADIO_NAME_MAX) {
            g_rename_buf[n] = c;
            g_rename_buf[n + 1] = '\0';
            drawRadioChrome();
        }
        return true;
    }
    return true;
}

static bool handleStationsInput(const Keyboard_Class::KeysState& status, const String& key) {
    if (g_view == RadioView::Rename) {
        return handleRenameInput(status);
    }

    if (key == "l" || key == "L") {
        closeStationsListToMain();
        return true;
    }

    const int nav = getMenuNavDelta(status);
    if (nav != 0) {
        if (g_station_count > 0) {
            g_sel_slot = (g_sel_slot + nav + g_station_count) % g_station_count;
            ensureStationVisible();
            drawRadioChrome();
        }
        return true;
    }

    // [] 按可见行数翻页
    const int page_nav = getBracketNavDelta(status);
    if (page_nav != 0) {
        if (g_station_count > 0) {
            const int vis = stationsVisibleRows();
            const int max_scroll = g_station_count > vis ? (g_station_count - vis) : 0;
            g_list_scroll += page_nav * vis;
            if (g_list_scroll < 0) {
                g_list_scroll = 0;
            }
            if (g_list_scroll > max_scroll) {
                g_list_scroll = max_scroll;
            }
            if (g_sel_slot < g_list_scroll) {
                g_sel_slot = g_list_scroll;
            } else if (g_sel_slot >= g_list_scroll + vis) {
                g_sel_slot = g_list_scroll + vis - 1;
                if (g_sel_slot >= g_station_count) {
                    g_sel_slot = g_station_count - 1;
                }
            }
            drawRadioChrome();
        }
        return true;
    }

    if (status.enter || status.space) {
        loadSelectedPresetAndExit();
        return true;
    }

    if (status.del || key == "d" || key == "D") {
        deleteSelectedStation();
        return true;
    }

    if (key == "r" || key == "R") {
        beginRenamePreset();
        return true;
    }

    if (key == "p" || key == "P") {
        pinSelectedStation();
        return true;
    }

    if (key == "n" || key == "N") {
        appendCurrentFreq();
        return true;
    }

    if (key == "c" || key == "C") {
        clearSavedStations();
        drawRadioChrome();
        return true;
    }

    if (key == "=" || key == "+") {
        saveCurrentFreqToSelectedSlot();
        return true;
    }

    for (const char c : status.word) {
        const int idx = presetIndexFromKey(c);
        // 数字键只跳到快捷槽 1-10
        if (idx >= 0 && idx < g_station_count && idx < RADIO_HOTKEY_COUNT) {
            g_sel_slot = idx;
            ensureStationVisible();
            drawRadioChrome();
            return true;
        }
    }
    return true;
}

// Cardputer 方向键：;↑  ,←  .↓  /→（与 main/hid_keyboard 一致）
// 左右：前后搜台
static int getSeekDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x50 || hid == 0x36) {
            return -1; // Left / ,
        }
        if (hid == 0x4F || hid == 0x38) {
            return 1; // Right / /
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

// 上下：频率微调（↑ 加频，↓ 减频）
static int getTuneDelta(const Keyboard_Class::KeysState& status) {
    for (const uint8_t hid : status.hid_keys) {
        if (hid == 0x52 || hid == 0x33) {
            return 1; // Up / ; → 加频
        }
        if (hid == 0x51 || hid == 0x37) {
            return -1; // Down / . → 减频
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

static int getVolumeDelta(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '-') {
            return -1;
        }
        if (c == '=' || c == '+') {
            return 1;
        }
    }
    return 0;
}

static void restoreAfterHelp() {
    g_help_kind = RadioHelpKind::None;
    g_help_page = 0;
    M5Cardputer.Display.fillScreen(BLACK);
    g_bat_drawn = false;
    drawRadioChrome();
}

static void openRadioHelp(const RadioHelpKind kind) {
    g_help_kind = kind;
    g_help_page = 0;
    drawHelpPage();
}

} // namespace

void enterRadioApp() {
    leaveRadioApp();
    g_help_kind = RadioHelpKind::None;
    g_view = RadioView::Main;
    g_muted = false;
    g_mono = false;
    g_seeking = false;
    g_auto_scanning = false;
    g_seek_hw = false;
    g_freq = 9850;
    g_rssi = 0;
    g_if_counter = 0;
    g_chip_id = 0;
    g_stereo = false;
    g_sel_slot = 0;
    g_list_scroll = 0;
    g_tuner_sel = 0;
    g_active_slot = -1;
    g_rename_buf[0] = '\0';
    g_repeat_kind = RadioRepeatKind::None;
    g_tune_repeat_dir = 0;
    g_tune_repeat_since_ms = 0;
    g_tune_repeat_last_ms = 0;
    g_bat_shown_level = -1; // 进入时强制采一次电量
    g_bat_drawn = false;
    g_bat_sample_ms = 0;
    g_rssi_pending = false;
    g_rssi_kick_ms = 0;
    g_rds_poll_ms = 0;
    g_rda_volume_dirty = false;

    loadTunerSettings(); // 先读频段，再装电台
    loadRadioPresets();
    clampFreqToBand();
    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.powerSaveOff();
    M5Cardputer.Display.fillScreen(BLACK);

    // Ex_I2C 启动时可能尚未 init；找不到再试内部总线（EXT 排针 G8/G9）
    M5Cardputer.Ex_I2C.begin();
    g_ready = g_radio.begin(M5Cardputer.Ex_I2C);
    if (!g_ready) {
        g_ready = g_radio.begin(M5Cardputer.In_I2C);
    }

    if (g_ready) {
        applyTunerToChip();
        clampFreqToBand();
        g_radio.setFrequency(g_freq);
        FmTuner::Status st{};
        if (g_radio.readStatus(st)) {
            g_rssi = st.rssi;
            g_stereo = st.stereo;
            g_if_counter = st.quality;
            g_chip_id = st.chip_id;
        }
        markRssiFresh();
    }

    if (!ensureRadioCanvas()) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_ERROR, BLACK);
        M5Cardputer.Display.setCursor(RADIO_UI_LEFT, RADIO_UI_TOP);
        M5Cardputer.Display.print("Canvas OOM");
        return;
    }
    drawRadioChrome();
}

void silenceFmRadioOnBus(m5::I2C_Class& bus) {
    FmTuner::silenceIfPresent(bus);
}

void leaveRadioApp() {
    g_help_kind = RadioHelpKind::None;
    g_view = RadioView::Main;
    g_rename_buf[0] = '\0';
    g_seeking = false;
    g_auto_scanning = false;
    g_seek_hw = false;
    if (g_rda_volume_dirty) {
        saveTunerSettings();
        g_rda_volume_dirty = false;
    }
    if (g_ready) {
        g_radio.abortSearch();
        g_radio.setStandby(true);
    }
    g_ready = false;
    g_radio.detach();
    if (g_canvas_ok) {
        radioCanvas.deleteSprite();
        g_canvas_ok = false;
    }
}

bool isRadioHelpVisible() {
    return g_help_kind != RadioHelpKind::None;
}

void getRadioShotFeature(char* out, const size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    const char* feature = "main";
    if (g_help_kind != RadioHelpKind::None) {
        feature = "help";
    } else {
        switch (g_view) {
            case RadioView::Main:
                feature = "main";
                break;
            case RadioView::Stations:
                feature = "stations";
                break;
            case RadioView::Rename:
                feature = "rename";
                break;
            case RadioView::Tuner:
                feature = "tuner";
                break;
            case RadioView::Rds:
                feature = "rds";
                break;
        }
    }
    strncpy(out, feature, out_len - 1);
    out[out_len - 1] = '\0';
}

bool closeRadioHelp() {
    if (g_help_kind == RadioHelpKind::None) {
        return false;
    }
    restoreAfterHelp();
    return true;
}

bool closeRadioStations() {
    if (g_help_kind != RadioHelpKind::None) {
        return false; // Help 优先由 closeRadioHelp 处理
    }
    if (g_view == RadioView::Rename) {
        cancelRenamePreset();
        return true;
    }
    if (g_view == RadioView::Stations) {
        closeStationsListToMain();
        return true;
    }
    if (g_view == RadioView::Tuner) {
        closeTunerSettingsToMain();
        return true;
    }
    if (g_view == RadioView::Rds) {
        closeRdsInfoToMain();
        return true;
    }
    return false;
}

bool closeRadioSeek() {
    if (!g_seeking) {
        return false;
    }
    if (g_auto_scanning) {
        if (g_scan_found_count > 0) {
            persistScanStations();
        } else {
            loadRadioPresets(); // 一个都没扫到则恢复原预设
        }
    }
    finishSeek(true);
    return true;
}

void updateRadioApp() {
    if (g_help_kind != RadioHelpKind::None) {
        return;
    }

    const uint32_t now = millis();
    if (g_view == RadioView::Rds) {
        if (g_ready && g_radio.isRda() && g_rda_rds && now - g_rds_poll_ms >= 100) {
            g_rds_poll_ms = now;
            if (g_radio.rda().pollRds()) {
                drawRadioChrome();
            }
        }
        return;
    }
    if (g_view != RadioView::Main) {
        return;
    }
    if (g_seeking) {
        // 扫描中只刷新真实信号标识，不做动画轮转
        if (g_ready && now - g_seek_poll_ms >= 100) {
            g_seek_poll_ms = now;
            FmTuner::Status st{};
            if (g_radio.readStatus(st)) {
                if (st.rssi != g_rssi || st.stereo != g_stereo || st.quality != g_if_counter) {
                    g_rssi = st.rssi;
                    g_stereo = st.stereo;
                    g_if_counter = st.quality;
                    g_chip_id = st.chip_id;
                    drawRadioMainPartial(RADIO_DIRTY_METER); // 只刷新信号条/立体声
                }
            }
        }
        if (g_seek_hw) {
            onHwSeekTick();
        } else if (now - g_seek_settle_ms >=
                   (g_radio.isRda() ? RADIO_RDA_SEEK_SETTLE_MS : RADIO_SEEK_SETTLE_MS)) {
            onSeekSettled();
        }
        return;
    }

    if (g_tune_repeat_dir != 0) {
        const bool held = g_repeat_kind == RadioRepeatKind::Volume
                              ? isVolumeDirectionStillHeld(g_tune_repeat_dir)
                              : isTuneDirectionStillHeld(g_tune_repeat_dir);
        if (!held) {
            g_tune_repeat_dir = 0;
            g_repeat_kind = RadioRepeatKind::None;
            if (g_rda_volume_dirty) {
                saveTunerSettings();
                g_rda_volume_dirty = false;
            }
        } else if (now - g_tune_repeat_since_ms >= RADIO_TUNE_REPEAT_DELAY_MS &&
                   now - g_tune_repeat_last_ms >= RADIO_TUNE_REPEAT_RATE_MS) {
            g_tune_repeat_last_ms = now;
            if (g_repeat_kind == RadioRepeatKind::Volume) {
                adjustRdaVolume(g_tune_repeat_dir);
            } else {
                tuneBySteps(g_tune_repeat_dir);
            }
        }
    }

    if (!g_ready) {
        return;
    }

    if (g_radio.isRda() && g_rda_rds && now - g_rds_poll_ms >= 100) {
        g_rds_poll_ms = now;
        const char before[9] = {
            g_radio.rda().programService()[0], g_radio.rda().programService()[1],
            g_radio.rda().programService()[2], g_radio.rda().programService()[3],
            g_radio.rda().programService()[4], g_radio.rda().programService()[5],
            g_radio.rda().programService()[6], g_radio.rda().programService()[7], '\0'};
        if (g_radio.rda().pollRds() &&
            strncmp(before, g_radio.rda().programService(), sizeof(before) - 1) != 0) {
            drawRadioMainPartial(RADIO_DIRTY_FREQ);
        }
    }

    // 长按调谐时 PLL 在变，交给 applyFrequency，避免夹一次刷新。
    if (g_tune_repeat_dir != 0 && g_repeat_kind == RadioRepeatKind::Tune) {
        return;
    }

    // TEA5767 电平 ADC / 立体声只在写寄存器后更新；周期 kick 再读
    if (!g_rssi_pending) {
        if (now - g_rssi_kick_ms >= RADIO_RSSI_POLL_MS) {
            g_rssi_kick_ms = now;
            g_radio.refreshSignal();
            g_rssi_pending = true;
        }
    } else if (now - g_rssi_kick_ms >= RADIO_SEEK_SETTLE_MS) {
        g_rssi_pending = false;
        FmTuner::Status st{};
        if (g_radio.readStatus(st)) {
            if (st.rssi != g_rssi || st.stereo != g_stereo || st.quality != g_if_counter) {
                g_rssi = st.rssi;
                g_stereo = st.stereo;
                g_if_counter = st.quality;
                g_chip_id = st.chip_id;
                drawRadioMainPartial(RADIO_DIRTY_METER);
            }
        }
    }
}

void handleRadioApp(const Keyboard_Class::KeysState& status) {
    const String key = getPressedKey();

    // 重命名时 h 当作普通字符；其余界面 h 开/关对应 Help
    if (g_view != RadioView::Rename && (key == "h" || key == "H")) {
        if (g_help_kind != RadioHelpKind::None) {
            restoreAfterHelp();
        } else if (g_view == RadioView::Stations) {
            openRadioHelp(RadioHelpKind::Stations);
        } else if (g_view == RadioView::Tuner) {
            openRadioHelp(RadioHelpKind::Tuner);
        } else if (g_view == RadioView::Rds) {
            openRadioHelp(RadioHelpKind::Rds);
        } else {
            openRadioHelp(RadioHelpKind::Main);
        }
        return;
    }
    if (g_help_kind != RadioHelpKind::None) {
        const int delta = getHelpNavDelta(status);
        if (delta != 0) {
            g_help_page = applyHelpPageDelta(g_help_page, radioHelpPageCount(), delta);
            drawHelpPage();
        }
        return;
    }

    if (g_view == RadioView::Stations || g_view == RadioView::Rename) {
        (void)handleStationsInput(status, key);
        return;
    }
    if (g_view == RadioView::Tuner) {
        (void)handleTunerInput(status, key);
        return;
    }
    if (g_view == RadioView::Rds) {
        if (key == "i" || key == "I") {
            closeRdsInfoToMain();
        }
        return;
    }

    // 搜台中：微调/音量或 [] 停在当前频点；左右同向再按停止，反向改搜台方向。
    if (g_seeking) {
        if (getTuneDelta(status) != 0 || getVolumeDelta(status) != 0) {
            (void)closeRadioSeek();
            return;
        }
        const int preset_delta = getBracketNavDelta(status);
        if (preset_delta != 0) {
            (void)closeRadioSeek();
            cycleSavedPreset(preset_delta); // 停扫后立刻切到上/下一台
            return;
        }
        if (!g_auto_scanning) {
            const int seek_delta = getSeekDelta(status);
            if (seek_delta != 0) {
                const bool want_up = seek_delta > 0;
                if (g_seek_up == want_up) {
                    (void)closeRadioSeek(); // 同方向再按：停在当前频点
                } else {
                    redirectSeek(want_up);
                }
            }
        }
        return;
    }

    if (key == "l" || key == "L") {
        openStationsList();
        return;
    }
    if (key == "t" || key == "T") {
        openTunerSettings();
        return;
    }
    if (key == "i" || key == "I") {
        openRdsInfo();
        return;
    }
    if (key == "a" || key == "A") {
        beginAutoScan();
        return;
    }

    if (key == "m" || key == "M") {
        toggleMute();
        return;
    }
    if (key == "o" || key == "O") {
        toggleMono();
        return;
    }

    for (const char c : status.word) {
        const int idx = presetIndexFromKey(c);
        if (idx >= 0) {
            loadPresetSlot(idx);
            return;
        }
    }

    // []：切换已保存电台
    const int preset_delta = getBracketNavDelta(status);
    if (preset_delta != 0) {
        cycleSavedPreset(preset_delta);
        return;
    }

    if (status.fn) {
        return;
    }

    const int volume_delta = getVolumeDelta(status);
    if (volume_delta != 0) {
        if (g_radio.isRda()) {
            adjustRdaVolume(volume_delta);
            g_repeat_kind = RadioRepeatKind::Volume;
        } else {
            tuneBySteps(volume_delta); // TEA 保留旧版 -= 调频操作。
            g_repeat_kind = RadioRepeatKind::Tune;
        }
        g_tune_repeat_dir = static_cast<int8_t>(volume_delta);
        g_tune_repeat_since_ms = millis();
        g_tune_repeat_last_ms = g_tune_repeat_since_ms;
        return;
    }

    // ↑↓：频率微调
    const int tune_delta = getTuneDelta(status);
    if (tune_delta != 0) {
        tuneBySteps(tune_delta);
        g_repeat_kind = RadioRepeatKind::Tune;
        g_tune_repeat_dir = static_cast<int8_t>(tune_delta);
        g_tune_repeat_since_ms = millis();
        g_tune_repeat_last_ms = g_tune_repeat_since_ms;
        return;
    }
    g_tune_repeat_dir = 0;
    g_repeat_kind = RadioRepeatKind::None;

    if (!g_ready) {
        return;
    }

    // ←→：搜台
    const int seek_delta = getSeekDelta(status);
    if (seek_delta < 0) {
        beginSeek(false);
    } else if (seek_delta > 0) {
        beginSeek(true);
    }
}
