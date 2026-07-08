#include "app_device_icons.h"
#include <FS.h>
#include <LittleFS.h>
#include "M5Cardputer.h"
#include <cstring>

// 与 assets/img 中文件名对应
static constexpr const char* ICON_FAN = "/img/fan@2x.png";
static constexpr const char* ICON_AIR_PURIFIER = "/img/air_normal@2x.png";
static constexpr const char* ICON_PLUG = "/img/switch_on@2x.png";
static constexpr const char* ICON_DEFAULT = "/img/default@2x.png";

// data/icon/device 原生图标
static constexpr const char* ICON_NATIVE_LAMP = "/icon/device/lamp.png";
static constexpr const char* ICON_NATIVE_BEDLIGHT = "/icon/device/bedlight.png";
static constexpr const char* ICON_NATIVE_BLUMB = "/icon/device/blumb.png";

static const char* const KEYWORDS_LAMP[] = {
    "台灯", "桌灯", "阅读灯", "lamp", "desk", nullptr,
};
static const char* const KEYWORDS_BEDLIGHT[] = {
    "夜灯", "床头", "床灯", "bedlight", "bslamp", "bed", "night", "bedside", nullptr,
};
static const char* const KEYWORDS_BLUMB[] = {
    "灯泡", "球泡", "吸顶", "bulb", "blumb", "ceiling", "mono", "color", "light", nullptr,
};

static char asciiLower(const char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// 子串匹配（英文忽略大小写，中文直接匹配）
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

static bool textContainsAny(const char* text, const char* const* keywords) {
    if (text == nullptr || text[0] == '\0' || keywords == nullptr) {
        return false;
    }
    for (const char* const* kw = keywords; *kw != nullptr; ++kw) {
        if (strContainsIgnoreCase(text, *kw)) {
            return true;
        }
    }
    return false;
}

static bool matchesKeywords(const char* name, const char* model, const char* const* keywords) {
    return textContainsAny(name, keywords) || textContainsAny(model, keywords);
}

const char* deviceIconPathForKind(const MijiaDevKind kind) {
    switch (kind) {
        case MijiaDevKind::FAN_P5:
        case MijiaDevKind::FAN_GENERIC:
            return ICON_FAN;
        case MijiaDevKind::AIR_PURIFIER_F20:
            return ICON_AIR_PURIFIER;
        case MijiaDevKind::PLUG:
            return ICON_PLUG;
        case MijiaDevKind::GENERIC:
            return ICON_DEFAULT;
        default:
            // 灯、空气炸锅等暂无对应 /img PNG，由矢量图标兜底
            return nullptr;
    }
}

const char* deviceIconPathForDevice(const char* name, const char* model, const MijiaDevKind kind) {
    if (matchesKeywords(name, model, KEYWORDS_LAMP)) {
        return ICON_NATIVE_LAMP;
    }
    if (matchesKeywords(name, model, KEYWORDS_BEDLIGHT)) {
        return ICON_NATIVE_BEDLIGHT;
    }
    if (matchesKeywords(name, model, KEYWORDS_BLUMB)) {
        return ICON_NATIVE_BLUMB;
    }
    if (kind == MijiaDevKind::LIGHT) {
        return ICON_NATIVE_BLUMB;
    }
    return nullptr;
}

int deviceIconDrawPx(const MijiaDevice* dev, const MijiaDevKind kind, const int scale) {
    const char* name = dev != nullptr ? dev->name : nullptr;
    const char* model = dev != nullptr ? dev->model : nullptr;
    const char* native_path = deviceIconPathForDevice(name, model, kind);
    if (native_path != nullptr && LittleFS.exists(native_path)) {
        return DEVICE_ICON_NATIVE_PX;
    }
    return DEVICE_ICON_SCALE_BASE * scale;
}

bool deviceIconsAvailable() {
    return LittleFS.exists(ICON_FAN) || LittleFS.exists(ICON_NATIVE_LAMP);
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

bool drawDevicePngFile(const char* path, const int x, const int y, const int size) {
    if (path == nullptr || path[0] == '\0' || size <= 0) {
        return false;
    }
    if (!LittleFS.exists(path)) {
        return false;
    }
    // scale 0 = 等比缩放到 maxWidth×maxHeight 区域内；middle_center 在方框内居中
    return M5Cardputer.Display.drawPngFile(LittleFS, path, x, y, size, size, 0, 0, 0.0f, 0.0f,
                                           lgfx::v1::datum_t::middle_center);
}

bool drawDevicePngIcon(const MijiaDevKind kind, const int x, const int y, const int size) {
    const char* path = deviceIconPathForKind(kind);
    if (path == nullptr) {
        return false;
    }
    return drawDevicePngFile(path, x, y, size);
}

bool drawDeviceIconFor(const MijiaDevice* dev, const MijiaDevKind kind, const int x, const int y,
                       const int scale) {
    const char* name = dev != nullptr ? dev->name : nullptr;
    const char* model = dev != nullptr ? dev->model : nullptr;
    const char* native_path = deviceIconPathForDevice(name, model, kind);
    if (native_path != nullptr && drawDevicePngNative(native_path, x, y)) {
        return true;
    }
    const int px = DEVICE_ICON_SCALE_BASE * scale;
    return drawDevicePngIcon(kind, x, y, px);
}
