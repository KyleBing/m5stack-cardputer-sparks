#pragma once

#include "M5Cardputer.h"

void enterAcAutoApp();
void leaveAcAutoApp();
void updateAcAutoApp();
void handleAcAutoApp(const Keyboard_Class::KeysState& status);
// 每帧轮询 BtnA：亮屏时切换息屏（与 Cursor 类似）
void pollAcAutoBtnA();
// 息屏时抑制主循环 header 刷新
bool acAutoAppSuppressesHeader();
