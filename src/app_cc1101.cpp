#include "app_cc1101.h"
#include "app_common.h"
#include "app_header.h"
#include <RadioLib.h>
#include <SPI.h>
#include <cstring>

namespace {

// Cardputer EXT14 SPI（与 microSD 同总线，CS 独立）
static constexpr int CC1101_SCK = 40;
static constexpr int CC1101_MISO = 39;
static constexpr int CC1101_MOSI = 14;
static constexpr int CC1101_CS = 13;   // EXT14 G13
static constexpr int CC1101_GDO0 = 15; // EXT14 G15，RX/TX 完成中断
static constexpr int CC1101_GDO2 = 5;  // EXT14 G5，可选载波侦测

static constexpr float CC1101_FREQ_MIN = 387.0f;
static constexpr float CC1101_FREQ_MAX = 464.0f;
static constexpr float CC1101_FREQ_STEP = 0.25f;
static constexpr float CC1101_FREQ_DEFAULT = 433.92f;

static SPIClass g_cc1101_spi(FSPI);
static SPISettings g_cc1101_spi_settings(2000000, MSBFIRST, SPI_MODE0);
static CC1101 g_radio =
    new Module(CC1101_CS, CC1101_GDO0, RADIOLIB_NC, CC1101_GDO2, g_cc1101_spi, g_cc1101_spi_settings);

static bool g_spi_ready = false;
static bool g_chip_ok = false;
static bool g_help_visible = false;
static bool g_listening = false;
static uint32_t g_listen_start_ms = 0;
static constexpr uint32_t CC1101_RX_TIMEOUT_MS = 3000;
static float g_freq_mhz = CC1101_FREQ_DEFAULT;
static float g_rssi_dbm = 0.0f;
static char g_status_msg[48] = "not init";
static char g_last_rx[40] = "--";
static uint32_t g_last_draw_ms = 0;
static uint32_t g_last_rssi_ms = 0;

static void setCc1101Status(const char* msg) {
    strncpy(g_status_msg, msg, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = '\0';
}

static void ensureCc1101Spi() {
    if (g_spi_ready) {
        return;
    }
    g_cc1101_spi.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    g_spi_ready = true;
}

static void stopCc1101Radio() {
    g_listening = false;
    if (g_chip_ok) {
        g_radio.standby();
    }
}

static bool initCc1101Chip() {
    ensureCc1101Spi();
    stopCc1101Radio();
    g_chip_ok = false;
    g_rssi_dbm = 0.0f;
    strncpy(g_last_rx, "--", sizeof(g_last_rx));

    // 433MHz 建议 ≤20kbps；4.8kbps 兼顾距离与稳定性
    const int state = g_radio.begin(g_freq_mhz, 4.8f, 4.8f, 58.0f, 10, 32);
    if (state != RADIOLIB_ERR_NONE) {
        snprintf(g_status_msg, sizeof(g_status_msg), "init err %d", state);
        return false;
    }
    g_chip_ok = true;
    setCc1101Status("ready");
    return true;
}

static void readCc1101Rssi() {
    if (!g_chip_ok) {
        return;
    }
    g_rssi_dbm = g_radio.getRSSI();
}

static void pollCc1101Rx() {
    if (!g_chip_ok || !g_listening) {
        return;
    }
    if (g_radio.available()) {
        const int state =
            g_radio.readData(reinterpret_cast<uint8_t*>(g_last_rx), sizeof(g_last_rx) - 1);
        g_last_rx[sizeof(g_last_rx) - 1] = '\0';
        g_listening = false;
        g_radio.standby();
        if (state == RADIOLIB_ERR_NONE) {
            setCc1101Status("rx ok");
        } else {
            snprintf(g_status_msg, sizeof(g_status_msg), "rx err %d", state);
        }
        return;
    }
    if (millis() - g_listen_start_ms >= CC1101_RX_TIMEOUT_MS) {
        g_listening = false;
        g_radio.standby();
        setCc1101Status("rx timeout");
    }
}

static void sendCc1101Ping() {
    if (!g_chip_ok) {
        setCc1101Status("no chip");
        return;
    }
    stopCc1101Radio();
    static uint8_t seq = 0;
    char payload[24];
    snprintf(payload, sizeof(payload), "CP-%u", static_cast<unsigned>(++seq));
    const int state = g_radio.transmit(payload);
    if (state == RADIOLIB_ERR_NONE) {
        snprintf(g_status_msg, sizeof(g_status_msg), "tx %s", payload);
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "tx err %d", state);
    }
    g_radio.standby();
}

static void drawCc1101Hints() {
    const int hint_y = 135 - 12;
    int cx = APP_CONTENT_X;
    cx += drawKeyBadge(cx, hint_y, 'r', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("init ");
    cx += 28;
    cx += drawKeyBadge(cx, hint_y, 't', 1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("tx ");
    cx += 18;
    cx += drawKeyBadge(cx, hint_y, 'l', 1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("rx");
    drawHelpHintRight("help", 0);
}

static void drawCc1101Screen() {
    beginAppScreen("CC1101");
    int y = APP_CONTENT_INSET_Y;
    const int x = APP_CONTENT_X;

    M5Cardputer.Display.setTextSize(1);
    drawInfoLine(x, y, "Chip", g_chip_ok ? "CC1101 OK" : "NOT FOUND");
    char freq_buf[16];
    snprintf(freq_buf, sizeof(freq_buf), "%.2f MHz", g_freq_mhz);
    drawInfoLine(x, y, "Freq", freq_buf);

    char rssi_buf[16];
    if (g_chip_ok) {
        snprintf(rssi_buf, sizeof(rssi_buf), "%.1f dBm", g_rssi_dbm);
    } else {
        snprintf(rssi_buf, sizeof(rssi_buf), "--");
    }
    drawInfoLine(x, y, "RSSI", rssi_buf);
    drawInfoLine(x, y, "Mode", g_listening ? "listening" : "idle");
    drawInfoLine(x, y, "Stat", g_status_msg);
    drawInfoLine(x, y, "Last", g_last_rx);

    M5Cardputer.Display.setTextColor(APP_COLOR_MUTED, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print("EXT14: CS G13 GDO0 G15");
    y += INFO_LINE_H;
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print("SPI G40/G39/G14  3.3V!");

    drawCc1101Hints();
    g_last_draw_ms = millis();
}

static void drawCc1101Help() {
    int y = drawAppHelpBegin("CC1101");
    constexpr int x = APP_HELP_CONTENT_X;
    y = drawAppHelpTextColored(x, y, "Wiring (EXT14)", APP_COLOR_LABEL);
    y = drawAppHelpLabelText(x, y, "VCC", APP_COLOR_ERROR, " 3.3V only (NOT 5V)");
    y = drawAppHelpLabelText(x, y, "GND", APP_COLOR_LABEL, " GND");
    y = drawAppHelpLabelText(x, y, "CSN", APP_COLOR_LABEL, " G13");
    y = drawAppHelpLabelText(x, y, "SCK", APP_COLOR_LABEL, " G40");
    y = drawAppHelpLabelText(x, y, "MOSI", APP_COLOR_LABEL, " G14");
    y = drawAppHelpLabelText(x, y, "MISO", APP_COLOR_LABEL, " G39");
    y = drawAppHelpLabelText(x, y, "GDO0", APP_COLOR_LABEL, " G15 (IRQ)");
    y = drawAppHelpKey(x, y, 'r', "re-init module");
    y = drawAppHelpKey(x, y, 't', "send test packet");
    y = drawAppHelpKey(x, y, 'l', "listen ~3s for packet");
    y = drawAppHelpArrows(x, y, "adjust frequency");
    y = drawAppHelpText(x, y, "Uses 433MHz Sub-GHz band.");
    y = drawAppHelpText(x, y, "Range ~380m open field.");
    drawHelpHintRight("close");
}

static void adjustCc1101Freq(const int delta_steps) {
    g_freq_mhz += static_cast<float>(delta_steps) * CC1101_FREQ_STEP;
    if (g_freq_mhz < CC1101_FREQ_MIN) {
        g_freq_mhz = CC1101_FREQ_MIN;
    }
    if (g_freq_mhz > CC1101_FREQ_MAX) {
        g_freq_mhz = CC1101_FREQ_MAX;
    }
    if (g_chip_ok) {
        const int state = g_radio.setFrequency(g_freq_mhz);
        if (state != RADIOLIB_ERR_NONE) {
            snprintf(g_status_msg, sizeof(g_status_msg), "freq err %d", state);
        } else {
            setCc1101Status("freq set");
        }
    }
}

} // namespace

void enterCc1101App() {
    leaveCc1101App();
    g_help_visible = false;
    g_listening = false;
    g_freq_mhz = CC1101_FREQ_DEFAULT;
    setCc1101Status("init...");
    initCc1101Chip();
    drawCc1101Screen();
}

void leaveCc1101App() {
    g_help_visible = false;
    stopCc1101Radio();
    g_chip_ok = false;
}

void updateCc1101App() {
    if (g_help_visible) {
        return;
    }
    const uint32_t now = millis();
    pollCc1101Rx();
    if (g_chip_ok && now - g_last_rssi_ms >= 500) {
        g_last_rssi_ms = now;
        readCc1101Rssi();
        if (now - g_last_draw_ms >= 500) {
            drawCc1101Screen();
        }
    }
}

void handleCc1101App(const Keyboard_Class::KeysState& status) {
    if (g_help_visible) {
        return;
    }
    for (const char raw : status.word) {
        const char c =
            (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'h') {
            g_help_visible = true;
            drawCc1101Help();
            return;
        }
        if (c == 'r') {
            setCc1101Status("init...");
            drawCc1101Screen();
            initCc1101Chip();
            drawCc1101Screen();
            return;
        }
        if (c == 't') {
            sendCc1101Ping();
            drawCc1101Screen();
            return;
        }
        if (c == 'l') {
            if (!g_chip_ok) {
                setCc1101Status("no chip");
                drawCc1101Screen();
                return;
            }
            stopCc1101Radio();
            const int state = g_radio.startReceive();
            if (state == RADIOLIB_ERR_NONE) {
                g_listening = true;
                g_listen_start_ms = millis();
                setCc1101Status("listening");
            } else {
                snprintf(g_status_msg, sizeof(g_status_msg), "rx start %d", state);
            }
            drawCc1101Screen();
            return;
        }
    }
    int delta = getMenuNavDelta(status);
    if (delta != 0) {
        adjustCc1101Freq(delta);
        drawCc1101Screen();
    }
}

bool closeCc1101Help() {
    if (!g_help_visible) {
        return false;
    }
    g_help_visible = false;
    drawCc1101Screen();
    return true;
}

bool isCc1101HelpVisible() {
    return g_help_visible;
}
