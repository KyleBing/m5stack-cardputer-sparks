#pragma once

#include "M5Cardputer.h"

// 官方 AT6668 GPS Unit（UART / NMEA 0183）应用。
void enterGpsApp();
void leaveGpsApp();
void updateGpsApp();
void handleGpsApp(const Keyboard_Class::KeysState& status);

// Help 打开时关闭并重绘当前 GPS 页面。
bool closeGpsHelp();
bool isGpsHelpVisible();
// 历史曲线页 ESC：回到历史列表（未在曲线页返回 false）。
bool closeGpsHistoryChart();
// Settings 页 ESC：回到进入前的页面（未在 Settings 返回 false）。
bool closeGpsSettings();

// 每帧轮询 BtnA：开始 / 停止录制（等同空格；Help 打开时忽略）
void pollGpsBtnA();

// 截图功能名：live / sats / speed / history / chart / settings / help
void getGpsShotFeature(char* out, size_t out_len);

