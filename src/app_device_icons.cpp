#include "app_device_icons.h"
#include <FS.h>
#include <LittleFS.h>
#include "M5Cardputer.h"
#include <cstring>

// 与 data/icon/device 中文件名一致；较长名称靠前，避免短名误匹配
static const char* const DEVICE_ICON_NAMES[] = {
    "airpurifier",
    "wifispeaker",
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

int deviceIconDrawPx(const MijiaDevice* /*dev*/) {
    return DEVICE_ICON_NATIVE_PX;
}

bool deviceIconsAvailable() {
    return LittleFS.exists("/icon/device/default.png");
}

bool drawDevicePngNative(const char* path, const int x, const int y) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (!LittleFS.exists(path)) {
        return false;
    }
    // maxWidth/maxHeight=0 且 scale=1 表示按 PNG 原始尺寸绘制
    return M5Cardputer.Display.drawPngFile(LittleFS, path, x, y, 0, 0, 0, 0, 1.0f, 1.0f,
                                           lgfx::v1::datum_t::top_left);
}

static bool drawDevicePngPath(const char* path, const int x, const int y) {
    if (drawDevicePngNative(path, x, y)) {
        return true;
    }
    return false;
}

bool drawDeviceIconDefault(const int x, const int y, const bool active) {
    const char* path = deviceIconPathForModel(nullptr, active);
    if (drawDevicePngPath(path, x, y)) {
        return true;
    }
    if (active) {
        return drawDevicePngPath(deviceIconPathForModel(nullptr, false), x, y);
    }
    return false;
}

bool drawDeviceIconFor(const MijiaDevice* dev, const int x, const int y, const bool active) {
    const char* model = dev != nullptr ? dev->model : nullptr;
    const char* path = deviceIconPathForModel(model, active);
    if (drawDevicePngPath(path, x, y)) {
        return true;
    }
    // active 图缺失时回退普通图
    if (active) {
        path = deviceIconPathForModel(model, false);
        if (drawDevicePngPath(path, x, y)) {
            return true;
        }
    }
    return drawDeviceIconDefault(x, y, active);
}
