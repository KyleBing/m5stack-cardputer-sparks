#pragma once

#include "M5Cardputer.h"
#include "app_colors.h"
#include <WString.h>

static constexpr int INFO_LINE_H = 10;
static constexpr int INFO_LINE_H_2X = 18; // 16px 字高 + 2px 行间距
static constexpr uint16_t INFO_LABEL_COLOR = APP_COLOR_LABEL;
static constexpr uint16_t INFO_VALUE_COLOR = APP_COLOR_VALUE;
// 指定字号下的行高（默认字体每级 8px）
constexpr int infoLineHeight(int text_size) { return 8 * text_size; }

// label / value 分色，固定 y，可指定字号倍率
void drawInfoLineAt(int x, int y, const char* label, const char* value, int text_size = 1);

// ASCII 小字：label / value 分色，自动递增 y
void drawInfoLine(int x, int& y, const char* label, const char* value);
void drawInfoLineInt(int x, int& y, const char* label, int value);

// Cardputer 等 ADC 机型 isCharging() 可能返回 charge_unknown
const char* getChargingStatusText();
bool isBatteryCharging();

// 绘制按键字母块（菜单键色底 + 黑字），text_size 仅支持 1 或 2，返回占用宽度（含右侧间距）
int drawKeyBadge(int x, int y, char key, int text_size = 1);

struct KeyHintItem {
    char key;
    const char* text;
};

// 按键提示行：按键徽章 + 文案（例如 o on / f off）
void drawKeyHintsRow(int x, int y, const KeyHintItem* items, int item_count, int text_size = 1,
                     uint16_t color = APP_COLOR_HINT);

// 提示小字：',' 左箭头，'.' 右箭头
void drawHintText(int x, int y, const char* text, int text_size = 1);

// 使用 config 连接 WiFi（Mijia / Time 等按需调用，timeout_ms 为最长等待毫秒）
bool ensureConfigWifi(uint32_t timeout_ms = 12000);

// 断开 WiFi 并关闭射频（离开应用或用完网络后调用）
void releaseConfigWifi();

// 获取当前按下的可打印字符
String getPressedKey();

// 排空键盘/BtnA 状态：等全部松开后吞掉 isChange / wasPressed 边沿（休眠唤醒后用）
void flushCardputerInput();

// 翻页键：-1 上一页，0 无，1 下一页（方向键 / ; , . /）
int getMenuNavDelta(const Keyboard_Class::KeysState& status);
