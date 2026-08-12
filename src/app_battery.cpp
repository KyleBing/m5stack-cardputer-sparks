#include "app_battery.h"

#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"

#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <esp_timer.h>
#include <time.h>

// ===== 小时采样日志（LittleFS 环形缓冲）=====

static constexpr const char* BAT_LOG_PATH = "/battery_log.bin";
static constexpr uint32_t BAT_LOG_MAGIC = 0x31424154; // BAT1
static constexpr int BAT_LOG_CAP = 168;               // 7 天 × 24h
static constexpr uint8_t BAT_FLAG_INTERP = 0x01;      // sleep 缺口线性补全
static constexpr uint8_t BAT_FLAG_SLEEP_LIGHT = 0x02;
static constexpr uint8_t BAT_FLAG_SLEEP_DEEP = 0x04;
static constexpr uint8_t BAT_FLAG_SLEEP_MASK = BAT_FLAG_SLEEP_LIGHT | BAT_FLAG_SLEEP_DEEP;
static constexpr uint16_t BAT_COLOR_LIGHT_SLEEP = APP_COLOR_WARN;
static constexpr uint16_t BAT_COLOR_DEEP_SLEEP = MAGENTA;

struct BatSample {
    uint32_t hour_epoch; // unix_sec / 3600
    uint16_t mv;
    uint8_t level; // 0–100
    uint8_t flags;
};

struct BatLogStore {
    uint32_t magic;
    uint16_t count;
    uint16_t next; // 下一写入下标
    BatSample samples[BAT_LOG_CAP];
};

static BatLogStore g_log{};
static bool g_log_loaded = false;
static uint32_t g_last_tick_ms = 0;

// 无墙钟时按开机 uptime 暂存；NTP/RTC 就绪后再回填 hour_epoch
static constexpr int BAT_PENDING_CAP = 48; // 约 2 天，深睡会丢 RAM
struct BatUptimeSample {
    uint32_t uptime_sec; // esp_timer 开机秒
    uint16_t mv;
    uint8_t level;
    uint8_t flags;
};
static BatUptimeSample g_pending[BAT_PENDING_CAP]{};
static int g_pending_count = 0;

// UI 缓存
static bool g_bat_ready = false;
static char g_last_bat[8] = "";
static char g_last_volt[12] = "";
static char g_last_curr[12] = "";
static char g_last_vbus[12] = "";
static bool g_show_detail = false; // curr/vbus 可读时才显示
static int g_chart_y = 0;
static int g_chart_h = 0;
static uint8_t g_chart_levels[24];
static uint8_t g_chart_flags[24];
static bool g_chart_valid[24];
static bool g_chart_has_clock = false;
static bool g_chart_has_data = false; // 墙钟日志或 uptime pending

// 后台 WiFi + NTP（不阻塞主循环 / 不挡电量显示）
enum class BatTimeSync : uint8_t {
    Idle = 0,
    BeginWifi,
    WaitWifi,
    BeginNtp,
    WaitNtp,
    Done,
};
static constexpr uint32_t BAT_WIFI_TIMEOUT_MS = 10000;
static constexpr uint32_t BAT_NTP_TIMEOUT_MS = 8000;
static BatTimeSync g_sync_state = BatTimeSync::Idle;
static uint32_t g_sync_deadline_ms = 0;
static uint32_t g_sync_header_ms = 0;

static constexpr int BAT_TEXT_SIZE = 2;
static constexpr int BAT_CHART_GAP = 1;
static constexpr uint16_t BAT_BAR_BORDER = DARKGREY;

// 时钟是否可用；必要时从硬件 RTC 拉系统时间
static bool batClockValid(time_t* out_now = nullptr) {
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
        return false;
    }
    if (out_now != nullptr) {
        *out_now = now;
    }
    return true;
}

static bool batSyncBusy() {
    return g_sync_state == BatTimeSync::BeginWifi || g_sync_state == BatTimeSync::WaitWifi ||
           g_sync_state == BatTimeSync::BeginNtp || g_sync_state == BatTimeSync::WaitNtp;
}

// 后台同步时钟；成功后由 update 刷新历史图
static void batUpdateTimeSync() {
    const AppConfig& cfg = getAppConfig();
    switch (g_sync_state) {
        case BatTimeSync::Idle:
        case BatTimeSync::Done:
            return;
        case BatTimeSync::BeginWifi: {
            if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
                g_sync_state = BatTimeSync::Done;
                return;
            }
            if (batClockValid()) {
                g_sync_state = BatTimeSync::Done;
                return;
            }
            if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid) {
                claimStaWifi();
                g_sync_state = BatTimeSync::BeginNtp;
                return;
            }
            claimStaWifi();
            WiFi.mode(WIFI_STA);
            applyWifiRadioSleepPolicy();
            WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
            g_sync_deadline_ms = millis() + BAT_WIFI_TIMEOUT_MS;
            g_sync_state = BatTimeSync::WaitWifi;
            break;
        }
        case BatTimeSync::WaitWifi:
            if (WiFi.status() == WL_CONNECTED) {
                g_sync_state = BatTimeSync::BeginNtp;
            } else if (static_cast<int32_t>(millis() - g_sync_deadline_ms) >= 0) {
                releaseConfigWifi();
                g_sync_state = BatTimeSync::Done;
            }
            break;
        case BatTimeSync::BeginNtp:
            configTzTime(getAppTimezone(), "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");
            g_sync_deadline_ms = millis() + BAT_NTP_TIMEOUT_MS;
            g_sync_state = BatTimeSync::WaitNtp;
            break;
        case BatTimeSync::WaitNtp: {
            struct tm timeinfo{};
            if (getLocalTime(&timeinfo, 0)) {
                if (M5.Rtc.isEnabled()) {
                    const time_t now = time(nullptr);
                    struct tm utc{};
                    gmtime_r(&now, &utc);
                    M5.Rtc.setDateTime(&utc);
                    M5.Rtc.setSystemTimeFromRtc();
                    applyLocalTimezone();
                }
                saveAppConfigTimezone(getAppTimezone());
                releaseConfigWifi();
                g_sync_state = BatTimeSync::Done;
            } else if (static_cast<int32_t>(millis() - g_sync_deadline_ms) >= 0) {
                releaseConfigWifi();
                g_sync_state = BatTimeSync::Done;
            }
            break;
        }
    }

    // 联网期间刷新 header WiFi 图标
    if (g_sync_state == BatTimeSync::WaitWifi || g_sync_state == BatTimeSync::WaitNtp) {
        if (millis() - g_sync_header_ms >= 500) {
            g_sync_header_ms = millis();
            updateAppHeaderStatus();
        }
    }
}

static uint32_t batHourEpoch(const time_t now) {
    return static_cast<uint32_t>(now / 3600);
}

static uint32_t batUptimeSec() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

static uint32_t batUptimeHour(const uint32_t uptime_sec) {
    return uptime_sec / 3600u;
}

static void batReadLive(uint8_t& level, uint16_t& mv) {
    int lv = M5Cardputer.Power.getBatteryLevel();
    if (lv < 0) {
        lv = 0;
    }
    if (lv > 100) {
        lv = 100;
    }
    level = static_cast<uint8_t>(lv);
    const int v = M5Cardputer.Power.getBatteryVoltage();
    mv = (v > 0) ? static_cast<uint16_t>(v) : 0;
}

static bool batSaveLog() {
    File f = LittleFS.open(BAT_LOG_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t n = f.write(reinterpret_cast<const uint8_t*>(&g_log), sizeof(g_log));
    f.close();
    return n == sizeof(g_log);
}

static void batResetLog() {
    memset(&g_log, 0, sizeof(g_log));
    g_log.magic = BAT_LOG_MAGIC;
}

static bool batLoadLog() {
    if (!LittleFS.exists(BAT_LOG_PATH)) {
        batResetLog();
        return true;
    }
    File f = LittleFS.open(BAT_LOG_PATH, "r");
    if (!f) {
        batResetLog();
        return false;
    }
    const size_t n = f.read(reinterpret_cast<uint8_t*>(&g_log), sizeof(g_log));
    f.close();
    if (n != sizeof(g_log) || g_log.magic != BAT_LOG_MAGIC || g_log.count > BAT_LOG_CAP ||
        g_log.next >= BAT_LOG_CAP) {
        batResetLog();
        return false;
    }
    return true;
}

// 取最近一条采样；无则 false
static bool batLastSample(BatSample& out) {
    if (g_log.count == 0) {
        return false;
    }
    const int idx = (g_log.next + BAT_LOG_CAP - 1) % BAT_LOG_CAP;
    out = g_log.samples[idx];
    return true;
}

// 写入或更新指定整点（同小时重复写入时：真实点可覆盖插值点）
static void batUpsertHour(const uint32_t hour, const uint8_t level, const uint16_t mv,
                          const uint8_t flags) {
    BatSample last{};
    if (batLastSample(last) && last.hour_epoch == hour) {
        const int idx = (g_log.next + BAT_LOG_CAP - 1) % BAT_LOG_CAP;
        // 已有真实采样则不让插值覆盖
        if ((g_log.samples[idx].flags & BAT_FLAG_INTERP) == 0 && (flags & BAT_FLAG_INTERP) != 0) {
            return;
        }
        g_log.samples[idx].level = level;
        g_log.samples[idx].mv = mv;
        g_log.samples[idx].flags = flags;
        return;
    }

    g_log.samples[g_log.next] = {hour, mv, level, flags};
    g_log.next = static_cast<uint16_t>((g_log.next + 1) % BAT_LOG_CAP);
    if (g_log.count < BAT_LOG_CAP) {
        g_log.count++;
    }
}

// 按小时查采样
static bool batFindHour(const uint32_t hour, BatSample& out) {
    for (uint16_t i = 0; i < g_log.count; i++) {
        const int idx = (g_log.next + BAT_LOG_CAP - g_log.count + i) % BAT_LOG_CAP;
        if (g_log.samples[idx].hour_epoch == hour) {
            out = g_log.samples[idx];
            return true;
        }
    }
    return false;
}

// sleep 缺口：在 last..now 之间线性插值补全（不含两端已有真实点时可覆盖）
static void batFillGap(const BatSample& before, const uint32_t now_hour, const uint8_t now_level,
                       const uint16_t now_mv) {
    if (now_hour <= before.hour_epoch) {
        return;
    }
    const uint32_t span = now_hour - before.hour_epoch;
    // 过大缺口（>7 天）只记当前点，避免无意义填充
    if (span > static_cast<uint32_t>(BAT_LOG_CAP)) {
        batUpsertHour(now_hour, now_level, now_mv, 0);
        return;
    }

    // 入睡前采样携带休眠类型，补出的每个小时继承该类型。
    const uint8_t sleep_flags = before.flags & BAT_FLAG_SLEEP_MASK;
    for (uint32_t h = before.hour_epoch + 1; h < now_hour; h++) {
        const float t = static_cast<float>(h - before.hour_epoch) / static_cast<float>(span);
        const int lv = static_cast<int>(before.level + (now_level - before.level) * t + 0.5f);
        const int mv =
            static_cast<int>(before.mv + static_cast<int>(now_mv - before.mv) * t + 0.5f);
        uint8_t level = static_cast<uint8_t>(lv < 0 ? 0 : (lv > 100 ? 100 : lv));
        batUpsertHour(h, level, static_cast<uint16_t>(mv < 0 ? 0 : mv),
                      BAT_FLAG_INTERP | sleep_flags);
    }
    batUpsertHour(now_hour, now_level, now_mv, 0);
}

// 无时钟：按 uptime 小时写入 RAM pending（同小时覆盖）
static void batRecordUptimePending(const uint8_t flags) {
    uint8_t level = 0;
    uint16_t mv = 0;
    batReadLive(level, mv);
    const uint32_t up = batUptimeSec();
    const uint32_t hour_u = batUptimeHour(up);

    if (g_pending_count > 0) {
        BatUptimeSample& last = g_pending[g_pending_count - 1];
        if (batUptimeHour(last.uptime_sec) == hour_u) {
            if ((last.flags & BAT_FLAG_INTERP) == 0 && (flags & BAT_FLAG_INTERP) != 0) {
                return;
            }
            last.uptime_sec = up;
            last.mv = mv;
            last.level = level;
            last.flags = flags;
            return;
        }
    }
    if (g_pending_count >= BAT_PENDING_CAP) {
        memmove(&g_pending[0], &g_pending[1], sizeof(g_pending[0]) * (BAT_PENDING_CAP - 1));
        g_pending_count = BAT_PENDING_CAP - 1;
    }
    g_pending[g_pending_count++] = {up, mv, level, flags};
}

// 时钟就绪：uptime → hour_epoch 后写入环形日志
static void batFlushPendingUptime() {
    time_t now = 0;
    if (g_pending_count == 0 || !batClockValid(&now)) {
        return;
    }
    const uint32_t now_up = batUptimeSec();
    BatSample prev{};
    bool has_prev = false;
    for (int i = 0; i < g_pending_count; i++) {
        const BatUptimeSample& p = g_pending[i];
        const uint32_t dt_sec = (now_up >= p.uptime_sec) ? (now_up - p.uptime_sec) : 0;
        const time_t sample_sec = now - static_cast<time_t>(dt_sec);
        if (sample_sec <= 1600000000) {
            continue;
        }
        const uint32_t hour = batHourEpoch(sample_sec);
        if (has_prev && hour > prev.hour_epoch + 1) {
            batFillGap(prev, hour, p.level, p.mv);
        } else {
            batUpsertHour(hour, p.level, p.mv, p.flags);
        }
        prev = {hour, p.mv, p.level, p.flags};
        has_prev = true;
    }
    g_pending_count = 0;
    batSaveLog();
}

static bool batFindPendingHour(const uint32_t hour_u, BatUptimeSample& out) {
    for (int i = 0; i < g_pending_count; i++) {
        if (batUptimeHour(g_pending[i].uptime_sec) == hour_u) {
            out = g_pending[i];
            return true;
        }
    }
    return false;
}

static void batRecordNow(const bool allow_gap_fill) {
    time_t now = 0;
    if (!batClockValid(&now)) {
        batRecordUptimePending(0);
        return;
    }
    if (g_pending_count > 0) {
        batFlushPendingUptime();
    }
    const uint32_t hour = batHourEpoch(now);
    uint8_t level = 0;
    uint16_t mv = 0;
    batReadLive(level, mv);

    BatSample last{};
    if (allow_gap_fill && batLastSample(last) && hour > last.hour_epoch + 1) {
        batFillGap(last, hour, level, mv);
    } else {
        batUpsertHour(hour, level, mv, 0);
    }
    batSaveLog();
}

void initBatteryLog() {
    batLoadLog();
    g_log_loaded = true;
    // 深睡重启：有时钟则补 sleep 缺口；无时钟则记一条 uptime pending
    batRecordNow(true);
}

void batteryLogTick() {
    if (!g_log_loaded) {
        return;
    }

    time_t now = 0;
    const bool clock_ok = batClockValid(&now);
    // 时钟刚可用：立刻回填 pending 并记当前点，不等整点
    if (clock_ok && g_pending_count > 0) {
        batRecordNow(true);
    }

    const uint32_t ms = millis();
    // 约每分钟检查一次是否跨整点 / uptime 小时
    if (g_last_tick_ms != 0 && (ms - g_last_tick_ms) < 60000) {
        return;
    }
    g_last_tick_ms = ms;

    if (!clock_ok) {
        const uint32_t hour_u = batUptimeHour(batUptimeSec());
        if (g_pending_count > 0 &&
            batUptimeHour(g_pending[g_pending_count - 1].uptime_sec) == hour_u) {
            return;
        }
        batRecordUptimePending(0);
        return;
    }
    const uint32_t hour = batHourEpoch(now);
    BatSample last{};
    if (batLastSample(last) && last.hour_epoch == hour) {
        return;
    }
    batRecordNow(true);
}

void batteryLogPrepareSleep(const BatterySleepMode mode) {
    if (!g_log_loaded) {
        return;
    }
    // 休眠类型写进最后一个采样，深睡重启后也能恢复并标记补全段。
    const uint8_t sleep_flag =
        mode == BatterySleepMode::Deep ? BAT_FLAG_SLEEP_DEEP : BAT_FLAG_SLEEP_LIGHT;
    time_t now = 0;
    if (batClockValid(&now)) {
        if (g_pending_count > 0) {
            batFlushPendingUptime();
        }
        uint8_t level = 0;
        uint16_t mv = 0;
        batReadLive(level, mv);
        batUpsertHour(batHourEpoch(now), level, mv, sleep_flag);
    } else {
        // 浅睡 RAM 还在；深睡 pending 会丢
        batRecordUptimePending(sleep_flag);
    }
    batSaveLog();
}

void batteryLogAfterWake() {
    if (!g_log_loaded) {
        return;
    }
    batRecordNow(true);
}

// ===== UI =====

static void batUpdateLine(const int y, const char* label, const char* value, char* cache,
                          const size_t cache_size) {
    if (strncmp(cache, value, cache_size) == 0 && cache[0] != '\0') {
        return;
    }
    M5Cardputer.Display.fillRect(APP_CONTENT_X, y, 220, INFO_LINE_H_2X, BLACK);
    drawInfoLineAt(APP_CONTENT_X, y, label, value, BAT_TEXT_SIZE);
    strncpy(cache, value, cache_size - 1);
    cache[cache_size - 1] = '\0';
}

static void batBarLayout(const int x, const int w, const int n, const int gap, const int i,
                         int& bar_x, int& bar_w) {
    if (gap > 0 && n > 1) {
        const int total_gap = gap * (n - 1);
        bar_w = (w - total_gap) / n;
        bar_x = x + i * (bar_w + gap);
        return;
    }
    bar_x = x + (i * w) / n;
    const int next_x = x + ((i + 1) * w) / n;
    bar_w = next_x - bar_x;
}

static void batBuildChartCache() {
    memset(g_chart_levels, 0, sizeof(g_chart_levels));
    memset(g_chart_flags, 0, sizeof(g_chart_flags));
    memset(g_chart_valid, 0, sizeof(g_chart_valid));
    g_chart_has_clock = false;
    g_chart_has_data = false;

    time_t now = 0;
    if (batClockValid(&now)) {
        g_chart_has_clock = true;
        const uint32_t end_hour = batHourEpoch(now);
        for (int i = 0; i < 24; i++) {
            const uint32_t h = end_hour - static_cast<uint32_t>(23 - i);
            BatSample s{};
            if (batFindHour(h, s)) {
                g_chart_levels[i] = s.level;
                g_chart_flags[i] = s.flags;
                g_chart_valid[i] = true;
            }
        }
        return;
    }

    // 无墙钟：用 uptime pending 画相对小时柱
    if (g_pending_count == 0) {
        return;
    }
    g_chart_has_data = true;
    const uint32_t end_hour = batUptimeHour(batUptimeSec());
    for (int i = 0; i < 24; i++) {
        const uint32_t rel = static_cast<uint32_t>(23 - i);
        if (end_hour < rel) {
            continue;
        }
        BatUptimeSample s{};
        if (batFindPendingHour(end_hour - rel, s)) {
            g_chart_levels[i] = s.level;
            g_chart_flags[i] = s.flags;
            g_chart_valid[i] = true;
        }
    }
}

static void batDrawChart(const int x, const int y, const int w, const int bar_h) {
    constexpr int n = 24;
    M5Cardputer.Display.fillRect(x, y, w, bar_h + 12, BLACK);

    if (!g_chart_has_clock && !g_chart_has_data) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x, y + 4);
        // 后台同步中：不挡上方电量行，只在图表区提示
        if (batSyncBusy()) {
            if (g_sync_state == BatTimeSync::WaitNtp || g_sync_state == BatTimeSync::BeginNtp) {
                M5Cardputer.Display.print("syncing ntp...");
            } else {
                M5Cardputer.Display.print("syncing wifi...");
            }
        } else {
            M5Cardputer.Display.print("need clock (Time sync)");
        }
        return;
    }

    for (int i = 0; i < n; i++) {
        int bar_x = 0;
        int bar_w = 0;
        batBarLayout(x, w, n, BAT_CHART_GAP, i, bar_x, bar_w);
        if (!g_chart_valid[i] || g_chart_levels[i] == 0) {
            // 无数据 / 0%：空框
            drawPercentBarV(bar_x, y, bar_w, bar_h, 0, APP_COLOR_WARN, BAT_BAR_BORDER);
            if (g_chart_valid[i] && g_chart_levels[i] == 0) {
                // 0% 画 1px 提示
                M5Cardputer.Display.drawFastHLine(bar_x + 1, y + bar_h - 1, max(0, bar_w - 2),
                                                 APP_COLOR_WARN);
            }
            continue;
        }
        const uint8_t flags = g_chart_flags[i];
        uint16_t color = CYAN;
        if (i == n - 1) {
            color = APP_COLOR_OK;
        } else if ((flags & BAT_FLAG_SLEEP_DEEP) != 0) {
            color = BAT_COLOR_DEEP_SLEEP;
        } else if ((flags & BAT_FLAG_SLEEP_LIGHT) != 0 ||
                   (flags & BAT_FLAG_INTERP) != 0) {
            // 旧版只有插值标志的记录继续按浅睡颜色显示。
            color = BAT_COLOR_LIGHT_SLEEP;
        }
        drawPercentBarV(bar_x, y, bar_w, bar_h, g_chart_levels[i], color, BAT_BAR_BORDER);
    }

    // 小时刻度：0 / 6 / 12 / 18 / 24（相对窗口）
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    const int label_y = y + bar_h + 2;
    const int marks[] = {0, 6, 12, 18, 23};
    const char* labels[] = {"-23", "-18", "-12", "-6", "now"};
    for (int m = 0; m < 5; m++) {
        int bar_x = 0;
        int bar_w = 0;
        batBarLayout(x, w, n, BAT_CHART_GAP, marks[m], bar_x, bar_w);
        // 末列 now 用绿色，与当前小时柱一致
        M5Cardputer.Display.setTextColor(m == 4 ? APP_COLOR_OK : APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(bar_x, label_y);
        M5Cardputer.Display.print(labels[m]);
    }
}

static void batDrawHints() {
    const int hint_y = M5Cardputer.Display.height() - 12;
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, M5Cardputer.Display.width() - APP_CONTENT_X * 2,
                                 12, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, hint_y);
    M5Cardputer.Display.print("1h log  ");
    M5Cardputer.Display.setTextColor(CYAN, BLACK);
    M5Cardputer.Display.print("live");
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(" ");
    M5Cardputer.Display.setTextColor(BAT_COLOR_LIGHT_SLEEP, BLACK);
    M5Cardputer.Display.print("light");
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(" ");
    M5Cardputer.Display.setTextColor(BAT_COLOR_DEEP_SLEEP, BLACK);
    M5Cardputer.Display.print("deep");
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(" ");
    // 绿色柱：此时此刻（当前小时）
    M5Cardputer.Display.setTextColor(APP_COLOR_OK, BLACK);
    M5Cardputer.Display.print("now");
}

static void batRedrawChart() {
    if (g_chart_h <= 0) {
        return;
    }
    batBuildChartCache();
    const int chart_w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
    batDrawChart(APP_CONTENT_X, g_chart_y, chart_w, g_chart_h);
    batDrawHints();
}

void enterBatteryApp() {
    g_bat_ready = false;
    g_last_bat[0] = '\0';
    g_last_volt[0] = '\0';
    g_last_curr[0] = '\0';
    g_last_vbus[0] = '\0';
    g_sync_state = BatTimeSync::Idle;
    g_sync_header_ms = 0;
    // 立刻记点（无时钟进 pending）；无时钟则后台 NTP
    batRecordNow(true);
    if (batClockValid()) {
        g_sync_state = BatTimeSync::Done;
    } else {
        g_sync_state = BatTimeSync::BeginWifi;
    }
    updateBatteryApp();
}

void updateBatteryApp() {
    const bool had_clock = g_chart_has_clock;
    const BatTimeSync prev_sync = g_sync_state;
    batUpdateTimeSync();

    char bat[8];
    char volt[12];
    char curr[12];
    char vbus[12];

    snprintf(bat, sizeof(bat), "%d%%", M5Cardputer.Power.getBatteryLevel());
    snprintf(volt, sizeof(volt), "%dmV", M5Cardputer.Power.getBatteryVoltage());

    const int16_t vbus_mv = M5Cardputer.Power.getVBUSVoltage();
    g_show_detail = vbus_mv >= 0;
    if (g_show_detail) {
        snprintf(curr, sizeof(curr), "%dmA", static_cast<int>(M5Cardputer.Power.getBatteryCurrent()));
        snprintf(vbus, sizeof(vbus), "%dmV", vbus_mv);
    }

    if (!g_bat_ready) {
        beginAppScreen("Battery");
        g_bat_ready = true;
        g_last_bat[0] = '\0';
        g_last_volt[0] = '\0';
        g_last_curr[0] = '\0';
        g_last_vbus[0] = '\0';

        int y = APP_CONTENT_INSET_Y;
        batUpdateLine(y, "bat", bat, g_last_bat, sizeof(g_last_bat));
        y += INFO_LINE_H_2X;
        batUpdateLine(y, "volt", volt, g_last_volt, sizeof(g_last_volt));
        y += INFO_LINE_H_2X;
        if (g_show_detail) {
            batUpdateLine(y, "curr", curr, g_last_curr, sizeof(g_last_curr));
            y += INFO_LINE_H_2X;
            batUpdateLine(y, "vbus", vbus, g_last_vbus, sizeof(g_last_vbus));
            y += INFO_LINE_H_2X;
        }

        // 图表区：留给底栏 tip
        const int tip_h = 12;
        const int avail = M5Cardputer.Display.height() - tip_h - y - 2;
        g_chart_y = y + 2;
        g_chart_h = avail > 28 ? avail - 12 : 28; // 预留刻度
        if (g_chart_h > 48) {
            g_chart_h = 48;
        }
        batRedrawChart();
        return;
    }

    int y = APP_CONTENT_INSET_Y;
    batUpdateLine(y, "bat", bat, g_last_bat, sizeof(g_last_bat));
    y += INFO_LINE_H_2X;
    batUpdateLine(y, "volt", volt, g_last_volt, sizeof(g_last_volt));
    y += INFO_LINE_H_2X;
    if (g_show_detail) {
        batUpdateLine(y, "curr", curr, g_last_curr, sizeof(g_last_curr));
        y += INFO_LINE_H_2X;
        batUpdateLine(y, "vbus", vbus, g_last_vbus, sizeof(g_last_vbus));
    }

    // 时钟刚就绪：回填 pending 并画历史；同步阶段变化时刷新占位文案
    batBuildChartCache();
    const bool clock_just_ready = !had_clock && g_chart_has_clock;
    if (clock_just_ready) {
        batRecordNow(true);
        batRedrawChart();
        return;
    }
    if (!g_chart_has_clock && !g_chart_has_data && prev_sync != g_sync_state) {
        batRedrawChart();
        return;
    }

    // 整点附近刷新图表
    static uint32_t last_chart_ms = 0;
    const uint32_t ms = millis();
    if (last_chart_ms == 0 || (ms - last_chart_ms) >= 60000) {
        last_chart_ms = ms;
        batRedrawChart();
    }
}

void handleBatteryApp(const Keyboard_Class::KeysState& status) {
    (void)status;
    // 预留：暂无按键；btngo 回菜单由 main 处理
}

bool batteryAppSyncBusy() {
    return batSyncBusy();
}
