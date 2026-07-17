#include "app_mic.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_config.h"
#include "app_connectivity.h"
#include "app_header.h"
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>

// Cardputer microSD SPI
static constexpr int MIC_SD_SCK = 40;
static constexpr int MIC_SD_MISO = 39;
static constexpr int MIC_SD_MOSI = 14;
static constexpr int MIC_SD_CS = 12;
static constexpr const char* MIC_SD_DIR = "/audioRecord";
static constexpr uint32_t MIC_SAMPLE_RATE = 16000;
static constexpr size_t MIC_CAPTURE_N = 512;
static constexpr int MIC_PLOT_N = 48;
static constexpr uint32_t MIC_WIFI_TIMEOUT_MS = 10000;
static constexpr uint32_t MIC_NTP_TIMEOUT_MS = 8000;
static constexpr uint32_t MIC_MSG_HOLD_MS = 2500;

enum class MicTimeSync : uint8_t {
    Idle = 0,
    BeginWifi,
    WaitWifi,
    BeginNtp,
    WaitNtp,
    Done,
};

static M5Canvas micSpr(&M5Cardputer.Display);
static bool micSprOk = false;
static bool micHeaderReady = false;
static bool micHelpVisible = false;
static int micUserGain = 1; // 1/2/4/8/16

static MicTimeSync micSyncState = MicTimeSync::Idle;
static uint32_t micSyncDeadlineMs = 0;
static uint32_t micHeaderStatusMs = 0;

static bool micSdReady = false;
static bool micSdChecked = false;
static bool micRecording = false;
static File micRecFile;
static uint32_t micRecStartMs = 0;
static uint32_t micRecDataBytes = 0;
static char micStatusMsg[20] = "";
static uint32_t micStatusMsgUntilMs = 0;

static int16_t micSamples[MIC_CAPTURE_N];
static uint32_t micAmpHold = 3000;
static int micPeakHoldPx = 0;
static uint8_t micLiveBlink = 0;

// ---- WAV 头（PCM 16bit mono）----
#pragma pack(push, 1)
struct MicWavHeader {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
};
#pragma pack(pop)

static void micFillWavHeader(MicWavHeader& h, const uint32_t data_bytes) {
    memcpy(h.riff, "RIFF", 4);
    h.file_size = 36 + data_bytes;
    memcpy(h.wave, "WAVE", 4);
    memcpy(h.fmt, "fmt ", 4);
    h.fmt_size = 16;
    h.audio_format = 1;
    h.num_channels = 1;
    h.sample_rate = MIC_SAMPLE_RATE;
    h.byte_rate = MIC_SAMPLE_RATE * 2;
    h.block_align = 2;
    h.bits_per_sample = 16;
    memcpy(h.data, "data", 4);
    h.data_size = data_bytes;
}

static void micSetStatusMsg(const char* msg) {
    strncpy(micStatusMsg, msg, sizeof(micStatusMsg) - 1);
    micStatusMsg[sizeof(micStatusMsg) - 1] = '\0';
    micStatusMsgUntilMs = millis() + MIC_MSG_HOLD_MS;
}

static bool micStatusMsgActive() {
    return micStatusMsg[0] != '\0' && static_cast<int32_t>(millis() - micStatusMsgUntilMs) < 0;
}

// 尝试挂载 SD；失败返回 false
static bool micEnsureSd() {
    if (micSdReady) {
        return true;
    }
    SPI.begin(MIC_SD_SCK, MIC_SD_MISO, MIC_SD_MOSI, MIC_SD_CS);
    if (!SD.begin(MIC_SD_CS, SPI, 25000000)) {
        micSdReady = false;
        micSdChecked = true;
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        micSdReady = false;
        micSdChecked = true;
        return false;
    }
    if (!SD.exists(MIC_SD_DIR)) {
        SD.mkdir(MIC_SD_DIR);
    }
    micSdReady = true;
    micSdChecked = true;
    return true;
}

static void micReleaseSd() {
    if (micSdReady) {
        SD.end();
        micSdReady = false;
    }
    micSdChecked = false;
}

static bool micHasValidTime(struct tm& out) {
    applyLocalTimezone();
    const time_t now = time(nullptr);
    if (now > 1600000000 && localtime_r(&now, &out) != nullptr) {
        return true;
    }
    if (M5.Rtc.isEnabled()) {
        const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
        if (dt.date.year >= 2020) {
            struct tm utc{};
            utc.tm_year = dt.date.year - 1900;
            utc.tm_mon = dt.date.month - 1;
            utc.tm_mday = dt.date.date;
            utc.tm_hour = dt.time.hours;
            utc.tm_min = dt.time.minutes;
            utc.tm_sec = dt.time.seconds;
            utc.tm_isdst = 0;
            setenv("TZ", "GMT0", 1);
            tzset();
            const time_t epoch = mktime(&utc);
            applyLocalTimezone();
            if (epoch > 1600000000 && localtime_r(&epoch, &out) != nullptr) {
                return true;
            }
        }
    }
    return false;
}

static void micBuildRecPath(char* path, const size_t path_len) {
    struct tm ti{};
    if (micHasValidTime(ti)) {
        snprintf(path, path_len, "%s/%04d%02d%02d_%02d%02d%02d.wav", MIC_SD_DIR, ti.tm_year + 1900,
                 ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        snprintf(path, path_len, "%s/rec_%lu.wav", MIC_SD_DIR,
                 static_cast<unsigned long>(millis()));
    }
}

static bool micFinalizeRecording() {
    if (!micRecording) {
        return false;
    }
    micRecording = false;
    if (!micRecFile) {
        micRecDataBytes = 0;
        return false;
    }
    MicWavHeader hdr{};
    micFillWavHeader(hdr, micRecDataBytes);
    micRecFile.seek(0);
    micRecFile.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    micRecFile.flush();
    micRecFile.close();
    micRecDataBytes = 0;
    return true;
}

static bool micStartRecording() {
    if (micRecording || micHelpVisible) {
        return false;
    }
    if (!micEnsureSd()) {
        micSetStatusMsg("no SD");
        return false;
    }

    char path[48];
    micBuildRecPath(path, sizeof(path));
    // 同名已存在则改用时间戳文件名
    if (SD.exists(path)) {
        snprintf(path, sizeof(path), "%s/rec_%lu.wav", MIC_SD_DIR,
                 static_cast<unsigned long>(millis()));
    }

    micRecFile = SD.open(path, FILE_WRITE);
    if (!micRecFile) {
        micSetStatusMsg("no SD");
        return false;
    }

    MicWavHeader hdr{};
    micFillWavHeader(hdr, 0);
    if (micRecFile.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        micRecFile.close();
        micSetStatusMsg("no SD");
        return false;
    }

    micRecDataBytes = 0;
    micRecStartMs = millis();
    micRecording = true;
    micStatusMsg[0] = '\0';
    return true;
}

static void micToggleRecording() {
    if (micHelpVisible) {
        return;
    }
    if (micRecording) {
        micFinalizeRecording();
    } else {
        micStartRecording();
    }
}

// 后台 WiFi + NTP（不阻塞主循环）
static void micUpdateTimeSync() {
    const AppConfig& cfg = getAppConfig();
    switch (micSyncState) {
        case MicTimeSync::Idle:
        case MicTimeSync::Done:
            return;
        case MicTimeSync::BeginWifi: {
            if (!cfg.loaded || cfg.wifi_ssid[0] == '\0') {
                micSyncState = MicTimeSync::Done;
                return;
            }
            struct tm ti{};
            // 已有可用时间则跳过联网
            if (micHasValidTime(ti)) {
                micSyncState = MicTimeSync::Done;
                return;
            }
            if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.wifi_ssid) {
                micSyncState = MicTimeSync::BeginNtp;
                return;
            }
            WiFi.mode(WIFI_STA);
            applyWifiRadioSleepPolicy();
            WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
            micSyncDeadlineMs = millis() + MIC_WIFI_TIMEOUT_MS;
            micSyncState = MicTimeSync::WaitWifi;
            break;
        }
        case MicTimeSync::WaitWifi:
            if (WiFi.status() == WL_CONNECTED) {
                micSyncState = MicTimeSync::BeginNtp;
            } else if (static_cast<int32_t>(millis() - micSyncDeadlineMs) >= 0) {
                releaseConfigWifi();
                micSyncState = MicTimeSync::Done;
            }
            break;
        case MicTimeSync::BeginNtp:
            configTzTime(getAppTimezone(), "ntp.aliyun.com", "pool.ntp.org", "time.windows.com");
            micSyncDeadlineMs = millis() + MIC_NTP_TIMEOUT_MS;
            micSyncState = MicTimeSync::WaitNtp;
            break;
        case MicTimeSync::WaitNtp: {
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
                micSyncState = MicTimeSync::Done;
            } else if (static_cast<int32_t>(millis() - micSyncDeadlineMs) >= 0) {
                releaseConfigWifi();
                micSyncState = MicTimeSync::Done;
            }
            break;
        }
    }

    // 联网期间刷新 header WiFi 图标
    if (micSyncState == MicTimeSync::WaitWifi || micSyncState == MicTimeSync::WaitNtp) {
        if (millis() - micHeaderStatusMs >= 500) {
            micHeaderStatusMs = millis();
            updateAppHeaderStatus();
        }
    }
}

static bool micSyncBusy() {
    return micSyncState == MicTimeSync::WaitWifi || micSyncState == MicTimeSync::WaitNtp ||
           micSyncState == MicTimeSync::BeginWifi || micSyncState == MicTimeSync::BeginNtp;
}

static int micSampleToY(const int16_t sample, const int centerY, const int halfH,
                        const uint32_t ampHold, const int userGain) {
    const int64_t scaled = static_cast<int64_t>(sample) * userGain;
    int y = centerY - static_cast<int>(scaled * (halfH - 2) / static_cast<int>(ampHold));
    return constrain(y, centerY - halfH + 1, centerY + halfH - 1);
}

static int drawMicHelpColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

static int drawMicHelpKeyAt(const int x, int y, const char key, const char* text) {
    int cx = x;
    cx += drawKeyBadge(cx, y, key, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawMicHelpBadgeAt(const int x, int y, const char* badge, const char* text) {
    int cx = x;
    cx += drawTextBadge(cx, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

static int drawMicHelpTextAt(const int x, int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 10;
}

static void drawMicHelpPage() {
    beginAppScreen("Mic");
    const int screen_w = M5Cardputer.Display.width();
    constexpr int col_gap = 4;
    const int col_w = (screen_w - col_gap) / 2;
    const int keys_x = 0;
    const int notes_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    const int content_h = M5Cardputer.Display.height() - col_y;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y, content_h, DARKGREY);

    int y = drawMicHelpColHeader(keys_x, col_y, col_w, "keymap");
    const int kx = keys_x + 2;
    y = drawMicHelpKeyAt(kx, y, 'r', "rec start/stop");
    y = drawMicHelpBadgeAt(kx, y, "BtnA", "rec start/stop");
    y = drawMicHelpKeyAt(kx, y, '-', "gain down");
    y = drawMicHelpKeyAt(kx, y, '=', "gain up");
    y = drawMicHelpKeyAt(kx, y, 'h', "help / close");

    y = drawMicHelpColHeader(notes_x, col_y, screen_w - notes_x, "manual");
    const int nx = notes_x + 2;
    y = drawMicHelpTextAt(nx, y, "need microSD");
    y = drawMicHelpTextAt(nx, y, "save /audioRecord");
    y = drawMicHelpTextAt(nx, y, "WAV 16k mono");
    y = drawMicHelpTextAt(nx, y, "WiFi syncs time");
    y = drawMicHelpTextAt(nx, y, "for file name");
    y = drawMicHelpTextAt(nx, y, "no SD -> tip row");
    y = drawMicHelpTextAt(nx, y, "r/BtnA toggles");

    drawHelpHintRight("close");
    updateAppHeaderStatus();
}

static void drawMicScope() {
    constexpr int kMeterW = 16;
    constexpr int kStatusH = 11;
    constexpr int kPad = 2;
    constexpr uint16_t kGridColor = 0x2104;
    constexpr uint16_t kWaveColor = CYAN;
    constexpr uint16_t kWaveDim = 0x0478;

    const int screenW = M5Cardputer.Display.width();
    const int screenH = M5Cardputer.Display.height();
    const int statusY = APP_CONTENT_Y;
    const int contentH = screenH - statusY;
    const int waveTop = kStatusH;
    const int waveH = contentH - waveTop - kPad;
    const int waveW = screenW - kMeterW - kPad - 1;
    const int halfH = waveH / 2;
    const int centerY = waveTop + halfH;
    const int meterX = waveW + 1;
    const int meterInnerX = meterX + 3;
    const int meterInnerW = kMeterW - 6;
    const int barBottom = waveTop + waveH;

    if (!M5Cardputer.Mic.isEnabled()) {
        beginAppScreen("Mic");
        micHeaderReady = false;
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_Y);
        M5Cardputer.Display.println("not found");
        return;
    }

    if (!micHeaderReady) {
        beginAppScreen("Mic");
        micHeaderReady = true;
        micSprOk = false;
    }

    if (!micSprOk) {
        micSpr.setColorDepth(16);
        if (!micSpr.createSprite(screenW, contentH)) {
            return;
        }
        micSprOk = true;
    }

    if (!M5Cardputer.Mic.record(micSamples, MIC_CAPTURE_N, MIC_SAMPLE_RATE, false)) {
        return;
    }

    // 录音中同步写 SD
    if (micRecording && micRecFile) {
        const size_t nbytes = MIC_CAPTURE_N * sizeof(int16_t);
        const size_t wrote =
            micRecFile.write(reinterpret_cast<const uint8_t*>(micSamples), nbytes);
        if (wrote == nbytes) {
            micRecDataBytes += static_cast<uint32_t>(wrote);
        } else {
            // 写入失败：收尾并提示
            micFinalizeRecording();
            micSetStatusMsg("no SD");
        }
    }

    int32_t framePeak = 0;
    int64_t sumSq = 0;
    for (size_t i = 0; i < MIC_CAPTURE_N; i++) {
        const int32_t v = micSamples[i];
        const int32_t a = abs(v);
        if (a > framePeak) {
            framePeak = a;
        }
        sumSq += static_cast<int64_t>(v) * v;
    }
    const int32_t frameRms =
        static_cast<int32_t>(sqrtf(static_cast<float>(sumSq / static_cast<int64_t>(MIC_CAPTURE_N))));

    if (framePeak > static_cast<int32_t>(micAmpHold)) {
        micAmpHold = static_cast<uint32_t>(framePeak);
    } else {
        micAmpHold = micAmpHold * 15 / 16 + static_cast<uint32_t>(framePeak) / 16;
    }
    if (micAmpHold < 800) {
        micAmpHold = 800;
    }

    size_t sync = 0;
    for (size_t i = 1; i + 64 < MIC_CAPTURE_N; i++) {
        if (micSamples[i - 1] < 0 && micSamples[i] >= 0) {
            sync = i;
            break;
        }
    }
    const size_t viewN = min(static_cast<size_t>(192), MIC_CAPTURE_N - sync);
    const size_t win = max(static_cast<size_t>(1), viewN / static_cast<size_t>(MIC_PLOT_N));

    micSpr.fillSprite(BLACK);

    micLiveBlink = static_cast<uint8_t>(micLiveBlink + 1);
    const bool clip = framePeak > 30000;
    const float db =
        framePeak > 0 ? 20.0f * log10f(static_cast<float>(framePeak) / 32768.0f) : -60.0f;
    const float dbShow = db < -60.0f ? -60.0f : db;

    // ---- 状态行：LIVE/REC + 时长/WiFi/no SD + dB + 增益 ----
    micSpr.setTextSize(1);
    const bool show_msg = micStatusMsgActive();
    int cx = 2;
    if (micRecording) {
        if ((micLiveBlink & 0x02) == 0) {
            micSpr.fillCircle(cx + 3, 4, 3, RED);
        } else {
            micSpr.drawCircle(cx + 3, 4, 3, DARKGREY);
        }
        cx += 10;
        micSpr.setTextColor(APP_COLOR_ERROR, BLACK);
        micSpr.setCursor(cx, 0);
        micSpr.print("REC");
        cx += 22;
        const uint32_t elapsed = (millis() - micRecStartMs) / 1000;
        const uint32_t mm = elapsed / 60;
        const uint32_t ss = elapsed % 60;
        micSpr.setTextColor(YELLOW, BLACK);
        micSpr.setCursor(cx, 0);
        micSpr.printf("%lu:%02lu", static_cast<unsigned long>(mm), static_cast<unsigned long>(ss));
        cx += 36;
    } else {
        if ((micLiveBlink & 0x04) == 0) {
            micSpr.fillCircle(cx + 3, 4, 3, RED);
        } else {
            micSpr.drawCircle(cx + 3, 4, 3, DARKGREY);
        }
        cx += 10;
        micSpr.setTextColor(APP_COLOR_HINT, BLACK);
        micSpr.setCursor(cx, 0);
        micSpr.print("LIVE");
        cx += 28;
        if (micSyncBusy()) {
            micSpr.setTextColor(CYAN, BLACK);
            micSpr.setCursor(cx, 0);
            micSpr.print("WiFi");
            cx += 28;
        }
    }

    if (show_msg) {
        micSpr.setTextColor(APP_COLOR_WARN, BLACK);
        micSpr.setCursor(cx, 0);
        micSpr.print(micStatusMsg);
    } else if (clip) {
        micSpr.setTextColor(APP_COLOR_ERROR, BLACK);
        micSpr.setCursor(cx, 0);
        micSpr.print("CLIP");
    } else {
        micSpr.setTextColor(YELLOW, BLACK);
        micSpr.setCursor(cx, 0);
        micSpr.printf("%4.0fdB", static_cast<double>(dbShow));
    }

    micSpr.setTextColor(CYAN, BLACK);
    micSpr.setCursor(waveW - 28, 0);
    micSpr.printf("x%d", micUserGain);

    for (int g = 1; g < 4; g++) {
        const int gy = waveTop + (waveH * g) / 4;
        micSpr.drawFastHLine(0, gy, waveW, kGridColor);
    }
    for (int g = 1; g < 4; g++) {
        const int gx = (waveW * g) / 4;
        micSpr.drawFastVLine(gx, waveTop, waveH, kGridColor);
    }
    micSpr.drawFastHLine(0, centerY, waveW, DARKGREY);
    micSpr.drawRect(0, waveTop, waveW, waveH, DARKGREY);

    int prevX = 1;
    int prevY = micSampleToY(micSamples[sync], centerY, halfH, micAmpHold, micUserGain);
    for (int i = 0; i < MIC_PLOT_N; i++) {
        const size_t base = sync + static_cast<size_t>(i) * win;
        int32_t acc = 0;
        size_t n = 0;
        for (size_t j = 0; j < win && base + j < MIC_CAPTURE_N; j++) {
            acc += micSamples[base + j];
            n++;
        }
        const int16_t avg = n > 0 ? static_cast<int16_t>(acc / static_cast<int32_t>(n)) : 0;
        const int x = 1 + (i * (waveW - 3)) / (MIC_PLOT_N - 1);
        const int y = micSampleToY(avg, centerY, halfH, micAmpHold, micUserGain);
        const int fillTop = min(centerY, y);
        const int fillH = abs(y - centerY);
        if (fillH > 1 && (i & 1) == 0) {
            micSpr.drawFastVLine(x, fillTop, fillH, kWaveDim);
        }
        micSpr.drawLine(prevX, prevY, x, y, kWaveColor);
        prevX = x;
        prevY = y;
    }

    constexpr int kSegH = 3;
    constexpr int kSegGap = 1;
    const int segPitch = kSegH + kSegGap;
    const int segCount = max(1, waveH / segPitch);
    // 常用 dBFS 对数表头：-60 dB 到 0 dB 映射到底部到顶部
    const float rmsDb =
        frameRms > 0 ? 20.0f * log10f(static_cast<float>(frameRms) / 32768.0f) : -60.0f;
    const float peakDb =
        framePeak > 0 ? 20.0f * log10f(static_cast<float>(framePeak) / 32768.0f) : -60.0f;
    const int rmsPx =
        constrain(static_cast<int>((rmsDb + 60.0f) * waveH / 60.0f + 0.5f), 0, waveH);
    const int peakPx =
        constrain(static_cast<int>((peakDb + 60.0f) * waveH / 60.0f + 0.5f), 0, waveH);
    if (peakPx > micPeakHoldPx) {
        micPeakHoldPx = peakPx;
    } else if (micPeakHoldPx > 0) {
        micPeakHoldPx -= 1;
    }
    const int litSegs = constrain((rmsPx * segCount + waveH - 1) / waveH, 0, segCount);

    micSpr.drawRect(meterX, waveTop, kMeterW, waveH, DARKGREY);
    for (int s = 0; s < litSegs; s++) {
        const float pct = static_cast<float>(s + 1) / static_cast<float>(segCount);
        uint16_t c = APP_COLOR_OK;
        if (pct > 0.85f) {
            c = APP_COLOR_ERROR;
        } else if (pct > 0.62f) {
            c = YELLOW;
        }
        const int sy = barBottom - (s + 1) * segPitch + kSegGap;
        micSpr.fillRect(meterInnerX, sy, meterInnerW, kSegH, c);
    }
    if (micPeakHoldPx > 0) {
        const int holdY = constrain(barBottom - micPeakHoldPx, waveTop, barBottom - 1);
        micSpr.drawFastHLine(meterX + 1, holdY, kMeterW - 2, YELLOW);
    }

    micSpr.pushSprite(0, statusY);
}

void enterMicApp() {
    leaveMicApp();
    micHelpVisible = false;
    micUserGain = micUserGain < 1 ? 1 : micUserGain;
    micStatusMsg[0] = '\0';
    micSyncState = MicTimeSync::BeginWifi;
    micHeaderReady = false;
    micSprOk = false;
    beginAppScreen("Mic");
    micHeaderReady = true;
    updateMicApp();
}

void leaveMicApp() {
    if (micRecording) {
        micFinalizeRecording();
    }
    if (micSprOk) {
        micSpr.deleteSprite();
        micSprOk = false;
    }
    if (micSyncBusy() || micSyncState == MicTimeSync::BeginWifi ||
        micSyncState == MicTimeSync::BeginNtp) {
        releaseConfigWifi();
    }
    micSyncState = MicTimeSync::Done;
    micReleaseSd();
    micHeaderReady = false;
    micHelpVisible = false;
}

void updateMicApp() {
    micUpdateTimeSync();
    if (micHelpVisible) {
        return;
    }
    drawMicScope();
}

void handleMicApp(const String& key) {
    for (unsigned i = 0; i < key.length(); i++) {
        const char c = key[i];
        if (c == 'h' || c == 'H') {
            micHelpVisible = !micHelpVisible;
            if (micHelpVisible) {
                // help 时停录音，避免后台丢块
                if (micRecording) {
                    micFinalizeRecording();
                }
                if (micSprOk) {
                    micSpr.deleteSprite();
                    micSprOk = false;
                }
                drawMicHelpPage();
            } else {
                micHeaderReady = false;
                updateMicApp();
            }
            return;
        }
        if (micHelpVisible) {
            continue;
        }
        if (c == 'r' || c == 'R') {
            micToggleRecording();
        } else if (c == '-' || c == ',') {
            if (micUserGain > 1) {
                micUserGain >>= 1;
            }
        } else if (c == '=' || c == '+' || c == '.') {
            if (micUserGain < 16) {
                micUserGain <<= 1;
            }
        }
    }
}

void pollMicBtnA() {
    if (!M5Cardputer.BtnA.wasPressed()) {
        return;
    }
    if (micHelpVisible) {
        return;
    }
    micToggleRecording();
}
