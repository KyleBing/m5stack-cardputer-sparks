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
#include <new>
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
    EMU_SOURCE,
    EMU_RECORD,
    EMU_TYPE,
    EMU_UID,
    EMU_PAYLOAD,
    EMULATION,
    WRITING,
};

enum class NfcOperation {
    NONE,
    READ,
    WRITE,
};

// Write / Emu 共用 SOURCE→RECORD 或 TYPE→UID→PAYLOAD 向导
enum class NfcWizardMode : uint8_t {
    None,
    Emu,
    Write,
};

enum class WriteSessionState : uint8_t {
    Idle = 0,
    Wait,  // 等待贴卡
    Busy,  // 正在写入
    Ok,
    Fail,
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
static constexpr char kDefaultEmuUid[] = "04:43:41:52:44:50:54";
static constexpr size_t NFC_EMU_MEMORY_MAX = 1024; // NTAG216 ≈ 924B
static constexpr uint8_t NFC_CLASSIC_PROBE_BLOCK = 4; // sector 1 block 0
static constexpr int NFC_BLK_CELL_MIN = 4;
static constexpr int NFC_BLK_CELL_MAX = 7;
static constexpr int NFC_BLK_GAP = 1;
static constexpr uint16_t NFC_BLK_IDLE = 0x9492; // 与米家 pager idle 同色 #929292
static constexpr int NFC_ICON_W = 50;
static constexpr int NFC_ICON_H = 45;
static constexpr int NFC_OVERVIEW_EDGE = 10;   // 概览页距边 ≥10px
static constexpr int NFC_LEFT_COL_W = 66;      // 左侧加宽，图标栏内居中
static constexpr int NFC_LEFT_RIGHT_GAP = 18; // 左栏与右栏间距
static constexpr int NFC_KV_GAP = 6;
static constexpr int NFC_KV_ROW_GAP = 5;       // USR/BLOCK/NDEF 间隔
static constexpr int NFC_TYPE_ROW_GAP = 10;    // Auth Error+类型 与上下间距
static constexpr int NFC_TYPE_INNER_GAP = 5;   // Auth Error 与类型行间距
static constexpr int NFC_META_TOP_GAP = 20;    // 状态与 ATQA 间距
static constexpr int NFC_DUMP_ROW_H = 9;
static constexpr int NFC_CONTENT_X =
    NFC_OVERVIEW_EDGE + NFC_LEFT_COL_W + NFC_LEFT_RIGHT_GAP;
// NDEF 空框：边框 MUTED，填充再暗一档（比 DARKGREY 更深）
static constexpr uint16_t NFC_NDEF_FILL = 0x3186;
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
NfcWizardMode g_wizard = NfcWizardMode::None;
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
int g_emu_source_sel = 0;
int g_emu_record_sel = 0;
int g_emu_type_sel = 0;
String g_emu_uid = kDefaultEmuUid;
String g_emu_payload = kDefaultWritePayload;
int g_emu_edit_cursor = 0;
std::vector<uint8_t> g_write_memory;
String g_write_show_uid = "--";
String g_write_show_type = "--";
String g_write_show_line = "--"; // tech / payload / dump 标签
WriteSessionState g_write_state = WriteSessionState::Idle;
String g_write_ok_msg = "write ok";
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
static const char* nfcTechLabel(Type t);

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

static void deleteSelectedHistory() {
    if (g_history_count <= 0 || g_history_sel < 0 || g_history_sel >= g_history_count) {
        return;
    }
    std::array<NfcRecord, NFC_HISTORY_MAX> kept{};
    int n = 0;
    for (int i = 0; i < g_history_count; ++i) {
        if (i == g_history_sel) {
            continue;
        }
        kept[static_cast<size_t>(n++)] = g_history[(g_history_start + i) % NFC_HISTORY_MAX];
    }
    for (int i = 0; i < n; ++i) {
        g_history[static_cast<size_t>(i)] = kept[static_cast<size_t>(i)];
    }
    for (int i = n; i < NFC_HISTORY_MAX; ++i) {
        g_history[static_cast<size_t>(i)] = {};
    }
    g_history_start = 0;
    g_history_count = n;
    if (g_history_count <= 0) {
        g_history_sel = 0;
    } else if (g_history_sel >= g_history_count) {
        g_history_sel = g_history_count - 1;
    }
    saveHistory();
    pushLog("> history deleted");
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

// Type2：从 CC(page3) 写到 lastUser（不写 UID 页）；按物理卡容量截断。
static bool writeType2Image(const PICC& picc, const std::vector<uint8_t>& mem) {
    if (!picc.supportsNFC() || mem.size() < 16) {
        pushLog("> need Type2 image");
        return false;
    }
    const uint16_t mem_pages = static_cast<uint16_t>(mem.size() / 4u);
    const uint16_t card_pages = picc.blocks > 0 ? picc.blocks : mem_pages;
    const uint16_t last_user = picc.lastUserBlock();
    const uint16_t end =
        min(min(mem_pages, card_pages), static_cast<uint16_t>(last_user + 1u));
    // page 0..2 = UID（只读）；从 CC 起写
    constexpr uint16_t kCcPage = 3;
    if (end <= kCcPage) {
        pushLog("> no writable pages");
        return false;
    }
    pushLogf("> TX pages %u..%u", kCcPage, end - 1);
    for (uint16_t page = kCcPage; page < end; ++page) {
        const uint8_t* p = mem.data() + static_cast<size_t>(page) * 4u;
        pushLogf("> TX write4 p%u %s", page, bytesToHex(p, 4).c_str());
        if (!g_nfc_a.write4(static_cast<uint8_t>(page), p, 4)) {
            pushLog("> TX write fail");
            return false;
        }
    }
    pushLog("> TX write ok");
    return true;
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

// 开始新一轮 scan 前清空概览/dump，避免残留上一张卡。
static void clearCurrentReadDisplay() {
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
    g_result_page = 0;
    g_dump_rows.clear();
    g_dump_bytes.clear();
    g_block_status.clear();
}

static void startReadScan() {
    clearLog();
    clearCurrentReadDisplay();
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

static void finishWriteSession(PICC& picc, const bool ok, const char* ok_msg) {
    if (ok) {
        g_last_msg = ok_msg != nullptr ? ok_msg : "write ok";
        g_write_state = WriteSessionState::Ok;
    } else if (g_last_msg.length() == 0 || g_last_msg == "writing...") {
        g_last_msg = "write failed";
        g_write_state = WriteSessionState::Fail;
    } else {
        g_write_state = WriteSessionState::Fail;
    }
    String data;
    pushLog("> verify read...");
    if (readCardData(picc, data)) {
        refreshCardDetails(picc, data);
        g_write_show_uid = g_last_uid;
        g_write_show_type = g_last_type;
        g_write_show_line = nfcTechLabel(picc.type);
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

static bool armWriteSession(std::vector<uint8_t>&& mem, const String& uid, const String& type,
                            const String& line, const char* ok_msg) {
    if (!g_ready) {
        g_last_msg = "unit not ready";
        return false;
    }
    if (mem.size() < 16) {
        g_last_msg = "write image empty";
        return false;
    }
    stopReadScan();
    g_write_memory = std::move(mem);
    g_write_show_uid = uid.length() > 0 ? uid : String("--");
    g_write_show_type = type.length() > 0 ? type : String("--");
    g_write_show_line = line.length() > 0 ? line : String("--");
    g_write_ok_msg = ok_msg != nullptr ? ok_msg : "write ok";
    g_write_state = WriteSessionState::Wait;
    g_operation = NfcOperation::WRITE;
    g_last_msg = "hold tag to write";
    pushLog("> write armed");
    return true;
}

static void leaveWriteView() {
    g_write_state = WriteSessionState::Idle;
    g_write_memory.clear();
    g_operation = NfcOperation::NONE;
    g_wizard = NfcWizardMode::None;
    g_nfc_a.deactivate();
    g_view = g_emu_return_view;
    if (g_view != NfcView::MAIN && g_view != NfcView::HISTORY && g_view != NfcView::DETAIL) {
        g_view = NfcView::MAIN;
    }
    if (g_last_msg == "hold tag to write" || g_last_msg == "writing...") {
        g_last_msg = "ready (Unit NFC)";
    }
}

static void pollWriteSession() {
    if (g_view != NfcView::WRITING || g_help_visible || !g_ready) {
        return;
    }
    if (g_write_state != WriteSessionState::Wait) {
        return;
    }
    static uint32_t last_poll_ms = 0;
    const uint32_t now = millis();
    if (now - last_poll_ms < 200) {
        return;
    }
    last_poll_ms = now;

    PICC picc{};
    if (!detectIdentifyAndActivate(picc, false)) {
        return;
    }

    // 贴卡后先刷新左侧 UID/类型，再写入
    g_write_show_uid = picc.uidAsString().c_str();
    g_write_show_type = picc.typeAsString().c_str();
    g_write_show_line = nfcTechLabel(picc.type);
    g_write_state = WriteSessionState::Busy;
    g_last_msg = "writing...";
    g_operation = NfcOperation::WRITE;
    drawNfcApp();

    clearLog();
    pushLog("> write start");
    bool ok = false;
    if (!picc.supportsNFC()) {
        pushLog("> need Type2 tag");
        g_last_msg = "need Type2 tag";
    } else {
        ok = writeType2Image(picc, g_write_memory);
    }
    finishWriteSession(picc, ok, g_write_ok_msg.c_str());
    drawNfcApp();
}

// UnitUnified 析构/赋值不会清 Component::_manager；若只重建 g_units 再 add，
// 会因 isRegistered() 仍为 true 而失败（ESC 退出再进显示 FAIL）。
static void resetNfcUnitObjects() {
    g_nfc_a.~NFCLayerA();
    g_emu_a.~EmulationLayerA();
    g_units = m5::unit::UnitUnified{};
    // UnitNFC 是 using 别名，伪析构须写真实类的限定名。
    g_unit.m5::unit::UnitST25R3916::~UnitST25R3916();
    new (&g_unit) m5::unit::UnitST25R3916();
    new (&g_nfc_a) NFCLayerA(g_unit);
    new (&g_emu_a) EmulationLayerA(g_unit);
}

static bool initializeNfcUnit(const bool emulation) {
    resetNfcUnitObjects();
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

// 接受纯 hex 或带 : / - / 空格分隔（与默认预填 04:43:... 一致）
static bool parseUidHex(const String& hex, uint8_t* out, uint8_t& out_len) {
    if (out == nullptr || hex.length() == 0) {
        return false;
    }
    char digits[21]{};
    size_t n = 0;
    for (unsigned i = 0; i < hex.length(); ++i) {
        const char c = hex[i];
        if (c == ':' || c == '-' || c == ' ') {
            continue;
        }
        if (hexNibble(c) < 0 || n >= sizeof(digits) - 1) {
            return false;
        }
        digits[n++] = c;
    }
    if (n == 0 || (n % 2) != 0) {
        return false;
    }
    const size_t len = n / 2;
    if (len != 4 && len != 7 && len != 10) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const int hi = hexNibble(digits[i * 2]);
        const int lo = hexNibble(digits[i * 2 + 1]);
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

struct EmuTypeOption {
    const char* name;
    Type type;
};

static constexpr EmuTypeOption kEmuTypes[] = {
    {"MIFARE Ultralight", Type::MIFARE_Ultralight},
    {"NTAG 203", Type::NTAG_203},
    {"NTAG 210", Type::NTAG_210},
    {"NTAG 210u", Type::NTAG_210u},
    {"NTAG 212", Type::NTAG_212},
    {"NTAG 213", Type::NTAG_213},
    {"NTAG 215", Type::NTAG_215},
    {"NTAG 216", Type::NTAG_216},
};
static constexpr int NFC_EMU_TYPE_COUNT =
    static_cast<int>(sizeof(kEmuTypes) / sizeof(kEmuTypes[0]));

static bool recordCanEmulate(const NfcRecord& rec) {
    return canEmulateType(recordType(rec)) && (!rec.rows.empty() || !rec.bytes.empty());
}

static int emulatableRecordCount() {
    int count = 0;
    for (int i = 0; i < g_history_count; ++i) {
        const NfcRecord& rec = g_history[(g_history_start + i) % NFC_HISTORY_MAX];
        if (recordCanEmulate(rec)) {
            ++count;
        }
    }
    return count;
}

static const NfcRecord* emulatableRecordAt(const int wanted) {
    int found = 0;
    for (int i = 0; i < g_history_count; ++i) {
        const NfcRecord& rec = g_history[(g_history_start + i) % NFC_HISTORY_MAX];
        if (!recordCanEmulate(rec)) {
            continue;
        }
        if (found++ == wanted) {
            return &rec;
        }
    }
    return nullptr;
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

static bool buildNdefMemory(const String& text, const Type type, const uint8_t* uid,
                            const uint8_t uid_len, std::vector<uint8_t>& mem) {
    const uint16_t pages = get_number_of_blocks(type);
    const size_t memory_size = static_cast<size_t>(pages) * 4u;
    if (pages == 0 || memory_size < 32 || memory_size > NFC_EMU_MEMORY_MAX || uid == nullptr) {
        return false;
    }
    mem.assign(memory_size, 0);
    writeUidIntoMemory(mem.data(), mem.size(), uid, uid_len);
    mem[9] = 0x48;
    mem[12] = 0xE1;
    mem[13] = 0x10;
    mem[14] = static_cast<uint8_t>(min<size_t>(0xFF, (memory_size - 16) / 8));

    const size_t max_text = memory_size > 25 ? memory_size - 25 : 0;
    const size_t text_len = min<size_t>(text.length(), min<size_t>(max_text, 240));
    const uint8_t payload_len = static_cast<uint8_t>(3 + text_len);
    const uint8_t record_len = static_cast<uint8_t>(4 + payload_len);
    size_t pos = 16;
    mem[pos++] = 0x03;
    mem[pos++] = record_len;
    mem[pos++] = 0xD1;
    mem[pos++] = 0x01;
    mem[pos++] = payload_len;
    mem[pos++] = 'T';
    mem[pos++] = 0x02;
    mem[pos++] = 'e';
    mem[pos++] = 'n';
    for (size_t i = 0; i < text_len; ++i) {
        mem[pos++] = static_cast<uint8_t>(text[i]);
    }
    mem[pos] = 0xFE;
    return true;
}

static bool buildEmulatedNdef(const String& text, const Type type, const uint8_t* uid,
                              const uint8_t uid_len) {
    if (!buildNdefMemory(text, type, uid, uid_len, g_emu_memory)) {
        return false;
    }
    return g_emu_picc.emulate(type, uid, uid_len);
}

static bool buildEmulatedNdef(const String& text) {
    static constexpr uint8_t uid[] = {0x04, 0x43, 0x41, 0x52, 0x44, 0x50, 0x54};
    return buildEmulatedNdef(text, Type::MIFARE_Ultralight, uid, sizeof(uid));
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

static bool restoreNfcReader();

// 进入模拟失败时恢复读卡模式，但保留错误文案供 EMU 页显示
static void abortEmulationStart(const char* fallback_msg) {
    const String err =
        (g_last_msg.length() > 0 && g_last_msg != "ready (Unit NFC)" &&
         g_last_msg != "reader ready" && g_last_msg != "init...")
            ? g_last_msg
            : String(fallback_msg != nullptr ? fallback_msg : "emulation start failed");
    restoreNfcReader();
    g_last_msg = err;
    g_emulation_on = false;
}

static bool startNfcEmulationDefault() {
    stopReadScan();
    g_nfc_a.deactivate();
    g_ready = initializeNfcUnit(true);
    g_emu_from_record = false;
    g_emu_label = g_write_payload;
    if (!g_ready) {
        g_last_msg = "emulation start failed";
        abortEmulationStart("emulation start failed");
        return false;
    }
    if (!buildEmulatedNdef(g_write_payload) || !beginEmulationSession()) {
        abortEmulationStart("emulation start failed");
        return false;
    }
    g_last_msg = "NDEF tag ready";
    return true;
}

static bool startNfcEmulationCustom() {
    uint8_t uid[10]{};
    uint8_t uid_len = 0;
    if (!parseUidHex(g_emu_uid, uid, uid_len)) {
        g_last_msg = "UID needs 4, 7 or 10 bytes";
        return false;
    }
    const Type type = kEmuTypes[constrain(g_emu_type_sel, 0, NFC_EMU_TYPE_COUNT - 1)].type;
    stopReadScan();
    g_nfc_a.deactivate();
    g_ready = initializeNfcUnit(true);
    g_emu_from_record = false;
    g_emu_label = g_emu_payload;
    if (!g_ready || !buildEmulatedNdef(g_emu_payload, type, uid, uid_len) ||
        !beginEmulationSession()) {
        abortEmulationStart("emulation start failed");
        return false;
    }
    g_last_msg = "custom NDEF ready";
    return true;
}

static bool startWriteCustom() {
    uint8_t uid[10]{};
    uint8_t uid_len = 0;
    if (!parseUidHex(g_emu_uid, uid, uid_len)) {
        g_last_msg = "UID needs 4, 7 or 10 bytes";
        return false;
    }
    const int type_i = constrain(g_emu_type_sel, 0, NFC_EMU_TYPE_COUNT - 1);
    const Type type = kEmuTypes[type_i].type;
    std::vector<uint8_t> mem;
    if (!buildNdefMemory(g_emu_payload, type, uid, uid_len, mem)) {
        g_last_msg = "NDEF build failed";
        return false;
    }
    g_write_payload = g_emu_payload;
    String line = g_emu_payload;
    if (line.length() > 18) {
        line = line.substring(0, 18);
    }
    return armWriteSession(std::move(mem), g_emu_uid, kEmuTypes[type_i].name, line,
                           "custom NDEF written");
}

static bool startWriteFromRecord(const NfcRecord& rec) {
    const Type src = recordType(rec);
    if (!canEmulateType(src)) {
        g_last_msg = "type not writable";
        return false;
    }
    const Type emu = emulationCompatibleType(src);
    if (emu == Type::Unknown) {
        g_last_msg = "type not writable";
        return false;
    }
    std::vector<uint8_t> mem;
    if (!buildEmuImageFromRecord(rec, emu, mem) || mem.size() < 16) {
        g_last_msg = "no dump to write";
        return false;
    }
    const String label = rec.name.length() > 0 ? rec.name : String("dump clone");
    return armWriteSession(std::move(mem), rec.uid, rec.type, label, "card dump written");
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
    if (!g_ready || !beginEmulationSession()) {
        abortEmulationStart("emulation start failed");
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
    g_wizard = NfcWizardMode::None;
    g_view = g_emu_return_view;
    if (g_view != NfcView::MAIN && g_view != NfcView::HISTORY && g_view != NfcView::DETAIL) {
        g_view = NfcView::MAIN;
    }
}

static bool wizardIsWrite() {
    return g_wizard == NfcWizardMode::Write;
}

static const char* wizardBar(const char* emu, const char* write_title) {
    return wizardIsWrite() ? write_title : emu;
}

static void openWizard(const NfcWizardMode mode) {
    if (g_read_scanning) {
        stopReadScan();
        g_last_msg = "scan cancelled";
    }
    g_wizard = mode;
    g_emu_return_view = (g_view == NfcView::MAIN || g_view == NfcView::HISTORY ||
                         g_view == NfcView::DETAIL)
                            ? g_view
                            : NfcView::MAIN;
    g_emu_source_sel = 0;
    g_view = NfcView::EMU_SOURCE;
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


static int dumpTextMaxWidth() {
    return M5Cardputer.Display.width() - APP_HELP_EDGE * 2 - APP_SCROLLBAR_W - 2;
}

// kind0=成功 hex 行；kind1=失败序号横向包
template <typename Fn>
static int visitDumpVisualLines(const int max_w, Fn&& fn) {
    auto& d = M5Cardputer.Display;
    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    int lines = 0;
    size_t i = 0;
    while (i < g_dump_rows.size()) {
        if (g_dump_rows[i].readable) {
            fn(0, static_cast<int>(i), 1);
            ++lines;
            ++i;
            continue;
        }
        const size_t begin = i;
        int used = 0;
        while (i < g_dump_rows.size() && !g_dump_rows[i].readable) {
            char num[8];
            snprintf(num, sizeof(num), "%03u", g_dump_rows[i].block);
            const int nw = d.textWidth(num) + 4;
            if (used > 0 && used + nw - 4 > max_w) {
                break;
            }
            used += nw;
            ++i;
        }
        fn(1, static_cast<int>(begin), static_cast<int>(i - begin));
        ++lines;
    }
    return lines;
}

static int dumpVisualLineCount() {
    return visitDumpVisualLines(dumpTextMaxWidth(), [](int, int, int) {});
}

static int dumpRowsPerPage() {
    const int usable = M5Cardputer.Display.height() - APP_HELP_EDGE * 2;
    return max(1, usable / NFC_DUMP_ROW_H);
}

static int resultPageCount() {
    const int visual = dumpVisualLineCount();
    if (visual <= 0) {
        return 1;
    }
    const int per = dumpRowsPerPage();
    return 1 + (visual + per - 1) / per;
}

static constexpr int NFC_DETAIL_TEXT_META_LINES = 7; // NAME..KEY；USR/BLOCK/NDEF 用图形区

static int detailContentTop() {
    return APP_HELP_EDGE + 12;
}

static int detailDumpRowsPerPage() {
    const int usable = M5Cardputer.Display.height() - detailContentTop() - APP_HELP_EDGE;
    return max(1, usable / NFC_DUMP_ROW_H);
}

static int detailPageCount() {
    const int visual = dumpVisualLineCount();
    if (visual <= 0) {
        return 1;
    }
    const int per = detailDumpRowsPerPage();
    return 1 + (visual + per - 1) / per;
}

static const char* nfcTechLabel(Type t);
static bool parseMetaFields(uint16_t& atqa, uint8_t& sak, uint8_t& uid_len);

static void drawDetailMetaLine(const int index, const int x, const int y, const int max_w) {
    auto& d = M5Cardputer.Display;
    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    const int vx = x + d.textWidth("BLOCK ");

    const auto printKv = [&](const char* key, const char* value, const uint16_t value_color) {
        d.setTextColor(APP_COLOR_LABEL, BLACK);
        d.setCursor(x, y);
        d.print(key);
        d.setTextColor(value_color, BLACK);
        String v = value != nullptr ? value : "--";
        while (v.length() > 0 && d.textWidth(v) > max_w - (vx - x)) {
            v.remove(v.length() - 1);
        }
        d.setCursor(vx, y);
        d.print(v);
    };

    switch (index) {
        case 0: {
            const NfcRecord* rec = selectedRecord();
            const char* name = (rec != nullptr && rec->name.length() > 0) ? rec->name.c_str() : "--";
            printKv("NAME", name, APP_COLOR_VALUE);
            break;
        }
        case 1:
            printKv("UID", g_last_uid.c_str(), APP_COLOR_VALUE);
            break;
        case 2: {
            const char* type_s =
                (g_last_type != "--" && g_last_type.length() > 0) ? g_last_type.c_str() : "--";
            const char* tech = nfcTechLabel(g_last_picc_type);
            d.setTextColor(APP_COLOR_LABEL, BLACK);
            d.setCursor(x, y);
            d.print("TYPE");
            d.setTextColor(YELLOW, BLACK);
            d.setCursor(vx, y);
            d.print(type_s);
            d.setTextColor(APP_COLOR_OK, BLACK);
            d.setCursor(vx + d.textWidth(type_s) + 6, y);
            d.print(tech);
            break;
        }
        case 3: {
            uint16_t atqa = 0;
            uint8_t sak = 0;
            uint8_t uid_len = 0;
            char buf[12] = "--";
            if (parseMetaFields(atqa, sak, uid_len)) {
                snprintf(buf, sizeof(buf), "%04X", atqa);
            }
            printKv("ATQA", buf, APP_COLOR_HINT);
            break;
        }
        case 4: {
            uint16_t atqa = 0;
            uint8_t sak = 0;
            uint8_t uid_len = 0;
            char buf[8] = "--";
            if (parseMetaFields(atqa, sak, uid_len)) {
                snprintf(buf, sizeof(buf), "%02X", sak);
            }
            printKv("SAK", buf, APP_COLOR_HINT);
            break;
        }
        case 5: {
            uint16_t atqa = 0;
            uint8_t sak = 0;
            uint8_t uid_len = 0;
            char buf[8] = "--";
            if (parseMetaFields(atqa, sak, uid_len)) {
                snprintf(buf, sizeof(buf), "%u", uid_len);
            }
            printKv("UIDL", buf, APP_COLOR_HINT);
            break;
        }
        case 6:
            printKv("KEY", g_last_key.c_str(),
                    g_last_key == "Auth Error" ? APP_COLOR_ERROR : APP_COLOR_HINT);
            break;
        default:
            break;
    }
}

static const char* nfcTechLabel(const Type t) {
    if (t == Type::Unknown) {
        return "--";
    }
    return "NFC-A";
}

static bool nfcNdefIsNone(const String& ndef_raw) {
    String n = ndef_raw;
    if (n.startsWith("NDEF: ")) {
        n = n.substring(6);
    } else if (n.startsWith("NDEF ")) {
        n = n.substring(5);
    }
    n.trim();
    return n.equalsIgnoreCase("none");
}

static bool parseMetaFields(uint16_t& atqa, uint8_t& sak, uint8_t& uid_len) {
    atqa = 0;
    sak = 0;
    uid_len = 0;
    unsigned a = 0;
    unsigned s = 0;
    unsigned u = 0;
    if (sscanf(g_last_meta1.c_str(), "ATQA : %x SAK : %x UID : %u", &a, &s, &u) != 3) {
        return false;
    }
    atqa = static_cast<uint16_t>(a);
    sak = static_cast<uint8_t>(s);
    uid_len = static_cast<uint8_t>(u);
    return true;
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
    // Auth Error / 读卡 fail 文案在 UID 下方，左侧状态保持 READY
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
    if (g_last_uid != "--" && g_last_uid.length() > 0) {
        return NFC_ICON_ACTIVE;
    }
    return NFC_ICON_IDLE;
}

// 读卡结果 fail / Auth Error：画在 UID 下方，不进左侧状态字
static bool readerReadFailHint(uint16_t& color) {
    if (g_last_key == "Auth Error" || g_last_msg == "Auth Error" ||
        g_last_msg.indexOf("fail") >= 0) {
        color = APP_COLOR_ERROR;
        return true;
    }
    return false;
}

// 左侧栏：图标居中；下方状态；再空一截显示 ATQA/SAK/UID（key 右对齐）
static void drawNfcStatusColumn() {
    auto& d = M5Cardputer.Display;
    constexpr int edge = NFC_OVERVIEW_EDGE;
    constexpr int col_w = NFC_LEFT_COL_W;
    constexpr int meta_row_h = 10;
    const int icon_x = edge + (col_w - NFC_ICON_W) / 2;
    const int icon_y = edge;
    if (!drawLittleFsPng(readerStatusIconPath(), icon_x, icon_y)) {
        const int cx = icon_x + NFC_ICON_W / 2;
        const int cy = icon_y + NFC_ICON_H / 2;
        d.drawArc(cx, cy, 6, 5, 210, 330, APP_COLOR_HINT);
        d.drawArc(cx, cy, 10, 9, 210, 330, APP_COLOR_HINT);
        d.drawArc(cx, cy, 14, 13, 210, 330, APP_COLOR_HINT);
    }

    int y = icon_y + NFC_ICON_H + 2;
    uint16_t status_color = APP_COLOR_OK;
    const char* status = readerStatusLabel(status_color);
    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    d.setTextColor(status_color, BLACK);
    const int sw = d.textWidth(status);
    d.setCursor(edge + (col_w - sw) / 2, y);
    d.print(status);
    y += 8 + NFC_META_TOP_GAP;

    uint16_t atqa = 0;
    uint8_t sak = 0;
    uint8_t uid_len = 0;
    const bool have = parseMetaFields(atqa, sak, uid_len) && g_last_uid != "--";

    d.setTextColor(APP_COLOR_HINT, BLACK);
    const int key_x = edge + 5;
    const int val_x = key_x + d.textWidth("ATQA") + 4;
    const auto row = [&](const char* key, const char* value) {
        d.setCursor(key_x, y);
        d.print(key);
        if (value != nullptr && value[0] != '\0') {
            d.setCursor(val_x, y);
            d.print(value);
        }
        y += meta_row_h;
    };
    if (have) {
        char v[8];
        snprintf(v, sizeof(v), "%04X", atqa);
        row("ATQA", v);
        snprintf(v, sizeof(v), "%02X", sak);
        row("SAK", v);
        snprintf(v, sizeof(v), "%u", uid_len);
        row("UID", v);
    } else {
        row("ATQA", "");
        row("SAK", "");
        row("UID", "");
    }
}

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
        int try_cols = min(count, max_cols);
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

static int drawBlockStatusGrid(const int x, const int y, const int max_w, const int max_h,
                               int* out_w = nullptr, int* out_cell = nullptr) {
    const int count = static_cast<int>(g_block_status.size());
    if (count <= 0) {
        if (out_w) {
            *out_w = 0;
        }
        if (out_cell) {
            *out_cell = NFC_BLK_CELL_MIN;
        }
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
        d.fillRect(x + (i % cols) * step, y + (i / cols) * step, cell, cell,
                    nfcBlockStatusColor(g_block_status[i]));
    }
    if (out_w) {
        *out_w = cols * step - NFC_BLK_GAP;
    }
    if (out_cell) {
        *out_cell = cell;
    }
    return rows * step - NFC_BLK_GAP;
}

// USR 进度条（user/total）+ BLOCK 点阵 + NDEF；贴底绘制，不低于 after_y
static void drawUsrBlockNdefKv(const int x, const int after_y, const int right, const int bottom) {
    auto& d = M5Cardputer.Display;
    d.setFont(&fonts::Font0);
    d.setTextSize(1);

    const int label_w = d.textWidth("BLOCK");
    const int value_x = x + label_w + NFC_KV_GAP;
    const int value_max_w = right - value_x;
    const int usr_h = 8;

    int cell = NFC_BLK_CELL_MIN;
    int blocks_w = 0;
    int grid_h = NFC_BLK_CELL_MIN;
    int ndef_h = 8;
    if (nfcNdefIsNone(g_last_ndef)) {
        ndef_h = cell * 3 + NFC_BLK_GAP * 2;
    }
    int grid_max_h =
        max(NFC_BLK_CELL_MIN, bottom - after_y - usr_h - NFC_KV_ROW_GAP * 2 - ndef_h);
    if (!g_block_status.empty()) {
        int cols = 0;
        int rows = 0;
        nfcBlockGridLayout(static_cast<int>(g_block_status.size()), value_max_w, grid_max_h, cell,
                           cols, rows);
        if (cols > 0 && rows > 0) {
            grid_h = rows * (cell + NFC_BLK_GAP) - NFC_BLK_GAP;
            blocks_w = cols * (cell + NFC_BLK_GAP) - NFC_BLK_GAP;
        }
    } else {
        blocks_w = 16 * (NFC_BLK_CELL_MIN + NFC_BLK_GAP) - NFC_BLK_GAP;
        grid_h = NFC_BLK_CELL_MIN + 2;
        cell = NFC_BLK_CELL_MIN;
    }
    if (nfcNdefIsNone(g_last_ndef)) {
        ndef_h = cell * 3 + NFC_BLK_GAP * 2;
    }
    int kv_h = usr_h + NFC_KV_ROW_GAP + grid_h + NFC_KV_ROW_GAP + ndef_h;
    int y = bottom - kv_h;
    if (y < after_y) {
        y = after_y;
        grid_max_h = max(NFC_BLK_CELL_MIN,
                         bottom - y - usr_h - NFC_KV_ROW_GAP * 2 - ndef_h);
    }

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(x, y);
    d.print("USR");
    int pct = 0;
    if (g_last_total_size > 0) {
        pct = constrain(static_cast<int>(g_last_user_size) * 100 / g_last_total_size, 0, 100);
    }
    drawPercentBar(value_x, y, value_max_w, usr_h, pct, APP_COLOR_LABEL, APP_COLOR_MUTED, BLACK);
    y += usr_h + NFC_KV_ROW_GAP;

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(x, y);
    d.print("BLOCK");
    if (!g_block_status.empty()) {
        grid_h = drawBlockStatusGrid(value_x, y, value_max_w, grid_max_h, &blocks_w, &cell);
    } else {
        const int step = NFC_BLK_CELL_MIN + NFC_BLK_GAP;
        for (int i = 0; i < 16; ++i) {
            d.fillRect(value_x + i * step, y + 2, NFC_BLK_CELL_MIN, NFC_BLK_CELL_MIN, NFC_BLK_IDLE);
        }
        blocks_w = 16 * step - NFC_BLK_GAP;
        cell = NFC_BLK_CELL_MIN;
        grid_h = NFC_BLK_CELL_MIN + 2;
    }
    y += max(grid_h, 8) + NFC_KV_ROW_GAP;

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(x, y);
    d.print("NDEF");
    if (nfcNdefIsNone(g_last_ndef)) {
        const int box_w = max(blocks_w, cell * 3);
        const int box_h = cell * 3 + NFC_BLK_GAP * 2;
        d.fillRect(value_x, y, box_w, box_h, NFC_NDEF_FILL);
        d.drawRect(value_x, y, box_w, box_h, APP_COLOR_MUTED);
    } else {
        d.setTextColor(g_last_all_zero ? APP_COLOR_WARN : APP_COLOR_MUTED, BLACK);
        String ndef = g_last_ndef;
        if (ndef.startsWith("NDEF: ")) {
            ndef = ndef.substring(6);
        } else if (ndef.startsWith("NDEF ")) {
            ndef = ndef.substring(5);
        }
        while (ndef.length() > 0 && d.textWidth(ndef) > value_max_w) {
            ndef.remove(ndef.length() - 1);
        }
        d.setCursor(value_x, y);
        d.print(ndef);
    }
}

// UID 按字节换行；per_line 为每行最多字节数（Write/Emu=4，Read 先试 8）
static int splitUidWrapLines(const String& uid, String* lines, const int max_lines,
                             const int per_line) {
    if (lines == nullptr || max_lines <= 0) {
        return 0;
    }
    const int per = max(1, per_line);
    char bytes[12][3]{};
    int n = 0;
    for (unsigned i = 0; i < uid.length() && n < 12;) {
        const char c = uid[i];
        if (c == ':' || c == '-' || c == ' ') {
            ++i;
            continue;
        }
        if (i + 1 >= uid.length()) {
            break;
        }
        bytes[n][0] = static_cast<char>(toupper(static_cast<unsigned char>(uid[i])));
        bytes[n][1] = static_cast<char>(toupper(static_cast<unsigned char>(uid[i + 1])));
        bytes[n][2] = '\0';
        ++n;
        i += 2;
    }
    if (n <= 0) {
        lines[0] = uid.length() > 0 ? uid : String("--");
        return 1;
    }
    int line = 0;
    for (int i = 0; i < n && line < max_lines;) {
        String s;
        for (int k = 0; k < per && i < n; ++k, ++i) {
            if (k > 0) {
                s += ':';
            }
            s += bytes[i];
        }
        lines[line++] = s;
    }
    return line;
}

static void drawReaderOverview() {
    auto& d = M5Cardputer.Display;
    constexpr int edge = NFC_OVERVIEW_EDGE;
    constexpr int x = NFC_CONTENT_X;
    constexpr int uid_h = 16;
    constexpr int uid_line_gap = 2;
    const int right = d.width() - edge;
    const int bottom = d.height() - edge;
    drawNfcStatusColumn();

    d.setFont(&fonts::Font0);
    d.setTextSize(2);
    d.setTextColor(APP_COLOR_LABEL, BLACK);

    // Read 内容区较宽：先按 8 字节/行；仍放不下再按 4 字节/行
    const int page_reserve = resultPageCount() > 1 ? 28 : 0;
    const int uid_max_w = max(8, right - x - page_reserve);
    String uid_lines[3];
    int uid_line_count = max(1, splitUidWrapLines(g_last_uid, uid_lines, 3, 8));
    bool uid_fits = true;
    for (int i = 0; i < uid_line_count; ++i) {
        if (d.textWidth(uid_lines[i]) > uid_max_w) {
            uid_fits = false;
            break;
        }
    }
    if (!uid_fits) {
        uid_line_count = max(1, splitUidWrapLines(g_last_uid, uid_lines, 3, 4));
    }
    int y = edge;
    for (int i = 0; i < uid_line_count; ++i) {
        d.setCursor(x, y);
        d.print(uid_lines[i]);
        y += uid_h;
        if (i + 1 < uid_line_count) {
            y += uid_line_gap;
        }
    }

    // Auth Error+类型：UID 块下方再空 TYPE_ROW_GAP
    y += NFC_TYPE_ROW_GAP;
    d.setTextSize(1);
    uint16_t fail_color = APP_COLOR_ERROR;
    if (readerReadFailHint(fail_color)) {
        d.setTextColor(fail_color, BLACK);
        String hint = (g_last_key == "Auth Error" || g_last_msg == "Auth Error")
                          ? String("Auth Error")
                          : g_last_msg;
        while (hint.length() > 0 && d.textWidth(hint) > right - x) {
            hint.remove(hint.length() - 1);
        }
        d.setCursor(x, y);
        d.print(hint);
        y += 8 + NFC_TYPE_INNER_GAP;
    }
    const char* type_s =
        (g_last_type != "--" && g_last_type.length() > 0) ? g_last_type.c_str() : "--";
    const char* tech = nfcTechLabel(g_last_picc_type);
    d.setTextColor(YELLOW, BLACK);
    d.setCursor(x, y);
    d.print(type_s);
    d.setTextColor(APP_COLOR_OK, BLACK);
    d.setCursor(x + d.textWidth(type_s) + 6, y);
    d.print(tech);
    const int after_type = y + 8 + NFC_TYPE_ROW_GAP;

    drawUsrBlockNdefKv(x, after_type, right, bottom);

    if (resultPageCount() > 1) {
        char page[12];
        snprintf(page, sizeof(page), "%d/%d", g_result_page + 1, resultPageCount());
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(d.width() - edge - d.textWidth(page), edge);
        d.print(page);
    }
}

static void drawMainView() {
    g_result_page = constrain(g_result_page, 0, resultPageCount() - 1);
    if (g_result_page == 0) {
        drawReaderOverview();
        return;
    }

    auto& d = M5Cardputer.Display;
    constexpr int edge = APP_HELP_EDGE;
    constexpr int x = edge;
    const int per = dumpRowsPerPage();
    const int text_max_w = dumpTextMaxWidth();
    const int visual_total = dumpVisualLineCount();
    const int first = (g_result_page - 1) * per;
    int y = edge;
    d.setFont(&fonts::Font0);
    d.setTextSize(1);

    int line_i = 0;
    visitDumpVisualLines(text_max_w, [&](const int kind, const int start, const int count) {
        if (line_i < first || line_i >= first + per) {
            ++line_i;
            return;
        }
        if (kind == 0) {
            const NfcDumpRow& row = g_dump_rows[static_cast<size_t>(start)];
            char line[96];
            snprintf(line, sizeof(line), "%03u %s", row.block, row.hex.c_str());
            while (strlen(line) > 4 && d.textWidth(line) > text_max_w) {
                line[strlen(line) - 1] = '\0';
            }
            d.setCursor(x, y);
            d.setTextColor(APP_COLOR_LABEL, BLACK);
            d.printf("%03u ", row.block);
            d.setTextColor(APP_COLOR_VALUE, BLACK);
            d.print(line + 4);
        } else {
            int cx = x;
            d.setTextColor(APP_COLOR_ERROR, BLACK);
            for (int k = 0; k < count; ++k) {
                const NfcDumpRow& row = g_dump_rows[static_cast<size_t>(start + k)];
                char num[8];
                snprintf(num, sizeof(num), "%03u", row.block);
                d.setCursor(cx, y);
                d.print(num);
                cx += d.textWidth(num) + 4;
            }
        }
        y += NFC_DUMP_ROW_H;
        ++line_i;
    });
    drawAppScrollbar(d, edge, d.height() - edge * 2, visual_total, per, first);
}

// 记录详情：第 0 页 meta + USR/BLOCK/NDEF 图形；其后为 dump
static void drawDetailView() {
    auto& d = M5Cardputer.Display;
    constexpr int edge = APP_HELP_EDGE;
    constexpr int x = edge;
    const int right = d.width() - edge - APP_SCROLLBAR_W - 2;
    const int bottom = d.height() - edge;
    const int text_max_w = dumpTextMaxWidth();
    g_result_page = constrain(g_result_page, 0, detailPageCount() - 1);

    if (g_result_page == 0) {
        int y = detailContentTop();
        d.setFont(&fonts::Font0);
        d.setTextSize(1);
        for (int line = 0; line < NFC_DETAIL_TEXT_META_LINES; ++line) {
            drawDetailMetaLine(line, x, y, text_max_w);
            y += NFC_DUMP_ROW_H;
        }
        drawUsrBlockNdefKv(x, y + NFC_KV_ROW_GAP, right, bottom);
        return;
    }

    const int per = detailDumpRowsPerPage();
    const int visual_total = dumpVisualLineCount();
    const int first = (g_result_page - 1) * per;
    int y = detailContentTop();
    d.setFont(&fonts::Font0);
    d.setTextSize(1);

    int line_i = 0;
    visitDumpVisualLines(text_max_w, [&](const int kind, const int start, const int count) {
        if (line_i < first || line_i >= first + per) {
            ++line_i;
            return;
        }
        if (kind == 0) {
            const NfcDumpRow& row = g_dump_rows[static_cast<size_t>(start)];
            char hex[96];
            snprintf(hex, sizeof(hex), "%s", row.hex.c_str());
            while (strlen(hex) > 0 && d.textWidth("000 ") + d.textWidth(hex) > text_max_w) {
                hex[strlen(hex) - 1] = '\0';
            }
            d.setCursor(x, y);
            d.setTextColor(APP_COLOR_LABEL, BLACK);
            d.printf("%03u ", row.block);
            d.setTextColor(APP_COLOR_VALUE, BLACK);
            d.print(hex);
        } else {
            int cx = x;
            d.setTextColor(APP_COLOR_ERROR, BLACK);
            for (int k = 0; k < count; ++k) {
                const NfcDumpRow& row = g_dump_rows[static_cast<size_t>(start + k)];
                char num[8];
                snprintf(num, sizeof(num), "%03u", row.block);
                d.setCursor(cx, y);
                d.print(num);
                cx += d.textWidth(num) + 4;
            }
        }
        y += NFC_DUMP_ROW_H;
        ++line_i;
    });
    drawAppScrollbar(d, detailContentTop(), d.height() - detailContentTop() - edge, visual_total,
                     per, first);
}

static void drawHistoryView() {
    auto& d = M5Cardputer.Display;
    constexpr int x = APP_HELP_EDGE;
    const int y = APP_HELP_EDGE + 12;
    const int list_w = d.width() - APP_HELP_EDGE * 2 - APP_SCROLLBAR_W - 2;
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
            d.fillRect(x, ry, list_w, NFC_HISTORY_ROW_H, APP_COLOR_MENU_KEY);
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
        while (line[0] != '\0' && d.textWidth(line) > list_w - NFC_HISTORY_PAD * 2) {
            line[strlen(line) - 1] = '\0';
        }
        d.print(line);
    }
    drawAppScrollbar(d, y, NFC_HISTORY_VISIBLE * NFC_HISTORY_ROW_H, g_history_count,
                     NFC_HISTORY_VISIBLE, first);
}

// 与 WiFi 密码框同色：深底 + 金强调
static uint16_t nfcInputCardBg() {
    return M5Cardputer.Display.color565(0x0D, 0x16, 0x22);
}
static uint16_t nfcInputAccentGold() {
    return M5Cardputer.Display.color565(0xE9, 0xC4, 0x6A);
}

static constexpr int NFC_INPUT_H = 28;
static constexpr int NFC_INPUT_PAD_X = 8;
static constexpr int NFC_INPUT_CARET_W = 3;
static constexpr int NFC_INPUT_LABEL_Y = APP_HELP_EDGE + 16;
static constexpr int NFC_INPUT_BOX_Y = NFC_INPUT_LABEL_Y + 12;

// WiFi 风格单行输入：标签行 + 大字圆角框 + 金色光标（支持中段光标滚动）
static void drawNfcTextInput(const char* label, const String& text, const int cursor,
                             const int max_len, const char* status = nullptr) {
    auto& d = M5Cardputer.Display;
    const int card_x = APP_HELP_EDGE;
    const int card_w = d.width() - card_x * 2;
    const uint16_t card_bg = nfcInputCardBg();
    const uint16_t gold = nfcInputAccentGold();
    const int label_y = NFC_INPUT_LABEL_Y;
    const int input_y = NFC_INPUT_BOX_Y;
    const size_t len = text.length();

    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    d.setTextColor(APP_COLOR_LABEL, BLACK);
    d.setCursor(card_x + 2, label_y);
    d.print(label);

    char count_buf[12];
    snprintf(count_buf, sizeof(count_buf), "%u/%d", static_cast<unsigned>(len), max_len);
    d.setTextColor(APP_COLOR_MUTED, BLACK);
    d.setCursor(card_x + card_w - 2 - d.textWidth(count_buf), label_y);
    d.print(count_buf);

    d.fillRoundRect(card_x, input_y, card_w, NFC_INPUT_H, 4, card_bg);
    d.drawRoundRect(card_x, input_y, card_w, NFC_INPUT_H, 4,
                    len > 0 ? APP_COLOR_OK : gold);

    const int text_x = card_x + NFC_INPUT_PAD_X;
    const int text_max_w = card_w - NFC_INPUT_PAD_X * 2 - NFC_INPUT_CARET_W;
    const int text_y = input_y + (NFC_INPUT_H - infoLineHeight(2)) / 2;
    d.setTextSize(2);

    int caret_px = 0;
    for (int i = 0; i < cursor && i < static_cast<int>(len); ++i) {
        char ch[2] = {text.charAt(i), '\0'};
        caret_px += d.textWidth(ch);
    }
    const int scroll = max(0, caret_px - text_max_w);

    d.setTextColor(WHITE, card_bg);
    int cx = text_x - scroll;
    for (int i = 0; i < static_cast<int>(len); ++i) {
        char ch[2] = {text.charAt(i), '\0'};
        const int cw = d.textWidth(ch);
        if (cx + cw > text_x && cx < text_x + text_max_w) {
            if (cx >= text_x) {
                d.setCursor(cx, text_y);
                d.print(ch);
            }
        }
        cx += cw;
    }
    const int caret_x = constrain(text_x + caret_px - scroll, text_x, text_x + text_max_w);
    d.fillRect(caret_x, text_y, NFC_INPUT_CARET_W, infoLineHeight(2), gold);

    if (status != nullptr && status[0] != '\0') {
        d.setTextSize(1);
        d.setTextColor(APP_COLOR_WARN, BLACK);
        d.setCursor(card_x + 2, input_y + NFC_INPUT_H + 5);
        d.print(status);
    }
}

static void drawRenameView() {
    drawNfcTextInput("name", g_rename_text, g_rename_cursor, NFC_NAME_MAX);
}

static void drawEmuMenu(const char* title, const char* const* items, const int count,
                        const int selected) {
    auto& d = M5Cardputer.Display;
    constexpr int x = APP_HELP_EDGE;
    // 8 项 Type 列表：行高 12 → 顶栏下约 96px，135 屏内完整可见
    constexpr int row_h = 12;
    const int top = APP_HELP_EDGE + 14;
    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    for (int i = 0; i < count; ++i) {
        const int y = top + i * row_h;
        const bool active = i == selected;
        if (active) {
            d.fillRoundRect(x, y, d.width() - APP_HELP_EDGE * 2, row_h - 1, 2,
                            APP_COLOR_MENU_KEY);
        }
        d.setTextColor(active ? BLACK : APP_COLOR_LABEL,
                       active ? APP_COLOR_MENU_KEY : BLACK);
        d.setCursor(x + 3, y + 2);
        d.printf("%c %s", active ? '>' : ' ', items[i]);
    }
    (void)title;
}

static void drawEmuSourceView() {
    static const char* const items[] = {"Saved Type2 card", "Custom NDEF tag"};
    drawEmuMenu(wizardBar("EMU SOURCE", "WRITE SOURCE"), items, 2, g_emu_source_sel);
}

static void drawEmuRecordView() {
    auto& d = M5Cardputer.Display;
    const int count = emulatableRecordCount();
    if (count == 0) {
        d.setTextSize(1);
        d.setTextColor(APP_COLOR_HINT, BLACK);
        d.setCursor(APP_HELP_EDGE, APP_HELP_EDGE + 20);
        d.print(wizardIsWrite() ? "No writable saved cards" : "No emulatable saved cards");
        return;
    }
    constexpr int visible = 7;
    constexpr int row_h = 15;
    const int first = min(max(0, g_emu_record_sel - 3), max(0, count - visible));
    const int top = APP_HELP_EDGE + 17;
    for (int row = 0; row < visible && first + row < count; ++row) {
        const int idx = first + row;
        const NfcRecord* rec = emulatableRecordAt(idx);
        if (rec == nullptr) {
            continue;
        }
        const int y = top + row * row_h;
        const bool active = idx == g_emu_record_sel;
        if (active) {
            d.fillRoundRect(APP_HELP_EDGE, y, d.width() - APP_HELP_EDGE * 2 - 4, row_h - 2, 2,
                            APP_COLOR_MENU_KEY);
        }
        d.setTextColor(active ? BLACK : APP_COLOR_LABEL,
                       active ? APP_COLOR_MENU_KEY : BLACK);
        d.setCursor(APP_HELP_EDGE + 3, y + 2);
        String line = rec->name + "  " + rec->type;
        while (line.length() > 0 && d.textWidth(line) > d.width() - 18) {
            line.remove(line.length() - 1);
        }
        d.print(line);
    }
    drawAppScrollbar(d, top, visible * row_h, count, visible, first);
}

static void drawEmuTypeView() {
    const char* names[NFC_EMU_TYPE_COUNT]{};
    for (int i = 0; i < NFC_EMU_TYPE_COUNT; ++i) {
        names[i] = kEmuTypes[i].name;
    }
    drawEmuMenu("EMU TYPE", names, NFC_EMU_TYPE_COUNT, g_emu_type_sel);
}

static void drawEmuEditor(const char* label, const String& text, const int cursor,
                          const int max_len, const char* status = nullptr) {
    drawNfcTextInput(label, text, cursor, max_len, status);
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

// EMU 布局：状态字在图标正下方；被动刷新只重画这一块
static void emuLayout(int& left_y, int& icon_x, int& icon_y, int& status_y) {
    auto& d = M5Cardputer.Display;
    constexpr int edge = NFC_OVERVIEW_EDGE;
    constexpr int title_h = 16;
    constexpr int title_gap = 15;
    constexpr int uid_h = 16;
    constexpr int uid_gap = 4;
    constexpr int type_h = 8;
    constexpr int type_gap = 4;
    constexpr int tech_h = 8;
    constexpr int status_gap = 2;
    constexpr int status_h = 8;
    const int left_h = title_h + title_gap + uid_h + uid_gap + type_h + type_gap + tech_h;
    const int right_h = NFC_ICON_H + status_gap + status_h;
    left_y = max(edge, (d.height() - left_h) / 2);
    icon_y = max(edge, (d.height() - right_h) / 2);
    icon_x = d.width() - edge - NFC_ICON_W;
    status_y = icon_y + NFC_ICON_H + status_gap;
}

static void drawEmulationStatus() {
    auto& d = M5Cardputer.Display;
    int left_y = 0;
    int icon_x = 0;
    int icon_y = 0;
    int status_y = 0;
    emuLayout(left_y, icon_x, icon_y, status_y);
    (void)left_y;
    (void)icon_y;

    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    d.fillRect(icon_x, status_y, NFC_ICON_W, 8, BLACK);
    const char* st = g_emulation_on ? emulationStateName(g_emu_state) : "ERR";
    d.setTextColor(g_emulation_on ? APP_COLOR_HINT : APP_COLOR_ERROR, BLACK);
    d.setCursor(icon_x + (NFC_ICON_W - d.textWidth(st)) / 2, status_y);
    d.print(st);
}

// EMU：左文案 + 右图标，整块纵向居中；UID 与 WRITE 相同按 4 字节换行
static void drawEmulationView() {
    auto& d = M5Cardputer.Display;
    constexpr int edge = NFC_OVERVIEW_EDGE;
    constexpr int title_h = 16; // Font0 x2
    constexpr int title_gap = 15;
    constexpr int uid_h = 16;
    constexpr int uid_line_gap = 2;
    constexpr int uid_gap = 4;
    constexpr int type_h = 8;
    constexpr int type_gap = 4;
    constexpr int tech_h = 8;
    constexpr int status_gap = 2;
    constexpr int status_h = 8;

    const String uid_text =
        g_emulation_on ? String(g_emu_picc.uidAsString().c_str()) : String("--");
    String uid_lines[3];
    const int uid_line_count = max(1, splitUidWrapLines(uid_text, uid_lines, 3, 4));
    const bool show_err = !g_emulation_on;
    const int uid_block_h =
        uid_line_count * uid_h + max(0, uid_line_count - 1) * uid_line_gap;
    const int left_h = title_h + title_gap + uid_block_h + uid_gap + type_h + type_gap +
                       tech_h + (show_err ? status_gap + 8 : 0);
    const int right_h = NFC_ICON_H + status_gap + status_h;
    const int left_y = max(edge, (d.height() - left_h) / 2);
    const int icon_y = max(edge, (d.height() - right_h) / 2);
    const int icon_x = d.width() - edge - NFC_ICON_W;

    if (!drawLittleFsPng(NFC_ICON_EMU, icon_x, icon_y)) {
        const int cx = icon_x + NFC_ICON_W / 2;
        const int cy = icon_y + NFC_ICON_H / 2;
        d.drawArc(cx, cy, 6, 5, 210, 330, MAGENTA);
        d.drawArc(cx, cy, 10, 9, 210, 330, MAGENTA);
        d.drawArc(cx, cy, 14, 13, 210, 330, MAGENTA);
    }

    const int x = edge;
    int y = left_y;
    d.setFont(&fonts::Font0);
    d.setTextSize(2);
    d.setTextColor(MAGENTA, BLACK);
    d.setCursor(x, y);
    d.print("EMU");
    y += title_h + title_gap;

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    for (int i = 0; i < uid_line_count; ++i) {
        d.setCursor(x, y);
        d.print(uid_lines[i]);
        y += uid_h;
        if (i + 1 < uid_line_count) {
            y += uid_line_gap;
        }
    }
    y += uid_gap;

    d.setTextSize(1);
    d.setTextColor(YELLOW, BLACK);
    d.setCursor(x, y);
    d.print(g_emulation_on ? g_emu_picc.typeAsString().c_str() : "--");
    y += type_h + type_gap;

    d.setTextColor(APP_COLOR_OK, BLACK);
    d.setCursor(x, y);
    d.print(g_emulation_on ? nfcTechLabel(g_emu_picc.type) : "--");
    y += tech_h + status_gap;

    // 启动失败时仍停在 EMU 页，把原因画出来（此前失败会立刻退回主界面，看起来像按 E 无反应）
    if (show_err) {
        d.setTextColor(APP_COLOR_ERROR, BLACK);
        String err = g_last_msg.length() > 0 ? g_last_msg : String("emulation off");
        const int max_w = max(8, icon_x - edge - 4);
        while (err.length() > 0 && d.textWidth(err) > max_w) {
            err.remove(err.length() - 1);
        }
        d.setCursor(x, y);
        d.print(err);
    }

    drawEmulationStatus();
}

static const char* writeSessionStateName(const WriteSessionState state) {
    switch (state) {
        case WriteSessionState::Wait:
            return "WAIT";
        case WriteSessionState::Busy:
            return "WRITE";
        case WriteSessionState::Ok:
            return "OK";
        case WriteSessionState::Fail:
            return "ERR";
        default:
            return "--";
    }
}

static void drawWriteStatus() {
    auto& d = M5Cardputer.Display;
    int left_y = 0;
    int icon_x = 0;
    int icon_y = 0;
    int status_y = 0;
    emuLayout(left_y, icon_x, icon_y, status_y);
    (void)left_y;
    (void)icon_y;

    d.setFont(&fonts::Font0);
    d.setTextSize(1);
    d.fillRect(icon_x, status_y, NFC_ICON_W, 8, BLACK);
    const char* st = writeSessionStateName(g_write_state);
    uint16_t color = APP_COLOR_HINT;
    if (g_write_state == WriteSessionState::Ok) {
        color = APP_COLOR_OK;
    } else if (g_write_state == WriteSessionState::Fail) {
        color = APP_COLOR_ERROR;
    } else if (g_write_state == WriteSessionState::Busy) {
        color = APP_COLOR_WARN;
    }
    d.setTextColor(color, BLACK);
    d.setCursor(icon_x + (NFC_ICON_W - d.textWidth(st)) / 2, status_y);
    d.print(st);
}

// WRITE 运行页：布局与 EMU 相同；左侧为待写/已刷信息，右侧图标+状态
static void drawWriteView() {
    auto& d = M5Cardputer.Display;
    constexpr int edge = NFC_OVERVIEW_EDGE;
    constexpr int title_h = 16;
    constexpr int title_gap = 15;
    constexpr int uid_h = 16;
    constexpr int uid_line_gap = 2;
    constexpr int uid_gap = 4;
    constexpr int type_h = 8;
    constexpr int type_gap = 4;
    constexpr int tech_h = 8;
    constexpr int status_gap = 2;
    constexpr int status_h = 8;

    String uid_lines[3];
    const int uid_line_count =
        max(1, splitUidWrapLines(g_write_show_uid, uid_lines, 3, 4));
    const bool show_msg = g_write_state == WriteSessionState::Fail ||
                          (g_write_state == WriteSessionState::Wait && g_last_msg.length() > 0);
    const int uid_block_h =
        uid_line_count * uid_h + max(0, uid_line_count - 1) * uid_line_gap;
    const int left_h = title_h + title_gap + uid_block_h + uid_gap + type_h + type_gap +
                       tech_h + (show_msg ? status_gap + 8 : 0);
    const int right_h = NFC_ICON_H + status_gap + status_h;
    const int left_y = max(edge, (d.height() - left_h) / 2);
    const int icon_y = max(edge, (d.height() - right_h) / 2);
    const int icon_x = d.width() - edge - NFC_ICON_W;

    const char* icon = (g_write_state == WriteSessionState::Wait ||
                        g_write_state == WriteSessionState::Busy)
                           ? NFC_ICON_ACTIVE
                           : NFC_ICON_IDLE;
    if (!drawLittleFsPng(icon, icon_x, icon_y)) {
        const int cx = icon_x + NFC_ICON_W / 2;
        const int cy = icon_y + NFC_ICON_H / 2;
        const uint16_t arc = g_write_state == WriteSessionState::Fail ? RED : YELLOW;
        d.drawArc(cx, cy, 6, 5, 210, 330, arc);
        d.drawArc(cx, cy, 10, 9, 210, 330, arc);
        d.drawArc(cx, cy, 14, 13, 210, 330, arc);
    }

    const int x = edge;
    int y = left_y;
    d.setFont(&fonts::Font0);
    d.setTextSize(2);
    d.setTextColor(YELLOW, BLACK);
    d.setCursor(x, y);
    d.print("WRITE");
    y += title_h + title_gap;

    d.setTextColor(APP_COLOR_LABEL, BLACK);
    for (int i = 0; i < uid_line_count; ++i) {
        d.setCursor(x, y);
        d.print(uid_lines[i]);
        y += uid_h;
        if (i + 1 < uid_line_count) {
            y += uid_line_gap;
        }
    }
    y += uid_gap;

    d.setTextSize(1);
    d.setTextColor(YELLOW, BLACK);
    d.setCursor(x, y);
    d.print(g_write_show_type);
    y += type_h + type_gap;

    d.setTextColor(APP_COLOR_OK, BLACK);
    d.setCursor(x, y);
    {
        String line = g_write_show_line;
        const int max_w = max(8, icon_x - edge - 4);
        while (line.length() > 0 && d.textWidth(line) > max_w) {
            line.remove(line.length() - 1);
        }
        d.print(line);
    }
    y += tech_h + status_gap;

    if (show_msg) {
        d.setTextColor(g_write_state == WriteSessionState::Fail ? APP_COLOR_ERROR
                                                               : APP_COLOR_HINT,
                       BLACK);
        String msg = g_last_msg;
        const int max_w = max(8, icon_x - edge - 4);
        while (msg.length() > 0 && d.textWidth(msg) > max_w) {
            msg.remove(msg.length() - 1);
        }
        d.setCursor(x, y);
        d.print(msg);
    }

    drawWriteStatus();
}

static void drawHelpPage() {
    static const AppHelpLine kLines[] = {
        appHelpTextColored("Reader/Writer", APP_COLOR_LABEL),
        appHelpKey('r', "scan until card found"),
        appHelpKey('w', "open write setup"),
        appHelpKey('o', "toggle save history"),
        appHelpKey('l', "open read history"),
        appHelpArrows("result / dump pages"),
        appHelpText("blk map: green=ok red=fail"),
        appHelpKey('e', "open emulation setup"),
        appHelpTextColored("History", APP_COLOR_LABEL),
        appHelpArrows("pick history item"),
        appHelpBadge("Enter", "open detail list"),
        appHelpArrows("scroll detail pages"),
        appHelpKey('r', "rename selected record"),
        appHelpBadge("Bksp", "delete selected record"),
        appHelpBadge("ESC", "dump→read; detail→list→read"),
        appHelpTextColored("Write / Emulation", APP_COLOR_LABEL),
        appHelpText("Same setup: saved card or custom"),
        appHelpText("Custom: type, UID, then text"),
        appHelpText("Write end: WAIT→WRITE→OK screen"),
        appHelpText("Emu clones UID + full Type2 dump"),
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
    // 列表/编辑页用顶栏；Reader / Emulation 运行态自绘布局
    if (g_view == NfcView::HISTORY) {
        drawTitleBar("RECORDS");
    } else if (g_view == NfcView::RENAME) {
        drawTitleBar("RENAME");
    } else if (g_view == NfcView::DETAIL) {
        g_result_page = constrain(g_result_page, 0, detailPageCount() - 1);
        if (detailPageCount() > 1) {
            char page[12];
            snprintf(page, sizeof(page), "%d/%d", g_result_page + 1, detailPageCount());
            drawTitleBar(page);
        } else {
            drawTitleBar("DETAIL");
        }
    } else if (g_view == NfcView::EMU_SOURCE) {
        drawTitleBar(wizardBar("EMU SOURCE", "WRITE SOURCE"));
    } else if (g_view == NfcView::EMU_RECORD) {
        drawTitleBar(wizardBar("EMU LIST", "WRITE LIST"));
    } else if (g_view == NfcView::EMU_TYPE) {
        drawTitleBar(wizardBar("EMU TYPE", "WRITE TYPE"));
    } else if (g_view == NfcView::EMU_UID) {
        drawTitleBar(wizardBar("EMU UID", "WRITE UID"));
    } else if (g_view == NfcView::EMU_PAYLOAD) {
        drawTitleBar(wizardBar("EMU DATA", "WRITE DATA"));
    }
    if (g_view == NfcView::MAIN) {
        drawMainView();
    } else if (g_view == NfcView::HISTORY) {
        drawHistoryView();
    } else if (g_view == NfcView::DETAIL) {
        drawDetailView();
    } else if (g_view == NfcView::RENAME) {
        drawRenameView();
    } else if (g_view == NfcView::EMU_SOURCE) {
        drawEmuSourceView();
    } else if (g_view == NfcView::EMU_RECORD) {
        drawEmuRecordView();
    } else if (g_view == NfcView::EMU_TYPE) {
        drawEmuTypeView();
    } else if (g_view == NfcView::EMU_UID) {
        const char* status =
            g_last_msg.startsWith("UID") ? g_last_msg.c_str() : nullptr;
        drawEmuEditor("uid hex (4/7/10 bytes)", g_emu_uid, g_emu_edit_cursor, 29, status);
    } else if (g_view == NfcView::EMU_PAYLOAD) {
        drawEmuEditor("ndef text", g_emu_payload, g_emu_edit_cursor, 120);
    } else if (g_view == NfcView::WRITING) {
        drawWriteView();
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
    g_wizard = NfcWizardMode::None;
    g_emu_label = kDefaultWritePayload;
    g_emu_source_sel = 0;
    g_emu_record_sel = 0;
    g_emu_type_sel = 0;
    g_emu_uid = kDefaultEmuUid;
    g_emu_payload = kDefaultWritePayload;
    g_emu_edit_cursor = 0;
    g_write_memory.clear();
    g_write_show_uid = "--";
    g_write_show_type = "--";
    g_write_show_line = "--";
    g_write_state = WriteSessionState::Idle;
    g_write_ok_msg = "write ok";
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
    g_write_state = WriteSessionState::Idle;
    g_write_memory.clear();
    g_nfc_a.deactivate();
    // 与 initialize 一致：释放注册态，避免下次 enter 时 unit 仍带旧 _manager。
    resetNfcUnitObjects();
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
            // 状态变化只刷图标下方状态字，避免整页闪烁
            if (g_view == NfcView::EMULATION && !g_help_visible) {
                drawEmulationStatus();
            } else {
                drawNfcApp();
            }
        }
        return;
    }
    if (g_view == NfcView::WRITING) {
        pollWriteSession();
        return;
    }
    pollReadScan();
}

void handleNfcApp(const Keyboard_Class::KeysState& status) {
    if ((g_view == NfcView::EMU_UID || g_view == NfcView::EMU_PAYLOAD) &&
        !g_help_visible) {
        String& text = g_view == NfcView::EMU_UID ? g_emu_uid : g_emu_payload;
        const int max_len = g_view == NfcView::EMU_UID ? 29 : 120;
        if (status.fn) {
            for (const uint8_t hid : status.hid_keys) {
                if (hid == 0x52 || hid == 0x50) {
                    g_emu_edit_cursor = max(0, g_emu_edit_cursor - 1);
                    drawNfcApp();
                    return;
                }
                if (hid == 0x51 || hid == 0x4F) {
                    g_emu_edit_cursor = min(static_cast<int>(text.length()),
                                            g_emu_edit_cursor + 1);
                    drawNfcApp();
                    return;
                }
            }
            return;
        }
        if (status.del) {
            if (g_emu_edit_cursor > 0) {
                text.remove(g_emu_edit_cursor - 1, 1);
                --g_emu_edit_cursor;
                if (g_last_msg.startsWith("UID")) {
                    g_last_msg = "";
                }
                drawNfcApp();
            }
            return;
        }
        if (status.enter) {
            if (g_view == NfcView::EMU_UID) {
                uint8_t uid[10]{};
                uint8_t uid_len = 0;
                if (!parseUidHex(g_emu_uid, uid, uid_len)) {
                    g_last_msg = "UID needs 4, 7 or 10 bytes";
                    drawNfcApp();
                    return;
                }
                g_last_msg = "";
                g_view = NfcView::EMU_PAYLOAD;
                g_emu_edit_cursor = g_emu_payload.length();
            } else if (g_emu_payload.length() > 0) {
                if (wizardIsWrite()) {
                    if (startWriteCustom()) {
                        g_view = NfcView::WRITING;
                    }
                } else if (startNfcEmulationCustom()) {
                    g_view = NfcView::EMULATION;
                }
            }
            drawNfcApp();
            return;
        }
        if (status.space && text.length() < static_cast<unsigned>(max_len)) {
            text = text.substring(0, g_emu_edit_cursor) + " " +
                   text.substring(g_emu_edit_cursor);
            ++g_emu_edit_cursor;
            if (g_last_msg.startsWith("UID")) {
                g_last_msg = "";
            }
        }
        for (const char raw : status.word) {
            if (raw == '\b') {
                if (g_emu_edit_cursor > 0) {
                    text.remove(g_emu_edit_cursor - 1, 1);
                    --g_emu_edit_cursor;
                    if (g_last_msg.startsWith("UID")) {
                        g_last_msg = "";
                    }
                }
            } else if (raw != ' ' && raw >= 32 && raw <= 126 &&
                       text.length() < static_cast<unsigned>(max_len)) {
                text = text.substring(0, g_emu_edit_cursor) + raw +
                       text.substring(g_emu_edit_cursor);
                ++g_emu_edit_cursor;
                if (g_last_msg.startsWith("UID")) {
                    g_last_msg = "";
                }
            }
        }
        drawNfcApp();
        return;
    }

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
        const int line_count = 29;
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
        if (status.del && g_history_count > 0) {
            deleteSelectedHistory();
            drawNfcApp();
            return;
        }
        for (const char raw : status.word) {
            if (raw == '\b' && g_history_count > 0) {
                deleteSelectedHistory();
                drawNfcApp();
                return;
            }
        }
    } else if (g_view == NfcView::EMU_SOURCE && help_delta != 0) {
        g_emu_source_sel = constrain(g_emu_source_sel + help_delta, 0, 1);
        drawNfcApp();
        return;
    } else if (g_view == NfcView::EMU_RECORD) {
        const int count = emulatableRecordCount();
        if (help_delta != 0 && count > 0) {
            g_emu_record_sel = constrain(g_emu_record_sel + help_delta, 0, count - 1);
            drawNfcApp();
            return;
        }
        if (status.enter && count > 0) {
            const NfcRecord* rec = emulatableRecordAt(g_emu_record_sel);
            if (rec != nullptr) {
                if (wizardIsWrite()) {
                    if (startWriteFromRecord(*rec)) {
                        g_view = NfcView::WRITING;
                    }
                } else if (startNfcEmulationFromRecord(*rec)) {
                    g_view = NfcView::EMULATION;
                }
            }
            drawNfcApp();
            return;
        }
    } else if (g_view == NfcView::EMU_TYPE) {
        if (help_delta != 0) {
            g_emu_type_sel =
                constrain(g_emu_type_sel + help_delta, 0, NFC_EMU_TYPE_COUNT - 1);
            drawNfcApp();
            return;
        }
        if (status.enter) {
            g_view = NfcView::EMU_UID;
            g_emu_edit_cursor = g_emu_uid.length();
            drawNfcApp();
            return;
        }
    } else if (g_view == NfcView::EMU_SOURCE && status.enter) {
        if (g_emu_source_sel == 0) {
            g_emu_record_sel = 0;
            g_view = NfcView::EMU_RECORD;
        } else {
            g_emu_type_sel = 0;
            g_view = NfcView::EMU_TYPE;
        }
        drawNfcApp();
        return;
    } else if (g_view == NfcView::MAIN && help_delta != 0 && resultPageCount() > 1) {
        g_result_page =
            constrain(g_result_page + help_delta, 0, resultPageCount() - 1);
        drawNfcApp();
        return;
    } else if (g_view == NfcView::DETAIL && help_delta != 0 && detailPageCount() > 1) {
        g_result_page =
            constrain(g_result_page + help_delta, 0, detailPageCount() - 1);
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
            if (g_view == NfcView::MAIN || g_view == NfcView::HISTORY ||
                g_view == NfcView::DETAIL) {
                openWizard(NfcWizardMode::Emu);
                drawNfcApp();
                return;
            }
        }
        if (c == 'w') {
            if (g_view == NfcView::WRITING) {
                leaveWriteView();
                drawNfcApp();
                return;
            }
            if (g_view == NfcView::MAIN || g_view == NfcView::HISTORY ||
                g_view == NfcView::DETAIL) {
                openWizard(NfcWizardMode::Write);
                drawNfcApp();
                return;
            }
        }
        if (c == 'l' && (g_view == NfcView::MAIN || g_view == NfcView::HISTORY ||
                         g_view == NfcView::DETAIL)) {
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
    }
}

bool closeNfcHelp() {
    if (g_help_visible) {
        g_help_visible = false;
        drawNfcApp();
        return true;
    }
    // ESC：dump 页先回概览；再逐层关闭向导 / emulation / rename / detail，最后回 reader。
    if (g_view == NfcView::MAIN && g_result_page > 0) {
        g_result_page = 0;
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::DETAIL && g_result_page > 0) {
        g_result_page = 0;
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::EMULATION) {
        leaveEmulationView();
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::WRITING) {
        leaveWriteView();
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::EMU_PAYLOAD) {
        g_view = NfcView::EMU_UID;
        g_emu_edit_cursor = g_emu_uid.length();
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::EMU_UID) {
        g_view = NfcView::EMU_TYPE;
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::EMU_RECORD || g_view == NfcView::EMU_TYPE) {
        g_view = NfcView::EMU_SOURCE;
        drawNfcApp();
        return true;
    }
    if (g_view == NfcView::EMU_SOURCE) {
        g_wizard = NfcWizardMode::None;
        g_view = g_emu_return_view;
        if (g_view != NfcView::MAIN && g_view != NfcView::HISTORY && g_view != NfcView::DETAIL) {
            g_view = NfcView::MAIN;
        }
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
            case NfcView::EMU_SOURCE:
            case NfcView::EMU_RECORD:
            case NfcView::EMU_TYPE:
            case NfcView::EMU_UID:
            case NfcView::EMU_PAYLOAD:
                feature = wizardIsWrite() ? "write_setup" : "emu_setup";
                break;
            case NfcView::EMULATION:
                feature = "emulate";
                break;
            case NfcView::WRITING:
                feature = "write";
                break;
        }
    }
    strncpy(out, feature, out_len - 1);
    out[out_len - 1] = '\0';
}
