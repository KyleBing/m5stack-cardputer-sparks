#pragma once

#include "M5Cardputer.h"

enum class BatterySleepMode : uint8_t {
    Light,
    Deep,
};

// 启动时加载日志；无时钟则按 uptime 暂存，深睡唤醒后补全 sleep 缺口
void initBatteryLog();
// 主循环：有时钟按整点记录；无时钟按 uptime 小时暂存，同步后回填
void batteryLogTick();
// 入睡前落盘当前采样和休眠类型（深睡后 RAM / pending 会丢）
void batteryLogPrepareSleep(BatterySleepMode mode);
// 浅睡唤醒后线性补全缺口并记当前点
void batteryLogAfterWake();

void enterBatteryApp();
void updateBatteryApp();
void handleBatteryApp(const Keyboard_Class::KeysState& status);
// 后台 NTP 进行中（可加快主循环轮询）
bool batteryAppSyncBusy();
