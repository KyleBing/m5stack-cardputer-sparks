#include "app_device_icons.h"
#include <FS.h>
#include <LittleFS.h>
#include "M5Cardputer.h"
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

static char s_device_icon_path[48];

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
    return LittleFS.exists("/icon/device/default.png");
}

// 按倍数缩放绘制 PNG（scale=1 为原生 70px）
static bool drawDevicePngNativeScaled(const char* path, const int x, const int y,
                                      const float scale) {
    if (path == nullptr || path[0] == '\0') {
        return false;
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

bool drawAppLogo60(const int x, const int y, const float scale) {
    if (!LittleFS.exists(APP_LOGO_60_PATH)) {
        return false;
    }
    return M5Cardputer.Display.drawPngFile(LittleFS, APP_LOGO_60_PATH, x, y, 0, 0, 0, 0, scale,
                                           scale, lgfx::v1::datum_t::top_left);
}

static bool drawDevicePngPath(const char* path, const int x, const int y, const float scale) {
    if (drawDevicePngNativeScaled(path, x, y, scale)) {
        return true;
    }
    return false;
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
