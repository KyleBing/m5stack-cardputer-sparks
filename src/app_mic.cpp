#include "app_mic.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_header.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// 256 样点 ≈ 16ms；多缓冲 + 落后 2 块再取用（对齐 M5 Mic 双槽）
static constexpr uint32_t MIC_SAMPLE_RATE = 16000;
static constexpr size_t MIC_CAPTURE_N = 256;
static constexpr size_t MIC_BUF_COUNT = 8;
static constexpr int MIC_PLOT_N = 48;

enum class MicUiMode : uint8_t {
    Scope = 0,
    Help,
};

static M5Canvas micSpr(&M5Cardputer.Display);
static bool micSprOk = false;
static bool micHeaderReady = false;
static MicUiMode micUiMode = MicUiMode::Scope;
static int micUserGain = 1; // 1/2/4/8/16

// 多缓冲采集：record() 只入队，落后 2 块的缓冲才已填满可绘图
static int16_t micCapBuf[MIC_BUF_COUNT][MIC_CAPTURE_N];
static size_t micRecIdx = 2;
static size_t micDrawIdx = 0;
static uint8_t micSkipReady = 2;
static const int16_t* micViewSamples = nullptr;
static uint32_t micAmpHold = 3000;
static int micPeakHoldPx = 0;
static uint8_t micLiveBlink = 0;

// 开麦前先解除 hold，避免抢不到 G43/G46
static void micEnsureCaptureOn() {
    if (M5Cardputer.Mic.isRunning()) {
        return;
    }
    releaseAudioPinHolds();
    M5Cardputer.Mic.begin();
}

static void micResetCapturePipeline() {
    micRecIdx = 2;
    micDrawIdx = 0;
    micSkipReady = 2;
    micViewSamples = nullptr;
    memset(micCapBuf, 0, sizeof(micCapBuf));
}

// 入队下一块；成功时消费落后 2 块的就绪缓冲（Mic.record 为异步）
static bool micPollCapture() {
    if (!M5Cardputer.Mic.isRunning()) {
        return false;
    }
    int16_t* dest = micCapBuf[micRecIdx];
    if (!M5Cardputer.Mic.record(dest, MIC_CAPTURE_N, MIC_SAMPLE_RATE, false)) {
        return false;
    }
    if (micSkipReady > 0) {
        micSkipReady--;
    } else {
        micViewSamples = micCapBuf[micDrawIdx];
    }
    micDrawIdx = (micDrawIdx + 1) % MIC_BUF_COUNT;
    micRecIdx = (micRecIdx + 1) % MIC_BUF_COUNT;
    return true;
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
    y = drawMicHelpBadgeAt(kx, y, "-=", "gain down");
    y = drawMicHelpBadgeAt(kx, y, "=+", "gain up");
    y = drawMicHelpBadgeAt(kx, y, ",.", "gain too");
    y = drawMicHelpKeyAt(kx, y, 'h', "help / close");

    y = drawMicHelpColHeader(notes_x, col_y, screen_w - notes_x, "manual");
    const int nx = notes_x + 2;
    y = drawMicHelpTextAt(nx, y, "live scope + VU");
    y = drawMicHelpTextAt(nx, y, "gain x1..x16");
    y = drawMicHelpTextAt(nx, y, "16kHz mono PCM");
    y = drawMicHelpTextAt(nx, y, "CLIP = too loud");

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
    const int statusY = APP_CONTENT_INSET_Y;
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
        M5Cardputer.Display.setCursor(APP_CONTENT_X, APP_CONTENT_INSET_Y);
        M5Cardputer.Display.println("not found");
        return;
    }

    if (!micHeaderReady) {
        beginAppScreen("Mic");
        micHeaderReady = true;
        micSprOk = false;
        M5Cardputer.Display.fillRect(0, APP_CONTENT_Y, M5Cardputer.Display.width(),
                                     M5Cardputer.Display.height() - APP_CONTENT_Y, BLACK);
    }

    if (!micSprOk) {
        micSpr.setColorDepth(16);
        if (!micSpr.createSprite(screenW, contentH)) {
            return;
        }
        micSprOk = true;
    }

    // 采集：尽量填满 Mic 双槽；record 异步，仅消费已填满的落后缓冲
    micPollCapture();
    if (M5Cardputer.Mic.isRecording() < 2) {
        micPollCapture();
    }
    if (micViewSamples == nullptr) {
        return;
    }
    const int16_t* micSamples = micViewSamples;

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

    // 状态行：LIVE + dB / CLIP + 增益
    micSpr.setTextSize(1);
    int cx = 2;
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

    if (clip) {
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
    micUiMode = MicUiMode::Scope;
    if (micUserGain < 1) {
        micUserGain = 1;
    }
    micResetCapturePipeline();
    // 麦克与喇叭共用 I2S，进 Mic 先关喇叭并拉低脚
    releaseSpeakerQuiet();
    micEnsureCaptureOn();
    micHeaderReady = false;
    micSprOk = false;
    beginAppScreen("Mic");
    micHeaderReady = true;
    updateMicApp();
}

void leaveMicApp() {
    if (M5Cardputer.Mic.isRunning()) {
        uint32_t wait_ms = millis() + 80;
        while (M5Cardputer.Mic.isRecording() && static_cast<int32_t>(millis() - wait_ms) < 0) {
            delay(1);
        }
        M5Cardputer.Mic.end();
        delay(30); // 等 PDM/I2S 矩阵松开，再让 Speaker 抢回 G43
    }
    // 仅 hold 不够：须 Speaker begin→end 抢回共用脚（同进 Time 播键音后嗡嗡消失）
    reclaimAndReleaseSpeakerQuiet();
    if (micSprOk) {
        micSpr.deleteSprite();
        micSprOk = false;
    }
    micResetCapturePipeline();
    micHeaderReady = false;
    micUiMode = MicUiMode::Scope;
}

void updateMicApp() {
    if (micUiMode == MicUiMode::Help) {
        return;
    }
    drawMicScope();
}

void handleMicApp(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == 'h' || c == 'H') {
            if (micUiMode == MicUiMode::Help) {
                micUiMode = MicUiMode::Scope;
                micHeaderReady = false;
                micEnsureCaptureOn();
                micResetCapturePipeline();
                updateMicApp();
            } else {
                // Help 页不采麦，避免功放被 PDM 时钟干扰
                if (M5Cardputer.Mic.isRunning()) {
                    M5Cardputer.Mic.end();
                    delay(30);
                }
                reclaimAndReleaseSpeakerQuiet();
                if (micSprOk) {
                    micSpr.deleteSprite();
                    micSprOk = false;
                }
                micUiMode = MicUiMode::Help;
                drawMicHelpPage();
            }
            return;
        }
        if (micUiMode == MicUiMode::Help) {
            continue;
        }
        if (c == '-' || c == ',') {
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
