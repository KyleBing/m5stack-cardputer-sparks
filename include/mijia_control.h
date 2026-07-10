#pragma once

#include "app_config.h"

// 设备类型（按 model 字符串识别）
enum class MijiaDevKind {
    GENERIC,
    LIGHT,
    FAN_P5,
    FAN_GENERIC,
    AIR_PURIFIER_F20,
    AIR_FRYER,
    PLUG,
};

MijiaDevKind mijiaClassifyModel(const char* model);

// 当前设备 UI 状态
struct MijiaUiState {
    bool power_known;
    bool power_on;
    bool extra_known;
    int bright;      // 灯 1-100
    int color_temp;  // 灯色温 K
    bool ct_known;
    int ct_min;
    int ct_max;
    int speed;       // 风扇 0-100 或 1-4
    bool roll;       // 风扇摇头
    int mode;        // 净化器 0-5
    int fan_level;   // 净化器风速 0-5
    int aqi;         // 净化器 AQI
    char status[48];
};

void mijiaResetUiState(MijiaUiState& state);

// 刷新当前设备状态
void mijiaRefreshDevice(const MijiaDevice* dev, MijiaUiState& state);

// 设置开关（按设备类型选正确 miIO 方法）
void mijiaSetDevicePower(const MijiaDevice* dev, MijiaUiState& state, bool on);

// 灯：调节亮度（delta 可正可负）
void mijiaAdjustBright(const MijiaDevice* dev, MijiaUiState& state, int delta);

// 灯：直接设亮度百分比 1-100
void mijiaSetBrightPercent(const MijiaDevice* dev, MijiaUiState& state, int percent);

// 灯：是否支持色温调节
bool mijiaLightSupportsCt(const char* model);

// 灯：调节色温（delta_k 可正可负，单位 K）
void mijiaAdjustColorTemp(const MijiaDevice* dev, MijiaUiState& state, int delta_k);

// 灯：直接设色温 K
void mijiaSetColorTemp(const MijiaDevice* dev, MijiaUiState& state, int kelvin);

// 风扇 P5：调节风速
void mijiaAdjustFanP5Speed(const MijiaDevice* dev, MijiaUiState& state, int delta);

// 风扇 P5：摇头开关
void mijiaToggleFanP5Roll(const MijiaDevice* dev, MijiaUiState& state);

// 风扇 P5：切换模式
void mijiaToggleFanP5Mode(const MijiaDevice* dev, MijiaUiState& state);

// 通用风扇：设档位 1-4
void mijiaSetFanSpeedLevel(const MijiaDevice* dev, MijiaUiState& state, int level);

// 净化器 F20：设模式 0-5
void mijiaSetPurifierMode(const MijiaDevice* dev, MijiaUiState& state, int mode);

// 净化器 F20：调节风速档
void mijiaAdjustPurifierFanLevel(const MijiaDevice* dev, MijiaUiState& state, int delta);
