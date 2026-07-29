#include "app_hid_keyboard.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_connectivity.h"
#include "app_header.h"

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <HIDTypes.h>
#include <Preferences.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <WiFi.h>
#include <esp_gap_ble_api.h>
#include <cstring>

#include "driver/periph_ctrl.h"
#include "esp_private/usb_phy.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_pins.h"
#include "soc/usb_serial_jtag_reg.h"
#include "tusb.h"

// Cardputer 单 USB PHY：开机保持 Serial/JTAG 烧录；
// 进入 USB 键盘时切到 TinyUSB OTG，退出/切 BLE 时再切回 JTAG。

enum class HidTransport : uint8_t {
    BLE = 0,
    USB = 1,
};

static constexpr int kEchoTextSize = 3;
static constexpr size_t kEchoMaxChars = 12;
static constexpr size_t kBleReportQueueCap = 48;
static constexpr uint32_t kBleReportIntervalMs = 12;

// HID modifier 位
static constexpr uint8_t kModLCtrl = 0x01;
static constexpr uint8_t kModLShift = 0x02;
static constexpr uint8_t kModLAlt = 0x04;
static constexpr uint8_t kModLGui = 0x08;  // Opt → Win/Cmd
static constexpr uint8_t kModRCtrl = 0x10;
static constexpr uint8_t kModRAlt = 0x40;
static constexpr uint8_t kModRGui = 0x80;
static constexpr uint8_t kHidCapsLock = 0x39;

static constexpr int kHelpPageCount = 2;
static constexpr int kBleHostSlots = 5;  // 最多保存 5 台已配对主机
static constexpr int kHostAliasMax = 16;  // 设备别名最大长度（屏宽约能放下）

struct BleHostSlot {
    bool used = false;
    bool has_last_conn = false;  // 最近一次连接地址（常为 RPA）
    uint8_t addr_type = BLE_ADDR_TYPE_PUBLIC;
    esp_bd_addr_t addr{};
    esp_bd_addr_t last_conn{};
    char alias[kHostAliasMax + 1] = "";  // 用户自定义名称；空则显示 MAC
};

static bool g_screen_ready = false;
static bool g_active = false;
static bool g_help_visible = false;
static bool g_hosts_ui = false;  // BLE 主机列表（切换 / 配对）
static bool g_hosts_exit_on_connect = false;  // 切换/新配对成功后自动回输入界面
static bool g_rename_ui = false;  // 主机列表内重命名别名
static char g_rename_buf[kHostAliasMax + 1] = "";
static int g_help_page = 0;
static bool g_fn_h_latched = false;
static bool g_fn_caps_latched = false;
static bool g_hosts_key_latched = false;
static HidTransport g_transport = HidTransport::BLE;
static bool g_usb_ready = false;
static bool g_usb_inited = false;
static bool g_ble_ready = false;
static bool g_ble_connected = false;  // 认证成功后才为 true（可发 HID）
static bool g_pairing_open = false;  // 只要新主机；已配对的连上会踢掉
static int g_prefer_slot = -1;       // >=0：切换中，只接受该槽回连
static uint16_t g_active_conn_id = 0xFFFF;  // 当前认可的连接；其它连接的 disconnect 忽略
static char g_echo[kEchoMaxChars + 1] = "";
static char g_last_label[16] = "";
static char g_drawn_echo[kEchoMaxChars + 1] = "";
static char g_drawn_label[16] = "";
static char g_peer_addr[18] = "";
// 需能装下 "#1 AA:BB:CC:DD:EE:FF"；过短会导致每帧 strcmp 失败而狂闪
static char g_drawn_peer[28] = "";
static int g_drawn_slot_num = -2;  // 右上角槽号缓存；-1=无，-2=需重绘
static char g_drawn_link_status[24] = "";  // 内容区状态文案缓存
static BleHostSlot g_hosts[kBleHostSlots];
static int g_active_slot = -1;  // 当前偏好主机槽
static int g_sel_slot = 0;      // 列表光标
static char g_hosts_status[36] = "";
static char g_auth_hint[28] = "";  // 认证失败提示（如主机端需先忘掉）
static int g_auth_fail_streak = 0;
// 单边 bond：停广播并拒连，直到用户按 n 重新开放配对
static bool g_stale_block = false;
static bool g_has_blocked_bda = false;
static esp_bd_addr_t g_blocked_bda{};
// AUTH 有时早于 onConnect（已绑定回连）；先记下成功地址
static bool g_early_auth_ok = false;
static esp_bd_addr_t g_early_auth_bda{};

// 链路已接上、等待 SMP 认证；认证前不写槽、不显示 paired
struct PendingBleConn {
    bool active = false;
    bool is_new = false;  // 新配对（无已有槽）
    int slot = -1;        // 已知 / prefer 槽；新配对为 -1
    uint16_t conn_id = 0xFFFF;
    uint8_t addr_type = BLE_ADDR_TYPE_PUBLIC;
    esp_bd_addr_t bda{};
    uint32_t since_ms = 0;
};
static PendingBleConn g_pending;

static USBHIDKeyboard g_usb_kb;
static usb_phy_handle_t g_otg_phy = nullptr;
static BLEHIDDevice* g_hid = nullptr;
static BLECharacteristic* g_kb_input = nullptr;
static BLEServer* g_ble_server = nullptr;

struct BleReport {
    uint8_t data[8];
};
static BleReport g_ble_q[kBleReportQueueCap];
static size_t g_ble_q_head = 0;
static size_t g_ble_q_tail = 0;
static size_t g_ble_q_count = 0;
static uint32_t g_ble_last_send_ms = 0;

static void clearBleReportQueue();
static void applyTransport(HidTransport next);
static void stopUsbKeyboard();
static void startUsbKeyboard();
static void stopBleKeyboard();
static void startBleKeyboard();
static void disconnectBleClients();

static const uint8_t kHidReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
    0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
};

static void clearPeerInfo() {
    g_peer_addr[0] = '\0';
    g_drawn_peer[0] = '\0';
}

static void formatPeerAddr(const esp_bd_addr_t bda, char* out, size_t out_len) {
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3],
             bda[4], bda[5]);
}

static bool sameBdAddr(const esp_bd_addr_t a, const esp_bd_addr_t b) {
    return memcmp(a, b, sizeof(esp_bd_addr_t)) == 0;
}

static int findHostSlot(const esp_bd_addr_t addr) {
    for (int i = 0; i < kBleHostSlots; i++) {
        if (!g_hosts[i].used) {
            continue;
        }
        if (sameBdAddr(g_hosts[i].addr, addr)) {
            return i;
        }
        if (g_hosts[i].has_last_conn && sameBdAddr(g_hosts[i].last_conn, addr)) {
            return i;
        }
    }
    return -1;
}

// 连接地址可能是 RPA：对照 bond / identity 再认一次槽位
static int findHostSlotFuzzy(const esp_bd_addr_t addr) {
    const int direct = findHostSlot(addr);
    if (direct >= 0) {
        return direct;
    }
    const int n = esp_ble_get_bond_device_num();
    if (n <= 0) {
        return -1;
    }
    auto* list = static_cast<esp_ble_bond_dev_t*>(malloc(sizeof(esp_ble_bond_dev_t) * static_cast<size_t>(n)));
    if (list == nullptr) {
        return -1;
    }
    int count = n;
    int found = -1;
    if (esp_ble_get_bond_device_list(&count, list) == ESP_OK) {
        for (int j = 0; j < count; j++) {
            bool hit = sameBdAddr(list[j].bd_addr, addr);
            if (!hit && (list[j].bond_key.key_mask & ESP_LE_KEY_PID)) {
                hit = sameBdAddr(list[j].bond_key.pid_key.static_addr, addr);
            }
            if (!hit) {
                continue;
            }
            found = findHostSlot(list[j].bd_addr);
            if (found < 0 && (list[j].bond_key.key_mask & ESP_LE_KEY_PID)) {
                found = findHostSlot(list[j].bond_key.pid_key.static_addr);
            }
            break;
        }
    }
    free(list);
    return found;
}

static int firstEmptyHostSlot() {
    for (int i = 0; i < kBleHostSlots; i++) {
        if (!g_hosts[i].used) {
            return i;
        }
    }
    return -1;
}

static int usedHostSlotCount() {
    int n = 0;
    for (int i = 0; i < kBleHostSlots; i++) {
        if (g_hosts[i].used) {
            n++;
        }
    }
    return n;
}

static void saveHostSlots() {
    Preferences prefs;
    // NVS 名 hidkb 保持兼容，避免升级丢掉已存主机槽
    if (!prefs.begin("hidkb", false)) {
        return;
    }
    for (int i = 0; i < kBleHostSlots; i++) {
        char key[4];
        snprintf(key, sizeof(key), "s%d", i);
        char nkey[4];
        snprintf(nkey, sizeof(nkey), "n%d", i);
        if (g_hosts[i].used) {
            uint8_t buf[7];
            buf[0] = g_hosts[i].addr_type;
            memcpy(buf + 1, g_hosts[i].addr, 6);
            prefs.putBytes(key, buf, sizeof(buf));
            // 别名单独存；空则删 key，兼容旧固件
            if (g_hosts[i].alias[0] != '\0') {
                prefs.putString(nkey, g_hosts[i].alias);
            } else {
                prefs.remove(nkey);
            }
        } else {
            prefs.remove(key);
            prefs.remove(nkey);
        }
    }
    prefs.putChar("act", static_cast<int8_t>(g_active_slot));
    prefs.end();
}

static void loadHostSlots() {
    Preferences prefs;
    if (!prefs.begin("hidkb", true)) {
        return;
    }
    for (int i = 0; i < kBleHostSlots; i++) {
        char key[4];
        snprintf(key, sizeof(key), "s%d", i);
        char nkey[4];
        snprintf(nkey, sizeof(nkey), "n%d", i);
        uint8_t buf[7] = {};
        const size_t n = prefs.getBytes(key, buf, sizeof(buf));
        g_hosts[i].alias[0] = '\0';
        if (n == sizeof(buf)) {
            g_hosts[i].used = true;
            g_hosts[i].addr_type = buf[0];
            memcpy(g_hosts[i].addr, buf + 1, 6);
            prefs.getString(nkey, g_hosts[i].alias, sizeof(g_hosts[i].alias));
            g_hosts[i].alias[sizeof(g_hosts[i].alias) - 1] = '\0';
        } else {
            g_hosts[i].used = false;
            g_hosts[i].addr_type = BLE_ADDR_TYPE_PUBLIC;
            memset(g_hosts[i].addr, 0, 6);
        }
    }
    g_active_slot = static_cast<int8_t>(prefs.getChar("act", -1));
    if (g_active_slot < -1 || g_active_slot >= kBleHostSlots ||
        (g_active_slot >= 0 && !g_hosts[g_active_slot].used)) {
        g_active_slot = -1;
    }
    prefs.end();
}

// 列表/状态：有别名用别名，否则 MAC
static void formatHostDisplayName(const int slot, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (slot < 0 || slot >= kBleHostSlots || !g_hosts[slot].used) {
        return;
    }
    if (g_hosts[slot].alias[0] != '\0') {
        strncpy(out, g_hosts[slot].alias, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    formatPeerAddr(g_hosts[slot].addr, out, out_len);
}

// 用 bond 列表校准槽位：补齐 / 剔除，最多保留 5 台
static void syncHostSlotsWithBonds() {
    const int n = esp_ble_get_bond_device_num();
    esp_ble_bond_dev_t* list = nullptr;
    int count = 0;
    if (n > 0) {
        list = static_cast<esp_ble_bond_dev_t*>(malloc(sizeof(esp_ble_bond_dev_t) * static_cast<size_t>(n)));
        if (list != nullptr) {
            count = n;
            if (esp_ble_get_bond_device_list(&count, list) != ESP_OK) {
                count = 0;
            }
        }
    }

    bool dirty = false;
    // 槽位地址已不在 bond 中则清空
    for (int i = 0; i < kBleHostSlots; i++) {
        if (!g_hosts[i].used) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (sameBdAddr(g_hosts[i].addr, list[j].bd_addr)) {
                found = true;
                if (list[j].bond_key.key_mask & ESP_LE_KEY_PID) {
                    const auto& pid = list[j].bond_key.pid_key;
                    if (!sameBdAddr(g_hosts[i].addr, pid.static_addr) ||
                        g_hosts[i].addr_type != pid.addr_type) {
                        memcpy(g_hosts[i].addr, pid.static_addr, 6);
                        g_hosts[i].addr_type = pid.addr_type;
                        dirty = true;
                    }
                }
                break;
            }
            // 也可能 slot 存的是 identity，bond 里是当前地址
            if ((list[j].bond_key.key_mask & ESP_LE_KEY_PID) &&
                sameBdAddr(g_hosts[i].addr, list[j].bond_key.pid_key.static_addr)) {
                found = true;
                g_hosts[i].addr_type = list[j].bond_key.pid_key.addr_type;
                break;
            }
        }
        if (!found) {
            g_hosts[i].used = false;
            g_hosts[i].has_last_conn = false;
            g_hosts[i].alias[0] = '\0';
            memset(g_hosts[i].addr, 0, 6);
            memset(g_hosts[i].last_conn, 0, 6);
            dirty = true;
        }
    }

    // bond 不在槽位里：填空槽；超出 5 台则删掉多余 bond
    for (int j = 0; j < count; j++) {
        esp_bd_addr_t id_addr;
        uint8_t id_type = BLE_ADDR_TYPE_PUBLIC;
        memcpy(id_addr, list[j].bd_addr, 6);
        if (list[j].bond_key.key_mask & ESP_LE_KEY_PID) {
            memcpy(id_addr, list[j].bond_key.pid_key.static_addr, 6);
            id_type = list[j].bond_key.pid_key.addr_type;
        }

        if (findHostSlot(id_addr) >= 0 || findHostSlot(list[j].bd_addr) >= 0) {
            continue;
        }
        const int empty = firstEmptyHostSlot();
        if (empty >= 0) {
            g_hosts[empty].used = true;
            g_hosts[empty].addr_type = id_type;
            memcpy(g_hosts[empty].addr, id_addr, 6);
            dirty = true;
        } else {
            esp_ble_remove_bond_device(list[j].bd_addr);
        }
    }

    if (g_active_slot >= 0 && !g_hosts[g_active_slot].used) {
        g_active_slot = -1;
        dirty = true;
    }
    if (g_active_slot < 0) {
        for (int i = 0; i < kBleHostSlots; i++) {
            if (g_hosts[i].used) {
                g_active_slot = i;
                dirty = true;
                break;
            }
        }
    }
    if (dirty) {
        saveHostSlots();
    }
    free(list);
}

static int addOrTouchHostSlot(const esp_bd_addr_t addr, const uint8_t addr_type) {
    int slot = findHostSlot(addr);
    if (slot < 0) {
        slot = firstEmptyHostSlot();
        if (slot < 0) {
            return -1;
        }
        g_hosts[slot].used = true;
        memcpy(g_hosts[slot].addr, addr, 6);
        g_hosts[slot].addr_type = addr_type;
    } else {
        g_hosts[slot].addr_type = addr_type;
    }
    // 记下本次连接地址，下次 RPA 变化仍能认出来
    memcpy(g_hosts[slot].last_conn, addr, 6);
    g_hosts[slot].has_last_conn = true;
    g_active_slot = slot;
    g_pairing_open = false;
    g_prefer_slot = -1;
    saveHostSlots();
    return slot;
}

static void deleteHostSlot(const int slot) {
    if (slot < 0 || slot >= kBleHostSlots || !g_hosts[slot].used) {
        return;
    }
    esp_ble_remove_bond_device(g_hosts[slot].addr);
    // 再扫一遍 bond，清掉同 identity 的条目
    const int n = esp_ble_get_bond_device_num();
    if (n > 0) {
        auto* list = static_cast<esp_ble_bond_dev_t*>(malloc(sizeof(esp_ble_bond_dev_t) * static_cast<size_t>(n)));
        if (list != nullptr) {
            int count = n;
            if (esp_ble_get_bond_device_list(&count, list) == ESP_OK) {
                for (int i = 0; i < count; i++) {
                    if (sameBdAddr(list[i].bd_addr, g_hosts[slot].addr)) {
                        esp_ble_remove_bond_device(list[i].bd_addr);
                    } else if ((list[i].bond_key.key_mask & ESP_LE_KEY_PID) &&
                               sameBdAddr(list[i].bond_key.pid_key.static_addr, g_hosts[slot].addr)) {
                        esp_ble_remove_bond_device(list[i].bd_addr);
                    }
                }
            }
            free(list);
        }
    }
    g_hosts[slot].used = false;
    g_hosts[slot].has_last_conn = false;
    g_hosts[slot].alias[0] = '\0';
    memset(g_hosts[slot].addr, 0, 6);
    memset(g_hosts[slot].last_conn, 0, 6);
    if (g_active_slot == slot) {
        g_active_slot = -1;
        for (int i = 0; i < kBleHostSlots; i++) {
            if (g_hosts[i].used) {
                g_active_slot = i;
                break;
            }
        }
    }
    saveHostSlots();
}

// 一律开放广播；是否接受由 onConnect 按 pairing / prefer 过滤
// （白名单地址类型经常对不上，会导致一直 wait）
static void configureBleAdvertising(const bool /*unused_open*/) {
    if (g_ble_server == nullptr) {
        return;
    }
    BLEAdvertising* adv = g_ble_server->getAdvertising();
    adv->stop();
    esp_ble_gap_clear_whitelist();
    adv->setScanFilter(false, false);
    adv->start();
}

static void clearPendingConn() {
    g_pending.active = false;
    g_pending.is_new = false;
    g_pending.slot = -1;
    g_pending.conn_id = 0xFFFF;
}

static bool isBlockedPeer(const esp_bd_addr_t addr) {
    return g_has_blocked_bda && sameBdAddr(g_blocked_bda, addr);
}

static void clearStaleHostBlock() {
    g_stale_block = false;
    g_has_blocked_bda = false;
    memset(g_blocked_bda, 0, sizeof(g_blocked_bda));
}

static void clearEarlyAuth() {
    g_early_auth_ok = false;
    memset(g_early_auth_bda, 0, sizeof(g_early_auth_bda));
}

// 电脑仍持旧配对狂连：停广播 + 记下地址，UI 稳定停在 plz forget on device
static void engageStaleHostBlock(const esp_bd_addr_t bda) {
    memcpy(g_blocked_bda, bda, 6);
    g_has_blocked_bda = true;
    g_stale_block = true;
    g_pairing_open = false;
    g_prefer_slot = -1;
    clearEarlyAuth();
    if (g_ble_server != nullptr) {
        BLEDevice::stopAdvertising();
    }
}

static void commitPendingConnection();  // 下方定义

// 已知主机：链路起来即可用（不等 AUTH，避免 AUTH 先于 Connect 时卡死）
// 新配对：只 stage，等认证成功后再写槽
static void stageBleConnection(esp_ble_gatts_cb_param_t* param, const int slot,
                               const bool is_new) {
    g_pending.active = true;
    g_pending.is_new = is_new;
    g_pending.slot = slot;
    g_pending.conn_id = param->connect.conn_id;
    g_pending.addr_type = param->connect.ble_addr_type;
    memcpy(g_pending.bda, param->connect.remote_bda, 6);
    g_pending.since_ms = millis();
    formatPeerAddr(g_pending.bda, g_peer_addr, sizeof(g_peer_addr));
    g_active_conn_id = param->connect.conn_id;
    g_ble_connected = false;
    BLEDevice::stopAdvertising();

    // AUTH 已先完成：直接提交
    if (g_early_auth_ok && sameBdAddr(g_early_auth_bda, param->connect.remote_bda)) {
        clearEarlyAuth();
        commitPendingConnection();
        return;
    }

    // 已配对槽回连：立即提交，不依赖 AUTH 回调顺序
    if (!is_new && slot >= 0) {
        clearEarlyAuth();
        commitPendingConnection();
        return;
    }

    // 新配对：拉起加密，等 onAuthenticationComplete
    esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
}

static void commitPendingConnection() {
    if (!g_pending.active) {
        g_ble_connected = true;
        return;
    }
    if (g_pending.slot >= 0 && g_hosts[g_pending.slot].used) {
        memcpy(g_hosts[g_pending.slot].last_conn, g_pending.bda, 6);
        g_hosts[g_pending.slot].has_last_conn = true;
        g_hosts[g_pending.slot].addr_type = g_pending.addr_type;
        g_active_slot = g_pending.slot;
        g_pairing_open = false;
        g_prefer_slot = -1;
        saveHostSlots();
    } else {
        addOrTouchHostSlot(g_pending.bda, g_pending.addr_type);
    }
    formatPeerAddr(g_pending.bda, g_peer_addr, sizeof(g_peer_addr));
    g_ble_connected = true;
    g_hosts_status[0] = '\0';
    g_auth_hint[0] = '\0';
    g_auth_fail_streak = 0;
    clearStaleHostBlock();
    clearEarlyAuth();
    clearPendingConn();
}

static void handleAuthFailure(const esp_bd_addr_t bda) {
    // 清掉半成品 / 不匹配的 bond，避免反复用坏密钥握手
    esp_bd_addr_t addr{};
    memcpy(addr, bda, 6);
    esp_ble_remove_bond_device(addr);
    g_auth_fail_streak++;
    clearEarlyAuth();

    // 仅新配对失败才提示 forget；已知槽回连失败不删槽、不停全部广播
    if (g_pending.active && g_pending.is_new) {
        snprintf(g_auth_hint, sizeof(g_auth_hint), "plz forget on device");
        snprintf(g_hosts_status, sizeof(g_hosts_status), "plz forget on device");
        engageStaleHostBlock(addr);
    } else if (g_pending.active && g_pending.slot >= 0) {
        snprintf(g_auth_hint, sizeof(g_auth_hint), "auth fail");
        snprintf(g_hosts_status, sizeof(g_hosts_status), "auth fail");
        // 只拉黑该地址，保留其它主机回连
        memcpy(g_blocked_bda, addr, 6);
        g_has_blocked_bda = true;
    } else {
        snprintf(g_auth_hint, sizeof(g_auth_hint), "plz forget on device");
        snprintf(g_hosts_status, sizeof(g_hosts_status), "plz forget on device");
        engageStaleHostBlock(addr);
    }

    if (g_ble_server != nullptr && g_pending.conn_id != 0xFFFF) {
        g_ble_server->disconnect(g_pending.conn_id);
    }
    g_ble_connected = false;
    clearPendingConn();
}

// 只断开该 conn；不要在这里清 g_ble_connected（旧主机延迟 disconnect 会误伤新连接）
static void kickConnection(BLEServer* server, const uint16_t conn_id) {
    if (server != nullptr) {
        server->disconnect(conn_id);
    }
}

class HidKeyboardSecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override {
        return 0;
    }
    void onPassKeyNotify(uint32_t pass_key) override {
        (void)pass_key;
    }
    bool onSecurityRequest() override {
        return true;
    }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        if (!cmpl.success) {
            handleAuthFailure(cmpl.bd_addr);
            return;
        }
        if (g_pending.active) {
            commitPendingConnection();
            return;
        }
        // 已绑定回连时 AUTH 常早于 onConnect：先记下，等 stage 时提交
        g_early_auth_ok = true;
        memcpy(g_early_auth_bda, cmpl.bd_addr, 6);
    }
    bool onConfirmPIN(uint32_t pin) override {
        (void)pin;
        return true;
    }
};

class HidKeyboardBleCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        (void)server;
    }

    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        if (param == nullptr) {
            return;
        }
        // 仅拒拉黑地址；g_stale_block 只停广播，不误杀其它已配对主机
        if (isBlockedPeer(param->connect.remote_bda)) {
            kickConnection(server, param->connect.conn_id);
            return;
        }
        if (g_stale_block) {
            kickConnection(server, param->connect.conn_id);
            return;
        }
        const int slot = findHostSlotFuzzy(param->connect.remote_bda);

        // 新配对模式：拒绝已在槽里的旧主机
        if (g_pairing_open) {
            if (slot >= 0) {
                kickConnection(server, param->connect.conn_id);
                return;
            }
            stageBleConnection(param, -1, true);
            return;
        }

        // 切换中：只留目标槽；未知 RPA 也先算作目标槽回连（认证成功后再写入）
        if (g_prefer_slot >= 0 && g_hosts[g_prefer_slot].used) {
            if (slot >= 0 && slot != g_prefer_slot) {
                kickConnection(server, param->connect.conn_id);
                return;
            }
            stageBleConnection(param, g_prefer_slot, false);
            return;
        }

        // 未知主机且已有配对槽：非新配对模式则拒
        if (slot < 0) {
            if (usedHostSlotCount() > 0) {
                kickConnection(server, param->connect.conn_id);
                return;
            }
            stageBleConnection(param, -1, true);
            return;
        }

        stageBleConnection(param, slot, false);
    }

    void onDisconnect(BLEServer* server) override {
        // 有参回调里按 conn_id 判断；此处不处理，避免误清
        (void)server;
    }

    void onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        // 旧主机被踢后的延迟 disconnect：若不是当前认可连接，忽略
        if (param != nullptr && g_active_conn_id != 0xFFFF &&
            param->disconnect.conn_id != g_active_conn_id) {
            return;
        }
        // 回调时 connectedCount 尚未 --：>1 说明还有别的连接
        if (server != nullptr && server->getConnectedCount() > 1) {
            return;
        }
        g_ble_connected = false;
        g_active_conn_id = 0xFFFF;
        clearPendingConn();
        clearPeerInfo();
        if (!g_ble_ready || server == nullptr) {
            return;
        }
        // stale block 期间绝不恢复广播，否则电脑会立刻再连上来跳 UI
        if (g_stale_block) {
            BLEDevice::stopAdvertising();
            return;
        }
        configureBleAdvertising(g_pairing_open || g_prefer_slot >= 0 ||
                                usedHostSlotCount() == 0);
    }
};

static const char* transportName(const HidTransport t) {
    return t == HidTransport::USB ? "USB" : "BLE";
}

static const char* connectionStatusText() {
    if (g_transport == HidTransport::USB) {
        return g_usb_ready ? "ready" : "init...";
    }
    if (!g_ble_ready) {
        return "off";
    }
    if (g_ble_connected) {
        return "paired";
    }
    // stale / 认证失败提示优先于 connecting，避免跳变
    if (g_auth_hint[0] != '\0') {
        return g_auth_hint;
    }
    if (g_pending.active) {
        return "connecting";
    }
    if (g_pairing_open) {
        return "pair new";
    }
    if (g_prefer_slot >= 0 || (g_active_slot >= 0 && g_hosts[g_active_slot].used)) {
        return "reconnecting";
    }
    return "pairing...";
}

static uint16_t connectionStatusColor() {
    if (g_transport == HidTransport::USB) {
        return g_usb_ready ? APP_COLOR_OK : APP_COLOR_WARN;
    }
    if (g_ble_connected) {
        return APP_COLOR_OK;
    }
    if (g_auth_hint[0] != '\0') {
        return APP_COLOR_WARN;
    }
    return APP_COLOR_HINT;
}

// Cardputer 面板橙色键：库只给 base HID，Fn 层由应用自己映射
static uint8_t mapFnLayerHid(const uint8_t hid) {
    switch (hid) {
        case 0x35: return 0x29;  // ` → Esc
        case 0x2A: return 0x4C;  // Bksp → Delete
        case 0x33: return 0x52;  // ; → Up
        case 0x36: return 0x50;  // , → Left
        case 0x37: return 0x51;  // . → Down
        case 0x38: return 0x4F;  // / → Right
        case 0x1E: return 0x3A;  // 1 → F1
        case 0x1F: return 0x3B;  // 2 → F2
        case 0x20: return 0x3C;  // 3 → F3
        case 0x21: return 0x3D;  // 4 → F4
        case 0x22: return 0x3E;  // 5 → F5
        case 0x23: return 0x3F;  // 6 → F6
        case 0x24: return 0x40;  // 7 → F7
        case 0x25: return 0x41;  // 8 → F8
        case 0x26: return 0x42;  // 9 → F9
        case 0x27: return 0x43;  // 0 → F10
        case 0x2D: return 0x44;  // - → F11
        case 0x2E: return 0x45;  // = → F12
        default: return 0;       // 未映射的 Fn 组合不发给主机
    }
}

static const char* fnLayerLabel(const uint8_t hid) {
    switch (hid) {
        case 0x29: return "ESC";
        case 0x4C: return "DEL";
        case 0x52: return "UP";
        case 0x50: return "LEFT";
        case 0x51: return "DOWN";
        case 0x4F: return "RIGHT";
        case 0x3A: return "F1";
        case 0x3B: return "F2";
        case 0x3C: return "F3";
        case 0x3D: return "F4";
        case 0x3E: return "F5";
        case 0x3F: return "F6";
        case 0x40: return "F7";
        case 0x41: return "F8";
        case 0x42: return "F9";
        case 0x43: return "F10";
        case 0x44: return "F11";
        case 0x45: return "F12";
        default: return nullptr;
    }
}

static void updateEchoBuffer(const Keyboard_Class::KeysState& status) {
    // Fn 层优先显示特殊键标签，不把 ` ; 等写进回显
    if (status.fn) {
        if (status.shift) {
            strncpy(g_last_label, "CAPS", sizeof(g_last_label) - 1);
            return;
        }
        if (status.ctrl) {
            strncpy(g_last_label, "RCTL", sizeof(g_last_label) - 1);
            return;
        }
        if (status.opt) {
            strncpy(g_last_label, "RGUI", sizeof(g_last_label) - 1);
            return;
        }
        if (status.alt) {
            strncpy(g_last_label, "RALT", sizeof(g_last_label) - 1);
            return;
        }
        for (const uint8_t raw : status.hid_keys) {
            const uint8_t mapped = mapFnLayerHid(raw & 0x7F);
            const char* label = fnLayerLabel(mapped);
            if (label != nullptr) {
                strncpy(g_last_label, label, sizeof(g_last_label) - 1);
                g_last_label[sizeof(g_last_label) - 1] = '\0';
                return;
            }
        }
        return;
    }
    if (status.opt && status.hid_keys.empty() && !status.ctrl && !status.shift && !status.alt &&
        !status.del && !status.enter && !status.space && !status.tab) {
        strncpy(g_last_label, "GUI", sizeof(g_last_label) - 1);
        return;
    }

    if (status.del) {
        const size_t n = strlen(g_echo);
        if (n > 0) {
            g_echo[n - 1] = '\0';
        }
        strncpy(g_last_label, "BKSP", sizeof(g_last_label) - 1);
        return;
    }
    if (status.enter) {
        g_echo[0] = '\0';
        strncpy(g_last_label, "ENT", sizeof(g_last_label) - 1);
        return;
    }
    if (status.space) {
        const size_t n = strlen(g_echo);
        if (n < kEchoMaxChars) {
            g_echo[n] = ' ';
            g_echo[n + 1] = '\0';
        } else {
            memmove(g_echo, g_echo + 1, kEchoMaxChars - 1);
            g_echo[kEchoMaxChars - 1] = ' ';
            g_echo[kEchoMaxChars] = '\0';
        }
        strncpy(g_last_label, "SPC", sizeof(g_last_label) - 1);
        return;
    }
    if (status.tab) {
        strncpy(g_last_label, "TAB", sizeof(g_last_label) - 1);
        return;
    }
    for (const char c : status.word) {
        if (c < 32 || c > 126) {
            continue;
        }
        const size_t n = strlen(g_echo);
        if (n < kEchoMaxChars) {
            g_echo[n] = c;
            g_echo[n + 1] = '\0';
        } else {
            memmove(g_echo, g_echo + 1, kEchoMaxChars - 1);
            g_echo[kEchoMaxChars - 1] = c;
            g_echo[kEchoMaxChars] = '\0';
        }
        snprintf(g_last_label, sizeof(g_last_label), "%c", c);
    }
}

// 退出 USB 键盘后把 PHY 还给 USB-Serial/JTAG，恢复烧录口
// 逻辑对齐 Arduino core esp32-hal-tinyusb.c 的 usb_switch_to_cdc_jtag()
static void restoreUsbSerialJtag() {
    if (tusb_inited()) {
        tud_disconnect();
        delay(30);
    }
    if (g_otg_phy != nullptr) {
        usb_del_phy(g_otg_phy);
        g_otg_phy = nullptr;
    }

    periph_module_reset(PERIPH_USB_MODULE);
    periph_module_disable(PERIPH_USB_MODULE);

    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, (RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL |
                                                 RTC_CNTL_USB_PAD_ENABLE));
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);

    // 拉低 D+/D- 迫使主机重新枚举
    pinMode(USBPHY_DM_NUM, OUTPUT_OPEN_DRAIN);
    pinMode(USBPHY_DP_NUM, OUTPUT_OPEN_DRAIN);
    digitalWrite(USBPHY_DM_NUM, LOW);
    digitalWrite(USBPHY_DP_NUM, LOW);
    delay(20);

    // 等价于 usb_phy_ll_int_jtag_enable（不直接 include ll 头，避免 C++ volatile 编译错误）
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL);
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_USB_PHY_SEL);
    delay(80);
}

static void startUsbKeyboard() {
    if (g_usb_ready) {
        return;
    }
    if (!g_usb_inited) {
        g_usb_kb.begin();
        USB.begin();
        g_usb_inited = true;
        g_usb_ready = true;
        return;
    }
    // 再次进入：重新挂 OTG PHY 并 connect
    usb_phy_config_t cfg = {};
    cfg.controller = USB_PHY_CTRL_OTG;
    cfg.target = USB_PHY_TARGET_INT;
    cfg.otg_mode = USB_OTG_MODE_DEVICE;
    cfg.otg_speed = USB_PHY_SPEED_FULL;
    if (usb_new_phy(&cfg, &g_otg_phy) != ESP_OK) {
        // 回退：直接使能 USB 模块
        periph_module_reset(PERIPH_USB_MODULE);
        periph_module_enable(PERIPH_USB_MODULE);
    }
    tud_connect();
    delay(50);
    g_usb_ready = true;
}

static void stopUsbKeyboard() {
    if (!g_usb_ready && !g_usb_inited) {
        return;
    }
    if (g_usb_ready) {
        g_usb_kb.releaseAll();
    }
    g_usb_ready = false;
    // 无论是否 inited，只要动过 USB 就切回 JTAG，保证可烧录
    restoreUsbSerialJtag();
}

static void stopBleKeyboard() {
    // 退出时完整释放 BLE，避免长期占几十 KB；deinit 可能卡几秒，调用方需先画 Exiting.
    if (g_ble_server != nullptr) {
        disconnectBleClients();
    }
    g_hid = nullptr;
    g_kb_input = nullptr;
    g_ble_server = nullptr;
    g_ble_ready = false;
    g_ble_connected = false;
    g_pairing_open = false;
    g_prefer_slot = -1;
    g_active_conn_id = 0xFFFF;
    g_auth_hint[0] = '\0';
    g_auth_fail_streak = 0;
    clearPendingConn();
    clearStaleHostBlock();
    clearEarlyAuth();
    clearPeerInfo();
    clearBleStackParked();
    resetBleStackFully();
    applyWifiRadioSleepPolicy();
}

static void disconnectBleClients() {
    if (g_ble_server == nullptr) {
        return;
    }
    auto peers = g_ble_server->getPeerDevices(false);
    for (auto& peer : peers) {
        g_ble_server->disconnect(peer.first);
    }
    if (g_ble_server->getConnectedCount() > 0) {
        g_ble_server->disconnect(g_ble_server->getConnId());
    }
}

static void startBleKeyboard() {
    if (g_ble_ready) {
        return;
    }
    stopBleStack();
    clearBleStackParked();
    resetBleStackFully();
    WiFi.setSleep(true);
    loadHostSlots();
    BLEDevice::init("Cardputer KB");
    g_ble_server = BLEDevice::createServer();
    g_ble_server->setCallbacks(new HidKeyboardBleCallbacks());
    BLEDevice::setSecurityCallbacks(new HidKeyboardSecurityCallbacks());

    g_hid = new BLEHIDDevice(g_ble_server);
    g_kb_input = g_hid->inputReport(1);
    g_hid->manufacturer()->setValue("M5Stack");
    g_hid->pnp(0x02, 0x1234, 0x5678, 0x0100);
    g_hid->hidInfo(0x00, 0x01);
    g_hid->reportMap(const_cast<uint8_t*>(kHidReportMap), sizeof(kHidReportMap));
    g_hid->startServices();

    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* advertising = g_ble_server->getAdvertising();
    advertising->setAppearance(ESP_BLE_APPEARANCE_HID_KEYBOARD);
    advertising->addServiceUUID(g_hid->hidService()->getUUID());
    advertising->setScanResponse(true);

    g_ble_ready = true;
    g_ble_connected = false;
    clearStaleHostBlock();
    clearEarlyAuth();
    g_auth_hint[0] = '\0';
    syncHostSlotsWithBonds();
    // 无已存主机时开放配对；否则只等 active 槽回连
    g_pairing_open = (usedHostSlotCount() == 0);
    configureBleAdvertising(g_pairing_open || usedHostSlotCount() > 0);
}

// 切到指定已配对主机：断开后开放广播，只接受该槽回连（需主机端点选键盘）
static void switchToHostSlot(const int slot) {
    if (slot < 0 || slot >= kBleHostSlots || !g_hosts[slot].used) {
        snprintf(g_hosts_status, sizeof(g_hosts_status), "empty");
        return;
    }
    applyTransport(HidTransport::BLE);
    if (!g_ble_ready) {
        return;
    }
    clearBleReportQueue();
    g_active_slot = slot;
    g_prefer_slot = slot;
    g_pairing_open = false;
    g_auth_hint[0] = '\0';
    g_auth_fail_streak = 0;
    clearStaleHostBlock();
    saveHostSlots();
    disconnectBleClients();
    g_ble_connected = false;
    clearPendingConn();
    clearPeerInfo();
    delay(120);
    configureBleAdvertising(false);
    // 等目标电脑自动回连（与商业多设备键盘同理；少数系统需点一下）
    snprintf(g_hosts_status, sizeof(g_hosts_status), "reconnecting #%d", slot + 1);
    g_hosts_exit_on_connect = true;
}

// 开放新配对：踢掉旧主机抢连，直到真正的新设备连上
static void startOpenPairing() {
    applyTransport(HidTransport::BLE);
    if (!g_ble_ready) {
        return;
    }
    if (firstEmptyHostSlot() < 0) {
        snprintf(g_hosts_status, sizeof(g_hosts_status), "full, del first");
        return;
    }
    clearBleReportQueue();
    g_pairing_open = true;
    g_prefer_slot = -1;
    g_auth_hint[0] = '\0';
    g_auth_fail_streak = 0;
    clearStaleHostBlock();
    disconnectBleClients();
    g_ble_connected = false;
    clearPendingConn();
    clearPeerInfo();
    delay(120);
    configureBleAdvertising(true);
    snprintf(g_hosts_status, sizeof(g_hosts_status), "pair new...");
    g_hosts_exit_on_connect = true;
}

static void openHostsUi() {
    applyTransport(HidTransport::BLE);
    g_hosts_ui = true;
    g_help_visible = false;
    g_rename_ui = false;
    g_rename_buf[0] = '\0';
    g_hosts_key_latched = true;
    g_hosts_exit_on_connect = false;  // 仅浏览列表时不自动退出
    g_hosts_status[0] = '\0';
    if (g_sel_slot < 0 || g_sel_slot >= kBleHostSlots) {
        g_sel_slot = 0;
    }
    if (g_active_slot >= 0) {
        g_sel_slot = g_active_slot;
    }
}

static void applyTransport(const HidTransport next) {
    if (g_transport == next &&
        ((next == HidTransport::USB && g_usb_ready) || (next == HidTransport::BLE && g_ble_ready))) {
        return;
    }
    g_transport = next;
    if (next == HidTransport::USB) {
        stopBleKeyboard();
        startUsbKeyboard();
    } else {
        stopUsbKeyboard();
        startBleKeyboard();
    }
}

static void drawHelpPage();
static void drawHostsUi();
static void redrawHostsListAndStatus();
static void drawHostsHintBar();
static void drawHidKeyboardApp(const bool full_init);

static void beginRenameHostSlot() {
    if (!g_hosts[g_sel_slot].used) {
        snprintf(g_hosts_status, sizeof(g_hosts_status), "empty");
        redrawHostsListAndStatus();
        return;
    }
    g_rename_ui = true;
    strncpy(g_rename_buf, g_hosts[g_sel_slot].alias, sizeof(g_rename_buf) - 1);
    g_rename_buf[sizeof(g_rename_buf) - 1] = '\0';
    g_hosts_status[0] = '\0';
    redrawHostsListAndStatus();
    drawHostsHintBar();
}

static void cancelRenameHostSlot() {
    g_rename_ui = false;
    g_rename_buf[0] = '\0';
    g_hosts_status[0] = '\0';
    redrawHostsListAndStatus();
    drawHostsHintBar();
}

static void commitRenameHostSlot() {
    if (!g_hosts[g_sel_slot].used) {
        cancelRenameHostSlot();
        return;
    }
    // 去首尾空格，避免列表里看起来像空槽
    size_t start = 0;
    while (g_rename_buf[start] == ' ') {
        start++;
    }
    size_t end = strlen(g_rename_buf);
    while (end > start && g_rename_buf[end - 1] == ' ') {
        end--;
    }
    const size_t n = end - start;
    if (n == 0) {
        g_hosts[g_sel_slot].alias[0] = '\0';
    } else {
        const size_t copy_n = (n > kHostAliasMax) ? kHostAliasMax : n;
        memcpy(g_hosts[g_sel_slot].alias, g_rename_buf + start, copy_n);
        g_hosts[g_sel_slot].alias[copy_n] = '\0';
    }
    saveHostSlots();
    g_rename_ui = false;
    g_rename_buf[0] = '\0';
    if (g_hosts[g_sel_slot].alias[0] != '\0') {
        snprintf(g_hosts_status, sizeof(g_hosts_status), "named #%d", g_sel_slot + 1);
    } else {
        snprintf(g_hosts_status, sizeof(g_hosts_status), "cleared #%d", g_sel_slot + 1);
    }
    redrawHostsListAndStatus();
    drawHostsHintBar();
}

// 主机列表页按键（不发给主机）
static bool tryHandleHostsUi(const Keyboard_Class::KeysState& status) {
    if (!g_hosts_ui) {
        return false;
    }
    if (!M5Cardputer.Keyboard.isPressed()) {
        g_hosts_key_latched = false;
        return true;
    }
    if (g_hosts_key_latched) {
        return true;
    }

    // 重命名模式：输入别名，不触发列表热键
    if (g_rename_ui) {
        if (status.del) {
            g_hosts_key_latched = true;
            const size_t n = strlen(g_rename_buf);
            if (n > 0) {
                g_rename_buf[n - 1] = '\0';
                redrawHostsListAndStatus();
            }
            return true;
        }
        if (status.enter) {
            g_hosts_key_latched = true;
            commitRenameHostSlot();
            return true;
        }
        if (status.space) {
            g_hosts_key_latched = true;
            const size_t n = strlen(g_rename_buf);
            if (n < kHostAliasMax) {
                g_rename_buf[n] = ' ';
                g_rename_buf[n + 1] = '\0';
                redrawHostsListAndStatus();
            }
            return true;
        }
        for (const char c : status.word) {
            if (c == '\b') {
                g_hosts_key_latched = true;
                const size_t n = strlen(g_rename_buf);
                if (n > 0) {
                    g_rename_buf[n - 1] = '\0';
                    redrawHostsListAndStatus();
                }
                return true;
            }
            // Esc / ` 取消（与 WiFi 密码页类似）
            if (c == 0x1B || c == '`') {
                g_hosts_key_latched = true;
                cancelRenameHostSlot();
                return true;
            }
            if (c < 32 || c > 126) {
                continue;
            }
            g_hosts_key_latched = true;
            const size_t n = strlen(g_rename_buf);
            if (n < kHostAliasMax) {
                g_rename_buf[n] = c;
                g_rename_buf[n + 1] = '\0';
                redrawHostsListAndStatus();
            }
            return true;
        }
        return true;
    }

    // 翻页键移动光标
    const int delta = getMenuNavDelta(status);
    if (delta != 0) {
        g_sel_slot = (g_sel_slot + delta + kBleHostSlots) % kBleHostSlots;
        g_hosts_key_latched = true;
        redrawHostsListAndStatus();
        return true;
    }

    if (status.enter || status.space) {
        g_hosts_key_latched = true;
        switchToHostSlot(g_sel_slot);
        redrawHostsListAndStatus();
        return true;
    }

    for (const char c : status.word) {
        if (c >= '1' && c <= '5') {
            g_sel_slot = c - '1';
            g_hosts_key_latched = true;
            redrawHostsListAndStatus();
            return true;
        }
        if (c == 'n' || c == 'N') {
            g_hosts_key_latched = true;
            startOpenPairing();
            redrawHostsListAndStatus();
            return true;
        }
        if (c == 'r' || c == 'R') {
            g_hosts_key_latched = true;
            beginRenameHostSlot();
            return true;
        }
        if (c == 'd' || c == 'D') {
            g_hosts_key_latched = true;
            if (g_hosts[g_sel_slot].used) {
                const bool was_conn_target =
                    g_ble_connected && g_active_slot == g_sel_slot;
                if (was_conn_target) {
                    disconnectBleClients();
                    g_ble_connected = false;
                    clearPeerInfo();
                }
                deleteHostSlot(g_sel_slot);
                if (g_ble_ready) {
                    g_pairing_open = (usedHostSlotCount() == 0);
                    configureBleAdvertising(g_pairing_open);
                }
                snprintf(g_hosts_status, sizeof(g_hosts_status), "deleted #%d", g_sel_slot + 1);
            } else {
                snprintf(g_hosts_status, sizeof(g_hosts_status), "empty");
            }
            redrawHostsListAndStatus();
            return true;
        }
        if (c == 'p' || c == 'P' || c == 'h' || c == 'H') {
            g_hosts_key_latched = true;
            g_hosts_ui = false;
            g_rename_ui = false;
            drawHidKeyboardApp(true);
            return true;
        }
    }
    return true;  // 列表打开时吞掉其它键
}

// Fn+h 帮助；帮助页内 h/,/. 翻页关闭；Fn+u/b/p 模式热键；主机列表
static bool tryHandleModeHotkey(const Keyboard_Class::KeysState& status) {
    if (tryHandleHostsUi(status)) {
        return true;
    }

    // 帮助页：h 关闭；方向键/;,./ 翻页（不发给主机）
    if (g_help_visible) {
        if (!M5Cardputer.Keyboard.isPressed()) {
            g_fn_h_latched = false;
            return true;
        }
        for (const char c : status.word) {
            if (c == 'h' || c == 'H') {
                if (!g_fn_h_latched) {
                    g_help_visible = false;
                    g_fn_h_latched = true;
                    // 帮助从 NO_GAP 画起，clearAppContentArea 清不干净，需全屏重绘
                    drawHidKeyboardApp(true);
                }
                return true;
            }
        }
        if (status.fn) {
            for (const char c : status.word) {
                if (c == 'h' || c == 'H') {
                    if (!g_fn_h_latched) {
                        g_help_visible = false;
                        g_fn_h_latched = true;
                        drawHidKeyboardApp(true);
                    }
                    return true;
                }
            }
        }
        const int delta = getMenuNavDelta(status);
        if (delta != 0) {
            const int next = g_help_page + delta;
            if (next >= 0 && next < kHelpPageCount) {
                g_help_page = next;
                drawHelpPage();
            }
        }
        return true;  // 帮助打开时吞掉其它键
    }

    if (!status.fn) {
        g_fn_h_latched = false;
        g_fn_caps_latched = false;
        return false;
    }

    // Fn+Aa：本地 Caps 状态翻转（主机侧靠 HID Caps 键）
    if (status.shift && !g_fn_caps_latched) {
        g_fn_caps_latched = true;
        M5Cardputer.Keyboard.setCapsLocked(!M5Cardputer.Keyboard.capslocked());
    } else if (!status.shift) {
        g_fn_caps_latched = false;
    }

    for (const char c : status.word) {
        if (c == 'h' || c == 'H') {
            if (!g_fn_h_latched) {
                g_help_visible = true;
                g_help_page = 0;
                g_fn_h_latched = true;
                drawHelpPage();
            }
            return true;
        }
        if (c == 'u' || c == 'U') {
            applyTransport(HidTransport::USB);
            return true;
        }
        if (c == 'b' || c == 'B') {
            applyTransport(HidTransport::BLE);
            return true;
        }
        if (c == 'p' || c == 'P') {
            openHostsUi();
            drawHostsUi();
            return true;
        }
    }
    return false;
}

static void clearBleReportQueue() {
    g_ble_q_head = 0;
    g_ble_q_tail = 0;
    g_ble_q_count = 0;
}

static void enqueueBleReport(const KeyReport& report) {
    BleReport item{};
    item.data[0] = report.modifiers;
    item.data[1] = 0;
    for (int i = 0; i < 6; i++) {
        item.data[2 + i] = report.keys[i];
    }
    if (g_ble_q_count > 0) {
        const size_t last = (g_ble_q_tail + kBleReportQueueCap - 1) % kBleReportQueueCap;
        if (memcmp(g_ble_q[last].data, item.data, sizeof(item.data)) == 0) {
            return;
        }
    }
    if (g_ble_q_count >= kBleReportQueueCap) {
        g_ble_q_head = (g_ble_q_head + 1) % kBleReportQueueCap;
        g_ble_q_count--;
    }
    g_ble_q[g_ble_q_tail] = item;
    g_ble_q_tail = (g_ble_q_tail + 1) % kBleReportQueueCap;
    g_ble_q_count++;
}

static void drainBleReportQueue() {
    if (!g_ble_ready || g_kb_input == nullptr || !g_ble_connected || g_ble_q_count == 0) {
        return;
    }
    const uint32_t now = millis();
    if (now - g_ble_last_send_ms < kBleReportIntervalMs) {
        return;
    }
    const BleReport& item = g_ble_q[g_ble_q_head];
    g_kb_input->setValue(const_cast<uint8_t*>(item.data), sizeof(item.data));
    g_kb_input->notify();
    g_ble_q_head = (g_ble_q_head + 1) % kBleReportQueueCap;
    g_ble_q_count--;
    g_ble_last_send_ms = now;
}

static void pushHidKey(KeyReport& report, uint8_t& idx, const uint8_t hid) {
    if (hid == 0 || idx >= 6) {
        return;
    }
    for (uint8_t i = 0; i < idx; ++i) {
        if (report.keys[i] == hid) {
            return;
        }
    }
    report.keys[idx++] = hid;
}

static void buildKeyReport(const Keyboard_Class::KeysState& status, KeyReport& report) {
    memset(&report, 0, sizeof(report));
    uint8_t idx = 0;

    if (status.fn) {
        // 左下角 Ctrl/Opt/Alt + Fn → 右侧对应修饰键；Aa+Fn → Caps Lock
        if (status.ctrl) {
            report.modifiers |= kModRCtrl;
        }
        if (status.opt) {
            report.modifiers |= kModRGui;
        }
        if (status.alt) {
            report.modifiers |= kModRAlt;
        }
        if (status.shift) {
            pushHidKey(report, idx, kHidCapsLock);
        }
        for (const uint8_t raw : status.hid_keys) {
            pushHidKey(report, idx, mapFnLayerHid(raw & 0x7F));
        }
        return;
    }

    // 普通层：Ctrl/Shift/Alt 左修饰；Opt 作为 Left GUI（Win/Cmd）
    if (status.ctrl) {
        report.modifiers |= kModLCtrl;
    }
    if (status.shift) {
        report.modifiers |= kModLShift;
    }
    if (status.alt) {
        report.modifiers |= kModLAlt;
    }
    if (status.opt) {
        report.modifiers |= kModLGui;
    }
    for (const uint8_t raw : status.hid_keys) {
        pushHidKey(report, idx, raw & 0x7F);
    }
    if (status.space) {
        pushHidKey(report, idx, 0x2C);
    }
}

static void sendHostReport(const Keyboard_Class::KeysState& status) {
    KeyReport report{};
    buildKeyReport(status, report);
    const bool empty = report.modifiers == 0 && report.keys[0] == 0;

    if (g_transport == HidTransport::USB) {
        if (!g_usb_ready) {
            return;
        }
        if (empty) {
            g_usb_kb.releaseAll();
        } else {
            g_usb_kb.sendReport(&report);
        }
        return;
    }

    if (!g_ble_ready || !g_ble_connected) {
        return;
    }
    enqueueBleReport(report);
}

static int hintBarY() {
    return M5Cardputer.Display.height() - 12;
}

// tip 共两排；上行在底栏上方
static int hintBarRow1Y() {
    return hintBarY() - 12;
}

// peer MAC 画在两排 tip 正上方
static int peerLineY() {
    return hintBarRow1Y() - INFO_LINE_H;
}

static int echoAreaY() {
    return APP_CONTENT_INSET_Y + INFO_LINE_H_2X + 4 + 5;  // 输入回显相对状态行再下移 5px
}

static void drawPeerLine() {
    // 底栏上方：有别名显示别名，否则 MAC；槽号在右上角
    char text[sizeof(g_drawn_peer)] = "";
    if (g_transport == HidTransport::BLE) {
        if (g_active_slot >= 0 && g_hosts[g_active_slot].used &&
            g_hosts[g_active_slot].alias[0] != '\0') {
            strncpy(text, g_hosts[g_active_slot].alias, sizeof(text) - 1);
        } else if (g_peer_addr[0] != '\0') {
            strncpy(text, g_peer_addr, sizeof(text) - 1);
        } else if (g_active_slot >= 0 && g_hosts[g_active_slot].used) {
            formatPeerAddr(g_hosts[g_active_slot].addr, text, sizeof(text));
        }
    }
    if (strcmp(g_drawn_peer, text) == 0) {
        return;
    }
    const int y = peerLineY();
    M5Cardputer.Display.fillRect(0, y, M5Cardputer.Display.width(), INFO_LINE_H, BLACK);
    if (text[0] != '\0') {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print(text);
    }
    memcpy(g_drawn_peer, text, sizeof(g_drawn_peer));
}

// 右上角黄底徽章显示当前主机槽号（1..5）
static void drawSlotBadge() {
    int num = -1;
    if (g_transport == HidTransport::BLE && g_active_slot >= 0 && g_hosts[g_active_slot].used) {
        num = g_active_slot + 1;
    }
    if (num == g_drawn_slot_num) {
        return;
    }
    const int screen_w = M5Cardputer.Display.width();
    constexpr int clear_w = 28;
    constexpr int clear_h = 20;
    const int clear_x = screen_w - clear_w - 2;
    const int y = APP_CONTENT_INSET_Y;
    M5Cardputer.Display.fillRect(clear_x, y, clear_w, clear_h, BLACK);
    if (num >= 1 && num <= 9) {
        const char key = static_cast<char>('0' + num);
        M5Cardputer.Display.setTextSize(2);
        const char str[2] = {key, '\0'};
        const int tw = M5Cardputer.Display.textWidth(str);
        const int bw = tw + 4;  // 与 drawKeyBadge pad_x*2 一致
        const int x = screen_w - bw - 4;
        drawKeyBadge(x, y, key, 2);
    }
    g_drawn_slot_num = num;
}

// 内容区状态：无 link: 前缀，仅状态字
static void drawLinkStatus() {
    const char* text = connectionStatusText();
    if (strcmp(g_drawn_link_status, text) == 0) {
        return;
    }
    const int y = APP_CONTENT_INSET_Y;
    // 留给右侧槽号徽章
    const int clear_w = M5Cardputer.Display.width() - 32;
    M5Cardputer.Display.fillRect(0, y, clear_w, INFO_LINE_H_2X, BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(connectionStatusColor(), BLACK);
    M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
    M5Cardputer.Display.print(text);
    strncpy(g_drawn_link_status, text, sizeof(g_drawn_link_status) - 1);
    g_drawn_link_status[sizeof(g_drawn_link_status) - 1] = '\0';
}

static void drawEchoOnly() {
    if (strcmp(g_drawn_echo, g_echo) == 0 && strcmp(g_drawn_label, g_last_label) == 0) {
        return;
    }
    const int echo_h = 8 * kEchoTextSize;
    const int echo_y = echoAreaY();
    M5Cardputer.Display.fillRect(0, echo_y, M5Cardputer.Display.width(), echo_h + 4, BLACK);
    M5Cardputer.Display.setTextSize(kEchoTextSize);
    M5Cardputer.Display.setTextColor(APP_COLOR_TEXT, BLACK);
    if (g_echo[0] != '\0') {
        M5Cardputer.Display.drawCenterString(g_echo, M5Cardputer.Display.width() / 2, echo_y);
    } else if (g_last_label[0] != '\0') {
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.drawCenterString(g_last_label, M5Cardputer.Display.width() / 2, echo_y);
    } else {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.drawCenterString("type to send", M5Cardputer.Display.width() / 2, echo_y + 8);
    }
    strncpy(g_drawn_echo, g_echo, sizeof(g_drawn_echo) - 1);
    g_drawn_echo[sizeof(g_drawn_echo) - 1] = '\0';
    strncpy(g_drawn_label, g_last_label, sizeof(g_drawn_label) - 1);
    g_drawn_label[sizeof(g_drawn_label) - 1] = '\0';
}

static void drawHintBar() {
    // 先刷 tip 上方 peer 行，再画两排 tip
    g_drawn_peer[0] = '\0';
    drawPeerLine();

    const int row1_y = hintBarRow1Y();
    const int row2_y = hintBarY();
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, row1_y, screen_w, 24, BLACK);

    // 上行：USB / BLE 切换
    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, row1_y, "Fn+u", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row1_y + 1);
    M5Cardputer.Display.print("usb ");
    cx = M5Cardputer.Display.getCursorX();
    cx += drawTextBadge(cx, row1_y, "Fn+b", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row1_y + 1);
    M5Cardputer.Display.print("ble");

    // 下行：退出 + 配对；右侧 h help
    cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, row2_y, "BtnGO", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("exit ");
    cx = M5Cardputer.Display.getCursorX() + 4;
    cx += drawTextBadge(cx, row2_y, "Fn+p", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("hosts");
    drawHelpHintRight("help");
}

// Help 分栏标题
static int helpDrawColHeader(const int x, const int y, const int w, const char* title) {
    M5Cardputer.Display.fillRect(x, y, w, 11, APP_COLOR_LABEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, APP_COLOR_LABEL);
    M5Cardputer.Display.setCursor(x + 2, y + 1);
    M5Cardputer.Display.print(title);
    return y + 13;
}

// Help 文本徽章说明；徽章后恢复说明文字颜色
static int helpDrawBadge(const int x, const int y, const char* badge, const char* text) {
    const int cx = x + drawTextBadge(x, y, badge, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// Help 功能说明
static int helpDrawLine(const int x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text);
    return y + 11;
}

// 底栏：箭头徽章翻页 + 页码，右侧 h close
static void drawHelpHintBar() {
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_CONTENT_X;
    cx += drawArrowBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y + 1);
    M5Cardputer.Display.print("page ");
    cx += M5Cardputer.Display.textWidth("page ");
    char buf[8];
    snprintf(buf, sizeof(buf), "%d/%d", g_help_page + 1, kHelpPageCount);
    M5Cardputer.Display.setCursor(cx, hint_y + 1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(buf);
    drawHelpHintRight("close");
}

static void drawHelpPage() {
    beginAppScreenAccent("Keyboard ", "Help", APP_COLOR_LABEL);
    g_screen_ready = true;
    clearAppContentArea();

    constexpr int col_gap = 4;
    const int screen_w = M5Cardputer.Display.width();
    const int col_w = (screen_w - col_gap) / 2;
    const int manual_x = col_w + col_gap;
    const int col_y = APP_CONTENT_Y_NO_TAP_TO_HEADER;
    M5Cardputer.Display.drawFastVLine(col_w + col_gap / 2, col_y,
                                     M5Cardputer.Display.height() - col_y, DARKGREY);

    if (g_help_page == 0) {
        int y = helpDrawColHeader(0, col_y, col_w, "keymap");
        y = helpDrawBadge(2, y, "BtnGO", "exit app");
        y = helpDrawBadge(2, y, "Fn+u", "USB");
        y = helpDrawBadge(2, y, "Fn+b", "BLE");
        y = helpDrawBadge(2, y, "Fn+p", "hosts");
        y = helpDrawBadge(2, y, "Fn+h", "help");

        y = helpDrawColHeader(manual_x, col_y, screen_w - manual_x, "manual");
        y = helpDrawLine(manual_x + 2, y, "USB / BLE keyboard");
        y = helpDrawLine(manual_x + 2, y, "all keys go to host");
        y = helpDrawLine(manual_x + 2, y, "BLE keeps 5 hosts");
        y = helpDrawLine(manual_x + 2, y, "Fn+p list / pair");
        y = helpDrawLine(manual_x + 2, y, "name: Cardputer KB");
        y = helpDrawLine(manual_x + 2, y, "exit restores JTAG");
    } else {
        int y = helpDrawColHeader(0, col_y, col_w, "keymap");
        y = helpDrawBadge(2, y, "Fn+`", "Esc");
        y = helpDrawBadge(2, y, "Fn+Bk", "Delete");
        y = helpDrawBadge(2, y, "Fn+; , . /", "arrows");
        y = helpDrawBadge(2, y, "Fn+1..0", "F1-10");
        y = helpDrawBadge(2, y, "Fn+- =", "F11/12");
        y = helpDrawBadge(2, y, "Fn+Aa", "Caps");
        y = helpDrawBadge(2, y, "Fn+mods", "right");

        y = helpDrawColHeader(manual_x, col_y, screen_w - manual_x, "hosts");
        y = helpDrawLine(manual_x + 2, y, "list: 1-5 select");
        y = helpDrawLine(manual_x + 2, y, "Enter switch");
        y = helpDrawLine(manual_x + 2, y, "n new / d delete");
        y = helpDrawLine(manual_x + 2, y, "r rename alias");
        y = helpDrawLine(manual_x + 2, y, "p close list");
        y = helpDrawLine(manual_x + 2, y, "one host at a time");
    }

    drawHelpHintBar();
}

static void drawHostsHintBar() {
    const int row1_y = hintBarRow1Y();
    const int row2_y = hintBarY();
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(0, row1_y, screen_w, 24, BLACK);

    if (g_rename_ui) {
        // 重命名：Enter 保存，Bksp 删字，` 取消
        int cx = APP_CONTENT_X;
        cx += drawTextBadge(cx, row1_y, "Ent", 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, row1_y + 1);
        M5Cardputer.Display.print("save ");
        cx = M5Cardputer.Display.getCursorX();
        cx += drawTextBadge(cx, row1_y, "Bk", 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, row1_y + 1);
        M5Cardputer.Display.print("del");

        cx = APP_CONTENT_X;
        cx += drawKeyBadge(cx, row2_y, '`', 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, row2_y + 1);
        M5Cardputer.Display.print("cancel");
        return;
    }

    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, row1_y, "1-5", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row1_y + 1);
    M5Cardputer.Display.print("sel ");
    cx = M5Cardputer.Display.getCursorX();
    cx += drawTextBadge(cx, row1_y, "Ent", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row1_y + 1);
    M5Cardputer.Display.print("go");

    cx = APP_CONTENT_X;
    cx += drawKeyBadge(cx, row2_y, 'n', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("new ");
    cx = M5Cardputer.Display.getCursorX();
    cx += drawKeyBadge(cx, row2_y, 'd', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("del ");
    cx = M5Cardputer.Display.getCursorX();
    cx += drawKeyBadge(cx, row2_y, 'r', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("name ");
    cx = M5Cardputer.Display.getCursorX();
    cx += drawKeyBadge(cx, row2_y, 'p', 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("back");
}

// 状态行：已连接绿色并带槽号；等待/配对等用橙色
static void formatHostsStatusLine(char* out, size_t out_len, uint16_t* color_out) {
    out[0] = '\0';
    *color_out = APP_COLOR_HINT;
    if (g_rename_ui) {
        *color_out = APP_COLOR_VALUE;
        snprintf(out, out_len, "rename #%d", g_sel_slot + 1);
        return;
    }
    if (g_ble_connected) {
        *color_out = APP_COLOR_OK;
        if (g_active_slot >= 0 && g_hosts[g_active_slot].used) {
            char name[18];
            formatHostDisplayName(g_active_slot, name, sizeof(name));
            snprintf(out, out_len, "connected #%d %s", g_active_slot + 1, name);
        } else if (g_peer_addr[0] != '\0') {
            snprintf(out, out_len, "connected %s", g_peer_addr);
        } else {
            snprintf(out, out_len, "connected");
        }
        return;
    }
    *color_out = APP_COLOR_WARN;
    if (g_hosts_status[0] != '\0') {
        snprintf(out, out_len, "%s", g_hosts_status);
        return;
    }
    if (g_pending.active) {
        snprintf(out, out_len, "connecting...");
        return;
    }
    if (g_pairing_open) {
        snprintf(out, out_len, "pair new...");
        return;
    }
    if (g_prefer_slot >= 0 && g_hosts[g_prefer_slot].used) {
        snprintf(out, out_len, "reconnecting #%d", g_prefer_slot + 1);
    }
}

static int hostsListStatusY() {
    return APP_CONTENT_INSET_Y + kBleHostSlots * 10 + 2;
}

// 只重绘列表行 + 状态行（不清屏、不重画 header/tip，避免切换时狂闪）
static void redrawHostsListAndStatus() {
    M5Cardputer.Display.setTextSize(1);
    int y = APP_CONTENT_INSET_Y;
    const int screen_w = M5Cardputer.Display.width();
    for (int i = 0; i < kBleHostSlots; i++) {
        M5Cardputer.Display.fillRect(0, y, screen_w, 10, BLACK);
        const bool sel = (i == g_sel_slot);
        const bool linked = g_ble_connected && i == g_active_slot && g_hosts[i].used;
        char line[32];
        if (g_rename_ui && sel && g_hosts[i].used) {
            // 编辑中：当前输入 + 光标
            snprintf(line, sizeof(line), "%c%d %s_", sel ? '>' : ' ', i + 1, g_rename_buf);
        } else if (g_hosts[i].used) {
            char name[18];
            formatHostDisplayName(i, name, sizeof(name));
            snprintf(line, sizeof(line), "%c%d %s%s", sel ? '>' : ' ', i + 1, name,
                     linked ? " *" : "");
        } else {
            snprintf(line, sizeof(line), "%c%d (empty)", sel ? '>' : ' ', i + 1);
        }
        M5Cardputer.Display.setTextColor(sel ? APP_COLOR_VALUE : APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y);
        M5Cardputer.Display.print(line);
        y += 10;
    }

    const int status_y = hostsListStatusY();
    M5Cardputer.Display.fillRect(0, status_y, screen_w, 12, BLACK);
    char status[40];
    uint16_t status_color = APP_COLOR_HINT;
    formatHostsStatusLine(status, sizeof(status), &status_color);
    if (status[0] != '\0') {
        M5Cardputer.Display.setTextColor(status_color, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, status_y);
        M5Cardputer.Display.print(status);
    }
}

static void drawHostsUi() {
    beginAppScreenAccent("Keyboard ", "Hosts", APP_COLOR_LABEL);
    g_screen_ready = true;
    clearAppContentArea();
    redrawHostsListAndStatus();
    drawHostsHintBar();
}

static void drawHidKeyboardApp(const bool full_init) {
    if (g_hosts_ui) {
        drawHostsUi();
        return;
    }
    if (g_help_visible) {
        drawHelpPage();
        return;
    }

    if (full_init || !g_screen_ready) {
        beginAppScreenAccent("Keyboard ", transportName(g_transport), APP_COLOR_LABEL);
        g_screen_ready = true;
    } else {
        clearAppContentArea();
        drawAppScreenHeaderAccent("Keyboard ", transportName(g_transport), APP_COLOR_LABEL);
    }

    // 清屏后像素已丢，必须失效缓存，否则仍显示 paired 时会跳过绘制
    g_drawn_link_status[0] = '\0';
    drawLinkStatus();
    g_drawn_slot_num = -2;
    drawSlotBadge();
    g_drawn_echo[0] = '\0';
    g_drawn_label[0] = '\0';
    drawEchoOnly();
    drawHintBar();  // 内含 tip 上方 peer MAC
}

void enterHidKeyboardApp() {
    g_screen_ready = false;
    g_active = true;
    g_help_visible = false;
    g_hosts_ui = false;
    g_rename_ui = false;
    g_rename_buf[0] = '\0';
    g_help_page = 0;
    g_fn_h_latched = false;
    g_fn_caps_latched = false;
    g_hosts_key_latched = false;
    g_hosts_status[0] = '\0';
    g_auth_hint[0] = '\0';
    g_auth_fail_streak = 0;
    clearPendingConn();
    g_echo[0] = '\0';
    g_last_label[0] = '\0';
    g_drawn_echo[0] = '\0';
    g_drawn_label[0] = '\0';
    g_drawn_link_status[0] = '\0';
    clearBleReportQueue();
    // 默认 BLE，不占用烧录口；需要 USB 时再 Fn+u
    applyTransport(g_transport);
    drawHidKeyboardApp(true);
}

void leaveHidKeyboardApp() {
    if (!g_active) {
        return;
    }
    g_active = false;
    g_help_visible = false;
    g_hosts_ui = false;
    g_rename_ui = false;
    clearBleReportQueue();

    // deinit 可能数秒：先清内容区提示，避免界面假死无反馈
    clearAppContentArea();
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.drawCenterString("Exiting.", M5Cardputer.Display.width() / 2,
                                         APP_CONTENT_INSET_Y + 36);

    stopBleKeyboard();
    // 退出应用时务必把 USB 还给 JTAG，否则无法 upload
    stopUsbKeyboard();
}

void updateHidKeyboardApp() {
    if (!g_active) {
        return;
    }
    if (g_help_visible) {
        return;
    }
    if (g_hosts_ui) {
        // 切换 / 新配对成功：直接回输入界面
        if (g_ble_connected && g_hosts_exit_on_connect) {
            g_hosts_ui = false;
            g_hosts_exit_on_connect = false;
            g_rename_ui = false;
            g_rename_buf[0] = '\0';
            g_hosts_status[0] = '\0';
            drawHidKeyboardApp(true);
            return;
        }
        // 状态变化时只局部刷新列表，不全屏 beginAppScreen
        static bool last_hosts_conn = false;
        static bool last_hosts_pending = false;
        static int last_hosts_slot = -2;
        static int last_prefer_slot = -2;
        static int last_sel_slot = -2;
        static bool last_pairing_open = false;
        static char last_status[sizeof(g_hosts_status)] = "";
        if (g_ble_connected != last_hosts_conn || g_pending.active != last_hosts_pending ||
            g_active_slot != last_hosts_slot || g_prefer_slot != last_prefer_slot ||
            g_pairing_open != last_pairing_open || g_sel_slot != last_sel_slot ||
            strcmp(last_status, g_hosts_status) != 0) {
            last_hosts_conn = g_ble_connected;
            last_hosts_pending = g_pending.active;
            last_hosts_slot = g_active_slot;
            last_prefer_slot = g_prefer_slot;
            last_pairing_open = g_pairing_open;
            last_sel_slot = g_sel_slot;
            strncpy(last_status, g_hosts_status, sizeof(last_status) - 1);
            last_status[sizeof(last_status) - 1] = '\0';
            if (g_ble_connected) {
                g_hosts_status[0] = '\0';
                last_status[0] = '\0';
            } else if (g_pairing_open && g_hosts_status[0] == '\0') {
                snprintf(g_hosts_status, sizeof(g_hosts_status), "pair new...");
                strncpy(last_status, g_hosts_status, sizeof(last_status) - 1);
            }
            redrawHostsListAndStatus();
        }
        return;
    }
    if (g_transport == HidTransport::BLE) {
        drainBleReportQueue();
        // 新配对若一直无 AUTH：超时提示 forget，不误伤已配对槽
        if (g_pending.active && g_pending.is_new &&
            (millis() - g_pending.since_ms) > 10000) {
            handleAuthFailure(g_pending.bda);
        }
        drawSlotBadge();
        if (g_peer_addr[0] != '\0' || (g_active_slot >= 0 && g_hosts[g_active_slot].used)) {
            drawPeerLine();
        }
    }

    static bool last_connected = false;
    static bool last_pending = false;
    static HidTransport last_transport = HidTransport::BLE;
    static char last_status[sizeof(g_auth_hint)] = "";
    const bool connected =
        (g_transport == HidTransport::USB) ? g_usb_ready : g_ble_connected;
    const char* status = connectionStatusText();
    if (connected != last_connected || g_pending.active != last_pending ||
        g_transport != last_transport || strcmp(last_status, status) != 0) {
        last_connected = connected;
        last_pending = g_pending.active;
        last_transport = g_transport;
        strncpy(last_status, status, sizeof(last_status) - 1);
        last_status[sizeof(last_status) - 1] = '\0';
        if (g_transport == HidTransport::BLE && !g_ble_connected && !g_pending.active) {
            clearBleReportQueue();
            // pending 阶段保留 peer 显示；完全断开再清
            if (g_auth_hint[0] == '\0') {
                clearPeerInfo();
            }
        }
        g_drawn_link_status[0] = '\0';
        drawLinkStatus();
        g_drawn_slot_num = -2;
        drawSlotBadge();
    }
}

void handleHidKeyboardApp(const Keyboard_Class::KeysState& status) {
    if (!g_active) {
        return;
    }
    if (tryHandleModeHotkey(status)) {
        // 打开帮助时已自绘；模式切换需刷新主界面
        if (!g_help_visible) {
            drawHidKeyboardApp(false);
        }
        return;
    }

    if (!M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState empty{};
        sendHostReport(empty);
        if (g_transport == HidTransport::BLE) {
            drainBleReportQueue();
        }
        return;
    }

    sendHostReport(status);
    updateEchoBuffer(status);
    drawEchoOnly();
    if (g_transport == HidTransport::BLE) {
        drainBleReportQueue();
    }
}

bool pollHidKeyboardBtnAExit() {
    if (!g_active) {
        return false;
    }
    // 真正的 leave 交给 showMenu，避免这里拆栈后再 leave 一次
    return M5Cardputer.BtnA.wasPressed();
}
