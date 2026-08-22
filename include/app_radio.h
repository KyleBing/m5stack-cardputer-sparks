#pragma once

#include "M5Cardputer.h"

void enterRadioApp();
void leaveRadioApp();
// I2C 扫描的写探测可能打开 Grove 收音机；扫完 / 退出时待机静音
void silenceFmRadioOnBus(m5::I2C_Class& bus);
void updateRadioApp();
void handleRadioApp(const Keyboard_Class::KeysState& status);
// 侧边 BtnA：亮屏灭屏 / 灭屏亮屏（边沿仅当帧有效）
void pollRadioBtnA();
bool isRadioDisplayBlanked();

bool isRadioHelpVisible();
bool closeRadioHelp();
// 关闭电台列表 / 取消重命名（ESC 优先于退出应用）
bool closeRadioStations();
// 取消正在进行的搜台 / 自动扫描
bool closeRadioSeek();
// 截图功能名：main / stations / rename / tuner / rds / help
void getRadioShotFeature(char* out, size_t out_len);
