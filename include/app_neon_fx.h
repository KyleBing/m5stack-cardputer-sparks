#pragma once

#include "M5Cardputer.h"

void enterNeonFxApp();
void leaveNeonFxApp();
void updateNeonFxApp();
void handleNeonFxApp(const Keyboard_Class::KeysState& status);

// Help 页是否可见（全屏 app 无 header，主循环据此跳过 header 状态刷新与节流）
bool isNeonFxHelpVisible();
