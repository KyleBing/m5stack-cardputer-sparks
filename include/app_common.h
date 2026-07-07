#pragma once

#include "M5Cardputer.h"
#include <WString.h>

static constexpr int INFO_LINE_H = 10;
static constexpr uint16_t INFO_LABEL_COLOR = CYAN;
static constexpr uint16_t INFO_VALUE_COLOR = WHITE;

// ASCII 小字：label / value 分色
void drawInfoLine(int x, int& y, const char* label, const char* value);
void drawInfoLineInt(int x, int& y, const char* label, int value);

// 使用 config 连接 WiFi（Mijia / Time 共用）
bool ensureConfigWifi();

// 获取当前按下的可打印字符
String getPressedKey();
