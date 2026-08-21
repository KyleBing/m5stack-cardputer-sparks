#include "app_nfc.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_device_icons.h"

#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <LittleFS.h>
#include <wiring/m5_unit_unified_wiring.hpp>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace m5::nfc;
using namespace m5::nfc::a;
using namespace m5::nfc::a::mifare;

enum class NfcView {
    MAIN,
    HISTORY,
    DETAIL,
    RENAME,
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

enum class NfcBlockStatus : uint8_t {
    Idle = 0, // 未读 / 未尝试
    Ok = 1,   // 读成功
    Fail = 2, // 读失败
    Skip = 3, // 跳过（如 Classic trailer 不入库）
};

struct NfcRecord {
    String name;
    String uid;
    String type;
    String data;
    String meta1;
    String meta2;
    String key; // Classic 默认密钥探测：default key / Auth Error / n/a
    String ndef;
    bool all_zero;
    uint8_t picc_type = 0; // m5::nfc::a::Type
    uint16_t user_size = 0;
    uint16_t total_size = 0;
    uint16_t block_count = 0;
    uint32_t ms;
    std::vector<NfcDumpRow> rows;
    std::vector<uint8_t> bytes;
};

enum class ClassicKeyProbe {
    Skipped,
    DefaultOk,
    AuthError,
    ReadFail,
};

static constexpr int NFC_HISTORY_MAX = 12;
static constexpr int NFC_HISTORY_PAD = 2;
static constexpr int NFC_HISTORY_ROW_H = 8 + NFC_HISTORY_PAD * 2;
static constexpr int NFC_HISTORY_VISIBLE = 7;
static constexpr int NFC_HISTORY_LIST_W = 220;
static constexpr int NFC_NAME_MAX = 28;
static constexpr uint32_t NFC_STORE_MAGIC = 0x3243464E; // "NFC2"
static constexpr uint16_t NFC_STORE_VERSION = 4;
static constexpr char NFC_STORE_PATH[] = "/nfc_records.bin";
static constexpr char kDefaultWritePayload[] = "Cardputer NFC";
static constexpr int NFC_DUMP_ROWS_PER_PAGE = 4;
static constexpr size_t NFC_EMU_MEMORY_MAX = 1024; // NTAG216 ≈ 924B
static constexpr uint8_t NFC_CLASSIC_PROBE_BLOCK = 4; // sector 1 block 0
static constexpr int NFC_BLK_CELL_MIN = 3;
static constexpr int NFC_BLK_CELL_MAX = 6;
static constexpr int NFC_BLK_GAP = 1;
static constexpr uint16_t NFC_BLK_IDLE = 0x9492; // 与米家 pager idle 同色 #929292
static constexpr int NFC_ICON_W = 40;
static constexpr int NFC_ICON_H = 18;
static constexpr int NFC_LEFT_W = 48; // 左侧仅图标+状态，不进内容区
static constexpr int NFC_CONTENT_X = APP_HELP_EDGE + NFC_LEFT_W;
static constexpr char NFC_ICON_IDLE[] = "/icon/nfc_signal.png";
static constexpr char NFC_ICON_ACTIVE[] = "/icon/nfc_signal_active.png";
static constexpr char NFC_ICON_EMU[] = "/icon/nfc_signal_emu.png";

m5::unit::UnitUnified g_units;
m5::unit::UnitNFC g_unit;
NFCLayerA g_nfc_a{g_unit};
EmulationLayerA g_emu_a{g_unit};

bool g_ready = false;
bool g_help_visible = false;
int g_help_page = 0;
bool g_read_scanning = false;
bool g_emulation_on = false;
bool g_emu_from_record = false;
NfcView g_view = NfcView::MAIN;
NfcView g_emu_return_view = NfcView::MAIN;
NfcOperation g_operation = NfcOperation::NONE;
bool g_record_on_read = true;
String g_write_payload = kDefaultWritePayload;
String g_last_msg = "ready";
String g_last_uid = "--";
String g_last_type = "--";
String g_last_data = "--";
String g_last_meta1 = "--";
String g_last_meta2 = "--";
String g_last_key = "--";
String g_last_ndef = "NDEF: --";
bool g_last_all_zero = false;
Type g_last_picc_type = Type::Unknown;
uint16_t g_last_user_size = 0;
uint16_t g_last_total_size = 0;
uint16_t g_last_block_count = 0;
int g_result_page = 0;
std::vector<NfcDumpRow> g_dump_rows;
std::vector<uint8_t> g_dump_bytes;
std::vector<NfcBlockStatus> g_block_status;
PICC g_emu_picc{};
std::vector<uint8_t> g_emu_memory;
String g_emu_label = kDefaultWritePayload;
EmulationLayerA::State g_emu_state = EmulationLayerA::State::None;
std::array<NfcRecord, NFC_HISTORY_MAX> g_history{};
int g_history_count = 0;
int g_history_start = 0;
int g_history_sel = 0;
String g_rename_text;
int g_rename_cursor = 0;
bool g_store_loaded = false;

// 交互过程 + 往复数据日志（可滚动）
static constexpr int NFC_LOG_MAX = 32;
static constexpr int NFC_LOG_LINE_MAX = 48;
static constexpr int NFC_LOG_VISIBLE = 7;
std::array<String, NFC_LOG_MAX> g_log{};
int g_log_count = 0;
int g_log_start = 0;
int g_log_scroll = 0; // 0 = 显示最新

static void drawNfcApp();
static void rebuildBlockStatusFromRows();

static bool writeU16(File& file, const uint16_t value) {
    return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
}

static bool writeU32(File& file, const uint32_t value) {
    return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
}

static bool readU16(File& file, uint16_t& value) {
    return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
}

static bool readU32(File& file, uint32_t& value) {
    return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
}

static bool writeString(File& file, const String& value) {
    const uint16_t len = static_cast<uint16_t>(min(static_cast<size_t>(UINT16_MAX), value.length()));
    return writeU16(file, len) &&
           (len == 0 || file.write(reinterpret_cast<const uint8_t*>(value.c_str()), len) == len);
}

static bool readString(File& file, String& value, const uint16_t max_len = 4096) {
    uint16_t len = 0;
    if (!readU16(file, len) || len > max_len) {
        return false;
    }
    value = "";
    value.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        const int c = file.read();
        if (c < 0) {
            return false;
        }
        value += static_cast<char>(c);
    }
    return true;
}

static String defaultRecordName(const String& type, const String& uid, const String& ndef) {
    String name;
    if (ndef.startsWith("NDEF: Text ")) {
        name = ndef.substring(11);
    } else if (ndef.startsWith("NDEF: URI ")) {
        name = ndef.substring(10);
    }
    if (name.length() == 0 || name == "none") {
        name = type;
        if (name.length() == 0 || name == "--") {
            name = "NFC";
        }
        name += " ";
        name += uid.length() > 8 ? uid.substring(uid.length() - 8) : uid;
    }
    if (name.length() > NFC_NAME_MAX) {
        name.remove(NFC_NAME_MAX);
    }
    return name;
}

static bool saveHistory() {
    if (!LittleFS.begin(false)) {
        return false;
    }
    const String temp_path = String(NFC_STORE_PATH) + ".tmp";
    File file = LittleFS.open(temp_path, "w");
    bool ok = file && writeU32(file, NFC_STORE_MAGIC) && writeU16(file, NFC_STORE_VERSION) &&
              writeU16(file, static_cast<uint16_t>(g_history_count));
    for (int i = 0; ok && i < g_history_count; ++i) {
        const NfcRecord& rec = g_history[(g_history_start + i) % NFC_HISTORY_MAX];
        ok = writeString(file, rec.name) && writeString(file, rec.uid) &&
             writeString(file, rec.type) && writeString(file, rec.data) &&
             writeString(file, rec.meta1) && writeString(file, rec.meta2) &&
             writeString(file, rec.key) && writeString(file, rec.ndef) && writeU32(file, rec.ms);
        const uint8_t zero = rec.all_zero ? 1 : 0;
        ok = ok && file.write(&zero, 1) == 1 && file.write(&rec.picc_type, 1) == 1 &&
             writeU16(file, rec.user_size) && writeU16(file, rec.total_size) &&
             writeU16(file, rec.block_count) &&
             writeU16(file, static_cast<uint16_t>(rec.rows.size())) &&
             writeU32(file, static_cast<uint32_t>(rec.bytes.size()));
        for (const NfcDumpRow& row : rec.rows) {
            const uint8_t readable = row.readable ? 1 : 0;
            ok = ok && writeU16(file, row.block) && file.write(&readable, 1) == 1 &&
                 writeString(file, row.hex) && writeString(file, row.ascii);
        }
        ok = ok && (rec.bytes.empty() ||
                    file.write(rec.bytes.data(), rec.bytes.size()) == rec.bytes.size());
    }
    if (file) {
        file.flush();
        file.close();
    }
    if (!ok) {
        LittleFS.remove(temp_path);
        return false;
    }
    LittleFS.remove(NFC_STORE_PATH);
    return LittleFS.rename(temp_path, NFC_STORE_PATH);
}

static void clearHistory() {
    for (NfcRecord& rec : g_history) {
        rec = {};
    }
    g_history_count = 0;
    g_history_start = 0;
    g_history_sel = 0;
}

static bool loadHistory() {
    clearHistory();
    if (!LittleFS.begin(false)) {
        return false;
    }
    g_store_loaded = true;
    File file = LittleFS.open(NFC_STORE_PATH, "r");
    if (!file) {
        return true;
    }
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t count = 0;
    bool ok = readU32(file, magic) && readU16(file, version) && readU16(file, count) &&
              magic == NFC_STORE_MAGIC && version >= 1 && version <= NFC_STORE_VERSION &&
              count <= NFC_HISTORY_MAX;
    for (uint16_t i = 0; ok && i < count; ++i) {
        NfcRecord& rec = g_history[i];
        uint8_t zero = 0;
        uint16_t row_count = 0;
        uint32_t byte_count = 0;
        ok = readString(file, rec.name, NFC_NAME_MAX) && readString(file, rec.uid, 128) &&
             readString(file, rec.type, 128) && readString(file, rec.data) &&
             readString(file, rec.meta1, 256) && readString(file, rec.meta2, 256);
        rec.key = "--";
        if (ok && version >= 3) {
            ok = readString(file, rec.key, 128);
        }
        ok = ok && readString(file, rec.ndef, 4096) && readU32(file, rec.ms) &&
             file.read(&zero, 1) == 1;
        rec.picc_type = 0;
        rec.user_size = 0;
        rec.total_size = 0;
        rec.block_count = 0;
        if (ok && version >= 2) {
            ok = file.read(&rec.picc_type, 1) == 1;
        }
        if (ok && version >= 4) {
            ok = readU16(file, rec.user_size) && readU16(file, rec.total_size) &&
                 readU16(file, rec.block_count);
        }
        ok = ok && readU16(file, row_count) && readU32(file, byte_count) && row_count <= 512 &&
             byte_count <= 65536;
        rec.all_zero = zero != 0;
        rec.rows.reserve(row_count);
        for (uint16_t row = 0; ok && row < row_count; ++row) {
            NfcDumpRow dump{};
            uint8_t readable = 0;
            ok = readU16(file, dump.block) && file.read(&readable, 1) == 1 &&
                 readString(file, dump.hex, 512) && readString(file, dump.ascii, 512);
            dump.readable = readable != 0;
            if (ok) {
                rec.rows.push_back(dump);
            }
        }
        rec.bytes.resize(byte_count);
        ok = ok && (byte_count == 0 || file.read(rec.bytes.data(), byte_count) == byte_count);
        if (ok && rec.block_count == 0 && !rec.rows.empty()) {
            uint16_t max_blk = 0;
            for (const NfcDumpRow& row : rec.rows) {
                if (row.block > max_blk) {
                    max_blk = row.block;
                }
            }
            rec.block_count = static_cast<uint16_t>(max_blk + 1);
        }
        if (ok && (rec.user_size == 0 || rec.total_size == 0) && rec.meta2.length() > 0) {
            unsigned usr = 0;
            unsigned tot = 0;
            if (sscanf(rec.meta2.c_str(), "usr:%u tot:%u", &usr, &tot) >= 2) {
                if (rec.user_size == 0) {
                    rec.user_size = static_cast<uint16_t>(usr);
                }
                if (rec.total_size == 0) {
                    rec.total_size = static_cast<uint16_t>(tot);
                }
            }
        }
        if (rec.name.length() == 0) {
            rec.name = defaultRecordName(rec.type, rec.uid, rec.ndef);
        }
    }
    file.close();
    if (!ok) {
        clearHistory();
        return false;
    }
    g_history_count = count;
    g_history_sel = count > 0 ? count - 1 : 0;
    return true;
}

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

// UID 相同的卡更新原记录，避免 Flash 中保存重复数据。
static void pushHistory(const String& uid, const String& type, const String& data) {
    int logical = -1;
    for (int i = 0; i < g_history_count; ++i) {
        if (g_history[(g_history_start + i) % NFC_HISTORY_MAX].uid == uid) {
            logical = i;
            break;
        }
    }
    if (logical < 0) {
        if (g_history_count < NFC_HISTORY_MAX) {
            logical = g_history_count++;
        } else {
            g_history_start = (g_history_start + 1) % NFC_HISTORY_MAX;
            logical = g_history_count - 1;
        }
    }
    NfcRecord& rec = g_history[(g_history_start + logical) % NFC_HISTORY_MAX];
    const String old_name = rec.name;
    rec.uid = uid;
    rec.type = type;
    rec.data = data;
    rec.meta1 = g_last_meta1;
    rec.meta2 = g_last_meta2;
    rec.key = g_last_key;
    rec.ndef = g_last_ndef;
    rec.all_zero = g_last_all_zero;
    rec.picc_type = static_cast<uint8_t>(g_last_picc_type);
    rec.user_size = g_last_user_size;
    rec.total_size = g_last_total_size;
    rec.block_count = g_last_block_count;
    rec.ms = millis();
    rec.rows = g_dump_rows;
    rec.bytes = g_dump_bytes;
    rec.name = old_name.length() > 0 ? old_name : defaultRecordName(type, uid, g_last_ndef);
    g_history_sel = logical;
    saveHistory();
}

static const NfcRecord* selectedRecord() {
    if (g_history_count <= 0 || g_history_sel < 0 || g_history_sel >= g_history_count) {
        return nullptr;
    }
    const int idx = (g_history_start + g_history_sel) % NFC_HISTORY_MAX;
    return &g_history[idx];
}

static NfcRecord* selectedRecordMutable() {
    return const_cast<NfcRecord*>(selectedRecord());
}

static void showSelectedRecord() {
    const NfcRecord* rec = selectedRecord();
    if (rec == nullptr) {
        return;
    }
    g_last_uid = rec->uid;
    g_last_type = rec->type;
    g_last_data = rec->data;
    g_last_meta1 = rec->meta1;
    g_last_meta2 = rec->meta2;
    g_last_key = rec->key.length() > 0 ? rec->key : String("--");
    g_last_ndef = rec->ndef;
    g_last_all_zero = rec->all_zero;
    g_last_picc_type = static_cast<Type>(rec->picc_type);
    g_last_user_size = rec->user_size;
    g_last_total_size = rec->total_size;
    g_last_block_count = rec->block_count;
    g_dump_rows = rec->rows;
    g_dump_bytes = rec->bytes;
    rebuildBlockStatusFromRows();
    g_result_page = 0;
    g_view = NfcView::DETAIL;
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

static void setBlockStatus(const uint16_t block, const NfcBlockStatus status) {
    if (block >= g_block_status.size()) {
        g_block_status.resize(static_cast<size_t>(block) + 1, NfcBlockStatus::Idle);
    }
    g_block_status[block] = status;
}

static void rebuildBlockStatusFromRows() {
    g_block_status.assign(g_last_block_count, NfcBlockStatus::Skip);
    for (const NfcDumpRow& row : g_dump_rows) {
        if (row.block >= g_block_status.size()) {
            g_block_status.resize(static_cast<size_t>(row.block) + 1, NfcBlockStatus::Skip);
        }
        g_block_status[row.block] = row.readable ? NfcBlockStatus::Ok : NfcBlockStatus::Fail;
    }
    if (g_last_block_count == 0 && !g_block_status.empty()) {
        g_last_block_count = static_cast<uint16_t>(g_block_status.size());
    }
}

static bool isClassicTrailer(const uint16_t block) {
    // MIFARE 4K 在 2K 以后每 16 块一个扇区，其余区域每 4 块一个扇区。
    return block < 128 ? (block % 4) == 3 : (block % 16) == 15;
}

static String parseNdefSummary(const std::vector<uint8_t>& bytes, size_t start = 0) {
    if (start >= bytes.size()) {
        return "NDEF: none";
    }
    // Type2 user area often begins with Capability Container (E1 10 ..).
    if (start + 4 <= bytes.size() && bytes[start] == 0xE1 && bytes[start + 1] == 0x10) {
        start += 4;
    }
    size_t tlv = start;
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
static bool readCardData(PICC& picc, String& out) {
    out = "--";
    g_dump_rows.clear();
    g_dump_bytes.clear();
    g_block_status.clear();
    g_last_block_count = picc.blocks;
    if (g_last_block_count > 0) {
        g_block_status.assign(g_last_block_count, NfcBlockStatus::Idle);
    }
    bool any_read = false;
    if (picc.isMifareClassic()) {
        const uint16_t n = g_last_block_count > 0 ? g_last_block_count : 64;
        if (g_block_status.size() < n) {
            g_block_status.assign(n, NfcBlockStatus::Idle);
            g_last_block_count = n;
        }
        for (uint16_t block = 0; block < n; ++block) {
            uint8_t buf[16]{};
            bool ok = g_nfc_a.mifareClassicAuthenticateA(static_cast<uint8_t>(block),
                                                         classic::DEFAULT_KEY) &&
                      g_nfc_a.read16(buf, static_cast<uint8_t>(block));
            if (!ok) {
                // Auth 失败后会话常失效，恢复后再记失败，避免拖垮后续扇区。
                g_nfc_a.reactivate(picc);
            }
            setBlockStatus(block, ok ? NfcBlockStatus::Ok : NfcBlockStatus::Fail);
            if (isClassicTrailer(block)) {
                // trailer 含密钥，不入库；状态仍标绿/红
                continue;
            }
            if (!ok) {
                appendUnreadableRow(block);
                continue;
            }
            appendDumpRow(block, buf, sizeof(buf));
            any_read = true;
        }
    } else if (picc.supportsNFC()) {
        // Full Type2 image (UID/CC/user) so history can be emulated 1:1.
        const uint16_t page_count = picc.blocks > 0 ? picc.blocks : 0;
        for (uint16_t page = 0; page < page_count; ++page) {
            uint8_t buf[4]{};
            if (!g_nfc_a.read4(buf, static_cast<uint8_t>(page))) {
                setBlockStatus(page, NfcBlockStatus::Fail);
                appendUnreadableRow(page);
                continue;
            }
            setBlockStatus(page, NfcBlockStatus::Ok);
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
                const uint16_t block = picc.firstUserBlock() + offset / 16;
                setBlockStatus(block, NfcBlockStatus::Ok);
                appendDumpRow(block, buf.data() + offset, row_len);
            }
            any_read = true;
        } else {
            setBlockStatus(picc.firstUserBlock(), NfcBlockStatus::Fail);
            appendUnreadableRow(picc.firstUserBlock());
        }
    }
    if (g_last_block_count == 0 && !g_block_status.empty()) {
        g_last_block_count = static_cast<uint16_t>(g_block_status.size());
    }
    if (!any_read) {
        g_last_ndef = "NDEF: unavailable";
        g_last_all_zero = false;
        return false;
    }
    g_last_all_zero = true;
    size_t zero_from = 0;
    if (picc.supportsNFC() && picc.unitSize() > 0) {
        zero_from = static_cast<size_t>(picc.firstUserBlock()) * picc.unitSize();
    }
    for (size_t i = zero_from; i < g_dump_bytes.size(); ++i) {
        if (g_dump_bytes[i] != 0) {
            g_last_all_zero = false;
            break;
        }
    }
    size_t ndef_off = zero_from;
    g_last_ndef = g_last_all_zero ? "NDEF: blank (all 00)" : parseNdefSummary(g_dump_bytes, ndef_off);
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
    snprintf(line1, sizeof(line1), "ATQA : %04X SAK : %02X UID : %u", picc.atqa, picc.sak,
             picc.size);
    g_last_meta1 = line1;
    char line2[64];
    snprintf(line2, sizeof(line2), "usr:%u tot:%u blk:%u-%u", picc.userAreaSize(), picc.totalSize(),
             picc.firstUserBlock(), picc.lastUserBlock());
    g_last_meta2 = line2;
    g_last_user_size = picc.userAreaSize();
    g_last_total_size = picc.totalSize();
    if (g_last_block_count == 0 && picc.blocks > 0) {
        g_last_block_count = picc.blocks;
    }
    g_last_uid = picc.uidAsString().c_str();
    g_last_type = picc.typeAsString().c_str();
    g_last_picc_type = picc.type;
    g_last_data = data_text;
}

// 物业卡探测：用默认 KeyA(FFFFFFFFFFFF) 认证并读 sector1/blk4。
// 成功 → 未改密；Auth Error → 已改密。非 Classic 跳过。
static ClassicKeyProbe probeClassicDefaultKeyBlk4(const PICC& picc) {
    g_last_key = "--";
    if (!picc.isMifareClassic()) {
        pushLog("> key probe skip (not Classic)");
        g_last_key = "n/a (not Classic)";
        return ClassicKeyProbe::Skipped;
    }

    pushLogf("> keyA blk%u FFFFFFFFFFFF", NFC_CLASSIC_PROBE_BLOCK);
    if (!g_nfc_a.mifareClassicAuthenticateA(NFC_CLASSIC_PROBE_BLOCK, classic::DEFAULT_KEY)) {
        pushLog("> Auth Error");
        g_last_key = "Auth Error";
        return ClassicKeyProbe::AuthError;
    }
    pushLog("> authA ok");

    uint8_t buf[16]{};
    if (!g_nfc_a.read16(buf, NFC_CLASSIC_PROBE_BLOCK)) {
        pushLog("> blk4 read fail");
        g_last_key = "auth ok / read fail";
        return ClassicKeyProbe::ReadFail;
    }
    pushLogf("> blk4 %s", bytesToHex(buf, sizeof(buf)).c_str());
    g_last_key = "default key";
    return ClassicKeyProbe::DefaultOk;
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

// 读到卡：先展示 UID/类型；能 activate 则先做 Classic 默认密钥探测，再读用户区。
static void finishReadCard(PICC& picc) {
    String data = "--";
    const char* msg = "uid ok";
    pushLog("> reactivate...");
    if (!g_nfc_a.reactivate(picc)) {
        pushLog("> reactivate fail");
        g_last_key = "--";
        applyReadResult(picc, data, "uid ok / act fail");
        pushLog("> done uid ok / act fail");
        g_nfc_a.deactivate();
        pushLog("> deactivate");
        g_operation = NfcOperation::NONE;
        g_result_page = 0;
        return;
    }
    pushLog("> reactivate ok");

    const ClassicKeyProbe key_probe = probeClassicDefaultKeyBlk4(picc);
    if (key_probe == ClassicKeyProbe::AuthError || key_probe == ClassicKeyProbe::ReadFail) {
        // Auth 失败后会话常失效，整卡 dump 前重新选卡。
        pushLog("> reactivate after key probe...");
        if (!g_nfc_a.reactivate(picc)) {
            pushLog("> reactivate fail");
        } else {
            pushLog("> reactivate ok");
        }
    }

    const bool dump_ok = readCardData(picc, data);
    if (key_probe == ClassicKeyProbe::AuthError) {
        msg = "Auth Error";
    } else if (key_probe == ClassicKeyProbe::DefaultOk) {
        msg = dump_ok ? "default key" : "default key / dump fail";
    } else if (key_probe == ClassicKeyProbe::ReadFail) {
        msg = "auth ok / blk4 fail";
    } else {
        msg = dump_ok ? "read ok" : "uid ok / data fail";
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
    g_last_key = "--";
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

static int hexNibble(const char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool parseUidHex(const String& hex, uint8_t* out, uint8_t& out_len) {
    if (out == nullptr || hex.length() == 0 || (hex.length() % 2) != 0) {
        return false;
    }
    const size_t len = hex.length() / 2;
    if (len != 4 && len != 7 && len != 10) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const int hi = hexNibble(hex[static_cast<unsigned>(i * 2)]);
        const int lo = hexNibble(hex[static_cast<unsigned>(i * 2 + 1)]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    out_len = static_cast<uint8_t>(len);
    return true;
}

static bool hexToBytes(const String& hex, uint8_t* out, const size_t out_len) {
    if (out == nullptr || hex.length() < out_len * 2) {
        return false;
    }
    for (size_t i = 0; i < out_len; ++i) {
        const int hi = hexNibble(hex[static_cast<unsigned>(i * 2)]);
        const int lo = hexNibble(hex[static_cast<unsigned>(i * 2 + 1)]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

static Type typeFromTypeString(const String& s) {
    if (s.startsWith("NTAG 216")) {
        return Type::NTAG_216;
    }
    if (s.startsWith("NTAG 215")) {
        return Type::NTAG_215;
    }
    if (s.startsWith("NTAG 213")) {
        return Type::NTAG_213;
    }
    if (s.startsWith("NTAG 212")) {
        return Type::NTAG_212;
    }
    if (s.startsWith("NTAG 210u") || s.startsWith("NTAG 210μ")) {
        return Type::NTAG_210u;
    }
    if (s.startsWith("NTAG 210")) {
        return Type::NTAG_210;
    }
    if (s.startsWith("NTAG 203")) {
        return Type::NTAG_203;
    }
    if (s.startsWith("MIFARE Ultralight EV1 21")) {
        return Type::MIFARE_Ultralight_EV1_2;
    }
    if (s.startsWith("MIFARE Ultralight EV1 11")) {
        return Type::MIFARE_Ultralight_EV1_1;
    }
    if (s.startsWith("MIFARE Ultralight Nano")) {
        return Type::MIFARE_Ultralight_Nano;
    }
    if (s.startsWith("MIFARE UltralightC")) {
        return Type::MIFARE_UltralightC;
    }
    if (s.startsWith("MIFARE Ultralight")) {
        return Type::MIFARE_Ultralight;
    }
    if (s.startsWith("MIFARE Classic Mini")) {
        return Type::MIFARE_Classic_Mini;
    }
    if (s.startsWith("MIFARE Classic 4K") || s.indexOf("Classic 4K") >= 0) {
        return Type::MIFARE_Classic_4K;
    }
    if (s.startsWith("MIFARE Classic 2K") || s.indexOf("Classic 2K") >= 0) {
        return Type::MIFARE_Classic_2K;
    }
    if (s.startsWith("MIFARE Classic 1K") || s.indexOf("Classic 1K") >= 0 ||
        s.startsWith("MIFARE Classic")) {
        return Type::MIFARE_Classic_1K;
    }
    return Type::Unknown;
}

static Type recordType(const NfcRecord& rec) {
    if (rec.picc_type != 0) {
        return static_cast<Type>(rec.picc_type);
    }
    return typeFromTypeString(rec.type);
}

static bool canEmulateType(const Type t) {
    return is_ntag2(t) || is_mifare_ultralight(t);
}

// EmulationLayerA::begin only accepts exact Ultralight or NTAG2xx.
static Type emulationCompatibleType(const Type t) {
    if (is_ntag2(t) || t == Type::MIFARE_Ultralight) {
        return t;
    }
    if (!is_mifare_ultralight(t)) {
        return Type::Unknown;
    }
    const uint16_t pages = get_number_of_blocks(t);
    if (pages <= 16) {
        return Type::MIFARE_Ultralight;
    }
    if (pages <= 20) {
        return Type::NTAG_210u;
    }
    if (pages <= 40) {
        return Type::NTAG_212;
    }
    if (pages <= 42) {
        return Type::NTAG_203;
    }
    if (pages <= 45) {
        return Type::NTAG_213;
    }
    if (pages <= 135) {
        return Type::NTAG_215;
    }
    return Type::NTAG_216;
}

static void writeUidIntoMemory(uint8_t* mem, const size_t mem_len, const uint8_t* uid,
                               const uint8_t uid_len) {
    if (mem == nullptr || uid == nullptr || mem_len < 9) {
        return;
    }
    if (uid_len == 7) {
        memcpy(mem, uid, 3);
        mem[3] = uidBcc(uid, 3, 0x88);
        memcpy(mem + 4, uid + 3, 4);
        mem[8] = uidBcc(uid + 3, 4);
    } else if (uid_len == 4) {
        memcpy(mem, uid, 4);
        mem[4] = uidBcc(uid, 4);
    } else if (uid_len == 10 && mem_len >= 15) {
        memcpy(mem, uid, 3);
        mem[3] = uidBcc(uid, 3, 0x88);
        memcpy(mem + 4, uid + 3, 3);
        mem[7] = uidBcc(uid + 3, 3);
        memcpy(mem + 8, uid + 6, 4);
        mem[12] = uidBcc(uid + 6, 4);
    }
}

static bool buildEmuImageFromRecord(const NfcRecord& rec, const Type emu_type,
                                    std::vector<uint8_t>& mem) {
    const uint16_t pages = get_number_of_blocks(emu_type);
    if (pages == 0) {
        return false;
    }
    const size_t need = static_cast<size_t>(pages) * 4u;
    if (need == 0 || need > NFC_EMU_MEMORY_MAX) {
        return false;
    }
    mem.assign(need, 0);

    bool from_rows = false;
    for (const NfcDumpRow& row : rec.rows) {
        if (!row.readable || row.block >= pages) {
            continue;
        }
        uint8_t page[4]{};
        if (!hexToBytes(row.hex, page, sizeof(page))) {
            continue;
        }
        memcpy(mem.data() + static_cast<size_t>(row.block) * 4u, page, sizeof(page));
        from_rows = true;
    }

    if (!from_rows && !rec.bytes.empty()) {
        const Type src_type = recordType(rec);
        const uint16_t first = get_first_user_block(src_type != Type::Unknown ? src_type : emu_type);
        if (rec.bytes.size() == need) {
            memcpy(mem.data(), rec.bytes.data(), need);
        } else if (first * 4u + rec.bytes.size() <= need) {
            memcpy(mem.data() + static_cast<size_t>(first) * 4u, rec.bytes.data(),
                   rec.bytes.size());
        } else {
            const size_t n = rec.bytes.size() < need ? rec.bytes.size() : need;
            memcpy(mem.data(), rec.bytes.data(), n);
        }
    }

    uint8_t uid[10]{};
    uint8_t uid_len = 0;
    if (parseUidHex(rec.uid, uid, uid_len)) {
        writeUidIntoMemory(mem.data(), mem.size(), uid, uid_len);
    }
    return true;
}

static bool buildEmulatedNdef(const String& text) {
    static constexpr uint8_t uid[] = {0x04, 0x43, 0x41, 0x52, 0x44, 0x50, 0x54};
    g_emu_memory.assign(64, 0);
    writeUidIntoMemory(g_emu_memory.data(), g_emu_memory.size(), uid, sizeof(uid));
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

static bool beginEmulationSession() {
    if (!g_ready || g_emu_memory.empty() ||
        !g_emu_a.begin(g_emu_picc, g_emu_memory.data(),
                       static_cast<uint32_t>(g_emu_memory.size()))) {
        g_last_msg = "emulation start failed";
        g_emulation_on = false;
        return false;
    }
    g_emulation_on = true;
    g_emu_state = g_emu_a.state();
    return true;
}

static bool startNfcEmulationDefault() {
    stopReadScan();
    g_nfc_a.deactivate();
    g_ready = initializeNfcUnit(true);
    g_emu_from_record = false;
    g_emu_label = g_write_payload;
    if (!g_ready || !buildEmulatedNdef(g_write_payload) || !beginEmulationSession()) {
        return false;
    }
    g_last_msg = "NDEF tag ready";
    return true;
}

static bool startNfcEmulationFromRecord(const NfcRecord& rec) {
    stopReadScan();
    g_nfc_a.deactivate();

    const Type src = recordType(rec);
    if (!canEmulateType(src)) {
        g_last_msg = "type not emulatable";
        pushLog("> emu unsupported type");
        return false;
    }
    const Type emu = emulationCompatibleType(src);
    if (emu == Type::Unknown) {
        g_last_msg = "type not emulatable";
        return false;
    }
    if (rec.rows.empty() && rec.bytes.empty()) {
        g_last_msg = "no dump to emulate";
        return false;
    }

    uint8_t uid[10]{};
    uint8_t uid_len = 0;
    if (!parseUidHex(rec.uid, uid, uid_len)) {
        g_last_msg = "bad stored uid";
        return false;
    }
    if (!buildEmuImageFromRecord(rec, emu, g_emu_memory) ||
        !g_emu_picc.emulate(emu, uid, uid_len)) {
        g_last_msg = "emulation build failed";
        return false;
    }

    g_ready = initializeNfcUnit(true);
    g_emu_from_record = true;
    g_emu_label = rec.name.length() > 0 ? rec.name : rec.uid;
    if (!beginEmulationSession()) {
        return false;
    }
    g_last_msg = "card emulate ready";
    pushLogf("> emu %s", rec.uid.c_str());
    return true;
}

static void stopNfcEmulation() {
    if (g_emulation_on) {
        g_emu_a.end();
    }
    g_emulation_on = false;
    g_emu_from_record = false;
    g_emu_state = EmulationLayerA::State::None;
}

static bool restoreNfcReader() {
    stopNfcEmulation();
    g_ready = initializeNfcUnit(false);
    g_last_msg = g_ready ? "reader ready" : "unit begin failed";
    return g_ready;
}

static void leaveEmulationView() {
    restoreNfcReader();
    g_view = g_emu_return_view;
    if (g_view != NfcView::MAIN && g_view != NfcView::HISTORY && g_view != NfcView::DETAIL) {
        g_view = NfcView::MAIN;
    }
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

static int readableDumpCount() {
    int n = 0;
    for (const NfcDumpRow& row : g_dump_rows) {
        if (row.readable) {
            ++n;
        }
    }
    return n;
}

static const NfcDumpRow* readableDumpRowAt(const int readable_index) {
    int n = 0;
    for (const NfcDumpRow& row : g_dump_rows) {
        if (!row.readable) {
            continue;
        }
        if (n == readable_index) {
            return &row;
        }
        ++n;
    }
    return nullptr;
}

static int resultPageCount() {
    const int readable = readableDumpCount();
    return 1 + (readable + NFC_DUMP_ROWS_PER_PAGE - 1) / NFC_DUMP_ROWS_PER_PAGE;
}

static const char* readerStatusLabel(uint16_t& color) {
    if (!g_ready) {
        color = APP_COLOR_ERROR;
        return "FAIL";
    }
    if (g_operation == NfcOperation::READ) {
        color = APP_COLOR_WARN;
        return "SCAN";
    }
    if (g_operation == NfcOperation::WRITE) {
        color = APP_COLOR_ERROR;
        return "WRITE";
    }
    if (g_last_key == "Auth Error") {
        color = APP_COLOR_ERROR;
        return "AUTH";
    }
    if (g_last_msg == "unit begin failed" || g_last_msg.endsWith("fail") ||
        g_last_msg.indexOf("fail") >= 0) {
        color = APP_COLOR_ERROR;
        return "FAIL";
    }
    if (g_last_uid != "--" && g_last_uid.length() > 0) {
        color = APP_COLOR_OK;
        return "READY";
    }
    color = APP_COLOR_OK;
    return "READY";
}

static const char* readerStatusIconPath() {
    if (g_view == NfcView::EMULATION || g_emulation_on) {
        return NFC_ICON_EMU;
    }
    if (!g_ready) {
        return NFC_ICON_IDLE;
    }
    if (g_operation == NfcOperation::READ || g_operation == NfcOperation::WRITE) {
        return NFC_ICON_ACTIVE;
    }
    if (g_last_key == "Auth Error" || g_last_msg.indexOf("fail") >= 0) {
        return NFC_ICON_IDLE;
    }
    if (g_last_uid != "--" && g_last_uid.length() > 0) {
        return NFC_ICON_ACTIVE;
    }
    return NFC_ICON_IDLE;
}

// 左侧栏：只画状态图标 + 文案，不参与内容排版
static void drawNfcStatusColumn() {
    auto& d = M5Cardputer.Display;
    const int icon_x = APP_HELP_EDGE + (NFC_LEFT_W - NFC_ICON_W) / 2;
    const int icon_y = APP_HELP_EDGE;
    if (!drawLittleFsPng(readerStatusIconPath(), icon_x, icon_y)) {
        d.drawArc(icon_x + 20, icon_y + 14, 6, 5, 210, 330, APP_COLOR_HINT);
        d.drawArc(icon_x + 20, icon_y + 14, 10, 9, 210, 330, APP_COLOR_HINT);
        d.drawArc(icon_x + 20, icon_y + 14, 14, 13, 210, 330, APP_COLOR_HINT);
    }
    uint16_t status_color = APP_COLOR_OK;
    const char* status = readerStatusLabel(status_color);
    d.setTextSize(1);
    d.setTextColor(status_color, BLACK);
    const int sw = d.textWidth(status);
    d.setCursor(APP_HELP_EDGE + (NFC_LEFT_W - sw) / 2, icon_y + NFC_ICON_H + 4);
    d.print(status);
}

// 按可用宽高自动决定 cell / 行列（偏横向铺满，类似米家序号点阵）
static void nfcBlockGridLayout(const int count, const int max_w, const int max_h, int& cell,
                               int& cols, int& rows) {
    cell = NFC_BLK_CELL_MIN;
    cols = 0;
    rows = 0;
    if (count <= 0 || max_w <= 0 || max_h <= 0) {
        return;
    }
    for (int c = NFC_BLK_CELL_MAX; c >= NFC_BLK_CELL_MIN; --c) {
        const int max_cols = (max_w + NFC_BLK_GAP) / (c + NFC_BLK_GAP);
        if (max_cols <= 0) {
            continue;
        }
        int try_cols = max_cols;
        if (try_cols > count) {
            try_cols = count;
        }
        if (count == 64 && max_cols >= 16) {
            try_cols = 16;
        } else if (count == 256 && max_cols >= 32) {
            try_cols = 32;
        }
        const int try_rows = (count + try_cols - 1) / try_cols;
        const int need_h = try_rows * c + (try_rows - 1) * NFC_BLK_GAP;
        if (need_h <= max_h) {
            cell = c;
            cols = try_cols;
            rows = try_rows;
            return;
        }
    }
    cell = NFC_BLK_CELL_MIN;
    rows = max(1, (max_h + NFC_BLK_GAP) / (cell + NFC_BLK_GAP));
    cols = (count + rows - 1) / rows;
    const int max_cols = max(1, (max_w + NFC_BLK_GAP) / (cell + NFC_BLK_GAP));
    if (cols > max_cols) {
        cols = max_cols;
        rows = (count + cols - 1) / cols;
    }
}

static uint16_t nfcBlockStatusColor(const NfcBlockStatus status) {
    switch (status) {
        case NfcBlockStatus::Ok:
            return APP_COLOR_OK;
        case NfcBlockStatus::Fail:
            return APP_COLOR_ERROR;
        case NfcBlockStatus::Skip:
            return APP_COLOR_MUTED;
        case NfcBlockStatus::Idle:
        default:
            return NFC_BLK_IDLE;
    }
}

static int drawBlockStatusGrid(const int x, const int y, const int max_w, const int max_h) {
    const int count = static_cast<int>(g_block_status.size());
    if (count <= 0) {
        return 0;
    }
    int cell = NFC_BLK_CELL_MIN;
    int cols = 0;
    int rows = 0;
    nfcBlockGridLayout(count, max_w, max_h, cell, cols, rows);
    if (cols <= 0 || rows <= 0) {
        return 0;
    }
    auto& d = M5Cardputer.Display;
    const int step = cell + NFC_BLK_GAP;
    for (int i = 0; i < count; ++i) {
        const int row = i / cols;
        const int col = i % cols;
        d.fillRect(x + col * step, y + row * step, cell, cell,
                    nfcBlockStatusColor(g_block_status[i]));
    }
    return rows * step - NFC_BLK_GAP;
}

static void drawReaderOverview(const bool /*detail*/) {
    auto& d = M5Cardputer.Display;
    constexpr int x = NFC_CONTENT_X;
    const int bottom = d.height() - APP_HELP_EDGE;
    drawNfcStatusColumn();

    int y = APP_HELP_EDGE;
    d.setTextSize(2);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(x, y);
    d.print(g_last_uid);
    y += 20;

    d.setTextSize(1);
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(x, y);
    if (g_last_meta1.startsWith("ATQA")) {
        d.print(g_last_meta1);
    } else {
        d.print("ATQA : -- SAK : -- UID : --");
    }
    y += 11;

    d.setTextColor(YELLOW, BLACK);
    d.setCursor(x, y);
    d.print(g_last_type != "--" && g_last_type.length() > 0 ? g_last_type.c_str() : "--");
    {
        uint16_t key_color = APP_COLOR_HINT;
        if (g_last_key == "Auth Error") {
            key_color = APP_COLOR_ERROR;
        } else if (g_last_key == "default key") {
            key_color = APP_COLOR_OK;
        }
        d.setTextColor(key_color, BLACK);
        const int kw = d.textWidth(g_last_key);
        d.setCursor(d.width() - APP_HELP_EDGE - kw, y);
        d.print(g_last_key);
    }
    y += 12;

    // USR/TOT 与 blk 共用标签列宽，进度条与点阵左缘对齐
    d.setTextSize(1);
    const int label_w = max(d.textWidth("USR/TOT"), d.textWidth("blk"));
    const int bar_x = x + label_w + 6;
    const int bar_w = d.width() - APP_HELP_EDGE - bar_x;
    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(x, y);
    d.print("USR/TOT");
    int pct = 0;
    if (g_last_total_size > 0) {
        pct = constrain(static_cast<int>(g_last_user_size) * 100 / g_last_total_size, 0, 100);
    }
    drawPercentBar(bar_x, y, bar_w, 8, pct, APP_COLOR_LABEL, APP_COLOR_MUTED, BLACK);
    y += 12;

    d.setTextColor(APP_COLOR_VALUE, BLACK);
    d.setCursor(x, y);
    d.print("blk");
    const int grid_max_w = d.width() - APP_HELP_EDGE - bar_x;
    const int grid_max_h = bottom - y - 12;
    if (!g_block_status.empty()) {
        drawBlockStatusGrid(bar_x, y, grid_max_w, max(grid_max_h, NFC_BLK_CELL_MIN));
    } else {
        for (int i = 0; i < 16; ++i) {
            d.fillRect(bar_x + i * (NFC_BLK_CELL_MIN + NFC_BLK_GAP), y + 2, NFC_BLK_CELL_MIN,
                        NFC_BLK_CELL_MIN, NFC_BLK_IDLE);
        }
    }
    y += max(14, min(grid_max_h, 28));

    d.setTextColor(g_last_all_zero ? APP_COLOR_WARN : APP_COLOR_VALUE, BLACK);
    d.setCursor(x, min(y, bottom - 8));
    String ndef = g_last_ndef;
    while (ndef.length() > 0 && d.textWidth(ndef) > d.width() - x - APP_HELP_EDGE) {
        ndef.remove(ndef.length() - 1);
    }
    d.print(ndef);

    if (resultPageCount() > 1) {
        char page[12];
        snprintf(page, sizeof(page), "%d/%d", g_result_page + 1, resultPageCount());
        d.setTextColor(APP_COLOR_HINT, BLACK);
        const int pw = d.textWidth(page);
        d.setCursor(d.width() - APP_HELP_EDGE - pw, APP_HELP_EDGE);
        d.print(page);
    }
}

static void drawMainView(const bool detail = false) {
    constexpr int x = NFC_CONTENT_X;
    g_result_page = constrain(g_result_page, 0, resultPageCount() - 1);

    if (g_result_page == 0) {
        drawReaderOverview(detail);
        return;
    }

    drawNfcStatusColumn();
    int y = APP_HELP_EDGE + 13;
    const int first = (g_result_page - 1) * NFC_DUMP_ROWS_PER_PAGE;
    M5Cardputer.Display.setTextSize(1);
    for (int i = 0; i < NFC_DUMP_ROWS_PER_PAGE; ++i) {
        const NfcDumpRow* row = readableDumpRowAt(first + i);
        if (row == nullptr) {
            break;
        }
        M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.printf("%03u ", row->block);
        M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, BLACK);
        M5Cardputer.Display.print(row->hex);
        y += 9;
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(x + 24, y);
        M5Cardputer.Display.print(row->ascii);
        y += 13;
    }
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    char page[20];
    snprintf(page, sizeof(page), "%d/%d", g_result_page + 1, resultPageCount());
    const int page_w = M5Cardputer.Display.textWidth(page);
    M5Cardputer.Display.setCursor(M5Cardputer.Display.width() - APP_HELP_EDGE - page_w,
                                  APP_HELP_EDGE + 13);
    M5Cardputer.Display.print(page);
}

static void drawHistoryView() {
    auto& d = M5Cardputer.Display;
    constexpr int x = APP_HELP_EDGE;
    const int y = APP_HELP_EDGE + 12;
    d.setTextSize(1);
    if (g_history_count == 0) {
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(x, y);
        d.print("No NFC records");
        return;
    }
    g_history_sel = constrain(g_history_sel, 0, g_history_count - 1);
    const int first =
        min(max(0, g_history_sel - 3), max(0, g_history_count - NFC_HISTORY_VISIBLE));
    for (int row = 0; row < NFC_HISTORY_VISIBLE && first + row < g_history_count; ++row) {
        const int idx = first + row;
        const int ring = (g_history_start + idx) % NFC_HISTORY_MAX;
        const NfcRecord& rec = g_history[ring];
        const int ry = y + row * NFC_HISTORY_ROW_H;
        const bool selected = idx == g_history_sel;
        if (selected) {
            d.fillRect(x, ry, NFC_HISTORY_LIST_W, NFC_HISTORY_ROW_H, APP_COLOR_MENU_KEY);
            d.setTextColor(BLACK, APP_COLOR_MENU_KEY);
        } else {
            d.setTextColor(APP_COLOR_LABEL, BLACK);
        }
        d.setCursor(x + NFC_HISTORY_PAD, ry + NFC_HISTORY_PAD);
        char line[72];
        const char* uid = rec.uid.c_str();
        const char* uid_tail = uid;
        if (rec.uid.length() > 8) {
            uid_tail = uid + (rec.uid.length() - 8);
        }
        snprintf(line, sizeof(line), "%c%02d %s %s", selected ? '>' : ' ', idx + 1, uid_tail,
                 rec.name.c_str());
        while (line[0] != '\0' && d.textWidth(line) > NFC_HISTORY_LIST_W - NFC_HISTORY_PAD * 2) {
            line[strlen(line) - 1] = '\0';
        }
        d.print(line);
    }
    drawAppScrollbar(d, y, NFC_HISTORY_VISIBLE * NFC_HISTORY_ROW_H, g_history_count,
                     NFC_HISTORY_VISIBLE, first);
}

static void drawRenameView() {
    constexpr int x = APP_HELP_EDGE;
    int y = APP_HELP_EDGE + 22;
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print("Edit current name:");
    y += 16;
    M5Cardputer.Display.fillRoundRect(x, y, M5Cardputer.Display.width() - x * 2, 25, 4, DARKGREY);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_VALUE, DARKGREY);
    M5Cardputer.Display.setCursor(x + 5, y + 5);
    M5Cardputer.Display.print(g_rename_text);
    const String before_cursor = g_rename_text.substring(0, g_rename_cursor);
    M5Cardputer.Display.fillRect(x + 5 + M5Cardputer.Display.textWidth(before_cursor), y + 5, 2, 15,
                                  APP_COLOR_OK);
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
    drawNfcStatusColumn();
    constexpr int x = NFC_CONTENT_X;
    int y = APP_HELP_EDGE;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(g_emulation_on ? APP_COLOR_OK : APP_COLOR_ERROR, BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(emulationStateName(g_emu_state));
    y += 22;
    drawInfoLineAt(x, y, "uid", g_emulation_on ? g_emu_picc.uidAsString().c_str() : "--", 1);
    y += INFO_LINE_H;
    drawInfoLineAt(x, y, "type", g_emulation_on ? g_emu_picc.typeAsString().c_str() : "--", 1);
    y += INFO_LINE_H;
    drawInfoLineAt(x, y, "src", g_emu_from_record ? "history" : "ndef text", 1);
    y += INFO_LINE_H;
    drawInfoLineAt(x, y, "tag", g_emu_label.c_str(), 1);
}

static void drawHelpPage() {
    static const AppHelpLine kLines[] = {
        appHelpTextColored("Reader/Writer", APP_COLOR_LABEL),
        appHelpKey('r', "scan until card found"),
        appHelpKey('w', "write payload to card"),
        appHelpKey('o', "toggle save history"),
        appHelpKey('l', "open read history"),
        appHelpArrows("result / dump pages"),
        appHelpText("blk map: green=ok red=fail"),
        appHelpKey('e', "default NDEF text emulate"),
        appHelpTextColored("History", APP_COLOR_LABEL),
        appHelpArrows("pick history item"),
        appHelpBadge("Enter", "show complete record"),
        appHelpKey('r', "rename selected record"),
        appHelpKey('e', "emulate stored Type2 card"),
        appHelpKey('w', "write from detail"),
        appHelpBadge("ESC", "detail→list→reader"),
        appHelpTextColored("Emulation", APP_COLOR_LABEL),
        appHelpText("Clones UID + full Type2 dump"),
        appHelpText("Ultralight / NTAG only"),
        appHelpText("Classic / DESFire not supported"),
        appHelpTextColored("Hardware", APP_COLOR_LABEL),
        appHelpText("Needs Unit NFC (ST25R3916)"),
        appHelpText("Not Unit RFID U031/0x28"),
        appHelpText("13.56MHz cards only"),
        appHelpText("Classic: probe blk4 KeyA FF..FF"),
        appHelpText("default key = factory; Auth Error = changed"),
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
    // Reader / Detail / Emulation：左侧状态栏；History / Rename 才用顶栏标题
    if (g_view == NfcView::HISTORY) {
        drawTitleBar("RECORDS");
    } else if (g_view == NfcView::RENAME) {
        drawTitleBar("RENAME");
    }
    if (g_view == NfcView::MAIN) {
        drawMainView();
    } else if (g_view == NfcView::HISTORY) {
        drawHistoryView();
    } else if (g_view == NfcView::DETAIL) {
        drawMainView(true);
    } else if (g_view == NfcView::RENAME) {
        drawRenameView();
    } else {
        drawEmulationView();
    }
}

} // namespace

void enterNfcApp() {
    // 上次异常退出也先重建 Grove 总线，避免 UnitUnified 保留旧绑定。
    leaveNfcApp();
    g_help_visible = false;
    g_help_page = 0;
    g_read_scanning = false;
    g_emulation_on = false;
    g_emu_from_record = false;
    g_emu_return_view = NfcView::MAIN;
    g_emu_label = kDefaultWritePayload;
    g_view = NfcView::MAIN;
    g_operation = NfcOperation::NONE;
    g_result_page = 0;
    g_last_msg = "init...";
    g_last_uid = "--";
    g_last_type = "--";
    g_last_data = "--";
    g_last_meta1 = "--";
    g_last_meta2 = "--";
    g_last_key = "--";
    g_last_ndef = "NDEF: --";
    g_last_all_zero = false;
    g_last_picc_type = Type::Unknown;
    g_last_user_size = 0;
    g_last_total_size = 0;
    g_last_block_count = 0;
    g_dump_rows.clear();
    g_dump_bytes.clear();
    g_block_status.clear();
    g_emu_memory.clear();
    clearLog();
    pushLog("> init");
    const bool store_ok = loadHistory();
    pushLog(store_ok ? "> records loaded" : "> records reset");

    // Cardputer Grove 走 Ex_I2C（G1/G2），与 Radio / I2C Scan 一致；勿用 Wire 抢同一组脚。
    g_ready = initializeNfcUnit(false);
    if (!g_ready) {
        // 某些退出路径会让 I2C 外设仍处于忙状态，释放后只重试一次。
        M5Cardputer.Ex_I2C.release();
        delay(10);
        M5Cardputer.Ex_I2C.begin();
        delay(10);
        g_ready = initializeNfcUnit(false);
    }
    g_last_msg = g_ready ? "ready (Unit NFC)" : "unit begin failed";
    pushLog(g_ready ? "> unit ready" : "> unit begin fail");
    drawNfcApp();
}

void leaveNfcApp() {
    if (g_store_loaded) {
        saveHistory();
    }
    stopReadScan();
    stopNfcEmulation();
    g_nfc_a.deactivate();
    g_units = m5::unit::UnitUnified{};
    M5Cardputer.Ex_I2C.release();
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
    if (g_view == NfcView::RENAME && !g_help_visible) {
        if (status.fn) {
            // Cardputer 的 Fn+方向键以 HID 方向键上报；上下与左右都可移动光标。
            for (const uint8_t hid : status.hid_keys) {
                if (hid == 0x52 || hid == 0x50) {
                    g_rename_cursor = max(0, g_rename_cursor - 1);
                    drawNfcApp();
                    return;
                }
                if (hid == 0x51 || hid == 0x4F) {
                    g_rename_cursor =
                        min(static_cast<int>(g_rename_text.length()), g_rename_cursor + 1);
                    drawNfcApp();
                    return;
                }
            }
            return;
        }
        if (status.del) {
            if (g_rename_cursor > 0) {
                g_rename_text.remove(g_rename_cursor - 1, 1);
                --g_rename_cursor;
                drawNfcApp();
            }
            return;
        }
        if (status.enter) {
            NfcRecord* rec = selectedRecordMutable();
            if (rec != nullptr && g_rename_text.length() > 0) {
                rec->name = g_rename_text;
                saveHistory();
            }
            g_view = NfcView::HISTORY;
            drawNfcApp();
            return;
        }
        if (status.space && g_rename_text.length() < NFC_NAME_MAX) {
            g_rename_text = g_rename_text.substring(0, g_rename_cursor) + " " +
                            g_rename_text.substring(g_rename_cursor);
            ++g_rename_cursor;
        }
        for (const char raw : status.word) {
            if (raw == '\b') {
                if (g_rename_cursor > 0) {
                    g_rename_text.remove(g_rename_cursor - 1, 1);
                    --g_rename_cursor;
                }
            } else if (raw != ' ' && raw >= 32 && raw <= 126 &&
                       g_rename_text.length() < NFC_NAME_MAX) {
                g_rename_text = g_rename_text.substring(0, g_rename_cursor) + raw +
                                g_rename_text.substring(g_rename_cursor);
                ++g_rename_cursor;
            }
        }
        drawNfcApp();
        return;
    }

    int help_delta = getHelpNavDelta(status);
    if (g_help_visible) {
        const int line_count = 23;
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
        if (status.enter) {
            showSelectedRecord();
            drawNfcApp();
            return;
        }
    } else if ((g_view == NfcView::MAIN || g_view == NfcView::DETAIL) &&
               help_delta != 0 && resultPageCount() > 1) {
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
            g_help_visible = true;
            g_help_page = 0;
            drawNfcApp();
            return;
        }
        if (c == 'r' && g_view == NfcView::HISTORY) {
            const NfcRecord* rec = selectedRecord();
            if (rec != nullptr) {
                g_rename_text = rec->name;
                g_rename_cursor = g_rename_text.length();
                g_view = NfcView::RENAME;
                drawNfcApp();
            }
            return;
        }
        if (c == 'e') {
            if (g_view == NfcView::EMULATION) {
                leaveEmulationView();
                drawNfcApp();
                return;
            }
            if (g_view == NfcView::HISTORY || g_view == NfcView::DETAIL) {
                const NfcRecord* rec = selectedRecord();
                if (rec == nullptr) {
                    g_last_msg = "no history";
                    drawNfcApp();
                    return;
                }
                g_emu_return_view = g_view;
                g_view = NfcView::EMULATION;
                if (!startNfcEmulationFromRecord(*rec)) {
                    g_view = g_emu_return_view;
                }
                drawNfcApp();
                return;
            }
            g_emu_return_view = NfcView::MAIN;
            g_view = NfcView::EMULATION;
            if (!startNfcEmulationDefault()) {
                g_view = NfcView::MAIN;
            }
            drawNfcApp();
            return;
        }
        if (c == 'l') {
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
    if (g_help_visible) {
        g_help_visible = false;
        drawNfcApp();
        return true;
    }
    // ESC：emulation / rename / detail → list → reader（未嵌套则交回 Grove hub）
    if (g_view == NfcView::EMULATION) {
        leaveEmulationView();
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::RENAME) {
        g_view = NfcView::HISTORY;
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::DETAIL) {
        g_view = NfcView::HISTORY;
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::HISTORY) {
        g_view = NfcView::MAIN;
        drawNfcApp();
        return true;
    }
    return false;
}

bool isNfcHelpVisible() {
    return g_help_visible;
}

void getNfcShotFeature(char* out, const size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return;
    }
    const char* feature = "main";
    if (g_help_visible) {
        feature = "help";
    } else {
        switch (g_view) {
            case NfcView::MAIN:
                feature = "main";
                break;
            case NfcView::HISTORY:
                feature = "history";
                break;
            case NfcView::DETAIL:
                feature = "detail";
                break;
            case NfcView::RENAME:
                feature = "rename";
                break;
            case NfcView::EMULATION:
                feature = "emulate";
                break;
        }
    }
    strncpy(out, feature, out_len - 1);
    out[out_len - 1] = '\0';
}
