#pragma once

#include <Arduino.h>
// 米家设备条目（miIO 控制需 ip + token，其余为识别信息）
struct MijiaDevice {
    char name[32];
    char id[16];
    char mac[18];
    char ip[16];
    char token[33];
    char model[48];
};

static constexpr int MIJIA_DEVICE_MAX = 50;

static constexpr int CURSOR_API_KEY_MAX = 1024;

struct AppConfig {
    char wifi_ssid[33];
    char wifi_password[65];
    char cursor_api_key[CURSOR_API_KEY_MAX];
    uint8_t brightness;
    MijiaDevice devices[MIJIA_DEVICE_MAX];
    int device_count;
    bool loaded;
};

// 挂载 LittleFS（不自动格式化）
bool initAppConfigFs();

// 从 /config.json 加载；文件不存在或解析失败返回 false
bool loadAppConfig();

// 保存 JSON 到 /config.json 并重新加载
bool saveAppConfigJson(const char* json);

// 更新 WiFi 字段并写回（保留 devices 等其它配置）
bool saveAppConfigWifi(const char* ssid, const char* password);

// 更新屏幕亮度并写回
bool saveAppConfigBrightness(uint8_t brightness);

// 读取原始 config.json 文本（用于 Web 展示）
bool readAppConfigRaw(String& out);

const AppConfig& getAppConfig();
