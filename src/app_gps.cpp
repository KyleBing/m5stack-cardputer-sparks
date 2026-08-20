#include "app_gps.h"

#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"

#include <FS.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Unit GPS v1.1 / Unit GPS SMA：黄线 RX 接主机 TX(G2)，白线 TX 接主机 RX(G1)。
static constexpr int GPS_RX_PIN = 1;
static constexpr int GPS_TX_PIN = 2;
static constexpr uint32_t GPS_BAUD = 115200;
static constexpr uint32_t GPS_STALE_MS = 2500;
static constexpr uint32_t GPS_DRAW_MS = 200;
static constexpr uint32_t GPS_LOG_MS = 1000;
static constexpr int GPS_HISTORY_MAX = 12;
static constexpr uint32_t GPS_INDEX_MAGIC = 0x32535047; // GPS2
static constexpr uint32_t GPS_RUN_MAGIC = 0x314E5552;   // RUN1
static constexpr const char* GPS_INDEX_PATH = "/gps_index.bin";

enum class GpsPage : uint8_t {
    Live = 0,
    Satellites,
    Speed,
    History,
    HistoryChart,
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
};

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
    uint8_t sats;
    uint8_t hdop_d10;
};

static HardwareSerial g_gps_serial(1);
static GpsFix g_fix{};
static SpeedStats g_stats{};
static GpsIndex g_index{};
static File g_run_file;
static RunMeta g_run{};
static GpsPage g_page = GpsPage::Live;
static GpsPage g_page_before_help = GpsPage::Live;
static ChartMetric g_chart_metric = ChartMetric::Speed;
static bool g_recording = false;
static bool g_history_loaded = false;
static bool g_imu_ok = false;
static int g_history_selected = 0;
static int g_help_page = 0;
static uint32_t g_last_draw_ms = 0;
static uint32_t g_last_log_ms = 0;
static uint32_t g_last_imu_ms = 0;
static float g_linear_accel_ms2 = 0.0f;
static float g_gravity[3] = {0.0f, 0.0f, 1.0f};
static char g_nmea_line[160]{};
static size_t g_nmea_len = 0;

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
    g_run.t_0_50 = g_stats.t_0_50;
    g_run.t_0_100 = g_stats.t_0_100;
    g_run.t_100_0 = g_stats.t_100_0;
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
    if (talker0 == 'G' && talker1 == 'P') {
        return (prn >= 193 && prn <= 199) ? GNSS_QZSS : GNSS_GPS;
    }
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
    if (prn >= 65 && prn <= 96) {
        return GNSS_GLO;
    }
    if (prn >= 193 && prn <= 199) {
        return GNSS_QZSS;
    }
    if (prn >= 201 && prn <= 237) {
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

static void parseGsv(char** fields, const int count, const char t0, const char t1) {
    if (count < 4) {
        return;
    }
    const int message = atoi(fields[2]);
    if (message == 1) {
        if (t0 == 'G' && t1 == 'N') {
            g_fix.visible = {};
            g_fix.systems_visible = 0;
        } else {
            const uint8_t sys = systemFromPrn(0, t0, t1);
            *countForSystem(sys) = 0;
            g_fix.systems_visible &= ~sys;
        }
    }
    for (int i = 4; i + 3 < count; i += 4) {
        const int prn = atoi(fields[i]);
        if (prn <= 0) {
            continue;
        }
        const uint8_t sys = systemFromPrn(prn, t0, t1);
        uint8_t* value = countForSystem(sys);
        if (*value < 255) {
            (*value)++;
        }
        g_fix.systems_visible |= sys;
    }
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
    sample.sats = g_fix.satellites_used;
    sample.hdop_d10 = static_cast<uint8_t>(constrain(g_fix.hdop * 10.0f, 0.0f, 255.0f));
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

static void drawTopStatus(const char* title) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(BLACK);
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(APP_HELP_EDGE, APP_HELP_EDGE);
    d.print("GPS");
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(APP_HELP_EDGE + 28, APP_HELP_EDGE);
    d.print(title);
    d.setTextColor(gpsFixFresh() ? APP_COLOR_OK : APP_COLOR_ERROR, BLACK);
    const int fix_w = d.textWidth(fixName());
    d.setCursor(d.width() - APP_HELP_EDGE - fix_w, APP_HELP_EDGE);
    d.print(fixName());
}

static void drawLive() {
    drawTopStatus("LIVE");
    auto& d = M5Cardputer.Display;
    const int y0 = APP_HELP_EDGE + 14;
    d.setTextSize(2);
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(5, y0);
    d.printf("%5.1f", static_cast<double>(g_stats.fused_kmh));
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(68, y0 + 7);
    d.print("km/h");

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y0 + 22);
    d.print("LAT");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(36, y0 + 22);
    d.printf("%.6f", g_fix.lat);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y0 + 34);
    d.print("LON");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(36, y0 + 34);
    d.printf("%.6f", g_fix.lon);

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y0 + 48);
    d.print("ALT");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(36, y0 + 48);
    d.printf("%.1fm", static_cast<double>(g_fix.altitude_m));
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(116, y0 + 48);
    d.print("HDOP");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(154, y0 + 48);
    d.printf("%.1f", static_cast<double>(g_fix.hdop));

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y0 + 60);
    d.print("SAT");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(36, y0 + 60);
    d.printf("%u/%u", g_fix.satellites_used, visibleTotal());
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(88, y0 + 60);
    d.print("CRS");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(116, y0 + 60);
    d.printf("%03.0f %s", static_cast<double>(g_fix.course_deg), cardinal(g_fix.course_deg));

    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(5, y0 + 74);
    d.printf("UTC %08lu %06lu", static_cast<unsigned long>(g_fix.utc_date),
             static_cast<unsigned long>(g_fix.utc_time));
    drawHelpHintRight("help");
}

static void drawSatellites() {
    drawTopStatus("SATELLITES");
    auto& d = M5Cardputer.Display;
    const int y = APP_HELP_EDGE + 14;
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
    d.setTextSize(1);
    for (int i = 0; i < 5; ++i) {
        const int ry = y + i * 14;
        d.setTextColor(rows[i].color, BLACK);
        d.setCursor(5, ry);
        d.print(rows[i].name);
        d.setTextColor(APP_COLOR_VALUE, BLACK);
        d.setCursor(72, ry);
        d.printf("%2u", rows[i].count);
        const int bar = min(140, static_cast<int>(rows[i].count) * 7);
        d.fillRect(94, ry + 2, bar, 5, rows[i].color);
        d.drawRect(94, ry + 2, 140, 5, APP_COLOR_MUTED);
    }
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(5, y + 73);
    d.printf("used %u  PDOP %.1f  VDOP %.1f", g_fix.satellites_used,
             static_cast<double>(g_fix.pdop), static_cast<double>(g_fix.vdop));
    drawHelpHintRight("help");
}

static void formatDuration(const uint32_t ms, char* out, const size_t size) {
    const uint32_t seconds = ms / 1000;
    snprintf(out, size, "%02lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds / 60) % 60),
             static_cast<unsigned long>(seconds % 60));
}

static void drawSpeed() {
    drawTopStatus("SPEED");
    auto& d = M5Cardputer.Display;
    const int y = APP_HELP_EDGE + 14;
    d.setTextSize(2);
    d.setTextColor(g_recording ? APP_COLOR_OK : APP_COLOR_VALUE, BLACK);
    d.setCursor(5, y);
    d.printf("%5.1f", static_cast<double>(g_stats.fused_kmh));
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(67, y + 7);
    d.print("km/h");
    d.setTextColor(g_recording ? APP_COLOR_ERROR : APP_COLOR_MUTED, BLACK);
    d.setCursor(188, y + 7);
    d.print(g_recording ? "REC" : "READY");

    char duration[16];
    formatDuration(g_recording ? millis() - g_stats.started_ms : 0, duration, sizeof(duration));
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y + 21);
    d.print("TIME");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(42, y + 21);
    d.print(duration);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(128, y + 21);
    d.print("MAX");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(158, y + 21);
    d.printf("%.1f", static_cast<double>(g_stats.max_kmh));

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y + 34);
    d.print("DIST");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(42, y + 34);
    d.printf("%.3fkm", static_cast<double>(g_stats.distance_m / 1000.0f));
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(128, y + 34);
    d.print("AVG");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(158, y + 34);
    const float avg =
        g_stats.speed_count ? g_stats.speed_sum / static_cast<float>(g_stats.speed_count) : 0;
    d.printf("%.1f", static_cast<double>(avg));

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y + 48);
    d.print("0-50");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(42, y + 48);
    d.printf(g_stats.t_0_50 > 0 ? "%.2fs" : "--", static_cast<double>(g_stats.t_0_50));
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(128, y + 48);
    d.print("0-100");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(174, y + 48);
    d.printf(g_stats.t_0_100 > 0 ? "%.2f" : "--", static_cast<double>(g_stats.t_0_100));

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, y + 62);
    d.print("100-0");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(42, y + 62);
    d.printf(g_stats.t_100_0 > 0 ? "%.2fs" : "--", static_cast<double>(g_stats.t_100_0));
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(128, y + 62);
    d.print("G");
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(145, y + 62);
    d.printf("+%.2f/%.2f", static_cast<double>(g_stats.max_accel_g),
             static_cast<double>(g_stats.max_brake_g));
    drawHelpHintRight("help");
}

static void drawHistory() {
    loadIndex();
    drawTopStatus("HISTORY");
    auto& d = M5Cardputer.Display;
    const int y = APP_HELP_EDGE + 14;
    d.setTextSize(1);
    if (g_index.count == 0) {
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(5, y);
        d.print("No speed records");
        drawHelpHintRight("help");
        return;
    }
    g_history_selected = constrain(g_history_selected, 0, g_index.count - 1);
    const int first = min(max(0, g_history_selected - 3), max(0, static_cast<int>(g_index.count) - 7));
    for (int row = 0; row < 7 && first + row < g_index.count; ++row) {
        const int idx = first + row;
        const RunMeta& run = g_index.runs[idx];
        const int ry = y + row * 13;
        d.setTextColor(idx == g_history_selected ? BLACK : APP_COLOR_LABEL,
                       idx == g_history_selected ? APP_COLOR_MENU_KEY : BLACK);
        d.setCursor(5, ry);
        d.printf("%c%02d %08lu %5.1fkm %5.1f", idx == g_history_selected ? '>' : ' ', idx + 1,
                 static_cast<unsigned long>(run.utc_date),
                 static_cast<double>(run.distance_m / 1000.0f), static_cast<double>(run.max_kmh));
    }
    drawAppScrollbar(d, y, 86, g_index.count, 7, first);
    drawHelpHintRight("help");
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
        return "ALT m";
    }
    if (g_chart_metric == ChartMetric::Accel) {
        return "ACC g";
    }
    return "SPEED km/h";
}

static void drawHistoryChart() {
    loadIndex();
    drawTopStatus("RECORD");
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
        d.setCursor(5, APP_HELP_EDGE + 14);
        d.print("record file missing");
        return;
    }
    RunMeta file_meta{};
    f.read(reinterpret_cast<uint8_t*>(&file_meta), sizeof(file_meta));

    constexpr int chart_x = 28;
    constexpr int chart_y = 31;
    constexpr int chart_w = 207;
    constexpr int chart_h = 70;
    constexpr int buckets = 103;
    float min_value = 1e9f;
    float max_value = -1e9f;
    float values[buckets]{};
    uint16_t counts[buckets]{};
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

    d.setTextSize(1);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(5, APP_HELP_EDGE + 14);
    d.printf("%s  MAX %.1f  %.2fkm", chartMetricName(), static_cast<double>(meta.max_kmh),
             static_cast<double>(meta.distance_m / 1000.0f));
    d.drawRect(chart_x, chart_y, chart_w, chart_h, APP_COLOR_MUTED);
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(1, chart_y);
    d.printf("%.0f", static_cast<double>(max_value));
    d.setCursor(1, chart_y + chart_h - 8);
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
    char duration[16];
    formatDuration(meta.duration_ms, duration, sizeof(duration));
    d.setTextColor(APP_COLOR_HINT, BLACK);
    d.setCursor(chart_x, chart_y + chart_h + 4);
    d.printf("0             %s", duration);
    drawHelpHintRight("help");
}

static void drawHelp() {
    static const AppHelpLine lines[] = {
        appHelpBadge("1..4", "live / sats / speed / history"),
        appHelpKey(' ', "start / stop speed record"),
        appHelpArrows("select history record"),
        appHelpKey('\n', "open selected curve"),
        appHelpKey('m', "curve speed / alt / accel"),
        appHelpKey('r', "reset live speed statistics"),
        appHelpKey('h', "help / close"),
        appHelpTextColored("Hardware", APP_COLOR_LABEL),
        appHelpText("AT6668 UART 115200 8N1"),
        appHelpText("G2 TX -> yellow RX"),
        appHelpText("G1 RX <- white TX"),
        appHelpText("GPS is long-term speed reference."),
        appHelpText("IMU smooths short transients only."),
        appHelpLabelText("REC", APP_COLOR_ERROR, " 1-second binary samples"),
    };
    const int count = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
    drawAppHelpLines("GPS", lines, count, g_help_page);
}

static void redraw() {
    if (g_page == GpsPage::Live) {
        drawLive();
    } else if (g_page == GpsPage::Satellites) {
        drawSatellites();
    } else if (g_page == GpsPage::Speed) {
        drawSpeed();
    } else if (g_page == GpsPage::History) {
        drawHistory();
    } else if (g_page == GpsPage::HistoryChart) {
        drawHistoryChart();
    } else {
        drawHelp();
    }
}

static void setPage(const GpsPage page) {
    g_page = page;
    g_last_draw_ms = 0;
    redraw();
}

} // namespace

void enterGpsApp() {
    leaveGpsApp();
    g_fix = {};
    g_nmea_len = 0;
    g_page = GpsPage::Live;
    g_history_selected = 0;
    g_last_draw_ms = 0;
    g_last_imu_ms = 0;
    g_gravity[0] = 0;
    g_gravity[1] = 0;
    g_gravity[2] = 1;
    resetSpeedStats();
    loadIndex();
    M5.Imu.update();
    g_imu_ok = M5.Imu.isEnabled();
    // 先释放 Grove I2C 外设，再把同一组 G1/G2 交给 UART。
    M5Cardputer.Ex_I2C.release();
    g_gps_serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
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
    if (g_page == GpsPage::Help || g_page == GpsPage::HistoryChart ||
        g_page == GpsPage::History) {
        return;
    }
    if (millis() - g_last_draw_ms >= GPS_DRAW_MS) {
        g_last_draw_ms = millis();
        redraw();
    }
}

bool closeGpsHelp() {
    if (g_page != GpsPage::Help) {
        return false;
    }
    setPage(g_page_before_help);
    return true;
}

bool isGpsHelpVisible() {
    return g_page == GpsPage::Help;
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
            static constexpr int line_count = 14;
            g_help_page = applyHelpPageDelta(g_help_page, appHelpPageCount(line_count), delta);
            drawHelp();
        }
        return;
    }

    if (status.space) {
        if (g_recording) {
            stopRecording();
        } else {
            startRecording();
        }
        redraw();
        return;
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
    }
    for (const char raw : status.word) {
        const char c =
            (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == '1') {
            setPage(GpsPage::Live);
        } else if (c == '2') {
            setPage(GpsPage::Satellites);
        } else if (c == '3') {
            setPage(GpsPage::Speed);
        } else if (c == '4') {
            setPage(GpsPage::History);
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

