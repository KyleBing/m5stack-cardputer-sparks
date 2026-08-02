#pragma once

#include "M5Cardputer.h"

void enterParticleClockApp();
void leaveParticleClockApp();
void updateParticleClockApp();
void handleParticleClockApp(const Keyboard_Class::KeysState& status);

// 每帧轮询 BtnA：强制重排粒子（等同空格）
void pollParticleClockBtnA();
