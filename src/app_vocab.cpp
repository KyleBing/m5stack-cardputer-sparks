#include "app_vocab.h"

#include "app_colors.h"
#include "app_common.h"

#include <FS.h>
#include <LittleFS.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr const char* VOCAB_DIR = "/vocabulary";
static constexpr const char* VOCAB_STATE_PATH = "/vocab_known.tsv";
static constexpr const char* VOCAB_STATE_TMP_PATH = "/vocab_known.tmp";
static constexpr int VOCAB_MAX_DICT = 16;
static constexpr int VOCAB_PATH_MAX = 96;
static constexpr int VOCAB_NAME_MAX = 48;
static constexpr uint32_t VOCAB_SAVE_DEBOUNCE_MS = 800;
static constexpr int VOCAB_HELP_PAGES = 2;

struct VocabDict {
    char path[VOCAB_PATH_MAX];
    char name[VOCAB_NAME_MAX];
    uint32_t dict_id;
};

static VocabDict g_dicts[VOCAB_MAX_DICT];
static int g_dict_count = 0;
static int g_dict_idx = 0;

static File g_vocab_file;
static uint32_t* g_offsets = nullptr;
static uint8_t* g_known_bits = nullptr;
static int g_line_count = 0;
static int g_cur_line = 0;
static int g_known_count = 0;
static String g_cur_word = "";
static String g_cur_meaning = "";
static bool g_screen_ready = false;
static bool g_dirty = false;
static uint32_t g_dirty_ms = 0;
static bool g_help = false;
static int g_help_page = 0;

static uint64_t* g_state_hashes = nullptr;
static int g_state_hash_cap = 0;
static int g_state_hash_count = 0;

static uint64_t fnv1a64Append(uint64_t h, const uint8_t* data, const size_t len) {
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t hashDictPath(const char* path) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a64Append(h, reinterpret_cast<const uint8_t*>(path), strlen(path));
    return h;
}

static String normalizeWord(const String& word) {
    String out = word;
    out.trim();
    for (size_t i = 0; i < out.length(); i++) {
        out[i] = static_cast<char>(tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

static uint64_t hashNormalizedWord(const String& word) {
    const String normalized = normalizeWord(word);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < normalized.length(); i++) {
        const uint8_t c = static_cast<uint8_t>(normalized[i]);
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

static void freeCurrentIndex() {
    if (g_vocab_file) {
        g_vocab_file.close();
    }
    if (g_offsets != nullptr) {
        free(g_offsets);
        g_offsets = nullptr;
    }
    if (g_known_bits != nullptr) {
        free(g_known_bits);
        g_known_bits = nullptr;
    }
    g_line_count = 0;
    g_cur_line = 0;
    g_known_count = 0;
    g_cur_word = "";
    g_cur_meaning = "";
}

static void freeStateHashes() {
    if (g_state_hashes != nullptr) {
        free(g_state_hashes);
        g_state_hashes = nullptr;
    }
    g_state_hash_cap = 0;
    g_state_hash_count = 0;
}

static bool stateHashContains(const uint64_t hash) {
    if (g_state_hash_cap <= 0 || g_state_hashes == nullptr) {
        return false;
    }
    int idx = static_cast<int>(hash & static_cast<uint64_t>(g_state_hash_cap - 1));
    for (int i = 0; i < g_state_hash_cap; i++) {
        const uint64_t cur = g_state_hashes[idx];
        if (cur == 0ULL) {
            return false;
        }
        if (cur == hash) {
            return true;
        }
        idx = (idx + 1) & (g_state_hash_cap - 1);
    }
    return false;
}

static void stateHashInsert(const uint64_t hash) {
    if (hash == 0ULL || g_state_hash_cap <= 0 || g_state_hashes == nullptr) {
        return;
    }
    int idx = static_cast<int>(hash & static_cast<uint64_t>(g_state_hash_cap - 1));
    for (int i = 0; i < g_state_hash_cap; i++) {
        if (g_state_hashes[idx] == 0ULL) {
            g_state_hashes[idx] = hash;
            g_state_hash_count++;
            return;
        }
        if (g_state_hashes[idx] == hash) {
            return;
        }
        idx = (idx + 1) & (g_state_hash_cap - 1);
    }
}

static bool parseStateLine(const String& line, uint64_t& dict_id, String& word) {
    const int p = line.indexOf('\t');
    if (p <= 0) {
        return false;
    }
    const String id_hex = line.substring(0, p);
    const String text = line.substring(p + 1);
    if (text.length() == 0) {
        return false;
    }
    char* end = nullptr;
    dict_id = strtoull(id_hex.c_str(), &end, 16);
    if (end == id_hex.c_str()) {
        return false;
    }
    word = text;
    word.trim();
    return word.length() > 0;
}

static void loadKnownWordHashesForDict(const uint64_t dict_id) {
    freeStateHashes();
    if (!LittleFS.exists(VOCAB_STATE_PATH)) {
        return;
    }
    File f = LittleFS.open(VOCAB_STATE_PATH, "r");
    if (!f) {
        return;
    }
    int matched = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        uint64_t id = 0;
        String word;
        if (!parseStateLine(line, id, word)) {
            continue;
        }
        if (id == dict_id) {
            matched++;
        }
    }
    if (matched <= 0) {
        f.close();
        return;
    }

    int cap = 1;
    while (cap < matched * 2) {
        cap <<= 1;
    }
    g_state_hashes = static_cast<uint64_t*>(calloc(static_cast<size_t>(cap), sizeof(uint64_t)));
    if (g_state_hashes == nullptr) {
        f.close();
        return;
    }
    g_state_hash_cap = cap;
    g_state_hash_count = 0;

    f.seek(0);
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        uint64_t id = 0;
        String word;
        if (!parseStateLine(line, id, word) || id != dict_id) {
            continue;
        }
        stateHashInsert(hashNormalizedWord(word));
    }
    f.close();
}

static void parseVocabLine(const String& raw, String& word, String& meaning) {
    word = "";
    meaning = "";
    String line = raw;
    line.trim();
    if (line.length() == 0) {
        return;
    }
    int split = line.indexOf('\t');
    if (split < 0) {
        for (int i = 0; i < static_cast<int>(line.length()); i++) {
            const char c = line[i];
            if (c == ' ' || c == ',') {
                split = i;
                break;
            }
        }
    }
    if (split < 0) {
        word = line;
        return;
    }
    word = line.substring(0, split);
    meaning = line.substring(split + 1);
    word.trim();
    meaning.trim();
}

static bool bitIsKnown(const int idx) {
    if (idx < 0 || idx >= g_line_count || g_known_bits == nullptr) {
        return false;
    }
    const int byte_i = idx >> 3;
    const uint8_t mask = static_cast<uint8_t>(1u << (idx & 7));
    return (g_known_bits[byte_i] & mask) != 0;
}

static void bitSetKnown(const int idx, const bool known) {
    if (idx < 0 || idx >= g_line_count || g_known_bits == nullptr) {
        return;
    }
    const int byte_i = idx >> 3;
    const uint8_t mask = static_cast<uint8_t>(1u << (idx & 7));
    const bool before = (g_known_bits[byte_i] & mask) != 0;
    if (known) {
        g_known_bits[byte_i] |= mask;
    } else {
        g_known_bits[byte_i] &= static_cast<uint8_t>(~mask);
    }
    const bool after = (g_known_bits[byte_i] & mask) != 0;
    if (before != after) {
        g_known_count += after ? 1 : -1;
    }
}

static bool readLineAt(const int idx, String& out) {
    out = "";
    if (!g_vocab_file || g_offsets == nullptr || idx < 0 || idx >= g_line_count) {
        return false;
    }
    if (!g_vocab_file.seek(g_offsets[idx])) {
        return false;
    }
    out = g_vocab_file.readStringUntil('\n');
    out.trim();
    return out.length() > 0;
}

static void loadCurrentLine() {
    String raw;
    if (!readLineAt(g_cur_line, raw)) {
        g_cur_word = "-";
        g_cur_meaning = "read failed";
        return;
    }
    parseVocabLine(raw, g_cur_word, g_cur_meaning);
    if (g_cur_word.length() == 0) {
        g_cur_word = "(blank)";
    }
}

static void drawVocabStatus(const char* message, const char* detail, const uint16_t color) {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setFont(&fonts::efontCN_14);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(color, BLACK);
    M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X, 48);
    M5Cardputer.Display.print(message);
    if (detail != nullptr && detail[0] != '\0') {
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X, 70);
        M5Cardputer.Display.print(detail);
    }
    M5Cardputer.Display.setTextFont(1);
}

static void drawVocabHelp() {
    int y = drawAppHelpBegin("Vocab");
    constexpr int x = APP_HELP_CONTENT_X;
    if (g_help_page == 0) {
        y = drawAppHelpBadge(x, y, "Arrows ,.", "previous / next word");
        y = drawAppHelpBadge(x, y, "O/K", "toggle known");
        y = drawAppHelpKey(x, y, 'r', "random unknown word");
        y = drawAppHelpBadge(x, y, "[]", "switch dictionary");
        y = drawAppHelpBadge(x, y, "BtnGO", "exit app");
        (void)drawAppHelpKey(x, y, 'h', "open / close help");
    } else {
        y = drawAppHelpTextColored(x, y, "Word status", APP_COLOR_LABEL);
        y = drawAppHelpLabelText(x, y, "green", APP_COLOR_OK, " = known");
        y = drawAppHelpLabelText(x, y, "white", APP_COLOR_VALUE, " = learning");
        y = drawAppHelpTextColored(x, y, "Storage", APP_COLOR_LABEL);
        (void)drawAppHelpText(x, y, "Known words are saved automatically.");
    }
    drawAppHelpFooter(g_help_page, VOCAB_HELP_PAGES);
}

static void drawVocabApp(const bool full_init) {
    (void)full_init;
    M5Cardputer.Display.fillScreen(BLACK);
    g_screen_ready = true;

    // 文件名和中文释义统一使用中文 14px 字体。
    M5Cardputer.Display.setFont(&fonts::efontCN_14);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_LABEL, BLACK);
    M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X, APP_HELP_EDGE);
    if (g_dict_count > 0) {
        M5Cardputer.Display.print(g_dicts[g_dict_idx].name);
    } else {
        M5Cardputer.Display.print("no file");
    }
    M5Cardputer.Display.setTextFont(1);

    int y = 24;
    char line_buf[24];
    snprintf(line_buf, sizeof(line_buf), "%d/%d", g_line_count > 0 ? g_cur_line + 1 : 0, g_line_count);
    drawInfoLineAt(APP_HELP_CONTENT_X, y, "line", line_buf, 1);

    y += INFO_LINE_H;
    char known_buf[24];
    const int pct = g_line_count > 0 ? (g_known_count * 100) / g_line_count : 0;
    snprintf(known_buf, sizeof(known_buf), "%d (%d%%)", g_known_count, pct);
    drawInfoLineAt(APP_HELP_CONTENT_X, y, "known", known_buf, 1);

    y += INFO_LINE_H + 5;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(bitIsKnown(g_cur_line) ? APP_COLOR_OK : APP_COLOR_VALUE, BLACK);
    M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X, y);
    M5Cardputer.Display.print(g_cur_word);

    y += INFO_LINE_H_2X + 7;
    M5Cardputer.Display.setFont(&fonts::efontCN_14);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(INFO_LABEL_COLOR, BLACK);
    M5Cardputer.Display.setCursor(APP_HELP_CONTENT_X, y);
    M5Cardputer.Display.print(g_cur_meaning);
    M5Cardputer.Display.setTextFont(1);
}

static bool rebuildIndexForDict(const int dict_idx) {
    freeCurrentIndex();
    freeStateHashes();
    if (dict_idx < 0 || dict_idx >= g_dict_count) {
        return false;
    }

    loadKnownWordHashesForDict(g_dicts[dict_idx].dict_id);
    File f = LittleFS.open(g_dicts[dict_idx].path, "r");
    if (!f) {
        freeStateHashes();
        return false;
    }

    // 单遍顺序扫描：同时建立行偏移和已会位图，避免逐行 seek 重读整个词库。
    int cap = 2048;
    uint32_t* offsets = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * static_cast<size_t>(cap)));
    uint8_t* known_bits =
        static_cast<uint8_t*>(calloc(static_cast<size_t>((cap + 7) / 8), sizeof(uint8_t)));
    if (offsets == nullptr || known_bits == nullptr) {
        free(offsets);
        free(known_bits);
        f.close();
        freeStateHashes();
        return false;
    }
    int lines = 0;
    int known_count = 0;
    while (f.available()) {
        const uint32_t off = f.position();
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        if (lines >= cap) {
            const int next_cap = cap * 2;
            uint32_t* next_offsets = static_cast<uint32_t*>(
                realloc(offsets, sizeof(uint32_t) * static_cast<size_t>(next_cap)));
            if (next_offsets == nullptr) {
                free(offsets);
                free(known_bits);
                f.close();
                freeStateHashes();
                return false;
            }
            offsets = next_offsets;

            const size_t old_bytes = static_cast<size_t>((cap + 7) / 8);
            const size_t next_bytes = static_cast<size_t>((next_cap + 7) / 8);
            uint8_t* next_known_bits = static_cast<uint8_t*>(realloc(known_bits, next_bytes));
            if (next_known_bits == nullptr) {
                free(offsets);
                free(known_bits);
                f.close();
                freeStateHashes();
                return false;
            }
            known_bits = next_known_bits;
            memset(known_bits + old_bytes, 0, next_bytes - old_bytes);
            cap = next_cap;
        }

        offsets[lines] = off;
        String word;
        String meaning;
        parseVocabLine(line, word, meaning);
        if (word.length() > 0 && stateHashContains(hashNormalizedWord(word))) {
            known_bits[lines >> 3] |= static_cast<uint8_t>(1u << (lines & 7));
            known_count++;
        }
        lines++;
        if ((lines & 0xFF) == 0) {
            delay(0);
        }
    }
    f.close();
    freeStateHashes();
    if (lines <= 0) {
        free(offsets);
        free(known_bits);
        return false;
    }

    g_offsets = offsets;
    g_known_bits = known_bits;
    g_line_count = lines;
    g_vocab_file = LittleFS.open(g_dicts[dict_idx].path, "r");
    if (!g_vocab_file) {
        freeCurrentIndex();
        return false;
    }

    g_known_count = known_count;
    g_cur_line = 0;
    loadCurrentLine();
    g_dirty = false;
    return true;
}

static bool loadDictList() {
    g_dict_count = 0;
    g_dict_idx = 0;

    File dir = LittleFS.open(VOCAB_DIR, "r");
    if (!dir || !dir.isDirectory()) {
        return false;
    }

    while (g_dict_count < VOCAB_MAX_DICT) {
        File file = dir.openNextFile();
        if (!file) {
            break;
        }
        if (file.isDirectory()) {
            file.close();
            continue;
        }
        const char* name = file.name();
        const int len = static_cast<int>(strlen(name));
        if (len < 5 || strcmp(name + len - 4, ".txt") != 0) {
            file.close();
            continue;
        }
        VocabDict& d = g_dicts[g_dict_count];
        const char* base = strrchr(name, '/');
        base = base == nullptr ? name : (base + 1);
        // openNextFile() 可能只返回文件名；统一保存 LittleFS 完整路径。
        snprintf(d.path, sizeof(d.path), "%s/%s", VOCAB_DIR, base);
        strncpy(d.name, base, sizeof(d.name) - 1);
        d.name[sizeof(d.name) - 1] = '\0';

        d.dict_id = static_cast<uint32_t>(hashDictPath(d.path) & 0xFFFFFFFFULL);
        g_dict_count++;
        file.close();
    }
    dir.close();
    return g_dict_count > 0;
}

static bool saveKnownStateNow() {
    if (g_dict_count <= 0 || g_dict_idx < 0 || g_dict_idx >= g_dict_count) {
        return false;
    }
    File out = LittleFS.open(VOCAB_STATE_TMP_PATH, "w");
    if (!out) {
        return false;
    }

    // 先拷贝其它词库记录，当前词库在后面重建，避免残留旧状态。
    if (LittleFS.exists(VOCAB_STATE_PATH)) {
        File in = LittleFS.open(VOCAB_STATE_PATH, "r");
        if (in) {
            while (in.available()) {
                String line = in.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    continue;
                }
                uint64_t dict_id = 0;
                String word;
                if (!parseStateLine(line, dict_id, word)) {
                    continue;
                }
                if (dict_id == g_dicts[g_dict_idx].dict_id) {
                    continue;
                }
                char buf[20];
                snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(dict_id));
                out.print(buf);
                out.print('\t');
                out.println(normalizeWord(word));
            }
            in.close();
        }
    }

    char cur_id[20];
    snprintf(cur_id, sizeof(cur_id), "%llx",
             static_cast<unsigned long long>(g_dicts[g_dict_idx].dict_id));
    for (int i = 0; i < g_line_count; i++) {
        if (!bitIsKnown(i)) {
            continue;
        }
        String raw;
        if (!readLineAt(i, raw)) {
            continue;
        }
        String word;
        String meaning;
        parseVocabLine(raw, word, meaning);
        word = normalizeWord(word);
        if (word.length() == 0) {
            continue;
        }
        out.print(cur_id);
        out.print('\t');
        out.println(word);
    }
    out.close();

    LittleFS.remove(VOCAB_STATE_PATH);
    if (!LittleFS.rename(VOCAB_STATE_TMP_PATH, VOCAB_STATE_PATH)) {
        LittleFS.remove(VOCAB_STATE_TMP_PATH);
        return false;
    }
    g_dirty = false;
    return true;
}

static void markDirty() {
    g_dirty = true;
    g_dirty_ms = millis();
}

static void stepLine(const int delta) {
    if (g_line_count <= 0) {
        return;
    }
    g_cur_line += delta;
    if (g_cur_line < 0) {
        g_cur_line = g_line_count - 1;
    }
    if (g_cur_line >= g_line_count) {
        g_cur_line = 0;
    }
    loadCurrentLine();
    drawVocabApp(false);
}

static void jumpRandomUnknown() {
    if (g_line_count <= 0) {
        return;
    }
    if (g_known_count >= g_line_count) {
        stepLine(1);
        return;
    }
    for (int i = 0; i < 64; i++) {
        const int idx = random(g_line_count);
        if (!bitIsKnown(idx)) {
            g_cur_line = idx;
            loadCurrentLine();
            drawVocabApp(false);
            return;
        }
    }
    for (int i = 0; i < g_line_count; i++) {
        if (!bitIsKnown(i)) {
            g_cur_line = i;
            loadCurrentLine();
            drawVocabApp(false);
            return;
        }
    }
}

static void switchDict(const int delta) {
    if (g_dict_count <= 1) {
        return;
    }
    int next = g_dict_idx + delta;
    if (next < 0) {
        next = g_dict_count - 1;
    }
    if (next >= g_dict_count) {
        next = 0;
    }
    if (next == g_dict_idx) {
        return;
    }
    if (g_dirty) {
        saveKnownStateNow();
    }
    g_dict_idx = next;
    g_screen_ready = false;
    drawVocabStatus("载入词库...", g_dicts[g_dict_idx].name, APP_COLOR_LABEL);
    if (!rebuildIndexForDict(g_dict_idx)) {
        g_cur_word = "error";
        g_cur_meaning = "load dict failed";
    }
    drawVocabApp(true);
}

void enterVocabApp() {
    randomSeed(micros());
    g_screen_ready = false;
    g_dirty = false;
    g_dirty_ms = 0;
    g_help = false;
    g_help_page = 0;

    drawVocabStatus("载入词库...", nullptr, APP_COLOR_LABEL);
    if (!loadDictList()) {
        freeCurrentIndex();
        g_screen_ready = false;
        drawVocabStatus("载入失败", "no /vocabulary/*.txt", APP_COLOR_ERROR);
        return;
    }

    drawVocabStatus("载入词库...", g_dicts[g_dict_idx].name, APP_COLOR_LABEL);
    if (!rebuildIndexForDict(g_dict_idx)) {
        drawVocabStatus("载入失败", g_dicts[g_dict_idx].name, APP_COLOR_ERROR);
        return;
    }
    drawVocabApp(true);
}

void leaveVocabApp() {
    // 离开学习页前强制落盘，避免最后一次标记丢失。
    if (g_dirty) {
        saveKnownStateNow();
    }
    g_help = false;
    g_help_page = 0;
    freeCurrentIndex();
    freeStateHashes();
}

bool isVocabHelpVisible() {
    return g_help;
}

bool closeVocabHelp() {
    if (!g_help) {
        return false;
    }
    g_help = false;
    g_help_page = 0;
    drawVocabApp(true);
    return true;
}

void updateVocabApp() {
    if (!g_dirty) {
        return;
    }
    if (millis() - g_dirty_ms < VOCAB_SAVE_DEBOUNCE_MS) {
        return;
    }
    saveKnownStateNow();
}

void handleVocabApp(const Keyboard_Class::KeysState& status) {
    const String key = getPressedKey();
    if (key == "h" || key == "H") {
        if (!closeVocabHelp()) {
            g_help = true;
            g_help_page = 0;
            drawVocabHelp();
        }
        return;
    }
    if (g_help) {
        const int delta = getHelpNavDelta(status);
        if (delta != 0) {
            g_help_page = applyHelpPageDelta(g_help_page, VOCAB_HELP_PAGES, delta);
            drawVocabHelp();
        }
        return;
    }

    bool handled = false;
    for (const char c : status.word) {
        if (c == ',') {
            stepLine(-1);
            handled = true;
        } else if (c == '.') {
            stepLine(1);
            handled = true;
        } else if (c == 'r' || c == 'R') {
            jumpRandomUnknown();
            handled = true;
        } else if (c == 'o' || c == 'O' || c == 'k' || c == 'K') {
            if (g_line_count > 0) {
                const bool next = !bitIsKnown(g_cur_line);
                bitSetKnown(g_cur_line, next);
                markDirty();
                drawVocabApp(false);
            }
            handled = true;
        } else if (c == '[') {
            switchDict(-1);
            handled = true;
        } else if (c == ']') {
            switchDict(1);
            handled = true;
        }
    }
    if (!handled) {
        const int nav = getMenuNavDelta(status);
        if (nav != 0) {
            stepLine(nav);
        }
    }
}
