#include "miio_client.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <mbedtls/aes.h>
#include <mbedtls/md5.h>
#include <cstring>

static constexpr uint16_t MIIO_PORT = 54321;
static constexpr int MIIO_TIMEOUT_MS = 2000;
static constexpr int MIIO_QUERY_TIMEOUT_MS = 1000;

static uint32_t g_query_deadline_ms = 0;

// 状态查询共用总超时（握手 + 命令）
class MiioQueryScope {
public:
    explicit MiioQueryScope(const uint32_t timeout_ms) { g_query_deadline_ms = millis() + timeout_ms; }
    ~MiioQueryScope() { g_query_deadline_ms = 0; }
};

static WiFiUDP g_udp;
static uint8_t g_token[16];
static uint8_t g_aes_key[16];
static uint8_t g_aes_iv[16];
static uint8_t g_device_id[4];
static uint32_t g_device_ts = 0;
static uint32_t g_msg_id = 1;
static char g_last_error[32] = "ok";

// 大端读写时间戳
static uint32_t readBe32(const uint8_t* buf) {
    return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
           (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

static void writeBe32(uint8_t* buf, const uint32_t value) {
    buf[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(value & 0xFF);
}

// 计算 AES key / iv
static void calcKeyIv() {
    mbedtls_md5(g_token, 16, g_aes_key);
    uint8_t tmp[32];
    memcpy(tmp, g_aes_key, 16);
    memcpy(tmp + 16, g_token, 16);
    mbedtls_md5(tmp, 32, g_aes_iv);
}

// AES-128-CBC 加密
static void miioEncrypt(uint8_t* buf, const size_t length) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, g_aes_key, 128);
    uint8_t iv[16];
    memcpy(iv, g_aes_iv, 16);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, length, iv, buf, buf);
    mbedtls_aes_free(&aes);
}

// AES-128-CBC 解密
static void miioDecrypt(uint8_t* buf, const size_t length) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, g_aes_key, 128);
    uint8_t iv[16];
    memcpy(iv, g_aes_iv, 16);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, length, iv, buf, buf);
    mbedtls_aes_free(&aes);
}

static void setResult(MiioResult& result, const bool ok, const char* message) {
    result.ok = ok;
    strncpy(result.message, message, sizeof(result.message) - 1);
    result.message[sizeof(result.message) - 1] = '\0';
}

bool miioParseTokenHex(const char* hex, uint8_t token[16]) {
    if (hex == nullptr || strlen(hex) != 32) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char* end = nullptr;
        const long val = strtol(pair, &end, 16);
        if (end == pair || val < 0 || val > 255) {
            return false;
        }
        token[i] = static_cast<uint8_t>(val);
    }
    return true;
}

// 清空残留 UDP 包
static void udpFlush() {
    while (g_udp.parsePacket() > 0) {
        while (g_udp.available() > 0) {
            g_udp.read();
        }
    }
}

// 发送 UDP 并等待来自目标 IP 的回复
static int udpSendTo(const char* ip_str, const uint8_t* packet, const size_t packet_len,
                     uint8_t* out, const size_t out_max) {
    IPAddress ip;
    if (!ip.fromString(ip_str)) {
        strncpy(g_last_error, "bad ip", sizeof(g_last_error));
        return -1;
    }

    udpFlush();

    if (g_udp.beginPacket(ip, MIIO_PORT) == 0) {
        strncpy(g_last_error, "tx begin", sizeof(g_last_error));
        return -1;
    }
    g_udp.write(packet, packet_len);
    if (!g_udp.endPacket()) {
        strncpy(g_last_error, "tx end", sizeof(g_last_error));
        return -1;
    }

    const uint32_t deadline =
        g_query_deadline_ms > 0 ? g_query_deadline_ms : (millis() + MIIO_TIMEOUT_MS);
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
        strncpy(g_last_error, "timeout", sizeof(g_last_error));
        return -1;
    }
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        const int len = g_udp.parsePacket();
        if (len > 0) {
            const IPAddress remote = g_udp.remoteIP();
            if (remote != ip) {
                while (g_udp.available() > 0) {
                    g_udp.read();
                }
                continue;
            }
            return g_udp.read(out, out_max);
        }
        delay(1);
    }

    strncpy(g_last_error, "timeout", sizeof(g_last_error));
    return -1;
}

// miIO 握手
static bool miioHandshake(const char* ip) {
    uint8_t hello[32];
    memset(hello, 0xFF, sizeof(hello));
    hello[0] = 0x21;
    hello[1] = 0x31;
    hello[2] = 0x00;
    hello[3] = 0x20;

    uint8_t reply[64];
    const int n = udpSendTo(ip, hello, sizeof(hello), reply, sizeof(reply));
    if (n != 32 || reply[0] != 0x21 || reply[1] != 0x31) {
        strncpy(g_last_error, "handshake", sizeof(g_last_error));
        return false;
    }

    memcpy(g_device_id, reply + 8, 4);
    g_device_ts = readBe32(reply + 12);
    return true;
}

// 发送 miIO JSON 命令
static bool miioSendJson(const char* ip, const char* json, char* resp, const size_t resp_max) {
    const size_t payload_len = strlen(json) + 1;
    const size_t pad = 16 - (payload_len & 0x0F);
    const size_t enc_len = payload_len + pad;
    const size_t total = 32 + enc_len;

    uint8_t* buf = static_cast<uint8_t*>(malloc(total));
    if (buf == nullptr) {
        strncpy(g_last_error, "oom", sizeof(g_last_error));
        return false;
    }

    buf[0] = 0x21;
    buf[1] = 0x31;
    buf[2] = static_cast<uint8_t>((total >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(total & 0xFF);
    memset(buf + 4, 0, 4);
    memcpy(buf + 8, g_device_id, 4);
    // 命令包时间戳 = 设备时间 + 1 秒（miIO 协议要求）
    writeBe32(buf + 12, g_device_ts + 1);

    memcpy(buf + 32, json, payload_len);
    for (size_t i = 0; i < pad; i++) {
        buf[32 + payload_len + i] = static_cast<uint8_t>(pad & 0x0F);
    }

    miioEncrypt(buf + 32, enc_len);

    // checksum = MD5(header16 + token + ciphertext)
    uint8_t checksum_src[16 + 16 + enc_len];
    memcpy(checksum_src, buf, 16);
    memcpy(checksum_src + 16, g_token, 16);
    memcpy(checksum_src + 32, buf + 32, enc_len);
    mbedtls_md5(checksum_src, sizeof(checksum_src), buf + 16);

    uint8_t rx[512];
    const int n = udpSendTo(ip, buf, total, rx, sizeof(rx));
    free(buf);

    if (n <= 32) {
        strncpy(g_last_error, "no reply", sizeof(g_last_error));
        return false;
    }

    miioDecrypt(rx + 32, n - 32);

    const uint8_t pad_byte = rx[n - 1];
    size_t json_len = n - 32;
    if (pad_byte > 0 && pad_byte <= 16 && pad_byte <= json_len) {
        json_len -= pad_byte;
    }

    const size_t copy_len = json_len < resp_max - 1 ? json_len : resp_max - 1;
    memcpy(resp, rx + 32, copy_len);
    resp[copy_len] = '\0';

    // 更新设备时间戳，便于连续命令
    g_device_ts = readBe32(rx + 12);
    return true;
}

// 执行 miIO 命令（g_token 须已设置）
static bool miioCommand(const char* ip, const char* method, const char* params_json,
                        char* resp, const size_t resp_max) {
    WiFi.setSleep(false);

    if (!g_udp.begin(0)) {
        strncpy(g_last_error, "udp init", sizeof(g_last_error));
        return false;
    }

    calcKeyIv();

    if (!miioHandshake(ip)) {
        g_udp.stop();
        return false;
    }

    char json[512];
    snprintf(json, sizeof(json), "{\"id\":%lu,\"method\":\"%s\",\"params\":%s}",
             static_cast<unsigned long>(g_msg_id++), method, params_json);

    const bool ok = miioSendJson(ip, json, resp, resp_max);
    g_udp.stop();
    return ok;
}

// 解析 result 首项为开关状态
static bool parseBoolResult(const char* json, bool& value) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }

    if (doc["error"].is<JsonObject>()) {
        return false;
    }

    JsonArray result = doc["result"].as<JsonArray>();
    if (result.isNull() || result.size() == 0) {
        if (doc["result"].is<bool>()) {
            value = doc["result"].as<bool>();
            return true;
        }
        return false;
    }

    if (result[0].is<bool>()) {
        value = result[0].as<bool>();
        return true;
    }
    if (result[0].is<const char*>()) {
        const char* s = result[0].as<const char*>();
        value = (strcmp(s, "on") == 0 || strcmp(s, "true") == 0 || strcmp(s, "1") == 0);
        return true;
    }
    if (result[0].is<int>()) {
        value = result[0].as<int>() != 0;
        return true;
    }
    return false;
}

MiioResult miioGetPower(const char* ip, const char* token_hex, bool& on) {
    const MiioQueryScope query_scope(MIIO_QUERY_TIMEOUT_MS);
    MiioResult result{};
    if (!miioParseTokenHex(token_hex, g_token)) {
        setResult(result, false, "bad token");
        return result;
    }

    char resp[256];
    if (!miioCommand(ip, "get_prop", "[\"power\"]", resp, sizeof(resp))) {
        setResult(result, false, g_last_error);
        return result;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        setResult(result, false, "bad json");
        return result;
    }

    if (doc["error"].is<JsonObject>()) {
        const char* msg = doc["error"]["message"] | "error";
        setResult(result, false, msg);
        return result;
    }

    if (!parseBoolResult(resp, on)) {
        setResult(result, false, resp);
        return result;
    }

    setResult(result, true, on ? "ON" : "OFF");
    return result;
}

MiioResult miioSetPower(const char* ip, const char* token_hex, const bool on) {
    MiioResult result{};
    if (!miioParseTokenHex(token_hex, g_token)) {
        setResult(result, false, "bad token");
        return result;
    }

    char resp[256];
    char params[24];
    snprintf(params, sizeof(params), "[\"%s\"]", on ? "on" : "off");

    if (!miioCommand(ip, "set_power", params, resp, sizeof(resp))) {
        setResult(result, false, g_last_error);
        return result;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        setResult(result, false, "bad json");
        return result;
    }

    if (doc["error"].is<JsonObject>()) {
        const char* msg = doc["error"]["message"] | "error";
        setResult(result, false, msg);
        return result;
    }

    setResult(result, true, on ? "ON" : "OFF");
    return result;
}

// 执行 miIO 命令并检查 error 字段
static MiioResult miioRun(const char* ip, const char* token_hex, const char* method,
                          const char* params_json, char* resp, const size_t resp_max) {
    MiioResult result{};
    if (!miioParseTokenHex(token_hex, g_token)) {
        setResult(result, false, "bad token");
        return result;
    }

    if (!miioCommand(ip, method, params_json, resp, resp_max)) {
        setResult(result, false, g_last_error);
        return result;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        setResult(result, false, "bad json");
        return result;
    }

    if (doc["error"].is<JsonObject>()) {
        const char* msg = doc["error"]["message"] | "error";
        setResult(result, false, msg);
        return result;
    }

    setResult(result, true, "ok");
    return result;
}

static int clampInt(const int value, const int min_v, const int max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

// 解析 get_prop 返回数组中的整型
static bool parseIntAt(const char* json, const int index, int& value) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }
    JsonArray result = doc["result"].as<JsonArray>();
    if (result.isNull() || static_cast<int>(result.size()) <= index) {
        return false;
    }
    if (result[index].is<int>()) {
        value = result[index].as<int>();
        return true;
    }
    if (result[index].is<const char*>()) {
        value = atoi(result[index].as<const char*>());
        return true;
    }
    return false;
}

MiioResult miioGetLightStatus(const char* ip, const char* token_hex, bool& on, int& bright,
                              bool& bright_known, int& color_temp, bool& ct_known) {
    const MiioQueryScope query_scope(MIIO_QUERY_TIMEOUT_MS);
    MiioResult result{};
    bright_known = false;
    ct_known = false;

    char resp[320];
    if (!miioParseTokenHex(token_hex, g_token)) {
        setResult(result, false, "bad token");
        return result;
    }
    if (!miioCommand(ip, "get_prop", "[\"power\",\"bright\",\"ct\"]", resp, sizeof(resp))) {
        setResult(result, false, g_last_error);
        return result;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        setResult(result, false, "bad json");
        return result;
    }
    if (doc["error"].is<JsonObject>()) {
        const char* msg = doc["error"]["message"] | "error";
        setResult(result, false, msg);
        return result;
    }

    if (!parseBoolResult(resp, on)) {
        setResult(result, false, "no power");
        return result;
    }

    int b = 0;
    if (parseIntAt(resp, 1, b)) {
        bright = clampInt(b, 1, 100);
        bright_known = true;
    }

    int ct = 0;
    if (parseIntAt(resp, 2, ct) && ct > 0) {
        color_temp = ct;
        ct_known = true;
    }

    char msg[32];
    if (bright_known && ct_known) {
        snprintf(msg, sizeof(msg), on ? "ON %d%% %dK" : "OFF", bright, color_temp);
    } else if (bright_known) {
        snprintf(msg, sizeof(msg), on ? "ON %d%%" : "OFF", bright);
    } else {
        snprintf(msg, sizeof(msg), on ? "ON" : "OFF");
    }
    setResult(result, true, msg);
    return result;
}

MiioResult miioSetBright(const char* ip, const char* token_hex, const int bright) {
    char params[16];
    snprintf(params, sizeof(params), "[%d]", clampInt(bright, 1, 100));
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "set_bright", params, resp, sizeof(resp));
    if (result.ok) {
        char msg[16];
        snprintf(msg, sizeof(msg), "B=%d", clampInt(bright, 1, 100));
        setResult(result, true, msg);
    }
    return result;
}

MiioResult miioSetColorTemp(const char* ip, const char* token_hex, const int kelvin) {
    char params[32];
    snprintf(params, sizeof(params), "[%d,\"smooth\",300]", kelvin);
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "set_ct_abx", params, resp, sizeof(resp));
    if (result.ok) {
        char msg[16];
        snprintf(msg, sizeof(msg), "CT=%dK", kelvin);
        setResult(result, true, msg);
    }
    return result;
}

MiioResult miioFanP5GetStatus(const char* ip, const char* token_hex, bool& on, int& speed,
                              bool& roll, int& mode) {
    const MiioQueryScope query_scope(MIIO_QUERY_TIMEOUT_MS);
    char resp[384];
    if (!miioParseTokenHex(token_hex, g_token)) {
        MiioResult result{};
        setResult(result, false, "bad token");
        return result;
    }
    if (!miioCommand(ip, "get_prop",
                     "[\"power\",\"mode\",\"speed\",\"roll_enable\"]", resp, sizeof(resp))) {
        MiioResult result{};
        setResult(result, false, g_last_error);
        return result;
    }

    JsonDocument doc;
    MiioResult result{};
    if (deserializeJson(doc, resp)) {
        setResult(result, false, "bad json");
        return result;
    }
    if (doc["error"].is<JsonObject>()) {
        const char* msg = doc["error"]["message"] | "error";
        setResult(result, false, msg);
        return result;
    }

    JsonArray arr = doc["result"].as<JsonArray>();
    if (arr.isNull() || arr.size() < 4) {
        setResult(result, false, "bad result");
        return result;
    }

    if (arr[0].is<bool>()) {
        on = arr[0].as<bool>();
    } else {
        parseBoolResult(resp, on);
    }

    int sp = 0;
    if (arr[2].is<int>()) {
        sp = arr[2].as<int>();
    }
    speed = clampInt(sp, 0, 100);

    if (arr[3].is<bool>()) {
        roll = arr[3].as<bool>();
    } else if (arr[3].is<const char*>()) {
        roll = strcmp(arr[3].as<const char*>(), "on") == 0;
    }

    mode = 0;
    if (arr[1].is<const char*>()) {
        mode = strcmp(arr[1].as<const char*>(), "nature") == 0 ? 1 : 0;
    }

    char msg[32];
    snprintf(msg, sizeof(msg), on ? "ON %d%%" : "OFF", speed);
    setResult(result, true, msg);
    return result;
}

MiioResult miioFanP5SetPower(const char* ip, const char* token_hex, const bool on) {
    char params[12];
    snprintf(params, sizeof(params), "[%s]", on ? "true" : "false");
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "s_power", params, resp, sizeof(resp));
    if (result.ok) {
        setResult(result, true, on ? "ON" : "OFF");
    }
    return result;
}

MiioResult miioFanP5SetSpeed(const char* ip, const char* token_hex, const int speed) {
    char params[8];
    snprintf(params, sizeof(params), "[%d]", clampInt(speed, 0, 100));
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "s_speed", params, resp, sizeof(resp));
    if (result.ok) {
        char msg[16];
        snprintf(msg, sizeof(msg), "SPD %d", clampInt(speed, 0, 100));
        setResult(result, true, msg);
    }
    return result;
}

MiioResult miioFanP5SetRoll(const char* ip, const char* token_hex, const bool on) {
    char params[12];
    snprintf(params, sizeof(params), "[%s]", on ? "true" : "false");
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "s_roll", params, resp, sizeof(resp));
    if (result.ok) {
        setResult(result, true, on ? "ROLL ON" : "ROLL OFF");
    }
    return result;
}

MiioResult miioFanP5SetMode(const char* ip, const char* token_hex, const char* mode) {
    char params[24];
    snprintf(params, sizeof(params), "[\"%s\"]", mode);
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "s_mode", params, resp, sizeof(resp));
    if (result.ok) {
        setResult(result, true, mode);
    }
    return result;
}

MiioResult miioFanGetStatus(const char* ip, const char* token_hex, bool& on, int& speed_level) {
    const MiioQueryScope query_scope(MIIO_QUERY_TIMEOUT_MS);
    char resp[256];
    if (!miioParseTokenHex(token_hex, g_token)) {
        MiioResult result{};
        setResult(result, false, "bad token");
        return result;
    }
    if (!miioCommand(ip, "get_prop", "[\"power\",\"speed_level\"]", resp, sizeof(resp))) {
        MiioResult result{};
        setResult(result, false, g_last_error);
        return result;
    }

    MiioResult result{};
    if (!parseBoolResult(resp, on)) {
        setResult(result, false, "no power");
        return result;
    }

    int lv = 1;
    if (parseIntAt(resp, 1, lv)) {
        speed_level = clampInt(lv, 1, 4);
    } else {
        speed_level = 1;
    }

    char msg[24];
    snprintf(msg, sizeof(msg), on ? "ON L%d" : "OFF", speed_level);
    setResult(result, true, msg);
    return result;
}

MiioResult miioFanSetSpeedLevel(const char* ip, const char* token_hex, const int level) {
    char params[8];
    snprintf(params, sizeof(params), "[%d]", clampInt(level, 1, 4));
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "set_speed_level", params, resp, sizeof(resp));
    if (result.ok) {
        char msg[16];
        snprintf(msg, sizeof(msg), "L=%d", clampInt(level, 1, 4));
        setResult(result, true, msg);
    }
    return result;
}

// 读取 MIoT get_properties 单项
static bool readMiotValue(JsonVariant item, int& out_int, bool& out_bool, bool& is_bool) {
    if (!item.is<JsonObject>()) {
        return false;
    }
    const int code = item["code"] | -1;
    if (code != 0 && code != -1) {
        return false;
    }
    if (item["value"].is<bool>()) {
        out_bool = item["value"].as<bool>();
        is_bool = true;
        return true;
    }
    if (item["value"].is<int>()) {
        out_int = item["value"].as<int>();
        is_bool = false;
        return true;
    }
    return false;
}

MiioResult miioF20GetStatus(const char* ip, const char* token_hex, const char* did, bool& on,
                            int& mode, int& fan_level, int& aqi) {
    const MiioQueryScope query_scope(MIIO_QUERY_TIMEOUT_MS);
    char params[256];
    snprintf(params, sizeof(params),
             "[{\"did\":\"%s\",\"siid\":2,\"piid\":1},{\"did\":\"%s\",\"siid\":2,\"piid\":4},"
             "{\"did\":\"%s\",\"siid\":2,\"piid\":7},{\"did\":\"%s\",\"siid\":3,\"piid\":4}]",
             did, did, did, did);

    char resp[512];
    MiioResult result = miioRun(ip, token_hex, "get_properties", params, resp, sizeof(resp));
    if (!result.ok) {
        return result;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        setResult(result, false, "bad json");
        return result;
    }

    JsonArray arr = doc["result"].as<JsonArray>();
    if (arr.isNull() || arr.size() < 4) {
        setResult(result, false, "bad result");
        return result;
    }

    int iv = 0;
    bool bv = false;
    bool is_bool = false;
    if (readMiotValue(arr[0], iv, bv, is_bool) && is_bool) {
        on = bv;
    }
    if (readMiotValue(arr[1], mode, bv, is_bool) && !is_bool) {
        // mode 已通过 out_int 写入
    }
    if (readMiotValue(arr[2], fan_level, bv, is_bool) && !is_bool) {
        // fan_level 已通过 out_int 写入
    }
    if (readMiotValue(arr[3], aqi, bv, is_bool) && !is_bool) {
        // aqi 已通过 out_int 写入
    }

    static const char* MODE_NAMES[] = {"auto", "sleep", "low", "med", "high", "fav"};
    const int mode_idx = clampInt(mode, 0, 5);
    char msg[32];
    snprintf(msg, sizeof(msg), on ? "ON %s AQI%d" : "OFF", MODE_NAMES[mode_idx], aqi);
    setResult(result, true, msg);
    return result;
}

MiioResult miioF20SetPower(const char* ip, const char* token_hex, const char* did, const bool on) {
    char params[96];
    snprintf(params, sizeof(params), "[{\"did\":\"%s\",\"siid\":2,\"piid\":1,\"value\":%s}]", did,
             on ? "true" : "false");
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "set_properties", params, resp, sizeof(resp));
    if (result.ok) {
        setResult(result, true, on ? "ON" : "OFF");
    }
    return result;
}

MiioResult miioF20SetMode(const char* ip, const char* token_hex, const char* did, const int mode) {
    char params[96];
    snprintf(params, sizeof(params), "[{\"did\":\"%s\",\"siid\":2,\"piid\":4,\"value\":%d}]", did,
             clampInt(mode, 0, 5));
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "set_properties", params, resp, sizeof(resp));
    if (result.ok) {
        static const char* MODE_NAMES[] = {"auto", "sleep", "low", "med", "high", "fav"};
        setResult(result, true, MODE_NAMES[clampInt(mode, 0, 5)]);
    }
    return result;
}

MiioResult miioF20SetFanLevel(const char* ip, const char* token_hex, const char* did,
                              const int level) {
    char params[96];
    snprintf(params, sizeof(params), "[{\"did\":\"%s\",\"siid\":2,\"piid\":7,\"value\":%d}]", did,
             clampInt(level, 0, 5));
    char resp[256];
    MiioResult result = miioRun(ip, token_hex, "set_properties", params, resp, sizeof(resp));
    if (result.ok) {
        char msg[16];
        snprintf(msg, sizeof(msg), "FL=%d", clampInt(level, 0, 5));
        setResult(result, true, msg);
    }
    return result;
}
