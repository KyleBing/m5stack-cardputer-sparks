#include "app_screenshot.h"
#include "app_common.h"
#include "app_web.h"
#include "M5Cardputer.h"
#include <lgfx/utility/lgfx_miniz.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>

// Cardputer microSD SPI（与 app_mic 相同）
static constexpr int SHOT_SD_SCK = 40;
static constexpr int SHOT_SD_MISO = 39;
static constexpr int SHOT_SD_MOSI = 14;
static constexpr int SHOT_SD_CS = 12;

static constexpr const char* SHOT_LAST = "/shot/.last";
static constexpr const char* SHOT_BOOT_PENDING = "/shot/.boot_pending";
// PNG（tdefl / RLE）通常数 KB～十几 KB；留足写盘余量即可
static constexpr size_t SHOT_MIN_FREE_BOOT = 48 * 1024;
static constexpr size_t SHOT_MIN_FREE_SAVE = 48 * 1024;
static constexpr size_t SHOT_MIN_FREE_SD = 64 * 1024;
static constexpr size_t SHOT_IDAT_BUF = 2048;

static bool g_shot_sd_ready = false;

// 流式 PNG 写出（逐行读屏 + tdefl，避免整帧进 RAM / 先 BMP 再转）
struct ShotPngSink {
    File* f = nullptr;
    bool ok = true;
};

static bool shotPngWrite(ShotPngSink* sink, const void* data, const size_t len) {
    if (sink == nullptr || !sink->ok || sink->f == nullptr) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (sink->f->write(static_cast<const uint8_t*>(data), len) != len) {
        sink->ok = false;
        return false;
    }
    return true;
}

static bool shotPngWriteU32Be(ShotPngSink* sink, const uint32_t v) {
    const uint8_t b[4] = {
        static_cast<uint8_t>((v >> 24) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>(v & 0xFF),
    };
    return shotPngWrite(sink, b, sizeof(b));
}

static bool shotPngWriteChunk(ShotPngSink* sink, const char type[4], const void* data,
                              const size_t len) {
    if (!shotPngWriteU32Be(sink, static_cast<uint32_t>(len))) {
        return false;
    }
    if (!shotPngWrite(sink, type, 4)) {
        return false;
    }
    if (len > 0 && data != nullptr) {
        if (!shotPngWrite(sink, data, len)) {
            return false;
        }
    }
    lgfx_mz_uint32 crc = static_cast<lgfx_mz_uint32>(
        lgfx_mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const lgfx_mz_uint8*>(type), 4));
    if (len > 0 && data != nullptr) {
        crc = static_cast<lgfx_mz_uint32>(
            lgfx_mz_crc32(crc, static_cast<const lgfx_mz_uint8*>(data), len));
    }
    return shotPngWriteU32Be(sink, static_cast<uint32_t>(crc));
}

struct ShotDeflateCtx {
    ShotPngSink* sink = nullptr;
    uint8_t buf[SHOT_IDAT_BUF] = {};
    size_t len = 0;
    bool ok = true;
};

static bool shotFlushIdat(ShotDeflateCtx* ctx) {
    if (ctx == nullptr || !ctx->ok || ctx->sink == nullptr) {
        return false;
    }
    if (ctx->len == 0) {
        return true;
    }
    const bool ok = shotPngWriteChunk(ctx->sink, "IDAT", ctx->buf, ctx->len);
    ctx->len = 0;
    ctx->ok = ok;
    return ok;
}

static lgfx_mz_bool shotDeflatePutter(const void* pBuf, const int len, void* pUser) {
    auto* ctx = static_cast<ShotDeflateCtx*>(pUser);
    if (ctx == nullptr || !ctx->ok || ctx->sink == nullptr) {
        return MZ_FALSE;
    }
    if (pBuf == nullptr || len <= 0) {
        return MZ_TRUE;
    }
    const auto* p = static_cast<const uint8_t*>(pBuf);
    size_t remaining = static_cast<size_t>(len);
    while (remaining > 0) {
        const size_t space = sizeof(ctx->buf) - ctx->len;
        if (space == 0) {
            if (!shotFlushIdat(ctx)) {
                return MZ_FALSE;
            }
            continue;
        }
        const size_t n = remaining < space ? remaining : space;
        memcpy(ctx->buf + ctx->len, p, n);
        ctx->len += n;
        p += n;
        remaining -= n;
    }
    return MZ_TRUE;
}

// zlib Adler-32
static uint32_t shotAdler32(uint32_t adler, const uint8_t* data, size_t len) {
    uint32_t s1 = adler & 0xffffu;
    uint32_t s2 = (adler >> 16) & 0xffffu;
    while (len > 0) {
        size_t n = len > 5550u ? 5550u : len;
        len -= n;
        while (n--) {
            s1 += *data++;
            s2 += s1;
        }
        s1 %= 65521u;
        s2 %= 65521u;
    }
    return (s2 << 16) | s1;
}

static bool shotPngWriteHeader(ShotPngSink* sink, const int w, const int h) {
    static const uint8_t kPngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (!shotPngWrite(sink, kPngSig, sizeof(kPngSig))) {
        return false;
    }
    uint8_t ihdr[13] = {};
    ihdr[0] = static_cast<uint8_t>((w >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8_t>((w >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8_t>((w >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8_t>(w & 0xFF);
    ihdr[4] = static_cast<uint8_t>((h >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8_t>((h >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8_t>((h >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8_t>(h & 0xFF);
    ihdr[8] = 8; // bit depth
    ihdr[9] = 2; // RGB
    return shotPngWriteChunk(sink, "IHDR", ihdr, sizeof(ihdr));
}

// —— 低内存 RLE zlib（fixed Huffman）：不分配 tdefl（~160KB），UI 截图仍远小于 BMP ——
struct ShotRleZlib {
    ShotPngSink* sink = nullptr;
    uint8_t idat[SHOT_IDAT_BUF] = {};
    size_t idat_len = 0;
    uint32_t bitbuf = 0;
    int bitcount = 0;
    uint32_t adler = 1;
    bool ok = true;
};

static bool shotRleFlushIdat(ShotRleZlib* z) {
    if (z == nullptr || !z->ok || z->sink == nullptr) {
        return false;
    }
    if (z->idat_len == 0) {
        return true;
    }
    const bool ok = shotPngWriteChunk(z->sink, "IDAT", z->idat, z->idat_len);
    z->idat_len = 0;
    z->ok = ok;
    return ok;
}

static bool shotRleEmitByte(ShotRleZlib* z, const uint8_t b) {
    if (z->idat_len >= sizeof(z->idat)) {
        if (!shotRleFlushIdat(z)) {
            return false;
        }
    }
    z->idat[z->idat_len++] = b;
    return true;
}

// Deflate 比特流：LSB first；Huffman 码需先按位反转再送入
static uint32_t shotBitRev(uint32_t code, int nbits) {
    uint32_t r = 0;
    for (int i = 0; i < nbits; i++) {
        r = (r << 1) | (code & 1u);
        code >>= 1;
    }
    return r;
}

static bool shotRlePutBits(ShotRleZlib* z, uint32_t bits, int nbits) {
    z->bitbuf |= (bits << z->bitcount);
    z->bitcount += nbits;
    while (z->bitcount >= 8) {
        if (!shotRleEmitByte(z, static_cast<uint8_t>(z->bitbuf & 0xFFu))) {
            return false;
        }
        z->bitbuf >>= 8;
        z->bitcount -= 8;
    }
    return z->ok;
}

static bool shotRlePutHuff(ShotRleZlib* z, uint32_t code, int nbits) {
    return shotRlePutBits(z, shotBitRev(code, nbits), nbits);
}

// Fixed Huffman：literal/length 符号（RFC 1951）
static bool shotRlePutLit(ShotRleZlib* z, const int sym) {
    if (sym < 0 || sym > 287) {
        return false;
    }
    if (sym <= 143) {
        return shotRlePutHuff(z, static_cast<uint32_t>(0x30 + sym), 8);
    }
    if (sym <= 255) {
        return shotRlePutHuff(z, static_cast<uint32_t>(0x190 + (sym - 144)), 9);
    }
    if (sym <= 279) {
        return shotRlePutHuff(z, static_cast<uint32_t>(sym - 256), 7);
    }
    return shotRlePutHuff(z, static_cast<uint32_t>(0xC0 + (sym - 280)), 8);
}

// length 3..258 → length code + extra bits
static bool shotRlePutLength(ShotRleZlib* z, int len) {
    static const uint16_t kBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11, 13, 15, 17, 19, 23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t kExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    if (len < 3) {
        len = 3;
    }
    if (len > 258) {
        len = 258;
    }
    int code = 28;
    for (int i = 0; i < 28; i++) {
        if (len < kBase[i + 1]) {
            code = i;
            break;
        }
    }
    if (!shotRlePutLit(z, 257 + code)) {
        return false;
    }
    if (kExtra[code] > 0) {
        // length/distance 额外比特按 LSB first，不反转
        if (!shotRlePutBits(z, static_cast<uint32_t>(len - kBase[code]), kExtra[code])) {
            return false;
        }
    }
    // distance=1 → dist code 0（fixed Huffman 5 bit）
    return shotRlePutHuff(z, 0, 5);
}

// 把一段字节用「字面量 + dist=1 的 RLE」写入当前 fixed block
static bool shotRleCompressBuf(ShotRleZlib* z, const uint8_t* data, const size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t run = 1;
        while (i + run < len && data[i + run] == data[i] && run < 258u) {
            run++;
        }
        if (!shotRlePutLit(z, data[i])) {
            return false;
        }
        size_t left = run - 1;
        while (left >= 3u) {
            const size_t m = left > 258u ? 258u : left;
            if (!shotRlePutLength(z, static_cast<int>(m))) {
                return false;
            }
            left -= m;
        }
        while (left > 0u) {
            if (!shotRlePutLit(z, data[i])) {
                return false;
            }
            left--;
        }
        i += run;
    }
    return z->ok;
}

// 低内存路径：Up 过滤 + RLE zlib，堆占用约数 KB（勿用未压缩 store，体积≈BMP）
static bool captureDisplayToPngRle(File& f, const int w, const int h, uint8_t* row, char* err,
                                   const size_t err_len) {
    auto& d = M5Cardputer.Display;
    const int bpl = w * 3;
    const size_t row_bytes = static_cast<size_t>(bpl) + 1u;

    uint8_t* prev = static_cast<uint8_t*>(malloc(static_cast<size_t>(bpl)));
    uint8_t* filtered = static_cast<uint8_t*>(malloc(row_bytes));
    if (prev == nullptr || filtered == nullptr) {
        free(prev);
        free(filtered);
        snprintf(err, err_len, "oom rle heap=%u", ESP.getFreeHeap());
        return false;
    }
    memset(prev, 0, static_cast<size_t>(bpl));

    ShotPngSink sink;
    sink.f = &f;
    sink.ok = true;
    bool ok = shotPngWriteHeader(&sink, w, h);

    ShotRleZlib z;
    z.sink = &sink;
    z.ok = true;

    if (ok) {
        // zlib 头 + fixed Huffman block（BFINAL=1，失败会删文件）
        ok = shotRleEmitByte(&z, 0x78) && shotRleEmitByte(&z, 0x01);
        ok = ok && shotRlePutBits(&z, 0x3u, 3); // BFINAL=1, BTYPE=01
    }

    for (int y = 0; ok && y < h; y++) {
        d.readRectRGB(0, y, w, 1, row);
        filtered[0] = 2; // Up
        for (int i = 0; i < bpl; i++) {
            filtered[i + 1] = static_cast<uint8_t>(row[i] - prev[i]);
        }
        memcpy(prev, row, static_cast<size_t>(bpl));
        z.adler = shotAdler32(z.adler, filtered, row_bytes);
        ok = shotRleCompressBuf(&z, filtered, row_bytes);
        if ((y & 7) == 0) {
            yield();
        }
    }

    if (ok) {
        ok = shotRlePutLit(&z, 256); // EOB
    }
    if (ok && z.bitcount > 0) {
        // 字节对齐
        ok = shotRlePutBits(&z, 0, 8 - z.bitcount);
    }
    if (ok) {
        const uint32_t a = z.adler;
        ok = shotRleEmitByte(&z, static_cast<uint8_t>((a >> 24) & 0xFF)) &&
             shotRleEmitByte(&z, static_cast<uint8_t>((a >> 16) & 0xFF)) &&
             shotRleEmitByte(&z, static_cast<uint8_t>((a >> 8) & 0xFF)) &&
             shotRleEmitByte(&z, static_cast<uint8_t>(a & 0xFF));
    }
    if (ok) {
        ok = shotRleFlushIdat(&z);
    }
    if (ok) {
        ok = shotPngWriteChunk(&sink, "IEND", nullptr, 0);
    }

    free(prev);
    free(filtered);
    if (!ok) {
        snprintf(err, err_len, sink.ok ? "png rle fail" : "png write fail");
        return false;
    }
    return true;
}

// tdefl 压缩路径（堆上分配 compressor + IDAT 缓冲；Up 过滤利于 UI）
static bool captureDisplayToPngDeflate(File& f, const int w, const int h, uint8_t* row, char* err,
                                       const size_t err_len) {
    auto& d = M5Cardputer.Display;
    const int bpl = w * 3;

    auto* comp = static_cast<tdefl_compressor*>(malloc(sizeof(tdefl_compressor)));
    auto* dctx = static_cast<ShotDeflateCtx*>(malloc(sizeof(ShotDeflateCtx)));
    uint8_t* prev = static_cast<uint8_t*>(malloc(static_cast<size_t>(bpl)));
    uint8_t* filtered = static_cast<uint8_t*>(malloc(static_cast<size_t>(bpl)));
    if (comp == nullptr || dctx == nullptr || prev == nullptr || filtered == nullptr) {
        free(comp);
        free(dctx);
        free(prev);
        free(filtered);
        snprintf(err, err_len, "oom deflate heap=%u", ESP.getFreeHeap());
        return false;
    }
    memset(prev, 0, static_cast<size_t>(bpl));

    ShotPngSink sink;
    sink.f = &f;
    sink.ok = true;
    bool ok = shotPngWriteHeader(&sink, w, h);

    dctx->sink = &sink;
    dctx->len = 0;
    dctx->ok = true;

    if (ok) {
        // level 6：UI 截图体积更小；仍远快于写 BMP
        static const lgfx_mz_uint kProbes[11] = {0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500};
        const int flags = static_cast<int>(kProbes[2]) | TDEFL_WRITE_ZLIB_HEADER;
        if (tdefl_init(comp, shotDeflatePutter, dctx, flags) != TDEFL_STATUS_OKAY) {
            ok = false;
        }
    }

    if (ok) {
        const uint8_t filter = 2; // Up
        for (int y = 0; y < h; y++) {
            d.readRectRGB(0, y, w, 1, row);
            for (int i = 0; i < bpl; i++) {
                filtered[i] = static_cast<uint8_t>(row[i] - prev[i]);
            }
            memcpy(prev, row, static_cast<size_t>(bpl));
            if (tdefl_compress_buffer(comp, &filter, 1, TDEFL_NO_FLUSH) < 0) {
                ok = false;
                break;
            }
            if (tdefl_compress_buffer(comp, filtered, static_cast<size_t>(bpl), TDEFL_NO_FLUSH) < 0) {
                ok = false;
                break;
            }
            if (!dctx->ok || !sink.ok) {
                ok = false;
                break;
            }
            if ((y & 7) == 0) {
                yield();
            }
        }
    }
    if (ok) {
        if (tdefl_compress_buffer(comp, nullptr, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE) {
            ok = false;
        }
    }
    if (ok) {
        ok = shotFlushIdat(dctx);
    }
    if (ok) {
        ok = shotPngWriteChunk(&sink, "IEND", nullptr, 0);
    }

    free(comp);
    free(dctx);
    free(prev);
    free(filtered);
    if (!ok) {
        snprintf(err, err_len, sink.ok ? "png encode fail" : "png write fail");
        return false;
    }
    return true;
}

// tdefl_compressor 约 160KB；不够则走 RLE
static bool shotLowHeapForDeflate() {
    const size_t need = sizeof(tdefl_compressor) + sizeof(ShotDeflateCtx) + 4096u;
    return ESP.getMaxAllocHeap() < need;
}

static bool captureDisplayToPngFile(File& f, char* err, const size_t err_len) {
    auto& d = M5Cardputer.Display;
    if (!d.isReadable()) {
        snprintf(err, err_len, "panel not readable");
        return false;
    }

    const int w = d.width();
    const int h = d.height();
    if (w <= 0 || h <= 0 || w > 320 || h > 240) {
        snprintf(err, err_len, "bad size %dx%d", w, h);
        return false;
    }

    const int bpl = w * 3;
    uint8_t* row = static_cast<uint8_t*>(malloc(static_cast<size_t>(bpl)));
    if (row == nullptr) {
        snprintf(err, err_len, "oom row heap=%u", ESP.getFreeHeap());
        return false;
    }

    d.waitDMA();

    // 堆够走 tdefl；不够走 RLE（勿再用未压缩 store，体积≈旧 BMP）
    bool ok;
    if (shotLowHeapForDeflate()) {
        Serial.printf("[shot] low heap maxalloc=%u, png rle\n", ESP.getMaxAllocHeap());
        ok = captureDisplayToPngRle(f, w, h, row, err, err_len);
    } else {
        ok = captureDisplayToPngDeflate(f, w, h, row, err, err_len);
    }

    free(row);
    return ok;
}

// slug 仅保留 a-z0-9_
static void sanitizeSlug(const char* in, char* out, const size_t out_len) {
    if (out_len == 0) {
        return;
    }
    size_t j = 0;
    for (size_t i = 0; in != nullptr && in[i] != '\0' && j + 1 < out_len; i++) {
        const char c = in[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (ok) {
            out[j++] = c;
        }
    }
    if (j == 0) {
        strncpy(out, "unknown", out_len);
        out[out_len - 1] = '\0';
        return;
    }
    out[j] = '\0';
}

// 尝试挂载 TF；失败返回 false（可与 mic 共用已挂载的 SD）
static bool shotEnsureSd() {
    if (g_shot_sd_ready && SD.cardType() != CARD_NONE) {
        return true;
    }
    g_shot_sd_ready = false;

    SPI.begin(SHOT_SD_SCK, SHOT_SD_MISO, SHOT_SD_MOSI, SHOT_SD_CS);
    if (!SD.begin(SHOT_SD_CS, SPI, 25000000)) {
        // 可能已被 mic 挂载：再看卡类型
        if (SD.cardType() != CARD_NONE) {
            g_shot_sd_ready = true;
            return true;
        }
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return false;
    }
    g_shot_sd_ready = true;
    return true;
}

bool isScreenshotSdReady() {
    return shotEnsureSd();
}

static bool ensureShotDirOn(fs::FS& fs) {
    if (fs.exists(SHOT_DIR)) {
        return true;
    }
    return fs.mkdir(SHOT_DIR);
}

// 从路径或文件名取出 base 名
static const char* shotBaseName(const char* name) {
    const char* slash = strrchr(name, '/');
    return slash != nullptr ? slash + 1 : name;
}

static bool endsWithIgnoreCase(const char* s, const char* suffix) {
    if (s == nullptr || suffix == nullptr) {
        return false;
    }
    const size_t n = strlen(s);
    const size_t m = strlen(suffix);
    if (n < m) {
        return false;
    }
    for (size_t i = 0; i < m; i++) {
        const char a = s[n - m + i];
        const char b = suffix[i];
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl) {
            return false;
        }
    }
    return true;
}

// 新截图为 .png；仍识别旧版 .bmp 便于列表/清理
static bool isShotFileName(const char* base) {
    if (base == nullptr || strncmp(base, "app_", 4) != 0) {
        return false;
    }
    return endsWithIgnoreCase(base, ".png") || endsWithIgnoreCase(base, ".bmp");
}

static size_t fsFreeBytes(fs::FS& fs, const bool is_sd) {
    if (is_sd) {
        const size_t total = SD.totalBytes();
        const size_t used = SD.usedBytes();
        return total > used ? total - used : 0;
    }
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    return total > used ? total - used : 0;
}

// 扫描同前缀最大序号（app_<slug>_NNN.png / .bmp）
static int findNextShotIndexOn(fs::FS& fs, const char* slug) {
    char prefix[40];
    snprintf(prefix, sizeof(prefix), "app_%s_", slug);
    const size_t prefix_len = strlen(prefix);

    int max_n = 0;
    File dir = fs.open(SHOT_DIR);
    if (!dir || !dir.isDirectory()) {
        return 1;
    }
    File f = dir.openNextFile();
    while (f) {
        const char* base = shotBaseName(f.name());
        if (isShotFileName(base) && strncmp(base, prefix, prefix_len) == 0) {
            int n = 0;
            if (sscanf(base + prefix_len, "%d", &n) == 1 && n > max_n) {
                max_n = n;
            }
        }
        f = dir.openNextFile();
    }
    dir.close();
    return max_n + 1;
}

// 记录最后一张文件名（写在对应 FS）
static void writeLastShotNameOn(fs::FS& fs, const char* filename) {
    File f = fs.open(SHOT_LAST, "w");
    if (!f) {
        return;
    }
    f.print(filename);
    f.close();
}

static bool readLastShotNameOn(fs::FS& fs, char* out, const size_t out_len) {
    if (out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!fs.exists(SHOT_LAST)) {
        return false;
    }
    File f = fs.open(SHOT_LAST, "r");
    if (!f) {
        return false;
    }
    const size_t n = f.readBytes(out, out_len - 1);
    f.close();
    out[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (out[i] == '\r' || out[i] == '\n') {
            out[i] = '\0';
            break;
        }
    }
    return out[0] != '\0' && isShotFileName(out);
}

// 找时间最新的一张（无 .last 时兜底）
static bool findNewestShotNameOn(fs::FS& fs, char* out, const size_t out_len) {
    if (out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!fs.exists(SHOT_DIR)) {
        return false;
    }
    File dir = fs.open(SHOT_DIR);
    if (!dir || !dir.isDirectory()) {
        return false;
    }
    time_t best_t = 0;
    char best[48] = "";
    bool any = false;
    File f = dir.openNextFile();
    while (f) {
        const char* base = shotBaseName(f.name());
        if (isShotFileName(base)) {
            const time_t t = f.getLastWrite();
            if (!any || t >= best_t) {
                best_t = t;
                strncpy(best, base, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                any = true;
            }
        }
        f = dir.openNextFile();
    }
    dir.close();
    if (!any) {
        return false;
    }
    strncpy(out, best, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

static bool deleteLastOn(fs::FS& fs, const bool is_sd) {
    char name[48];
    if (!readLastShotNameOn(fs, name, sizeof(name))) {
        if (!findNewestShotNameOn(fs, name, sizeof(name))) {
            return false;
        }
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SHOT_DIR, name);
    if (!fs.exists(path)) {
        if (!findNewestShotNameOn(fs, name, sizeof(name))) {
            fs.remove(SHOT_LAST);
            return false;
        }
        snprintf(path, sizeof(path), "%s/%s", SHOT_DIR, name);
    }
    const bool ok = fs.remove(path);
    fs.remove(SHOT_LAST);
    if (ok) {
        Serial.printf("[shot] deleted last %s on %s free=%u\n", name, is_sd ? "TF" : "Flash",
                      static_cast<unsigned>(fsFreeBytes(fs, is_sd)));
    }
    return ok;
}

bool deleteLastScreenshot() {
    if (!LittleFS.begin(false)) {
        return false;
    }
    return deleteLastOn(LittleFS, false);
}

static int countOn(fs::FS& fs) {
    if (!fs.exists(SHOT_DIR)) {
        return 0;
    }
    File dir = fs.open(SHOT_DIR);
    if (!dir || !dir.isDirectory()) {
        return 0;
    }
    int n = 0;
    File f = dir.openNextFile();
    while (f) {
        if (isShotFileName(shotBaseName(f.name()))) {
            n++;
        }
        f = dir.openNextFile();
    }
    dir.close();
    return n;
}

static size_t usedBytesOn(fs::FS& fs) {
    if (!fs.exists(SHOT_DIR)) {
        return 0;
    }
    File dir = fs.open(SHOT_DIR);
    if (!dir || !dir.isDirectory()) {
        return 0;
    }
    size_t sum = 0;
    File f = dir.openNextFile();
    while (f) {
        if (isShotFileName(shotBaseName(f.name()))) {
            sum += static_cast<size_t>(f.size());
        }
        f = dir.openNextFile();
    }
    dir.close();
    return sum;
}

static int clearAllOn(fs::FS& fs) {
    if (!fs.exists(SHOT_DIR)) {
        return 0;
    }
    int n = 0;
    for (;;) {
        File dir = fs.open(SHOT_DIR);
        if (!dir || !dir.isDirectory()) {
            break;
        }
        char to_del[48] = "";
        File f = dir.openNextFile();
        while (f) {
            const char* base = shotBaseName(f.name());
            if (isShotFileName(base)) {
                strncpy(to_del, base, sizeof(to_del) - 1);
                to_del[sizeof(to_del) - 1] = '\0';
                f.close(); // 打开着 remove 会失败
                break;
            }
            f = dir.openNextFile();
        }
        dir.close();
        if (to_del[0] == '\0') {
            break;
        }
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", SHOT_DIR, to_del);
        if (fs.remove(path)) {
            n++;
            Serial.printf("[shot] cleared %s\n", to_del);
        } else {
            Serial.printf("[shot] clear remove fail %s\n", path);
            break;
        }
    }
    fs.remove(SHOT_LAST);
    return n;
}

static void enumOn(fs::FS& fs, const char* storage, ShotEnumCallback cb, void* user) {
    if (cb == nullptr || !fs.exists(SHOT_DIR)) {
        return;
    }
    File dir = fs.open(SHOT_DIR);
    if (!dir || !dir.isDirectory()) {
        return;
    }
    File f = dir.openNextFile();
    while (f) {
        const char* base = shotBaseName(f.name());
        if (isShotFileName(base)) {
            cb(storage, base, static_cast<size_t>(f.size()), user);
        }
        f = dir.openNextFile();
    }
    dir.close();
}

void recoverScreenshotsOnBoot() {
    if (!LittleFS.begin(false)) {
        return;
    }

    // 上次 setup 没跑完（崩溃/看门狗）→ 删最后一张 Flash 截图
    if (LittleFS.exists(SHOT_BOOT_PENDING)) {
        Serial.println("[shot] boot pending: remove last screenshot");
        deleteLastScreenshot();
        LittleFS.remove(SHOT_BOOT_PENDING);
    }

    // 确保 /shot 可写。勿在开机调用 LittleFS.usedBytes()：图标一多全盘扫描可达数秒。
    // 空间回收改到真正截图保存时（saveToFs 已有删旧逻辑）。
    if (!ensureShotDirOn(LittleFS)) {
        for (int i = 0; i < 8 && !ensureShotDirOn(LittleFS); i++) {
            if (!deleteLastScreenshot()) {
                break;
            }
        }
    }

    // 标记启动中；正常结束由 markScreenshotBootOk 清除
    File pend = LittleFS.open(SHOT_BOOT_PENDING, "w");
    if (pend) {
        pend.print("1");
        pend.close();
    }
}

void markScreenshotBootOk() {
    if (!LittleFS.begin(false)) {
        return;
    }
    LittleFS.remove(SHOT_BOOT_PENDING);
}

// 写到指定 FS；is_sd 决定空闲阈值与日志标签
static bool saveToFs(fs::FS& fs, const bool is_sd, const char* app_slug, char* out_name,
                     const size_t out_name_len, char* err, const size_t err_len) {
    if (!ensureShotDirOn(fs)) {
        snprintf(err, err_len, "mkdir /shot fail");
        return false;
    }

    // 空间不足时不删旧图，只报错（由调用方提示）
    const size_t min_free = is_sd ? SHOT_MIN_FREE_SD : SHOT_MIN_FREE_SAVE;
    const size_t free_n = fsFreeBytes(fs, is_sd);
    if (free_n < min_free) {
        snprintf(err, err_len, "%s full free=%u", is_sd ? "TF" : "fs",
                 static_cast<unsigned>(free_n));
        return false;
    }

    char slug[24];
    sanitizeSlug(app_slug, slug, sizeof(slug));

    const int idx = findNextShotIndexOn(fs, slug);
    if (idx > 999) {
        snprintf(err, err_len, "too many %s shots", slug);
        return false;
    }

    char filename[48];
    snprintf(filename, sizeof(filename), "app_%s_%03d.png", slug, idx);
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SHOT_DIR, filename);

    File out = fs.open(path, "w");
    if (!out) {
        snprintf(err, err_len, "open fail (%s full?)", is_sd ? "TF" : "fs");
        return false;
    }
    if (!captureDisplayToPngFile(out, err, err_len)) {
        out.close();
        fs.remove(path);
        return false;
    }
    out.flush();
    const size_t sz = out.size();
    out.close();
    // 防止编码“成功”却写出空/残缺文件；亦拦截未压缩量级（约 ≥90KB）
    if (sz < 64) {
        fs.remove(path);
        snprintf(err, err_len, "empty shot %uB", static_cast<unsigned>(sz));
        return false;
    }
    writeLastShotNameOn(fs, filename);

    if (out_name != nullptr && out_name_len > 0) {
        strncpy(out_name, filename, out_name_len);
        out_name[out_name_len - 1] = '\0';
    }
    Serial.printf("[shot] saved %s (%uB) on %s free=%u\n", path, static_cast<unsigned>(sz),
                  is_sd ? "TF" : "Flash", static_cast<unsigned>(fsFreeBytes(fs, is_sd)));
    return true;
}

bool saveScreenshotToFlash(const char* app_slug, char* out_name, const size_t out_name_len, char* err,
                           const size_t err_len) {
    if (err != nullptr && err_len > 0) {
        err[0] = '\0';
    }
    if (out_name != nullptr && out_name_len > 0) {
        out_name[0] = '\0';
    }

    // 有 TF 优先写卡
    if (shotEnsureSd()) {
        if (saveToFs(SD, true, app_slug, out_name, out_name_len, err, err_len)) {
            return true;
        }
        Serial.printf("[shot] TF save fail (%s), fallback Flash\n", err != nullptr ? err : "?");
    }

    if (!LittleFS.begin(false)) {
        snprintf(err, err_len, "fs mount fail");
        return false;
    }
    return saveToFs(LittleFS, false, app_slug, out_name, out_name_len, err, err_len);
}

// 截图成功后反色闪一下，不重绘界面
static void flashScreenshotFeedback() {
    auto& d = M5Cardputer.Display;
    const bool inv = d.getInvert();
    d.invertDisplay(!inv);
    delay(50);
    d.invertDisplay(inv);
}

// 空间不足等失败：底栏黄条提示（约 1s；下层可能被盖住，交互后界面会重绘）
static void showScreenshotFailTip(const char* msg) {
    auto& d = M5Cardputer.Display;
    const int screen_w = d.width();
    const int screen_h = d.height();
    const int box_h = 14;
    const int box_y = screen_h - box_h - 1;
    d.fillRect(0, box_y, screen_w, box_h, APP_COLOR_WARN);
    d.setTextSize(1);
    d.setTextColor(BLACK, APP_COLOR_WARN);
    d.setCursor(4, box_y + 3);
    d.print(msg != nullptr ? msg : "shot fail");
    delay(1100);
}

bool tryHandleScreenshotHotkey() {
    if (!M5Cardputer.Keyboard.isPressed()) {
        return false;
    }
    const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    if (!status.fn) {
        return false;
    }
    bool hit = false;
    for (const char c : status.word) {
        if (c == 's' || c == 'S') {
            hit = true;
            break;
        }
    }
    if (!hit) {
        return false;
    }

    const char* slug = getCurrentAppShotSlug();

    char name[48];
    char err[96];
    if (saveScreenshotToFlash(slug, name, sizeof(name), err, sizeof(err))) {
        // 先闪屏再响，避免音效期间用户误以为还没拍完
        flashScreenshotFeedback();
        if (isScreenshotSoundEnabled()) {
            playUiTone(1200.0f, 40);
        }
        Serial.printf("[shot] Fn+s ok %s\n", name);
    } else {
        if (isScreenshotSoundEnabled()) {
            playUiTone(300.0f, 80);
        }
        Serial.printf("[shot] Fn+s fail: %s\n", err);
        // 空间不足：明确提示，不删旧截图
        if (strstr(err, "full") != nullptr) {
            showScreenshotFailTip("no space for shot");
        } else {
            showScreenshotFailTip("shot failed");
        }
    }
    return true;
}

int countTfScreenshots() {
    return shotEnsureSd() ? countOn(SD) : 0;
}

int countFlashScreenshots() {
    return LittleFS.begin(false) ? countOn(LittleFS) : 0;
}

int countScreenshots() {
    return countTfScreenshots() + countFlashScreenshots();
}

size_t screenshotsUsedBytesTf() {
    return shotEnsureSd() ? usedBytesOn(SD) : 0;
}

size_t screenshotsUsedBytesFlash() {
    return LittleFS.begin(false) ? usedBytesOn(LittleFS) : 0;
}

size_t screenshotsUsedBytes() {
    return screenshotsUsedBytesTf() + screenshotsUsedBytesFlash();
}

void getFlashDataSpace(size_t* total, size_t* used, size_t* free_bytes) {
    if (total != nullptr) {
        *total = 0;
    }
    if (used != nullptr) {
        *used = 0;
    }
    if (free_bytes != nullptr) {
        *free_bytes = 0;
    }
    if (!LittleFS.begin(false)) {
        return;
    }
    const size_t t = LittleFS.totalBytes();
    const size_t u = LittleFS.usedBytes();
    const size_t free_n = t > u ? t - u : 0;
    if (total != nullptr) {
        *total = t;
    }
    if (used != nullptr) {
        *used = u;
    }
    if (free_bytes != nullptr) {
        *free_bytes = free_n;
    }
}

void getSdDataSpace(size_t* total, size_t* used, size_t* free_bytes) {
    if (total != nullptr) {
        *total = 0;
    }
    if (used != nullptr) {
        *used = 0;
    }
    if (free_bytes != nullptr) {
        *free_bytes = 0;
    }
    if (!shotEnsureSd()) {
        return;
    }
    const size_t t = SD.totalBytes();
    const size_t u = SD.usedBytes();
    const size_t free_n = t > u ? t - u : 0;
    if (total != nullptr) {
        *total = t;
    }
    if (used != nullptr) {
        *used = u;
    }
    if (free_bytes != nullptr) {
        *free_bytes = free_n;
    }
}

void enumTfScreenshots(ShotEnumCallback cb, void* user) {
    if (cb == nullptr || !shotEnsureSd()) {
        return;
    }
    enumOn(SD, "TF", cb, user);
}

void enumFlashScreenshots(ShotEnumCallback cb, void* user) {
    if (cb == nullptr || !LittleFS.begin(false)) {
        return;
    }
    enumOn(LittleFS, "Flash", cb, user);
}

void enumScreenshots(ShotEnumCallback cb, void* user) {
    enumTfScreenshots(cb, user);
    enumFlashScreenshots(cb, user);
}

bool openScreenshotFile(const String& uri, File& out) {
    if (!isSafeShotPath(uri)) {
        return false;
    }
    if (shotEnsureSd()) {
        out = SD.open(uri, "r");
        if (out) {
            return true;
        }
    }
    if (LittleFS.begin(false)) {
        out = LittleFS.open(uri, "r");
        if (out) {
            return true;
        }
    }
    return false;
}

int clearTfScreenshots() {
    if (!shotEnsureSd()) {
        return 0;
    }
    return clearAllOn(SD);
}

int clearFlashScreenshots() {
    if (!LittleFS.begin(false)) {
        return 0;
    }
    const int n = clearAllOn(LittleFS);
    LittleFS.remove(SHOT_BOOT_PENDING);
    return n;
}

int clearAllScreenshots() {
    return clearTfScreenshots() + clearFlashScreenshots();
}

// 删除单张：storage 为 "TF" / "Flash"；name 为 basename
bool deleteScreenshotFile(const char* storage, const char* basename) {
    if (storage == nullptr || !isShotFileName(basename)) {
        return false;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SHOT_DIR, basename);
    // 路径安全：与 isSafeShotPath 同规则
    String uri = String(path);
    if (!isSafeShotPath(uri)) {
        return false;
    }

    fs::FS* fs = nullptr;
    bool is_sd = false;
    if (strcmp(storage, "TF") == 0) {
        if (!shotEnsureSd()) {
            return false;
        }
        fs = &SD;
        is_sd = true;
    } else if (strcmp(storage, "Flash") == 0) {
        if (!LittleFS.begin(false)) {
            return false;
        }
        fs = &LittleFS;
    } else {
        return false;
    }

    if (!fs->exists(path)) {
        return false;
    }
    if (!fs->remove(path)) {
        return false;
    }

    // 若删的是 .last 记录的那张，清掉指针
    char last[48];
    if (readLastShotNameOn(*fs, last, sizeof(last)) && strcmp(last, basename) == 0) {
        fs->remove(SHOT_LAST);
    }
    Serial.printf("[shot] deleted %s on %s free=%u\n", basename, is_sd ? "TF" : "Flash",
                  static_cast<unsigned>(fsFreeBytes(*fs, is_sd)));
    return true;
}

bool isSafeShotPath(const String& uri) {
    // 允许 /shot/app_xxx_001.png（及旧版 .bmp）
    if (!uri.startsWith("/shot/app_")) {
        return false;
    }
    if (!uri.endsWith(".png") && !uri.endsWith(".bmp")) {
        return false;
    }
    if (uri.indexOf("..") >= 0 || uri.length() > 64) {
        return false;
    }
    for (size_t i = 0; i < uri.length(); i++) {
        const char c = uri[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.' || c == '/';
        if (!ok) {
            return false;
        }
    }
    return true;
}
