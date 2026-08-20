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

