#pragma once

#include "M5Cardputer.h"
#include <stdint.h>

void enterIrApp();
void leaveIrApp(); // 释放模式/风速图标缓存
// 遥控主页是无 header 的全屏布局，主循环别定时刷 header（否则会画出分隔横线）
bool irAppSuppressesHeader();
void updateIrApp();
void handleIrApp(const Keyboard_Class::KeysState& status);
// 每帧轮询 BtnA：发送当前遥控指令（wasPressed 仅单帧有效）
void pollIrBtnA();
// Help 可见时关闭并重绘；否则返回 false
bool closeIrHelp();

// 共享红外空调发送（供 Infrared / 空调自动化共用）
// brand: 0..IR_AC_BRAND_COUNT-1
// mode: 0=cool 1=heat 2=dry 3=fan 4=auto
// fan:  0=auto 1=min 2=low 3=med 4=high 5=max
// temp_c: 16..30
bool irSendAc(uint8_t brand, bool power, uint8_t mode, uint8_t temp_c, uint8_t fan);
