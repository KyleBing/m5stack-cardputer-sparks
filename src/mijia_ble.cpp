#include "mijia_ble.h"
#include "mijia_control.h"
#include "app_connectivity.h"
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUUID.h>
#include <mbedtls/ccm.h>
#include <cstring>

static constexpr uint16_t MI_SERVICE_UUID = 0xFE95;
static constexpr uint16_t QINGPING_SERVICE_UUID = 0xFDCD;
static constexpr size_t MIJIA_BLE_SD_MAX = 31;
static constexpr int MIJIA_BLE_WATCH_MAX = 16;

static void setMsg(MijiaBleReading& out, const char* msg) {
    strncpy(out.message, msg, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = '\0';
}

static bool parseMacBytes(const char* mac, uint8_t out[6]) {
    if (mac == nullptr) {
        return false;
    }
    unsigned b[6] = {};
    if (sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) !=
            6 &&
        sscanf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) !=
            6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = static_cast<uint8_t>(b[i]);
    }
    return true;
}

static bool macEqualIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        const char ca = (*a >= 'a' && *a <= 'f') ? static_cast<char>(*a - 32) : *a;
        const char cb = (*b >= 'a' && *b <= 'f') ? static_cast<char>(*b - 32) : *b;
        if (ca != cb) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

static bool parseBindKey(const char* hex, uint8_t key[16]) {
    if (hex == nullptr || strlen(hex) < 32) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        unsigned v = 0;
        if (sscanf(hex + i * 2, "%02x", &v) != 1 && sscanf(hex + i * 2, "%02X", &v) != 1) {
            return false;
        }
        key[i] = static_cast<uint8_t>(v);
    }
    return true;
}

static bool loadBindKey(const char* ble_key, uint8_t key[16]) {
    if (parseBindKey(ble_key, key)) {
        return true;
    }
    if (ble_key == nullptr || ble_key[0] == '\0' || strlen(ble_key) >= 32) {
        return false;
    }
    char padded[33] = {};
    strncpy(padded, ble_key, 32);
    for (size_t i = strlen(padded); i < 32; i++) {
        padded[i] = '0';
    }
    return parseBindKey(padded, key);
}

static uint16_t u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t u32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static bool decryptXiaomiPayload(uint8_t* raw, const size_t raw_size, const uint8_t bindkey[16],
                                 const uint8_t mac[6]) {
    if (!(raw_size == 19 || (raw_size >= 22 && raw_size <= 24))) {
        return false;
    }
    uint8_t mac_reverse[6];
    for (int i = 0; i < 6; i++) {
        mac_reverse[i] = mac[5 - i];
    }
    const size_t datasize = (raw_size == 19) ? raw_size - 12 : raw_size - 18;
    const int cipher_pos = (raw_size == 19) ? 5 : 11;
    if (datasize > 16) {
        return false;
    }

    uint8_t ciphertext[16] = {};
    uint8_t plaintext[16] = {};
    uint8_t tag[4] = {};
    uint8_t iv[12] = {};
    const uint8_t authdata[1] = {0x11};
    memcpy(ciphertext, raw + cipher_pos, datasize);
    memcpy(tag, raw + raw_size - 4, 4);
    memcpy(iv, mac_reverse, 6);
    memcpy(iv + 6, raw + 2, 3);
    memcpy(iv + 9, raw + raw_size - 7, 3);

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    if (mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, bindkey, 128) != 0) {
        mbedtls_ccm_free(&ctx);
        return false;
    }
    const int ret =
        mbedtls_ccm_auth_decrypt(&ctx, datasize, iv, 12, authdata, 1, ciphertext, plaintext, tag, 4);
    mbedtls_ccm_free(&ctx);
    if (ret != 0) {
        return false;
    }
    memcpy(raw + cipher_pos, plaintext, datasize);
    raw[0] &= static_cast<uint8_t>(~0x08);
    return true;
}

static bool parseXiaomiValue(const uint16_t value_type, const uint8_t* data, const uint8_t value_length,
                             MijiaBleReading& out) {
    if ((value_type == 0x1001) && value_length == 3) {
        out.has_button = true;
        out.button = data[2] == 0;
        return true;
    }
    if ((value_type == 0x0003) && value_length == 1) {
        out.has_motion = true;
        out.motion = data[0] != 0;
        return true;
    }
    if ((value_type == 0x1004) && value_length == 2) {
        out.has_temp = true;
        out.temperature = static_cast<int16_t>(u16le(data)) / 10.0f;
        return true;
    }
    if ((value_type == 0x1006) && value_length == 2) {
        out.has_humidity = true;
        out.humidity = static_cast<int16_t>(u16le(data)) / 10.0f;
        return true;
    }
    if ((value_type == 0x100A || value_type == 0x4803) && value_length == 1) {
        out.has_battery = true;
        out.battery = data[0];
        return true;
    }
    if ((value_type == 0x100D) && value_length == 4) {
        out.has_temp = true;
        out.has_humidity = true;
        out.temperature = static_cast<int16_t>(u16le(data)) / 10.0f;
        out.humidity = static_cast<int16_t>(u16le(data + 2)) / 10.0f;
        return true;
    }
    if ((value_type == 0x1017) && value_length == 4) {
        out.has_motion = true;
        out.motion = u32le(data) == 0;
        return true;
    }
    if ((value_type == 0x4C01 || value_type == 0x4801) && value_length == 4) {
        float t = 0;
        memcpy(&t, data, sizeof(t));
        out.has_temp = true;
        out.temperature = t;
        return true;
    }
    if ((value_type == 0x4C02 || value_type == 0x4802) && value_length == 1) {
        out.has_humidity = true;
        out.humidity = data[0];
        return true;
    }
    if ((value_type == 0x4C08) && value_length == 4) {
        float h = 0;
        memcpy(&h, data, sizeof(h));
        out.has_humidity = true;
        out.humidity = h;
        return true;
    }
    return false;
}

static bool parseXiaomiMessage(const uint8_t* message, const size_t message_size, const int raw_offset,
                               MijiaBleReading& out) {
    if (raw_offset < 0 || static_cast<size_t>(raw_offset) >= message_size) {
        return false;
    }
    const uint8_t* payload = message + raw_offset;
    int payload_length = static_cast<int>(message_size - static_cast<size_t>(raw_offset));
    int payload_offset = 0;
    bool success = false;
    while (payload_length > 3) {
        const uint8_t mid = payload[payload_offset + 1];
        if (mid != 0x10 && mid != 0x00 && mid != 0x4C && mid != 0x48) {
            break;
        }
        const uint8_t value_length = payload[payload_offset + 2];
        if (value_length < 1 || value_length > 4 || payload_length < (3 + value_length)) {
            break;
        }
        const uint16_t value_type =
            static_cast<uint16_t>(payload[payload_offset] | (payload[payload_offset + 1] << 8));
        if (parseXiaomiValue(value_type, &payload[payload_offset + 3], value_length, out)) {
            success = true;
        }
        payload_length -= 3 + value_length;
        payload_offset += 3 + value_length;
    }
    return success;
}

static bool processXiaomiServiceData(const uint8_t* data, const size_t size, const uint8_t mac[6],
                                     const uint8_t* bindkey, const bool have_bindkey,
                                     MijiaBleReading& out) {
    if (size < 5 || size > MIJIA_BLE_SD_MAX) {
        return false;
    }
    uint8_t raw[MIJIA_BLE_SD_MAX];
    memcpy(raw, data, size);
    const bool has_data = (raw[0] & 0x40) != 0;
    const bool has_capability = (raw[0] & 0x20) != 0;
    const bool has_encryption = (raw[0] & 0x08) != 0;
    if (!has_data) {
        return false;
    }
    if (has_encryption) {
        if (!have_bindkey) {
            setMsg(out, "no key");
            return false;
        }
        if (!decryptXiaomiPayload(raw, size, bindkey, mac)) {
            setMsg(out, "decrypt fail");
            return false;
        }
    }
    int raw_offset = has_capability ? 12 : 11;
    if (size == 19) {
        raw_offset -= 6;
    }
    if (parseXiaomiMessage(raw, size, raw_offset, out)) {
        return true;
    }
    return size == 19 && parseXiaomiMessage(raw, size, 5, out);
}

static bool processQingpingServiceData(const uint8_t* data, const size_t size, MijiaBleReading& out) {
    if (size < 10) {
        return false;
    }
    size_t i = 6;
    bool success = false;
    while (i + 2 < size) {
        const uint8_t type = data[i++];
        const uint8_t len = data[i++];
        if (i + len > size) {
            break;
        }
        if (type == 0x01 && len == 2) {
            out.has_temp = true;
            out.temperature = static_cast<int16_t>(u16le(data + i)) / 10.0f;
            success = true;
        } else if (type == 0x02 && len == 2) {
            out.has_humidity = true;
            out.humidity = static_cast<int16_t>(u16le(data + i)) / 10.0f;
            success = true;
        } else if (type == 0x02 && len == 1) {
            out.has_humidity = true;
            out.humidity = data[i];
            success = true;
        } else if (type == 0x08 && len == 1) {
            out.has_battery = true;
            out.battery = data[i];
            success = true;
        }
        i += len;
    }
    return success;
}

struct MijiaBleTarget {
    char mac[18];
    uint8_t mac_bytes[6];
    uint8_t bindkey[16];
    bool have_bindkey;
    int device_idx;
};

struct MijiaBlePending {
    bool have;
    bool is_qingping;
    int target_idx;
    uint8_t len;
    uint8_t data[MIJIA_BLE_SD_MAX];
};

static MijiaBleTarget g_targets[MIJIA_BLE_WATCH_MAX];
static int g_target_count = 0;
static bool g_stop_on_ok = true;
static MijiaBlePending g_pending{};
static char g_last_fail[48] = "";
static volatile bool g_scan_done = false;
static bool g_scan_running = false;
static uint32_t g_scan_deadline_ms = 0;
static bool g_session_held = false;

static int findTargetByMac(const char* mac) {
    for (int i = 0; i < g_target_count; i++) {
        if (macEqualIgnoreCase(mac, g_targets[i].mac)) {
            return i;
        }
    }
    return -1;
}

static void captureServiceData(BLEAdvertisedDevice& adv, const int target_idx) {
    if (!adv.haveServiceData() || g_pending.have) {
        return;
    }
    const int count = adv.getServiceDataCount();
    const int n = count > 0 ? count : 1;
    for (int i = 0; i < n; i++) {
        BLEUUID uuid = count > 0 ? adv.getServiceDataUUID(i) : adv.getServiceDataUUID();
        const std::string sd = count > 0 ? adv.getServiceData(i) : adv.getServiceData();
        if (sd.empty() || sd.size() > MIJIA_BLE_SD_MAX) {
            continue;
        }
        const bool mi = uuid.equals(BLEUUID(static_cast<uint16_t>(MI_SERVICE_UUID)));
        const bool qp = uuid.equals(BLEUUID(static_cast<uint16_t>(QINGPING_SERVICE_UUID)));
        if (!mi && !qp) {
            continue;
        }
        g_pending.have = true;
        g_pending.is_qingping = qp;
        g_pending.target_idx = target_idx;
        g_pending.len = static_cast<uint8_t>(sd.size());
        memcpy(g_pending.data, sd.data(), g_pending.len);
        return;
    }
}

class MijiaBleCaptureCbs : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (g_pending.have || g_target_count <= 0) {
            return;
        }
        const int idx = findTargetByMac(advertisedDevice.getAddress().toString().c_str());
        if (idx < 0) {
            return;
        }
        captureServiceData(advertisedDevice, idx);
        // 仅缓存原始包，解析放主循环；脏包可丢弃后继续听
    }
};

static MijiaBleCaptureCbs g_cbs;

static void finishSession() {
    BLEScan* scan = BLEDevice::getScan();
    if (scan != nullptr) {
        scan->setAdvertisedDeviceCallbacks(nullptr, false);
        scan->stop();
        scan->clearResults();
    }
    if (g_session_held) {
        endBleScanSession(true);
        g_session_held = false;
    }
    g_scan_running = false;
    g_scan_done = false;
    g_pending.have = false;
}

static bool beginScanSession(const uint32_t scan_seconds) {
    if (g_scan_running || g_target_count <= 0) {
        return false;
    }
    if (ESP.getFreeHeap() < 28000) {
        return false;
    }
    if (!beginBleScanSession()) {
        return false;
    }
    g_session_held = true;

    BLEScan* scan = BLEDevice::getScan();
    if (scan == nullptr) {
        finishSession();
        return false;
    }

    g_pending = {};
    g_last_fail[0] = '\0';
    g_scan_done = false;

    // wantDuplicates=true：有回调时不往结果集堆积，省内存
    scan->setAdvertisedDeviceCallbacks(&g_cbs, true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);

    const uint32_t seconds = scan_seconds == 0 ? 30 : scan_seconds;
    if (!scan->start(seconds, nullptr, false)) {
        finishSession();
        return false;
    }

    g_scan_running = true;
    g_scan_deadline_ms = millis() + (seconds + 2) * 1000UL;
    return true;
}

static bool fillTargetFromDevice(MijiaBleTarget& t, const MijiaDevice& dev, const int device_idx) {
    if (dev.mac[0] == '\0' || !parseMacBytes(dev.mac, t.mac_bytes)) {
        return false;
    }
    strncpy(t.mac, dev.mac, sizeof(t.mac) - 1);
    t.mac[sizeof(t.mac) - 1] = '\0';
    t.have_bindkey = loadBindKey(dev.ble_key, t.bindkey);
    t.device_idx = device_idx;
    return true;
}

static bool tryParsePending(MijiaBleReading& out, int& device_idx) {
    out = {};
    device_idx = -1;
    if (!g_pending.have) {
        return false;
    }
    const int tidx = g_pending.target_idx;
    const bool is_qp = g_pending.is_qingping;
    const uint8_t len = g_pending.len;
    uint8_t data[MIJIA_BLE_SD_MAX];
    memcpy(data, g_pending.data, len);
    g_pending.have = false;

    if (tidx < 0 || tidx >= g_target_count) {
        setMsg(out, "parse fail");
        strncpy(g_last_fail, out.message, sizeof(g_last_fail) - 1);
        return false;
    }

    const MijiaBleTarget& t = g_targets[tidx];
    device_idx = t.device_idx;
    bool parsed = false;
    if (is_qp) {
        parsed = processQingpingServiceData(data, len, out);
    } else {
        parsed = processXiaomiServiceData(data, len, t.mac_bytes, t.bindkey, t.have_bindkey, out);
    }
    if (parsed) {
        out.ok = true;
        setMsg(out, "ok");
        g_last_fail[0] = '\0';
        return true;
    }
    if (out.message[0] == '\0') {
        setMsg(out, "parse fail");
    }
    strncpy(g_last_fail, out.message, sizeof(g_last_fail) - 1);
    g_last_fail[sizeof(g_last_fail) - 1] = '\0';
    return false;
}

bool mijiaBleScanIsRunning() {
    return g_scan_running;
}

void mijiaBleScanAbort() {
    if (!g_scan_running) {
        return;
    }
    g_scan_done = true;
    finishSession();
}

bool mijiaBleScanStart(const MijiaDevice& dev, const uint32_t scan_seconds, const int device_idx) {
    if (g_scan_running) {
        return false;
    }
    g_target_count = 0;
    if (!fillTargetFromDevice(g_targets[0], dev, device_idx)) {
        return false;
    }
    g_target_count = 1;
    g_stop_on_ok = true;
    return beginScanSession(scan_seconds);
}

bool mijiaBleWatchStart(const MijiaDevice* devices, const int device_count, const uint32_t scan_seconds) {
    if (g_scan_running || devices == nullptr || device_count <= 0) {
        return false;
    }
    g_target_count = 0;
    for (int i = 0; i < device_count && g_target_count < MIJIA_BLE_WATCH_MAX; i++) {
        if (!mijiaBleCanScan(devices[i])) {
            continue;
        }
        if (!fillTargetFromDevice(g_targets[g_target_count], devices[i], i)) {
            continue;
        }
        g_target_count++;
    }
    if (g_target_count <= 0) {
        return false;
    }
    g_stop_on_ok = false;
    return beginScanSession(scan_seconds);
}

bool mijiaBleScanPoll(MijiaBleReading& out, int* device_idx) {
    out = {};
    setMsg(out, "listening");
    if (device_idx != nullptr) {
        *device_idx = -1;
    }

    if (!g_scan_running) {
        setMsg(out, "idle");
        return true;
    }

    // 有待解析包：成功则按模式收尾或继续；失败丢弃继续听
    if (g_pending.have) {
        int idx = -1;
        MijiaBleReading parsed{};
        if (tryParsePending(parsed, idx)) {
            out = parsed;
            if (device_idx != nullptr) {
                *device_idx = idx;
            }
            if (g_stop_on_ok) {
                finishSession();
                return true;
            }
            // 后台：上报读数，扫描继续
            return false;
        }
        // 脏包 / 解密失败：继续听
        setMsg(out, "listening");
        return false;
    }

    if (!g_scan_done && millis() < g_scan_deadline_ms) {
        setMsg(out, "listening");
        return false;
    }

    finishSession();
    if (g_last_fail[0] != '\0') {
        setMsg(out, g_last_fail);
    } else {
        setMsg(out, "no adv");
    }
    return true;
}
