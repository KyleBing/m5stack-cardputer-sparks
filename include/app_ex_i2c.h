#pragma once

#include "M5Cardputer.h"

// Grove 外设合集：左侧 Grove（I2C Radio/NFC、UART GPS）与 EXT14 SPI（CC1101）。
void enterExI2cApp();
void leaveExI2cApp();
void updateExI2cApp();
void handleExI2cApp(const Keyboard_Class::KeysState& status);
// 子 app 中 ESC/BtnGO 回 hub；已在 hub 时返回 false 交给主菜单。
bool handleExI2cBack();
// Help 打开时关闭并重绘；Radio / CC1101 子项委托对应 close*。
bool closeExI2cHelp();
bool isExI2cHelpVisible();
// 截图 slug：exi2c_<app>_<feature>（如 exi2c_gps_live）
void getExI2cShotSlug(char* out, size_t out_len);
// 兼容旧调用（截图已改用 getExI2cShotSlug）
bool isExI2cRadioActive();
bool isExI2cCc1101Active();
