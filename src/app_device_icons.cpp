#include "app_device_icons.h"
#include <FS.h>
#include <LittleFS.h>
#include "M5Cardputer.h"
#include "app_colors.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 与 data/icon/device 中文件名一致；较长名称靠前，避免短名误匹配
static const char* const DEVICE_ICON_NAMES[] = {
    "airpurifier",
    "wifispeaker",
    "sensor_ht",
    "bslamp2",
    "juicer",
    "camera",
    "cooker",
    "fryer",
    "lamp2",
    "plug",
    "fan",
    nullptr,
};

static char s_device_icon_path[64];

static constexpr size_t RGB565_MAX_PIXELS =
    static_cast<size_t>(DEVICE_ICON_NATIVE_PX * DEVICE_ICON_NATIVE_PX);

// .rgb565 头部：'R','5','6','5' + uint16 宽 + uint16 高（小端），其后为裸像素
static constexpr size_t RGB565_HEADER_BYTES = 8;
static const uint8_t RGB565_MAGIC[4] = {'R', '5', '6', '5'};

static char asciiLower(const char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// 子串匹配（英文忽略大小写）
static bool strContainsIgnoreCase(const char* haystack, const char* needle) {
    if (haystack == nullptr || needle == nullptr || needle[0] == '\0') {
        return false;
    }
    const size_t nlen = strlen(needle);
    for (const char* p = haystack; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] != '\0' && asciiLower(p[i]) == asciiLower(needle[i])) {
            ++i;
        }
        if (i == nlen) {
            return true;
        }
    }
    return false;
}

const char* const* deviceIconNames() {
    return DEVICE_ICON_NAMES;
}

const char* deviceIconBasenameForModel(const char* model) {
    if (model != nullptr && model[0] != '\0') {
        for (const char* const* name = DEVICE_ICON_NAMES; *name != nullptr; ++name) {
            if (strContainsIgnoreCase(model, *name)) {
                return *name;
            }
        }
        // model 含 Light 但无专用图标时回退 light
        if (strContainsIgnoreCase(model, "light")) {
            return "light";
        }
    }
    return "default";
}

const char* deviceIconPathForModel(const char* model, const bool active) {
    const char* basename = deviceIconBasenameForModel(model);
    if (active) {
        snprintf(s_device_icon_path, sizeof(s_device_icon_path), "%s/%s_active.png",
                 DEVICE_ICON_NATIVE_DIR, basename);
    } else {
        snprintf(s_device_icon_path, sizeof(s_device_icon_path), "%s/%s.png",
                 DEVICE_ICON_NATIVE_DIR, basename);
    }
    return s_device_icon_path;
}

// 列表用小图标：{basename}_25w.png / {basename}_active_25w.png
const char* deviceIconPathForModelList(const char* model, const bool active) {
    const char* basename = deviceIconBasenameForModel(model);
    if (active) {
        snprintf(s_device_icon_path, sizeof(s_device_icon_path), "%s/%s_active_25w.png",
                 DEVICE_ICON_NATIVE_DIR, basename);
    } else {
        snprintf(s_device_icon_path, sizeof(s_device_icon_path), "%s/%s_25w.png",
                 DEVICE_ICON_NATIVE_DIR, basename);
    }
    return s_device_icon_path;
}

int deviceIconDrawPx(const MijiaDevice* /*dev*/) {
    return DEVICE_ICON_NATIVE_PX;
}

bool deviceIconsAvailable() {
    return LittleFS.exists("/icon/device/default.rgb565") ||
           LittleFS.exists("/icon/device/default.png");
}

// 替换扩展名：.png → .rgb565
static bool pathReplaceExt(const char* path, const char* new_ext, char* out, const size_t out_sz) {
    if (path == nullptr || path[0] == '\0' || new_ext == nullptr || out == nullptr || out_sz < 8) {
        return false;
    }
    const char* dot = strrchr(path, '.');
    if (dot != nullptr && strcmp(dot, new_ext) == 0) {
        snprintf(out, out_sz, "%s", path);
        return true;
    }
    if (dot == nullptr) {
        return false;
    }
    const size_t stem_len = static_cast<size_t>(dot - path);
    const size_t ext_len = strlen(new_ext);
    if (stem_len + ext_len + 1 > out_sz) {
        return false;
    }
    memcpy(out, path, stem_len);
    memcpy(out + stem_len, new_ext, ext_len + 1);
    return true;
}

static bool pathToRgb565(const char* path, char* out, const size_t out_sz) {
    const char* dot = strrchr(path != nullptr ? path : "", '.');
    if (dot != nullptr && strcmp(dot, ".rgb565") == 0) {
        snprintf(out, out_sz, "%s", path);
        return true;
    }
    return pathReplaceExt(path, ".rgb565", out, out_sz);
}

// 打开 RGB565 并解析头部宽高；成功时文件游标停在像素数据起点，失败返回 false
static bool openRgb565Image(const char* path, File& f, int& w, int& h) {
    if (path == nullptr || !LittleFS.exists(path)) {
        return false;
    }
    f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }
    uint8_t hdr[RGB565_HEADER_BYTES];
    if (f.read(hdr, sizeof(hdr)) != sizeof(hdr) || memcmp(hdr, RGB565_MAGIC, 4) != 0) {
        f.close();
        return false;
    }
    w = static_cast<int>(hdr[4]) | (static_cast<int>(hdr[5]) << 8);
    h = static_cast<int>(hdr[6]) | (static_cast<int>(hdr[7]) << 8);
    if (w <= 0 || h <= 0 || w > DEVICE_ICON_NATIVE_PX || h > DEVICE_ICON_NATIVE_PX) {
        f.close();
        return false;
    }
    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (pixels > RGB565_MAX_PIXELS || f.size() != RGB565_HEADER_BYTES + pixels * 2u) {
        f.close();
        return false;
    }
    return true;
}

bool loadRgb565File(const char* path, uint16_t* out, const int expect_w, const int expect_h) {
    if (out == nullptr || expect_w <= 0 || expect_h <= 0) {
        return false;
    }
    File f;
    int w = 0;
    int h = 0;
    if (!openRgb565Image(path, f, w, h)) {
        return false;
    }
    if (w != expect_w || h != expect_h) {
        f.close();
        return false;
    }
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2u;
    const size_t n = f.read(reinterpret_cast<uint8_t*>(out), bytes);
    f.close();
    return n == bytes;
}

// 1:1：按行推屏，不占整图 RAM；缩放：临时 malloc 整图
static bool drawRgb565Path(const char* path, const int x, const int y, const float scale) {
    char rgb_path[64];
    if (!pathToRgb565(path, rgb_path, sizeof(rgb_path))) {
        return false;
    }
    File f;
    int w = 0;
    int h = 0;
    if (!openRgb565Image(rgb_path, f, w, h)) {
        return false;
    }

    if (scale > 0.99f && scale < 1.01f) {
        // 行缓冲：最大 70×2 字节，无需常驻 scratch
        uint16_t row[DEVICE_ICON_NATIVE_PX];
        const size_t row_bytes = static_cast<size_t>(w) * 2u;
        for (int row_i = 0; row_i < h; row_i++) {
            if (f.read(reinterpret_cast<uint8_t*>(row), row_bytes) != row_bytes) {
                f.close();
                return false;
            }
            M5Cardputer.Display.pushImage(x, y + row_i, w, 1, row);
        }
        f.close();
        return true;
    }

    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2u;
    auto* px = static_cast<uint16_t*>(malloc(bytes));
    if (px == nullptr) {
        f.close();
        return false;
    }
    const size_t n = f.read(reinterpret_cast<uint8_t*>(px), bytes);
    f.close();
    if (n != bytes) {
        free(px);
        return false;
    }
    M5Cardputer.Display.pushImageRotateZoomWithAA(static_cast<float>(x), static_cast<float>(y), 0.0f,
                                                  0.0f, 0.0f, scale, scale, w, h, px);
    free(px);
    return true;
}

// 优先 bake 的 .rgb565，缺失时回退现场解 PNG
static bool drawDevicePngNativeScaled(const char* path, const int x, const int y,
                                      const float scale) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (drawRgb565Path(path, x, y, scale)) {
        return true;
    }
    if (!LittleFS.exists(path)) {
        return false;
    }
    return M5Cardputer.Display.drawPngFile(LittleFS, path, x, y, 0, 0, 0, 0, scale, scale,
                                           lgfx::v1::datum_t::top_left);
}

bool drawDevicePngNative(const char* path, const int x, const int y) {
    return drawDevicePngNativeScaled(path, x, y, 1.0f);
}

bool drawLittleFsPng(const char* path, const int x, const int y, const float scale) {
    return drawDevicePngNativeScaled(path, x, y, scale);
}

// 离屏 sprite 铺黑底再解 PNG（PNG 带 alpha），回读得到可直接 pushImage 的像素
bool decodePngToRgb565(const char* path, uint16_t* out, const int w, const int h) {
    if (path == nullptr || out == nullptr || w <= 0 || h <= 0 || !LittleFS.exists(path)) {
        return false;
    }
    M5Canvas spr(&M5Cardputer.Display);
    spr.setColorDepth(16);
    if (!spr.createSprite(w, h)) {
        return false;
    }
    spr.fillSprite(BLACK);
    const bool ok = spr.drawPngFile(LittleFS, path, 0, 0);
    if (ok) {
        spr.readRect(0, 0, w, h, out);
    }
    spr.deleteSprite();
    return ok;
}

bool drawAppLogo60(const int x, const int y, const float scale) {
    if (drawRgb565Path(APP_LOGO_60_PATH, x, y, scale)) {
        return true;
    }
    if (!LittleFS.exists(APP_LOGO_60_PATH)) {
        return false;
    }
    return M5Cardputer.Display.drawPngFile(LittleFS, APP_LOGO_60_PATH, x, y, 0, 0, 0, 0, scale,
                                           scale, lgfx::v1::datum_t::top_left);
}

static bool drawDevicePngPath(const char* path, const int x, const int y, const float scale) {
    return drawDevicePngNativeScaled(path, x, y, scale);
}

// 从 PNG 头读宽高
static bool readPngIhdrSize(File& f, int& w, int& h) {
    uint8_t hdr[24];
    if (f.read(hdr, sizeof(hdr)) != sizeof(hdr)) {
        return false;
    }
    static const uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(hdr, kSig, 8) != 0) {
        return false;
    }
    w = (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19];
    h = (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23];
    return w > 0 && h > 0 && w <= DEVICE_ICON_NATIVE_PX && h <= DEVICE_ICON_NATIVE_PX &&
           static_cast<size_t>(w * h) <= RGB565_MAX_PIXELS;
}

// M5GFX 现场解码 → readRect → .rgb565（与屏上观感一致）；缓冲仅 bake 时临时分配
bool bakePngToRgb565File(const char* png_path) {
    if (png_path == nullptr || !LittleFS.exists(png_path)) {
        return false;
    }
    File in = LittleFS.open(png_path, "r");
    if (!in) {
        return false;
    }
    int w = 0;
    int h = 0;
    if (!readPngIhdrSize(in, w, h)) {
        in.close();
        return false;
    }
    in.close();

    char out_path[64];
    if (!pathReplaceExt(png_path, ".rgb565", out_path, sizeof(out_path))) {
        return false;
    }

    const size_t bytes = static_cast<size_t>(w * h * 2);
    auto* px = static_cast<uint16_t*>(malloc(bytes));
    if (px == nullptr) {
        return false;
    }

    // 先清最大烘焙区：只清 w×h 时，前面更大的图标会残留在周围
    M5Cardputer.Display.fillRect(0, 0, DEVICE_ICON_NATIVE_PX, DEVICE_ICON_NATIVE_PX, BLACK);
    if (!M5Cardputer.Display.drawPngFile(LittleFS, png_path, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f,
                                        lgfx::v1::datum_t::top_left)) {
        free(px);
        return false;
    }
    M5Cardputer.Display.readRect(0, 0, w, h, px);

    File out = LittleFS.open(out_path, "w");
    if (!out) {
        free(px);
        return false;
    }
    const uint8_t hdr[RGB565_HEADER_BYTES] = {
        RGB565_MAGIC[0],
        RGB565_MAGIC[1],
        RGB565_MAGIC[2],
        RGB565_MAGIC[3],
        static_cast<uint8_t>(w & 0xFF),
        static_cast<uint8_t>((w >> 8) & 0xFF),
        static_cast<uint8_t>(h & 0xFF),
        static_cast<uint8_t>((h >> 8) & 0xFF),
    };
    const size_t hn = out.write(hdr, sizeof(hdr));
    const size_t n = out.write(reinterpret_cast<const uint8_t*>(px), bytes);
    out.close();
    free(px);
    return hn == sizeof(hdr) && n == bytes;
}

static int bakePngFilesInDir(const char* dir) {
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
        return 0;
    }
    int ok = 0;
    File f = root.openNextFile();
    while (f) {
        const bool is_dir = f.isDirectory();
        const String name = f.name();
        f.close();
        char path[128];
        if (name.startsWith("/")) {
            snprintf(path, sizeof(path), "%s", name.c_str());
        } else {
            snprintf(path, sizeof(path), "%s/%s", dir, name.c_str());
        }
        if (is_dir) {
            // 递归处理 /icon 下的所有子目录，新增目录无需再维护白名单
            ok += bakePngFilesInDir(path);
        } else {
            const char* dot = strrchr(path, '.');
            if (dot != nullptr && strcmp(dot, ".png") == 0) {
                if (bakePngToRgb565File(path)) {
                    ok++;
                }
            }
        }
        f = root.openNextFile();
    }
    root.close();
    return ok;
}

int bakeAllPngIconsToRgb565() {
    // 整屏清黑：左上角作解码缓冲，下方提示进度（无 header）
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(4, DEVICE_ICON_NATIVE_PX + 8);
    M5Cardputer.Display.print("Baking...");

    int ok = 0;
    ok += bakePngFilesInDir("/icon");
    if (LittleFS.exists("/logo_60.png") && bakePngToRgb565File("/logo_60.png")) {
        ok++;
    }
    if (LittleFS.exists("/logo_50.png") && bakePngToRgb565File("/logo_50.png")) {
        ok++;
    }
    // 收尾整屏清掉图标残留，结果提示由 Config 处理器绘制
    M5Cardputer.Display.fillScreen(BLACK);
    return ok;
}

bool drawDeviceIconDefault(const int x, const int y, const bool active) {
    return drawDeviceIconForScaled(nullptr, x, y, active, 1.0f);
}

bool drawDeviceIconFor(const MijiaDevice* dev, const int x, const int y, const bool active) {
    return drawDeviceIconForScaled(dev, x, y, active, 1.0f);
}

bool drawDeviceIconForScaled(const MijiaDevice* dev, const int x, const int y, const bool active,
                             const float scale) {
    const char* model = dev != nullptr ? dev->model : nullptr;
    const char* path = deviceIconPathForModel(model, active);
    if (drawDevicePngPath(path, x, y, scale)) {
        return true;
    }
    // active 图缺失时回退普通图
    if (active) {
        path = deviceIconPathForModel(model, false);
        if (drawDevicePngPath(path, x, y, scale)) {
            return true;
        }
    }
    const char* default_path = deviceIconPathForModel(nullptr, active);
    if (drawDevicePngPath(default_path, x, y, scale)) {
        return true;
    }
    if (active) {
        return drawDevicePngPath(deviceIconPathForModel(nullptr, false), x, y, scale);
    }
    return false;
}

bool drawDeviceIconForList(const MijiaDevice* dev, const int x, const int y, const bool active,
                           const float scale) {
    const char* model = dev != nullptr ? dev->model : nullptr;
    const char* path = deviceIconPathForModelList(model, active);
    if (drawDevicePngPath(path, x, y, scale)) {
        return true;
    }
    // active 图缺失时回退普通图
    if (active) {
        path = deviceIconPathForModelList(model, false);
        if (drawDevicePngPath(path, x, y, scale)) {
            return true;
        }
    }
    const char* default_path = deviceIconPathForModelList(nullptr, active);
    if (drawDevicePngPath(default_path, x, y, scale)) {
        return true;
    }
    if (active) {
        return drawDevicePngPath(deviceIconPathForModelList(nullptr, false), x, y, scale);
    }
    return false;
}
