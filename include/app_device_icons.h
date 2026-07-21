#pragma once

#include "app_config.h"
#include "mijia_control.h"

// data/icon/device 打包后的原生尺寸图标目录
static constexpr const char* DEVICE_ICON_NATIVE_DIR = "/icon/device";
static constexpr int DEVICE_ICON_NATIVE_PX = 70;
static constexpr int DEVICE_ICON_LIST_PX = 25; // 列表用 _25w.png 边长

// 按 model 子串匹配图标名；无匹配返回 "default"
const char* deviceIconBasenameForModel(const char* model);

// 以 nullptr 结尾的图标 basename 列表（较长名在前，与匹配顺序一致）
const char* const* deviceIconNames();

// 生成 /icon/device/{basename}[_active].png 路径（静态缓冲，勿并发使用）
const char* deviceIconPathForModel(const char* model, bool active);

// 生成列表用小图标路径：{basename}_25w.png / {basename}_active_25w.png
const char* deviceIconPathForModelList(const char* model, bool active);

// data 根目录打包后的 Version logo（优先 bake 的 .rgb565，缺失回退 .png）
static constexpr const char* APP_LOGO_60_PATH = "/logo_60.png";
static constexpr int APP_LOGO_60_PX = 60;

// 绘制 Version 页 logo（1:1 原生尺寸）
bool drawAppLogo60(const int x, const int y, const float scale = 1.0f);

// 按路径 1:1 原始尺寸绘制（左上角对齐）
bool drawDevicePngNative(const char* path, int x, int y);

// LittleFS 任意 PNG，按 scale 绘制（1.0 = 像素 1:1）
bool drawLittleFsPng(const char* path, int x, int y, float scale = 1.0f);

// 绘制设备图标：按 model 匹配 PNG，active 为开关态；失败返回 false
bool drawDeviceIconFor(const MijiaDevice* dev, int x, int y, bool active);

// 按 scale 缩放绘制 PNG 设备图标（1.0 为原生 70px）
bool drawDeviceIconForScaled(const MijiaDevice* dev, int x, int y, bool active, float scale);

// 绘制列表用 _25w.png 小图标（1:1，无缩放）；失败返回 false
bool drawDeviceIconForList(const MijiaDevice* dev, int x, int y, bool active, float scale = 1.0f);

// 无设备信息时绘制默认图标
bool drawDeviceIconDefault(int x, int y, bool active);

// 设备 PNG 资源是否已在 LittleFS 中
bool deviceIconsAvailable();

// 用 M5GFX 解码 PNG 并写入同名 .rgb565（黑底上的库输出，与屏上一致）
bool bakePngToRgb565File(const char* png_path);

// 批量烘焙 /icon/device、/icon/ir、logo；返回成功个数
int bakeAllPngIconsToRgb565();
