#pragma once

#include "M5Cardputer.h"

void enterRtcApp();
void updateRtcApp();
// 每帧轮询 BtnA（Countdown / Stopwatch 开始暂停）
void pollTimeAppBtnA();
void handleTimeApp(const Keyboard_Class::KeysState& status);
bool isTimePureMode();
// 倒计时到期：切到 COUNTDOWN 并全量重绘（响铃已由 countdown 触发）
void presentCountdownAlarmUi();
// 当前是否在 Time 的倒计时子页（用于避免重复切入）
bool isTimeCountdownUiActive();
