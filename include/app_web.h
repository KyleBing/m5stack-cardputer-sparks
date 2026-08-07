#pragma once

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

void drawWebApp();
void enterWebApp();
void handleWebApp(const String& key);
