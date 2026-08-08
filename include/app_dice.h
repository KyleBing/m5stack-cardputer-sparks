#pragma once

#include "M5Cardputer.h"

void enterDiceApp();
void leaveDiceApp();
void updateDiceApp();
void handleDiceApp(const Keyboard_Class::KeysState& status);

// Help 页是否可见（全屏 app 无 header，主循环据此跳过 header 状态刷新）
bool isDiceHelpVisible();
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h），不回主菜单
bool closeDiceHelp();
