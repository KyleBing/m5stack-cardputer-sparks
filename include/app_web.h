#pragma once

// 启动 AP + Web 配网服务
bool startConfigWebServer();

// 停止 Web 服务并关闭 AP
void stopConfigWebServer();

// loop 中轮询 HTTP 请求
void handleConfigWebServer();

bool isConfigWebServerRunning();

const char* getConfigWebApSsid();
const char* getConfigWebApPass();
const char* getConfigWebUrl();
const char* getConfigWebStatus();

void drawWebApp();
void enterWebApp();
