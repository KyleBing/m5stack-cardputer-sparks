#pragma once

#include "M5Cardputer.h"

void enterRtcApp();
void leaveRtcApp();
void updateRtcApp();
// 每帧轮询 BtnA（Countdown / Stopwatch 开始暂停）
void pollTimeAppBtnA();
void handleTimeApp(const Keyboard_Class::KeysState& status);
// Uptime / Clock（含 big time）：可降频省电的纯时间显示
bool isTimeClockLikeMode();
// 无操作满 1 分钟后主循环可降到 1s 一拍
bool isTimeIdleSlowLoop();
// 倒计时到期：切到 COUNTDOWN 并全量重绘（响铃已由 countdown 触发）
void presentCountdownAlarmUi();
// 当前是否在 Time 的倒计时子页（用于避免重复切入）
bool isTimeCountdownUiActive();
