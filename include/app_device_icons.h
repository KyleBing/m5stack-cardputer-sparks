#pragma once

#include "app_config.h"
#include "mijia_control.h"

// LittleFS 中 /img 设备 PNG（由构建脚本从 assets/img 复制）
static constexpr const char* DEVICE_ICON_DIR = "/img";
// data/icon/device 打包后的原生尺寸图标目录
static constexpr const char* DEVICE_ICON_NATIVE_DIR = "/icon/device";
static constexpr int DEVICE_ICON_NATIVE_PX = 52;
static constexpr int DEVICE_ICON_SCALE_BASE = 16; // 与 MIJIA_ICON_BASE 一致

// Mijia 设备类型 → /img PNG 路径；无匹配返回 nullptr
const char* deviceIconPathForKind(MijiaDevKind kind);

// 按设备名称/型号匹配 /icon/device 原生 PNG；无匹配返回 nullptr
const char* deviceIconPathForDevice(const char* name, const char* model, MijiaDevKind kind);

// 设备图标绘制宽度：有原生 PNG 时为 52，否则为 scale 换算后的像素
int deviceIconDrawPx(const MijiaDevice* dev, MijiaDevKind kind, int scale);

// 从 LittleFS 绘制 PNG，缩放到 size×size；成功返回 true
bool drawDevicePngIcon(MijiaDevKind kind, int x, int y, int size);

// 按路径缩放绘制（Icons 展示页用）
bool drawDevicePngFile(const char* path, int x, int y, int size);

// 按路径 1:1 原始尺寸绘制（左上角对齐）
bool drawDevicePngNative(const char* path, int x, int y);

// 绘制设备图标：优先原生 PNG，其次 /img PNG，最后由调用方画矢量
bool drawDeviceIconFor(const MijiaDevice* dev, MijiaDevKind kind, int x, int y, int scale);

// 设备 PNG 资源是否已在 LittleFS 中
bool deviceIconsAvailable();
