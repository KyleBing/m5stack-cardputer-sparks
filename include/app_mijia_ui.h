#pragma once

#include "app_config.h"
#include "mijia_control.h"
#include <cstdint>

static constexpr int MIJIA_ICON_BASE = 16; // 16x16 设计基准
static constexpr int MIJIA_ICON_SCALE_DEFAULT = 1;
static constexpr int MIJIA_ICON_SCALE_LIST = 2;        // 概览列表矢量图标倍数
static constexpr int MIJIA_LIST_VISIBLE_COUNT = 2;     // 概览每页设备数
static constexpr int MIJIA_LIST_ICON_PX = 36;          // 概览列表图标边长
static constexpr int MIJIA_TAG_H = 12;
static constexpr int MIJIA_TAG_H_2X = 20;              // 2x 字号 tag 高度
static constexpr int MIJIA_PANEL_TEXT_SIZE = 1;        // 控制页右栏控制项字号
static constexpr int MIJIA_PANEL_NAME_TEXT_SIZE = 2;   // 控制页设备名字号
static constexpr int MIJIA_DEVICE_NAME_TOP_MARGIN = 0; // 设备名距内容区顶部
static constexpr int MIJIA_PANEL_RIGHT_PAD = 10;        // 控制页右栏右边距
static constexpr int MIJIA_PANEL_ICON_UP_OFFSET = 5;    // 左栏图标上移
static constexpr int MIJIA_PANEL_BAR_TEXT_SIZE = 2;    // 进度条说明与数值字号
// 列表项高度：缩放图标 + 三行文字
static constexpr int MIJIA_LIST_ITEM_H = 42;
static constexpr int MIJIA_LIST_ITEM_GAP = 6;

// 倍数换算为像素边长
inline int mijiaIconPx(const int scale) { return MIJIA_ICON_BASE * scale; }

// 按设备类型绘制简笔图标（scale 为相对 16x16 的倍数）
void drawMijiaDeviceIcon(MijiaDevKind kind, int x, int y, uint16_t color,
                         int scale = MIJIA_ICON_SCALE_DEFAULT);

// 按 model 匹配 PNG；active 为开关态，失败时回退矢量图标
void drawMijiaDeviceIconFor(const MijiaDevice* dev, MijiaDevKind kind, int x, int y,
                            uint16_t color, bool active,
                            int scale = MIJIA_ICON_SCALE_DEFAULT, float png_scale = 1.0f);

// 圆角 tag，active 时高亮；返回占用宽度（含间距）
int drawMijiaStatusTag(int x, int y, const char* text, bool active, uint16_t active_bg,
                       int text_size = 1);

// 百分比进度条（0-100）
void drawMijiaPercentBar(int x, int y, int w, int h, int percent, uint16_t fill_color);

// 带刻度线的百分比进度条（tick_count 为刻度数量，含首尾）
void drawMijiaScaledPercentBar(int x, int y, int w, int h, int percent, uint16_t fill_color,
                               int tick_count = 11);

// 说明在左、数值右对齐（上行）；下行进度条（percent 0-100）
int drawMijiaBarRow(int x, int y, const char* label, const char* value, int percent, int total_w,
                    uint16_t fill_color);

// 分段档位条（level 1..max_level，0 表示全灭）
void drawMijiaLevelSegments(int x, int y, int w, int h, int level, int max_level,
                            uint16_t fill_color);

// 控制页主面板：左大图标 + 开关状态，右设备信息与控制；net_status 非空时先显示网络状态
int drawMijiaDevicePanel(const MijiaDevice* dev, MijiaDevKind kind, int device_idx,
                         int device_count, const MijiaUiState& ui, int x, int y,
                         const char* net_status = nullptr);

// ON/OFF 双 tag；inline_status 为 false 时不绘制行尾状态字
void drawMijiaPowerTags(int x, int y, bool known, bool on, const char* status,
                        bool inline_status = true, int text_size = 1);

// 按设备类型绘制控制区（右栏）；返回下一行 y
int drawMijiaDeviceControls(const MijiaDevice* dev, MijiaDevKind kind, const MijiaUiState& ui,
                            int x, int y, int w);
