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
    char hotkey;      // 快速选择快捷键：a-z / 0-9；'\0' 表示未设置
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

// 每周起始日（config: system.week_start）
enum class WeekStartDay : uint8_t {
    Sunday = 0,
    Monday = 1,
};

// 红外入口默认功能块（config: infrared.default）
enum class IrDefaultCategory : uint8_t {
    Tv = 0,
    Ac = 1,
};

// HID Keyboard 默认传输方式（config: hid_keyboard.transport）
enum class HidKeyboardTransport : uint8_t {
    Ble = 0,
    Usb = 1,
};

// 与 app_ir 品牌表一致
static constexpr int IR_TV_BRAND_COUNT = 5;
static constexpr int IR_AC_BRAND_COUNT = 6;

// 多 WiFi 配置上限（wifis[]）
static constexpr int WIFI_PROFILE_MAX = 5;

struct WifiProfile {
    char ssid[33];
    char password[65];
};

struct AppConfig {
    // 当前 active 镜像（ensureStaWifi 等直接读这两项）
    char wifi_ssid[33];
    char wifi_password[65];
    WifiProfile wifis[WIFI_PROFILE_MAX];
    int wifi_count;
    char wifi_active[33]; // 当前选用的 ssid
    char cursor_token[CURSOR_TOKEN_MAX];
    char timezone[48]; // POSIX TZ，如 CST-8；缺省东八区
    uint8_t brightness;      // screen.brightness：0~100；setBrightness 时再转 0~255
    bool screen_invert;      // screen.invert：屏幕反色
    uint8_t speaker_volume;  // 喇叭音量 0~100；setVolume 时再转 0~255
    bool time_key_sound;     // Time 内按键声（countdown 到点闹钟不受影响）
    bool mijia_on_off_sound; // 米家开/关提示音
    TimeDefaultMode time_default_mode; // 按 T 进入 Time 时的默认模块
    bool time_pure;                    // Time 是否默认 pure 全屏
    WeekStartDay week_start;           // 日历每周起始日
    IrDefaultCategory infrared_default; // 进入红外时默认 TV / AC
    uint8_t infrared_tv_brand;          // 0..IR_TV_BRAND_COUNT-1
    uint8_t infrared_ac_brand;          // 0..IR_AC_BRAND_COUNT-1
    HidKeyboardTransport hid_keyboard_transport; // HID Keyboard 默认 BLE / USB
    uint8_t hid_keyboard_imu_sensitivity;       // IMU 鼠标灵敏度 1..10
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

// 按 ssid upsert 到 wifis[]，并设为 wifi_active 后写回
bool saveAppConfigWifi(const char* ssid, const char* password);

// 切换当前 active（须已在 wifis[] 中）；写回并同步镜像
bool setAppConfigWifiActive(const char* ssid);

// 按 ssid 更新或追加；set_active 为 true 时同时设为当前；满且非同名则失败
bool upsertAppConfigWifi(const char* ssid, const char* password, bool set_active = true);

// 更新屏幕亮度并写回（screen.brightness，percent：0~100）
bool saveAppConfigBrightness(uint8_t brightness_percent);

// 更新屏幕反色并写回（screen.invert）
bool saveAppConfigScreenInvert(bool invert);

// 更新喇叭音量并写回（percent：0~100）
bool saveAppConfigSpeakerVolume(uint8_t volume_percent);
// 仅更新内存中的音量（不写盘；供 UI 调节立刻生效）
void setAppConfigSpeakerVolumeLocal(uint8_t volume_percent);

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

// 更新日历每周起始日并写回 system.week_start
bool saveAppConfigWeekStart(WeekStartDay day);

// 每周起始日 ↔ 配置字符串
const char* weekStartDayName(WeekStartDay day);
WeekStartDay parseWeekStartDay(const char* s);

// Time 默认模块 ↔ 配置字符串
const char* timeDefaultModeName(TimeDefaultMode mode);
TimeDefaultMode parseTimeDefaultMode(const char* s);

// 红外默认：功能块 / 品牌 ↔ 配置字符串
const char* irDefaultCategoryName(IrDefaultCategory category);
IrDefaultCategory parseIrDefaultCategory(const char* s);
const char* irTvBrandConfigName(uint8_t idx);
const char* irTvBrandDisplayName(uint8_t idx);
uint8_t parseIrTvBrand(const char* s);
const char* irAcBrandConfigName(uint8_t idx);
const char* irAcBrandDisplayName(uint8_t idx);
uint8_t parseIrAcBrand(const char* s);
uint8_t cycleIrTvBrand(uint8_t cur, int delta);
uint8_t cycleIrAcBrand(uint8_t cur, int delta);
IrDefaultCategory cycleIrDefaultCategory(IrDefaultCategory cur, int delta);

// 更新红外默认并写回（infrared 对象）
bool saveAppConfigInfrared(IrDefaultCategory category, uint8_t tv_brand, uint8_t ac_brand);

// HID Keyboard 传输方式 ↔ 配置字符串
const char* hidKeyboardTransportName(HidKeyboardTransport transport);
HidKeyboardTransport parseHidKeyboardTransport(const char* s);

// 更新 HID Keyboard 偏好并写回（hid_keyboard 对象）
bool saveAppConfigHidKeyboard(HidKeyboardTransport transport, uint8_t imu_sensitivity);

// 常用时区预设（Settings 里 -= 循环）
const char* cycleAppTimezonePreset(const char* current, int delta);

// 亮度：配置 0~100 ↔ 硬件 0~255
uint8_t brightnessPercentToHw(uint8_t percent);
uint8_t brightnessHwToPercent(uint8_t hw);

// 喇叭音量 0~100 ↔ Speaker.setVolume 0~255
uint8_t speakerVolumePercentToHw(uint8_t percent);
uint8_t speakerVolumeHwToPercent(uint8_t hw);

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

// 规范化快捷键：a-z / 0-9；非法或保留键 q 返回 '\0'
char mijiaNormalizeHotkey(char c);

// 按快捷键查找 devices[] 下标；未找到返回 -1
int mijiaFindDeviceIndexByHotkey(char hotkey);

// 写入设备快捷键并落盘；同键其它设备会被清空（调用方已确认替换）
bool saveAppConfigDeviceHotkey(int device_idx, char hotkey);
