#pragma once

#include "M5Cardputer.h"

void enterHidKeyboardApp();
void leaveHidKeyboardApp();
void updateHidKeyboardApp();
void handleHidKeyboardApp(const Keyboard_Class::KeysState& status);
// 侧边 BtnA：退出回主菜单（本应用占用全部按键，不能用 ESC）
bool pollHidKeyboardBtnAExit();
// 主输入界面无 header：禁止全局刷蓝牙/WiFi 图标
bool hidKeyboardSuppressesHeader();
