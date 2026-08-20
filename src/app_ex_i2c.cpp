#include "app_ex_i2c.h"
#include "app_cc1101.h"
#include "app_common.h"
#include "app_gps.h"
#include "app_header.h"
#include "app_i2c_scan.h"
#include "app_nfc.h"
#include "app_radio.h"

namespace {

enum class ExI2cMode {
    HUB,
    RADIO,
    SCAN,
    CC1101,
    NFC,
    GPS,
};

struct ExI2cHubItem {
    const char* title;
    ExI2cMode mode;
    char letter; // 0 = 无字母快捷键；与页内数字并存，跨页直达
};

static constexpr int EXI2C_HUB_ITEMS_PER_PAGE = 8;

static constexpr ExI2cHubItem EXI2C_HUB_ITEMS[] = {
    {"RADIO", ExI2cMode::RADIO, 'r'},
    {"EXI2", ExI2cMode::SCAN, 'e'},
    {"CC1101", ExI2cMode::CC1101, 'c'},
    {"NFC", ExI2cMode::NFC, 'n'},
    {"GPS", ExI2cMode::GPS, 'g'},
};
static constexpr int EXI2C_HUB_ITEM_COUNT =
    static_cast<int>(sizeof(EXI2C_HUB_ITEMS) / sizeof(EXI2C_HUB_ITEMS[0]));

static ExI2cMode g_mode = ExI2cMode::HUB;
static int g_hub_page = 0;

// ExI2 hub 用暖绿主题（与 Games 金、Test 青区分）
static uint16_t exI2cHubRgb(const uint8_t r, const uint8_t g, const uint8_t b) {
    return M5Cardputer.Display.color565(r, g, b);
}

static uint16_t exI2cHubBg() {
    return exI2cHubRgb(0x04, 0x0C, 0x06);
}

static uint16_t exI2cHubCardBg() {
    return exI2cHubRgb(0x0A, 0x1A, 0x10);
}

static uint16_t exI2cHubAccent() {
    return exI2cHubRgb(0x6A, 0xD4, 0x7A);
}

static uint16_t exI2cHubBorder() {
    return exI2cHubRgb(0x3A, 0x8C, 0x48);
}

static uint16_t exI2cHubTitleColor() {
    return exI2cHubRgb(0xE0, 0xF4, 0xE4);
}

static void drawExI2cHubCard(const int x, const int y, const char key, const char* title) {
    const uint16_t card_bg = exI2cHubCardBg();
    const uint16_t accent = exI2cHubAccent();
    M5Cardputer.Display.fillRoundRect(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, 4, card_bg);
    M5Cardputer.Display.drawRoundRect(x, y, APP_HUB_CARD_W, APP_HUB_CARD_H, 4, exI2cHubBorder());
    M5Cardputer.Display.fillRoundRect(x + 3, y + 3, 18, 16, 3, accent);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(BLACK, accent);
    M5Cardputer.Display.setCursor(x + 9, y + 7);
    M5Cardputer.Display.print(key);
    M5Cardputer.Display.setTextColor(exI2cHubTitleColor(), card_bg);
    M5Cardputer.Display.setCursor(x + 28, y + 7);
    M5Cardputer.Display.print(title);
}

static void drawExI2cHubCardAt(const int index, const char key, const char* title) {
    const int row = index / APP_HUB_CARD_COLS;
    const int col = index % APP_HUB_CARD_COLS;
    const int x = APP_HUB_CARD_ORIGIN_X + col * (APP_HUB_CARD_W + APP_HUB_CARD_GAP_X);
    const int y = APP_HUB_CARD_ORIGIN_Y + row * (APP_HUB_CARD_H + APP_HUB_CARD_GAP_Y);
    drawExI2cHubCard(x, y, key, title);
}

static int getExI2cHubPageCount() {
    return (EXI2C_HUB_ITEM_COUNT + EXI2C_HUB_ITEMS_PER_PAGE - 1) / EXI2C_HUB_ITEMS_PER_PAGE;
}

static void drawExI2cHubCards() {
    const int start = g_hub_page * EXI2C_HUB_ITEMS_PER_PAGE;
    const int end = min(start + EXI2C_HUB_ITEMS_PER_PAGE, EXI2C_HUB_ITEM_COUNT);
    for (int item = start; item < end; ++item) {
        const int slot = item - start;
        drawExI2cHubCardAt(slot, static_cast<char>('1' + slot), EXI2C_HUB_ITEMS[item].title);
    }
}

static void showExI2cHubScreen() {
    g_mode = ExI2cMode::HUB;
    beginAppHubScreen("EX I2C", exI2cHubBg(), g_hub_page, getExI2cHubPageCount());
    drawExI2cHubCards();
}

static void leaveExI2cChild(const ExI2cMode mode) {
    if (mode == ExI2cMode::RADIO) {
        leaveRadioApp();
    } else if (mode == ExI2cMode::SCAN) {
        silenceFmRadioOnBus(M5Cardputer.Ex_I2C);
        resetI2cScanHelp();
    } else if (mode == ExI2cMode::CC1101) {
        leaveCc1101App();
    } else if (mode == ExI2cMode::NFC) {
        leaveNfcApp();
    } else if (mode == ExI2cMode::GPS) {
        leaveGpsApp();
    }
}

static void selectExI2cMode(const ExI2cMode mode) {
    leaveExI2cChild(g_mode);
    if (mode != ExI2cMode::HUB) {
        clearAppHeaderStatusRefresh();
    }
    g_mode = mode;
    if (mode == ExI2cMode::RADIO) {
        enterRadioApp();
    } else if (mode == ExI2cMode::SCAN) {
        resetI2cScanHelp();
        drawI2cScanApp(M5Cardputer.Ex_I2C, "ExI2", false);
    } else if (mode == ExI2cMode::CC1101) {
        enterCc1101App();
    } else if (mode == ExI2cMode::NFC) {
        enterNfcApp();
    } else if (mode == ExI2cMode::GPS) {
        enterGpsApp();
    } else {
        showExI2cHubScreen();
    }
}

} // namespace

void enterExI2cApp() {
    leaveExI2cApp();
    g_hub_page = 0;
    g_mode = ExI2cMode::HUB;
    showExI2cHubScreen();
}

void leaveExI2cApp() {
    leaveExI2cChild(g_mode);
    g_mode = ExI2cMode::HUB;
}

void updateExI2cApp() {
    if (g_mode == ExI2cMode::RADIO) {
        updateRadioApp();
    } else if (g_mode == ExI2cMode::CC1101) {
        updateCc1101App();
    } else if (g_mode == ExI2cMode::NFC) {
        updateNfcApp();
    } else if (g_mode == ExI2cMode::GPS) {
        updateGpsApp();
    }
}

void handleExI2cApp(const Keyboard_Class::KeysState& status) {
    if (g_mode == ExI2cMode::HUB) {
        int delta = getMenuNavDelta(status);
        if (delta == 0) {
            delta = getBracketNavDelta(status);
        }
        const int page_count = getExI2cHubPageCount();
        if (delta != 0 && page_count > 1) {
            g_hub_page = (g_hub_page + delta + page_count) % page_count;
            showExI2cHubScreen();
            return;
        }
        for (const char raw : status.word) {
            const char c =
                (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
            if (c >= '1' && c <= '8') {
                const int item = g_hub_page * EXI2C_HUB_ITEMS_PER_PAGE + (c - '1');
                if (item < EXI2C_HUB_ITEM_COUNT) {
                    selectExI2cMode(EXI2C_HUB_ITEMS[item].mode);
                    return;
                }
            }
            for (int i = 0; i < EXI2C_HUB_ITEM_COUNT; ++i) {
                if (EXI2C_HUB_ITEMS[i].letter != '\0' && EXI2C_HUB_ITEMS[i].letter == c) {
                    selectExI2cMode(EXI2C_HUB_ITEMS[i].mode);
                    return;
                }
            }
        }
        return;
    }
    if (g_mode == ExI2cMode::RADIO) {
        handleRadioApp(status);
    } else if (g_mode == ExI2cMode::SCAN) {
        handleI2cScanApp(getPressedKey(), M5Cardputer.Ex_I2C, "ExI2", false);
    } else if (g_mode == ExI2cMode::CC1101) {
        handleCc1101App(status);
    } else if (g_mode == ExI2cMode::NFC) {
        handleNfcApp(status);
    } else if (g_mode == ExI2cMode::GPS) {
        handleGpsApp(status);
    }
}

bool handleExI2cBack() {
    if (g_mode == ExI2cMode::HUB) {
        return false;
    }
    selectExI2cMode(ExI2cMode::HUB);
    return true;
}

bool closeExI2cHelp() {
    if (g_mode == ExI2cMode::RADIO) {
        if (closeRadioHelp()) {
            return true;
        }
        if (closeRadioStations()) {
            return true;
        }
        return closeRadioSeek();
    }
    if (g_mode == ExI2cMode::SCAN) {
        return closeI2cScanHelp(M5Cardputer.Ex_I2C, "ExI2", false);
    }
    if (g_mode == ExI2cMode::CC1101) {
        return closeCc1101Help();
    }
    if (g_mode == ExI2cMode::NFC) {
        return closeNfcHelp();
    }
    if (g_mode == ExI2cMode::GPS) {
        return closeGpsHelp();
    }
    return false;
}

bool isExI2cHelpVisible() {
    if (g_mode == ExI2cMode::RADIO) {
        return isRadioHelpVisible();
    }
    if (g_mode == ExI2cMode::SCAN) {
        return isI2cScanHelpVisible();
    }
    if (g_mode == ExI2cMode::CC1101) {
        return isCc1101HelpVisible();
    }
    if (g_mode == ExI2cMode::NFC) {
        return isNfcHelpVisible();
    }
    if (g_mode == ExI2cMode::GPS) {
        return isGpsHelpVisible();
    }
    return false;
}

bool isExI2cRadioActive() {
    return g_mode == ExI2cMode::RADIO;
}

bool isExI2cCc1101Active() {
    return g_mode == ExI2cMode::CC1101;
}
