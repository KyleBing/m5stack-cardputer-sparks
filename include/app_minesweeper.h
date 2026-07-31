#pragma once

#include "M5Cardputer.h"

void enterMinesweeperApp();
void leaveMinesweeperApp();
void updateMinesweeperApp();
void handleMinesweeperApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：翻开 / 和弦展开（等同空格）
void pollMinesweeperBtnA();
