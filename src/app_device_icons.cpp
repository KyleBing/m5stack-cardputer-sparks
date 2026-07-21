#include "app_device_icons.h"
#include <FS.h>
#include <LittleFS.h>
#include "M5Cardputer.h"
#include <cmath>
#include <cstdio>
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

// 单次绘制/烘焙暂存：最大原生 70x70 RGB565
static uint16_t s_rgb565_scratch[DEVICE_ICON_NATIVE_PX * DEVICE_ICON_NATIVE_PX];
static constexpr size_t RGB565_SCRATCH_BYTES =
    sizeof(s_rgb565_scratch);
static constexpr size_t RGB565_SCRATCH_PIXELS =
    sizeof(s_rgb565_scratch) / sizeof(s_rgb565_scratch[0]);

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

// 读正方形 RGB565，返回边长；失败 0
static int loadRgb565Square(const char* path) {
    if (path == nullptr || !LittleFS.exists(path)) {
        return 0;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return 0;
    }
    const size_t sz = f.size();
    if (sz == 0 || sz % 2 != 0 || sz > RGB565_SCRATCH_BYTES) {
        f.close();
        return 0;
    }
    const int pixels = static_cast<int>(sz / 2);
    if (pixels > static_cast<int>(RGB565_SCRATCH_PIXELS)) {
        f.close();
        return 0;
    }
    const int side = static_cast<int>(lround(sqrt(static_cast<double>(pixels))));
    if (side <= 0 || side * side != pixels) {
        f.close();
        return 0;
    }
    const size_t n = f.read(reinterpret_cast<uint8_t*>(s_rgb565_scratch), sz);
    f.close();
    return n == sz ? side : 0;
}

static bool drawRgb565Path(const char* path, const int x, const int y, const float scale) {
    char rgb_path[64];
    if (!pathToRgb565(path, rgb_path, sizeof(rgb_path))) {
        return false;
    }
    const int side = loadRgb565Square(rgb_path);
    if (side <= 0) {
        return false;
    }
    if (scale > 0.99f && scale < 1.01f) {
        M5Cardputer.Display.pushImage(x, y, side, side, s_rgb565_scratch);
        return true;
    }
    M5Cardputer.Display.pushImageRotateZoomWithAA(static_cast<float>(x), static_cast<float>(y), 0.0f,
                                                  0.0f, 0.0f, scale, scale, side, side,
                                                  s_rgb565_scratch);
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
           static_cast<size_t>(w * h) <= RGB565_SCRATCH_PIXELS;
}

// M5GFX 现场解码 → readRect → .rgb565（与屏上观感一致）
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

    // 左上角黑底上解码，再读回库处理后的 RGB565
    M5Cardputer.Display.fillRect(0, 0, w, h, BLACK);
    if (!M5Cardputer.Display.drawPngFile(LittleFS, png_path, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f,
                                        lgfx::v1::datum_t::top_left)) {
        return false;
    }
    M5Cardputer.Display.readRect(0, 0, w, h, s_rgb565_scratch);

    File out = LittleFS.open(out_path, "w");
    if (!out) {
        return false;
    }
    const size_t bytes = static_cast<size_t>(w * h * 2);
    const size_t n = out.write(reinterpret_cast<const uint8_t*>(s_rgb565_scratch), bytes);
    out.close();
    return n == bytes;
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
        if (!is_dir) {
            char path[64];
            if (name.startsWith("/")) {
                snprintf(path, sizeof(path), "%s", name.c_str());
            } else {
                snprintf(path, sizeof(path), "%s/%s", dir, name.c_str());
            }
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
    int ok = 0;
    ok += bakePngFilesInDir("/icon/device");
    ok += bakePngFilesInDir("/icon/ir");
    if (LittleFS.exists("/logo_60.png") && bakePngToRgb565File("/logo_60.png")) {
        ok++;
    }
    if (LittleFS.exists("/logo_50.png") && bakePngToRgb565File("/logo_50.png")) {
        ok++;
    }
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
