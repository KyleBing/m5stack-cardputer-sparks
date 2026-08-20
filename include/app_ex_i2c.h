#pragma once

#include "M5Cardputer.h"

// 外设集合：左侧 Grove（I2C Radio/NFC、UART GPS）与 EXT14 SPI（CC1101）。
void enterExI2cApp();
void leaveExI2cApp();
void updateExI2cApp();
void handleExI2cApp(const Keyboard_Class::KeysState& status);
// 子 app 中 ESC/BtnGO 回 hub；已在 hub 时返回 false 交给主菜单。
bool handleExI2cBack();
// Help 打开时关闭并重绘；Radio / CC1101 子项委托对应 close*。
bool closeExI2cHelp();
bool isExI2cHelpVisible();
// 截图 slug：Radio 子界面时为 true
bool isExI2cRadioActive();
// 截图 slug：CC1101 子界面时为 true
bool isExI2cCc1101Active();
