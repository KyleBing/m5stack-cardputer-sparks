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

// 绘制文本徽章（黄底黑字），text_size 仅支持 1 或 2，返回占用宽度（含右侧间距）
int drawTextBadge(int x, int y, const char* label, int text_size = 1);

// 绘制左右箭头徽章（黄底黑箭头），text_size 仅支持 1 或 2，返回占用宽度（含右侧间距）
int drawArrowBadge(int x, int y, int text_size = 1);

// 绘制上下箭头徽章（黄底黑箭头）
int drawArrowUpDownBadge(int x, int y, int text_size = 1);

// 绘制单方向箭头徽章（黄底黑箭头）
int drawArrowLeftBadge(int x, int y, int text_size = 1);
int drawArrowRightBadge(int x, int y, int text_size = 1);
int drawArrowUpBadge(int x, int y, int text_size = 1);
int drawArrowDownBadge(int x, int y, int text_size = 1);

struct KeyHintItem {
    char key;
    const char* text;
};

// 按键提示行：按键徽章 + 文案（例如 o on / f off）
void drawKeyHintsRow(int x, int y, const KeyHintItem* items, int item_count, int text_size = 1,
                     uint16_t color = APP_COLOR_HINT);

// 底栏右下角 h help（各应用统一位置）
void drawHelpHintRight(const char* help_label = "help");

// 提示小字：',' 左箭头，'.' 右箭头
void drawHintText(int x, int y, const char* text, int text_size = 1);

// 使用 config 连接 WiFi（Mijia / Time 等按需调用，timeout_ms 为最长等待毫秒）
bool ensureConfigWifi(uint32_t timeout_ms = 12000);

// 断开 WiFi 并关闭射频（离开应用或用完网络后调用）
void releaseConfigWifi();

// 启动/唤醒后调用，避免 deep sleep 恢复了 UTC 时钟却未设 TZ
void applyLocalTimezone();

// 获取当前按下的可打印字符
String getPressedKey();

// ===== btngo：返回主菜单键（可改）=====
// 原硬件为侧边 BtnA(GO)/GPIO0；休眠唤醒仍固定用 BtnA。
// 改回 BtnA：把 BTNGO_USE_KEYBOARD 改为 0。
#ifndef BTNGO_USE_KEYBOARD
#define BTNGO_USE_KEYBOARD 1
#endif
// 键盘左上角 `（grave / HID 0x35）；改键时同步改 CHAR 与 HID
static constexpr char BTNGO_KEY_CHAR = '`';
static constexpr uint8_t BTNGO_HID = 0x35;
// 提示文案用短标签（如 "ESC" / "GO"）
const char* btnGoHintLabel();
// 本帧是否触发返回主菜单（边沿）
bool wasBtnGoPressed();
// 重置 btngo 边沿状态（休眠唤醒后调用）
void resetBtnGoEdge();

// 排空键盘/BtnA：等松开后吞掉边沿（休眠唤醒后用）
// wait_btn_a=false：不因侧边 BtnA 仍按住而长时间阻塞（light sleep 唤醒后）
void flushCardputerInput(bool wait_btn_a = true);

// 翻页键：-1 上一页，0 无，1 下一页（方向键 / ; , . /）
int getMenuNavDelta(const Keyboard_Class::KeysState& status);

// I2S/功放冷启动预热（短提示音前调用）
void warmUpSpeakerIfNeeded();
// 播放短 UI 提示音（内部会按需预热）
void playUiTone(float freq_hz, uint32_t duration_ms);
// Time 按键声：受 settings/sound.time_key 控制（countdown 闹钟请用 playUiTone）
void playTimeKeyTone(float freq_hz, uint32_t duration_ms);
bool isTimeKeySoundEnabled();
// 米家开/关提示音：受 settings/sound.mijia_on_off 控制
bool isMijiaOnOffSoundEnabled();
