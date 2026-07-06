#pragma once

// 全局 header 高度与内容区起始坐标
static constexpr int APP_HEADER_H = 28;
static constexpr int APP_CONTENT_X = 4;
static constexpr int APP_CONTENT_Y = APP_HEADER_H + 2;

// 子界面 header：应用名 + BtnA(GO) 返回按钮
void drawAppScreenHeader(const char* title);

// 主菜单 header：Logo + 应用名 + 电量块 + 分页圆点
void drawMenuScreenHeader(const char* app_name, int page, int page_count);

// 仅刷新主菜单 header 电量块（主菜单定时调用）
void updateMenuScreenBattery(int page_count);

// 清屏并绘制子界面 header
void beginAppScreen(const char* title);
