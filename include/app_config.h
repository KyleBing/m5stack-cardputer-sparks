#pragma once

#include <Arduino.h>

// 米家设备条目：WiFi miIO 用 ip+token；BLE 传感器用 mac+ble_key
struct MijiaDevice {
    char name[32];
    char name_zh[48]; // 中文显示名（优先于 name）
    char id[48];      // blt. 设备 id 较长
    char mac[18];
    char ip[16];
    char token[33];
    char model[48];
    char ble_key[33]; // 32 hex bindkey；空表示非 BLE
};

static constexpr int MIJIA_DEVICE_MAX = 50;
static constexpr int MIJIA_GROUP_MAX = 16;
static constexpr int MIJIA_GROUP_MEMBER_MAX = 16;

// 设备编组：members 在 JSON 里用 id 引用；加载时解析成下标
struct MijiaDeviceGroup {
    char name[32];
    char name_zh[48];
    int member_indices[MIJIA_GROUP_MEMBER_MAX]; // 对应 devices[]；无效已剔除
    int member_count;
};

static constexpr int CURSOR_TOKEN_MAX = 1024;
// POSIX TZ 默认东八区（NTP 为 UTC，显示靠此字段）
static constexpr const char* APP_TIMEZONE_DEFAULT = "CST-8";

// Time 入口默认模块（config: time.default）
enum class TimeDefaultMode : uint8_t {
    Up = 0,
    Ntp = 1,
    Countdown = 2,
    Stopwatch = 3,
};

struct AppConfig {
    char wifi_ssid[33];
    char wifi_password[65];
    char cursor_token[CURSOR_TOKEN_MAX];
    char timezone[48]; // POSIX TZ，如 CST-8；缺省东八区
    uint8_t brightness;      // 配置存 0~100；setBrightness 时再转 0~255
    bool time_key_sound;     // Time 内按键声（countdown 到点闹钟不受影响）
    bool mijia_on_off_sound; // 米家开/关提示音
    TimeDefaultMode time_default_mode; // 按 T 进入 Time 时的默认模块
    bool time_pure;                    // Time 是否默认 pure 全屏
    MijiaDevice devices[MIJIA_DEVICE_MAX];
    int device_count;
    MijiaDeviceGroup device_groups[MIJIA_GROUP_MAX];
    int device_group_count;
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

// 更新屏幕亮度并写回（percent：0~100）
bool saveAppConfigBrightness(uint8_t brightness_percent);

// 更新 Time 按键声开关并写回
bool saveAppConfigTimeKeySound(bool enabled);

// 更新米家开/关提示音开关并写回
bool saveAppConfigMijiaOnOffSound(bool enabled);

// 更新时区（POSIX TZ）并写回
bool saveAppConfigTimezone(const char* timezone);

// 更新 Time 默认模块并写回
bool saveAppConfigTimeDefaultMode(TimeDefaultMode mode);

// 更新 Time pure 偏好并写回
bool saveAppConfigTimePure(bool enabled);

// Time 默认模块 ↔ 配置字符串
const char* timeDefaultModeName(TimeDefaultMode mode);
TimeDefaultMode parseTimeDefaultMode(const char* s);

// 常用时区预设（Settings 里 -= 循环）
const char* cycleAppTimezonePreset(const char* current, int delta);

// 亮度：配置 0~100 ↔ 硬件 0~255
uint8_t brightnessPercentToHw(uint8_t percent);
uint8_t brightnessHwToPercent(uint8_t hw);

// 读取原始 config.json 文本（用于 Web 展示）
bool readAppConfigRaw(String& out);

const AppConfig& getAppConfig();

// 当前生效时区：config 有值用 config，否则默认 CST-8
const char* getAppTimezone();

// 显示名：优先 name_zh，否则 name
const char* mijiaDeviceDisplayName(const MijiaDevice& dev);

// 是否走 BLE 被动读取（有 ble_key 且无可用局域网 miIO）
bool mijiaDeviceUsesBle(const MijiaDevice& dev);

// 按设备 id 查找 devices[] 下标；未找到返回 -1
int mijiaFindDeviceIndexById(const char* id);
