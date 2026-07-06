# M5Cardputer API 参考

> 基于 `.pio/libdeps/m5stack-cardputer/M5Cardputer/` 库整理。  
> 全局实例：`extern m5::M5_CARDPUTER M5Cardputer;`  
> 底层依赖 [M5Unified](./M5Unified.md)，屏幕 API 见 [Display.md](./Display.md)。

## 概述

M5Cardputer 库在 M5Unified 基础上封装了 Cardputer 专属键盘（TCA8418 I2C 矩阵键盘），并提供统一的 `begin()` / `update()` 入口。

```cpp
#include "M5Cardputer.h"

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
}

void loop() {
    M5Cardputer.update();  // 必须调用，更新 M5 + 键盘状态
}
```

---

## M5_CARDPUTER 主类

### 初始化

| 方法 | 说明 |
|------|------|
| `begin(enableKeyboard = true)` | 默认配置初始化，默认启用键盘 |
| `begin(cfg, enableKeyboard = true)` | 自定义 M5Unified 配置初始化 |
| `update()` | 更新 M5 状态 + 键盘扫描（需在 loop 中调用） |

`begin()` 内部调用 `M5.begin(cfg)`，若 `enableKeyboard` 为 true 则自动 `Keyboard.begin()`。

`update()` 内部调用：
1. `M5.update()` — 更新按键/触摸等
2. `Keyboard.updateKeyList()` — 扫描物理按键
3. `Keyboard.updateKeysState()` — 更新按键状态缓冲区

### 成员引用

| 成员 | 类型 | 说明 |
|------|------|------|
| `Display` / `Lcd` | `M5GFX&` | 屏幕（引用 `M5.Display`） |
| `Power` | `Power_Class&` | 电源管理 |
| `Speaker` | `Speaker_Class&` | 扬声器 |
| `Mic` | `Mic_Class&` | 麦克风 |
| `BtnA` | `Button_Class&` | 机身按键 A（`M5.getButton(0)`） |
| `Keyboard` | `Keyboard_Class` | Cardputer 矩阵键盘 |
| `In_I2C` | `I2C_Class&` | 内部 I2C（键盘、RTC 等） |
| `Ex_I2C` | `I2C_Class&` | 外部 I2C（HY2.0-4P Port） |

---

## Keyboard_Class 键盘

键盘通过 TCA8418 I2C 芯片读取 4×14 矩阵，支持 Fn/Shift/Ctrl 等修饰键。

### 初始化

| 方法 | 说明 |
|------|------|
| `begin()` | 默认 Reader 初始化 |
| `begin(unique_ptr<KeyboardReader>)` | 自定义 Reader 初始化 |

### 状态查询

| 方法 | 说明 |
|------|------|
| `isChange()` | 按键状态是否有变化 |
| `isPressed()` | 是否有按键被按下（返回按下数量） |
| `isKeyPressed(char c)` | 指定字符是否被按下 |
| `keysState()` | 获取当前按键状态（`KeysState&`） |
| `keyList()` | 当前按下的键坐标列表 |
| `capslocked()` | Caps Lock 是否开启 |
| `setCapsLocked(bool)` | 设置 Caps Lock |

### 更新（通常由 M5Cardputer.update 自动调用）

| 方法 | 说明 |
|------|------|
| `updateKeyList()` | 扫描物理按键，更新按键列表 |
| `updateKeysState()` | 根据按键列表更新状态缓冲区 |

### 键值查询

| 方法 | 说明 |
|------|------|
| `getKey(Point2D_t keyCoor)` | 获取键坐标对应的键码 |
| `getKeyValue(keyCoor)` | 获取键的正常/Shift 字符映射 |

---

## KeysState 结构体

`Keyboard.keysState()` 返回的状态对象：

```cpp
struct KeysState {
    bool tab;           // Tab 键
    bool fn;            // Fn 键
    bool shift;         // Shift 键
    bool ctrl;          // Ctrl 键
    bool opt;           // Opt 键
    bool alt;           // Alt 键
    bool del;           // 退格键
    bool enter;         // 回车键
    bool space;         // 空格键
    uint8_t modifiers;  // 修饰键位掩码

    std::vector<char> word;              // 当前按下的可打印字符
    std::vector<uint8_t> hid_keys;       // HID 键码
    std::vector<uint8_t> modifier_keys;  // 修饰键码

    void reset();  // 重置所有状态
};
```

### 常用读取方式

```cpp
// 获取当前按下的字符串
Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
String key;
for (char c : status.word) {
    key += c;
}

// 判断修饰键
if (status.shift) { /* Shift 按下 */ }
if (status.ctrl)  { /* Ctrl 按下 */ }
if (status.fn)    { /* Fn 按下 */ }
if (status.enter) { /* Enter 按下 */ }
if (status.del)   { /* Backspace 按下 */ }
```

---

## 特殊键常量

定义于 `Keyboard_def.h`：

| 常量 | 值 | 说明 |
|------|-----|------|
| `KEY_LEFT_CTRL` | 0x80 | 左 Ctrl |
| `KEY_LEFT_SHIFT` | 0x81 | 左 Shift |
| `KEY_LEFT_ALT` | 0x82 | 左 Alt |
| `KEY_FN` | 0xFF | Fn |
| `KEY_OPT` | 0x00 | Opt |
| `KEY_BACKSPACE` | 0x2A | 退格 |
| `KEY_TAB` | 0x2B | Tab |
| `KEY_ENTER` | 0x28 | 回车 |
| `SHIFT` | 0x80 | Shift 标志位 |

---

## 键盘布局（4 行 × 14 列）

```
行0: ` 1 2 3 4 5 6 7 8 9 0 - = Backspace
行1: Tab q w e r t y u i o p [ ] \
行2: Fn Shift a s d f g h j k l ; ' Enter
行3: Ctrl Opt Alt z x c v b n m , . / Space
```

每个键有正常字符和 Shift 字符（见 `_key_value_map`）。

---

## 典型用法

### 按键输入

```cpp
void loop() {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        if (M5Cardputer.Keyboard.isPressed()) {
            auto status = M5Cardputer.Keyboard.keysState();

            M5Cardputer.Display.clear();
            M5Cardputer.Display.setCursor(0, 0);

            String key;
            for (char c : status.word) key += c;
            M5Cardputer.Display.printf("Key: %s\n", key.c_str());

            if (status.enter) {
                // 回车处理
            }
            if (status.del) {
                // 退格处理
            }
        }
    }
}
```

### 禁用键盘

```cpp
M5Cardputer.begin(cfg, false);  // 不初始化键盘
```

### 使用 M5Unified 子模块

Cardputer 通过引用直接使用 M5Unified 组件：

```cpp
// 电源
int level = M5Cardputer.Power.getBatteryLevel();

// 扬声器
M5Cardputer.Speaker.tone(1000, 200);

// 麦克风
int16_t buf[512];
M5Cardputer.Mic.record(buf, 512);

// 机身按键
if (M5Cardputer.BtnA.wasPressed()) { /* ... */ }

// 外部 I2C 设备
M5Cardputer.Ex_I2C.begin();
```

---

## Cardputer 硬件说明

| 项目 | 说明 |
|------|------|
| 主控 | ESP32-S3（StampS3） |
| 屏幕 | ST7789 135×240（通过 M5GFX） |
| 键盘 | TCA8418 I2C 矩阵键盘 |
| 音频 | 内置 Speaker + Mic（I2S） |
| 按键 | BtnA（机身侧键） |
| 触摸 | 无 |
| IMU | 无（Cardputer 标准版） |

---

## 源码路径

| 文件 | 内容 |
|------|------|
| `M5Cardputer/src/M5Cardputer.h` | 主类定义 |
| `M5Cardputer/src/M5Cardputer.cpp` | begin/update 实现 |
| `M5Cardputer/src/utility/Keyboard/Keyboard.h` | 键盘类 |
| `M5Cardputer/src/utility/Keyboard/Keyboard_def.h` | 键码定义 |
| `M5Cardputer/src/utility/Keyboard/KeyboardReader/TCA8418.h` | I2C 键盘驱动 |
| `M5Cardputer/src/utility/Keyboard/KeyboardReader/IOMatrix.h` | IO 矩阵 Reader |
