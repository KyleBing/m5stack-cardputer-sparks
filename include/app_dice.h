#pragma once

#include "M5Cardputer.h"

void enterDiceApp();
void leaveDiceApp();
void updateDiceApp();
void handleDiceApp(const Keyboard_Class::KeysState& status);

// Help 页是否可见（全屏 app 无 header，主循环据此跳过 header 状态刷新）
bool isDiceHelpVisible();
