#pragma once

#include "M5Cardputer.h"

void enterNfcApp();
void leaveNfcApp();
void updateNfcApp();
void handleNfcApp(const Keyboard_Class::KeysState& status);
bool closeNfcHelp();
bool isNfcHelpVisible();
