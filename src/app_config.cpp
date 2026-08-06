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

// 把旧顶层 timezone 迁到 time 对象，并确保 calendar 对象存在
static void normalizeTimeCalendarConfig(JsonDocument& doc) {
    JsonObject time_obj = doc["time"].as<JsonObject>();
    if (time_obj.isNull()) {
        time_obj = doc["time"].to<JsonObject>();
    }
    if (time_obj["timezone"].isNull()) {
        const char* legacy_tz = doc["timezone"];
        if (legacy_tz != nullptr && legacy_tz[0] != '\0') {
            time_obj["timezone"] = legacy_tz;
        }
    }
    doc.remove("timezone");

    if (doc["calendar"].as<JsonObject>().isNull()) {
        doc["calendar"].to<JsonObject>();
    }
}

// 安全拷贝字符串到定长缓冲区
static void copyField(char* dest, const size_t dest_size, const char* src) {
    if (src == nullptr || dest_size == 0) {
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// 按 ssid 查找 wifis[] 下标；未找到返回 -1
static int findWifiProfileIndex(const char* ssid) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < g_config.wifi_count; i++) {
        if (strcmp(g_config.wifis[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

// 把 wifi_active 对应项镜像到 wifi_ssid / wifi_password
static void syncActiveWifiMirror() {
    g_config.wifi_ssid[0] = '\0';
    g_config.wifi_password[0] = '\0';
    if (g_config.wifi_count <= 0) {
        g_config.wifi_active[0] = '\0';
        return;
    }
    int idx = findWifiProfileIndex(g_config.wifi_active);
    if (idx < 0) {
        idx = 0;
        copyField(g_config.wifi_active, sizeof(g_config.wifi_active), g_config.wifis[0].ssid);
    }
    copyField(g_config.wifi_ssid, sizeof(g_config.wifi_ssid), g_config.wifis[idx].ssid);
    copyField(g_config.wifi_password, sizeof(g_config.wifi_password),
              g_config.wifis[idx].password);
}

// 从 JsonDocument 写入 wifis[] + wifi_active，并去掉旧 wifi 对象
static void writeWifisToDoc(JsonDocument& doc) {
    doc.remove("wifi");
    doc.remove("wifis");
    JsonArray arr = doc["wifis"].to<JsonArray>();
    for (int i = 0; i < g_config.wifi_count; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["ssid"] = g_config.wifis[i].ssid;
        item["password"] = g_config.wifis[i].password;
    }
    if (g_config.wifi_active[0] != '\0') {
        doc["wifi_active"] = g_config.wifi_active;
    } else if (g_config.wifi_count > 0) {
        doc["wifi_active"] = g_config.wifis[0].ssid;
    } else {
        doc["wifi_active"] = "";
    }
}

// 把当前内存中的 wifis 写回 /config.json 并 reload
static bool persistWifiProfiles() {
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

    writeWifisToDoc(doc);

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
    g_config.screen_invert = false;
    g_config.speaker_volume = 25; // 默认 25% ≈ setVolume(64)
    g_config.time_key_sound = true; // 默认开
    g_config.mijia_on_off_sound = true;
    g_config.time_default_mode = TimeDefaultMode::Up;
    g_config.week_start = WeekStartDay::Sunday;
    g_config.infrared_default = IrDefaultCategory::Tv;
    g_config.infrared_tv_brand = 0; // Samsung
    g_config.infrared_ac_brand = 0; // Midea
    // 空调自动化默认：29℃开 / 26℃关 / 过滤 3 次 / 制冷 26℃
    g_config.ac_auto = {};
    g_config.ac_auto.on_temp_c = 29;
    g_config.ac_auto.off_temp_c = 26;
    g_config.ac_auto.filter_count = 3;
    g_config.ac_auto.ac_brand = 0;
    g_config.ac_auto.ac_mode = 0; // cool
    g_config.ac_auto.ac_temp_c = 26;
    g_config.ac_auto.ac_fan = 0; // auto
    g_config.hid_keyboard_transport = HidKeyboardTransport::Ble;
    g_config.hid_keyboard_imu_sensitivity = 5;
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

    // WiFi：优先 wifis[] + wifi_active；否则迁移旧 wifi 对象
    g_config.wifi_count = 0;
    g_config.wifi_active[0] = '\0';
    g_config.wifi_ssid[0] = '\0';
    g_config.wifi_password[0] = '\0';
    {
        JsonArray wifis = doc["wifis"].as<JsonArray>();
        if (!wifis.isNull() && wifis.size() > 0) {
            for (JsonObject item : wifis) {
                if (g_config.wifi_count >= WIFI_PROFILE_MAX) {
                    break;
                }
                const char* ssid = item["ssid"] | "";
                if (ssid[0] == '\0') {
                    continue;
                }
                WifiProfile& p = g_config.wifis[g_config.wifi_count];
                copyField(p.ssid, sizeof(p.ssid), ssid);
                copyField(p.password, sizeof(p.password), item["password"] | "");
                g_config.wifi_count++;
            }
            copyField(g_config.wifi_active, sizeof(g_config.wifi_active),
                      doc["wifi_active"] | "");
        } else {
            // 兼容旧格式："wifi": { "ssid", "password" }
            JsonObject wifi = doc["wifi"];
            if (!wifi.isNull()) {
                const char* ssid = wifi["ssid"] | "";
                if (ssid[0] != '\0') {
                    copyField(g_config.wifis[0].ssid, sizeof(g_config.wifis[0].ssid), ssid);
                    copyField(g_config.wifis[0].password, sizeof(g_config.wifis[0].password),
                              wifi["password"] | "");
                    g_config.wifi_count = 1;
                    copyField(g_config.wifi_active, sizeof(g_config.wifi_active), ssid);
                }
            }
        }
        syncActiveWifiMirror();
    }

    JsonObject cursor = doc["cursor"];
    if (!cursor.isNull()) {
        copyField(g_config.cursor_token, sizeof(g_config.cursor_token), cursor["token"]);
    }

    // screen：brightness / invert（兼容旧顶层 brightness）
    {
        g_config.screen_invert = false;
        int raw = 30;
        JsonObject screen = doc["screen"];
        if (!screen.isNull()) {
            if (!screen["brightness"].isNull()) {
                raw = screen["brightness"] | 30;
            } else {
                raw = doc["brightness"] | 30;
            }
            g_config.screen_invert = screen["invert"] | false;
        } else {
            raw = doc["brightness"] | 30;
        }
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
    g_config.speaker_volume = 25;
    JsonObject sound = doc["sound"];
    if (!sound.isNull()) {
        g_config.time_key_sound = sound["time_key"] | true;
        g_config.mijia_on_off_sound = sound["mijia_on_off"] | true;
        int vol = sound["volume"] | 25;
        if (vol < 0) {
            vol = 0;
        }
        if (vol > 100) {
            vol = 100;
        }
        g_config.speaker_volume = static_cast<uint8_t>(vol);
    }

    // Time：优先新路径；timezone 兼容旧顶层字段（time.pure 已废弃，忽略）
    g_config.time_default_mode = TimeDefaultMode::Up;
    JsonObject time_obj = doc["time"];
    if (!time_obj.isNull()) {
        g_config.time_default_mode = parseTimeDefaultMode(time_obj["default"]);
    }
    const char* tz = time_obj.isNull() ? nullptr : time_obj["timezone"];
    if (tz == nullptr || tz[0] == '\0') {
        tz = doc["timezone"];
    }
    if (tz != nullptr && tz[0] != '\0') {
        copyField(g_config.timezone, sizeof(g_config.timezone), tz);
    }

    // Calendar：只认 calendar.week_start，缺失时默认周日
    g_config.week_start = WeekStartDay::Sunday;
    JsonObject calendar_obj = doc["calendar"];
    if (!calendar_obj.isNull()) {
        g_config.week_start = parseWeekStartDay(calendar_obj["week_start"]);
    }

    // infrared：default / tv_brand / ac_brand（兼容旧大写 Infrared）
    g_config.infrared_default = IrDefaultCategory::Tv;
    g_config.infrared_tv_brand = 0;
    g_config.infrared_ac_brand = 0;
    JsonObject ir_obj = doc["infrared"];
    if (ir_obj.isNull()) {
        ir_obj = doc["Infrared"];
    }
    if (!ir_obj.isNull()) {
        g_config.infrared_default = parseIrDefaultCategory(ir_obj["default"]);
        g_config.infrared_tv_brand = parseIrTvBrand(ir_obj["tv_brand"]);
        g_config.infrared_ac_brand = parseIrAcBrand(ir_obj["ac_brand"]);
    }

    // ac_auto：温湿度触发开关空调
    g_config.ac_auto = {};
    g_config.ac_auto.on_temp_c = 29;
    g_config.ac_auto.off_temp_c = 26;
    g_config.ac_auto.filter_count = 3;
    g_config.ac_auto.ac_brand = g_config.infrared_ac_brand;
    g_config.ac_auto.ac_mode = 0;
    g_config.ac_auto.ac_temp_c = 26;
    g_config.ac_auto.ac_fan = 0;
    JsonObject ac_auto_obj = doc["ac_auto"];
    if (!ac_auto_obj.isNull()) {
        copyField(g_config.ac_auto.sensor_id, sizeof(g_config.ac_auto.sensor_id),
                  ac_auto_obj["sensor_id"] | "");
        int on_t = ac_auto_obj["on_temp"] | 29;
        int off_t = ac_auto_obj["off_temp"] | 26;
        int filter = ac_auto_obj["filter"] | 3;
        int set_t = ac_auto_obj["ac_temp"] | 26;
        g_config.ac_auto.on_temp_c = static_cast<uint8_t>(constrain(on_t, 16, 40));
        g_config.ac_auto.off_temp_c = static_cast<uint8_t>(constrain(off_t, 10, 35));
        g_config.ac_auto.filter_count = static_cast<uint8_t>(constrain(filter, 1, 10));
        g_config.ac_auto.ac_temp_c = static_cast<uint8_t>(constrain(set_t, 16, 30));
        g_config.ac_auto.ac_brand = parseIrAcBrand(ac_auto_obj["ac_brand"]);
        g_config.ac_auto.ac_mode = parseAcAutoMode(ac_auto_obj["ac_mode"]);
        g_config.ac_auto.ac_fan = parseAcAutoFan(ac_auto_obj["ac_fan"]);
        normalizeAcAutoConfig(g_config.ac_auto);
    }

    // HID Keyboard：缺字段时保持安全默认 BLE、灵敏度 5
    JsonObject hid_keyboard_obj = doc["hid_keyboard"];
    if (!hid_keyboard_obj.isNull()) {
        g_config.hid_keyboard_transport =
            parseHidKeyboardTransport(hid_keyboard_obj["transport"]);
        int sensitivity = hid_keyboard_obj["imu_sensitivity"] | 5;
        if (sensitivity < 1 || sensitivity > 10) {
            sensitivity = 5;
        }
        g_config.hid_keyboard_imu_sensitivity = static_cast<uint8_t>(sensitivity);
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
            // 快捷键：单字符 a-z / 0-9
            entry.hotkey = '\0';
            const char* hotkey = device["hotkey"];
            if (hotkey != nullptr && hotkey[0] != '\0') {
                entry.hotkey = mijiaNormalizeHotkey(hotkey[0]);
            }
            g_config.device_count++;
        }
    }

    // 快捷键去重：保留靠前的第一个，后面相同键清空
    {
        bool seen[256] = {};
        for (int i = 0; i < g_config.device_count; i++) {
            const unsigned char h = static_cast<unsigned char>(g_config.devices[i].hotkey);
            if (h == 0) {
                continue;
            }
            if (seen[h]) {
                g_config.devices[i].hotkey = '\0';
            } else {
                seen[h] = true;
            }
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

char mijiaNormalizeHotkey(const char c) {
    char key = c;
    if (key >= 'A' && key <= 'Z') {
        key = static_cast<char>(key - 'A' + 'a');
    }
    // q 留给快速选择页开关
    if (key == 'q') {
        return '\0';
    }
    if ((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9')) {
        return key;
    }
    return '\0';
}

int mijiaFindDeviceIndexByHotkey(const char hotkey) {
    const char key = mijiaNormalizeHotkey(hotkey);
    if (key == '\0') {
        return -1;
    }
    for (int i = 0; i < g_config.device_count; i++) {
        if (g_config.devices[i].hotkey == key) {
            return i;
        }
    }
    return -1;
}

bool saveAppConfigDeviceHotkey(const int device_idx, const char hotkey) {
    if (device_idx < 0 || device_idx >= g_config.device_count) {
        return false;
    }
    const char key = mijiaNormalizeHotkey(hotkey);

    JsonDocument doc;
    if (LittleFS.exists(CONFIG_PATH)) {
        File in = LittleFS.open(CONFIG_PATH, "r");
        if (in) {
            const DeserializationError err = deserializeJson(doc, in);
            in.close();
            if (err) {
                return false;
            }
        }
    }

    JsonArray devices = doc["devices"].as<JsonArray>();
    if (devices.isNull() || device_idx >= static_cast<int>(devices.size())) {
        return false;
    }

    // 同键其它设备清空，保证唯一
    for (size_t i = 0; i < devices.size(); i++) {
        JsonObject d = devices[i].as<JsonObject>();
        if (d.isNull()) {
            continue;
        }
        if (static_cast<int>(i) == device_idx) {
            if (key == '\0') {
                d.remove("hotkey");
            } else {
                char buf[2] = {key, '\0'};
                d["hotkey"] = buf;
            }
            continue;
        }
        if (key == '\0') {
            continue;
        }
        const char* existing = d["hotkey"];
        if (existing != nullptr && existing[0] != '\0' &&
            mijiaNormalizeHotkey(existing[0]) == key) {
            d.remove("hotkey");
        }
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return loadAppConfig();
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
    normalizeTimeCalendarConfig(doc);

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        return false;
    }
    serializeJsonPretty(doc, file);
    file.close();
    return loadAppConfig();
}

bool saveAppConfigWifi(const char* ssid, const char* password) {
    // 扫网连上后：upsert 并设为 active
    return upsertAppConfigWifi(ssid, password, true);
}

bool setAppConfigWifiActive(const char* ssid) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    if (findWifiProfileIndex(ssid) < 0) {
        return false;
    }
    copyField(g_config.wifi_active, sizeof(g_config.wifi_active), ssid);
    syncActiveWifiMirror();
    return persistWifiProfiles();
}

bool upsertAppConfigWifi(const char* ssid, const char* password, const bool set_active) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    int idx = findWifiProfileIndex(ssid);
    if (idx >= 0) {
        copyField(g_config.wifis[idx].password, sizeof(g_config.wifis[idx].password),
                  password == nullptr ? "" : password);
    } else {
        if (g_config.wifi_count >= WIFI_PROFILE_MAX) {
            return false;
        }
        idx = g_config.wifi_count;
        copyField(g_config.wifis[idx].ssid, sizeof(g_config.wifis[idx].ssid), ssid);
        copyField(g_config.wifis[idx].password, sizeof(g_config.wifis[idx].password),
                  password == nullptr ? "" : password);
        g_config.wifi_count++;
    }

    if (set_active) {
        copyField(g_config.wifi_active, sizeof(g_config.wifi_active), ssid);
    }
    syncActiveWifiMirror();
    return persistWifiProfiles();
}

bool removeAppConfigWifi(const char* ssid) {
    const int idx = findWifiProfileIndex(ssid);
    if (idx < 0) {
        return false;
    }

    const bool was_active = strcmp(g_config.wifi_active, g_config.wifis[idx].ssid) == 0;
    for (int i = idx; i + 1 < g_config.wifi_count; i++) {
        g_config.wifis[i] = g_config.wifis[i + 1];
    }
    g_config.wifi_count--;
    g_config.wifis[g_config.wifi_count] = WifiProfile{};
    if (was_active) {
        // 清空后由 syncActiveWifiMirror 回落到第一条（无档案则全清）
        g_config.wifi_active[0] = '\0';
    }
    syncActiveWifiMirror();
    return persistWifiProfiles();
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
    // 统一写 screen.brightness；去掉旧顶层键
    doc.remove("brightness");
    JsonObject screen = doc["screen"].as<JsonObject>();
    if (screen.isNull()) {
        screen = doc["screen"].to<JsonObject>();
    }
    screen["brightness"] = pct;

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

bool saveAppConfigScreenInvert(const bool invert) {
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

    JsonObject screen = doc["screen"].as<JsonObject>();
    if (screen.isNull()) {
        screen = doc["screen"].to<JsonObject>();
    }
    screen["invert"] = invert;

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

void setAppConfigSpeakerVolumeLocal(const uint8_t volume_percent) {
    g_config.speaker_volume = volume_percent > 100 ? 100 : volume_percent;
}

bool saveAppConfigSpeakerVolume(const uint8_t volume_percent) {
    // 先更新内存，UI / getAppConfig 立刻一致；再写盘（不再整表 reload）
    const uint8_t pct = volume_percent > 100 ? 100 : volume_percent;
    g_config.speaker_volume = pct;

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
    sound["volume"] = pct;

    if (doc["devices"].isNull()) {
        doc["devices"].to<JsonArray>();
    }

    File out = LittleFS.open(CONFIG_PATH, "w");
    if (!out) {
        return false;
    }
    serializeJsonPretty(doc, out);
    out.close();
    return true;
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

    normalizeTimeCalendarConfig(doc);
    JsonObject time_obj = doc["time"].as<JsonObject>();
    time_obj["timezone"] = timezone;

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

const char* weekStartDayName(const WeekStartDay day) {
    return day == WeekStartDay::Monday ? "monday" : "sunday";
}

WeekStartDay parseWeekStartDay(const char* s) {
    if (s != nullptr && (strcasecmp(s, "monday") == 0 || strcasecmp(s, "mon") == 0)) {
        return WeekStartDay::Monday;
    }
    return WeekStartDay::Sunday;
}

bool saveAppConfigWeekStart(const WeekStartDay day) {
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

    normalizeTimeCalendarConfig(doc);
    JsonObject calendar_obj = doc["calendar"].as<JsonObject>();
    calendar_obj["week_start"] = weekStartDayName(day);

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

    // 统一写 infrared；去掉旧大写键避免重复
    doc.remove("Infrared");
    JsonObject ir_obj = doc["infrared"].as<JsonObject>();
    if (ir_obj.isNull()) {
        ir_obj = doc["infrared"].to<JsonObject>();
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

const char* acAutoModeConfigName(const uint8_t idx) {
    static const char* const kNames[] = {"cool", "heat", "dry", "fan", "auto"};
    if (idx >= AC_AUTO_MODE_COUNT) {
        return kNames[0];
    }
    return kNames[idx];
}

const char* acAutoModeDisplayName(const uint8_t idx) {
    static const char* const kNames[] = {"Cool", "Heat", "Dry", "Fan", "Auto"};
    if (idx >= AC_AUTO_MODE_COUNT) {
        return kNames[0];
    }
    return kNames[idx];
}

uint8_t parseAcAutoMode(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return 0;
    }
    for (uint8_t i = 0; i < AC_AUTO_MODE_COUNT; i++) {
        if (strcasecmp(s, acAutoModeConfigName(i)) == 0) {
            return i;
        }
    }
    return 0;
}

uint8_t cycleAcAutoMode(const uint8_t cur, const int delta) {
    int idx = static_cast<int>(cur) + delta;
    idx = (idx % AC_AUTO_MODE_COUNT + AC_AUTO_MODE_COUNT) % AC_AUTO_MODE_COUNT;
    return static_cast<uint8_t>(idx);
}

const char* acAutoFanConfigName(const uint8_t idx) {
    static const char* const kNames[] = {"auto", "min", "low", "med", "high", "max"};
    if (idx >= AC_AUTO_FAN_COUNT) {
        return kNames[0];
    }
    return kNames[idx];
}

const char* acAutoFanDisplayName(const uint8_t idx) {
    static const char* const kNames[] = {"Auto", "Min", "Low", "Med", "High", "Max"};
    if (idx >= AC_AUTO_FAN_COUNT) {
        return kNames[0];
    }
    return kNames[idx];
}

uint8_t parseAcAutoFan(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return 0;
    }
    for (uint8_t i = 0; i < AC_AUTO_FAN_COUNT; i++) {
        if (strcasecmp(s, acAutoFanConfigName(i)) == 0) {
            return i;
        }
    }
    return 0;
}

uint8_t cycleAcAutoFan(const uint8_t cur, const int delta) {
    int idx = static_cast<int>(cur) + delta;
    idx = (idx % AC_AUTO_FAN_COUNT + AC_AUTO_FAN_COUNT) % AC_AUTO_FAN_COUNT;
    return static_cast<uint8_t>(idx);
}

void normalizeAcAutoConfig(AcAutoConfig& cfg) {
    if (cfg.on_temp_c < 16) {
        cfg.on_temp_c = 16;
    }
    if (cfg.on_temp_c > 40) {
        cfg.on_temp_c = 40;
    }
    if (cfg.off_temp_c < 10) {
        cfg.off_temp_c = 10;
    }
    if (cfg.off_temp_c > 35) {
        cfg.off_temp_c = 35;
    }
    // 开阈值必须严格高于关阈值
    if (cfg.on_temp_c <= cfg.off_temp_c) {
        if (cfg.off_temp_c >= 40) {
            cfg.off_temp_c = 39;
            cfg.on_temp_c = 40;
        } else {
            cfg.on_temp_c = static_cast<uint8_t>(cfg.off_temp_c + 1);
        }
    }
    if (cfg.filter_count < 1) {
        cfg.filter_count = 1;
    }
    if (cfg.filter_count > 10) {
        cfg.filter_count = 10;
    }
    if (cfg.ac_brand >= IR_AC_BRAND_COUNT) {
        cfg.ac_brand = 0;
    }
    if (cfg.ac_mode >= AC_AUTO_MODE_COUNT) {
        cfg.ac_mode = 0;
    }
    if (cfg.ac_fan >= AC_AUTO_FAN_COUNT) {
        cfg.ac_fan = 0;
    }
    if (cfg.ac_temp_c < 16) {
        cfg.ac_temp_c = 16;
    }
    if (cfg.ac_temp_c > 30) {
        cfg.ac_temp_c = 30;
    }
}

bool saveAppConfigAcAuto(const AcAutoConfig& cfg_in) {
    AcAutoConfig cfg = cfg_in;
    normalizeAcAutoConfig(cfg);

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

    JsonObject obj = doc["ac_auto"].as<JsonObject>();
    if (obj.isNull()) {
        obj = doc["ac_auto"].to<JsonObject>();
    }
    obj["sensor_id"] = cfg.sensor_id;
    obj["on_temp"] = cfg.on_temp_c;
    obj["off_temp"] = cfg.off_temp_c;
    obj["filter"] = cfg.filter_count;
    obj["ac_brand"] = irAcBrandConfigName(cfg.ac_brand);
    obj["ac_mode"] = acAutoModeConfigName(cfg.ac_mode);
    obj["ac_temp"] = cfg.ac_temp_c;
    obj["ac_fan"] = acAutoFanConfigName(cfg.ac_fan);

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

const char* hidKeyboardTransportName(const HidKeyboardTransport transport) {
    return transport == HidKeyboardTransport::Usb ? "usb" : "ble";
}

HidKeyboardTransport parseHidKeyboardTransport(const char* s) {
    if (s != nullptr && strcasecmp(s, "usb") == 0) {
        return HidKeyboardTransport::Usb;
    }
    return HidKeyboardTransport::Ble;
}

bool saveAppConfigHidKeyboard(const HidKeyboardTransport transport,
                              const uint8_t imu_sensitivity) {
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

    const uint8_t sensitivity =
        imu_sensitivity < 1 || imu_sensitivity > 10 ? 5 : imu_sensitivity;
    JsonObject hid_keyboard_obj = doc["hid_keyboard"].as<JsonObject>();
    if (hid_keyboard_obj.isNull()) {
        hid_keyboard_obj = doc["hid_keyboard"].to<JsonObject>();
    }
    hid_keyboard_obj["transport"] = hidKeyboardTransportName(transport);
    // 兼容清理已写入过的旧字段；IMU 开关是临时运行状态
    hid_keyboard_obj.remove("imu_mouse");
    hid_keyboard_obj["imu_sensitivity"] = sensitivity;

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

uint8_t speakerVolumePercentToHw(const uint8_t percent) {
    const uint8_t pct = percent > 100 ? 100 : percent;
    return static_cast<uint8_t>((static_cast<uint16_t>(pct) * 255 + 50) / 100);
}

uint8_t speakerVolumeHwToPercent(const uint8_t hw) {
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
