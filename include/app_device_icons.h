#pragma once

#include "app_config.h"
#include "mijia_control.h"

// data/icon/device 打包后的原生尺寸图标目录
static constexpr const char* DEVICE_ICON_NATIVE_DIR = "/icon/device";
static constexpr int DEVICE_ICON_NATIVE_PX = 70;

// 按 model 子串匹配图标名；无匹配返回 "default"
const char* deviceIconBasenameForModel(const char* model);

// 以 nullptr 结尾的图标 basename 列表（较长名在前，与匹配顺序一致）
const char* const* deviceIconNames();

// 生成 /icon/device/{basename}[_active].png 路径（静态缓冲，勿并发使用）
const char* deviceIconPathForModel(const char* model, bool active);

// 设备图标绘制边长（原生 PNG）
int deviceIconDrawPx(const MijiaDevice* dev);

// 按路径 1:1 原始尺寸绘制（左上角对齐）
bool drawDevicePngNative(const char* path, int x, int y);

// 绘制设备图标：按 model 匹配 PNG，active 为开关态；失败返回 false
bool drawDeviceIconFor(const MijiaDevice* dev, int x, int y, bool active);

// 无设备信息时绘制默认图标
bool drawDeviceIconDefault(int x, int y, bool active);

// 设备 PNG 资源是否已在 LittleFS 中
bool deviceIconsAvailable();
