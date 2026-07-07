#include "app_mijia.h"
#include "app_common.h"
#include "app_config.h"
#include "app_header.h"
#include "mijia_control.h"
#include <cstring>

static int mijiaDeviceIdx = 0;
static MijiaUiState mijiaUi{};

static const MijiaDevice* getCurrentMijiaDevice() {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count == 0) {
        return nullptr;
    }
    if (mijiaDeviceIdx < 0) {
        mijiaDeviceIdx = 0;
    }
    if (mijiaDeviceIdx >= cfg.device_count) {
        mijiaDeviceIdx = cfg.device_count - 1;
    }
    return &cfg.devices[mijiaDeviceIdx];
}

// 查询当前设备状态
static void refreshMijiaDevice() {
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        strncpy(mijiaUi.status, "no device", sizeof(mijiaUi.status));
        mijiaUi.power_known = false;
        return;
    }

    if (!ensureConfigWifi()) {
        strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        mijiaUi.power_known = false;
        return;
    }

    strncpy(mijiaUi.status, "query...", sizeof(mijiaUi.status));
    drawMijiaApp();
    mijiaRefreshDevice(dev, mijiaUi);
}

// 设置当前设备开关
static void setMijiaPower(const bool on) {
    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        strncpy(mijiaUi.status, "no device", sizeof(mijiaUi.status));
        return;
    }

    if (!ensureConfigWifi()) {
        strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        return;
    }

    drawMijiaApp();
    mijiaSetDevicePower(dev, mijiaUi, on);
}

// 按设备类型绘制额外状态行
static void drawMijiaExtraLines(const MijiaDevice* dev, int& y) {
    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    char buf[32];

    switch (kind) {
        case MijiaDevKind::LIGHT:
            if (mijiaUi.extra_known) {
                snprintf(buf, sizeof(buf), "%d%%", mijiaUi.bright);
                drawInfoLine(APP_CONTENT_X, y, "bright", buf);
            }
            break;
        case MijiaDevKind::FAN_P5:
            if (mijiaUi.extra_known) {
                snprintf(buf, sizeof(buf), "%d%%", mijiaUi.speed);
                drawInfoLine(APP_CONTENT_X, y, "speed", buf);
                drawInfoLine(APP_CONTENT_X, y, "roll", mijiaUi.roll ? "ON" : "OFF");
                drawInfoLine(APP_CONTENT_X, y, "mode", mijiaUi.mode == 1 ? "nature" : "normal");
            }
            break;
        case MijiaDevKind::FAN_GENERIC:
            if (mijiaUi.extra_known) {
                snprintf(buf, sizeof(buf), "L%d", mijiaUi.speed);
                drawInfoLine(APP_CONTENT_X, y, "speed", buf);
            }
            break;
        case MijiaDevKind::AIR_PURIFIER_F20: {
            static const char* MODE_NAMES[] = {"auto", "sleep", "low", "med", "high", "fav"};
            if (mijiaUi.extra_known) {
                const int mi = constrain(mijiaUi.mode, 0, 5);
                drawInfoLine(APP_CONTENT_X, y, "mode", MODE_NAMES[mi]);
                snprintf(buf, sizeof(buf), "%d", mijiaUi.fan_level);
                drawInfoLine(APP_CONTENT_X, y, "fan", buf);
                snprintf(buf, sizeof(buf), "%d", mijiaUi.aqi);
                drawInfoLine(APP_CONTENT_X, y, "aqi", buf);
            }
            break;
        }
        default:
            break;
    }
}

// 按设备类型绘制操作提示
static void drawMijiaHints(const MijiaDevice* dev, int y) {
    const MijiaDevKind kind = mijiaClassifyModel(dev->model);

    M5Cardputer.Display.setTextColor(LIGHTGREY, BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("o on f off t toggle");
    y += INFO_LINE_H;
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.println("r refresh , . switch");
    y += INFO_LINE_H;

    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    switch (kind) {
        case MijiaDevKind::LIGHT:
            M5Cardputer.Display.println("-/+ bright");
            break;
        case MijiaDevKind::FAN_P5:
            M5Cardputer.Display.println("9/0 spd w roll m mode");
            break;
        case MijiaDevKind::FAN_GENERIC:
            M5Cardputer.Display.println("1-4 speed level");
            break;
        case MijiaDevKind::AIR_PURIFIER_F20:
            M5Cardputer.Display.println("1-5 mode 9/0 fan lv");
            break;
        default:
            break;
    }
}

void drawMijiaApp() {
    beginAppScreen("Mijia");
    M5Cardputer.Display.setTextSize(1);

    int y = APP_CONTENT_Y;
    const AppConfig& cfg = getAppConfig();
    const MijiaDevice* dev = getCurrentMijiaDevice();

    if (!cfg.loaded || dev == nullptr) {
        drawInfoLine(APP_CONTENT_X, y, "cfg", "none");
        drawInfoLine(APP_CONTENT_X, y, "hint", "press u web");
        return;
    }

    char buf[40];
    snprintf(buf, sizeof(buf), "%s [%d/%d]", dev->name, mijiaDeviceIdx + 1, cfg.device_count);
    drawInfoLine(APP_CONTENT_X, y, "dev", buf);

    if (mijiaUi.power_known) {
        drawInfoLine(APP_CONTENT_X, y, "power", mijiaUi.power_on ? "ON" : "OFF");
    } else {
        drawInfoLine(APP_CONTENT_X, y, "power", "?");
    }

    drawMijiaExtraLines(dev, y);
    drawInfoLine(APP_CONTENT_X, y, "status", mijiaUi.status);
    drawMijiaHints(dev, y);
}

void enterMijiaApp() {
    mijiaDeviceIdx = 0;
    mijiaResetUiState(mijiaUi);
    strncpy(mijiaUi.status, "connecting", sizeof(mijiaUi.status));
    drawMijiaApp();
    refreshMijiaDevice();
    drawMijiaApp();
}

void handleMijiaApp(const String& key) {
    const AppConfig& cfg = getAppConfig();
    if (!cfg.loaded || cfg.device_count == 0) {
        return;
    }

    const MijiaDevice* dev = getCurrentMijiaDevice();
    if (dev == nullptr) {
        return;
    }

    if (!ensureConfigWifi()) {
        strncpy(mijiaUi.status, "wifi fail", sizeof(mijiaUi.status));
        drawMijiaApp();
        return;
    }

    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    bool handled = true;

    if (key == "o") {
        setMijiaPower(true);
    } else if (key == "f") {
        setMijiaPower(false);
    } else if (key == "t") {
        setMijiaPower(!mijiaUi.power_on);
    } else if (key == "r") {
        refreshMijiaDevice();
    } else if (key == "," || key == ";") {
        mijiaDeviceIdx = (mijiaDeviceIdx - 1 + cfg.device_count) % cfg.device_count;
        mijiaResetUiState(mijiaUi);
        refreshMijiaDevice();
    } else if (key == "." || key == "/") {
        mijiaDeviceIdx = (mijiaDeviceIdx + 1) % cfg.device_count;
        mijiaResetUiState(mijiaUi);
        refreshMijiaDevice();
    } else if (kind == MijiaDevKind::LIGHT &&
               (key == "-" || key == "_" || key == "+" || key == "=")) {
        const int delta = (key == "-" || key == "_") ? -10 : 10;
        mijiaAdjustBright(dev, mijiaUi, delta);
    } else if (kind == MijiaDevKind::FAN_P5) {
        if (key == "9") {
            mijiaAdjustFanP5Speed(dev, mijiaUi, -10);
        } else if (key == "0") {
            mijiaAdjustFanP5Speed(dev, mijiaUi, 10);
        } else if (key == "w") {
            mijiaToggleFanP5Roll(dev, mijiaUi);
        } else if (key == "m") {
            mijiaToggleFanP5Mode(dev, mijiaUi);
        } else {
            handled = false;
        }
    } else if (kind == MijiaDevKind::FAN_GENERIC && key.length() == 1 && key[0] >= '1' &&
               key[0] <= '4') {
        mijiaSetFanSpeedLevel(dev, mijiaUi, key[0] - '0');
    } else if (kind == MijiaDevKind::AIR_PURIFIER_F20) {
        if (key.length() == 1 && key[0] >= '1' && key[0] <= '5') {
            mijiaSetPurifierMode(dev, mijiaUi, key[0] - '1');
        } else if (key == "9") {
            mijiaAdjustPurifierFanLevel(dev, mijiaUi, -1);
        } else if (key == "0") {
            mijiaAdjustPurifierFanLevel(dev, mijiaUi, 1);
        } else {
            handled = false;
        }
    } else {
        handled = false;
    }

    if (handled) {
        drawMijiaApp();
    }
}
