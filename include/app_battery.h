#pragma once

#include "M5Cardputer.h"

// 启动时加载日志；深睡唤醒后补全 sleep 缺口
void initBatteryLog();
// 主循环：设备开启时按整点记录电量
void batteryLogTick();
// 入睡前落盘当前采样（深睡后 RAM 会丢）
void batteryLogPrepareSleep();
// 浅睡唤醒后线性补全缺口并记当前点
void batteryLogAfterWake();

void enterBatteryApp();
void updateBatteryApp();
void handleBatteryApp(const Keyboard_Class::KeysState& status);
// 后台 NTP 进行中（可加快主循环轮询）
bool batteryAppSyncBusy();
