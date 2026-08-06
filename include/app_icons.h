#pragma once

#include <cstdint>

// ===== Logo =====
static constexpr int APP_LOGO_DESIGN_SIZE = 64;

void drawAppLogo(int dest_x, int dest_y, int size = APP_LOGO_DESIGN_SIZE);

// ===== 方向箭头（分页等） =====
static constexpr int ICON_ARROW_W = 6;
static constexpr int ICON_ARROW_H = 7;
static constexpr int ICON_ARROW_LR_W = 14;      // 左右合成图标宽
static constexpr int ICON_ARROW_UD_H = 14;      // 上下纵向合成图标高
static constexpr int ICON_ARROW_UD_FLAT_W = 14; // 上下横向并排宽（适合 tip）

void drawIconArrowLeft(int x, int cy, uint16_t color);
void drawIconArrowRight(int x, int cy, uint16_t color);
void drawIconArrowUp(int x, int cy, uint16_t color);
void drawIconArrowDown(int x, int cy, uint16_t color);
void drawIconArrowLeftRight(int x, int cy, uint16_t color);
void drawIconArrowUpDown(int x, int cy, uint16_t color);
// 上下箭头横向并排（高度与单箭头一致，不撑高 tip）
void drawIconArrowUpDownFlat(int x, int cy, uint16_t color);

// ===== 返回图标（header） =====
static constexpr int ICON_BACK_W = 28;
static constexpr int ICON_BACK_H = 18;
void drawIconBack(int x, int y, uint16_t color = 0xFFFF);

// ===== WiFi 信号条（列表 RSSI） =====
static constexpr int ICON_SIGNAL_W = 11;
static constexpr int ICON_SIGNAL_H = 8;

int signalLevelFromRssi(int rssi);
void drawSignalBars(int x, int y, int rssi, uint16_t color = 0xFFFF);

// ===== WiFi 扇形圆图标（header） =====
static constexpr int WIFI_INNER_SIDE = 2;
static constexpr int WIFI_RING_GAP = 2;  // 弧间距（不含线宽）
static constexpr int WIFI_RING_COUNT = 3;
static constexpr int WIFI_RING_STEP = WIFI_RING_GAP + 1;  // 间隔 + 线宽 1px
// 内块 2px + 每层 3px → 弧半径 4 / 7 / 10，外廓 11×11（内容贴边）
static constexpr int ICON_WIFI_SIDE = WIFI_INNER_SIDE + WIFI_RING_STEP * WIFI_RING_COUNT;
static constexpr int ICON_WIFI_W = ICON_WIFI_SIDE;
static constexpr int ICON_WIFI_H = ICON_WIFI_SIDE;

void drawIconWifi(int x, int y, int rssi, uint16_t color = 0xFFFF);

// ===== 蓝牙 =====
static constexpr int ICON_BLE_W = 8;
static constexpr int ICON_BLE_H = 13;  // 去空边后内容高（原 14 含顶 1px 留白）

void drawIconBle(int x, int y, uint16_t color = 0xFFFF);

// ===== 充电闪电 =====
void drawIconChargingBolt(int zone_x, int y, int body_h);

// ===== 电池图标 =====
int getIconBatteryBodyHeight();
int getIconBatteryDisplayWidth(bool charging);
void drawIconBattery(int x, int y, int level, bool charging);

// ===== 分页圆点 =====
void drawIconPageDots(int x, int cy, int page, int page_count);
