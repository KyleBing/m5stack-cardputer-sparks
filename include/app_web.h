#pragma once

#include "M5Cardputer.h"
#include <WString.h>

// 启动 Web 配网（非阻塞，由 updateWebApp 推进）
bool startConfigWebServer();

// 停止 Web 服务
void stopConfigWebServer();

// loop 中轮询连接进度与 HTTP 请求
void updateWebApp();

bool isConfigWebServerRunning();

// 当前是否通过路由器局域网 IP 提供配置页
bool isConfigWebStaMode();

const char* getConfigWebApSsid();
const char* getConfigWebApPass();
const char* getConfigWebUrl();
const char* getConfigWebStatus();

// 当前界面短名（main 实现），截图文件为 app_<slug>_NNN.png
const char* getCurrentAppShotSlug();

// Config 全程无 header：禁止全局刷电池 / WiFi / BLE 图标
bool webAppSuppressesHeader();

void drawWebApp();
void enterWebApp();
void leaveWebApp();
void handleWebApp(const Keyboard_Class::KeysState& status);
// Help 打开时 ESC/BtnGO 关闭 Help（等同按 h），不回主菜单
bool closeWebHelp();
