#pragma once

#include "M5Cardputer.h"

void enterNeonFxApp();
void leaveNeonFxApp();
void updateNeonFxApp();
void handleNeonFxApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：脉冲闪光（等同空格）
void pollNeonFxBtnA();

// Help 页是否可见（全屏 app 无 header，主循环据此跳过 header 状态刷新与节流）
bool isNeonFxHelpVisible();
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h），不回主菜单
bool closeNeonFxHelp();
