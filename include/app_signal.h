#pragma once

#include <cstdint>

static constexpr int SIGNAL_BAR_ICON_W = 11;  // 4 格信号条总宽
static constexpr int SIGNAL_BAR_ICON_H = 8;

// RSSI(dBm) → 信号格数 0-4
int signalLevelFromRssi(int rssi);

// 绘制信号强度条（4 格天线图标，左低右高）
void drawSignalBars(int x, int y, int rssi, uint16_t color = 0xFFFF);
