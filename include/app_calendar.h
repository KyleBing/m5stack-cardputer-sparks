#pragma once

#include "M5Cardputer.h"

void enterCalendarApp();
void updateCalendarApp();
void handleCalendarApp(const Keyboard_Class::KeysState& status);
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h），回到月视图
bool closeCalendarHelp();
