#pragma once

#include "M5Cardputer.h"

void enterWifiApp();
void drawWifiApp();
void updateWifiApp();
void handleWifiApp(const Keyboard_Class::KeysState& status);
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h），不回主菜单
bool closeWifiHelp();
