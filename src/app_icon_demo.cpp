#include "app_icon_demo.h"
#include "app_colors.h"
#include "app_common.h"
#include "app_device_icons.h"
#include "app_header.h"
#include "app_icons.h"
#include <cstdio>

struct IconDemoItem {
    const char* name;
    int width;
    int height;
    void (*draw)(int x, int y);
};

static int iconDemoPage = 0;
static constexpr int ICON_DEMO_ITEMS_PER_PAGE = 1;
static constexpr int ICON_DEMO_SIZE = 64;

// 将小图标居中绘制在 ICON_DEMO_SIZE 方框内
static void drawDemoInBox(const int x, const int y, const int w, const int h,
                          void (*draw)(int, int)) {
    draw(x + (ICON_DEMO_SIZE - w) / 2, y + (ICON_DEMO_SIZE - h) / 2);
}

static void drawDemoLogo(const int x, const int y) { drawAppLogo(x, y, ICON_DEMO_SIZE); }
static void drawDemoArrowLeft(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowLeft(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowRight(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowRight(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowUp(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowUp(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowDown(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowDown(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowLeftRight(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_LR_W, ICON_ARROW_H, [](const int bx, const int by) {
        drawIconArrowLeftRight(bx, by + 4, WHITE);
    });
}
static void drawDemoArrowUpDown(const int x, const int y) {
    drawDemoInBox(x, y, ICON_ARROW_W, ICON_ARROW_UD_H, [](const int bx, const int by) {
        drawIconArrowUpDown(bx, by + 7, WHITE);
    });
}
static void drawDemoSignalBars(const int x, const int y) {
    drawDemoInBox(x, y, ICON_SIGNAL_W, ICON_SIGNAL_H, [](const int bx, const int by) {
        drawSignalBars(bx, by, -56, WHITE);
    });
}
static void drawDemoWifi(const int x, const int y) {
    drawDemoInBox(x, y, ICON_WIFI_W, ICON_WIFI_H, [](const int bx, const int by) {
        drawIconWifi(bx, by, -56, WHITE);
    });
}
static void drawDemoBle(const int x, const int y) {
    drawDemoInBox(x, y, ICON_BLE_W, ICON_BLE_H,
                  [](const int bx, const int by) { drawIconBle(bx, by, WHITE); });
}
static void drawDemoChargingBolt(const int x, const int y) {
    drawDemoInBox(x, y, 6, 12, [](const int bx, const int by) { drawIconChargingBolt(bx, by, 12); });
}
static void drawDemoBattery(const int x, const int y) {
    const int bw = getIconBatteryDisplayWidth(false);
    const int bh = getIconBatteryBodyHeight();
    drawDemoInBox(x, y, bw, bh,
                  [](const int bx, const int by) { drawIconBattery(bx, by, 75, false); });
}
static void drawDemoPageDots(const int x, const int y) {
    drawDemoInBox(x, y, 22, 4, [](const int bx, const int by) { drawIconPageDots(bx, by + 2, 1, 4); });
}
static void drawDemoInfoChip(const int x, const int y) {
    drawIconInfoChipSized(x, y, WHITE, ICON_DEMO_SIZE);
}
static void drawDemoInfoStorage(const int x, const int y) {
    drawIconInfoStorageSized(x, y, WHITE, ICON_DEMO_SIZE);
}
static void drawDemoInfoBattery(const int x, const int y) {
    drawIconInfoBatterySized(x, y, WHITE, ICON_DEMO_SIZE);
}

static constexpr int DEVICE_ICON_DEMO_GAP = 8;
static constexpr int DEVICE_ICON_DEMO_PAIR_W = DEVICE_ICON_NATIVE_PX * 2 + DEVICE_ICON_DEMO_GAP;

// 并排展示 off / on 两种原生设备图标
static void drawDemoDevicePair(const char* basename, const int x, const int y) {
    char path[48];
    snprintf(path, sizeof(path), "/icon/device/%s.png", basename);
    drawDevicePngNative(path, x, y);
    snprintf(path, sizeof(path), "/icon/device/%s_active.png", basename);
    drawDevicePngNative(path, x + DEVICE_ICON_NATIVE_PX + DEVICE_ICON_DEMO_GAP, y);
}

#define DEFINE_DEVICE_ICON_DEMO(name)                                                              \
    static void drawDemoDevice_##name(const int x, const int y) {                                  \
        drawDemoDevicePair(#name, x, y);                                                           \
    }

DEFINE_DEVICE_ICON_DEMO(airpurifier)
DEFINE_DEVICE_ICON_DEMO(bslamp2)
DEFINE_DEVICE_ICON_DEMO(camera)
DEFINE_DEVICE_ICON_DEMO(cooker)
DEFINE_DEVICE_ICON_DEMO(fan)
DEFINE_DEVICE_ICON_DEMO(fryer)
DEFINE_DEVICE_ICON_DEMO(juicer)
DEFINE_DEVICE_ICON_DEMO(lamp2)
DEFINE_DEVICE_ICON_DEMO(plug)
DEFINE_DEVICE_ICON_DEMO(sensor_ht)
DEFINE_DEVICE_ICON_DEMO(wifispeaker)
DEFINE_DEVICE_ICON_DEMO(default)

static const IconDemoItem ICON_DEMO_ITEMS[] = {
    {"app logo", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoLogo},
    {"arrow left", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowLeft},
    {"arrow right", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowRight},
    {"arrow up", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowUp},
    {"arrow down", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowDown},
    {"arrow left-right", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowLeftRight},
    {"arrow up-down", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoArrowUpDown},
    {"signal bars", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoSignalBars},
    {"wifi", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoWifi},
    {"ble", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoBle},
    {"charging bolt", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoChargingBolt},
    {"battery", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoBattery},
    {"page dots", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoPageDots},
    {"info chip", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoInfoChip},
    {"info storage", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoInfoStorage},
    {"info battery", ICON_DEMO_SIZE, ICON_DEMO_SIZE, drawDemoInfoBattery},
    {"device airpurifier", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_airpurifier},
    {"device bslamp2", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_bslamp2},
    {"device camera", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_camera},
    {"device cooker", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_cooker},
    {"device fan", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_fan},
    {"device fryer", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_fryer},
    {"device juicer", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_juicer},
    {"device lamp2", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_lamp2},
    {"device plug", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_plug},
    {"device sensor_ht", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_sensor_ht},
    {"device wifispeaker", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX,
     drawDemoDevice_wifispeaker},
    {"device default", DEVICE_ICON_DEMO_PAIR_W, DEVICE_ICON_NATIVE_PX, drawDemoDevice_default},
};

static int getIconDemoItemCount() {
    return sizeof(ICON_DEMO_ITEMS) / sizeof(ICON_DEMO_ITEMS[0]);
}

static int getIconDemoPageCount() {
    const int total = getIconDemoItemCount();
    return (total + ICON_DEMO_ITEMS_PER_PAGE - 1) / ICON_DEMO_ITEMS_PER_PAGE;
}

static void drawIconDemoApp() {
    beginAppScreen("Icons");
    const int page_count = getIconDemoPageCount();
    if (iconDemoPage >= page_count) {
        iconDemoPage = page_count - 1;
    }
    if (iconDemoPage < 0) {
        iconDemoPage = 0;
    }

    int y = APP_CONTENT_Y;
    char buf[32];
    int cx = APP_CONTENT_X;
    cx += drawArrowBadge(cx, y, 1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.setCursor(cx, y);
    M5Cardputer.Display.print("page");
    snprintf(buf, sizeof(buf), "page %d/%d", iconDemoPage + 1, page_count);
    M5Cardputer.Display.setCursor(APP_CONTENT_X + 168, y);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
    M5Cardputer.Display.print(buf);
    y += INFO_LINE_H + 4;

    const int start = iconDemoPage * ICON_DEMO_ITEMS_PER_PAGE;
    int end = start + ICON_DEMO_ITEMS_PER_PAGE;
    const int total = getIconDemoItemCount();
    if (end > total) {
        end = total;
    }

    for (int i = start; i < end; i++) {
        const IconDemoItem& item = ICON_DEMO_ITEMS[i];
        const int row_y = y;
        const bool large_icon = item.height > ICON_DEMO_SIZE;
        const int title_size = large_icon ? 1 : 2;
        const int title_line_h = title_size == 2 ? INFO_LINE_H_2X : INFO_LINE_H;

        M5Cardputer.Display.setTextSize(title_size);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, row_y);
        M5Cardputer.Display.printf("%02d %s", i + 1, item.name);

        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(APP_COLOR_HINT, BLACK);
        M5Cardputer.Display.setCursor(APP_CONTENT_X, row_y + title_line_h);
        if (large_icon) {
            M5Cardputer.Display.printf("off | on  %dx%d", item.width, item.height);
        } else {
            M5Cardputer.Display.printf("size %dx%d", item.width, item.height);
        }

        const int label_bottom = row_y + title_line_h + INFO_LINE_H + 4;
        const int avail_h = M5Cardputer.Display.height() - label_bottom - 4;
        const int icon_x = M5Cardputer.Display.width() - APP_CONTENT_X - item.width;
        const int icon_y = label_bottom + (avail_h - item.height) / 2;
        item.draw(icon_x, icon_y);

        y += title_line_h + INFO_LINE_H + item.height + 12;
    }
}

void enterIconDemoApp() {
    iconDemoPage = 0;
    drawIconDemoApp();
}

void handleIconDemoNav(const Keyboard_Class::KeysState& status) {
    const int delta = getMenuNavDelta(status);
    if (delta == 0) {
        return;
    }
    const int page_count = getIconDemoPageCount();
    iconDemoPage = (iconDemoPage + delta + page_count) % page_count;
    drawIconDemoApp();
}
