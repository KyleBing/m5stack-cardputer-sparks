#include "app_hid_kb.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_connectivity.h"
#include "app_header.h"

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <HIDTypes.h>
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

static bool g_screen_ready = false;
static bool g_active = false;
static bool g_help_visible = false;
static int g_help_page = 0;
static bool g_fn_h_latched = false;
static bool g_fn_caps_latched = false;
static HidTransport g_transport = HidTransport::BLE;
static bool g_usb_ready = false;
static bool g_usb_inited = false;
static bool g_ble_ready = false;
static bool g_ble_connected = false;
static char g_echo[kEchoMaxChars + 1] = "";
static char g_last_label[16] = "";
static char g_drawn_echo[kEchoMaxChars + 1] = "";
static char g_drawn_label[16] = "";
static char g_peer_addr[18] = "";
static char g_drawn_peer[18] = "";

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

class HidKbBleCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        (void)server;
        g_ble_connected = true;
    }

    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        (void)server;
        g_ble_connected = true;
        if (param != nullptr) {
            formatPeerAddr(param->connect.remote_bda, g_peer_addr, sizeof(g_peer_addr));
        }
    }

    void onDisconnect(BLEServer* server) override {
        g_ble_connected = false;
        clearPeerInfo();
        if (g_ble_ready && server != nullptr) {
            server->startAdvertising();
        }
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
    return g_ble_connected ? "paired" : "pairing...";
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
    if (!g_ble_ready && g_hid == nullptr) {
        resetBleStackFully();
        return;
    }
    BLEDevice::stopAdvertising();
    BLEDevice::deinit(false);
    g_hid = nullptr;
    g_kb_input = nullptr;
    g_ble_server = nullptr;
    g_ble_ready = false;
    g_ble_connected = false;
    clearPeerInfo();
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

static void clearBleBonds() {
    const int n = esp_ble_get_bond_device_num();
    if (n <= 0) {
        return;
    }
    auto* list = static_cast<esp_ble_bond_dev_t*>(malloc(sizeof(esp_ble_bond_dev_t) * static_cast<size_t>(n)));
    if (list == nullptr) {
        return;
    }
    int count = n;
    if (esp_ble_get_bond_device_list(&count, list) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            esp_ble_remove_bond_device(list[i].bd_addr);
        }
    }
    free(list);
}

static void startBleKeyboard() {
    if (g_ble_ready) {
        return;
    }
    stopBleStack();
    resetBleStackFully();
    WiFi.setSleep(true);
    BLEDevice::init("Cardputer KB");
    g_ble_server = BLEDevice::createServer();
    g_ble_server->setCallbacks(new HidKbBleCallbacks());

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
    advertising->start();

    g_ble_ready = true;
    g_ble_connected = false;
}

static void restartBlePairing() {
    applyTransport(HidTransport::BLE);
    if (!g_ble_ready) {
        return;
    }
    clearBleReportQueue();
    disconnectBleClients();
    clearBleBonds();
    g_ble_connected = false;
    clearPeerInfo();
    delay(120);
    BLEDevice::startAdvertising();
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
static void drawHidKbApp(const bool full_init);

// Fn+h 帮助；帮助页内 h/,/. 翻页关闭；Fn+u/b/p 模式热键
static bool tryHandleModeHotkey(const Keyboard_Class::KeysState& status) {
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
                    drawHidKbApp(true);
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
                        drawHidKbApp(true);
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
            restartBlePairing();
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
    return APP_CONTENT_Y + INFO_LINE_H_2X + 4;
}

static void drawPeerLine() {
    // 作为外设连上主机时，连接事件通常只有 BD 地址；主机名一般拿不到
    const char* text = (g_transport == HidTransport::BLE) ? g_peer_addr : "";
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
    strncpy(g_drawn_peer, text, sizeof(g_drawn_peer) - 1);
    g_drawn_peer[sizeof(g_drawn_peer) - 1] = '\0';
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
    cx += drawTextBadge(cx, row2_y, "BtnA", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("exit ");
    cx = M5Cardputer.Display.getCursorX() + 4;
    cx += drawTextBadge(cx, row2_y, "Fn+p", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, row2_y + 1);
    M5Cardputer.Display.print("pair");
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
    beginAppScreenAccent("KB ", "Help", APP_COLOR_LABEL);
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
        y = helpDrawBadge(2, y, "BtnA", "exit app");
        y = helpDrawBadge(2, y, "Fn+u", "USB");
        y = helpDrawBadge(2, y, "Fn+b", "BLE");
        y = helpDrawBadge(2, y, "Fn+p", "pair");
        y = helpDrawBadge(2, y, "Fn+h", "help");

        y = helpDrawColHeader(manual_x, col_y, screen_w - manual_x, "manual");
        y = helpDrawLine(manual_x + 2, y, "USB / BLE keyboard");
        y = helpDrawLine(manual_x + 2, y, "all keys go to host");
        y = helpDrawLine(manual_x + 2, y, "default uses BLE");
        y = helpDrawLine(manual_x + 2, y, "name: Cardputer KB");
        y = helpDrawLine(manual_x + 2, y, "USB blocks upload");
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

        y = helpDrawColHeader(manual_x, col_y, screen_w - manual_x, "manual");
        y = helpDrawLine(manual_x + 2, y, "Opt = Win / Cmd");
        y = helpDrawLine(manual_x + 2, y, "Fn uses orange keys");
        y = helpDrawLine(manual_x + 2, y, "unknown Fn ignored");
        y = helpDrawLine(manual_x + 2, y, "side BtnA exits");
    }

    drawHelpHintBar();
}

static void drawHidKbApp(const bool full_init) {
    if (g_help_visible) {
        drawHelpPage();
        return;
    }

    if (full_init || !g_screen_ready) {
        beginAppScreenAccent("KB ", transportName(g_transport), APP_COLOR_LABEL);
        g_screen_ready = true;
    } else {
        clearAppContentArea();
        drawAppScreenHeaderAccent("KB ", transportName(g_transport), APP_COLOR_LABEL);
    }

    drawInfoLineAt(APP_CONTENT_X, APP_CONTENT_Y, "link", connectionStatusText(), 2);
    g_drawn_echo[0] = '\0';
    g_drawn_label[0] = '\0';
    drawEchoOnly();
    drawHintBar();  // 内含 tip 上方 peer MAC
}

void enterHidKbApp() {
    g_screen_ready = false;
    g_active = true;
    g_help_visible = false;
    g_help_page = 0;
    g_fn_h_latched = false;
    g_fn_caps_latched = false;
    g_echo[0] = '\0';
    g_last_label[0] = '\0';
    g_drawn_echo[0] = '\0';
    g_drawn_label[0] = '\0';
    clearBleReportQueue();
    // 默认 BLE，不占用烧录口；需要 USB 时再 Fn+u
    applyTransport(g_transport);
    drawHidKbApp(true);
}

void leaveHidKbApp() {
    if (!g_active) {
        return;
    }
    g_active = false;
    g_help_visible = false;
    clearBleReportQueue();
    stopBleKeyboard();
    // 退出应用时务必把 USB 还给 JTAG，否则无法 upload
    stopUsbKeyboard();
}

void updateHidKbApp() {
    if (!g_active) {
        return;
    }
    if (g_help_visible) {
        return;
    }
    if (g_transport == HidTransport::BLE) {
        drainBleReportQueue();
        if (g_ble_connected && g_peer_addr[0] != '\0') {
            drawPeerLine();
        }
    }

    static bool last_connected = false;
    static HidTransport last_transport = HidTransport::BLE;
    const bool connected =
        (g_transport == HidTransport::USB) ? g_usb_ready : g_ble_connected;
    if (connected != last_connected || g_transport != last_transport) {
        last_connected = connected;
        last_transport = g_transport;
        if (g_transport == HidTransport::BLE && !g_ble_connected) {
            clearBleReportQueue();
            clearPeerInfo();
        }
        drawHidKbApp(false);
    }
}

void handleHidKbApp(const Keyboard_Class::KeysState& status) {
    if (!g_active) {
        return;
    }
    if (tryHandleModeHotkey(status)) {
        // 打开帮助时已自绘；模式切换需刷新主界面
        if (!g_help_visible) {
            drawHidKbApp(false);
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

bool pollHidKbBtnAExit() {
    if (!g_active) {
        return false;
    }
    if (!M5Cardputer.BtnA.wasPressed()) {
        return false;
    }
    leaveHidKbApp();
    return true;
}
