#pragma once

#include "M5Cardputer.h"

void enterMatrixRainApp();
void leaveMatrixRainApp();
void updateMatrixRainApp();
void handleMatrixRainApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：脉冲爆发（等同空格）
void pollMatrixRainBtnA();
