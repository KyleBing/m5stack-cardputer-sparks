#pragma once

#include "M5Cardputer.h"

void enterWifiApp();
void drawWifiApp();
void updateWifiApp();
void handleWifiApp(const Keyboard_Class::KeysState& status);
// 各页都带 header，无需屏蔽定时状态刷新
bool wifiAppSuppressesHeader();
