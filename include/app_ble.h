#pragma once

#include "M5Cardputer.h"
#include <WString.h>

void enterBleApp();
void leaveBleApp();
void drawBleApp(bool full_init = false);
void updateBleApp();
void handleBleApp(const String& key);
bool handleBlePageNav(const Keyboard_Class::KeysState& status);
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h），不回主菜单
bool closeBleHelp();
