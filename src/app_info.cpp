#include "app_info.h"
#include "app_common.h"
#include "app_header.h"
#include "app_screenshot.h"
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_timer.h>

static constexpr int INFO_HINT_H = 12;
static constexpr int INFO_BODY_SIZE = 1;
static constexpr int INFO_MAX_LINES = 8;
static constexpr int INFO_BAR_H = 8;
static constexpr int INFO_BAR_GAP = 3;
static constexpr uint32_t INFO_REFRESH_MS = 500;
// LittleFS / WiFi / 存储查询较慢，单独降频，避免卡住刷新
static constexpr uint32_t INFO_SLOW_SAMPLE_MS = 2000;

enum class InfoPage : uint8_t {
    Mem = 0,
    Storage = 1,
    Chip = 2,
    Fw = 3,
    Net = 4,
    Run = 5,
    Count = 6,
};

struct InfoLine {
    const char* label;
    const char* value;
};

struct InfoSection {
    InfoLine lines[INFO_MAX_LINES];
    int line_count;
};

static int g_info_page = 0;
static int g_header_page = -1;
static bool g_screen_ready = false;
static uint32_t g_info_last_draw_ms = 0;
static uint32_t g_info_last_slow_ms = 0;

// 当前页展示缓存（先画缓存，再异步感采样更新）
static InfoSection g_sec_cache = {};
static bool g_sec_cache_valid = false;

// Mem 页缓存
static uint32_t g_mem_heap_used = 0;
static uint32_t g_mem_heap_total = 0;
static uint32_t g_mem_psram_used = 0;
static uint32_t g_mem_psram_total = 0;
static uint32_t g_mem_sketch = 0;
static uint32_t g_mem_sketch_part = 0;
static uint32_t g_mem_lfs_used = 0;
static uint32_t g_mem_lfs_total = 0;
static bool g_mem_lfs_ok = false;
static char g_mem_maxa[20] = "-";
static char g_mem_hmin[20] = "-";
static bool g_mem_cache_valid = false;

// Mem 上次已绘制快照（定时刷新只重画变化行）
static uint32_t g_drawn_mem_heap_used = 0;
static uint32_t g_drawn_mem_heap_total = 0;
static uint32_t g_drawn_mem_psram_used = 0;
static uint32_t g_drawn_mem_psram_total = 0;
static uint32_t g_drawn_mem_sketch = 0;
static uint32_t g_drawn_mem_sketch_part = 0;
static uint32_t g_drawn_mem_lfs_used = 0;
static uint32_t g_drawn_mem_lfs_total = 0;
static bool g_drawn_mem_lfs_ok = false;
static char g_drawn_mem_maxa[20] = "";
static char g_drawn_mem_hmin[20] = "";
static bool g_drawn_mem_valid = false;

// Storage 页：Flash(LittleFS) + TF；无卡时只探测一次，避免反复 SD.begin
static uint32_t g_sto_flash_used = 0;
static uint32_t g_sto_flash_total = 0;
static uint32_t g_sto_flash_free = 0;
static bool g_sto_flash_ok = false;
static uint32_t g_sto_tf_used = 0;
static uint32_t g_sto_tf_total = 0;
static uint32_t g_sto_tf_free = 0;
static bool g_sto_tf_ok = false;
static bool g_sto_tf_probed = false;
static bool g_sto_cache_valid = false;

// Storage 上次已绘制快照
static uint32_t g_drawn_sto_flash_used = 0;
static uint32_t g_drawn_sto_flash_free = 0;
static uint32_t g_drawn_sto_flash_total = 0;
static bool g_drawn_sto_flash_ok = false;
static uint32_t g_drawn_sto_tf_used = 0;
static uint32_t g_drawn_sto_tf_free = 0;
static uint32_t g_drawn_sto_tf_total = 0;
static bool g_drawn_sto_tf_ok = false;
static bool g_drawn_sto_valid = false;

// 文字页上次已绘制行（按值比较，跳过未变行）
static char g_drawn_sec_vals[INFO_MAX_LINES][28] = {};
static int g_drawn_sec_count = 0;
static int g_drawn_sec_page = -1;

// 文字页静态缓冲（build 时写入，draw 时读）
static char g_buf_model[24];
static char g_buf_rev[8];
static char g_buf_cores[8];
static char g_buf_freq[16];
static char g_buf_feat[20];
static char g_buf_flash[16];
static char g_buf_fspd[16];
static char g_buf_fmode[8];
static char g_buf_sdk[24];
static char g_buf_sketch[16];
static char g_buf_free_sk[16];
static char g_buf_mac[20];
static char g_buf_ssid[20] = "";
static char g_buf_ip[20] = "";
static char g_buf_rssi[12] = "";
static char g_buf_net[16];
static char g_buf_uptime[24];
static char g_buf_reset[16];

static const char* infoPageAccent(const int page) {
    switch (static_cast<InfoPage>(page)) {
        case InfoPage::Mem:
            return "Memory";
        case InfoPage::Storage:
            return "Storage";
        case InfoPage::Chip:
            return "Chip";
        case InfoPage::Fw:
            return "Firmware";
        case InfoPage::Net:
            return "Network";
        case InfoPage::Run:
            return "Runtime";
        default:
            return "";
    }
}

// 复位原因文案
static const char* infoResetReasonText() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "ext";
        case ESP_RST_SW:
            return "sw";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "wdt";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unknown";
    }
}

// Flash 模式文案
static const char* infoFlashModeText() {
    switch (ESP.getFlashChipMode()) {
        case FM_QIO:
            return "QIO";
        case FM_QOUT:
            return "QOUT";
        case FM_DIO:
            return "DIO";
        case FM_DOUT:
            return "DOUT";
        default:
            return "N/A";
    }
}

// 芯片特性摘要（WiFi/BT/BLE）
static void formatInfoFeatures(char* out, const size_t out_size, const uint32_t features) {
    out[0] = '\0';
    auto append = [&](const char* token) {
        if (out[0] != '\0') {
            strncat(out, "/", out_size - strlen(out) - 1);
        }
        strncat(out, token, out_size - strlen(out) - 1);
    };
    if (features & CHIP_FEATURE_WIFI_BGN) {
        append("WiFi");
    }
    if (features & CHIP_FEATURE_BT) {
        append("BT");
    }
    if (features & CHIP_FEATURE_BLE) {
        append("BLE");
    }
    if (out[0] == '\0') {
        strncpy(out, "none", out_size - 1);
        out[out_size - 1] = '\0';
    }
}

// 运行时长（上电起算；light sleep 期间 esp_timer 不停）
static void formatInfoUptime(char* out, const size_t out_size) {
    const uint64_t sec = static_cast<uint64_t>(esp_timer_get_time() / 1000000LL);
    const uint64_t h = sec / 3600ULL;
    const uint64_t m = (sec % 3600ULL) / 60ULL;
    const uint64_t s = sec % 60ULL;
    if (h > 0) {
        snprintf(out, out_size, "%lluh %llum %llus", static_cast<unsigned long long>(h),
                 static_cast<unsigned long long>(m), static_cast<unsigned long long>(s));
    } else if (m > 0) {
        snprintf(out, out_size, "%llum %llus", static_cast<unsigned long long>(m),
                 static_cast<unsigned long long>(s));
    } else {
        snprintf(out, out_size, "%llus", static_cast<unsigned long long>(s));
    }
}

// MAC（efuse，不依赖 WiFi 已连接）
static void formatInfoMac(char* out, const size_t out_size) {
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned>((mac >> 0) & 0xFF), static_cast<unsigned>((mac >> 8) & 0xFF),
             static_cast<unsigned>((mac >> 16) & 0xFF), static_cast<unsigned>((mac >> 24) & 0xFF),
             static_cast<unsigned>((mac >> 32) & 0xFF), static_cast<unsigned>((mac >> 40) & 0xFF));
}

static void infoAddLine(InfoSection& sec, const char* label, const char* value) {
    if (sec.line_count >= INFO_MAX_LINES) {
        return;
    }
    sec.lines[sec.line_count++] = {label, value};
}

// 占用越高越告急
static uint16_t memUsedBarColor(const int used_pct) {
    if (used_pct >= 90) {
        return APP_COLOR_ERROR;
    }
    if (used_pct >= 75) {
        return APP_COLOR_WARN;
    }
    return APP_COLOR_OK;
}

// 格式化 used/total（字节 → KB/MB）
static void formatMemPair(char* out, const size_t out_size, const uint32_t used,
                          const uint32_t total) {
    if (total >= 1024u * 1024u) {
        snprintf(out, out_size, "%lu/%lu MB", static_cast<unsigned long>((used + 512) / 1024 / 1024),
                 static_cast<unsigned long>((total + 512) / 1024 / 1024));
    } else {
        snprintf(out, out_size, "%lu/%lu KB", static_cast<unsigned long>((used + 512) / 1024),
                 static_cast<unsigned long>((total + 512) / 1024));
    }
}

// 单段容量短文案（已用 / 剩余）
static void formatSizeShort(char* out, const size_t out_size, const uint32_t bytes) {
    if (bytes >= 1024u * 1024u * 1024u) {
        // TF 大卡：一位小数 GB
        const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        snprintf(out, out_size, "%.1f GB", gb);
    } else if (bytes >= 1024u * 1024u) {
        snprintf(out, out_size, "%lu MB",
                 static_cast<unsigned long>((bytes + 512u * 1024u) / (1024u * 1024u)));
    } else {
        snprintf(out, out_size, "%lu KB", static_cast<unsigned long>((bytes + 512u) / 1024u));
    }
}

// 已用 · 剩余（Storage 页右侧）
static void formatUsedFree(char* out, const size_t out_size, const uint32_t used,
                           const uint32_t free_n) {
    char u[16];
    char f[16];
    formatSizeShort(u, sizeof(u), used);
    formatSizeShort(f, sizeof(f), free_n);
    snprintf(out, out_size, "%s · free %s", u, f);
}

// 单行内存占用：标签 + used/total + 百分比 + 进度条
// total==0 且 shell：空壳进度条（进页先画，避免空白）
static int drawMemBarRow(const int x, const int y, const int w, const char* label,
                         const uint32_t used, const uint32_t total, const bool shell = false) {
    // 先清本行，避免局部刷新残影（不整页闪）
    M5Cardputer.Display.fillRect(x, y, w, INFO_LINE_H + INFO_BAR_H + INFO_BAR_GAP, BLACK);

    const int bar_y = y + INFO_LINE_H;
    if (shell || total == 0) {
        drawInfoLineAt(x, y, label, shell ? "--" : "n/a", INFO_BODY_SIZE);
        M5Cardputer.Display.drawRect(x, bar_y, w, INFO_BAR_H, APP_COLOR_MUTED);
        return bar_y + INFO_BAR_H + INFO_BAR_GAP;
    }

    const int used_pct = static_cast<int>((static_cast<uint64_t>(used) * 100ull) / total);
    const int pct = constrain(used_pct, 0, 100);

    static char pair[28];
    static char right[40];
    formatMemPair(pair, sizeof(pair), used, total);
    snprintf(right, sizeof(right), "%s %d%%", pair, pct);

    drawInfoLineAt(x, y, label, right, INFO_BODY_SIZE);

    M5Cardputer.Display.drawRect(x, bar_y, w, INFO_BAR_H, APP_COLOR_MUTED);
    const int fill_w = (w - 2) * pct / 100;
    if (fill_w > 0) {
        M5Cardputer.Display.fillRect(x + 1, bar_y + 1, fill_w, INFO_BAR_H - 2,
                                     memUsedBarColor(pct));
    }
    return bar_y + INFO_BAR_H + INFO_BAR_GAP;
}

// Storage 行：已用 · 剩余 + 百分比 + 进度条
static int drawStorageBarRow(const int x, const int y, const int w, const char* label,
                             const uint32_t used, const uint32_t free_n, const uint32_t total,
                             const bool shell = false) {
    M5Cardputer.Display.fillRect(x, y, w, INFO_LINE_H + INFO_BAR_H + INFO_BAR_GAP, BLACK);

    const int bar_y = y + INFO_LINE_H;
    if (shell || total == 0) {
        drawInfoLineAt(x, y, label, shell ? "--" : "n/a", INFO_BODY_SIZE);
        M5Cardputer.Display.drawRect(x, bar_y, w, INFO_BAR_H, APP_COLOR_MUTED);
        return bar_y + INFO_BAR_H + INFO_BAR_GAP;
    }

    const int used_pct = static_cast<int>((static_cast<uint64_t>(used) * 100ull) / total);
    const int pct = constrain(used_pct, 0, 100);

    static char pair[36];
    static char right[48];
    formatUsedFree(pair, sizeof(pair), used, free_n);
    snprintf(right, sizeof(right), "%s %d%%", pair, pct);

    drawInfoLineAt(x, y, label, right, INFO_BODY_SIZE);

    M5Cardputer.Display.drawRect(x, bar_y, w, INFO_BAR_H, APP_COLOR_MUTED);
    const int fill_w = (w - 2) * pct / 100;
    if (fill_w > 0) {
        M5Cardputer.Display.fillRect(x + 1, bar_y + 1, fill_w, INFO_BAR_H - 2,
                                     memUsedBarColor(pct));
    }
    return bar_y + INFO_BAR_H + INFO_BAR_GAP;
}

// Memory 空壳：进页立刻画，不等采样
static void drawInfoMemShell(const int x, const int y, const int w) {
    int cy = y;
    cy = drawMemBarRow(x, cy, w, "Heap", 0, 0, true);
    // PSRAM 有无很快，空壳阶段就定行数，避免后面跳动
    if (ESP.getPsramSize() > 0) {
        cy = drawMemBarRow(x, cy, w, "PSRAM", 0, 0, true);
    }
    cy = drawMemBarRow(x, cy, w, "Sketch", 0, 0, true);
    cy = drawMemBarRow(x, cy, w, "LittleFS", 0, 0, true);

    M5Cardputer.Display.fillRect(x, cy, w, INFO_LINE_H * 2, BLACK);
    drawInfoLineAt(x, cy, "Max Alloc", "--", INFO_BODY_SIZE);
    cy += INFO_LINE_H;
    drawInfoLineAt(x, cy, "Min Free", "--", INFO_BODY_SIZE);
}

// 采样 Mem（heap 每拍；lfs 降频）
static void sampleInfoMem(const bool force_slow) {
    const uint32_t heap_total = ESP.getHeapSize();
    const uint32_t heap_free = ESP.getFreeHeap();
    g_mem_heap_total = heap_total;
    g_mem_heap_used = heap_total > heap_free ? heap_total - heap_free : 0;

    g_mem_psram_total = ESP.getPsramSize();
    if (g_mem_psram_total > 0) {
        const uint32_t psram_free = ESP.getFreePsram();
        g_mem_psram_used =
            g_mem_psram_total > psram_free ? g_mem_psram_total - psram_free : 0;
    } else {
        g_mem_psram_used = 0;
    }

    g_mem_sketch = ESP.getSketchSize();
    const uint32_t sketch_free = ESP.getFreeSketchSpace();
    g_mem_sketch_part = g_mem_sketch + sketch_free;

    snprintf(g_mem_maxa, sizeof(g_mem_maxa), "%lu KB",
             static_cast<unsigned long>(ESP.getMaxAllocHeap() / 1024));
    snprintf(g_mem_hmin, sizeof(g_mem_hmin), "%lu KB",
             static_cast<unsigned long>(ESP.getMinFreeHeap() / 1024));

    const uint32_t now = millis();
    if (force_slow || now - g_info_last_slow_ms >= INFO_SLOW_SAMPLE_MS) {
        g_info_last_slow_ms = now;
        // 已挂载则直接读；未挂载再 begin（避免每拍 begin）
        if (LittleFS.begin(false)) {
            g_mem_lfs_total = LittleFS.totalBytes();
            g_mem_lfs_used = LittleFS.usedBytes();
            g_mem_lfs_ok = g_mem_lfs_total > 0;
        } else {
            g_mem_lfs_ok = false;
        }
    }

    g_mem_cache_valid = true;
}

static void drawInfoMemPage(const int x, const int y, const int w, const bool force) {
    if (!g_mem_cache_valid) {
        drawInfoMemShell(x, y, w);
        g_drawn_mem_valid = false;
        return;
    }

    const int row_h = INFO_LINE_H + INFO_BAR_H + INFO_BAR_GAP;
    int cy = y;

    // Heap：数值变了才重画该行
    if (force || !g_drawn_mem_valid || g_drawn_mem_heap_used != g_mem_heap_used ||
        g_drawn_mem_heap_total != g_mem_heap_total) {
        cy = drawMemBarRow(x, cy, w, "Heap", g_mem_heap_used, g_mem_heap_total);
        g_drawn_mem_heap_used = g_mem_heap_used;
        g_drawn_mem_heap_total = g_mem_heap_total;
    } else {
        cy += row_h;
    }

    if (g_mem_psram_total > 0) {
        if (force || !g_drawn_mem_valid || g_drawn_mem_psram_used != g_mem_psram_used ||
            g_drawn_mem_psram_total != g_mem_psram_total) {
            cy = drawMemBarRow(x, cy, w, "PSRAM", g_mem_psram_used, g_mem_psram_total);
            g_drawn_mem_psram_used = g_mem_psram_used;
            g_drawn_mem_psram_total = g_mem_psram_total;
        } else {
            cy += row_h;
        }
    }

    const uint32_t sketch_part = g_mem_sketch_part > 0 ? g_mem_sketch_part : g_mem_sketch;
    if (force || !g_drawn_mem_valid || g_drawn_mem_sketch != g_mem_sketch ||
        g_drawn_mem_sketch_part != sketch_part) {
        cy = drawMemBarRow(x, cy, w, "Sketch", g_mem_sketch, sketch_part);
        g_drawn_mem_sketch = g_mem_sketch;
        g_drawn_mem_sketch_part = sketch_part;
    } else {
        cy += row_h;
    }

    // LittleFS 未采到前保持空壳行，避免整页空白/跳动
    if (force || !g_drawn_mem_valid || g_drawn_mem_lfs_ok != g_mem_lfs_ok ||
        g_drawn_mem_lfs_used != g_mem_lfs_used || g_drawn_mem_lfs_total != g_mem_lfs_total) {
        if (g_mem_lfs_ok) {
            cy = drawMemBarRow(x, cy, w, "LittleFS", g_mem_lfs_used, g_mem_lfs_total);
        } else {
            cy = drawMemBarRow(x, cy, w, "LittleFS", 0, 0, true);
        }
        g_drawn_mem_lfs_ok = g_mem_lfs_ok;
        g_drawn_mem_lfs_used = g_mem_lfs_used;
        g_drawn_mem_lfs_total = g_mem_lfs_total;
    } else {
        cy += row_h;
    }

    if (force || !g_drawn_mem_valid || strcmp(g_drawn_mem_maxa, g_mem_maxa) != 0) {
        M5Cardputer.Display.fillRect(x, cy, w, INFO_LINE_H, BLACK);
        drawInfoLineAt(x, cy, "Max Alloc", g_mem_maxa, INFO_BODY_SIZE);
        strncpy(g_drawn_mem_maxa, g_mem_maxa, sizeof(g_drawn_mem_maxa) - 1);
        g_drawn_mem_maxa[sizeof(g_drawn_mem_maxa) - 1] = '\0';
    }
    cy += INFO_LINE_H;
    if (force || !g_drawn_mem_valid || strcmp(g_drawn_mem_hmin, g_mem_hmin) != 0) {
        M5Cardputer.Display.fillRect(x, cy, w, INFO_LINE_H, BLACK);
        drawInfoLineAt(x, cy, "Min Free", g_mem_hmin, INFO_BODY_SIZE);
        strncpy(g_drawn_mem_hmin, g_mem_hmin, sizeof(g_drawn_mem_hmin) - 1);
        g_drawn_mem_hmin[sizeof(g_drawn_mem_hmin) - 1] = '\0';
    }

    g_drawn_mem_valid = true;
}

// Storage 空壳
static void drawInfoStorageShell(const int x, const int y, const int w) {
    int cy = y;
    cy = drawStorageBarRow(x, cy, w, "Flash", 0, 0, 0, true);
    cy = drawStorageBarRow(x, cy, w, "TF", 0, 0, 0, true);

    // 补充两行说明，避免内容区太空
    M5Cardputer.Display.fillRect(x, cy, w, INFO_LINE_H * 2, BLACK);
    drawInfoLineAt(x, cy, "Flash", "LittleFS", INFO_BODY_SIZE);
    cy += INFO_LINE_H;
    drawInfoLineAt(x, cy, "TF", "optional", INFO_BODY_SIZE);
}

// 采样 Storage（Flash/TF 均降频；无 TF 时本页内不反复挂载）
static void sampleInfoStorage(const bool force_slow) {
    const uint32_t now = millis();
    if (!force_slow && now - g_info_last_slow_ms < INFO_SLOW_SAMPLE_MS && g_sto_cache_valid) {
        return;
    }
    g_info_last_slow_ms = now;

    size_t flash_total = 0;
    size_t flash_used = 0;
    size_t flash_free = 0;
    getFlashDataSpace(&flash_total, &flash_used, &flash_free);
    g_sto_flash_total = static_cast<uint32_t>(flash_total);
    g_sto_flash_used = static_cast<uint32_t>(flash_used);
    g_sto_flash_free = static_cast<uint32_t>(flash_free);
    g_sto_flash_ok = g_sto_flash_total > 0;

    // 进页强制探测；已确认无卡则跳过，避免反复 SD.begin
    if (force_slow || !g_sto_tf_probed || g_sto_tf_ok) {
        size_t tf_total = 0;
        size_t tf_used = 0;
        size_t tf_free = 0;
        getSdDataSpace(&tf_total, &tf_used, &tf_free);
        g_sto_tf_total = static_cast<uint32_t>(tf_total);
        g_sto_tf_used = static_cast<uint32_t>(tf_used);
        g_sto_tf_free = static_cast<uint32_t>(tf_free);
        g_sto_tf_ok = g_sto_tf_total > 0;
        g_sto_tf_probed = true;
    }

    g_sto_cache_valid = true;
}

static void drawInfoStoragePage(const int x, const int y, const int w, const bool force) {
    if (!g_sto_cache_valid) {
        drawInfoStorageShell(x, y, w);
        g_drawn_sto_valid = false;
        return;
    }

    const int row_h = INFO_LINE_H + INFO_BAR_H + INFO_BAR_GAP;
    // 先记下挂载态是否变化（后面会写进 drawn 快照）
    const bool footer_dirty =
        force || !g_drawn_sto_valid || g_drawn_sto_tf_ok != g_sto_tf_ok;
    int cy = y;

    if (force || !g_drawn_sto_valid || g_drawn_sto_flash_ok != g_sto_flash_ok ||
        g_drawn_sto_flash_used != g_sto_flash_used ||
        g_drawn_sto_flash_free != g_sto_flash_free ||
        g_drawn_sto_flash_total != g_sto_flash_total) {
        if (g_sto_flash_ok) {
            cy = drawStorageBarRow(x, cy, w, "Flash", g_sto_flash_used, g_sto_flash_free,
                                   g_sto_flash_total);
        } else {
            cy = drawStorageBarRow(x, cy, w, "Flash", 0, 0, 0, true);
        }
        g_drawn_sto_flash_ok = g_sto_flash_ok;
        g_drawn_sto_flash_used = g_sto_flash_used;
        g_drawn_sto_flash_free = g_sto_flash_free;
        g_drawn_sto_flash_total = g_sto_flash_total;
    } else {
        cy += row_h;
    }

    if (force || !g_drawn_sto_valid || g_drawn_sto_tf_ok != g_sto_tf_ok ||
        g_drawn_sto_tf_used != g_sto_tf_used || g_drawn_sto_tf_free != g_sto_tf_free ||
        g_drawn_sto_tf_total != g_sto_tf_total) {
        if (g_sto_tf_ok) {
            cy = drawStorageBarRow(x, cy, w, "TF", g_sto_tf_used, g_sto_tf_free, g_sto_tf_total);
        } else {
            // 无卡：n/a（非空壳 --）
            cy = drawStorageBarRow(x, cy, w, "TF", 0, 0, 0, false);
        }
        g_drawn_sto_tf_ok = g_sto_tf_ok;
        g_drawn_sto_tf_used = g_sto_tf_used;
        g_drawn_sto_tf_free = g_sto_tf_free;
        g_drawn_sto_tf_total = g_sto_tf_total;
    } else {
        cy += row_h;
    }

    // 说明行：翻页强刷，或 TF 挂载状态变化时重画
    if (footer_dirty) {
        M5Cardputer.Display.fillRect(x, cy, w, INFO_LINE_H * 2, BLACK);
        drawInfoLineAt(x, cy, "Flash", "LittleFS", INFO_BODY_SIZE);
        cy += INFO_LINE_H;
        drawInfoLineAt(x, cy, "TF", g_sto_tf_ok ? "mounted" : "not present", INFO_BODY_SIZE);
    }

    g_drawn_sto_valid = true;
}

// 采样当前文字页到 g_sec_cache（仅本页字段）
static void sampleInfoTextPage(const int page, const bool force_slow) {
    InfoSection& sec = g_sec_cache;
    sec = {};

    switch (static_cast<InfoPage>(page)) {
        case InfoPage::Chip: {
            esp_chip_info_t chipInfo{};
            esp_chip_info(&chipInfo);
            snprintf(g_buf_model, sizeof(g_buf_model), "%s", ESP.getChipModel());
            snprintf(g_buf_rev, sizeof(g_buf_rev), "%d", ESP.getChipRevision());
            snprintf(g_buf_cores, sizeof(g_buf_cores), "%d", chipInfo.cores);
            snprintf(g_buf_freq, sizeof(g_buf_freq), "%d MHz", ESP.getCpuFreqMHz());
            formatInfoFeatures(g_buf_feat, sizeof(g_buf_feat), chipInfo.features);
            infoAddLine(sec, "Model", g_buf_model);
            infoAddLine(sec, "Revision", g_buf_rev);
            infoAddLine(sec, "Cores", g_buf_cores);
            infoAddLine(sec, "Frequency", g_buf_freq);
            infoAddLine(sec, "Features", g_buf_feat);
            break;
        }
        case InfoPage::Fw: {
            snprintf(g_buf_sdk, sizeof(g_buf_sdk), "%s", ESP.getSdkVersion());
            snprintf(g_buf_sketch, sizeof(g_buf_sketch), "%d KB", ESP.getSketchSize() / 1024);
            snprintf(g_buf_free_sk, sizeof(g_buf_free_sk), "%d KB",
                     ESP.getFreeSketchSpace() / 1024);
            snprintf(g_buf_flash, sizeof(g_buf_flash), "%d MB",
                     ESP.getFlashChipSize() / (1024 * 1024));
            snprintf(g_buf_fspd, sizeof(g_buf_fspd), "%d MHz",
                     ESP.getFlashChipSpeed() / 1000000);
            strncpy(g_buf_fmode, infoFlashModeText(), sizeof(g_buf_fmode));
            g_buf_fmode[sizeof(g_buf_fmode) - 1] = '\0';
            infoAddLine(sec, "SDK", g_buf_sdk);
            infoAddLine(sec, "Sketch", g_buf_sketch);
            infoAddLine(sec, "Free Sketch", g_buf_free_sk);
            infoAddLine(sec, "Flash", g_buf_flash);
            infoAddLine(sec, "Flash Speed", g_buf_fspd);
            infoAddLine(sec, "Flash Mode", g_buf_fmode);
            break;
        }
        case InfoPage::Net: {
            formatInfoMac(g_buf_mac, sizeof(g_buf_mac));
            // WiFi 字符串查询可能慢：降频；link 状态每拍可看
            const bool wifi_up = (WiFi.status() == WL_CONNECTED);
            strncpy(g_buf_net, wifi_up ? "up" : "down", sizeof(g_buf_net));
            g_buf_net[sizeof(g_buf_net) - 1] = '\0';

            const uint32_t now = millis();
            const bool slow_due = force_slow || (now - g_info_last_slow_ms >= INFO_SLOW_SAMPLE_MS) ||
                                  (g_buf_ssid[0] == '\0');
            if (slow_due) {
                g_info_last_slow_ms = now;
                if (wifi_up) {
                    strncpy(g_buf_ssid, WiFi.SSID().c_str(), sizeof(g_buf_ssid) - 1);
                    g_buf_ssid[sizeof(g_buf_ssid) - 1] = '\0';
                    strncpy(g_buf_ip, WiFi.localIP().toString().c_str(), sizeof(g_buf_ip) - 1);
                    g_buf_ip[sizeof(g_buf_ip) - 1] = '\0';
                    snprintf(g_buf_rssi, sizeof(g_buf_rssi), "%d dBm", WiFi.RSSI());
                } else {
                    strncpy(g_buf_ssid, "-", sizeof(g_buf_ssid));
                    g_buf_ssid[sizeof(g_buf_ssid) - 1] = '\0';
                    strncpy(g_buf_ip, "-", sizeof(g_buf_ip));
                    g_buf_ip[sizeof(g_buf_ip) - 1] = '\0';
                    strncpy(g_buf_rssi, "-", sizeof(g_buf_rssi));
                    g_buf_rssi[sizeof(g_buf_rssi) - 1] = '\0';
                }
            }
            infoAddLine(sec, "MAC", g_buf_mac);
            infoAddLine(sec, "Link", g_buf_net);
            infoAddLine(sec, "SSID", g_buf_ssid);
            infoAddLine(sec, "IP", g_buf_ip);
            infoAddLine(sec, "RSSI", g_buf_rssi);
            break;
        }
        case InfoPage::Run: {
            formatInfoUptime(g_buf_uptime, sizeof(g_buf_uptime));
            strncpy(g_buf_reset, infoResetReasonText(), sizeof(g_buf_reset));
            g_buf_reset[sizeof(g_buf_reset) - 1] = '\0';
            infoAddLine(sec, "Uptime", g_buf_uptime);
            infoAddLine(sec, "Reset", g_buf_reset);
            break;
        }
        default:
            break;
    }

    g_sec_cache_valid = true;
}

static void drawInfoSectionAt(const InfoSection& sec, const int x, const int y, const int w,
                              const bool force) {
    int cy = y;
    for (int i = 0; i < sec.line_count; i++) {
        const char* val = sec.lines[i].value != nullptr ? sec.lines[i].value : "";
        // 仅当本行文案变化时清行重画
        const bool dirty =
            force || i >= g_drawn_sec_count || g_drawn_sec_page != g_info_page ||
            strcmp(g_drawn_sec_vals[i], val) != 0;
        if (dirty) {
            M5Cardputer.Display.fillRect(x, cy, w, INFO_LINE_H, BLACK);
            drawInfoLineAt(x, cy, sec.lines[i].label, val, INFO_BODY_SIZE);
            strncpy(g_drawn_sec_vals[i], val, sizeof(g_drawn_sec_vals[i]) - 1);
            g_drawn_sec_vals[i][sizeof(g_drawn_sec_vals[i]) - 1] = '\0';
        }
        cy += INFO_LINE_H;
    }
    g_drawn_sec_count = sec.line_count;
    g_drawn_sec_page = g_info_page;
}

static void drawInfoHints() {
    const int hint_y = M5Cardputer.Display.height() - INFO_HINT_H;
    const int screen_w = M5Cardputer.Display.width();
    M5Cardputer.Display.fillRect(APP_CONTENT_X, hint_y, screen_w - APP_CONTENT_X * 2, INFO_HINT_H,
                                 BLACK);

    int cx = APP_CONTENT_X;
    cx += drawTextBadge(cx, hint_y, "[ ]", 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK); // 徽章后恢复 tip 色
    M5Cardputer.Display.setCursor(cx, hint_y);
    M5Cardputer.Display.print("page ");
    cx = M5Cardputer.Display.getCursorX();

    char pager[12];
    snprintf(pager, sizeof(pager), "%d/%d", g_info_page + 1, static_cast<int>(InfoPage::Count));
    M5Cardputer.Display.print(pager);
}

// 采样当前页到缓存（不碰屏幕）
static void sampleInfoCurrentPage(const bool force_slow) {
    if (g_info_page == static_cast<int>(InfoPage::Mem)) {
        sampleInfoMem(force_slow);
    } else if (g_info_page == static_cast<int>(InfoPage::Storage)) {
        sampleInfoStorage(force_slow);
    } else {
        sampleInfoTextPage(g_info_page, force_slow);
    }
}

// 仅画内容区（用缓存；不清整屏）。force：进页/翻页全量；否则只刷变化行
static void drawInfoContent(const bool force) {
    const int x = APP_CONTENT_X;
    const int y = APP_CONTENT_INSET_Y;
    const int w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;

    if (g_info_page == static_cast<int>(InfoPage::Mem)) {
        drawInfoMemPage(x, y, w, force);
    } else if (g_info_page == static_cast<int>(InfoPage::Storage)) {
        drawInfoStoragePage(x, y, w, force);
    } else if (g_sec_cache_valid) {
        drawInfoSectionAt(g_sec_cache, x, y, w, force);
    }

    // 底栏页码只在进页/翻页时重画
    if (force) {
        drawInfoHints();
    }
}

// 翻页/进页时丢掉已绘制快照，避免跨页误跳过
static void invalidateInfoDrawnCache() {
    g_drawn_mem_valid = false;
    g_drawn_sto_valid = false;
    g_drawn_sec_count = 0;
    g_drawn_sec_page = -1;
}

// full：进页/翻页；refresh：定时局部刷新
static void drawInfoApp(const bool full) {
    const char* accent = infoPageAccent(g_info_page);
    const bool page_changed = (g_header_page != g_info_page);
    const bool first_paint = !g_screen_ready;

    if (!g_screen_ready) {
        beginAppScreenAccent("Info ", accent, APP_COLOR_LABEL);
        g_screen_ready = true;
        g_header_page = g_info_page;
    } else if (page_changed) {
        // 只改 header 副标题，不清整屏
        drawAppScreenHeaderAccent("Info ", accent, APP_COLOR_LABEL);
        g_header_page = g_info_page;
        // 翻页时清内容区，避免残留
        const int screen_w = M5Cardputer.Display.width();
        const int screen_h = M5Cardputer.Display.height();
        M5Cardputer.Display.fillRect(0, APP_HEADER_H, screen_w, screen_h - APP_HEADER_H, BLACK);
        invalidateInfoDrawnCache();
    }

    // 进页/翻到 Memory / Storage：先画空壳，再采样填数（避免文件系统查询导致空白）
    if ((first_paint || page_changed) &&
        (g_info_page == static_cast<int>(InfoPage::Mem) ||
         g_info_page == static_cast<int>(InfoPage::Storage))) {
        const int x = APP_CONTENT_X;
        const int y = APP_CONTENT_INSET_Y;
        const int w = M5Cardputer.Display.width() - APP_CONTENT_X * 2;
        if (g_info_page == static_cast<int>(InfoPage::Mem)) {
            drawInfoMemShell(x, y, w);
        } else {
            // 翻到 Storage 重新探测 TF（插拔后可再试）
            g_sto_tf_probed = false;
            g_sto_cache_valid = false;
            drawInfoStorageShell(x, y, w);
        }
        drawInfoHints();
        updateAppHeaderStatus();
        // 空壳已画，后续强制刷数据行时不要再跳过
        invalidateInfoDrawnCache();
    }

    if (full || page_changed) {
        sampleInfoCurrentPage(true);
    }

    drawInfoContent(true);
    updateAppHeaderStatus();
    g_info_last_draw_ms = millis();
}

static bool advanceInfoPage(const int delta) {
    const int count = static_cast<int>(InfoPage::Count);
    if (count <= 1 || delta == 0) {
        return false;
    }
    g_info_page = (g_info_page + delta + count) % count;
    g_sec_cache_valid = false;
    g_mem_cache_valid = false;
    g_sto_cache_valid = false;
    invalidateInfoDrawnCache();
    return true;
}

static int getInfoBracketDelta(const Keyboard_Class::KeysState& status) {
    for (const char c : status.word) {
        if (c == '[') {
            return -1;
        }
        if (c == ']') {
            return 1;
        }
    }
    return 0;
}

void enterInfoApp() {
    g_info_page = static_cast<int>(InfoPage::Mem);
    g_header_page = -1;
    g_screen_ready = false;
    g_sec_cache_valid = false;
    g_mem_cache_valid = false;
    g_sto_cache_valid = false;
    g_sto_tf_probed = false;
    g_info_last_slow_ms = 0;
    invalidateInfoDrawnCache();
    drawInfoApp(true);
}

void updateInfoApp() {
    if (!g_screen_ready) {
        return;
    }
    if (millis() - g_info_last_draw_ms < INFO_REFRESH_MS) {
        return;
    }

    // heap/uptime 等快路径先采样；LittleFS/WiFi 字符串已降频，不拖住本帧
    sampleInfoCurrentPage(false);
    // 定时：只重画数值变化的行，不整页擦
    drawInfoContent(false);
    updateAppHeaderStatus();
    g_info_last_draw_ms = millis();
}

void handleInfoApp(const Keyboard_Class::KeysState& status) {
    const int bracket = getInfoBracketDelta(status);
    if (bracket != 0) {
        if (advanceInfoPage(bracket)) {
            drawInfoApp(true);
        }
    }
}
