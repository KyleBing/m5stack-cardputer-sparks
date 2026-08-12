#pragma once

#include "M5Cardputer.h"

void enterAcAutoApp();
void leaveAcAutoApp();
void updateAcAutoApp();
void handleAcAutoApp(const Keyboard_Class::KeysState& status);
// 每帧轮询 BtnA：亮屏手动熄屏 / 息屏唤醒
void pollAcAutoBtnA();
// Help 可见时关闭并重绘；否则返回 false
bool closeAcAutoHelp();
// Config 打开时 ESC 先关配置页回展示页；否则返回 false
bool handleAcAutoBack();
