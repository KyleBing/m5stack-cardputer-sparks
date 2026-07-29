#include "app_hid_keyboard.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_connectivity.h"
#include "app_header.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <HIDTypes.h>
#include <Preferences.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
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
static constexpr size_t kEchoMaxChars = 3;  // 只保留最近 3 个按键
static constexpr int kEchoCellW = 18;       // size3 单字符格
static constexpr int kEchoCellH = 24;
static constexpr size_t kBleReportQueueCap = 48;
static constexpr uint32_t kBleReportIntervalMs = 12;
static constexpr uint32_t kFnLongPressMs = 650;  // 长按 Fn 切换 IMU
static constexpr int kModColW = 56;              // 左侧修饰键胶囊列宽（x2 字体）
static constexpr int kModMarginX = 5;            // 左侧特殊键距屏左边界
static constexpr int kModMarginY = 5;            // 左侧特殊键距上下边界
static constexpr int kModCount = 5;              // Fn Aa Opt Ctrl Alt（IMU 由鼠标区表示）
static constexpr int kKbTopY = 2;                // 主界面无 header，内容顶边
static constexpr int kFooterH = 12;              // 底栏（x1 匹配信息）
static constexpr int kSensBarW = 20;             // 灵敏度块宽
static constexpr int kSensSegH = 5;              // 灵敏度块高
static constexpr int kSensSegGap = 2;
static constexpr int kSensMargin = 10;           // 灵敏度距右缘
static constexpr int kImuSensMin = 1;
static constexpr int kImuSensMax = 10;
static constexpr int kImuSensDefault = 5;

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
// Hosts 卡片：左侧单列 5 槽；右侧竖排 tip
static constexpr int kHostCardW = 128;
static constexpr int kHostCardH = 18;
static constexpr int kHostCardGapY = 3;
static constexpr int kHostCardOriginX = 4;
static constexpr int kHostCardOriginY = 3;
static constexpr int kHostTipsX = 140;

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
static bool g_exiting = false;  // leave 期间仍禁止刷 header，避免蓝牙图标闪一下
static bool g_help_visible = false;
static bool g_hosts_ui = false;  // BLE 主机列表（切换 / 配对）
static bool g_hosts_exit_on_connect = false;  // 切换/新配对成功后自动回输入界面
static bool g_rename_ui = false;  // 主机列表内重命名别名
static char g_rename_buf[kHostAliasMax + 1] = "";
static int g_help_page = 0;
static bool g_fn_h_latched = false;
static bool g_fn_caps_latched = false;
static bool g_hosts_key_latched = false;
static bool g_imu_mouse_on = false;  // IMU 姿态映射主机鼠标指针
static bool g_imu_ok = false;
static int g_imu_sens = kImuSensDefault;  // 1..10，数字键调节
static uint8_t g_mouse_buttons = 0;       // 当前鼠标按键位
static int8_t g_imu_dx = 0;               // 最近一次发送的相对位移
static int8_t g_imu_dy = 0;
static int g_drawn_imu_sens = -1;
static uint8_t g_drawn_imu_buttons = 0xFF;
static bool g_imu_pad_drawn = false;  // IMU 鼠标区已画底板
static uint32_t g_fn_down_ms = 0;     // Fn 按下计时（长按切 IMU）
static bool g_fn_long_fired = false;
static bool g_fn_long_cancelled = false;  // Fn+其它键则取消长按
static bool g_drawn_mod_fn = false;
static bool g_drawn_mod_shift = false;
static bool g_drawn_mod_opt = false;
static bool g_drawn_mod_ctrl = false;
static bool g_drawn_mod_alt = false;
static bool g_mods_drawn = false;
static bool g_echo_area_ready = false;  // 键盘回显区已初始化
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
// 右下角配对条缓存（状态 / 设备名 / 槽号）
static char g_drawn_footer_status[24] = "";
static char g_drawn_footer_name[20] = "";
static int g_drawn_footer_slot = -2;
static int g_drawn_slot_num = -2;  // Hosts 列表仍可能用槽号徽章
static char g_drawn_link_status[24] = "";  // Hosts 等旧状态缓存（主界面不再用）
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
static USBHIDMouse g_usb_mouse;
static usb_phy_handle_t g_otg_phy = nullptr;
static BLEHIDDevice* g_hid = nullptr;
static BLECharacteristic* g_kb_input = nullptr;
static BLECharacteristic* g_mouse_input = nullptr;  // report id 2
static BLEServer* g_ble_server = nullptr;

struct BleReport {
    uint8_t data[8];
    uint8_t len;   // 8=键盘，4=鼠标
    uint8_t kind;  // 0=kb，1=mouse
};
static BleReport g_ble_q[kBleReportQueueCap];
static size_t g_ble_q_head = 0;
static size_t g_ble_q_tail = 0;
static size_t g_ble_q_count = 0;
static uint32_t g_ble_last_send_ms = 0;
static KeyReport g_last_kb_report{};  // 上次已发给主机的键盘报告（防漏松键）
static bool g_last_kb_valid = false;

static void clearBleReportQueue();
static void applyTransport(HidTransport next);
static void stopUsbKeyboard();
static void startUsbKeyboard();
static void stopBleKeyboard();
static void startBleKeyboard();
static void disconnectBleClients();
static void releaseMouseButtons();
static void sendMouseReport(int8_t dx, int8_t dy, uint8_t buttons);
static void releaseAllToHost();
static void syncKeysToHost();
static void drainBleReportQueueBurst();
static void buildKeyReport(const Keyboard_Class::KeysState& status, KeyReport& report);
static void sendKeyReportToHost(const KeyReport& report);
static void drawEchoOnly();
static void drawModStateCol(const Keyboard_Class::KeysState& status, bool force);
static void drawImuHud(bool force);
static void drawPairFooter(bool force);
static void refreshRightPanel(bool force);
static void enableBleHidNotifications();
static void formatHostDisplayName(int slot, char* out, size_t out_len);

// Report 1 = Keyboard；Report 2 = Mouse（IMU 指针 / z·x 点击）
static const uint8_t kHidReportMap[] = {
    // Keyboard
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
    0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    // Mouse
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09,
    0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
};

static void clearPeerInfo() {
    g_peer_addr[0] = '\0';
    // 迫使底栏重绘设备名
    g_drawn_footer_name[0] = '\0';
    g_drawn_footer_slot = -2;
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

static void enableBleHidNotifications() {
    // 部分主机只订了键盘 CCCD；主动打开键/鼠通知，避免 IMU 鼠标无输出
    auto enable_one = [](BLECharacteristic* ch) {
        if (ch == nullptr) {
            return;
        }
        BLE2902* cccd = (BLE2902*)ch->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
        if (cccd != nullptr) {
            cccd->setNotifications(true);
        }
    };
    enable_one(g_kb_input);
    enable_one(g_mouse_input);
}

static void commitPendingConnection() {
    if (!g_pending.active) {
        g_ble_connected = true;
        enableBleHidNotifications();
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
    enableBleHidNotifications();
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
    // 裸按 ` → Esc（无需 Fn）
    for (const uint8_t raw : status.hid_keys) {
        if ((raw & 0x7F) == 0x35) {
            strncpy(g_last_label, "ESC", sizeof(g_last_label) - 1);
            return;
        }
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
        g_usb_mouse.begin();
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
        g_usb_mouse.release(MOUSE_ALL);
        g_mouse_buttons = 0;
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
    g_mouse_input = nullptr;
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
    g_mouse_input = g_hid->inputReport(2);
    // Report Reference 需可无加密读取，否则部分主机枚举不到鼠标报告
    if (g_mouse_input != nullptr) {
        BLEDescriptor* ref = g_mouse_input->getDescriptorByUUID(BLEUUID((uint16_t)0x2908));
        if (ref != nullptr) {
            ref->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
        }
    }
    if (g_kb_input != nullptr) {
        BLEDescriptor* ref = g_kb_input->getDescriptorByUUID(BLEUUID((uint16_t)0x2908));
        if (ref != nullptr) {
            ref->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
        }
    }
    g_hid->manufacturer()->setValue("M5Stack");
    // 版本 bump：主机需重新配对才能吃到键盘+鼠标复合报告
    g_hid->pnp(0x02, 0x1234, 0x5679, 0x0200);
    g_hid->hidInfo(0x00, 0x01);
    g_hid->reportMap(const_cast<uint8_t*>(kHidReportMap), sizeof(kHidReportMap));
    g_hid->startServices();

    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* advertising = g_ble_server->getAdvertising();
    advertising->setAppearance(GENERIC_HID);  // 复合键鼠
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
    releaseAllToHost();
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
    // 切换前抬起全部按键，避免旧链路卡键
    releaseAllToHost();
    g_transport = next;
    g_last_kb_valid = false;
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
static void drawHostsTips();
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
    drawHostsTips();
}

static void cancelRenameHostSlot() {
    g_rename_ui = false;
    g_rename_buf[0] = '\0';
    g_hosts_status[0] = '\0';
    redrawHostsListAndStatus();
    drawHostsTips();
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
    drawHostsTips();
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
                releaseAllToHost();
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

static bool bleReportIsKbRelease(const BleReport& r) {
    if (r.kind != 0) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (r.data[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool keyReportsEqual(const KeyReport& a, const KeyReport& b) {
    return a.modifiers == b.modifiers && memcmp(a.keys, b.keys, sizeof(a.keys)) == 0;
}

static void enqueueBleReport(const KeyReport& report) {
    BleReport item{};
    item.kind = 0;
    item.len = 8;
    item.data[0] = report.modifiers;
    item.data[1] = 0;
    for (int i = 0; i < 6; i++) {
        item.data[2 + i] = report.keys[i];
    }
    if (g_ble_q_count > 0) {
        const size_t last = (g_ble_q_tail + kBleReportQueueCap - 1) % kBleReportQueueCap;
        if (g_ble_q[last].kind == 0 && memcmp(g_ble_q[last].data, item.data, 8) == 0) {
            return;
        }
    }
    if (g_ble_q_count >= kBleReportQueueCap) {
        // 松开报告绝不能丢：队列满时清空后只保留本次释放
        if (bleReportIsKbRelease(item)) {
            clearBleReportQueue();
        } else {
            // 优先丢掉最旧的鼠标移动，避免冲掉键盘松开
            bool dropped = false;
            for (size_t n = 0; n < g_ble_q_count; n++) {
                const size_t i = (g_ble_q_head + n) % kBleReportQueueCap;
                if (g_ble_q[i].kind == 1) {
                    // 压缩：删掉该槽，后续前移（简单做法：丢弃队头直到非鼠标或清空一个鼠标）
                    if (i == g_ble_q_head) {
                        g_ble_q_head = (g_ble_q_head + 1) % kBleReportQueueCap;
                        g_ble_q_count--;
                        dropped = true;
                    }
                    break;
                }
            }
            if (!dropped) {
                g_ble_q_head = (g_ble_q_head + 1) % kBleReportQueueCap;
                g_ble_q_count--;
            }
        }
    }
    g_ble_q[g_ble_q_tail] = item;
    g_ble_q_tail = (g_ble_q_tail + 1) % kBleReportQueueCap;
    g_ble_q_count++;
}

static void enqueueBleMouseReport(const uint8_t buttons, const int8_t dx, const int8_t dy) {
    BleReport item{};
    item.kind = 1;
    item.len = 4;
    item.data[0] = buttons;
    item.data[1] = static_cast<uint8_t>(dx);
    item.data[2] = static_cast<uint8_t>(dy);
    item.data[3] = 0;
    if (g_ble_q_count > 0) {
        const size_t last = (g_ble_q_tail + kBleReportQueueCap - 1) % kBleReportQueueCap;
        if (g_ble_q[last].kind == 1 && g_ble_q[last].data[0] == buttons) {
            g_ble_q[last].data[1] = item.data[1];
            g_ble_q[last].data[2] = item.data[2];
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

static void notifyBleRaw(BLECharacteristic* ch, uint8_t* data, const size_t len) {
    if (ch == nullptr || !g_ble_connected) {
        return;
    }
    ch->setValue(data, len);
    ch->notify();
}

static void drainBleReportQueue() {
    if (!g_ble_ready || !g_ble_connected || g_ble_q_count == 0) {
        return;
    }
    const uint32_t now = millis();
    const BleReport& peek = g_ble_q[g_ble_q_head];
    // 松开报告立刻发，不受 12ms 节流（否则连按易丢松键）
    const bool urgent = bleReportIsKbRelease(peek) ||
                        (peek.kind == 1 && peek.data[0] == 0 && peek.data[1] == 0 && peek.data[2] == 0);
    if (!urgent && now - g_ble_last_send_ms < kBleReportIntervalMs) {
        return;
    }
    const BleReport& item = g_ble_q[g_ble_q_head];
    if (item.kind == 0) {
        notifyBleRaw(g_kb_input, const_cast<uint8_t*>(item.data), item.len);
    } else {
        notifyBleRaw(g_mouse_input, const_cast<uint8_t*>(item.data), item.len);
    }
    g_ble_q_head = (g_ble_q_head + 1) % kBleReportQueueCap;
    g_ble_q_count--;
    g_ble_last_send_ms = now;
}

// 一帧尽量多排空（松开优先已在 drain 内）
static void drainBleReportQueueBurst() {
    for (int i = 0; i < 6; i++) {
        const size_t before = g_ble_q_count;
        drainBleReportQueue();
        if (g_ble_q_count == before) {
            break;
        }
    }
}

static void sendKeyReportToHost(const KeyReport& report) {
    const bool empty = report.modifiers == 0 && report.keys[0] == 0;
    if (g_transport == HidTransport::USB) {
        if (!g_usb_ready) {
            return;
        }
        if (empty) {
            g_usb_kb.releaseAll();
        } else {
            KeyReport tmp = report;
            g_usb_kb.sendReport(&tmp);
        }
        g_last_kb_report = report;
        g_last_kb_valid = true;
        return;
    }
    if (!g_ble_ready || !g_ble_connected) {
        return;
    }
    enqueueBleReport(report);
    g_last_kb_report = report;
    g_last_kb_valid = true;
    // 松开立刻尝试发出
    if (empty) {
        drainBleReportQueueBurst();
    }
}

// 每帧对照当前物理键位，漏掉的松开也会补发给主机
static void syncKeysToHost() {
    if (!g_active || g_help_visible || g_hosts_ui || g_rename_ui) {
        return;
    }
    Keyboard_Class::KeysState status{};
    if (M5Cardputer.Keyboard.isPressed()) {
        status = M5Cardputer.Keyboard.keysState();
    }
    KeyReport report{};
    buildKeyReport(status, report);
    if (g_last_kb_valid && keyReportsEqual(report, g_last_kb_report)) {
        return;
    }
    sendKeyReportToHost(report);
}

// 退出 / 切传输前：强制全键抬起，避免主机卡键
static void releaseAllToHost() {
    KeyReport empty{};
    g_last_kb_valid = false;  // 强制再发一次空报告
    if (g_transport == HidTransport::USB && g_usb_ready) {
        g_usb_kb.releaseAll();
        g_usb_mouse.release(MOUSE_ALL);
        g_mouse_buttons = 0;
        delay(20);
        g_usb_kb.releaseAll();
        g_last_kb_report = empty;
        g_last_kb_valid = true;
        return;
    }
    if (g_transport == HidTransport::BLE && g_ble_ready && g_ble_connected) {
        clearBleReportQueue();
        uint8_t kb_z[8] = {};
        uint8_t ms_z[4] = {};
        notifyBleRaw(g_kb_input, kb_z, sizeof(kb_z));
        notifyBleRaw(g_mouse_input, ms_z, sizeof(ms_z));
        delay(20);
        notifyBleRaw(g_kb_input, kb_z, sizeof(kb_z));
        notifyBleRaw(g_mouse_input, ms_z, sizeof(ms_z));
        delay(15);
        g_mouse_buttons = 0;
        g_last_kb_report = empty;
        g_last_kb_valid = true;
    }
}

// 发送鼠标相对移动 / 按键（USB 或 BLE）
static void sendMouseReport(const int8_t dx, const int8_t dy, const uint8_t buttons) {
    g_imu_dx = dx;
    g_imu_dy = dy;
    if (g_transport == HidTransport::USB) {
        if (!g_usb_ready) {
            return;
        }
        if (buttons != g_mouse_buttons) {
            const uint8_t released = static_cast<uint8_t>(g_mouse_buttons & ~buttons);
            const uint8_t pressed = static_cast<uint8_t>(buttons & ~g_mouse_buttons);
            if (released != 0) {
                g_usb_mouse.release(released);
            }
            if (pressed != 0) {
                g_usb_mouse.press(pressed);
            }
            g_mouse_buttons = buttons;
        }
        if (dx != 0 || dy != 0) {
            g_usb_mouse.move(dx, dy);
        }
        return;
    }
    if (!g_ble_ready || g_mouse_input == nullptr || !g_ble_connected) {
        return;
    }
    if (dx == 0 && dy == 0 && buttons == g_mouse_buttons) {
        return;
    }
    g_mouse_buttons = buttons;
    enqueueBleMouseReport(buttons, dx, dy);
}

static void releaseMouseButtons() {
    if (g_mouse_buttons == 0) {
        return;
    }
    sendMouseReport(0, 0, 0);
    g_mouse_buttons = 0;
}

static int8_t clampMouseDelta(const float v) {
    if (v > 127.0f) {
        return 127;
    }
    if (v < -127.0f) {
        return -127;
    }
    return static_cast<int8_t>(v);
}

// IMU 陀螺仪 → 相对鼠标；死区抑制漂移；灵敏度 1..10
static void pollImuMousePointer() {
    if (!g_imu_mouse_on || !g_imu_ok) {
        return;
    }
    M5.Imu.update();
    const auto data = M5.Imu.getImuData();
    const float gx = data.gyro.x;
    const float gy = data.gyro.y;
    const float gz = data.gyro.z;
    const float sens = static_cast<float>(g_imu_sens);
    const float gyro_mag = gx * gx + gy * gy + gz * gz;
    int8_t dx = 0;
    int8_t dy = 0;
    if (gyro_mag < 0.01f) {
        constexpr float kTiltDead = 0.08f;
        const float kTiltScale = 3.0f * sens;
        float mx = data.accel.y;
        float my = -data.accel.x;
        if (mx > -kTiltDead && mx < kTiltDead) {
            mx = 0.0f;
        }
        if (my > -kTiltDead && my < kTiltDead) {
            my = 0.0f;
        }
        dx = clampMouseDelta(mx * kTiltScale);
        dy = clampMouseDelta(my * kTiltScale);
    } else {
        constexpr float kGyroDead = 3.0f;
        const float kGyroScale = 0.10f * sens;
        float mx = -gz;
        float my = -gx;
        (void)gy;
        if (mx > -kGyroDead && mx < kGyroDead) {
            mx = 0.0f;
        }
        if (my > -kGyroDead && my < kGyroDead) {
            my = 0.0f;
        }
        dx = clampMouseDelta(mx * kGyroScale);
        dy = clampMouseDelta(my * kGyroScale);
    }
    g_imu_dx = dx;
    g_imu_dy = dy;
    if (dx != 0 || dy != 0 || g_mouse_buttons != 0) {
        sendMouseReport(dx, dy, g_mouse_buttons);
    }
}

// 字母键左右半区：左=LMB，右=RMB（分界 ygv|uhb）
static bool isLeftHalfLetter(const char c) {
    switch (c) {
        case 'q': case 'w': case 'e': case 'r': case 't': case 'y':
        case 'a': case 's': case 'd': case 'f': case 'g':
        case 'z': case 'x': case 'c': case 'v':
        case 'Q': case 'W': case 'E': case 'R': case 'T': case 'Y':
        case 'A': case 'S': case 'D': case 'F': case 'G':
        case 'Z': case 'X': case 'C': case 'V':
            return true;
        default:
            return false;
    }
}

static bool isRightHalfLetter(const char c) {
    switch (c) {
        case 'u': case 'i': case 'o': case 'p':
        case 'h': case 'j': case 'k': case 'l':
        case 'b': case 'n': case 'm':
        case 'U': case 'I': case 'O': case 'P':
        case 'H': case 'J': case 'K': case 'L':
        case 'B': case 'N': case 'M':
            return true;
        default:
            return false;
    }
}

static uint8_t mouseButtonsFromKeys(const Keyboard_Class::KeysState& status) {
    if (!g_imu_mouse_on) {
        return 0;
    }
    uint8_t buttons = 0;
    for (const char c : status.word) {
        if (isLeftHalfLetter(c)) {
            buttons |= MOUSE_LEFT;
        }
        if (isRightHalfLetter(c)) {
            buttons |= MOUSE_RIGHT;
        }
    }
    return buttons;
}

// 数字 1-0 → 灵敏度 1..10（Fn 按下时留给 F 键，不抢灵敏度）
static bool tryHandleImuSensKey(const Keyboard_Class::KeysState& status) {
    if (!g_imu_mouse_on || status.fn) {
        return false;
    }
    for (const char c : status.word) {
        if (c >= '1' && c <= '9') {
            g_imu_sens = c - '0';
            return true;
        }
        if (c == '0') {
            g_imu_sens = 10;
            return true;
        }
    }
    return false;
}

static void toggleImuMouse() {
    if (!g_imu_ok) {
        g_imu_mouse_on = false;
        strncpy(g_last_label, "NO IMU", sizeof(g_last_label) - 1);
    } else {
        g_imu_mouse_on = !g_imu_mouse_on;
        if (g_imu_mouse_on) {
            strncpy(g_last_label,
                    (g_transport == HidTransport::BLE) ? "IMU ON*" : "IMU ON",
                    sizeof(g_last_label) - 1);
            // 抬起当前字母键，避免进 IMU 时卡键；非字母键仍可发
            KeyReport empty{};
            sendKeyReportToHost(empty);
        } else {
            releaseMouseButtons();
            g_imu_dx = 0;
            g_imu_dy = 0;
            strncpy(g_last_label, "IMU OFF", sizeof(g_last_label) - 1);
        }
    }
    g_last_label[sizeof(g_last_label) - 1] = '\0';
    g_mods_drawn = false;
    g_echo[0] = '\0';
    // 切 IMU 时只换右侧面板，不全屏闪
    Keyboard_Class::KeysState empty{};
    drawModStateCol(empty, true);
    refreshRightPanel(true);
}

// 每帧轮询：长按 Fn 切换 IMU（Fn+其它键则取消）
static void pollFnLongPressToggle() {
    const auto status = M5Cardputer.Keyboard.keysState();
    if (!status.fn) {
        g_fn_down_ms = 0;
        g_fn_long_fired = false;
        g_fn_long_cancelled = false;
        return;
    }
    const bool alone = status.hid_keys.empty() && status.word.empty() && !status.ctrl &&
                       !status.shift && !status.alt && !status.opt && !status.del &&
                       !status.enter && !status.space && !status.tab;
    if (!alone) {
        g_fn_long_cancelled = true;
        g_fn_down_ms = 0;
        return;
    }
    if (g_fn_long_cancelled || g_fn_long_fired) {
        return;
    }
    if (g_fn_down_ms == 0) {
        g_fn_down_ms = millis();
        return;
    }
    if (millis() - g_fn_down_ms >= kFnLongPressMs) {
        g_fn_long_fired = true;
        toggleImuMouse();
    }
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

static bool isLetterHid(const uint8_t hid) {
    return hid >= 0x04 && hid <= 0x1D;  // a..z
}

static bool isDigitHid(const uint8_t hid) {
    return hid >= 0x1E && hid <= 0x27;  // 1..0
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
    // IMU 开启时仍发非字母键；字母留给鼠标点击，数字留给灵敏度
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
        uint8_t hid = raw & 0x7F;
        // 裸按 ` → Esc（本 App 用 BtnGO 退出，不占 Esc）
        if (hid == 0x35) {
            hid = 0x29;
        }
        if (g_imu_mouse_on) {
            if (isLetterHid(hid) || isDigitHid(hid)) {
                continue;
            }
        }
        pushHidKey(report, idx, hid);
    }
    if (status.space) {
        pushHidKey(report, idx, 0x2C);
    }
}

static void sendHostReport(const Keyboard_Class::KeysState& status) {
    KeyReport report{};
    buildKeyReport(status, report);
    sendKeyReportToHost(report);
}

static int contentBottomY() {
    return M5Cardputer.Display.height() - kFooterH;
}

static int rightPanelX() {
    return kModMarginX + kModColW + 2;
}

// 中间内容区；IMU 时右侧留给灵敏度条（含边距）
static void centerPanelGeom(int& x, int& y, int& w, int& h) {
    x = rightPanelX();
    y = kKbTopY;
    const int sens_reserve = g_imu_mouse_on ? (kSensBarW + kSensMargin + 2) : 0;
    w = M5Cardputer.Display.width() - x - 2 - sens_reserve;
    h = contentBottomY() - y;
}

static void rightPanelGeom(int& x, int& y, int& w, int& h) {
    centerPanelGeom(x, y, w, h);
}

// 左侧特殊键：左右/上下均留边距后平分高度（配对信息只占右侧剩余宽度）
static int modAreaTop() {
    return kModMarginY;
}

static int modAreaH() {
    return M5Cardputer.Display.height() - kModMarginY * 2;
}

static int modSlotY(const int index) {
    return modAreaTop() + index * modAreaH() / kModCount;
}

static int modSlotH(const int index) {
    return modAreaTop() + (index + 1) * modAreaH() / kModCount - modSlotY(index);
}

// 左侧修饰键：平分整屏高度，间隔 3px；距左边界 5px；按下显示各键专属底色
static void drawModPillAt(const int x, const int y, const int slot_h, const char* label,
                          const bool active, const uint16_t active_color) {
    constexpr int gap = 3;
    const int w = kModColW;
    const int h = slot_h - gap;
    M5Cardputer.Display.fillRect(x, y, kModColW, slot_h, BLACK);
    const uint16_t bg = active ? active_color : 0x4208;
    const uint16_t fg = active ? BLACK : WHITE;
    M5Cardputer.Display.fillRoundRect(x, y + gap / 2, w, h, h / 2, bg);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(fg, bg);
    const int tw = M5Cardputer.Display.textWidth(label);
    M5Cardputer.Display.setCursor(x + (w - tw) / 2, y + gap / 2 + (h - 16) / 2);
    M5Cardputer.Display.print(label);
}

static void drawOneModIfChanged(const int index, const char* label, const bool active, bool& cache,
                                const uint16_t active_color, const bool force) {
    if (!force && g_mods_drawn && cache == active) {
        return;
    }
    cache = active;
    drawModPillAt(kModMarginX, modSlotY(index), modSlotH(index), label, active, active_color);
}

// 左侧列：Fn 橙 / Aa 蓝 / Opt 绿 / Ctrl·Alt 白
static void drawModStateCol(const Keyboard_Class::KeysState& status, const bool force) {
    drawOneModIfChanged(0, "Fn", status.fn, g_drawn_mod_fn, ORANGE, force);
    drawOneModIfChanged(1, "Aa", status.shift, g_drawn_mod_shift, CYAN, force);
    drawOneModIfChanged(2, "Opt", status.opt, g_drawn_mod_opt, GREEN, force);
    drawOneModIfChanged(3, "Ctrl", status.ctrl, g_drawn_mod_ctrl, WHITE, force);
    drawOneModIfChanged(4, "Alt", status.alt, g_drawn_mod_alt, WHITE, force);
    g_mods_drawn = true;
}

// 彩色文本徽章（样式同 drawTextBadge），返回占用宽度（含右侧间距）
static int drawColoredTextBadge(const int x, const int y, const char* label, const uint16_t bg) {
    if (label == nullptr || label[0] == '\0') {
        return 0;
    }
    M5Cardputer.Display.setTextSize(1);
    const int tw = M5Cardputer.Display.textWidth(label);
    constexpr int pad_x = 2;
    constexpr int pad_y = 1;
    const int bw = tw + pad_x * 2;
    const int bh = 8 + pad_y * 2;
    M5Cardputer.Display.fillRoundRect(x, y, bw, bh, 2, bg);
    M5Cardputer.Display.setTextColor(BLACK, bg);
    M5Cardputer.Display.setCursor(x + pad_x, y + pad_y);
    M5Cardputer.Display.print(label);
    return bw + 3;
}

// 底栏右下角：设备名(x1) + 状态徽章（已连接绿 #N / 连接中黄）
static void drawPairFooter(const bool force) {
    char badge[20] = "";
    char name[20] = "";
    uint16_t badge_bg = YELLOW;
    int slot = -1;

    if (g_transport == HidTransport::USB) {
        if (g_usb_ready) {
            strncpy(badge, "USB", sizeof(badge) - 1);
            badge_bg = APP_COLOR_OK;
        } else {
            strncpy(badge, "Init", sizeof(badge) - 1);
            badge_bg = YELLOW;
        }
    } else {
        if (g_active_slot >= 0 && g_hosts[g_active_slot].used) {
            slot = g_active_slot + 1;
            formatHostDisplayName(g_active_slot, name, sizeof(name));
        } else if (g_peer_addr[0] != '\0') {
            strncpy(name, g_peer_addr, sizeof(name) - 1);
        }
        if (g_ble_connected) {
            // 已连接：绿色 #N
            if (slot >= 1) {
                snprintf(badge, sizeof(badge), "#%d", slot);
            } else {
                strncpy(badge, "OK", sizeof(badge) - 1);
            }
            badge_bg = APP_COLOR_OK;
        } else {
            // 连接中 / 配对中：黄色徽章
            const char* st = connectionStatusText();
            if (strcmp(st, "connecting") == 0 || strcmp(st, "reconnecting") == 0) {
                strncpy(badge, "...", sizeof(badge) - 1);
            } else if (strcmp(st, "pair new") == 0) {
                strncpy(badge, "New", sizeof(badge) - 1);
            } else if (strcmp(st, "pairing...") == 0) {
                strncpy(badge, "Pair", sizeof(badge) - 1);
            } else if (st[0] != '\0') {
                strncpy(badge, st, sizeof(badge) - 1);
                badge[sizeof(badge) - 1] = '\0';
                if (strlen(badge) > 10) {
                    badge[10] = '\0';
                }
            } else {
                strncpy(badge, "...", sizeof(badge) - 1);
            }
            badge_bg = YELLOW;
        }
    }

    if (!force && strcmp(g_drawn_footer_status, badge) == 0 &&
        strcmp(g_drawn_footer_name, name) == 0 && g_drawn_footer_slot == slot) {
        return;
    }

    const int y = contentBottomY();
    const int screen_w = M5Cardputer.Display.width();
    const int foot_x = rightPanelX();
    // 配对信息只占特殊键列右侧剩余宽度
    M5Cardputer.Display.fillRect(foot_x, y, screen_w - foot_x, kFooterH, BLACK);

    M5Cardputer.Display.setTextSize(1);
    const int badge_inner =
        (badge[0] != '\0') ? (M5Cardputer.Display.textWidth(badge) + 4) : 0;
    const int bx = screen_w - 2 - badge_inner;
    if (badge[0] != '\0') {
        drawColoredTextBadge(bx, y + 1, badge, badge_bg);
    }
    if (name[0] != '\0') {
        // 匹配信息：设备名 x1，徽章左侧（不侵入左侧特殊键）
        const int tw = M5Cardputer.Display.textWidth(name);
        int nx = bx - 4 - tw;
        if (nx < foot_x) {
            nx = foot_x;
        }
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(nx, y + 2);
        M5Cardputer.Display.print(name);
    }

    strncpy(g_drawn_footer_status, badge, sizeof(g_drawn_footer_status) - 1);
    g_drawn_footer_status[sizeof(g_drawn_footer_status) - 1] = '\0';
    strncpy(g_drawn_footer_name, name, sizeof(g_drawn_footer_name) - 1);
    g_drawn_footer_name[sizeof(g_drawn_footer_name) - 1] = '\0';
    g_drawn_footer_slot = slot;
}

static void clearRightPanel() {
    int x, y, w, h;
    x = rightPanelX();
    y = kKbTopY;
    w = M5Cardputer.Display.width() - x;
    h = contentBottomY() - y;
    M5Cardputer.Display.fillRect(x, y, w, h, BLACK);
    g_imu_pad_drawn = false;
    g_echo_area_ready = false;
    g_drawn_echo[0] = '\0';
    g_drawn_label[0] = '\0';
    g_drawn_imu_sens = -1;
    g_drawn_imu_buttons = 0xFF;
}

// 仅左上角圆角
static void fillRectRoundTL(const int x, const int y, const int w, const int h, const int r,
                            const uint16_t c) {
    int rr = r;
    if (rr * 2 > w) {
        rr = w / 2;
    }
    if (rr * 2 > h) {
        rr = h / 2;
    }
    M5Cardputer.Display.fillRect(x + rr, y, w - rr, h, c);
    M5Cardputer.Display.fillRect(x, y + rr, rr, h - rr, c);
    M5Cardputer.Display.fillCircle(x + rr, y + rr, rr, c);
}

// 仅右上角圆角
static void fillRectRoundTR(const int x, const int y, const int w, const int h, const int r,
                            const uint16_t c) {
    int rr = r;
    if (rr * 2 > w) {
        rr = w / 2;
    }
    if (rr * 2 > h) {
        rr = h / 2;
    }
    M5Cardputer.Display.fillRect(x, y, w - rr, h, c);
    M5Cardputer.Display.fillRect(x + w - rr, y + rr, rr, h - rr, c);
    M5Cardputer.Display.fillCircle(x + w - 1 - rr, y + rr, rr, c);
}

// 仅左右下角圆角（机身）
static void fillRectRoundBLBR(const int x, const int y, const int w, const int h, const int r,
                              const uint16_t c) {
    int rr = r;
    if (rr * 2 > w) {
        rr = w / 2;
    }
    if (rr > h) {
        rr = h;
    }
    M5Cardputer.Display.fillRect(x, y, w, h - rr, c);
    M5Cardputer.Display.fillRect(x + rr, y + h - rr, w - 2 * rr, rr, c);
    M5Cardputer.Display.fillCircle(x + rr, y + h - 1 - rr, rr, c);
    M5Cardputer.Display.fillCircle(x + w - 1 - rr, y + h - 1 - rr, rr, c);
}

// 鼠标：上排左右键色块 + 下机身，间隔 2px；点击时整块变色
static void drawMouseIcon(const int cx, const int cy, const uint8_t buttons) {
    constexpr int mw = 56;
    constexpr int btn_h = 22;
    constexpr int body_h = 40;
    constexpr int gap = 2;  // 键与机身 / 左右键间隔
    constexpr int r_btn = 5;
    constexpr int r_body = 10;
    const int total_h = btn_h + gap + body_h;
    const int x = cx - mw / 2;
    const int y = cy - total_h / 2;
    const int btn_w = (mw - gap) / 2;
    const uint16_t idle = 0x6B4D;
    const uint16_t body = 0x8410;
    const uint16_t lit = YELLOW;

    // 左键：仅左上 5px 圆角
    fillRectRoundTL(x, y, btn_w, btn_h, r_btn, (buttons & MOUSE_LEFT) ? lit : idle);
    // 右键：仅右上 5px 圆角
    fillRectRoundTR(x + btn_w + gap, y, btn_w, btn_h, r_btn,
                    (buttons & MOUSE_RIGHT) ? lit : idle);
    // 机身：仅左右下角 10px 圆角
    fillRectRoundBLBR(x, y + btn_h + gap, mw, body_h, r_body, body);
}

// 右侧灵敏度：固定 20x5 直角块，距右缘 10px
static void drawSensBar(const bool force) {
    if (!g_imu_mouse_on) {
        return;
    }
    if (!force && g_drawn_imu_sens == g_imu_sens) {
        return;
    }
    const int screen_w = M5Cardputer.Display.width();
    const int bar_x = screen_w - kSensBarW - kSensMargin;
    const int total_h = kImuSensMax * kSensSegH + (kImuSensMax - 1) * kSensSegGap;
    const int top = kKbTopY + (contentBottomY() - kKbTopY - total_h) / 2;
    M5Cardputer.Display.fillRect(bar_x - 1, kKbTopY, kSensBarW + 2, contentBottomY() - kKbTopY,
                                 BLACK);
    for (int i = 0; i < kImuSensMax; i++) {
        const int level = kImuSensMax - i;
        const int sy = top + i * (kSensSegH + kSensSegGap);
        const bool on = level <= g_imu_sens;
        M5Cardputer.Display.fillRect(bar_x, sy, kSensBarW, kSensSegH, on ? YELLOW : DARKGREY);
    }
    g_drawn_imu_sens = g_imu_sens;
}

// IMU：居中鼠标 + 右侧灵敏度；仅 IMU 时显示
static void drawImuHud(const bool force) {
    if (!g_imu_mouse_on) {
        return;
    }
    int x, y, w, h;
    centerPanelGeom(x, y, w, h);

    if (force || !g_imu_pad_drawn) {
        M5Cardputer.Display.fillRect(x, y, w, h, BLACK);
        drawMouseIcon(x + w / 2, y + h / 2, g_mouse_buttons);
        drawSensBar(true);
        g_imu_pad_drawn = true;
        g_drawn_imu_buttons = g_mouse_buttons;
        g_echo_area_ready = false;
        return;
    }
    if (g_mouse_buttons != g_drawn_imu_buttons) {
        // 只重画鼠标区，避免闪灵敏度条
        M5Cardputer.Display.fillRect(x, y, w, h, BLACK);
        drawMouseIcon(x + w / 2, y + h / 2, g_mouse_buttons);
        g_drawn_imu_buttons = g_mouse_buttons;
    }
    drawSensBar(false);
}

// 键盘回显：最近 3 个按键；特殊键用标签
static void drawEchoOnly() {
    if (g_imu_mouse_on) {
        return;
    }
    int px, py, pw, ph;
    centerPanelGeom(px, py, pw, ph);

    const bool label_mode = (g_echo[0] == '\0' && g_last_label[0] != '\0');
    if (label_mode) {
        if (strcmp(g_drawn_label, g_last_label) == 0 && g_echo_area_ready &&
            g_drawn_echo[0] == '\0') {
            return;
        }
        M5Cardputer.Display.fillRect(px, py, pw, ph, BLACK);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(APP_COLOR_TEXT, BLACK);
        M5Cardputer.Display.drawCenterString(g_last_label, px + pw / 2, py + ph / 2 - 8);
        strncpy(g_drawn_label, g_last_label, sizeof(g_drawn_label) - 1);
        g_drawn_label[sizeof(g_drawn_label) - 1] = '\0';
        g_drawn_echo[0] = '\0';
        g_echo_area_ready = true;
        return;
    }

    if (!g_echo_area_ready || g_drawn_label[0] != '\0') {
        M5Cardputer.Display.fillRect(px, py, pw, ph, BLACK);
        g_drawn_label[0] = '\0';
        memset(g_drawn_echo, 0, sizeof(g_drawn_echo));
        g_echo_area_ready = true;
    }

    const int total_w = static_cast<int>(kEchoMaxChars) * kEchoCellW;
    const int ox = px + (pw - total_w) / 2;
    const int oy = py + (ph - kEchoCellH) / 2;
    for (size_t i = 0; i < kEchoMaxChars; i++) {
        const char c = (i < strlen(g_echo)) ? g_echo[i] : '\0';
        if (c == g_drawn_echo[i]) {
            continue;
        }
        const int cx = ox + static_cast<int>(i) * kEchoCellW;
        M5Cardputer.Display.fillRect(cx, oy, kEchoCellW, kEchoCellH, BLACK);
        if (c != '\0') {
            char s[2] = {c, '\0'};
            M5Cardputer.Display.setTextSize(kEchoTextSize);
            M5Cardputer.Display.setTextColor(APP_COLOR_TEXT, BLACK);
            M5Cardputer.Display.setCursor(cx + 1, oy);
            M5Cardputer.Display.print(s);
        }
        g_drawn_echo[i] = c;
    }
    g_drawn_echo[kEchoMaxChars] = '\0';
}

static void refreshRightPanel(const bool force) {
    if (g_imu_mouse_on) {
        if (force || !g_imu_pad_drawn) {
            clearRightPanel();
        }
        drawImuHud(force || !g_imu_pad_drawn);
    } else {
        if (force || g_imu_pad_drawn) {
            clearRightPanel();
        }
        drawEchoOnly();
    }
}

// Hosts 列表仍用旧槽号徽章（右上角）
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
        const int bw = tw + 4;
        const int x = screen_w - bw - 4;
        drawKeyBadge(x, y, key, 2);
    }
    g_drawn_slot_num = num;
}

// Hosts / Help 底栏用
static int hintBarY() {
    return M5Cardputer.Display.height() - 12;
}
static int hintBarRow1Y() {
    return hintBarY() - 12;
}

static void drawMainFooter() {
    g_drawn_footer_status[0] = '\0';
    g_drawn_footer_name[0] = '\0';
    g_drawn_footer_slot = -2;
    drawPairFooter(true);
}

// Help：分区小标题（无色块栏，易扫读）
static int helpDrawSection(const int x, const int y, const char* title) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(title);
    return y + 10;
}

// 徽章后打印说明，并恢复提示色
static void helpPrintAfterBadge(int& x, const int y, const char* text) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y + 1);
    M5Cardputer.Display.print(text);
    x = M5Cardputer.Display.getCursorX();
}

// 单行：文本徽章 + 说明
static int helpRowBadge(const int x0, const int y, const char* badge, const char* text) {
    int x = x0;
    x += drawTextBadge(x, y, badge, 1);
    helpPrintAfterBadge(x, y, text);
    return y + 11;
}

// 单行：单字符徽章 + 说明
static int helpRowKey(const int x0, const int y, const char key, const char* text) {
    int x = x0;
    x += drawKeyBadge(x, y, key, 1);
    helpPrintAfterBadge(x, y, text);
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

// Help：无固定 header / 左右栏；单列流式阅读，按键一律徽章包裹
static void drawHelpPage() {
    M5Cardputer.Display.fillScreen(BLACK);
    g_screen_ready = true;

    constexpr int x0 = 4;
    int y = 2;

    if (g_help_page == 0) {
        y = helpDrawSection(x0, y, "Mode");
        y = helpRowBadge(x0, y, "BtnGO", " exit app");
        {
            int x = x0;
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(x, y + 1);
            M5Cardputer.Display.print("hold ");
            x = M5Cardputer.Display.getCursorX();
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, " IMU on/off");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawKeyBadge(x, y, 'u', 1);
            helpPrintAfterBadge(x, y, " USB  ");
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawKeyBadge(x, y, 'b', 1);
            helpPrintAfterBadge(x, y, " BLE");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawKeyBadge(x, y, 'p', 1);
            helpPrintAfterBadge(x, y, " hosts  ");
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawKeyBadge(x, y, 'h', 1);
            helpPrintAfterBadge(x, y, " help");
            y += 12;
        }

        y = helpDrawSection(x0, y, "IMU mouse");
        {
            int x = x0;
            x += drawTextBadge(x, y, "ygv", 1);
            helpPrintAfterBadge(x, y, " left click");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "uhb", 1);
            helpPrintAfterBadge(x, y, " right click");
            y += 11;
        }
        {
            int x = x0;
            x += drawKeyBadge(x, y, '1', 1);
            helpPrintAfterBadge(x, y, "-");
            x += drawKeyBadge(x, y, '0', 1);
            helpPrintAfterBadge(x, y, " sens · other keys OK");
            y += 11;
        }
        y = helpRowKey(x0, y, '`', " Esc to host");
    } else {
        y = helpDrawSection(x0, y, "Fn layer");
        y = helpRowKey(x0, y, '`', " Esc");
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawTextBadge(x, y, "Bksp", 1);
            helpPrintAfterBadge(x, y, " Delete");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawTextBadge(x, y, ";,./", 1);
            helpPrintAfterBadge(x, y, " arrows");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawTextBadge(x, y, "1-0", 1);
            helpPrintAfterBadge(x, y, " F1-F10");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawTextBadge(x, y, "-=", 1);
            helpPrintAfterBadge(x, y, " F11/F12");
            y += 11;
        }
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawTextBadge(x, y, "Aa", 1);
            helpPrintAfterBadge(x, y, " Caps · mods=right");
            y += 12;
        }

        y = helpDrawSection(x0, y, "Tip");
        {
            int x = x0;
            x += drawTextBadge(x, y, "Fn", 1);
            helpPrintAfterBadge(x, y, "+");
            x += drawKeyBadge(x, y, 'p', 1);
            helpPrintAfterBadge(x, y, " hosts list");
        }
    }

    drawHelpHintBar();
}

static uint16_t hostCardBg() {
    return M5Cardputer.Display.color565(0x0D, 0x16, 0x22);
}

static uint16_t hostCardAccent() {
    return M5Cardputer.Display.color565(0xE9, 0xC4, 0x6A);
}

static uint16_t hostCardBorder() {
    return M5Cardputer.Display.color565(0x9A, 0x82, 0x48);
}

static uint16_t hostCardTitleColor() {
    return M5Cardputer.Display.color565(0xF4, 0xF1, 0xE8);
}

static int hostCardY(const int index) {
    return kHostCardOriginY + index * (kHostCardH + kHostCardGapY);
}

// 左侧单列卡片
static void drawHostCard(const int index) {
    const int x = kHostCardOriginX;
    const int y = hostCardY(index);
    const bool sel = (index == g_sel_slot);
    const bool linked = g_ble_connected && index == g_active_slot && g_hosts[index].used;
    const uint16_t card_bg = hostCardBg();
    const uint16_t accent = linked ? APP_COLOR_OK : hostCardAccent();
    const uint16_t border = sel ? YELLOW : hostCardBorder();

    M5Cardputer.Display.fillRoundRect(x, y, kHostCardW, kHostCardH, 3, card_bg);
    M5Cardputer.Display.drawRoundRect(x, y, kHostCardW, kHostCardH, 3, border);
    M5Cardputer.Display.fillRoundRect(x + 3, y + 3, 14, 12, 2, accent);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, accent);
    M5Cardputer.Display.setCursor(x + 7, y + 5);
    M5Cardputer.Display.print(index + 1);

    char title[20] = "(empty)";
    if (g_rename_ui && sel && g_hosts[index].used) {
        snprintf(title, sizeof(title), "%s_", g_rename_buf);
    } else if (g_hosts[index].used) {
        formatHostDisplayName(index, title, sizeof(title));
    }
    M5Cardputer.Display.setTextColor(sel ? APP_COLOR_VALUE : hostCardTitleColor(), card_bg);
    M5Cardputer.Display.setCursor(x + 22, y + 5);
    M5Cardputer.Display.print(title);
}

// 右侧竖排 tip（徽章 + 说明）
static void drawHostsTips() {
    const int screen_w = M5Cardputer.Display.width();
    const int tip_x = kHostTipsX;
    M5Cardputer.Display.fillRect(tip_x - 2, 0, screen_w - tip_x + 2,
                                 M5Cardputer.Display.height() - 12, BLACK);

    int y = kHostCardOriginY;
    auto tip_row = [&](const char* badge, const char* text) {
        int x = tip_x;
        x += drawTextBadge(x, y, badge, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x, y + 1);
        M5Cardputer.Display.print(text);
        y += 12;
    };
    auto tip_key = [&](const char key, const char* text) {
        int x = tip_x;
        x += drawKeyBadge(x, y, key, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x, y + 1);
        M5Cardputer.Display.print(text);
        y += 12;
    };

    if (g_rename_ui) {
        tip_row("Ent", " save");
        tip_row("Bk", " del");
        tip_key('`', " cancel");
        return;
    }
    tip_row("1-5", " select");
    tip_row("Ent", " switch");
    tip_key('n', " new");
    tip_key('d', " del");
    tip_key('r', " rename");
    tip_key('p', " back");
}

// 配对状态文案
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
            snprintf(out, out_len, "#%d %s", g_active_slot + 1, name);
        } else if (g_peer_addr[0] != '\0') {
            snprintf(out, out_len, "%s", g_peer_addr);
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

// 左下角配对信息
static void drawHostsPairStatus() {
    const int y = M5Cardputer.Display.height() - 12;
    // 只清左侧列表宽度，不盖右侧 tip
    M5Cardputer.Display.fillRect(0, y, kHostTipsX - 2, 12, BLACK);
    char status[40];
    uint16_t status_color = APP_COLOR_HINT;
    formatHostsStatusLine(status, sizeof(status), &status_color);
    if (status[0] != '\0') {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(status_color, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, y + 2);
        M5Cardputer.Display.print(status);
    }
}

// 重绘左侧 5 槽 + 左下角状态（右侧 tip 不动）
static void redrawHostsListAndStatus() {
    for (int i = 0; i < kBleHostSlots; i++) {
        drawHostCard(i);
    }
    drawHostsPairStatus();
}

static void drawHostsUi() {
    M5Cardputer.Display.fillScreen(BLACK);
    g_screen_ready = true;
    redrawHostsListAndStatus();
    drawHostsTips();
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

    // 主界面无 header：全屏黑底
    (void)full_init;
    M5Cardputer.Display.fillScreen(BLACK);
    g_screen_ready = true;

    // 清屏后失效缓存
    g_mods_drawn = false;
    g_imu_pad_drawn = false;
    g_echo_area_ready = false;
    g_drawn_footer_status[0] = '\0';
    g_drawn_footer_name[0] = '\0';
    g_drawn_footer_slot = -2;
    {
        Keyboard_Class::KeysState empty{};
        drawModStateCol(empty, true);
    }
    refreshRightPanel(true);
    drawPairFooter(true);
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
    g_fn_down_ms = 0;
    g_fn_long_fired = false;
    g_fn_long_cancelled = false;
    g_imu_ok = M5.Imu.isEnabled();
    g_imu_mouse_on = false;
    g_imu_sens = kImuSensDefault;
    g_imu_dx = 0;
    g_imu_dy = 0;
    g_mouse_buttons = 0;
    g_mods_drawn = false;
    g_hosts_status[0] = '\0';
    g_auth_hint[0] = '\0';
    g_auth_fail_streak = 0;
    clearPendingConn();
    g_echo[0] = '\0';
    g_last_label[0] = '\0';
    g_drawn_echo[0] = '\0';
    g_drawn_label[0] = '\0';
    g_drawn_footer_status[0] = '\0';
    g_drawn_footer_name[0] = '\0';
    g_drawn_footer_slot = -2;
    g_imu_pad_drawn = false;
    g_echo_area_ready = false;
    g_last_kb_valid = false;
    memset(&g_last_kb_report, 0, sizeof(g_last_kb_report));
    clearBleReportQueue();
    // 默认 BLE，不占用烧录口；需要 USB 时再 Fn+u
    applyTransport(g_transport);
    drawHidKeyboardApp(true);
}

void leaveHidKeyboardApp() {
    if (!g_active && !g_exiting) {
        return;
    }
    // 先进入 exiting：立刻全屏 Exiting，并继续压制 header 刷新
    g_exiting = true;
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.drawCenterString("Exiting", M5Cardputer.Display.width() / 2,
                                         M5Cardputer.Display.height() / 2 - 8);

    g_active = false;
    g_help_visible = false;
    g_hosts_ui = false;
    g_rename_ui = false;
    g_imu_mouse_on = false;
    // 先抬键再拆栈，否则主机端会一直认为键还按着
    releaseAllToHost();
    clearBleReportQueue();

    stopBleKeyboard();
    // 退出应用时务必把 USB 还给 JTAG，否则无法 upload
    stopUsbKeyboard();
    g_exiting = false;
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
        drainBleReportQueueBurst();
        // 新配对若一直无 AUTH：超时提示 forget，不误伤已配对槽
        if (g_pending.active && g_pending.is_new &&
            (millis() - g_pending.since_ms) > 10000) {
            handleAuthFailure(g_pending.bda);
        }
    }

    static bool last_connected = false;
    static bool last_pending = false;
    static HidTransport last_transport = HidTransport::BLE;
    static char last_status[sizeof(g_auth_hint)] = "";
    static int last_slot = -3;
    const bool connected =
        (g_transport == HidTransport::USB) ? g_usb_ready : g_ble_connected;
    const char* status = connectionStatusText();
    if (connected != last_connected || g_pending.active != last_pending ||
        g_transport != last_transport || strcmp(last_status, status) != 0 ||
        g_active_slot != last_slot) {
        last_connected = connected;
        last_pending = g_pending.active;
        last_transport = g_transport;
        last_slot = g_active_slot;
        strncpy(last_status, status, sizeof(last_status) - 1);
        last_status[sizeof(last_status) - 1] = '\0';
        if (g_transport == HidTransport::BLE && !g_ble_connected && !g_pending.active) {
            clearBleReportQueue();
            if (g_auth_hint[0] == '\0') {
                clearPeerInfo();
            }
        }
        drawPairFooter(false);
    }

    // 每帧同步键位（不依赖 isChange，避免连按漏松键）+ 排空 BLE
    if (!g_help_visible && !g_hosts_ui) {
        syncKeysToHost();
        pollFnLongPressToggle();
        if (g_imu_mouse_on) {
            pollImuMousePointer();
            drawImuHud(false);
        }
    }
    if (g_transport == HidTransport::BLE) {
        drainBleReportQueueBurst();
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

    // IMU 模式：灵敏度 / 鼠标键；字母作点击，其它功能键仍发给主机
    if (g_imu_mouse_on && !g_help_visible && !g_hosts_ui) {
        (void)tryHandleImuSensKey(status);
        const uint8_t mb = mouseButtonsFromKeys(status);
        if (mb != g_mouse_buttons) {
            sendMouseReport(0, 0, mb);
        }
        drawModStateCol(status, false);
        drawImuHud(false);
        if (!M5Cardputer.Keyboard.isPressed()) {
            if (g_mouse_buttons != 0) {
                releaseMouseButtons();
                Keyboard_Class::KeysState empty{};
                drawModStateCol(empty, false);
                drawImuHud(false);
            }
            sendHostReport(Keyboard_Class::KeysState{});
        } else {
            sendHostReport(status);
        }
        if (g_transport == HidTransport::BLE) {
            drainBleReportQueueBurst();
        }
        return;
    }

    if (!g_help_visible && !g_hosts_ui) {
        drawModStateCol(status, false);
    }

    if (!M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState empty{};
        sendHostReport(empty);
        if (g_transport == HidTransport::BLE) {
            drainBleReportQueueBurst();
        }
        return;
    }

    sendHostReport(status);
    updateEchoBuffer(status);
    drawEchoOnly();
    if (g_transport == HidTransport::BLE) {
        drainBleReportQueueBurst();
    }
}

bool pollHidKeyboardBtnAExit() {
    if (!g_active) {
        return false;
    }
    // 真正的 leave 交给 showMenu，避免这里拆栈后再 leave 一次
    return M5Cardputer.BtnA.wasPressed();
}

// 主输入 / Help / Hosts / 退出中：均无 header（避免蓝牙图标刷到无顶栏界面）
bool hidKeyboardSuppressesHeader() {
    return g_active || g_exiting;
}
