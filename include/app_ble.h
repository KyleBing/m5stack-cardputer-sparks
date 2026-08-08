#pragma once

#include "M5Cardputer.h"
#include <WString.h>

void enterBleApp();
void leaveBleApp();
void drawBleApp(bool full_init = false);
void updateBleApp();
void handleBleApp(const String& key);
bool handleBlePageNav(const Keyboard_Class::KeysState& status);
