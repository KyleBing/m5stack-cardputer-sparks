#pragma once

#include "M5Cardputer.h"

void enterNfcApp();
void leaveNfcApp();
void updateNfcApp();
void handleNfcApp(const Keyboard_Class::KeysState& status);
bool closeNfcHelp();
bool isNfcHelpVisible();
// 截图功能名：main / history / detail / rename / emulate / help
// History/Detail `e`：Ultralight/NTAG 完整 UID+dump 模拟；Reader `e`：默认 NDEF 文本
void getNfcShotFeature(char* out, size_t out_len);
