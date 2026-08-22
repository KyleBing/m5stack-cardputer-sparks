#pragma once

#include "M5Cardputer.h"
#include <stddef.h>
#include <stdint.h>

// AT6668 NMEA GPS：自动探测 Grove Unit（G1/G2）或 Cap LoRa-1262 GNSS（G15/G13）。
void enterGpsApp();
void leaveGpsApp();
void updateGpsApp();
void handleGpsApp(const Keyboard_Class::KeysState& status);

// Help 打开时关闭并重绘当前 GPS 页面。
bool closeGpsHelp();
bool isGpsHelpVisible();
// 历史曲线页 ESC：回到历史列表（未在曲线页返回 false）。
bool closeGpsHistoryChart();
// Settings 页 ESC：回到进入前的页面（未在 Settings 返回 false）。
bool closeGpsSettings();

// 每帧轮询 BtnA：开始 / 停止录制（等同空格；Help 打开时忽略）
void pollGpsBtnA();

// 截图功能名：live / sats / speed / history / chart / settings / help
void getGpsShotFeature(char* out, size_t out_len);

// —— History 导入 / 导出（Config Web /gps；GPX 1.1 + 扩展）——
static constexpr int GPS_HISTORY_CAPACITY = 12;

struct GpsHistoryEntry {
    uint32_t id;
    uint32_t utc_date;  // YYYYMMDD
    uint32_t utc_time;  // HHMMSS
    uint32_t duration_ms;
    uint32_t samples;
    float distance_m;
    float max_kmh;
    float avg_kmh;
};

using GpsGpxWriteFn = bool (*)(const char* data, size_t len, void* user);

bool gpsIsRecording();
void gpsHistoryReload();
int gpsHistoryCount();
bool gpsHistoryGet(int index, GpsHistoryEntry* out);
bool gpsHistoryDeleteById(uint32_t id);
// 流式写出单条 / 全部行程为 GPX（write 返回 false 则中止）。
bool gpsHistoryExportGpx(uint32_t id, GpsGpxWriteFn write, void* user);
bool gpsHistoryExportAllGpx(GpsGpxWriteFn write, void* user);
// 从 LittleFS 上的 GPX 文件导入（可含多条 <trk>）；失败时写入 err。
bool gpsHistoryImportGpxFile(const char* path, char* err, size_t err_len);

