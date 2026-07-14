#pragma once

#include "M5Cardputer.h"

void enterCountdownApp();
void leaveCountdownApp();
void updateCountdownApp();
void redrawCountdownApp();
void handleCountdownApp(const Keyboard_Class::KeysState& status);
// 每帧调用：BtnA 开始/暂停（wasPressed 仅单帧有效）
void pollCountdownBtnA();

// 后台：检测到期并推进闹钟；true 表示需要把倒计时 UI 推到前台
bool pollCountdownBackground();
bool isCountdownAlarmRinging();
