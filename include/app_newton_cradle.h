#pragma once

#include "M5Cardputer.h"

void enterNewtonCradleApp(bool embedded = false);
void leaveNewtonCradleApp();
void updateNewtonCradleApp();
void handleNewtonCradleApp(const Keyboard_Class::KeysState& status);
// 每帧轮询 BtnA：重播（等同空格）
void pollNewtonCradleBtnA();
