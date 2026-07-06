#include "M5Cardputer.h"

struct VersionInfo {
    const String version;
    const String update_time;
    const String author;
    const String email;
    const String website;
};

// 返回固件版本信息
VersionInfo getVersionInfo() {
    return VersionInfo{
        "0.0.1",
        "2026-07-06",
        "KyleBing",
        "kylebing@163.com",
        "kylebing.cn"
    };
}

void setup() {
    const auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setBrightness(30);
    M5Cardputer.Display.println("PRESS ANY KEY");
}


void loop() {
    M5Cardputer.update();


    // 键盘 - 机身按键
    // wasPressed() 仅在按下瞬间触发一次，避免按住时反复切换
    if (M5Cardputer.BtnA.wasPressed()) {
        const bool isDisplayInverted = M5Cardputer.Display.getInvert();
        if (isDisplayInverted) {
            M5Cardputer.Display.invertDisplay(false);
        } else {
            M5Cardputer.Display.invertDisplay(true);
        }

        M5Cardputer.Display.clear();
        M5Cardputer.Display.setCursor(5, 5);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.println("SCREEN INVERTED");
    }


    // 键盘 - 矩阵键盘
    if (M5Cardputer.Keyboard.isChange()) {
        // 如果有按键被按下
        if (M5Cardputer.Keyboard.isPressed()) {
            // 通过 keysState().word 获取当前按下的字符
            const Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
            String key;
            for (const char c : status.word) {
                key += c;
            }


            M5Cardputer.Display.clear();
            M5Cardputer.Display.setCursor(5, 5);
            M5Cardputer.Display.setTextSize(5);
            if (key != "v") {
                M5Cardputer.Display.printf("%s\n", key.c_str());
                Serial.println(key);
            }

            // 逻辑处理：如果是 'a' 则反色
            if (key == "b") {
                M5Cardputer.Display.setTextSize(2);
                const auto brightness = M5Cardputer.Display.getBrightness();
                M5Cardputer.Display.printf("brightness: %d\n", brightness);
            } else if (key.length() == 1 && key[0] >= '0' && key[0] <= '9') {
                // 0-9 亮度控制：0 为 0，1-9 均分 255
                const int level = key[0] - '0';
                const uint8_t brightness = level * 255 / 9;
                M5Cardputer.Display.setBrightness(brightness);
                M5Cardputer.Display.setTextSize(2);
                M5Cardputer.Display.printf("level %d -> %d\n", level, brightness);
            } else if (key == "v") {
                const VersionInfo versionInfo = getVersionInfo();
                M5Cardputer.Display.setTextSize(3);
                M5Cardputer.Display.println("SHOW KEYS");
                M5Cardputer.Display.setTextSize(2);
                M5Cardputer.Display.printf(" auth: \t%s\n", versionInfo.author.c_str());
                M5Cardputer.Display.printf(" date: \t%s\n", versionInfo.update_time.c_str());
                M5Cardputer.Display.printf("  web: \t%s\n", versionInfo.website.c_str());
            }
        }
    }
    delay(10);
}