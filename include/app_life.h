#pragma once

#include "M5Cardputer.h"

void enterLifeApp();
void leaveLifeApp();
void updateLifeApp();
void handleLifeApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：运行 / 暂停（等同空格）
void pollLifeBtnA();
