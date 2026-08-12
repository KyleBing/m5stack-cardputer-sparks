#pragma once

#include "M5Cardputer.h"

// 词汇学习（多词库）：随机/顺序浏览，标记已会并持久化
void enterVocabApp();
void leaveVocabApp();
void updateVocabApp();
void handleVocabApp(const Keyboard_Class::KeysState& status);

bool isVocabHelpVisible();
bool closeVocabHelp();
