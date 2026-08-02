#pragma once

#include "M5Cardputer.h"

void enterLissajousApp();
void leaveLissajousApp();
void updateLissajousApp();
void handleLissajousApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：相位脉冲（等同空格）
void pollLissajousBtnA();
