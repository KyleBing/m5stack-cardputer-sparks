#pragma once

#include "app_config.h"
#include "app_header.h"
#include "mijia_control.h"
#include <cstdint>

static constexpr int MIJIA_ICON_BASE = 16; // 16x16 设计基准
static constexpr int MIJIA_ICON_SCALE_DEFAULT = 1;
static constexpr int MIJIA_ICON_SCALE_LIST = 2;        // 概览列表矢量图标倍数
static constexpr int MIJIA_LIST_VISIBLE_COUNT = 3;     // 概览列表每页设备数
static constexpr int MIJIA_GRID_COLS = 3;             // 概览宫格列数
static constexpr int MIJIA_GRID_ROWS = 3;             // 概览宫格行数
static constexpr int MIJIA_GRID_PAGE_SIZE = MIJIA_GRID_COLS * MIJIA_GRID_ROWS;
static constexpr int MIJIA_LIST_ICON_PX = 28;          // 概览列表图标最大边长（随行高缩放）
static constexpr int MIJIA_TAG_H = 12;
static constexpr int MIJIA_TAG_H_2X = 20;              // 2x 字号 tag 高度
static constexpr int MIJIA_PANEL_TEXT_SIZE = 1;        // 控制页右栏控制项字号
static constexpr int MIJIA_PANEL_RIGHT_PAD = 10;        // 控制页右栏右边距
static constexpr int MIJIA_PANEL_ICON_LEFT = 10;        // 控制页左图标左边距
static constexpr int MIJIA_PANEL_ICON_INFO_GAP = 10;    // 图标与右栏文字间距
static constexpr int MIJIA_PANEL_BAR_TEXT_SIZE = 2;    // 进度条说明与数值字号
// Header 设备 indicator：每格 3x3，间隔 1px；高度最多 4 行
static constexpr int MIJIA_PAGER_CELL = 3;
static constexpr int MIJIA_PAGER_GAP = 1;
static constexpr int MIJIA_PAGER_MAX_ROWS = 4;
static constexpr uint16_t MIJIA_PAGER_COLOR_IDLE = 0x9492; // #929292
// 列表项高度：缩放图标 + 三行文字
static constexpr int MIJIA_LIST_ITEM_H = 42;
static constexpr int MIJIA_LIST_ITEM_GAP = 6;
static constexpr int MIJIA_LIST_NUM_MARGIN_R = 10; // 设备序号右侧间距
// 概览列表/宫格分隔线（比 APP_COLOR_MUTED 更暗）
static constexpr uint16_t MIJIA_DIVIDER_COLOR = 0x3186;

// 倍数换算为像素边长
inline int mijiaIconPx(const int scale) { return MIJIA_ICON_BASE * scale; }

// 按设备类型绘制简笔图标（scale 为相对 16x16 的倍数）
void drawMijiaDeviceIcon(MijiaDevKind kind, int x, int y, uint16_t color,
                         int scale = MIJIA_ICON_SCALE_DEFAULT);

// 按 model 匹配 PNG；active 为开关态，失败时回退矢量图标
void drawMijiaDeviceIconFor(const MijiaDevice* dev, MijiaDevKind kind, int x, int y,
                            uint16_t color, bool active,
                            int scale = MIJIA_ICON_SCALE_DEFAULT, float png_scale = 1.0f);

// 概览列表：优先 _25w.png，失败回退矢量图标；scale 为 PNG 缩放倍数
void drawMijiaDeviceIconForList(const MijiaDevice* dev, MijiaDevKind kind, int x, int y,
                                uint16_t color, bool active,
                                int scale = MIJIA_ICON_SCALE_LIST, float png_scale = 1.0f);

// 圆角 tag，active 时高亮；返回占用宽度（含间距）
int drawMijiaStatusTag(int x, int y, const char* text, bool active, uint16_t active_bg,
                       int text_size = 1);

// 宫格单元状态 tag 文案与配色
struct MijiaGridStatusTag {
    char text[12];
    bool active;
    uint16_t bg;
};

void mijiaFormatGridStatusTag(const MijiaUiState& ui, MijiaGridStatusTag& tag);

// 百分比进度条（0-100）
void drawMijiaPercentBar(int x, int y, int w, int h, int percent, uint16_t fill_color);

// 带刻度线的百分比进度条（tick_count 为刻度数量，含首尾）
void drawMijiaScaledPercentBar(int x, int y, int w, int h, int percent, uint16_t fill_color,
                               int tick_count = 11);

// 说明在左、数值右对齐（上行）；下行进度条（text_size / bar_h 可按设备压缩）
int drawMijiaBarRow(int x, int y, const char* label, const char* value, int percent, int total_w,
                    uint16_t fill_color, int text_size = MIJIA_PANEL_BAR_TEXT_SIZE,
                    int bar_h = 11);

// 分段档位条（level 1..max_level，0 表示全灭）
void drawMijiaLevelSegments(int x, int y, int w, int h, int level, int max_level,
                            uint16_t fill_color);

// Header 设备 indicator 尺寸（0=不显示）
int mijiaDevicePagerWidth(int device_count);
int mijiaDevicePagerHeight(int device_count);
// 绘制设备 indicator（current_idx 为黄块，其余灰块）
void drawMijiaDevicePager(int x, int y, int current_idx, int device_count);

// 控制页主面板：左大图标（内容区纵向居中）+ 右控制区（与图标顶对齐；设备名在 header）
int drawMijiaDevicePanel(const MijiaDevice* dev, MijiaDevKind kind, const MijiaUiState& ui, int x,
                         int y, const char* net_status = nullptr);

// 控制页布局（局部刷新用）
struct MijiaPanelLayout {
    int layout_y;
    int icon_px;
    int left_w;
    int content_h;
    int icon_x;
    int icon_y;
    int info_x;
    int info_w;
    int right_top_y; // 右栏控制区起点（与图标顶对齐）
};

MijiaPanelLayout calcMijiaPanelLayout(int panel_y, const MijiaDevice* dev, MijiaDevKind kind,
                                     const MijiaUiState& ui, const char* net_status = nullptr,
                                     int x = APP_CONTENT_X);

// 是否显示行内连接/查询状态
bool mijiaPanelShowsInlineStatus(const char* status, bool power_known);

// 绘制控制页左栏图标
void drawMijiaPanelIcon(const MijiaDevice* dev, MijiaDevKind kind, const MijiaPanelLayout& layout,
                        const MijiaUiState& ui);

// 绘制控制页右栏状态与控制区
void drawMijiaPanelRightColumn(const MijiaDevice* dev, MijiaDevKind kind,
                               const MijiaPanelLayout& layout, const MijiaUiState& ui,
                               const char* net_status = nullptr);

// 仅刷新炸锅状态行右侧剩余时间（不重绘整栏）
void drawMijiaFryerRemainTick(const MijiaUiState& ui, const MijiaPanelLayout& layout,
                              const char* net_status = nullptr);

// ON/OFF 双 tag；inline_status 为 false 时不绘制行尾状态字
void drawMijiaPowerTags(int x, int y, bool known, bool on, const char* status,
                        bool inline_status = true, int text_size = 1);

// 按设备类型绘制控制区（右栏）；返回下一行 y
int drawMijiaDeviceControls(const MijiaDevice* dev, MijiaDevKind kind, const MijiaUiState& ui,
                            int x, int y, int w);

// 控制页：设备开启时贴紧 header 画 2px 状态框（左下/右下 4px 圆角）；关闭则擦除
void drawMijiaControlPowerBorder(bool power_on);
