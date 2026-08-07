#pragma once

#include "M5Cardputer.h"

void enterAcAutoApp();
void leaveAcAutoApp();
void updateAcAutoApp();
void handleAcAutoApp(const Keyboard_Class::KeysState& status);
// 每帧轮询 BtnA：亮屏手动熄屏 / 息屏唤醒
void pollAcAutoBtnA();
// 全程抑制系统 header（自绘顶栏）
bool acAutoAppSuppressesHeader();
