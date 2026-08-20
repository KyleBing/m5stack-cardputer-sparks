#include "app_nfc.h"
#include "app_colors.h"
#include "app_common.h"

#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <wiring/m5_unit_unified_wiring.hpp>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace {

using namespace m5::nfc;
using namespace m5::nfc::a;
using namespace m5::nfc::a::mifare;

enum class NfcView {
    MAIN,
    HISTORY,
    EMULATION,
};

enum class NfcOperation {
    NONE,
    READ,
    WRITE,
};

struct NfcDumpRow {
    uint16_t block;
    String hex;
    String ascii;
    bool readable;
};

struct NfcRecord {
    String uid;
    String type;
    String data;
    uint32_t ms;
};

static constexpr int NFC_HISTORY_MAX = 12;
static constexpr char kDefaultWritePayload[] = "Cardputer NFC";
static constexpr int NFC_DUMP_ROWS_PER_PAGE = 4;
static constexpr size_t NFC_EMU_MEMORY_SIZE = 64;

m5::unit::UnitUnified g_units;
m5::unit::UnitNFC g_unit;
NFCLayerA g_nfc_a{g_unit};
EmulationLayerA g_emu_a{g_unit};

bool g_ready = false;
bool g_help_visible = false;
int g_help_page = 0;
bool g_read_scanning = false;
bool g_emulation_on = false;
NfcView g_view = NfcView::MAIN;
NfcOperation g_operation = NfcOperation::NONE;
bool g_record_on_read = true;
String g_write_payload = kDefaultWritePayload;
String g_last_msg = "ready";
String g_last_uid = "--";
String g_last_type = "--";
String g_last_data = "--";
String g_last_meta1 = "--";
String g_last_meta2 = "--";
String g_last_ndef = "NDEF: --";
bool g_last_all_zero = false;
int g_result_page = 0;
std::vector<NfcDumpRow> g_dump_rows;
std::vector<uint8_t> g_dump_bytes;
PICC g_emu_picc{};
std::array<uint8_t, NFC_EMU_MEMORY_SIZE> g_emu_memory{};
EmulationLayerA::State g_emu_state = EmulationLayerA::State::None;
std::array<NfcRecord, NFC_HISTORY_MAX> g_history{};
int g_history_count = 0;
int g_history_start = 0;
int g_history_sel = 0;

// 交互过程 + 往复数据日志（可滚动）
static constexpr int NFC_LOG_MAX = 32;
static constexpr int NFC_LOG_LINE_MAX = 48;
static constexpr int NFC_LOG_VISIBLE = 7;
std::array<String, NFC_LOG_MAX> g_log{};
int g_log_count = 0;
int g_log_start = 0;
int g_log_scroll = 0; // 0 = 显示最新

static void drawNfcApp();

static void clearLog() {
    g_log_count = 0;
    g_log_start = 0;
    g_log_scroll = 0;
}

static void pushLog(const char* line) {
    if (line == nullptr || line[0] == '\0') {
        return;
    }
    const int idx = (g_log_start + g_log_count) % NFC_LOG_MAX;
    g_log[idx] = line;
    if (g_log_count < NFC_LOG_MAX) {
        ++g_log_count;
    } else {
        g_log_start = (g_log_start + 1) % NFC_LOG_MAX;
    }
    // 新日志到来时跟到底部
    g_log_scroll = 0;
}

static void pushLogf(const char* fmt, ...) {
    char buf[NFC_LOG_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    pushLog(buf);
}

static const String& logLineAt(const int logical_idx) {
    static const String kEmpty;
    if (logical_idx < 0 || logical_idx >= g_log_count) {
        return kEmpty;
    }
    const int idx = (g_log_start + logical_idx) % NFC_LOG_MAX;
    return g_log[idx];
}

// 循环队列追加，保留最近 NFC_HISTORY_MAX 条记录。
static void pushHistory(const String& uid, const String& type, const String& data) {
    const int idx = (g_history_start + g_history_count) % NFC_HISTORY_MAX;
    g_history[idx] = {uid, type, data, millis()};
    if (g_history_count < NFC_HISTORY_MAX) {
        ++g_history_count;
    } else {
        g_history_start = (g_history_start + 1) % NFC_HISTORY_MAX;
    }
    g_history_sel = g_history_count > 0 ? (g_history_count - 1) : 0;
}

static const NfcRecord* selectedRecord() {
    if (g_history_count <= 0 || g_history_sel < 0 || g_history_sel >= g_history_count) {
        return nullptr;
    }
    const int idx = (g_history_start + g_history_sel) % NFC_HISTORY_MAX;
    return &g_history[idx];
}

static String bytesToHex(const uint8_t* data, const size_t len) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += kHex[(data[i] >> 4) & 0x0F];
        out += kHex[data[i] & 0x0F];
    }
    return out;
}

static String bytesToAscii(const uint8_t* data, const size_t len) {
    String out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out += (data[i] >= 32 && data[i] <= 126) ? static_cast<char>(data[i]) : '.';
    }
    return out;
}

static void appendDumpRow(const uint16_t block, const uint8_t* data, const size_t len) {
    g_dump_rows.push_back({block, bytesToHex(data, len), bytesToAscii(data, len), true});
    g_dump_bytes.insert(g_dump_bytes.end(), data, data + len);
}

static void appendUnreadableRow(const uint16_t block) {
    g_dump_rows.push_back({block, "--", "protected/read fail", false});
}

static bool isClassicTrailer(const uint16_t block) {
    // MIFARE 4K 在 2K 以后每 16 块一个扇区，其余区域每 4 块一个扇区。
    return block < 128 ? (block % 4) == 3 : (block % 16) == 15;
}

static String parseNdefSummary(const std::vector<uint8_t>& bytes) {
    size_t tlv = 0;
    while (tlv < bytes.size() && (bytes[tlv] == 0x00 || bytes[tlv] == 0x01 ||
                                  bytes[tlv] == 0x02)) {
        if (bytes[tlv] == 0x00) {
            ++tlv;
            continue;
        }
        if (tlv + 1 >= bytes.size()) {
            return "NDEF: malformed TLV";
        }
        tlv += 2 + bytes[tlv + 1];
    }
    if (tlv >= bytes.size() || bytes[tlv] != 0x03) {
        return "NDEF: none";
    }
    ++tlv;
    if (tlv >= bytes.size()) {
        return "NDEF: malformed";
    }
    size_t ndef_len = bytes[tlv++];
    if (ndef_len == 0xFF) {
        if (tlv + 1 >= bytes.size()) {
            return "NDEF: malformed";
        }
        ndef_len = (static_cast<size_t>(bytes[tlv]) << 8) | bytes[tlv + 1];
        tlv += 2;
    }
    if (ndef_len == 0 || tlv + ndef_len > bytes.size() || tlv + 3 > bytes.size()) {
        return "NDEF: malformed";
    }

    const size_t record_end = tlv + ndef_len;
    const uint8_t flags = bytes[tlv++];
    const bool short_record = (flags & 0x10) != 0;
    const bool has_id = (flags & 0x08) != 0;
    const uint8_t type_len = bytes[tlv++];
    uint32_t payload_len = 0;
    if (short_record) {
        payload_len = bytes[tlv++];
    } else {
        if (tlv + 4 > record_end) {
            return "NDEF: malformed";
        }
        payload_len = (static_cast<uint32_t>(bytes[tlv]) << 24) |
                      (static_cast<uint32_t>(bytes[tlv + 1]) << 16) |
                      (static_cast<uint32_t>(bytes[tlv + 2]) << 8) | bytes[tlv + 3];
        tlv += 4;
    }
    uint8_t id_len = 0;
    if (has_id) {
        if (tlv >= record_end) {
            return "NDEF: malformed";
        }
        id_len = bytes[tlv++];
    }
    if (tlv + type_len + id_len + payload_len > record_end) {
        return "NDEF: malformed";
    }
    const uint8_t* type = bytes.data() + tlv;
    tlv += type_len + id_len;
    const uint8_t* payload = bytes.data() + tlv;

    if (type_len == 1 && type[0] == 'T' && payload_len > 0) {
        const uint8_t lang_len = payload[0] & 0x3F;
        if (1u + lang_len > payload_len) {
            return "NDEF Text: malformed";
        }
        String text;
        for (size_t i = 1 + lang_len; i < payload_len; ++i) {
            text += static_cast<char>(payload[i]);
        }
        return "NDEF Text: " + text;
    }
    if (type_len == 1 && type[0] == 'U' && payload_len > 0) {
        static const char* const prefixes[] = {
            "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
        };
        String uri = payload[0] < (sizeof(prefixes) / sizeof(prefixes[0]))
                         ? prefixes[payload[0]]
                         : "";
        for (size_t i = 1; i < payload_len; ++i) {
            uri += static_cast<char>(payload[i]);
        }
        return "NDEF URI: " + uri;
    }
    char summary[48];
    snprintf(summary, sizeof(summary), "NDEF TNF:%u type:%.*s len:%lu", flags & 0x07,
             type_len, reinterpret_cast<const char*>(type),
             static_cast<unsigned long>(payload_len));
    return summary;
}

// 按卡型读取全部可访问用户区；读取失败的块单独标记，避免显示伪造的全零数据。
static bool readCardData(const PICC& picc, String& out) {
    out = "--";
    g_dump_rows.clear();
    g_dump_bytes.clear();
    bool any_read = false;
    if (picc.isMifareClassic()) {
        for (uint16_t block = picc.firstUserBlock(); block <= picc.lastUserBlock(); ++block) {
            if (isClassicTrailer(block)) {
                continue;
            }
            uint8_t buf[16]{};
            if (!g_nfc_a.mifareClassicAuthenticateA(static_cast<uint8_t>(block),
                                                    classic::DEFAULT_KEY) ||
                !g_nfc_a.read16(buf, static_cast<uint8_t>(block))) {
                appendUnreadableRow(block);
                continue;
            }
            appendDumpRow(block, buf, sizeof(buf));
            any_read = true;
        }
    } else if (picc.supportsNFC()) {
        for (uint16_t page = picc.firstUserBlock(); page <= picc.lastUserBlock(); ++page) {
            uint8_t buf[4]{};
            if (!g_nfc_a.read4(buf, static_cast<uint8_t>(page))) {
                appendUnreadableRow(page);
                continue;
            }
            appendDumpRow(page, buf, sizeof(buf));
            any_read = true;
        }
    } else if (picc.isFileSystemMemory()) {
        const uint16_t capacity = picc.userAreaSize();
        std::vector<uint8_t> buf(capacity);
        uint16_t len = capacity;
        if (capacity > 0 &&
            g_nfc_a.read(buf.data(), len, picc.firstUserBlock(), classic::DEFAULT_KEY)) {
            for (uint16_t offset = 0; offset < len; offset += 16) {
                const size_t remaining = len - offset;
                const size_t row_len = remaining < 16 ? remaining : 16;
                appendDumpRow(picc.firstUserBlock() + offset / 16, buf.data() + offset, row_len);
            }
            any_read = true;
        } else {
            appendUnreadableRow(picc.firstUserBlock());
        }
    }
    if (!any_read) {
        g_last_ndef = "NDEF: unavailable";
        g_last_all_zero = false;
        return false;
    }
    g_last_all_zero = true;
    for (const uint8_t value : g_dump_bytes) {
        if (value != 0) {
            g_last_all_zero = false;
            break;
        }
    }
    g_last_ndef = g_last_all_zero ? "NDEF: blank (all 00)" : parseNdefSummary(g_dump_bytes);
    out = g_last_all_zero ? "blank (all 00)" : g_last_ndef;
    return true;
}

// 写入时按卡型路由；TX 为待写 hex，成功后再读回记 RX。
static bool writeCardData(const PICC& picc, const String& payload) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(payload.c_str());
    const size_t len = payload.length();
    if (picc.isMifareClassic()) {
        const uint8_t block = picc.firstUserBlock();
        pushLogf("> authA blk%u", block);
        if (!g_nfc_a.mifareClassicAuthenticateA(block, classic::DEFAULT_KEY)) {
            pushLog("> authA fail");
            return false;
        }
        pushLog("> authA ok");
        uint8_t page[16]{};
        const size_t n = len > sizeof(page) ? sizeof(page) : len;
        for (size_t i = 0; i < n; ++i) {
            page[i] = bytes[i];
        }
        pushLogf("> TX write16 %s", bytesToHex(page, sizeof(page)).c_str());
        if (!g_nfc_a.write16(block, page, sizeof(page))) {
            pushLog("> TX write fail");
            return false;
        }
        pushLog("> TX write ok");
        return true;
    }
    if (picc.supportsNFC()) {
        const uint8_t page = picc.firstUserBlock();
        uint8_t p[4]{};
        const size_t n = len > sizeof(p) ? sizeof(p) : len;
        for (size_t i = 0; i < n; ++i) {
            p[i] = bytes[i];
        }
        pushLogf("> TX write4 %s", bytesToHex(p, sizeof(p)).c_str());
        if (!g_nfc_a.write4(page, p, sizeof(p))) {
            pushLog("> TX write fail");
            return false;
        }
        pushLog("> TX write ok");
        return true;
    }
    if (picc.isFileSystemMemory()) {
        pushLogf("> TX write %s", bytesToHex(bytes, len).c_str());
        if (!g_nfc_a.write(picc.firstUserBlock(), bytes, static_cast<uint16_t>(len), classic::DEFAULT_KEY)) {
            pushLog("> TX write fail");
            return false;
        }
        pushLog("> TX write ok");
        return true;
    }
    pushLog("> no RW path");
    return false;
}

static void refreshCardDetails(const PICC& picc, const String& data_text) {
    char line1[64];
    snprintf(line1, sizeof(line1), "ATQA:%04X SAK:%02X uid:%u", picc.atqa, picc.sak, picc.size);
    g_last_meta1 = line1;
    char line2[64];
    snprintf(line2, sizeof(line2), "usr:%u tot:%u blk:%u-%u", picc.userAreaSize(), picc.totalSize(),
             picc.firstUserBlock(), picc.lastUserBlock());
    g_last_meta2 = line2;
    g_last_uid = picc.uidAsString().c_str();
    g_last_type = picc.typeAsString().c_str();
    g_last_data = data_text;
}

// 把 PICC 详情与交互步骤写入日志。
static void logPiccDetails(const PICC& picc) {
    pushLogf("> ATQA:%04X SAK:%02X", picc.atqa, picc.sak);
    pushLogf("> UID(%u) %s", picc.size, picc.uidAsString().c_str());
    pushLogf("> type %s", picc.typeAsString().c_str());
    pushLogf("> usr:%u tot:%u unit:%u", picc.userAreaSize(), picc.totalSize(), picc.unitSize());
    pushLogf("> blk %u..%u n:%u", picc.firstUserBlock(), picc.lastUserBlock(), picc.blocks);
    if (picc.isMifarePlus()) {
        pushLogf("> plus SL%u", picc.security_level);
    }
}

// 官方 Detect 流程：detect(list) -> identify；读写才需要 reactivate。
// update_msg=false 时用于轮询，未检测到卡不刷状态、也不打日志（避免刷屏）。
static bool detectAndIdentify(PICC& picc, const bool update_msg = true) {
    std::vector<PICC> piccs;
    if (update_msg) {
        pushLog("> detect...");
    }
    if (!g_nfc_a.detect(piccs) || piccs.empty()) {
        if (update_msg) {
            g_last_msg = "no card";
            pushLog("> detect no card");
        }
        return false;
    }
    picc = piccs.front();
    pushLogf("> detect %u card(s)", static_cast<unsigned>(piccs.size()));
    pushLog("> identify...");
    if (!g_nfc_a.identify(picc)) {
        if (update_msg) {
            g_last_msg = "identify failed";
        }
        pushLog("> identify fail");
        return false;
    }
    pushLog("> identify ok");
    logPiccDetails(picc);
    return true;
}

static bool detectIdentifyAndActivate(PICC& picc, const bool update_msg = true) {
    if (!detectAndIdentify(picc, update_msg)) {
        return false;
    }
    pushLog("> reactivate...");
    if (!g_nfc_a.reactivate(picc)) {
        if (update_msg) {
            g_last_msg = "activate failed";
        }
        pushLog("> reactivate fail");
        return false;
    }
    pushLog("> reactivate ok");
    return true;
}

static void stopReadScan() {
    g_read_scanning = false;
    if (g_operation == NfcOperation::READ) {
        g_operation = NfcOperation::NONE;
    }
}

static void applyReadResult(PICC& picc, const String& data, const char* msg) {
    g_last_msg = msg;
    refreshCardDetails(picc, data);
    // RFID 场景优先记 UID；无区块数据时 data 记为 uid
    if (g_record_on_read) {
        pushHistory(g_last_uid, g_last_type, data != "--" ? data : g_last_uid);
        pushLog("> history saved");
    }
}

// 读到卡：先展示 UID/类型；能 activate 再尝试读用户区。
static void finishReadCard(PICC& picc) {
    String data = "--";
    const char* msg = "uid ok";
    pushLog("> reactivate...");
    if (g_nfc_a.reactivate(picc)) {
        pushLog("> reactivate ok");
        if (readCardData(picc, data)) {
            msg = "read ok";
        } else {
            msg = "uid ok / data fail";
        }
    } else {
        pushLog("> reactivate fail");
        msg = "uid ok / act fail";
    }
    applyReadResult(picc, data, msg);
    pushLogf("> done %s", msg);
    g_nfc_a.deactivate();
    pushLog("> deactivate");
    g_operation = NfcOperation::NONE;
    g_result_page = 0;
}

static void startReadScan() {
    clearLog();
    pushLog("> scan start");
    g_read_scanning = true;
    g_operation = NfcOperation::READ;
    g_last_msg = "scanning...";
    drawNfcApp();
}

// 在 update 中轮询：对齐官方 Detect，未检测到卡不 deactivate。
static void pollReadScan() {
    if (!g_ready || !g_read_scanning || g_help_visible || g_view != NfcView::MAIN) {
        return;
    }
    // 限制轮询频率，避免射频被打满
    static uint32_t last_poll_ms = 0;
    const uint32_t now = millis();
    if (now - last_poll_ms < 80) {
        return;
    }
    last_poll_ms = now;

    PICC picc{};
    if (!detectAndIdentify(picc, false)) {
        return;
    }
    g_read_scanning = false;
    finishReadCard(picc);
    drawNfcApp();
}

static void doReadCurrentCard() {
    if (!g_ready) {
        g_last_msg = "unit not ready";
        return;
    }
    startReadScan();
}

static void doWriteCurrentCard(const String& payload) {
    if (!g_ready) {
        g_last_msg = "unit not ready";
        return;
    }
    g_operation = NfcOperation::WRITE;
    g_last_msg = "writing...";
    drawNfcApp();
    clearLog();
    pushLog("> write start");
    pushLogf("> payload %s", payload.c_str());
    PICC picc{};
    if (!detectIdentifyAndActivate(picc)) {
        g_nfc_a.deactivate();
        pushLog("> deactivate");
        g_operation = NfcOperation::NONE;
        return;
    }
    const bool ok = writeCardData(picc, payload);
    if (ok) {
        g_last_msg = "write ok";
        g_write_payload = payload;
    } else {
        g_last_msg = "write failed";
    }
    String data;
    pushLog("> verify read...");
    if (readCardData(picc, data)) {
        refreshCardDetails(picc, data);
        pushLog("> verify ok");
    } else {
        refreshCardDetails(picc, "--");
        pushLog("> verify fail");
    }
    g_nfc_a.deactivate();
    pushLogf("> done %s", g_last_msg.c_str());
    pushLog("> deactivate");
    g_operation = NfcOperation::NONE;
    g_result_page = 0;
}

static bool initializeNfcUnit(const bool emulation) {
    g_units = m5::unit::UnitUnified{};
    auto cfg = g_unit.config();
    cfg.emulation = emulation;
    cfg.mode = NFC::A;
    g_unit.config(cfg);
    M5Cardputer.Ex_I2C.begin();
    return m5::unit::wiring::i2cClass(g_units, g_unit, M5Cardputer.Ex_I2C) &&
           g_units.begin();
}

static uint8_t uidBcc(const uint8_t* data, const size_t len, const uint8_t initial = 0) {
    uint8_t value = initial;
    for (size_t i = 0; i < len; ++i) {
        value ^= data[i];
    }
    return value;
}

static bool buildEmulatedNdef(const String& text) {
    static constexpr uint8_t uid[] = {0x04, 0x43, 0x41, 0x52, 0x44, 0x50, 0x54};
    g_emu_memory.fill(0);
    // Type 2 Tag 的 UID、BCC 与 capability container。
    memcpy(g_emu_memory.data(), uid, 3);
    g_emu_memory[3] = uidBcc(uid, 3, 0x88);
    memcpy(g_emu_memory.data() + 4, uid + 3, 4);
    g_emu_memory[8] = uidBcc(uid + 3, 4);
    g_emu_memory[9] = 0x48;
    g_emu_memory[12] = 0xE1;
    g_emu_memory[13] = 0x10;
    g_emu_memory[14] = 0x06;

    const size_t text_len = text.length() < 36 ? text.length() : 36;
    const uint8_t payload_len = static_cast<uint8_t>(3 + text_len);
    const uint8_t record_len = static_cast<uint8_t>(4 + payload_len);
    size_t pos = 16;
    g_emu_memory[pos++] = 0x03;
    g_emu_memory[pos++] = record_len;
    g_emu_memory[pos++] = 0xD1;
    g_emu_memory[pos++] = 0x01;
    g_emu_memory[pos++] = payload_len;
    g_emu_memory[pos++] = 'T';
    g_emu_memory[pos++] = 0x02;
    g_emu_memory[pos++] = 'e';
    g_emu_memory[pos++] = 'n';
    for (size_t i = 0; i < text_len; ++i) {
        g_emu_memory[pos++] = static_cast<uint8_t>(text[i]);
    }
    g_emu_memory[pos] = 0xFE;
    return g_emu_picc.emulate(Type::MIFARE_Ultralight, uid, sizeof(uid));
}

static bool startNfcEmulation() {
    stopReadScan();
    g_nfc_a.deactivate();
    g_ready = initializeNfcUnit(true);
    if (!g_ready || !buildEmulatedNdef(g_write_payload) ||
        !g_emu_a.begin(g_emu_picc, g_emu_memory.data(), g_emu_memory.size())) {
        g_last_msg = "emulation start failed";
        g_emulation_on = false;
        return false;
    }
    g_emulation_on = true;
    g_emu_state = g_emu_a.state();
    g_last_msg = "NDEF tag ready";
    return true;
}

static void stopNfcEmulation() {
    if (g_emulation_on) {
        g_emu_a.end();
    }
    g_emulation_on = false;
    g_emu_state = EmulationLayerA::State::None;
}

static bool restoreNfcReader() {
    stopNfcEmulation();
    g_ready = initializeNfcUnit(false);
    g_last_msg = g_ready ? "reader ready" : "unit begin failed";
    return g_ready;
}

static void drawTitleBar(const char* right) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_HELP_EDGE, APP_HELP_EDGE);
    M5Cardputer.Display.print("NFC ST25R3916");
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    const int w = M5Cardputer.Display.textWidth(right);
    M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - APP_HELP_EDGE - w, APP_HELP_EDGE);
    M5Cardputer.Display.print(right);
}

static int resultPageCount() {
    return 1 + (static_cast<int>(g_dump_rows.size()) + NFC_DUMP_ROWS_PER_PAGE - 1) /
                   NFC_DUMP_ROWS_PER_PAGE;
}

static void drawOperationBadge(const int x, const int y) {
    if (g_operation == NfcOperation::NONE) {
        return;
    }
    auto& d = M5Cardputer.Display;
    const uint16_t color = g_operation == NfcOperation::READ ? APP_COLOR_OK : APP_COLOR_ERROR;
    const char letter = g_operation == NfcOperation::READ ? 'R' : 'W';
    d.fillRoundRect(x, y, 22, 22, 3, color);
    d.setTextSize(2);
    d.setTextColor(BLACK, color);
    d.setCursor(x + 5, y + 4);
    d.print(letter);
}

static void drawResultFooter() {
    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_HELP_EDGE;
    if (resultPageCount() > 1) {
        cx += drawArrowUpDownFlatBadge(cx, hint_y, 1);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(cx, hint_y + 1);
        M5Cardputer.Display.print(" ");
        cx += M5Cardputer.Display.textWidth(" ");
    }
    const KeyHintItem hints[] = {
        {'r', "scan"},
        {'w', "write"},
        {'e', "emu"},
        {'y', "hist"},
    };
    drawKeyHintsRow(cx, hint_y, hints, 4, 1, APP_COLOR_HINT);
    drawHelpHintRight("help");
}

static void drawMainView() {
    int y = APP_HELP_EDGE + 13;
    constexpr int x = APP_HELP_EDGE;
    g_result_page = constrain(g_result_page, 0, resultPageCount() - 1);
    if (g_operation != NfcOperation::NONE) {
        drawOperationBadge(x, y);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.setCursor(x + 30, y + 4);
        M5Cardputer.Display.print(g_last_msg);
        drawResultFooter();
        return;
    }

    if (g_result_page == 0) {
        drawInfoLineAt(x, y, "status", g_last_msg.c_str(), 1);
        y += INFO_LINE_H;
        drawInfoLineAt(x, y, "uid", g_last_uid.c_str(), 1);
        y += INFO_LINE_H;
        drawInfoLineAt(x, y, "type", g_last_type.c_str(), 1);
        y += INFO_LINE_H;
        drawInfoLineAt(x, y, "card", g_last_meta1.c_str(), 1);
        y += INFO_LINE_H;
        drawInfoLineAt(x, y, "mem", g_last_meta2.c_str(), 1);
        y += INFO_LINE_H;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(g_last_all_zero ? APP_COLOR_WARN : APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(g_last_ndef);
    } else {
        const int first = (g_result_page - 1) * NFC_DUMP_ROWS_PER_PAGE;
        M5Cardputer.Display.setTextSize(1);
        for (int i = 0; i < NFC_DUMP_ROWS_PER_PAGE; ++i) {
            const int idx = first + i;
            if (idx >= static_cast<int>(g_dump_rows.size())) {
                break;
            }
            const NfcDumpRow& row = g_dump_rows[idx];
            M5Cardputer.Display.setTextColor(row.readable ? APP_COLOR_LABEL : APP_COLOR_ERROR,
                                             BLACK);
            M5Cardputer.Display.setCursor(x, y);
            M5Cardputer.Display.printf("%03u ", row.block);
            M5Cardputer.Display.setTextColor(row.readable ? APP_COLOR_VALUE : APP_COLOR_ERROR,
                                             BLACK);
            M5Cardputer.Display.print(row.hex);
            y += 9;
            M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
            M5Cardputer.Display.setCursor(x + 24, y);
            M5Cardputer.Display.print(row.ascii);
            y += 13;
        }
    }
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    char page[20];
    snprintf(page, sizeof(page), "%d/%d", g_result_page + 1, resultPageCount());
    const int page_w = M5Cardputer.Display.textWidth(page);
    M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - APP_HELP_EDGE - page_w,
                                  APP_HELP_EDGE + 13);
    M5Cardputer.Display.print(page);
    drawResultFooter();
}

static void drawHistoryView() {
    constexpr int x = APP_HELP_EDGE;
    int y = APP_HELP_EDGE + 12;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    char title[32];
    snprintf(title, sizeof(title), "records:%d", g_history_count);
    M5Cardputer.Display.print(title);
    y += 10;

    const int visible = 6;
    int start = 0;
    if (g_history_sel >= visible) {
        start = g_history_sel - visible + 1;
    }
    for (int i = 0; i < visible; ++i) {
        const int idx = start + i;
        if (idx >= g_history_count) {
            break;
        }
        const int ring = (g_history_start + idx) % NFC_HISTORY_MAX;
        const NfcRecord& rec = g_history[ring];
        const bool selected = idx == g_history_sel;
        if (selected) {
            M5Cardputer.Display.fillRect(x, y - 1, M5Cardputer.Display.width() - x * 2, 10, YELLOW);
        }
        M5Cardputer.Display.setTextColor(selected ? BLACK : APP_COLOR_HINT, selected ? YELLOW : BLACK);
        M5Cardputer.Display.setCursor(x, y);
        char line[64];
        snprintf(line, sizeof(line), "%02d %s %s", idx + 1, rec.uid.c_str(), rec.type.c_str());
        M5Cardputer.Display.print(line);
        y += 10;
    }

    const NfcRecord* rec = selectedRecord();
    if (rec != nullptr) {
        y += 2;
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print("selected data:");
        y += 10;
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(rec->data);
    }

    const int hint_y = M5Cardputer.Display.height() - 12;
    int cx = APP_HELP_EDGE;
    cx += drawArrowUpDownFlatBadge(cx, hint_y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y + 1);
    M5Cardputer.Display.print("sel ");
    cx += M5Cardputer.Display.textWidth("sel ");
    cx += drawKeyBadge(cx, hint_y, 'w', 1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, hint_y + 1);
    M5Cardputer.Display.print("write ");
    drawHelpHintRight("back");
}

static const char* emulationStateName(const EmulationLayerA::State state) {
    switch (state) {
        case EmulationLayerA::State::Off:
            return "OFF";
        case EmulationLayerA::State::Idle:
            return "IDLE";
        case EmulationLayerA::State::Ready:
            return "READY";
        case EmulationLayerA::State::Active:
            return "ACTIVE";
        case EmulationLayerA::State::Halt:
            return "HALT";
        default:
            return "--";
    }
}

static void drawEmulationView() {
    constexpr int x = APP_HELP_EDGE;
    int y = APP_HELP_EDGE + 16;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(g_emulation_on ? APP_COLOR_OK : APP_COLOR_ERROR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(emulationStateName(g_emu_state));
    y += 22;
    drawInfoLineAt(x, y, "uid", g_emulation_on ? g_emu_picc.uidAsString().c_str() : "--", 1);
    y += INFO_LINE_H;
    drawInfoLineAt(x, y, "type", "MIFARE Ultralight", 1);
    y += INFO_LINE_H;
    drawInfoLineAt(x, y, "NDEF", g_write_payload.c_str(), 1);
    y += INFO_LINE_H;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print("Tap a phone to read this text tag");

    const int hint_y = M5Cardputer.Display.height() - 12;
    const KeyHintItem hints[] = {
        {'e', "reader"},
    };
    drawKeyHintsRow(x, hint_y, hints, 1, 1, APP_COLOR_HINT);
    drawHelpHintRight("help");
}

static void drawHelpPage() {
    static const AppHelpLine kLines[] = {
        appHelpTextColored("Reader/Writer", APP_COLOR_LABEL),
        appHelpKey('r', "scan until card found"),
        appHelpKey('w', "write payload to card"),
        appHelpKey('o', "toggle save history"),
        appHelpKey('y', "open read history"),
        appHelpArrows("result / dump pages"),
        appHelpKey('e', "NDEF tag emulation"),
        appHelpTextColored("History", APP_COLOR_LABEL),
        appHelpArrows("pick history item"),
        appHelpKey('w', "write selected history"),
        appHelpTextColored("Hardware", APP_COLOR_LABEL),
        appHelpText("Needs Unit NFC (ST25R3916)"),
        appHelpText("Not Unit RFID U031/0x28"),
        appHelpText("13.56MHz cards only"),
        appHelpText("Protected blocks need matching keys"),
    };
    drawAppHelpLines("NFC", kLines, static_cast<int>(sizeof(kLines) / sizeof(kLines[0])), g_help_page);
}

static void drawNfcApp() {
    if (g_help_visible) {
        drawHelpPage();
        return;
    }
    M5Cardputer.Display.fillScreen(BLACK);
    drawTitleBar(g_view == NfcView::MAIN
                     ? "READER"
                     : (g_view == NfcView::HISTORY ? "HISTORY" : "EMULATION"));
    if (g_view == NfcView::MAIN) {
        drawMainView();
    } else if (g_view == NfcView::HISTORY) {
        drawHistoryView();
    } else {
        drawEmulationView();
    }
}

} // namespace

void enterNfcApp() {
    g_help_visible = false;
    g_help_page = 0;
    g_read_scanning = false;
    g_emulation_on = false;
    g_view = NfcView::MAIN;
    g_operation = NfcOperation::NONE;
    g_result_page = 0;
    g_last_msg = "init...";
    g_last_uid = "--";
    g_last_type = "--";
    g_last_data = "--";
    g_last_meta1 = "--";
    g_last_meta2 = "--";
    g_last_ndef = "NDEF: --";
    g_last_all_zero = false;
    g_dump_rows.clear();
    g_dump_bytes.clear();
    clearLog();
    pushLog("> init");

    // Cardputer Grove 走 Ex_I2C（G1/G2），与 Radio / I2C Scan 一致；勿用 Wire 抢同一组脚。
    g_ready = initializeNfcUnit(false);
    g_last_msg = g_ready ? "ready (Unit NFC)" : "unit begin failed";
    pushLog(g_ready ? "> unit ready" : "> unit begin fail");
    drawNfcApp();
}

void leaveNfcApp() {
    stopReadScan();
    stopNfcEmulation();
    g_nfc_a.deactivate();
    g_units = m5::unit::UnitUnified{};
    g_ready = false;
}

void updateNfcApp() {
    if (g_ready) {
        g_units.update();
    }
    if (g_emulation_on) {
        g_emu_a.update();
        const EmulationLayerA::State state = g_emu_a.state();
        if (state != g_emu_state) {
            g_emu_state = state;
            drawNfcApp();
        }
        return;
    }
    pollReadScan();
}

void handleNfcApp(const Keyboard_Class::KeysState& status) {
    int help_delta = getHelpNavDelta(status);
    if (g_help_visible) {
        const int line_count = 15;
        const int pages = appHelpPageCount(line_count);
        g_help_page = applyHelpPageDelta(g_help_page, pages, help_delta);
        for (const char c : status.word) {
            if (c == 'h' || c == 'H') {
                g_help_visible = false;
                drawNfcApp();
                return;
            }
        }
        drawNfcApp();
        return;
    }

    if (g_view == NfcView::HISTORY) {
        if (help_delta != 0 && g_history_count > 0) {
            g_history_sel = constrain(g_history_sel + help_delta, 0, g_history_count - 1);
            drawNfcApp();
            return;
        }
    } else if (g_view == NfcView::MAIN && help_delta != 0 && resultPageCount() > 1) {
        g_result_page =
            constrain(g_result_page + help_delta, 0, resultPageCount() - 1);
        drawNfcApp();
        return;
    }

    for (const char raw : status.word) {
        const char c = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
        if (c == 'h') {
            if (g_read_scanning) {
                stopReadScan();
            }
            if (g_view == NfcView::HISTORY) {
                g_view = NfcView::MAIN;
            } else {
                g_help_visible = true;
                g_help_page = 0;
            }
            drawNfcApp();
            return;
        }
        if (c == 'e') {
            if (g_view == NfcView::EMULATION) {
                restoreNfcReader();
                g_view = NfcView::MAIN;
            } else {
                g_view = NfcView::EMULATION;
                startNfcEmulation();
            }
            drawNfcApp();
            return;
        }
        if (c == 'y') {
            if (g_read_scanning) {
                stopReadScan();
                g_last_msg = "scan cancelled";
            }
            if (g_view == NfcView::EMULATION) {
                continue;
            }
            g_view = g_view == NfcView::MAIN ? NfcView::HISTORY : NfcView::MAIN;
            drawNfcApp();
            return;
        }
        if (c == 'o' && g_view == NfcView::MAIN) {
            g_record_on_read = !g_record_on_read;
            g_last_msg = g_record_on_read ? "record on" : "record off";
            pushLogf("> record %s", g_record_on_read ? "on" : "off");
            drawNfcApp();
            return;
        }
        if (c == 'r' && g_view == NfcView::MAIN) {
            if (g_read_scanning) {
                stopReadScan();
                g_last_msg = "scan cancelled";
                pushLog("> scan cancelled");
            } else {
                doReadCurrentCard();
            }
            drawNfcApp();
            return;
        }
        if (c == 'w' && g_view != NfcView::EMULATION) {
            if (g_view == NfcView::MAIN) {
                doWriteCurrentCard(g_write_payload);
            } else {
                const NfcRecord* rec = selectedRecord();
                if (rec != nullptr) {
                    doWriteCurrentCard(rec->data);
                } else {
                    g_last_msg = "no history";
                }
            }
            drawNfcApp();
            return;
        }
    }
}

bool closeNfcHelp() {
    if (!g_help_visible) {
        return false;
    }
    g_help_visible = false;
    drawNfcApp();
    return true;
}

bool isNfcHelpVisible() {
    return g_help_visible;
}
