#include "app_config.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <cstring>
#include <strings.h>

static constexpr const char* CONFIG_PATH = "/config.json";

static AppConfig g_config{};

// 按设备 id 查找（供 loadAppConfig 解析编组）
int mijiaFindDeviceIndexById(const char* id);

// 安全拷贝字符串到定长缓冲区
static void copyField(char* dest, const size_t dest_size, const char* src) {
    if (src == nullptr || dest_size == 0) {
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// 粗判私网局域网 IP（用于区分云端假 IP）
static bool isPrivateLanIp(const char* ip) {
    if (ip == nullptr || ip[0] == '\0') {
        return false;
    }
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    if (a == 10) {
        return true;
    }
    if (a == 192 && b == 168) {
        return true;
    }
    if (a == 172 && b >= 16 && b <= 31) {
        return true;
    }
    return false;
}

bool initAppConfigFs() {
    return LittleFS.begin(false);
}

bool loadAppConfig() {
    g_config = {};
    g_config.loaded = false;
    g_config.brightness = 30; // 默认 30%
    g_config.time_key_sound = true; // 默认开
    g_config.mijia_on_off_sound = true;
    g_config.time_default_mode = TimeDefaultMode::Up;
    g_config.time_pure = false;
    g_config.infrared_default = IrDefaultCategory::Tv;
    g_config.infrared_tv_brand = 0; // Samsung
    g_config.infrared_ac_brand = 0; // Midea
    copyField(g_config.timezone, sizeof(g_config.timezone), APP_TIMEZONE_DEFAULT);

    if (!LittleFS.exists(CONFIG_PATH)) {
        return false;
    }

    File file = LittleFS.open(CONFIG_PATH, "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        return false;
    }

    JsonObject wifi = doc["wifi"];
    if (!wifi.isNull()) {
        copyField(g_config.wifi_ssid, sizeof(g_config.wifi_ssid), wifi["ssid"]);
        copyField(g_config.wifi_password, sizeof(g_config.wifi_password), wifi["password"]);
    }

    JsonObject cursor = doc["cursor"];
    if (!cursor.isNull()) {
        copyField(g_config.cursor_token, sizeof(g_config.cursor_token), cursor["token"]);
    }

    // 亮度：配置为 0~100；>100 视为旧版 0~255 并换算
    {
        int raw = doc["brightness"] | 30;
        if (raw < 0) {
            raw = 0;
        }
        if (raw > 100) {
            raw = raw * 100 / 255;
        }
        g_config.brightness = static_cast<uint8_t>(raw);
    }
    // 默认开；缺字段时保持开启
    g_config.time_key_sound = true;
    g_config.mijia_on_off_sound = true;
    JsonObject sound = doc["sound"];
    if (!sound.isNull()) {
        g_config.time_key_sound = sound["time_key"] | true;
        g_config.mijia_on_off_sound = sound["mijia_on_off"] | true;
    }

    // 时区：缺字段时保持默认 CST-8
    const char* tz = doc["timezone"];
    if (tz != nullptr && tz[0] != '\0') {
        copyField(g_config.timezone, sizeof(g_config.timezone), tz);
    }

    // Time：time.default / time.pure
    g_config.time_default_mode = TimeDefaultMode::Up;
    g_config.time_pure = false;
    JsonObject time_obj = doc["time"];
    if (!time_obj.isNull()) {
        g_config.time_default_mode = parseTimeDefaultMode(time_obj["default"]);
        g_config.time_pure = time_obj["pure"] | false;
    }

    // Infrared：default / tv_brand / ac_brand（兼容小写 infrared）
    g_config.infrared_default = IrDefaultCategory::Tv;
    g_config.infrared_tv_brand = 0;
    g_config.infrared_ac_brand = 0;
    JsonObject ir_obj = doc["Infrared"];
    if (ir_obj.isNull()) {
        ir_obj = doc["infrared"];
    }
    if (!ir_obj.isNull()) {
        g_config.infrared_default = parseIrDefaultCategory(ir_obj["default"]);
        g_config.infrared_tv_brand = parseIrTvBrand(ir_obj["tv_brand"]);
        g_config.infrared_ac_brand = parseIrAcBrand(ir_obj["ac_brand"]);
    }

    JsonArray devices = doc["devices"].as<JsonArray>();
    if (!devices.isNull()) {
        for (JsonObject device : devices) {
            if (g_config.device_count >= MIJIA_DEVICE_MAX) {
                break;
            }
            MijiaDevice& entry = g_config.devices[g_config.device_count];
            copyField(entry.name, sizeof(entry.name), device["name"]);
            // name_zh 优先；兼容旧字段 name_cn
            const char* name_zh = device["name_zh"];
            if (name_zh == nullptr || name_zh[0] == '\0') {
                name_zh = device["name_cn"];
            }
            copyField(entry.name_zh, sizeof(entry.name_zh), name_zh);
            copyField(entry.id, sizeof(entry.id), device["id"]);
            copyField(entry.mac, sizeof(entry.mac), device["mac"]);
            copyField(entry.ip, sizeof(entry.ip), device["ip"]);
            copyField(entry.token, sizeof(entry.token), device["token"]);
            copyField(entry.model, sizeof(entry.model), device["model"]);
            // ble.key 优先；兼容顶层 ble_key
            const char* ble_key = nullptr;
            JsonObject ble = device["ble"].as<JsonObject>();
            if (!ble.isNull()) {
                ble_key = ble["key"];
            }
            if (ble_key == nullptr || ble_key[0] == '\0') {
                ble_key = device["ble_key"];
            }
            copyField(entry.ble_key, sizeof(entry.ble_key), ble_key);
            g_config.device_count++;
        }
    }

    // 编组：members 以设备 id 引用，加载时解析成下标
    JsonArray groups = doc["device_groups"].as<JsonArray>();
    if (!groups.isNull()) {
        for (JsonVariant group_var : groups) {
            if (g_config.device_group_count >= MIJIA_GROUP_MAX) {
                break;
            }
            JsonObject group = group_var.as<JsonObject>();
            if (group.isNull()) {
                continue;
            }
            MijiaDeviceGroup& entry = g_config.device_groups[g_config.device_group_count];
            copyField(entry.name, sizeof(entry.name), group["name"]);
            const char* name_zh = group["name_zh"];
            if (name_zh == nullptr || name_zh[0] == '\0') {
                name_zh = group["name_cn"];
            }
            copyField(entry.name_zh, sizeof(entry.name_zh), name_zh);
            entry.member_count = 0;

            JsonArray members = group["members"].as<JsonArray>();
            if (!members.isNull()) {
                for (JsonVariant member_var : members) {
                    if (entry.member_count >= MIJIA_GROUP_MEMBER_MAX) {
                        break;
                    }
                    const char* member_id = nullptr;
                    if (member_var.is<const char*>()) {
                        // 兼容纯 id 字符串
                        member_id = member_var.as<const char*>();
                    } else {
                        JsonObject member = member_var.as<JsonObject>();
                        if (!member.isNull()) {
                            member_id = member["id"];
                        }
                    }
                    if (member_id == nullptr || member_id[0] == '\0') {
                        continue;
                    }
                    const int idx = mijiaFindDeviceIndexById(member_id);
                    if (idx < 0) {
                        continue;
                    }
                    // 去重
                    bool dup = false;
                    for (int i = 0; i < entry.member_count; i++) {
                        if (entry.member_indices[i] == idx) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup) {
                        continue;
                    }
                    entry.member_indices[entry.member_count++] = idx;
                }
            }
            g_config.device_group_count++;
        }
    }

    g_config.loaded = true;
    return true;
}

const AppConfig& getAppConfig() {
    return g_config;
}

const char* getAppTimezone() {
    if (g_config.timezone[0] != '\0') {
        return g_config.timezone;
    }
    return APP_TIMEZONE_DEFAULT;
}

const char* mijiaDeviceDisplayName(const MijiaDevice& dev) {
    // 界面标题只用英文 name，不切换中文字体
    if (dev.name[0] != '\0') {
        return dev.name;
    }
    return "device";
}

bool mijiaDeviceUsesBle(const MijiaDevice& dev) {
    if (dev.ble_key[0] == '\0') {
        return false;
    }
    // 有可用局域网 miIO 时优先走 WiFi
    if (isPrivateLanIp(dev.ip) && strlen(dev.token) >= 32) {
        return false;
    }
    return true;
}

int mijiaFindDeviceIndexById(const char* id) {
    if (id == nullptr || id[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < g_config.device_count; i++) {
        if (g_config.devices[i].id[0] != '\0' && strcmp(g_config.devices[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

bool saveAppConfigJson(const char* json) {
    if (json == nullptr) {
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, json);
    if (err) {
        return false;
    }

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        return false;
    }
    serializeJsonPretty(doc, file);
    file.close();
    return loadAppConfig();
}

bool saveAppConfigWifi(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = ssid;
    wifi["password"] = password == nullptr ? "" : password;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

bool saveAppConfigBrightness(const uint8_t brightness_percent) {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    const uint8_t pct = brightness_percent > 100 ? 100 : brightness_percent;
    doc["brightness"] = pct;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

// 取得/创建 sound 对象，保留已有字段
static JsonObject ensureSoundObject(JsonDocument& doc) {
    if (!doc["sound"].is<JsonObject>()) {
        doc["sound"].to<JsonObject>();
    }
    return doc["sound"].as<JsonObject>();
}

bool saveAppConfigTimeKeySound(const bool enabled) {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    JsonObject sound = ensureSoundObject(doc);
    sound["time_key"] = enabled;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

bool saveAppConfigMijiaOnOffSound(const bool enabled) {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    JsonObject sound = ensureSoundObject(doc);
    sound["mijia_on_off"] = enabled;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

bool saveAppConfigTimezone(const char* timezone) {
    if (timezone == nullptr || timezone[0] == '\0') {
        return false;
    }

    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    doc["timezone"] = timezone;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

const char* timeDefaultModeName(const TimeDefaultMode mode) {
    switch (mode) {
        case TimeDefaultMode::Ntp:
            return "ntp";
        case TimeDefaultMode::Countdown:
            return "countdown";
        case TimeDefaultMode::Stopwatch:
            return "stopwatch";
        case TimeDefaultMode::Up:
        default:
            return "up";
    }
}

TimeDefaultMode parseTimeDefaultMode(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return TimeDefaultMode::Up;
    }
    if (strcmp(s, "ntp") == 0 || strcmp(s, "clock") == 0 || strcmp(s, "clk") == 0) {
        return TimeDefaultMode::Ntp;
    }
    if (strcmp(s, "countdown") == 0 || strcmp(s, "cd") == 0) {
        return TimeDefaultMode::Countdown;
    }
    if (strcmp(s, "stopwatch") == 0 || strcmp(s, "sw") == 0) {
        return TimeDefaultMode::Stopwatch;
    }
    return TimeDefaultMode::Up;
}

bool saveAppConfigTimeDefaultMode(const TimeDefaultMode mode) {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    JsonObject time_obj = doc["time"].as<JsonObject>();
    if (time_obj.isNull()) {
        time_obj = doc["time"].to<JsonObject>();
    }
    time_obj["default"] = timeDefaultModeName(mode);

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

bool saveAppConfigTimePure(const bool enabled) {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    JsonObject time_obj = doc["time"].as<JsonObject>();
    if (time_obj.isNull()) {
        time_obj = doc["time"].to<JsonObject>();
    }
    time_obj["pure"] = enabled;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

const char* irDefaultCategoryName(const IrDefaultCategory category) {
    return category == IrDefaultCategory::Ac ? "ac" : "tv";
}

IrDefaultCategory parseIrDefaultCategory(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return IrDefaultCategory::Tv;
    }
    if (strcmp(s, "ac") == 0 || strcmp(s, "AC") == 0 || strcmp(s, "aircon") == 0) {
        return IrDefaultCategory::Ac;
    }
    return IrDefaultCategory::Tv;
}

const char* irTvBrandConfigName(const uint8_t idx) {
    static const char* names[] = {"samsung", "sony", "lg", "panasonic", "nec"};
    if (idx >= IR_TV_BRAND_COUNT) {
        return names[0];
    }
    return names[idx];
}

const char* irTvBrandDisplayName(const uint8_t idx) {
    static const char* names[] = {"Samsung", "Sony", "LG", "Panasonic", "NEC"};
    if (idx >= IR_TV_BRAND_COUNT) {
        return names[0];
    }
    return names[idx];
}

uint8_t parseIrTvBrand(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return 0;
    }
    for (uint8_t i = 0; i < IR_TV_BRAND_COUNT; i++) {
        if (strcasecmp(s, irTvBrandConfigName(i)) == 0 ||
            strcasecmp(s, irTvBrandDisplayName(i)) == 0) {
            return i;
        }
    }
    return 0;
}

const char* irAcBrandConfigName(const uint8_t idx) {
    static const char* names[] = {"midea", "gree", "haier", "aux", "hisense", "xiaomi"};
    if (idx >= IR_AC_BRAND_COUNT) {
        return names[0];
    }
    return names[idx];
}

const char* irAcBrandDisplayName(const uint8_t idx) {
    static const char* names[] = {"Midea", "Gree", "Haier", "AUX", "Hisense", "Xiaomi"};
    if (idx >= IR_AC_BRAND_COUNT) {
        return names[0];
    }
    return names[idx];
}

uint8_t parseIrAcBrand(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return 0;
    }
    for (uint8_t i = 0; i < IR_AC_BRAND_COUNT; i++) {
        if (strcasecmp(s, irAcBrandConfigName(i)) == 0 ||
            strcasecmp(s, irAcBrandDisplayName(i)) == 0) {
            return i;
        }
    }
    return 0;
}

uint8_t cycleIrTvBrand(const uint8_t cur, const int delta) {
    int idx = static_cast<int>(cur) + delta;
    idx = (idx % IR_TV_BRAND_COUNT + IR_TV_BRAND_COUNT) % IR_TV_BRAND_COUNT;
    return static_cast<uint8_t>(idx);
}

uint8_t cycleIrAcBrand(const uint8_t cur, const int delta) {
    int idx = static_cast<int>(cur) + delta;
    idx = (idx % IR_AC_BRAND_COUNT + IR_AC_BRAND_COUNT) % IR_AC_BRAND_COUNT;
    return static_cast<uint8_t>(idx);
}

IrDefaultCategory cycleIrDefaultCategory(const IrDefaultCategory cur, const int delta) {
    const int idx = (static_cast<int>(cur) + delta) & 1;
    return static_cast<IrDefaultCategory>(idx);
}

bool saveAppConfigInfrared(const IrDefaultCategory category, const uint8_t tv_brand,
                           const uint8_t ac_brand) {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                doc.clear();
            }
        }
    }

    // 统一写 Infrared；去掉旧小写键避免重复
    doc.remove("infrared");
    JsonObject ir_obj = doc["Infrared"].as<JsonObject>();
    if (ir_obj.isNull()) {
        ir_obj = doc["Infrared"].to<JsonObject>();
    }
    ir_obj["default"] = irDefaultCategoryName(category);
    ir_obj["tv_brand"] = irTvBrandConfigName(tv_brand);
    ir_obj["ac_brand"] = irAcBrandConfigName(ac_brand);

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
}

const char* cycleAppTimezonePreset(const char* current, const int delta) {
    static const char* kPresets[] = {
        "CST-8", "JST-9", "KST-9", "UTC", "GMT0", "CET-1", "EST5", "PST8",
    };
    constexpr int n = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    int idx = 0;
    if (current != nullptr && current[0] != '\0') {
        for (int i = 0; i < n; i++) {
            if (strcmp(current, kPresets[i]) == 0) {
                idx = i;
                break;
            }
        }
    }
    const int d = delta == 0 ? 1 : delta;
    idx = (idx + d) % n;
    if (idx < 0) {
        idx += n;
    }
    return kPresets[idx];
}

uint8_t brightnessPercentToHw(const uint8_t percent) {
    const uint8_t pct = percent > 100 ? 100 : percent;
    return static_cast<uint8_t>((static_cast<uint16_t>(pct) * 255 + 50) / 100);
}

uint8_t brightnessHwToPercent(const uint8_t hw) {
    return static_cast<uint8_t>((static_cast<uint16_t>(hw) * 100 + 127) / 255);
}

bool readAppConfigRaw(String& out) {
    out = "";
    if (!LittleFS.exists(CONFIG_PATH)) {
        return false;
    }

    File file = LittleFS.open(CONFIG_PATH, "r");
    if (!file) {
        return false;
    }
    out = file.readString();
    file.close();
    return true;
}
