#pragma once

#include "M5Cardputer.h"

void enterRadioApp();
void leaveRadioApp();
void updateRadioApp();
void handleRadioApp(const Keyboard_Class::KeysState& status);

bool isRadioHelpVisible();
bool closeRadioHelp();
// 关闭电台列表 / 取消重命名（ESC 优先于退出应用）
bool closeRadioStations();
// 取消正在进行的搜台 / 自动扫描
bool closeRadioSeek();
