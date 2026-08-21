#pragma once

#include "M5Cardputer.h"

// 游戏集合：硬币、双摆、迷宫滚球和抽奖轮。
void enterGamesApp();
void leaveGamesApp();
void updateGamesApp();
void handleGamesApp(const Keyboard_Class::KeysState& status);
// 子游戏中返回 Games 顶层；已在顶层时返回 false 交给主菜单处理。
bool handleGamesBack();
bool isGamesHelpVisible();
// Help 可见时关闭并重绘；否则返回 false
bool closeGamesHelp();
// 截图 slug：gamemini_<app>_<feature>（如 gamemini_snake_help）
void getGamesShotSlug(char* out, size_t out_len);
// 每帧轮询 BtnA（wasPressed / 蓄力 isPressed 在 update 内）
void pollGamesBtnA();
