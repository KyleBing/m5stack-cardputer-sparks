#include "app_config.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <cstring>

static constexpr const char* CONFIG_PATH = "/config.json";

static AppConfig g_config{};

// 安全拷贝字符串到定长缓冲区
static void copyField(char* dest, const size_t dest_size, const char* src) {
    if (src == nullptr || dest_size == 0) {
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

bool initAppConfigFs() {
    return LittleFS.begin(false);
}

bool loadAppConfig() {
    g_config = {};
    g_config.loaded = false;

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
        copyField(g_config.cursor_api_key, sizeof(g_config.cursor_api_key), cursor["api_key"]);
    }

    JsonArray devices = doc["devices"].as<JsonArray>();
    if (!devices.isNull()) {
        for (JsonObject device : devices) {
            if (g_config.device_count >= MIJIA_DEVICE_MAX) {
                break;
            }
            MijiaDevice& entry = g_config.devices[g_config.device_count];
            copyField(entry.name, sizeof(entry.name), device["name"]);
            copyField(entry.id, sizeof(entry.id), device["id"]);
            copyField(entry.mac, sizeof(entry.mac), device["mac"]);
            copyField(entry.ip, sizeof(entry.ip), device["ip"]);
            copyField(entry.token, sizeof(entry.token), device["token"]);
            copyField(entry.model, sizeof(entry.model), device["model"]);
            g_config.device_count++;
        }
    }

    g_config.loaded = true;
    return true;
}

const AppConfig& getAppConfig() {
    return g_config;
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
