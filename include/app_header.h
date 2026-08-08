#pragma once

#include <stdint.h>

// 全局 header 高度与内容区坐标
static constexpr int APP_HEADER_H = 28;
static constexpr int APP_CONTENT_X = 4;
// header 右侧状态图标（电池 / WiFi / BLE / 分页圆点）之间的最小间距
static constexpr int APP_HEADER_ICON_GAP = 5;
// 内容区顶边 = header 下沿（背景填充从这里开始，含 padding 带）
static constexpr int APP_CONTENT_Y = APP_HEADER_H;
// 内容绘制时自行留出的上内边距（旧版 APP_CONTENT_Y 与内容区顶边的差值）
static constexpr int APP_CONTENT_PAD_Y = 5;
// 实际内容布局起始 Y（文本、卡片等）
static constexpr int APP_CONTENT_INSET_Y = APP_CONTENT_Y + APP_CONTENT_PAD_Y;
// 贴 header 下沿的满铺布局（无 padding）
static constexpr int APP_CONTENT_Y_NO_TAP_TO_HEADER = APP_CONTENT_Y;

// 主菜单 / Games / Test 等 hub 页卡片网格（尺寸一致）
static constexpr int APP_HUB_CARD_W = 111;
static constexpr int APP_HUB_CARD_H = 22;
static constexpr int APP_HUB_CARD_GAP_X = 8;
static constexpr int APP_HUB_CARD_GAP_Y = 4;
static constexpr int APP_HUB_CARD_ORIGIN_X = 5;
static constexpr int APP_HUB_CARD_ORIGIN_Y = APP_CONTENT_INSET_Y - 3; // 相对默认 inset 上移 3px
static constexpr int APP_HUB_CARD_COLS = 2;

// 子界面 header：应用名 + 状态图标（右侧）
void drawAppScreenHeader(const char* title, bool draw_divider = true);
// 标题 + 次要色后缀（如 Infrared + TV）
void drawAppScreenHeaderAccent(const char* title, const char* accent, uint16_t accent_color,
                               bool draw_divider = true);
// 米家控制页：左上角设备 indicator + 设备名；右侧 BLE
void drawAppScreenHeaderWithDevicePager(const char* title, int device_idx, int device_count,
                                        bool draw_divider = true);
void beginAppScreenWithDevicePager(const char* title, int device_idx, int device_count,
                                   bool draw_divider = true);
// 清除 header 设备 indicator（离开控制页时调用）
void clearAppHeaderDevicePager();

// 主菜单 header：Logo + 应用名 + 电量/连接状态 + 分页圆点
void drawMenuScreenHeader(const char* app_name, int page, int page_count);

// 仅刷新主菜单分页圆点（翻页时用，不碰电量区）
void updateMenuPageDots(int page, int page_count);

// 刷新主菜单 header 状态区（电量 / WiFi / BLE）
void updateMenuHeaderStatus(int page_count);

// 刷新子界面 header 状态区（WiFi / BLE 等）；仅在共享 header 已绘制且未 suppress 时生效
void updateAppHeaderStatus();

// 绘制共享 header 时自动置位；全屏/无 header 界面应清除，避免定时刷状态图标
void clearAppHeaderStatusRefresh();
bool isAppHeaderStatusRefreshEnabled();

// 仅刷新主菜单 header 电量块（兼容旧调用）
void updateMenuScreenBattery(int page_count);

// 清屏并绘制子界面 header
void beginAppScreen(const char* title, bool draw_divider = true);
// 清屏并绘制带电池的子界面 header（如 Cursor）
void beginAppScreenWithBattery(const char* title, bool draw_divider = true);
// 清屏并绘制带电池 + 次要色后缀的 header
void beginAppScreenAccentWithBattery(const char* title, const char* accent, uint16_t accent_color,
                                     bool draw_divider = true);
// 仅重绘带电池的 header（不清屏，用于翻页改副标题）
void drawAppScreenHeaderWithBattery(const char* title, bool draw_divider = true);
void drawAppScreenHeaderAccentWithBattery(const char* title, const char* accent,
                                          uint16_t accent_color, bool draw_divider = true);
// 清屏并绘制带次要色后缀的 header
void beginAppScreenAccent(const char* title, const char* accent, uint16_t accent_color,
                          bool draw_divider = true);

// 仅清除 header 下方内容区（局部刷新用）
void clearAppContentArea();
// 填充整块内容区背景（从 header 下沿铺满，含 padding 带）
void fillAppContentArea(uint16_t color);
// Hub 页：header（无下边框）+ 内容区背景；page_count > 1 时 header 右侧画分页圆点
void beginAppHubScreen(const char* title, uint16_t content_bg, int page = 0, int page_count = 1);
