#pragma once

#include <stdint.h>

// 全局 header 高度与内容区起始坐标
static constexpr int APP_HEADER_H = 28;
static constexpr int APP_CONTENT_X = 4;
// 紧贴 header 下沿的内容起始 Y（无 5px 间隙）
static constexpr int APP_CONTENT_Y_NO_TAP_TO_HEADER = APP_HEADER_H;
static constexpr int APP_CONTENT_Y = APP_HEADER_H + 5;

// 子界面 header：应用名 + btngo 返回图标（右侧）
void drawAppScreenHeader(const char* title, bool draw_divider = true);
// 标题 + 次要色后缀（如 Infrared + TV）
void drawAppScreenHeaderAccent(const char* title, const char* accent, uint16_t accent_color,
                               bool draw_divider = true);

// 主菜单 header：Logo + 应用名 + 电量/连接状态 + 分页圆点
void drawMenuScreenHeader(const char* app_name, int page, int page_count);

// 刷新主菜单 header 状态区（电量 / WiFi / BLE）
void updateMenuHeaderStatus(int page_count);

// 刷新子界面 header 状态区（WiFi / BLE）
void updateAppHeaderStatus();

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
