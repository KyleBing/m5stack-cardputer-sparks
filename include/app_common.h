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

// 底栏右下角 h help（各应用统一位置；y_offset 整行下移，宫格 tip 用 1）
void drawHelpHintRight(const char* help_label = "help", int y_offset = 0);

// 提示小字：',' 左箭头，'.' 右箭头
void drawHintText(int x, int y, const char* text, int text_size = 1);

// 点阵风格文字：Font0 1x 渲染后再按 scale 画方块，方块间留 1px 缝
// 返回绘制占用宽高（scale<2 时退回普通 setTextSize）
void drawDotText(const char* text, int x, int y, int scale, uint16_t color);
// Font0 1x 下的文字宽度（用于按 scale 推布局）
int measureDotTextWidth1x(const char* text);
static constexpr int DOT_TEXT_H_1X = 8;

// 使用 config 连接 WiFi（timeout_ms 为最长等待毫秒）
bool ensureConfigWifi(uint32_t timeout_ms = 12000);

// 用完网络：立刻 disconnect + WIFI_OFF
void releaseConfigWifi();

// 立刻关闭 WiFi（休眠 / AP 等必须独占射频时）；与 releaseConfigWifi 同效
void forceReleaseConfigWifi();

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

// 括号翻页键：-1 上一页（'['），0 无，1 下一页（']'）
// 仅供 [ ] 未被占作他用的界面调用，与 getMenuNavDelta 并存
int getBracketNavDelta(const Keyboard_Class::KeysState& status);

// ===== IMU 倾斜方向（贪吃蛇 / 扫雷用倾斜代替方向键）=====
// 开启时以当前握持姿态为中立位，之后只看偏移量，手持不用端平设备
struct ImuTiltConfig {
    float enter;              // 触发阈值（g），越大越钝
    float leave;              // 回中阈值（g），小于 enter 形成迟滞
    float full;               // 倾到这个量即达到最快连发速度
    uint32_t repeat_delay_ms; // 保持倾斜后首次连发的延迟
    uint32_t repeat_slow_ms;  // 刚过 enter 时的连发间隔；0 = 只在方向变化时触发一次
    uint32_t repeat_fast_ms;  // 倾到 full 时的连发间隔，倾得越多走得越快
};

struct ImuTiltState {
    float base[3];  // 校准瞬间的重力方向（单位向量）
    float up[3];    // 屏幕上方投影到「垂直于 base」平面后的单位向量
    float right[3]; // 屏幕右方，由「上方 × base」推出的单位向量
    float tilt_x;   // 低通后的倾斜量（右正，数值约等于倾角正弦）
    float tilt_y;   // 低通后的倾斜量（下正）
    int8_t dir_x;
    int8_t dir_y;
    uint32_t hold_since_ms;
    uint32_t last_emit_ms;
    uint32_t last_sample_ms;
    bool base_ready;
};

// 板载 IMU 是否可用
bool isImuTiltAvailable();

// 清空状态：下一次采样重新取中立姿态（开启倾斜操控时调用）
void imuTiltReset(ImuTiltState& state);

// 采样一次；返回本帧是否产生方向（dx/dy 其一为 ±1，屏幕坐标：dy<0 向上）
bool imuTiltPoll(ImuTiltState& state, const ImuTiltConfig& cfg, int& dx, int& dy);

// 需要出声时 begin 并套用音量
void warmUpSpeakerIfNeeded();
// 关喇叭并拉低 I2S 脚，避免 NS4168 悬空嗡嗡（Mic 占用 WS 时不碰 WS）
void releaseSpeakerQuiet();
// Mic.end 后：Speaker 抢回与 PDM 共用的 G43，再静音卸 I2S + 拉低（否则只 hold 常仍嗡）
void reclaimAndReleaseSpeakerQuiet();
// 出声/开麦前解除 gpio_hold，否则 begin 抢不到脚
void releaseAudioPinHolds();
// 主循环占位（已取消提示音播完自动静音）
void pollSpeakerQuietRelease();
void cancelSpeakerQuietRelease();
// 主循环：音量脏标记空闲后再写盘（避免挡 UI）
void pollSpeakerVolumeSave();
// 按配置音量应用到已启用的 Speaker
void applyAppSpeakerVolume();
// 当前有效音量 0~100（含未写盘的调节）
uint8_t getAppSpeakerVolumePercent();
// 调节音量（先改内存/Speaker，写盘延后到 flush / poll）
void adjustAppSpeakerVolume(int delta_percent);
void flushSpeakerVolumeSave();
// 播放短 UI 提示音；auto_quiet 已忽略（保留参数兼容旧调用）
void playUiTone(float freq_hz, uint32_t duration_ms, bool auto_quiet = false);
// Time 按键声：受 settings/sound.time_key 控制（countdown 闹钟请用 playUiTone）
void playTimeKeyTone(float freq_hz, uint32_t duration_ms);
bool isTimeKeySoundEnabled();
// 米家开/关提示音：受 settings/sound.mijia_on_off 控制
bool isMijiaOnOffSoundEnabled();
