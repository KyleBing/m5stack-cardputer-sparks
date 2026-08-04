#pragma once

#include "M5Cardputer.h"
#include "app_common.h"
#include <cstdint>

// 大字时间区（无 header / 底栏 tip，整屏）
void getTimeDisplayArea(int& area_y, int& area_h);

// Pure 模式：整屏内容区
void getTimePureDisplayArea(int& area_y, int& area_h);

struct BigTimeState {
    int ts = 1;
    int main_x = 0;
    int main_y = 0;
    int main_h = 0;
    int digit_w = 0;
    int colon_w = 0;
    int ms_x = 0;
    int ms_y = 0;
    int ms_w = 0;
    int last_h = -1;
    int last_m = -1;
    int last_s = -1;
    int last_frac = -1;
};

// 绘制 HH:MM:SS（可选毫秒），force 时全量重算布局
void drawBigTimeDisplay(BigTimeState& state, int area_y, int area_h, int hours, int minutes,
                        int seconds, int frac_ms, bool show_ms, bool force);

// 毫秒时间戳转时分秒（支持 power-on uptime 的 64 位毫秒）
void splitTimeMs(uint64_t elapsed_ms, int& hours, int& minutes, int& seconds, int& frac);
