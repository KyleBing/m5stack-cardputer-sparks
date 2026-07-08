#include "mijia_control.h"
#include "miio_client.h"
#include <cstring>

static int clampInt(const int value, const int min_v, const int max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static bool startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

MijiaDevKind mijiaClassifyModel(const char* model) {
    if (model == nullptr || model[0] == '\0') {
        return MijiaDevKind::GENERIC;
    }
    if (startsWith(model, "yeelink.light.")) {
        return MijiaDevKind::LIGHT;
    }
    if (strcmp(model, "dmaker.fan.p5") == 0) {
        return MijiaDevKind::FAN_P5;
    }
    if (strstr(model, ".fan.") != nullptr) {
        return MijiaDevKind::FAN_GENERIC;
    }
    if (strcmp(model, "dmaker.airpurifier.f20") == 0) {
        return MijiaDevKind::AIR_PURIFIER_F20;
    }
    if (strstr(model, "airfryer") != nullptr || strstr(model, ".fryer.") != nullptr) {
        return MijiaDevKind::AIR_FRYER;
    }
    if (strstr(model, ".plug.") != nullptr) {
        return MijiaDevKind::PLUG;
    }
    return MijiaDevKind::GENERIC;
}

void mijiaResetUiState(MijiaUiState& state) {
    state.power_known = false;
    state.power_on = false;
    state.extra_known = false;
    state.bright = 50;
    state.speed = 0;
    state.roll = false;
    state.mode = 0;
    state.fan_level = 0;
    state.aqi = 0;
    strncpy(state.status, "ready", sizeof(state.status));
}

static void applyResult(MijiaUiState& state, const MiioResult& result) {
    strncpy(state.status, result.message, sizeof(state.status) - 1);
    state.status[sizeof(state.status) - 1] = '\0';
}

// 状态查询失败时统一显示 timeout
static void applyRefreshResult(MijiaUiState& state, const MiioResult& result) {
    if (!result.ok) {
        strncpy(state.status, "timeout", sizeof(state.status));
        return;
    }
    applyResult(state, result);
}

void mijiaRefreshDevice(const MijiaDevice* dev, MijiaUiState& state) {
    if (dev == nullptr) {
        strncpy(state.status, "no device", sizeof(state.status));
        state.power_known = false;
        state.extra_known = false;
        return;
    }

    strncpy(state.status, "query...", sizeof(state.status));
    state.power_known = false;
    state.extra_known = false;

    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    MiioResult result{};

    switch (kind) {
        case MijiaDevKind::LIGHT: {
            bool bright_known = false;
            result = miioGetLightStatus(dev->ip, dev->token, state.power_on, state.bright,
                                        bright_known);
            if (result.ok) {
                state.power_known = true;
                state.extra_known = bright_known;
            }
            break;
        }
        case MijiaDevKind::FAN_P5:
            result = miioFanP5GetStatus(dev->ip, dev->token, state.power_on, state.speed,
                                        state.roll, state.mode);
            if (result.ok) {
                state.power_known = true;
                state.extra_known = true;
            }
            break;
        case MijiaDevKind::FAN_GENERIC:
            result = miioFanGetStatus(dev->ip, dev->token, state.power_on, state.speed);
            if (result.ok) {
                state.power_known = true;
                state.extra_known = true;
            }
            break;
        case MijiaDevKind::AIR_PURIFIER_F20:
            result = miioF20GetStatus(dev->ip, dev->token, dev->id, state.power_on, state.mode,
                                      state.fan_level, state.aqi);
            if (result.ok) {
                state.power_known = true;
                state.extra_known = true;
            }
            break;
        default: {
            bool power = false;
            result = miioGetPower(dev->ip, dev->token, power);
            if (result.ok) {
                state.power_known = true;
                state.power_on = power;
            }
            break;
        }
    }

    applyRefreshResult(state, result);
}

void mijiaSetDevicePower(const MijiaDevice* dev, MijiaUiState& state, const bool on) {
    if (dev == nullptr) {
        strncpy(state.status, "no device", sizeof(state.status));
        return;
    }

    strncpy(state.status, on ? "turn on..." : "turn off...", sizeof(state.status));

    const MijiaDevKind kind = mijiaClassifyModel(dev->model);
    MiioResult result{};

    switch (kind) {
        case MijiaDevKind::FAN_P5:
            result = miioFanP5SetPower(dev->ip, dev->token, on);
            break;
        case MijiaDevKind::AIR_PURIFIER_F20:
            result = miioF20SetPower(dev->ip, dev->token, dev->id, on);
            break;
        default:
            result = miioSetPower(dev->ip, dev->token, on);
            break;
    }

    if (result.ok) {
        state.power_known = true;
        state.power_on = on;
    }
    applyResult(state, result);
}

void mijiaAdjustBright(const MijiaDevice* dev, MijiaUiState& state, const int delta) {
    if (dev == nullptr) {
        return;
    }

    int target = state.extra_known ? state.bright : 50;
    target = clampInt(target + delta, 1, 100);
    mijiaSetBrightPercent(dev, state, target);
}

void mijiaSetBrightPercent(const MijiaDevice* dev, MijiaUiState& state, const int percent) {
    if (dev == nullptr) {
        return;
    }

    const int target = clampInt(percent, 1, 100);
    strncpy(state.status, "bright...", sizeof(state.status));
    const MiioResult result = miioSetBright(dev->ip, dev->token, target);
    if (result.ok) {
        state.extra_known = true;
        state.bright = target;
        state.power_on = true;
        state.power_known = true;
    }
    applyResult(state, result);
}

void mijiaAdjustFanP5Speed(const MijiaDevice* dev, MijiaUiState& state, const int delta) {
    if (dev == nullptr) {
        return;
    }

    int target = state.extra_known ? state.speed : 30;
    target = clampInt(target + delta, 0, 100);

    strncpy(state.status, "speed...", sizeof(state.status));
    const MiioResult result = miioFanP5SetSpeed(dev->ip, dev->token, target);
    if (result.ok) {
        state.extra_known = true;
        state.speed = target;
        state.power_known = true;
        state.power_on = target > 0;
    }
    applyResult(state, result);
}

void mijiaToggleFanP5Roll(const MijiaDevice* dev, MijiaUiState& state) {
    if (dev == nullptr) {
        return;
    }

    const bool next = !state.roll;
    strncpy(state.status, "roll...", sizeof(state.status));
    const MiioResult result = miioFanP5SetRoll(dev->ip, dev->token, next);
    if (result.ok) {
        state.roll = next;
        state.extra_known = true;
    }
    applyResult(state, result);
}

void mijiaToggleFanP5Mode(const MijiaDevice* dev, MijiaUiState& state) {
    if (dev == nullptr) {
        return;
    }

    // mode 字段：0=normal 1=nature
    const int next = state.mode == 1 ? 0 : 1;
    const char* mode = next == 1 ? "nature" : "normal";

    strncpy(state.status, "mode...", sizeof(state.status));
    const MiioResult result = miioFanP5SetMode(dev->ip, dev->token, mode);
    if (result.ok) {
        state.mode = next;
        state.extra_known = true;
    }
    applyResult(state, result);
}

void mijiaSetFanSpeedLevel(const MijiaDevice* dev, MijiaUiState& state, const int level) {
    if (dev == nullptr) {
        return;
    }

    const int lv = clampInt(level, 1, 4);
    strncpy(state.status, "speed...", sizeof(state.status));
    const MiioResult result = miioFanSetSpeedLevel(dev->ip, dev->token, lv);
    if (result.ok) {
        state.speed = lv;
        state.extra_known = true;
        state.power_known = true;
        state.power_on = true;
    }
    applyResult(state, result);
}

void mijiaSetPurifierMode(const MijiaDevice* dev, MijiaUiState& state, const int mode) {
    if (dev == nullptr) {
        return;
    }

    const int m = clampInt(mode, 0, 5);
    strncpy(state.status, "mode...", sizeof(state.status));
    const MiioResult result = miioF20SetMode(dev->ip, dev->token, dev->id, m);
    if (result.ok) {
        state.mode = m;
        state.extra_known = true;
        state.power_known = true;
        state.power_on = true;
    }
    applyResult(state, result);
}

void mijiaAdjustPurifierFanLevel(const MijiaDevice* dev, MijiaUiState& state, const int delta) {
    if (dev == nullptr) {
        return;
    }

    int target = state.extra_known ? state.fan_level : 1;
    target = clampInt(target + delta, 0, 5);

    strncpy(state.status, "fan lv...", sizeof(state.status));
    const MiioResult result = miioF20SetFanLevel(dev->ip, dev->token, dev->id, target);
    if (result.ok) {
        state.fan_level = target;
        state.extra_known = true;
    }
    applyResult(state, result);
}
