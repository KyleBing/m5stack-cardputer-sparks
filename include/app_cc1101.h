#pragma once

#include "M5Cardputer.h"

// CC1101 433MHz Sub-GHz 模块（EXT14 SPI）
void enterCc1101App();
void leaveCc1101App();
void updateCc1101App();
void handleCc1101App(const Keyboard_Class::KeysState& status);
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h）
bool closeCc1101Help();
bool isCc1101HelpVisible();
// 截图功能名：main / help
void getCc1101ShotFeature(char* out, size_t out_len);
