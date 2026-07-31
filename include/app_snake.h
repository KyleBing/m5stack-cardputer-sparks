#pragma once

#include "M5Cardputer.h"

void enterSnakeApp();
void leaveSnakeApp();
void updateSnakeApp();
void handleSnakeApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：暂停 / 继续；结束时重开一局
void pollSnakeBtnA();
