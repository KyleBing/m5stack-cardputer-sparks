#pragma once

#include "M5Cardputer.h"

void enterBezierWaveApp();
void leaveBezierWaveApp();
void updateBezierWaveApp();
void handleBezierWaveApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：相位脉冲（等同空格）
void pollBezierWaveBtnA();
