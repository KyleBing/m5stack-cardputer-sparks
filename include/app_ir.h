#pragma once

#include "M5Cardputer.h"

void enterIrApp();
void leaveIrApp(); // 释放模式/风速图标缓存
// 遥控主页是无 header 的全屏布局，主循环别定时刷 header（否则会画出分隔横线）
bool irAppSuppressesHeader();
void updateIrApp();
void handleIrApp(const Keyboard_Class::KeysState& status);
// 每帧轮询 BtnA：发送当前遥控指令（wasPressed 仅单帧有效）
void pollIrBtnA();
