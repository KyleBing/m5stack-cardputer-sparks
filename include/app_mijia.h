#pragma once

#include "M5Cardputer.h"
#include <WString.h>

void enterMijiaApp();
// status：全屏提示文案（进入其他 BLE 应用用 "Entering."，真正退出用 "Exiting."）
void leaveMijiaApp(const char* status = "Exiting.");
void drawMijiaApp();
void handleMijiaApp(const String& key);
// Q 快速选择 / Fn+Q 编辑快捷键；在这些模式下吞掉按键
bool handleMijiaHotkeyUi(const Keyboard_Class::KeysState& status);
// 快速选择 / 快捷键编辑 / Exiting：无 Header，抑制主循环 header 刷新
bool mijiaAppSuppressesHeader();
// 概览导航：宫格方向键选设备、[ ] 翻页；编组切组/翻页；回车回控制页
bool handleMijiaOverviewPageNav(const Keyboard_Class::KeysState& status);
// 控制页切换设备（方向键 / ; , . / / [ ]）
bool handleMijiaDeviceNav(const Keyboard_Class::KeysState& status);
// 主循环调用：应用异步查询结果并重绘
void updateMijiaApp();
// 每帧调用：BtnA 切换当前设备/编组开关（控制页 / Grid / Groups）；快捷键编辑页确认
void pollMijiaBtnA();
// Help / 设备信息可见时关闭并重绘；否则返回 false
bool closeMijiaHelp();
