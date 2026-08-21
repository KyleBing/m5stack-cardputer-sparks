#include "app_gps.h"

#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"

#include <FS.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// Unit GPS v1.1 / Unit GPS SMA：黄线 RX 接主机 TX(G2)，白线 TX 接主机 RX(G1)。
static constexpr int GPS_RX_PIN = 1;
static constexpr int GPS_TX_PIN = 2;
static constexpr uint32_t GPS_BAUD = 115200;
static constexpr uint32_t GPS_STALE_MS = 2500;
static constexpr uint32_t GPS_LOG_MS = 1000;
static constexpr int GPS_HISTORY_MAX = 12;
static constexpr int GPS_HISTORY_PAD = 2;
static constexpr int GPS_HISTORY_ROW_H = 8 + GPS_HISTORY_PAD * 2;
static constexpr int GPS_HISTORY_VISIBLE = 7;
static constexpr int GPS_TITLE_SIZE = 2;
static constexpr int GPS_SPEED_SIZE = 3;
static constexpr int GPS_SPEED_H = GPS_SPEED_SIZE * 8;
static constexpr int GPS_CONTENT_GAP = 10; // 内容距 header / 速度区上下间距
static constexpr int GPS_REC_ICON_SMALL = 12;
static constexpr uint32_t GPS_REC_BLINK_MS = 500;
static constexpr uint32_t GPS_INDEX_MAGIC = 0x34535047; // GPS4
static constexpr uint32_t GPS_RUN_MAGIC = 0x334E5552;   // RUN3
static constexpr uint32_t GPS_RUN_MAGIC_V2 = 0x324E5552; // RUN2
static constexpr size_t GPS_RUN_META_V2_SIZE = 56;
static constexpr const char* GPS_INDEX_PATH = "/gps_index.bin";
static constexpr const char* GPS_PREFS_NS = "gps";
static constexpr const char* GPS_PREFS_RATE_HZ = "rate_hz";
static constexpr int GPS_SAT_MAX = 64;
static constexpr int GPS_SKY_R = 46;
static constexpr uint32_t GPS_SAT_STALE_MS = 3500;

// AT6668 / PCAS02：间隔 ms；模块最高 10Hz，出厂默认 1Hz。
struct GpsRateOption {
    uint8_t hz;
    uint16_t interval_ms;
};
static constexpr GpsRateOption GPS_RATE_OPTIONS[] = {
    {1, 1000},
    {2, 500},
    {5, 200},
    {10, 100},
};
static constexpr int GPS_RATE_OPTION_COUNT =
    static_cast<int>(sizeof(GPS_RATE_OPTIONS) / sizeof(GPS_RATE_OPTIONS[0]));

enum class GpsPage : uint8_t {
    Live = 0,
    Speed,
    Satellites,
    SkyPlot,
    History,
    HistoryChart,
    Settings,
    Help,
};

enum class ChartMetric : uint8_t {
    Speed = 0,
    Altitude,
    Accel,
};

enum GnssMask : uint8_t {
    GNSS_GPS = 1 << 0,
    GNSS_BDS = 1 << 1,
    GNSS_GLO = 1 << 2,
    GNSS_GAL = 1 << 3,
    GNSS_QZSS = 1 << 4,
};

struct GnssCounts {
    uint8_t gps;
    uint8_t bds;
    uint8_t glo;
    uint8_t gal;
    uint8_t qzss;
};

struct GpsFix {
    double lat;
    double lon;
    float altitude_m;
    float speed_kmh;
    float course_deg;
    float hdop;
    float pdop;
    float vdop;
    uint8_t fix_quality;
    uint8_t fix_type;
    uint8_t satellites_used;
    GnssCounts visible;
    uint8_t systems_visible;
    uint8_t systems_used;
    uint32_t utc_date; // YYYYMMDD
    uint32_t utc_time; // HHMMSS
    uint32_t last_sentence_ms;
    uint32_t last_fix_ms;
    uint32_t checksum_ok;
    uint32_t checksum_bad;
    bool location_valid;
    bool speed_valid;
};

struct SpeedStats {
    uint32_t started_ms;
    uint32_t last_ms;
    uint32_t moving_ms;
    uint32_t stopped_ms;
    float fused_kmh;
    float gps_kmh;
    float max_kmh;
    float distance_m;
    float speed_sum;
    uint32_t speed_count;
    float max_accel_g;
    float max_brake_g;
    float ascent_m;
    float descent_m;
    float last_alt_m;
    bool alt_ready;
    float t_0_30;
    float t_0_50;
    float t_0_100;
    float t_100_0;
    uint32_t launch_ms;
    uint32_t brake_ms;
    bool launch_armed;
    bool launch_active;
    bool brake_active;
};

struct RunMeta {
    uint32_t magic;
    uint32_t id;
    uint32_t utc_date;
    uint32_t utc_time;
    uint32_t duration_ms;
    uint32_t moving_ms;
    uint32_t samples;
    float distance_m;
    float max_kmh;
    float avg_kmh;
    float t_0_50;
    float t_0_100;
    float t_100_0;
    uint8_t sats_used;
    uint8_t sats_vis;
    uint8_t reserved[2];
    // RUN3：测速峰值（旧 RUN2 文件无此段，读取时按头长度区分）
    float t_0_30;
    float max_accel_g;
    float max_brake_g;
};
static_assert(offsetof(RunMeta, t_0_30) == GPS_RUN_META_V2_SIZE,
              "RUN2 header size must match V3 field offset");

struct GpsIndex {
    uint32_t magic;
    uint32_t next_id;
    uint8_t count;
    uint8_t reserved[3];
    RunMeta runs[GPS_HISTORY_MAX];
};

struct RunSample {
    uint32_t elapsed_ms;
    int32_t lat_e7;
    int32_t lon_e7;
    int16_t speed_d10;
    int16_t gps_speed_d10;
    int16_t altitude_d10;
    int16_t accel_mg;
    uint16_t course_d10;
    uint8_t sats_used;
    uint8_t sats_vis;
    uint8_t hdop_d10;
    uint8_t reserved;
};

struct LiveUiCache {
    char fix[12];
    char speed[8];
    char lat[14];
    char lon[14];
    char alt[12];
    char hdop[8];
    char sat[12];
    char course[12];
    char utc[24];
    bool fix_ok;
    bool recording;
    bool rec_icon_on;
};

struct SatSky {
    uint16_t prn;
    uint8_t system;
    uint8_t elev;  // 0..90
    uint16_t azim; // 0..359
    uint8_t snr;   // 0..99, 0 = no signal
    uint32_t last_ms;
};

struct SatUiCache {
    char fix[12];
    uint8_t counts[5];
    char footer[40];
    bool fix_ok;
    bool recording;
    bool rec_icon_on;
    uint32_t sky_fp;
};

struct SpeedUiCache {
    char fix[12];
    char speed[8];
    char duration[16];
    char max_kmh[8];
    char dist[12];
    char avg[8];
    char t050[8];
    char t0100[8];
    char t1000[8];
    char g[16];
    bool fix_ok;
    bool recording;
    bool rec_icon_on;
};

static HardwareSerial g_gps_serial(1);
static GpsFix g_fix{};
static SpeedStats g_stats{};
static GpsIndex g_index{};
static File g_run_file;
static RunMeta g_run{};
static GpsPage g_page = GpsPage::Live;
static GpsPage g_page_before_help = GpsPage::Live;
static GpsPage g_page_before_settings = GpsPage::Live;
static ChartMetric g_chart_metric = ChartMetric::Speed;
static bool g_recording = false;
static bool g_history_loaded = false;
static bool g_imu_ok = false;
static int g_history_selected = 0;
static int g_help_page = 0;
static int g_rate_hz = 1;
static int g_rate_cursor = 0;
static uint32_t g_last_log_ms = 0;
static uint32_t g_last_imu_ms = 0;
static float g_linear_accel_ms2 = 0.0f;
static float g_gravity[3] = {0.0f, 0.0f, 1.0f};
static char g_nmea_line[160]{};
static size_t g_nmea_len = 0;
static LiveUiCache g_live_ui{};
static SatUiCache g_sat_ui{};
static SpeedUiCache g_speed_ui{};
static bool g_hist_rec_recording = false;
static bool g_hist_rec_icon_on = false;
static SatSky g_sats[GPS_SAT_MAX]{};
static uint8_t g_sat_count = 0;
static int g_chrome_title_size = GPS_TITLE_SIZE;

static bool gpsFixFresh() {
    return g_fix.location_valid && millis() - g_fix.last_fix_ms <= GPS_STALE_MS;
}

static uint8_t visibleTotal() {
    const int total = g_fix.visible.gps + g_fix.visible.bds + g_fix.visible.glo +
                      g_fix.visible.gal + g_fix.visible.qzss;
    return static_cast<uint8_t>(min(total, 255));
}

static const char* fixName() {
    if (!gpsFixFresh()) {
        return "NO FIX";
    }
    if (g_fix.fix_type >= 3) {
        return g_fix.fix_quality >= 2 ? "3D DGPS" : "3D FIX";
    }
    if (g_fix.fix_type == 2) {
        return "2D FIX";
    }
    return g_fix.fix_quality >= 2 ? "DGPS" : "FIX";
}

static const char* cardinal(const float degrees) {
    static const char* const names[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW",
    };
    int idx = static_cast<int>((degrees + 22.5f) / 45.0f) & 7;
    return names[idx];
}

static void runPath(const uint32_t id, char* out, const size_t size) {
    snprintf(out, size, "/gps_%08lu.bin", static_cast<unsigned long>(id));
}

static int rateOptionIndex(const int hz) {
    for (int i = 0; i < GPS_RATE_OPTION_COUNT; ++i) {
        if (GPS_RATE_OPTIONS[i].hz == hz) {
            return i;
        }
    }
    return 0;
}

static uint8_t nmeaChecksum(const char* body) {
    uint8_t cs = 0;
    for (const char* p = body; *p != '\0'; ++p) {
        cs ^= static_cast<uint8_t>(*p);
    }
    return cs;
}

static void loadGpsRatePrefs() {
    Preferences prefs;
    if (!prefs.begin(GPS_PREFS_NS, true)) {
        g_rate_hz = 1;
        g_rate_cursor = 0;
        return;
    }
    const int hz = prefs.getUChar(GPS_PREFS_RATE_HZ, 1);
    prefs.end();
    g_rate_hz = GPS_RATE_OPTIONS[rateOptionIndex(hz)].hz;
    g_rate_cursor = rateOptionIndex(g_rate_hz);
}

static void saveGpsRatePrefs() {
    Preferences prefs;
    if (!prefs.begin(GPS_PREFS_NS, false)) {
        return;
    }
    prefs.putUChar(GPS_PREFS_RATE_HZ, static_cast<uint8_t>(g_rate_hz));
    prefs.end();
}

static void applyGpsUpdateRate() {
    const GpsRateOption& opt = GPS_RATE_OPTIONS[rateOptionIndex(g_rate_hz)];
    char body[24];
    snprintf(body, sizeof(body), "PCAS02,%u", opt.interval_ms);
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "$%s*%02X\r\n", body, nmeaChecksum(body));
    g_gps_serial.print(cmd);
}

static void setGpsUpdateRate(const int hz) {
    g_rate_hz = GPS_RATE_OPTIONS[rateOptionIndex(hz)].hz;
    g_rate_cursor = rateOptionIndex(g_rate_hz);
    saveGpsRatePrefs();
    applyGpsUpdateRate();
}

static void resetIndex() {
    memset(&g_index, 0, sizeof(g_index));
    g_index.magic = GPS_INDEX_MAGIC;
    g_index.next_id = 1;
}

static bool saveIndex() {
    File f = LittleFS.open(GPS_INDEX_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(&g_index), sizeof(g_index));
    f.close();
    return written == sizeof(g_index);
}

static void loadIndex() {
    if (g_history_loaded) {
        return;
    }
    g_history_loaded = true;
    File f = LittleFS.open(GPS_INDEX_PATH, "r");
    if (!f) {
        resetIndex();
        return;
    }
    const size_t read = f.read(reinterpret_cast<uint8_t*>(&g_index), sizeof(g_index));
    f.close();
    if (read != sizeof(g_index) || g_index.magic != GPS_INDEX_MAGIC ||
        g_index.count > GPS_HISTORY_MAX || g_index.next_id == 0) {
        resetIndex();
    }
}

static void updateRunMeta() {
    g_run.duration_ms = millis() - g_stats.started_ms;
    g_run.moving_ms = g_stats.moving_ms;
    g_run.distance_m = g_stats.distance_m;
    g_run.max_kmh = g_stats.max_kmh;
    g_run.avg_kmh =
        g_stats.speed_count > 0 ? g_stats.speed_sum / static_cast<float>(g_stats.speed_count) : 0;
    g_run.t_0_30 = g_stats.t_0_30;
    g_run.t_0_50 = g_stats.t_0_50;
    g_run.t_0_100 = g_stats.t_0_100;
    g_run.t_100_0 = g_stats.t_100_0;
    g_run.max_accel_g = g_stats.max_accel_g;
    g_run.max_brake_g = g_stats.max_brake_g;
    g_run.sats_used = g_fix.satellites_used;
    g_run.sats_vis = visibleTotal();
}

static bool readRunMeta(File& f, RunMeta* out) {
    *out = {};
    uint32_t magic = 0;
    if (f.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) != sizeof(magic)) {
        return false;
    }
    f.seek(0);
    if (magic == GPS_RUN_MAGIC) {
        return f.read(reinterpret_cast<uint8_t*>(out), sizeof(*out)) == sizeof(*out);
    }
    if (magic == GPS_RUN_MAGIC_V2) {
        if (f.read(reinterpret_cast<uint8_t*>(out), GPS_RUN_META_V2_SIZE) != GPS_RUN_META_V2_SIZE) {
            return false;
        }
        out->t_0_30 = 0;
        out->max_accel_g = 0;
        out->max_brake_g = 0;
        return true;
    }
    return false;
}

static void checkpointRun() {
    if (!g_run_file) {
        return;
    }
    updateRunMeta();
    const size_t pos = g_run_file.position();
    g_run_file.seek(0);
    g_run_file.write(reinterpret_cast<const uint8_t*>(&g_run), sizeof(g_run));
    g_run_file.seek(pos);
    g_run_file.flush();
}

static void stopRecording() {
    if (!g_recording) {
        return;
    }
    checkpointRun();
    g_run_file.close();
    g_recording = false;

    loadIndex();
    if (g_index.count >= GPS_HISTORY_MAX) {
        char old_path[24];
        runPath(g_index.runs[GPS_HISTORY_MAX - 1].id, old_path, sizeof(old_path));
        LittleFS.remove(old_path);
        g_index.count = GPS_HISTORY_MAX - 1;
    }
    for (int i = g_index.count; i > 0; --i) {
        g_index.runs[i] = g_index.runs[i - 1];
    }
    g_index.runs[0] = g_run;
    g_index.count++;
    saveIndex();
    g_history_selected = 0;
}

static void resetSpeedStats() {
    g_stats = {};
    g_stats.started_ms = millis();
    g_stats.last_ms = g_stats.started_ms;
    g_stats.launch_armed = true;
    g_stats.fused_kmh = g_fix.speed_valid ? g_fix.speed_kmh : 0.0f;
    g_stats.gps_kmh = g_stats.fused_kmh;
}

static void startRecording() {
    if (g_recording) {
        return;
    }
    loadIndex();
    resetSpeedStats();
    g_run = {};
    g_run.magic = GPS_RUN_MAGIC;
    g_run.id = g_index.next_id++;
    g_run.utc_date = g_fix.utc_date;
    g_run.utc_time = g_fix.utc_time;
    saveIndex();

    char path[24];
    runPath(g_run.id, path, sizeof(path));
    g_run_file = LittleFS.open(path, "w+");
    if (!g_run_file) {
        return;
    }
    g_run_file.write(reinterpret_cast<const uint8_t*>(&g_run), sizeof(g_run));
    g_run_file.flush();
    g_recording = true;
    g_last_log_ms = 0;
}

static void toggleRecording() {
    if (g_recording) {
        stopRecording();
    } else {
        startRecording();
    }
}

static void deleteSelectedHistory() {
    loadIndex();
    if (g_index.count == 0) {
        return;
    }
    g_history_selected = constrain(g_history_selected, 0, g_index.count - 1);
    char path[24];
    runPath(g_index.runs[g_history_selected].id, path, sizeof(path));
    LittleFS.remove(path);
    for (int i = g_history_selected; i + 1 < g_index.count; ++i) {
        g_index.runs[i] = g_index.runs[i + 1];
    }
    g_index.count--;
    if (g_history_selected >= g_index.count && g_history_selected > 0) {
        g_history_selected--;
    }
    saveIndex();
}

static double parseCoordinate(const char* value, const char hemi) {
    if (value == nullptr || value[0] == '\0') {
        return 0.0;
    }
    const double raw = atof(value);
    const double degrees = floor(raw / 100.0);
    double result = degrees + (raw - degrees * 100.0) / 60.0;
    if (hemi == 'S' || hemi == 'W') {
        result = -result;
    }
    return result;
}

static uint32_t parseUtcTime(const char* s) {
    if (s == nullptr || strlen(s) < 6) {
        return 0;
    }
    return static_cast<uint32_t>((s[0] - '0') * 100000 + (s[1] - '0') * 10000 +
                                 (s[2] - '0') * 1000 + (s[3] - '0') * 100 +
                                 (s[4] - '0') * 10 + (s[5] - '0'));
}

static uint32_t parseUtcDate(const char* s) {
    if (s == nullptr || strlen(s) < 6) {
        return 0;
    }
    const int day = (s[0] - '0') * 10 + s[1] - '0';
    const int month = (s[2] - '0') * 10 + s[3] - '0';
    const int yy = (s[4] - '0') * 10 + s[5] - '0';
    return static_cast<uint32_t>((2000 + yy) * 10000 + month * 100 + day);
}

static uint8_t systemFromPrn(const int prn, const char talker0, const char talker1) {
    // 分系统 talker：按语句头判定（BD/GB 的 PRN 常为 1–37，与 GPS 重叠）
    if ((talker0 == 'B' && talker1 == 'D') || (talker0 == 'G' && talker1 == 'B')) {
        return GNSS_BDS;
    }
    if (talker0 == 'G' && talker1 == 'L') {
        return GNSS_GLO;
    }
    if (talker0 == 'G' && talker1 == 'A') {
        return GNSS_GAL;
    }
    if (talker0 == 'G' && talker1 == 'Q') {
        return GNSS_QZSS;
    }
    if (talker0 == 'G' && talker1 == 'P') {
        return (prn >= 193 && prn <= 199) ? GNSS_QZSS : GNSS_GPS;
    }
    // GNGSV / 未知 talker：按 NMEA ID 分段
    if (prn >= 65 && prn <= 96) {
        return GNSS_GLO;
    }
    if (prn >= 193 && prn <= 199) {
        return GNSS_QZSS;
    }
    if ((prn >= 201 && prn <= 237) || (prn >= 401 && prn <= 437)) {
        return GNSS_BDS;
    }
    if (prn >= 301 && prn <= 336) {
        return GNSS_GAL;
    }
    return GNSS_GPS;
}

static uint8_t* countForSystem(const uint8_t system) {
    if (system == GNSS_BDS) {
        return &g_fix.visible.bds;
    }
    if (system == GNSS_GLO) {
        return &g_fix.visible.glo;
    }
    if (system == GNSS_GAL) {
        return &g_fix.visible.gal;
    }
    if (system == GNSS_QZSS) {
        return &g_fix.visible.qzss;
    }
    return &g_fix.visible.gps;
}

static uint16_t satSystemColor(const uint8_t system) {
    if (system == GNSS_BDS) {
        return RED;
    }
    if (system == GNSS_GLO) {
        return ORANGE;
    }
    if (system == GNSS_GAL) {
        return GREEN;
    }
    if (system == GNSS_QZSS) {
        return MAGENTA;
    }
    return CYAN;
}

static char satSystemLetter(const uint8_t system) {
    if (system == GNSS_BDS) {
        return 'C';
    }
    if (system == GNSS_GLO) {
        return 'R';
    }
    if (system == GNSS_GAL) {
        return 'E';
    }
    if (system == GNSS_QZSS) {
        return 'J';
    }
    return 'G';
}

static int satDisplayId(const SatSky& sat) {
    if (sat.system == GNSS_GLO && sat.prn >= 65 && sat.prn <= 96) {
        return sat.prn - 64;
    }
    if (sat.system == GNSS_QZSS && sat.prn >= 193) {
        return sat.prn - 192;
    }
    if (sat.system == GNSS_BDS) {
        if (sat.prn >= 401) {
            return sat.prn - 400;
        }
        if (sat.prn >= 201) {
            return sat.prn - 200;
        }
    }
    if (sat.system == GNSS_GAL && sat.prn >= 301) {
        return sat.prn - 300;
    }
    return sat.prn;
}

static void formatSatName(const SatSky& sat, char* out, const size_t size) {
    snprintf(out, size, "%c%02d", satSystemLetter(sat.system), satDisplayId(sat));
}

static void recountVisibleFromSats() {
    g_fix.visible = {};
    g_fix.systems_visible = 0;
    for (uint8_t i = 0; i < g_sat_count; ++i) {
        uint8_t* value = countForSystem(g_sats[i].system);
        if (*value < 255) {
            (*value)++;
        }
        g_fix.systems_visible |= g_sats[i].system;
    }
}

static void upsertSat(const uint8_t system, const int prn, const int elev, const int azim,
                      const int snr) {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < g_sat_count; ++i) {
        if (g_sats[i].prn == static_cast<uint16_t>(prn) && g_sats[i].system == system) {
            g_sats[i].elev = static_cast<uint8_t>(constrain(elev, 0, 90));
            g_sats[i].azim = static_cast<uint16_t>(constrain(azim, 0, 359));
            g_sats[i].snr = static_cast<uint8_t>(constrain(snr, 0, 99));
            g_sats[i].last_ms = now;
            return;
        }
    }
    if (g_sat_count >= GPS_SAT_MAX) {
        return;
    }
    SatSky& sat = g_sats[g_sat_count++];
    sat.prn = static_cast<uint16_t>(prn);
    sat.system = system;
    sat.elev = static_cast<uint8_t>(constrain(elev, 0, 90));
    sat.azim = static_cast<uint16_t>(constrain(azim, 0, 359));
    sat.snr = static_cast<uint8_t>(constrain(snr, 0, 99));
    sat.last_ms = now;
}

// 多频点 GSV（B1I/B1C…）各自 message=1 时若清库，会只剩最后一组频点 → 北斗显得很少
static void pruneStaleSats() {
    const uint32_t now = millis();
    uint8_t w = 0;
    for (uint8_t i = 0; i < g_sat_count; ++i) {
        if (now - g_sats[i].last_ms <= GPS_SAT_STALE_MS) {
            g_sats[w++] = g_sats[i];
        }
    }
    g_sat_count = w;
}

static void parseGsv(char** fields, const int count, const char t0, const char t1) {
    if (count < 4) {
        return;
    }
    for (int i = 4; i + 3 < count; i += 4) {
        const int prn = atoi(fields[i]);
        if (prn <= 0) {
            continue;
        }
        const int elev = fields[i + 1][0] != '\0' ? atoi(fields[i + 1]) : 0;
        const int azim = fields[i + 2][0] != '\0' ? atoi(fields[i + 2]) : 0;
        const int snr = fields[i + 3][0] != '\0' ? atoi(fields[i + 3]) : 0;
        upsertSat(systemFromPrn(prn, t0, t1), prn, elev, azim, snr);
    }
    pruneStaleSats();
    recountVisibleFromSats();
}

static uint32_t satsSkyFingerprint() {
    uint32_t h = static_cast<uint32_t>(g_sat_count) * 1315423911u;
    for (uint8_t i = 0; i < g_sat_count; ++i) {
        const SatSky& s = g_sats[i];
        h = (h ^ s.prn ^ (static_cast<uint32_t>(s.elev) << 8) ^
             (static_cast<uint32_t>(s.azim) << 16) ^ (static_cast<uint32_t>(s.snr) << 24)) *
            16777619u;
    }
    return h;
}

static void parseNmeaLine(char* line) {
    if (line[0] != '$') {
        return;
    }
    char* star = strchr(line, '*');
    if (star == nullptr || strlen(star) < 3) {
        return;
    }
    uint8_t checksum = 0;
    for (char* p = line + 1; p < star; ++p) {
        checksum ^= static_cast<uint8_t>(*p);
    }
    const unsigned expected = strtoul(star + 1, nullptr, 16);
    if (checksum != static_cast<uint8_t>(expected)) {
        g_fix.checksum_bad++;
        return;
    }
    g_fix.checksum_ok++;
    g_fix.last_sentence_ms = millis();
    *star = '\0';

    char* fields[32]{};
    int count = 0;
    char* p = line + 1;
    while (count < 32) {
        fields[count++] = p;
        char* comma = strchr(p, ',');
        if (comma == nullptr) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }
    if (count == 0 || strlen(fields[0]) < 5) {
        return;
    }
    const char t0 = fields[0][0];
    const char t1 = fields[0][1];
    const char* type = fields[0] + 2;

    if (strcmp(type, "RMC") == 0 && count >= 10) {
        g_fix.utc_time = parseUtcTime(fields[1]);
        g_fix.utc_date = parseUtcDate(fields[9]);
        const bool valid = fields[2][0] == 'A';
        if (valid && fields[3][0] != '\0' && fields[5][0] != '\0') {
            g_fix.lat = parseCoordinate(fields[3], fields[4][0]);
            g_fix.lon = parseCoordinate(fields[5], fields[6][0]);
            g_fix.speed_kmh = static_cast<float>(atof(fields[7]) * 1.852);
            g_fix.course_deg = static_cast<float>(atof(fields[8]));
            g_fix.location_valid = true;
            g_fix.speed_valid = true;
            g_fix.last_fix_ms = millis();
        } else {
            g_fix.speed_valid = false;
        }
    } else if (strcmp(type, "GGA") == 0 && count >= 10) {
        g_fix.utc_time = parseUtcTime(fields[1]);
        g_fix.fix_quality = static_cast<uint8_t>(atoi(fields[6]));
        g_fix.satellites_used = static_cast<uint8_t>(atoi(fields[7]));
        g_fix.hdop = static_cast<float>(atof(fields[8]));
        g_fix.altitude_m = static_cast<float>(atof(fields[9]));
        if (g_fix.fix_quality > 0 && fields[2][0] != '\0' && fields[4][0] != '\0') {
            g_fix.lat = parseCoordinate(fields[2], fields[3][0]);
            g_fix.lon = parseCoordinate(fields[4], fields[5][0]);
            g_fix.location_valid = true;
            g_fix.last_fix_ms = millis();
        }
    } else if (strcmp(type, "GSA") == 0 && count >= 18) {
        g_fix.fix_type = static_cast<uint8_t>(atoi(fields[2]));
        g_fix.pdop = static_cast<float>(atof(fields[count - 3]));
        g_fix.hdop = static_cast<float>(atof(fields[count - 2]));
        g_fix.vdop = static_cast<float>(atof(fields[count - 1]));
        const uint8_t sys = systemFromPrn(atoi(fields[3]), t0, t1);
        if (g_fix.fix_type >= 2) {
            g_fix.systems_used |= sys;
        }
    } else if (strcmp(type, "GSV") == 0) {
        parseGsv(fields, count, t0, t1);
    } else if (strcmp(type, "VTG") == 0 && count >= 8 && fields[7][0] != '\0') {
        g_fix.course_deg = static_cast<float>(atof(fields[1]));
        g_fix.speed_kmh = static_cast<float>(atof(fields[7]));
        g_fix.speed_valid = true;
    }
}

static void pollSerial() {
    while (g_gps_serial.available() > 0) {
        const char c = static_cast<char>(g_gps_serial.read());
        if (c == '\n') {
            g_nmea_line[g_nmea_len] = '\0';
            if (g_nmea_len > 6) {
                parseNmeaLine(g_nmea_line);
            }
            g_nmea_len = 0;
        } else if (c != '\r') {
            if (g_nmea_len + 1 < sizeof(g_nmea_line)) {
                g_nmea_line[g_nmea_len++] = c;
            } else {
                g_nmea_len = 0;
            }
        }
    }
}

static void updateImu() {
    const uint32_t now = millis();
    if (!g_imu_ok || now - g_last_imu_ms < 20) {
        return;
    }
    const float dt = g_last_imu_ms == 0 ? 0.02f : min((now - g_last_imu_ms) / 1000.0f, 0.1f);
    g_last_imu_ms = now;
    M5.Imu.update();
    float ax = 0;
    float ay = 0;
    float az = 0;
    M5.Imu.getAccel(&ax, &ay, &az);

    // 静止/匀速时慢速跟踪重力；动态分量用于短时速度预测和异常值抑制。
    const float gravity_alpha = g_stats.fused_kmh < 3.0f ? 0.035f : 0.004f;
    g_gravity[0] += (ax - g_gravity[0]) * gravity_alpha;
    g_gravity[1] += (ay - g_gravity[1]) * gravity_alpha;
    g_gravity[2] += (az - g_gravity[2]) * gravity_alpha;
    const float dx = ax - g_gravity[0];
    const float dy = ay - g_gravity[1];
    const float dz = az - g_gravity[2];
    const float dynamic_g = sqrtf(dx * dx + dy * dy + dz * dz);
    g_linear_accel_ms2 += (dynamic_g * 9.80665f - g_linear_accel_ms2) * min(1.0f, dt * 8.0f);
}

static void updateSpeedStats() {
    const uint32_t now = millis();
    if (g_stats.last_ms == 0) {
        g_stats.last_ms = now;
        return;
    }
    const float dt = min((now - g_stats.last_ms) / 1000.0f, 0.25f);
    if (dt <= 0.0f) {
        return;
    }
    g_stats.last_ms = now;
    const bool gps_ok = g_fix.speed_valid && millis() - g_fix.last_sentence_ms <= GPS_STALE_MS;
    const float previous = g_stats.fused_kmh;
    const float gps_speed = gps_ok ? max(0.0f, g_fix.speed_kmh) : g_stats.gps_kmh;
    const float gps_delta = gps_speed - g_stats.gps_kmh;
    g_stats.gps_kmh = gps_speed;

    // AT6668 多普勒速度是长期基准；IMU 仅预测短时变化，避免姿态未知造成积分漂移。
    float predicted = previous;
    if (gps_ok && fabsf(gps_delta) > 0.05f) {
        const float sign = gps_delta >= 0.0f ? 1.0f : -1.0f;
        predicted += sign * g_linear_accel_ms2 * dt * 3.6f;
    }
    const float correction = gps_ok ? (g_linear_accel_ms2 > 1.5f ? 0.20f : 0.38f) : 0.0f;
    g_stats.fused_kmh = max(0.0f, predicted + (gps_speed - predicted) * correction);
    if (!gps_ok) {
        g_stats.fused_kmh *= powf(0.985f, dt * 10.0f);
    }
    if (gps_ok && gps_speed < 0.8f && g_linear_accel_ms2 < 0.25f) {
        g_stats.fused_kmh = 0.0f;
    }

    const float accel_g = ((g_stats.fused_kmh - previous) / 3.6f) / dt / 9.80665f;
    g_stats.max_accel_g = max(g_stats.max_accel_g, accel_g);
    g_stats.max_brake_g = min(g_stats.max_brake_g, accel_g);
    g_stats.max_kmh = max(g_stats.max_kmh, g_stats.fused_kmh);
    g_stats.distance_m += (previous + g_stats.fused_kmh) * 0.5f / 3.6f * dt;
    g_stats.speed_sum += g_stats.fused_kmh;
    g_stats.speed_count++;
    if (g_stats.fused_kmh >= 1.0f) {
        g_stats.moving_ms += static_cast<uint32_t>(dt * 1000.0f);
    } else {
        g_stats.stopped_ms += static_cast<uint32_t>(dt * 1000.0f);
    }

    if (gpsFixFresh()) {
        if (g_stats.alt_ready) {
            const float delta = g_fix.altitude_m - g_stats.last_alt_m;
            if (delta > 0.8f) {
                g_stats.ascent_m += delta;
            } else if (delta < -0.8f) {
                g_stats.descent_m -= delta;
            }
        }
        g_stats.last_alt_m = g_fix.altitude_m;
        g_stats.alt_ready = true;
    }

    if (g_stats.fused_kmh < 1.0f) {
        g_stats.launch_armed = true;
    }
    if (g_stats.launch_armed && !g_stats.launch_active && g_stats.fused_kmh >= 2.0f) {
        g_stats.launch_ms = now;
        g_stats.launch_active = true;
        g_stats.launch_armed = false;
        g_stats.t_0_30 = g_stats.t_0_50 = g_stats.t_0_100 = 0.0f;
    }
    if (g_stats.launch_active) {
        const float elapsed = (now - g_stats.launch_ms) / 1000.0f;
        if (g_stats.t_0_30 == 0.0f && g_stats.fused_kmh >= 30.0f) {
            g_stats.t_0_30 = elapsed;
        }
        if (g_stats.t_0_50 == 0.0f && g_stats.fused_kmh >= 50.0f) {
            g_stats.t_0_50 = elapsed;
        }
        if (g_stats.t_0_100 == 0.0f && g_stats.fused_kmh >= 100.0f) {
            g_stats.t_0_100 = elapsed;
            g_stats.launch_active = false;
        } else if (elapsed > 120.0f) {
            g_stats.launch_active = false;
        }
    }
    if (!g_stats.brake_active && previous >= 100.0f && g_stats.fused_kmh < previous) {
        g_stats.brake_active = true;
        g_stats.brake_ms = now;
        g_stats.t_100_0 = 0.0f;
    }
    if (g_stats.brake_active && g_stats.fused_kmh < 1.0f) {
        g_stats.t_100_0 = (now - g_stats.brake_ms) / 1000.0f;
        g_stats.brake_active = false;
    }
}

static void appendRunSample() {
    if (!g_recording || !g_run_file || millis() - g_last_log_ms < GPS_LOG_MS) {
        return;
    }
    g_last_log_ms = millis();
    const float accel_g = g_stats.speed_count > 1
                              ? ((g_stats.fused_kmh - g_stats.gps_kmh) / 3.6f) / 9.80665f
                              : 0.0f;
    RunSample sample{};
    sample.elapsed_ms = millis() - g_stats.started_ms;
    sample.lat_e7 = static_cast<int32_t>(g_fix.lat * 10000000.0);
    sample.lon_e7 = static_cast<int32_t>(g_fix.lon * 10000000.0);
    sample.speed_d10 = static_cast<int16_t>(constrain(g_stats.fused_kmh * 10.0f, 0.0f, 32767.0f));
    sample.gps_speed_d10 = static_cast<int16_t>(constrain(g_stats.gps_kmh * 10.0f, 0.0f, 32767.0f));
    sample.altitude_d10 =
        static_cast<int16_t>(constrain(g_fix.altitude_m * 10.0f, -32768.0f, 32767.0f));
    sample.accel_mg = static_cast<int16_t>(constrain(accel_g * 1000.0f, -32768.0f, 32767.0f));
    sample.course_d10 =
        static_cast<uint16_t>(constrain(g_fix.course_deg * 10.0f, 0.0f, 3599.0f));
    sample.sats_used = g_fix.satellites_used;
    sample.sats_vis = visibleTotal();
    sample.hdop_d10 = static_cast<uint8_t>(constrain(g_fix.hdop * 10.0f, 0.0f, 255.0f));
    sample.reserved = 0;
    if (g_run_file.write(reinterpret_cast<const uint8_t*>(&sample), sizeof(sample)) ==
        sizeof(sample)) {
        g_run.samples++;
        if ((g_run.samples % 10) == 0) {
            checkpointRun();
        }
    } else {
        stopRecording();
    }
}

static void drawField(const int x, const int y, const int clear_w, const int text_size,
                      const uint16_t color, const char* text) {
    auto& d = M5Cardputer.Display;
    d.fillRect(x, y, clear_w, text_size * 8, BLACK);
    d.setTextSize(text_size);
    d.setTextColor(color, BLACK);
    d.setCursor(x, y);
    d.print(text);
}

// 录制状态：未录制=绿色播放三角；录制中=红色停止方块，约 500ms 闪烁。
static bool recIconVisible() {
    return !g_recording || ((millis() / GPS_REC_BLINK_MS) & 1) != 0;
}

static void drawRecordIcon(const int x, const int y, const int size, const bool visible) {
    auto& d = M5Cardputer.Display;
    d.fillRect(x, y, size, size, BLACK);
    if (!visible) {
        return;
    }
    if (g_recording) {
        const int pad = max(2, size / 4);
        d.fillRoundRect(x + pad, y + pad, size - pad * 2, size - pad * 2, 2, APP_COLOR_ERROR);
    } else {
        const int pad = max(2, size / 5);
        d.fillTriangle(x + pad, y + pad, x + pad, y + size - pad, x + size - pad, y + size / 2,
                       APP_COLOR_OK);
    }
}

static void gpsRecChromePos(int* out_icon_x, int* out_icon_y, int* out_fix_x, int* out_fix_y) {
    auto& d = M5Cardputer.Display;
    constexpr int size = GPS_REC_ICON_SMALL;
    constexpr int fix_region_w = 56;
    const int title_h = g_chrome_title_size * 8;
    // 相对标题区：右移 15px；图标较默认居中下移 1px（原上移 2 → 上移 1）
    const int icon_x = d.width() - APP_HELP_EDGE - fix_region_w - size - 4 + 15;
    const int icon_y = APP_HELP_EDGE + (title_h - size) / 2 - 1;
    if (out_icon_x != nullptr) {
        *out_icon_x = icon_x;
    }
    if (out_icon_y != nullptr) {
        *out_icon_y = icon_y;
    }
    if (out_fix_x != nullptr) {
        *out_fix_x = d.width() - APP_HELP_EDGE - fix_region_w;
    }
    if (out_fix_y != nullptr) {
        // size-1 文字（8px）与 12px 图标纵向居中，再下移 1px
        *out_fix_y = icon_y + (size - 8) / 2 + 1;
    }
}

static void drawSmallRecordStatus(bool* cache_recording, bool* cache_icon_on) {
    int x = 0;
    int y = 0;
    gpsRecChromePos(&x, &y, nullptr, nullptr);
    const bool icon_on = recIconVisible();
    if (cache_recording != nullptr && *cache_recording == g_recording &&
        cache_icon_on != nullptr && *cache_icon_on == icon_on) {
        return;
    }
    drawRecordIcon(x, y, GPS_REC_ICON_SMALL, icon_on);
    if (cache_recording != nullptr) {
        *cache_recording = g_recording;
    }
    if (cache_icon_on != nullptr) {
        *cache_icon_on = icon_on;
    }
}

static void drawFixStatus(char* cache_fix, const size_t cache_size, bool* cache_ok) {
    auto& d = M5Cardputer.Display;
    const char* name = fixName();
    const bool ok = gpsFixFresh();
    if (cache_fix != nullptr && strcmp(cache_fix, name) == 0 && cache_ok != nullptr &&
        *cache_ok == ok) {
        return;
    }
    constexpr int region_w = 56;
    int x = 0;
    int y = 0;
    gpsRecChromePos(nullptr, nullptr, &x, &y);
    d.fillRect(x, y, region_w, 8, BLACK);
    d.setTextSize(1);
    d.setTextColor(ok ? APP_COLOR_OK : APP_COLOR_ERROR, BLACK);
    const int fix_w = d.textWidth(name);
    d.setCursor(d.width() - APP_HELP_EDGE - fix_w, y);
    d.print(name);
    if (cache_fix != nullptr) {
        strncpy(cache_fix, name, cache_size - 1);
        cache_fix[cache_size - 1] = '\0';
    }
    if (cache_ok != nullptr) {
        *cache_ok = ok;
    }
}

static void drawTopChrome(const char* title, const int title_size = GPS_TITLE_SIZE) {
    auto& d = M5Cardputer.Display;
    g_chrome_title_size = title_size < 1 ? 1 : title_size;
    d.fillScreen(BLACK);
    d.setTextSize(g_chrome_title_size);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(APP_HELP_EDGE, APP_HELP_EDGE);
    d.print("GPS");
    const int title_x = APP_HELP_EDGE + d.textWidth("GPS") + APP_HELP_SUBTITLE_GAP;
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(title_x, APP_HELP_EDGE);
    d.print(title);
}

static int gpsContentY() {
    return APP_HELP_EDGE + g_chrome_title_size * 8 + GPS_CONTENT_GAP;
}

static int gpsSpeedY() {
    return gpsContentY();
}

static int gpsBelowSpeedY() {
    return gpsSpeedY() + GPS_SPEED_H + GPS_CONTENT_GAP;
}

static void drawLive(const bool full) {
    auto& d = M5Cardputer.Display;
    // 先画 header 再算内容 y：Sky plot 会把 g_chrome_title_size 设为 1，
    // 若先算 y 再 drawTopChrome，标签会贴在错误的小间距上。
    if (full) {
        drawTopChrome("Live");
    }
    const int speed_y = gpsSpeedY();
    const int y0 = gpsBelowSpeedY();
    constexpr int speed_w = 96;
    if (full) {
        d.setTextSize(1);
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(5 + speed_w + 2, speed_y + (GPS_SPEED_H - 8) / 2);
        d.print("km/h");
        d.setTextColor(APP_COLOR_LABEL, BLACK);
        d.setCursor(5, y0);
        d.print("Lat");
        d.setCursor(5, y0 + 12);
        d.print("Lon");
        d.setCursor(5, y0 + 26);
        d.print("Alt");
        d.setCursor(116, y0 + 26);
        d.print("HDOP");
        d.setCursor(5, y0 + 38);
        d.print("Sats");
        d.setCursor(88, y0 + 38);
        d.print("Course");
        memset(&g_live_ui, 0, sizeof(g_live_ui));
        g_live_ui.recording = !g_recording;
        g_live_ui.rec_icon_on = !recIconVisible();
    }

    drawFixStatus(g_live_ui.fix, sizeof(g_live_ui.fix), &g_live_ui.fix_ok);
    drawSmallRecordStatus(&g_live_ui.recording, &g_live_ui.rec_icon_on);

    char buf[24];
    snprintf(buf, sizeof(buf), "%5.1f", static_cast<double>(g_stats.fused_kmh));
    if (strcmp(g_live_ui.speed, buf) != 0) {
        drawField(5, speed_y, speed_w, GPS_SPEED_SIZE, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.speed, buf, sizeof(g_live_ui.speed) - 1);
    }

    snprintf(buf, sizeof(buf), "%.6f", g_fix.lat);
    if (strcmp(g_live_ui.lat, buf) != 0) {
        drawField(36, y0, 78, 1, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.lat, buf, sizeof(g_live_ui.lat) - 1);
    }

    snprintf(buf, sizeof(buf), "%.6f", g_fix.lon);
    if (strcmp(g_live_ui.lon, buf) != 0) {
        drawField(36, y0 + 12, 78, 1, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.lon, buf, sizeof(g_live_ui.lon) - 1);
    }

    snprintf(buf, sizeof(buf), "%.1fm", static_cast<double>(g_fix.altitude_m));
    if (strcmp(g_live_ui.alt, buf) != 0) {
        drawField(36, y0 + 26, 72, 1, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.alt, buf, sizeof(g_live_ui.alt) - 1);
    }

    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(g_fix.hdop));
    if (strcmp(g_live_ui.hdop, buf) != 0) {
        drawField(154, y0 + 26, 40, 1, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.hdop, buf, sizeof(g_live_ui.hdop) - 1);
    }

    snprintf(buf, sizeof(buf), "%u/%u", g_fix.satellites_used, visibleTotal());
    if (strcmp(g_live_ui.sat, buf) != 0) {
        drawField(36, y0 + 38, 48, 1, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.sat, buf, sizeof(g_live_ui.sat) - 1);
    }

    snprintf(buf, sizeof(buf), "%03.0f %s", static_cast<double>(g_fix.course_deg),
             cardinal(g_fix.course_deg));
    if (strcmp(g_live_ui.course, buf) != 0) {
        drawField(136, y0 + 38, 56, 1, APP_COLOR_VALUE, buf);
        strncpy(g_live_ui.course, buf, sizeof(g_live_ui.course) - 1);
    }

    snprintf(buf, sizeof(buf), "UTC %08lu %06lu", static_cast<unsigned long>(g_fix.utc_date),
             static_cast<unsigned long>(g_fix.utc_time));
    if (strcmp(g_live_ui.utc, buf) != 0) {
        drawField(5, y0 + 52, 150, 1, APP_COLOR_HINT, buf);
        strncpy(g_live_ui.utc, buf, sizeof(g_live_ui.utc) - 1);
    }
}

static void drawSatellitesBars(const bool full) {
    auto& d = M5Cardputer.Display;
    if (full) {
        drawTopChrome("Satellites");
    }
    const int y = gpsContentY();
    struct Row {
        const char* name;
        uint8_t count;
        uint16_t color;
    };
    const Row rows[] = {
        {"GPS", g_fix.visible.gps, CYAN},       {"BeiDou", g_fix.visible.bds, RED},
        {"GLONASS", g_fix.visible.glo, ORANGE}, {"Galileo", g_fix.visible.gal, GREEN},
        {"QZSS", g_fix.visible.qzss, MAGENTA},
    };
    if (full) {
        d.setTextSize(1);
        for (int i = 0; i < 5; ++i) {
            const int ry = y + i * 14;
            d.setTextColor(rows[i].color, BLACK);
            d.setCursor(5, ry);
            d.print(rows[i].name);
        }
        memset(&g_sat_ui, 0, sizeof(g_sat_ui));
        g_sat_ui.counts[0] = 0xFF;
        g_sat_ui.recording = !g_recording;
        g_sat_ui.rec_icon_on = !recIconVisible();
    }

    drawFixStatus(g_sat_ui.fix, sizeof(g_sat_ui.fix), &g_sat_ui.fix_ok);
    drawSmallRecordStatus(&g_sat_ui.recording, &g_sat_ui.rec_icon_on);

    d.setTextSize(1);
    for (int i = 0; i < 5; ++i) {
        if (!full && g_sat_ui.counts[i] == rows[i].count) {
            continue;
        }
        const int ry = y + i * 14;
        char count_buf[4];
        snprintf(count_buf, sizeof(count_buf), "%2u", rows[i].count);
        drawField(72, ry, 18, 1, APP_COLOR_VALUE, count_buf);
        // 20 颗满格；已填充无边框，空白完整边框
        const int pct = min(100, static_cast<int>(rows[i].count) * 5);
        drawPercentBar(94, ry + 2, 140, 5, pct, rows[i].color);
        g_sat_ui.counts[i] = rows[i].count;
    }

    char footer[40];
    snprintf(footer, sizeof(footer), "used %u  PDOP %.1f  VDOP %.1f", g_fix.satellites_used,
             static_cast<double>(g_fix.pdop), static_cast<double>(g_fix.vdop));
    if (strcmp(g_sat_ui.footer, footer) != 0) {
        const int footer_y = d.height() - APP_HELP_EDGE - 8;
        drawField(5, footer_y, 200, 1, APP_COLOR_HINT, footer);
        strncpy(g_sat_ui.footer, footer, sizeof(g_sat_ui.footer) - 1);
    }
}

static void skyPlotPoint(const int cx, const int cy, const float azim_deg, const float elev_deg,
                         const int sky_r, int* out_x, int* out_y) {
    const float elev = constrain(elev_deg, 0.0f, 90.0f);
    const float r = (90.0f - elev) / 90.0f * static_cast<float>(sky_r);
    // 固定上北下南：方位角 0° 在屏顶，顺时针为正
    float az = azim_deg;
    while (az < 0.0f) {
        az += 360.0f;
    }
    while (az >= 360.0f) {
        az -= 360.0f;
    }
    const float rad = az * static_cast<float>(M_PI) / 180.0f;
    *out_x = cx + static_cast<int>(lroundf(r * sinf(rad)));
    *out_y = cy - static_cast<int>(lroundf(r * cosf(rad)));
}

static void drawSatellitesSky(const bool full) {
    auto& d = M5Cardputer.Display;
    const uint32_t fp = satsSkyFingerprint();
    if (full) {
        drawTopChrome("Sky plot", 1);
        memset(&g_sat_ui, 0, sizeof(g_sat_ui));
        g_sat_ui.recording = !g_recording;
        g_sat_ui.rec_icon_on = !recIconVisible();
        g_sat_ui.sky_fp = fp ^ 1u; // force body redraw
    }

    drawFixStatus(g_sat_ui.fix, sizeof(g_sat_ui.fix), &g_sat_ui.fix_ok);
    drawSmallRecordStatus(&g_sat_ui.recording, &g_sat_ui.rec_icon_on);

    constexpr int pad = APP_HELP_EDGE;
    const int footer_y = d.height() - pad - 8;
    const int y0 = gpsContentY();
    const int plot_bottom = footer_y - pad;
    const bool body_dirty = full || g_sat_ui.sky_fp != fp;
    if (body_dirty) {
        d.fillRect(0, y0, d.width(), max(0, plot_bottom - y0), BLACK);

        constexpr int label_out = 10;
        const int avail_h = max(0, plot_bottom - y0);
        const int list_x = d.width() / 2 + 8;
        const int max_r_h = max(8, avail_h / 2 - pad);
        const int max_r_w = max(8, list_x - pad - label_out - pad);
        const int sky_r = min(GPS_SKY_R, min(max_r_h, max_r_w));
        const int cx = pad + label_out + sky_r;
        const int cy = y0 + avail_h / 2;
        const uint16_t ring = APP_COLOR_MUTED;

        // 多层同心圆：地平线 / 30° / 60°
        d.drawCircle(cx, cy, sky_r, ring);
        d.drawCircle(cx, cy, sky_r * 2 / 3, ring);
        d.drawCircle(cx, cy, sky_r / 3, ring);
        d.drawFastHLine(cx - sky_r, cy, sky_r * 2, ring);
        d.drawFastVLine(cx, cy - sky_r, sky_r * 2, ring);
        d.fillCircle(cx, cy, 2, APP_COLOR_HINT);

        // 固定上北下南、左西右东
        static const struct {
            float azim;
            const char* label;
        } dirs[] = {{0.0f, "N"}, {90.0f, "E"}, {180.0f, "S"}, {270.0f, "W"}};
        d.setTextSize(1);
        for (const auto& dir : dirs) {
            const float rad = dir.azim * static_cast<float>(M_PI) / 180.0f;
            const float rr = static_cast<float>(sky_r + 7);
            const int lx = cx + static_cast<int>(lroundf(rr * sinf(rad)));
            const int ly = cy - static_cast<int>(lroundf(rr * cosf(rad)));
            d.setTextColor(APP_COLOR_LABEL, BLACK);
            d.setCursor(lx - 3, ly - 3);
            d.print(dir.label);
        }

        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(cx + sky_r / 3 + 2, cy - 3);
        d.print("60");
        d.setCursor(cx + sky_r * 2 / 3 + 2, cy - 3);
        d.print("30");
        d.setCursor(cx + sky_r - 10, cy + sky_r - 10);
        d.print("0");

        // 按 SNR 降序索引，右侧列表 + 天空点
        uint8_t order[GPS_SAT_MAX];
        for (uint8_t i = 0; i < g_sat_count; ++i) {
            order[i] = i;
        }
        for (uint8_t i = 1; i < g_sat_count; ++i) {
            const uint8_t key = order[i];
            int j = i - 1;
            while (j >= 0 && g_sats[order[j]].snr < g_sats[key].snr) {
                order[j + 1] = order[j];
                --j;
            }
            order[j + 1] = key;
        }

        for (uint8_t i = 0; i < g_sat_count; ++i) {
            const SatSky& sat = g_sats[i];
            int sx = 0;
            int sy = 0;
            skyPlotPoint(cx, cy, sat.azim, sat.elev, sky_r, &sx, &sy);
            const uint16_t color = satSystemColor(sat.system);
            const int dot_r = sat.snr >= 40 ? 3 : (sat.snr >= 25 ? 2 : 1);
            if (sat.snr > 0) {
                d.fillCircle(sx, sy, dot_r, color);
            } else {
                d.drawCircle(sx, sy, max(dot_r, 2), color);
            }
        }

        const int list_rows = max(1, (plot_bottom - y0 - 10) / 10);
        d.setTextSize(1);
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(list_x, y0);
        d.print("ID  SNR");
        const uint8_t show = min(static_cast<uint8_t>(list_rows), g_sat_count);
        for (uint8_t i = 0; i < show; ++i) {
            const SatSky& sat = g_sats[order[i]];
            char name[6];
            formatSatName(sat, name, sizeof(name));
            const int ry = y0 + 10 + static_cast<int>(i) * 10;
            d.setTextColor(satSystemColor(sat.system), BLACK);
            d.setCursor(list_x, ry);
            d.print(name);
            char snr_buf[8];
            if (sat.snr > 0) {
                snprintf(snr_buf, sizeof(snr_buf), "%2u", sat.snr);
            } else {
                snprintf(snr_buf, sizeof(snr_buf), "--");
            }
            d.setTextColor(APP_COLOR_VALUE, BLACK);
            d.setCursor(list_x + 36, ry);
            d.print(snr_buf);
            const int bar_pct = sat.snr > 0 ? min(100, static_cast<int>(sat.snr)) : 0;
            const int bar_w = min(42, d.width() - pad - (list_x + 54));
            if (bar_w >= 8) {
                drawPercentBar(list_x + 54, ry + 1, bar_w, 5, bar_pct, satSystemColor(sat.system));
            }
        }
        if (g_sat_count == 0) {
            d.setTextColor(APP_COLOR_HINT, BLACK);
            d.setCursor(list_x, y0 + 14);
            d.print("no sats");
        } else if (g_sat_count > show) {
            d.setTextColor(APP_COLOR_HINT, BLACK);
            d.setCursor(list_x, y0 + 10 + static_cast<int>(show) * 10);
            d.printf("+%u more", g_sat_count - show);
        }

        g_sat_ui.sky_fp = fp;
    }

    char footer[40];
    snprintf(footer, sizeof(footer), "used %u  hd %03.0f %s", g_fix.satellites_used,
             static_cast<double>(g_fix.course_deg), cardinal(g_fix.course_deg));
    if (full || strcmp(g_sat_ui.footer, footer) != 0) {
        // 右下角，避开天空图下方的方位/仰角标注
        constexpr int footer_h = 8;
        const int clear_x = d.width() / 2;
        const int clear_w = max(0, d.width() - pad - clear_x);
        d.fillRect(clear_x, footer_y, clear_w, footer_h, BLACK);
        d.setTextSize(1);
        d.setTextColor(APP_COLOR_HINT, BLACK);
        const int tw = d.textWidth(footer);
        d.setCursor(max(clear_x, d.width() - pad - tw), footer_y);
        d.print(footer);
        strncpy(g_sat_ui.footer, footer, sizeof(g_sat_ui.footer) - 1);
        g_sat_ui.footer[sizeof(g_sat_ui.footer) - 1] = '\0';
    }
}

static void drawSatellites(const bool full) {
    drawSatellitesBars(full);
}

static void formatDuration(const uint32_t ms, char* out, const size_t size) {
    const uint32_t seconds = ms / 1000;
    snprintf(out, size, "%02lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds / 60) % 60),
             static_cast<unsigned long>(seconds % 60));
}

static void drawSpeed(const bool full) {
    auto& d = M5Cardputer.Display;
    if (full) {
        drawTopChrome("Speed");
    }
    const int y = gpsSpeedY();
    constexpr int speed_w = 96;
    const int stats_y = gpsBelowSpeedY();
    if (full) {
        d.setTextSize(1);
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(5 + speed_w + 2, y + (GPS_SPEED_H - 8) / 2);
        d.print("km/h");
        d.setTextColor(APP_COLOR_LABEL, BLACK);
        d.setCursor(5, stats_y);
        d.print("Time");
        d.setCursor(128, stats_y);
        d.print("Max");
        d.setCursor(5, stats_y + 13);
        d.print("Dist");
        d.setCursor(128, stats_y + 13);
        d.print("Avg");
        d.setCursor(5, stats_y + 26);
        d.print("0-50");
        d.setCursor(128, stats_y + 26);
        d.print("0-100");
        d.setCursor(5, stats_y + 39);
        d.print("100-0");
        d.setCursor(128, stats_y + 39);
        d.print("Accel");
        memset(&g_speed_ui, 0, sizeof(g_speed_ui));
        g_speed_ui.recording = !g_recording;
        g_speed_ui.rec_icon_on = !recIconVisible();
    }

    drawFixStatus(g_speed_ui.fix, sizeof(g_speed_ui.fix), &g_speed_ui.fix_ok);
    const bool recording_changed = g_speed_ui.recording != g_recording;
    drawSmallRecordStatus(&g_speed_ui.recording, &g_speed_ui.rec_icon_on);

    char buf[24];
    snprintf(buf, sizeof(buf), "%5.1f", static_cast<double>(g_stats.fused_kmh));
    if (strcmp(g_speed_ui.speed, buf) != 0 || recording_changed) {
        drawField(5, y, speed_w, GPS_SPEED_SIZE, g_recording ? APP_COLOR_OK : APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.speed, buf, sizeof(g_speed_ui.speed) - 1);
    }

    formatDuration(g_recording ? millis() - g_stats.started_ms : 0, buf, sizeof(buf));
    if (strcmp(g_speed_ui.duration, buf) != 0) {
        drawField(42, stats_y, 72, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.duration, buf, sizeof(g_speed_ui.duration) - 1);
    }

    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(g_stats.max_kmh));
    if (strcmp(g_speed_ui.max_kmh, buf) != 0) {
        drawField(158, stats_y, 48, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.max_kmh, buf, sizeof(g_speed_ui.max_kmh) - 1);
    }

    snprintf(buf, sizeof(buf), "%.3fkm", static_cast<double>(g_stats.distance_m / 1000.0f));
    if (strcmp(g_speed_ui.dist, buf) != 0) {
        drawField(42, stats_y + 13, 72, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.dist, buf, sizeof(g_speed_ui.dist) - 1);
    }

    const float avg =
        g_stats.speed_count ? g_stats.speed_sum / static_cast<float>(g_stats.speed_count) : 0;
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(avg));
    if (strcmp(g_speed_ui.avg, buf) != 0) {
        drawField(158, stats_y + 13, 48, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.avg, buf, sizeof(g_speed_ui.avg) - 1);
    }

    if (g_stats.t_0_50 > 0) {
        snprintf(buf, sizeof(buf), "%.2fs", static_cast<double>(g_stats.t_0_50));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    if (strcmp(g_speed_ui.t050, buf) != 0) {
        drawField(42, stats_y + 26, 72, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.t050, buf, sizeof(g_speed_ui.t050) - 1);
    }

    if (g_stats.t_0_100 > 0) {
        snprintf(buf, sizeof(buf), "%.2fs", static_cast<double>(g_stats.t_0_100));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    if (strcmp(g_speed_ui.t0100, buf) != 0) {
        drawField(174, stats_y + 26, 48, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.t0100, buf, sizeof(g_speed_ui.t0100) - 1);
    }

    if (g_stats.t_100_0 > 0) {
        snprintf(buf, sizeof(buf), "%.2fs", static_cast<double>(g_stats.t_100_0));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    if (strcmp(g_speed_ui.t1000, buf) != 0) {
        drawField(42, stats_y + 39, 72, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.t1000, buf, sizeof(g_speed_ui.t1000) - 1);
    }

    snprintf(buf, sizeof(buf), "+%.2f/%.2f", static_cast<double>(g_stats.max_accel_g),
             static_cast<double>(g_stats.max_brake_g));
    if (strcmp(g_speed_ui.g, buf) != 0) {
        drawField(168, stats_y + 39, 66, 1, APP_COLOR_VALUE, buf);
        strncpy(g_speed_ui.g, buf, sizeof(g_speed_ui.g) - 1);
    }
}

static void drawHistory() {
    loadIndex();
    drawTopChrome("History");
    drawFixStatus(nullptr, 0, nullptr);
    g_hist_rec_recording = !g_recording;
    g_hist_rec_icon_on = !recIconVisible();
    drawSmallRecordStatus(&g_hist_rec_recording, &g_hist_rec_icon_on);
    auto& d = M5Cardputer.Display;
    const int y = gpsContentY();
    d.setTextSize(1);
    if (g_index.count == 0) {
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(5, y);
        d.print("No speed records");
        return;
    }
    g_history_selected = constrain(g_history_selected, 0, g_index.count - 1);
    const int first =
        min(max(0, g_history_selected - 3),
            max(0, static_cast<int>(g_index.count) - GPS_HISTORY_VISIBLE));
    constexpr int list_w = 220;
    for (int row = 0; row < GPS_HISTORY_VISIBLE && first + row < g_index.count; ++row) {
        const int idx = first + row;
        const RunMeta& run = g_index.runs[idx];
        const int ry = y + row * GPS_HISTORY_ROW_H;
        const bool selected = idx == g_history_selected;
        if (selected) {
            d.fillRect(5, ry, list_w, GPS_HISTORY_ROW_H, APP_COLOR_MENU_KEY);
            d.setTextColor(BLACK, APP_COLOR_MENU_KEY);
        } else {
            d.setTextColor(APP_COLOR_LABEL, BLACK);
        }
        d.setCursor(5 + GPS_HISTORY_PAD, ry + GPS_HISTORY_PAD);
        d.printf("%c%02d %08lu %4.1fkm %4.1f %u/%u", selected ? '>' : ' ', idx + 1,
                 static_cast<unsigned long>(run.utc_date),
                 static_cast<double>(run.distance_m / 1000.0f), static_cast<double>(run.max_kmh),
                 run.sats_used, run.sats_vis);
    }
    drawAppScrollbar(d, y, GPS_HISTORY_VISIBLE * GPS_HISTORY_ROW_H, g_index.count,
                     GPS_HISTORY_VISIBLE, first);
}

static float chartSampleValue(const RunSample& sample) {
    if (g_chart_metric == ChartMetric::Altitude) {
        return sample.altitude_d10 / 10.0f;
    }
    if (g_chart_metric == ChartMetric::Accel) {
        return sample.accel_mg / 1000.0f;
    }
    return sample.speed_d10 / 10.0f;
}

static const char* chartMetricName() {
    if (g_chart_metric == ChartMetric::Altitude) {
        return "Alt m";
    }
    if (g_chart_metric == ChartMetric::Accel) {
        return "Acc g";
    }
    return "Speed km/h";
}

static void drawChartKv(const int x, const int y, const char* label, const char* value,
                        const uint16_t value_color) {
    auto& d = M5Cardputer.Display;
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(x, y);
    d.print(label);
    d.setTextColor(value_color, BLACK);
    d.setCursor(x + d.textWidth(label) + 4, y);
    d.print(value);
}

static void formatTiming(const float seconds, char* out, const size_t size) {
    if (seconds > 0.0f) {
        snprintf(out, size, "%.2fs", static_cast<double>(seconds));
    } else {
        snprintf(out, size, "--");
    }
}

static void drawHistoryChart() {
    loadIndex();
    drawTopChrome("Record");
    drawFixStatus(nullptr, 0, nullptr);
    auto& d = M5Cardputer.Display;
    if (g_index.count == 0 || g_history_selected >= g_index.count) {
        return;
    }
    const RunMeta& meta = g_index.runs[g_history_selected];
    char path[24];
    runPath(meta.id, path, sizeof(path));
    File f = LittleFS.open(path, "r");
    if (!f) {
        d.setTextColor(APP_COLOR_ERROR, BLACK);
        d.setCursor(APP_HELP_EDGE, gpsContentY());
        d.print("record file missing");
        return;
    }
    RunMeta file_meta{};
    if (!readRunMeta(f, &file_meta)) {
        f.close();
        d.setTextColor(APP_COLOR_ERROR, BLACK);
        d.setCursor(APP_HELP_EDGE, gpsContentY());
        d.print("record file invalid");
        return;
    }

    constexpr int pad = APP_HELP_EDGE;
    constexpr int label_w = 22;
    constexpr int axis_h = 9;
    constexpr int stats_rows = 4;
    constexpr int stats_row_h = 10;
    constexpr int stats_h = stats_rows * stats_row_h;
    // 内容贴近 header，少留空
    const int title_y = APP_HELP_EDGE + g_chrome_title_size * 8 + 3;
    const int chart_x = pad + label_w;
    const int chart_y = title_y + 9;
    const int chart_w = d.width() - chart_x - pad;
    const int chart_h = max(24, d.height() - chart_y - pad - axis_h - stats_h);
    constexpr int buckets = 103;
    float min_value = 1e9f;
    float max_value = -1e9f;
    float values[buckets]{};
    uint16_t counts[buckets]{};
    float peak_accel_g = file_meta.max_accel_g;
    float peak_brake_g = file_meta.max_brake_g;
    float t_0_30 = file_meta.t_0_30;
    float prev_spd = -1.0f;
    uint32_t prev_ms = 0;
    RunSample sample{};
    while (f.read(reinterpret_cast<uint8_t*>(&sample), sizeof(sample)) == sizeof(sample)) {
        const int idx = file_meta.duration_ms > 0
                            ? min(buckets - 1, static_cast<int>(
                                                   (static_cast<uint64_t>(sample.elapsed_ms) * buckets) /
                                                   file_meta.duration_ms))
                            : 0;
        const float value = chartSampleValue(sample);
        values[idx] += value;
        counts[idx]++;
        min_value = min(min_value, value);
        max_value = max(max_value, value);

        const float spd = sample.speed_d10 / 10.0f;
        if (t_0_30 <= 0.0f && spd >= 30.0f) {
            t_0_30 = sample.elapsed_ms / 1000.0f;
        }
        if (prev_ms > 0 && sample.elapsed_ms > prev_ms && prev_spd >= 0.0f) {
            const float dt = (sample.elapsed_ms - prev_ms) / 1000.0f;
            if (dt >= 0.2f) {
                const float ag = ((spd - prev_spd) / 3.6f) / dt / 9.80665f;
                peak_accel_g = max(peak_accel_g, ag);
                peak_brake_g = min(peak_brake_g, ag);
            }
        }
        prev_spd = spd;
        prev_ms = sample.elapsed_ms;
    }
    f.close();
    if (min_value > max_value) {
        min_value = 0;
        max_value = 1;
    }
    if (g_chart_metric == ChartMetric::Speed) {
        min_value = 0;
    }
    if (max_value - min_value < 0.1f) {
        max_value = min_value + 0.1f;
    }

    // 优先用索引里更新后的摘要（录制结束写入）
    const float show_max = meta.max_kmh > 0 ? meta.max_kmh : file_meta.max_kmh;
    const float show_avg = meta.avg_kmh > 0 ? meta.avg_kmh : file_meta.avg_kmh;
    const float show_dist = meta.distance_m > 0 ? meta.distance_m : file_meta.distance_m;
    const float show_t050 = meta.t_0_50 > 0 ? meta.t_0_50 : file_meta.t_0_50;
    const float show_t0100 = meta.t_0_100 > 0 ? meta.t_0_100 : file_meta.t_0_100;
    const float show_t1000 = meta.t_100_0 > 0 ? meta.t_100_0 : file_meta.t_100_0;
    if (meta.t_0_30 > 0) {
        t_0_30 = meta.t_0_30;
    }
    if (meta.max_accel_g > peak_accel_g) {
        peak_accel_g = meta.max_accel_g;
    }
    if (meta.max_brake_g < peak_brake_g) {
        peak_brake_g = meta.max_brake_g;
    }

    char duration[16];
    formatDuration(meta.duration_ms ? meta.duration_ms : file_meta.duration_ms, duration,
                   sizeof(duration));
    char buf[24];

    // 顶栏：指标名 + 着色摘要
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(pad, title_y);
    d.print(chartMetricName());
    int tx = pad + d.textWidth(chartMetricName()) + 6;
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(tx, title_y);
    d.print("Max");
    tx += d.textWidth("Max") + 3;
    snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(show_max));
    d.setTextColor(APP_COLOR_WARN, BLACK);
    d.setCursor(tx, title_y);
    d.print(buf);
    tx += d.textWidth(buf) + 6;
    snprintf(buf, sizeof(buf), "%.2fkm", static_cast<double>(show_dist / 1000.0f));
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(tx, title_y);
    d.print(buf);

    if (chart_w > 4 && chart_h > 4) {
        d.drawRect(chart_x, chart_y, chart_w, chart_h, APP_COLOR_MUTED);
    }
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(pad, chart_y);
    d.printf("%.0f", static_cast<double>(max_value));
    d.setCursor(pad, chart_y + chart_h - 8);
    d.printf("%.0f", static_cast<double>(min_value));
    int px = chart_x + 1;
    int py = chart_y + chart_h - 2;
    bool have_prev = false;
    const uint16_t color = g_chart_metric == ChartMetric::Speed
                               ? CYAN
                               : (g_chart_metric == ChartMetric::Altitude ? GREEN : ORANGE);
    for (int i = 0; i < buckets; ++i) {
        if (counts[i] == 0) {
            continue;
        }
        const float value = values[i] / counts[i];
        const int x = chart_x + 1 + i * (chart_w - 3) / (buckets - 1);
        const int y = chart_y + chart_h - 2 -
                      static_cast<int>((value - min_value) * (chart_h - 3) /
                                       (max_value - min_value));
        if (have_prev) {
            d.drawLine(px, py, x, constrain(y, chart_y + 1, chart_y + chart_h - 2), color);
        }
        px = x;
        py = constrain(y, chart_y + 1, chart_y + chart_h - 2);
        have_prev = true;
    }

    // X 轴：左侧 0，时长靠右
    const int axis_y = chart_y + chart_h + 1;
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(chart_x, axis_y);
    d.print("0");
    const int dur_w = d.textWidth(duration);
    d.setCursor(chart_x + chart_w - dur_w, axis_y);
    d.print(duration);

    // 测速详情：Time/Max/Dist/Avg + 0-30/0-50/0-100/100-0 + Accel
    const int stats_y = axis_y + axis_h;
    const int col2 = 128;
    drawChartKv(pad, stats_y, "Time", duration, APP_COLOR_VALUE);
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(show_max));
    drawChartKv(col2, stats_y, "Max", buf, APP_COLOR_WARN);

    snprintf(buf, sizeof(buf), "%.3fkm", static_cast<double>(show_dist / 1000.0f));
    drawChartKv(pad, stats_y + stats_row_h, "Dist", buf, APP_COLOR_VALUE);
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(show_avg));
    drawChartKv(col2, stats_y + stats_row_h, "Avg", buf, APP_COLOR_VALUE);

    char t030[8];
    char t050[8];
    char t0100[8];
    char t1000[8];
    formatTiming(t_0_30, t030, sizeof(t030));
    formatTiming(show_t050, t050, sizeof(t050));
    formatTiming(show_t0100, t0100, sizeof(t0100));
    formatTiming(show_t1000, t1000, sizeof(t1000));
    drawChartKv(pad, stats_y + stats_row_h * 2, "0-30", t030,
                t_0_30 > 0.0f ? APP_COLOR_OK : APP_COLOR_HINT);
    drawChartKv(72, stats_y + stats_row_h * 2, "0-50", t050,
                show_t050 > 0.0f ? APP_COLOR_OK : APP_COLOR_HINT);
    drawChartKv(144, stats_y + stats_row_h * 2, "0-100", t0100,
                show_t0100 > 0.0f ? APP_COLOR_OK : APP_COLOR_HINT);

    drawChartKv(pad, stats_y + stats_row_h * 3, "100-0", t1000,
                show_t1000 > 0.0f ? APP_COLOR_OK : APP_COLOR_HINT);
    snprintf(buf, sizeof(buf), "+%.2f/%.2f", static_cast<double>(peak_accel_g),
             static_cast<double>(peak_brake_g));
    const bool has_accel = peak_accel_g > 0.01f || peak_brake_g < -0.01f;
    drawChartKv(88, stats_y + stats_row_h * 3, "Accel", has_accel ? buf : "--",
                has_accel ? APP_COLOR_WARN : APP_COLOR_HINT);
}

// 第 1 页：当前模块；第 2 页：全局快捷键（固定 2 页，避免内容与 tip 重叠）
static constexpr int GPS_HELP_PAGE_COUNT = 2;

static const char* gpsHelpSubtitle() {
    switch (g_page_before_help) {
        case GpsPage::Speed:
            return "Speed";
        case GpsPage::Satellites:
            return "Sats";
        case GpsPage::SkyPlot:
            return "Sky";
        case GpsPage::History:
            return "History";
        case GpsPage::HistoryChart:
            return "Chart";
        case GpsPage::Settings:
            return "Settings";
        case GpsPage::Live:
        default:
            return "Live";
    }
}

static int drawGpsModuleHelp(const int x, int y) {
    switch (g_page_before_help) {
        case GpsPage::Speed:
            y = drawAppHelpBadge(x, y, "Space", "start / stop record");
            y = drawAppHelpBadge(x, y, "BtnGO", "same as Space");
            y = drawAppHelpKey(x, y, 'r', "reset trip stats");
            y = drawAppHelpLabelTextPlain(x, y, "0-30/50/100", APP_COLOR_VALUE, " accel time");
            y = drawAppHelpLabelTextPlain(x, y, "100-0", APP_COLOR_VALUE, " brake from 100");
            y = drawAppHelpLabelTextPlain(x, y, "Accel", APP_COLOR_VALUE, " peak accel/brake g");
            y = drawAppHelpLabelTextPlain(x, y, "Play/Stop", APP_COLOR_OK, " idle / recording");
            break;
        case GpsPage::Satellites:
            y = drawAppHelpText(x, y, "G/C/R/E/J = GPS/BDS/GLO/GAL/QZSS");
            y = drawAppHelpLabelTextPlain(x, y, "Sats", APP_COLOR_VALUE, " used / visible");
            y = drawAppHelpLabelTextPlain(x, y, "PDOP", APP_COLOR_VALUE, " position dilution");
            y = drawAppHelpLabelTextPlain(x, y, "VDOP", APP_COLOR_VALUE, " vertical dilution");
            y = drawAppHelpText(x, y, "Lower DOP is better.");
            break;
        case GpsPage::SkyPlot:
            y = drawAppHelpText(x, y, "Rings: elev 0 / 30 / 60 deg");
            y = drawAppHelpText(x, y, "North up, south down (fixed)");
            y = drawAppHelpText(x, y, "Dots G/C/R/E/J by system");
            y = drawAppHelpText(x, y, "Dot size follows SNR");
            y = drawAppHelpText(x, y, "Right list: PRN + SNR");
            break;
        case GpsPage::History:
            y = drawAppHelpArrows(x, y, "select record");
            y = drawAppHelpBadge(x, y, "Enter", "open speed curve");
            y = drawAppHelpBadge(x, y, "Bk", "delete selected");
            y = drawAppHelpBadge(x, y, "Space", "start / stop record");
            y = drawAppHelpBadge(x, y, "BtnGO", "same as Space");
            break;
        case GpsPage::HistoryChart:
            y = drawAppHelpKey(x, y, 'm', "cycle Speed/Alt/Accel");
            y = drawAppHelpBadge(x, y, "ESC", "back to history");
            y = drawAppHelpText(x, y, "Curve from selected run.");
            y = drawAppHelpText(x, y, "Stats below: peak / times.");
            break;
        case GpsPage::Settings:
            y = drawAppHelpArrows(x, y, "select update rate");
            y = drawAppHelpBadge(x, y, "Enter", "apply rate");
            y = drawAppHelpBadge(x, y, "ESC", "back");
            y = drawAppHelpText(x, y, "Rates: 1 / 2 / 5 / 10 Hz");
            y = drawAppHelpText(x, y, "PCAS02; saved in NVS.");
            y = drawAppHelpText(x, y, "Reapplied each GPS open.");
            break;
        case GpsPage::Live:
        default:
            y = drawAppHelpBadge(x, y, "Space", "start / stop record");
            y = drawAppHelpBadge(x, y, "BtnGO", "same as Space");
            y = drawAppHelpKey(x, y, 'r', "reset speed stats");
            y = drawAppHelpLabelTextPlain(x, y, "Lat/Lon/Alt", APP_COLOR_VALUE, " position");
            y = drawAppHelpLabelTextPlain(x, y, "HDOP", APP_COLOR_VALUE, " lower=better");
            y = drawAppHelpLabelTextPlain(x, y, "Sats", APP_COLOR_VALUE, " used / visible");
            y = drawAppHelpLabelTextPlain(x, y, "Course", APP_COLOR_VALUE, " heading deg");
            break;
    }
    return y;
}

static int drawGpsGlobalHelp(const int x, int y) {
    y = drawAppHelpBadge(x, y, "1-6", "switch page");
    y = drawAppHelpBadge(x, y, "s/l/o", "speed / history / settings");
    y = drawAppHelpBadge(x, y, "Space", "start / stop record");
    y = drawAppHelpBadge(x, y, "BtnGO", "same as Space");
    y = drawAppHelpKey(x, y, 'r', "reset live / speed stats");
    y = drawAppHelpKey(x, y, 'h', "help / close");
    y = drawAppHelpBadge(x, y, "ESC", "back nested / close help");
    return y;
}

static void drawHelp() {
    int page = g_help_page;
    if (page < 0) {
        page = 0;
    }
    if (page >= GPS_HELP_PAGE_COUNT) {
        page = GPS_HELP_PAGE_COUNT - 1;
    }
    int y = drawAppHelpBegin(gpsHelpSubtitle());
    constexpr int x = APP_HELP_CONTENT_X;
    if (page == 0) {
        y = drawGpsModuleHelp(x, y);
    } else {
        y = drawGpsGlobalHelp(x, y);
    }
    (void)y;
    drawAppHelpFooter(page, GPS_HELP_PAGE_COUNT);
}

static void drawSettings() {
    auto& d = M5Cardputer.Display;
    drawTopChrome("Settings");
    const int y0 = gpsContentY();
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y0);
    d.print("Update rate");
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(5, y0 + 12);
    d.printf("active %d Hz", g_rate_hz);

    for (int i = 0; i < GPS_RATE_OPTION_COUNT; ++i) {
        const int ry = y0 + 28 + i * GPS_HISTORY_ROW_H;
        const bool selected = i == g_rate_cursor;
        const bool active = GPS_RATE_OPTIONS[i].hz == g_rate_hz;
        if (selected) {
            d.fillRect(5, ry, d.width() - 10, GPS_HISTORY_ROW_H, APP_COLOR_MENU_KEY);
            d.setTextColor(BLACK, APP_COLOR_MENU_KEY);
        } else {
            d.setTextColor(APP_COLOR_VALUE, BLACK);
        }
        d.setTextSize(1);
        d.setCursor(5 + GPS_HISTORY_PAD, ry + GPS_HISTORY_PAD);
        d.printf("%c %d Hz", active ? '*' : ' ', GPS_RATE_OPTIONS[i].hz);
    }
}

static void redraw(const bool full = true) {
    if (g_page == GpsPage::Live) {
        drawLive(full);
    } else if (g_page == GpsPage::Speed) {
        drawSpeed(full);
    } else if (g_page == GpsPage::Satellites) {
        drawSatellites(full);
    } else if (g_page == GpsPage::SkyPlot) {
        drawSatellitesSky(full);
    } else if (g_page == GpsPage::History) {
        drawHistory();
    } else if (g_page == GpsPage::HistoryChart) {
        drawHistoryChart();
    } else if (g_page == GpsPage::Settings) {
        drawSettings();
    } else {
        drawHelp();
    }
}

static void setPage(const GpsPage page) {
    g_page = page;
    redraw();
}

} // namespace

void enterGpsApp() {
    leaveGpsApp();
    g_fix = {};
    g_nmea_len = 0;
    g_sat_count = 0;
    g_page = GpsPage::Live;
    g_history_selected = 0;
    g_last_imu_ms = 0;
    g_gravity[0] = 0;
    g_gravity[1] = 0;
    g_gravity[2] = 1;
    resetSpeedStats();
    loadIndex();
    loadGpsRatePrefs();
    M5.Imu.update();
    g_imu_ok = M5.Imu.isEnabled();
    // 先释放 Grove I2C 外设，再把同一组 G1/G2 交给 UART。
    M5Cardputer.Ex_I2C.release();
    g_gps_serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(50);
    applyGpsUpdateRate();
    redraw();
}

void leaveGpsApp() {
    stopRecording();
    g_gps_serial.end();
    // 离开后恢复 Grove 外部 I2C，确保 Radio/NFC/扫描可继续使用。
    M5Cardputer.Ex_I2C.begin();
    g_page = GpsPage::Live;
}

void updateGpsApp() {
    pollSerial();
    updateImu();
    updateSpeedStats();
    appendRunSample();
    pollGpsBtnA();
    if (g_page == GpsPage::Help || g_page == GpsPage::HistoryChart ||
        g_page == GpsPage::Settings) {
        return;
    }
    // History 只刷新录制图标闪烁，避免整页重绘。
    if (g_page == GpsPage::History) {
        if (g_recording) {
            drawSmallRecordStatus(&g_hist_rec_recording, &g_hist_rec_icon_on);
        }
        return;
    }
    // Live / Satellites / Speed：每帧增量刷新（字段缓存跳过未变区域）。
    redraw(false);
}

bool closeGpsHelp() {
    if (g_page != GpsPage::Help) {
        return false;
    }
    setPage(g_page_before_help);
    return true;
}

bool closeGpsHistoryChart() {
    if (g_page != GpsPage::HistoryChart) {
        return false;
    }
    setPage(GpsPage::History);
    return true;
}

bool closeGpsSettings() {
    if (g_page != GpsPage::Settings) {
        return false;
    }
    setPage(g_page_before_settings);
    return true;
}

bool isGpsHelpVisible() {
    return g_page == GpsPage::Help;
}

void pollGpsBtnA() {
    if (g_page == GpsPage::Help) {
        return;
    }
    if (!M5Cardputer.BtnA.wasPressed()) {
        return;
    }
    toggleRecording();
    redraw();
}

void getGpsShotFeature(char* out, const size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    const char* feature = "live";
    switch (g_page) {
        case GpsPage::Live:
            feature = "live";
            break;
        case GpsPage::Speed:
            feature = "speed";
            break;
        case GpsPage::Satellites:
            feature = "sats";
            break;
        case GpsPage::SkyPlot:
            feature = "sky";
            break;
        case GpsPage::History:
            feature = "history";
            break;
        case GpsPage::HistoryChart:
            feature = "chart";
            break;
        case GpsPage::Settings:
            feature = "settings";
            break;
        case GpsPage::Help:
            feature = "help";
            break;
    }
    strncpy(out, feature, out_len - 1);
    out[out_len - 1] = '\0';
}

void handleGpsApp(const Keyboard_Class::KeysState& status) {
    for (const char raw : status.word) {
        const char c =
            (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'h') {
            if (!closeGpsHelp()) {
                g_page_before_help = g_page;
                g_help_page = 0;
                setPage(GpsPage::Help);
            }
            return;
        }
    }
    if (g_page == GpsPage::Help) {
        const int delta = getHelpNavDelta(status);
        if (delta != 0) {
            g_help_page = applyHelpPageDelta(g_help_page, GPS_HELP_PAGE_COUNT, delta);
            drawHelp();
        }
        return;
    }

    if (status.space) {
        toggleRecording();
        redraw();
        return;
    }
    if (g_page == GpsPage::Settings) {
        const int delta = getMenuNavDelta(status);
        if (delta != 0) {
            g_rate_cursor =
                (g_rate_cursor + delta + GPS_RATE_OPTION_COUNT) % GPS_RATE_OPTION_COUNT;
            drawSettings();
            return;
        }
        if (status.enter) {
            setGpsUpdateRate(GPS_RATE_OPTIONS[g_rate_cursor].hz);
            drawSettings();
            return;
        }
    }
    if (g_page == GpsPage::History) {
        const int delta = getMenuNavDelta(status);
        if (delta != 0 && g_index.count > 0) {
            g_history_selected =
                (g_history_selected + delta + g_index.count) % static_cast<int>(g_index.count);
            drawHistory();
            return;
        }
        if (status.enter && g_index.count > 0) {
            setPage(GpsPage::HistoryChart);
            return;
        }
        if (status.del && g_index.count > 0) {
            deleteSelectedHistory();
            drawHistory();
            return;
        }
    }
    for (const char raw : status.word) {
        const char c =
            (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == '1') {
            setPage(GpsPage::Live);
        } else if (c == '2' || c == 's') {
            setPage(GpsPage::Speed);
        } else if (c == '3') {
            setPage(GpsPage::Satellites);
        } else if (c == '4') {
            setPage(GpsPage::SkyPlot);
        } else if (c == '5' || c == 'l') {
            setPage(GpsPage::History);
        } else if (c == '6' || c == 'o') {
            if (g_page != GpsPage::Settings) {
                g_page_before_settings = g_page;
                g_rate_cursor = rateOptionIndex(g_rate_hz);
            }
            setPage(GpsPage::Settings);
        } else if (c == 'm' && g_page == GpsPage::HistoryChart) {
            g_chart_metric = static_cast<ChartMetric>((static_cast<int>(g_chart_metric) + 1) % 3);
            drawHistoryChart();
        } else if (c == 'r') {
            const bool was_recording = g_recording;
            if (was_recording) {
                stopRecording();
            }
            resetSpeedStats();
            if (was_recording) {
                startRecording();
            }
            redraw();
        }
    }
}

