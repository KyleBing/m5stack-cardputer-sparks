# M5Cardputer Display API 参考

> 基于 `.pio/libdeps/m5stack-cardputer/` 中的 M5Cardputer / M5Unified / M5GFX 库整理。  
> 相关文档：[M5Unified.md](./M5Unified.md) · [M5Cardputer.md](./M5Cardputer.md)

## 类型与继承链

```cpp
M5Cardputer.Display   // 类型: M5GFX
```

```
M5GFX
  └── LGFX_Device       // 设备控制（亮度、反色、触摸等）
        └── LovyanGFX   // LGFXBase + 文件系统支持
              └── LGFXBase
                    └── Print (Arduino)  // print / println / printf 等
```

| 源码位置 | 说明 |
|----------|------|
| `M5Cardputer/src/M5Cardputer.h` | `M5GFX &Display = M5.Display` |
| `M5GFX/src/M5GFX.h` | `class M5GFX : public lgfx::LGFX_Device` |
| `M5GFX/src/lgfx/v1/LGFXBase.hpp` | 主要绘图 API |
| `M5Unified/src/M5Unified.hpp` | `M5.Display` 定义 |

---

## 1. M5GFX 专有方法

| 方法 | 说明 |
|------|------|
| `progressBar(x, y, w, h, val)` | 绘制进度条 |
| `pushState()` / `popState()` | 保存/恢复字体、光标等显示状态 |
| `init(Panel_Device* panel)` | 指定 Panel 初始化 |
| `getInstance()` | 获取单例 |
| `setResolution(...)` | HDMI 外接屏分辨率（Cardputer 一般不用） |

---

## 2. 设备 / 屏幕控制（LGFX_Device）

| 方法 | 说明 |
|------|------|
| `init()` / `begin()` | 初始化屏幕 |
| `init_without_reset(clear)` | 不 reset 初始化 |
| `getBoard()` | 获取板型 |
| `setRotation(r)` / `getRotation()` | 旋转（0–3） |
| `invertDisplay(bool)` / `getInvert()` | 反色 |
| `setBrightness(0–255)` / `getBrightness()` | 背光亮度 |
| `sleep()` / `wakeup()` | 休眠/唤醒 |
| `powerSave(bool)` / `powerSaveOn()` / `powerSaveOff()` | 省电模式 |
| `width()` / `height()` | 屏幕宽高 |
| `setColorDepth(...)` / `getColorDepth()` | 色深 |
| `writeCommand()` / `writeData()` / `readData8/16/32()` | 底层 SPI 命令 |
| `getTouch()` / `getTouchRaw()` | 触摸（Cardputer 无触摸屏） |

---

## 3. 清屏 / 填充

| 方法 | 说明 |
|------|------|
| `clear()` / `clearDisplay()` | 清屏 |
| `fillScreen(color)` | 全屏填充 |
| `fillRect(x, y, w, h, color)` | 填充矩形 |
| `setColor(r, g, b)` / `setColor(color)` | 设置当前绘制颜色 |
| `setBaseColor(color)` / `getBaseColor()` | 设置/获取背景基色（用于 clear） |

---

## 4. 基础图形绘制

| 方法 | 说明 |
|------|------|
| `drawPixel(x, y, color)` | 画点 |
| `drawLine(x0, y0, x1, y1, color)` | 直线 |
| `drawFastHLine()` / `drawFastVLine()` | 水平/垂直线 |
| `drawRect()` / `fillRect()` | 矩形 |
| `drawRoundRect()` / `fillRoundRect()` | 圆角矩形 |
| `drawCircle()` / `fillCircle()` | 圆 |
| `drawEllipse()` / `fillEllipse()` | 椭圆 |
| `drawTriangle()` / `fillTriangle()` | 三角形 |
| `drawArc()` / `fillArc()` | 圆弧 |
| `drawBezier()` | 贝塞尔曲线 |
| `floodFill()` / `paint()` | 填充封闭区域 |
| `drawGradientLine()` / `fillGradientRect()` | 渐变 |
| `fillSmoothCircle()` / `fillSmoothRoundRect()` | 抗锯齿圆/圆角矩形 |
| `drawWideLine()` / `drawWedgeLine()` | 宽线/楔形线 |
| `drawSpot()` | 绘制圆点 |

---

## 5. 文字 / 打印

| 方法 | 说明 |
|------|------|
| `setCursor(x, y)` / `getCursorX()` / `getCursorY()` | 光标位置 |
| `setTextSize(size)` / `setTextSize(sx, sy)` | 文字缩放 |
| `setTextColor(fg)` / `setTextColor(fg, bg)` | 文字颜色 |
| `setTextDatum(datum)` / `getTextDatum()` | 对齐方式 |
| `setTextWrap(wrapX, wrapY)` | 自动换行 |
| `setTextScroll(scroll)` | 文字滚动 |
| `setTextPadding(padding_x)` | 文字内边距 |
| `setFont(&fonts::Font0)` / `getFont()` | 设置/获取字体 |
| `loadFont(path)` / `unloadFont()` | 加载/卸载 VLW 字体 |
| `showFont(td)` | 预览当前字体 |
| `drawString()` / `drawNumber()` / `drawFloat()` | 指定位置绘制 |
| `drawCenterString()` / `drawRightString()` | 居中/右对齐 |
| `drawChar(unicode, x, y)` | 绘制单个字符 |
| `textWidth()` / `textLength()` / `fontHeight()` / `fontWidth()` | 文字尺寸 |
| `print()` / `println()` | 继承自 Arduino `Print` |
| `printf()` / `vprintf()` | 格式化输出 |
| `write()` | 输出单字符/字节流 |
| `qrcode(string, x, y, width, version, margin)` | 绘制二维码 |
| `cp437(enable)` | CP437 字符集兼容 |

### 常用示例

```cpp
M5Cardputer.Display.setRotation(1);
M5Cardputer.Display.setTextSize(2);
M5Cardputer.Display.setTextColor(WHITE, BLACK);
M5Cardputer.Display.setCursor(0, 0);
M5Cardputer.Display.println("Hello");
M5Cardputer.Display.printf("Value: %d\n", 42);
```

---

## 6. 图片 / 位图

| 方法 | 说明 |
|------|------|
| `pushImage(x, y, w, h, data)` | 推送 RGB 图像 |
| `pushImageRotateZoom(...)` | 旋转缩放 |
| `pushImageAffine(...)` | 仿射变换 |
| `pushImageRotateZoomWithAA(...)` | 带抗锯齿的旋转缩放 |
| `pushAlphaImage(...)` | 带 Alpha 通道 |
| `pushGrayscaleImage(...)` | 灰度图像 |
| `drawBmp()` / `drawJpg()` / `drawPng()` / `drawQoi()` | 从内存解码 |
| `drawBmpFile()` / `drawJpgFile()` / `drawPngFile()` / `drawQoiFile()` | 从文件解码 |
| `drawBitmap()` / `drawXBitmap()` | 1-bit 位图 |
| `createPng()` / `releasePngMemory()` | 生成 PNG 缓冲区 |

---

## 7. 读取 / 滚动 / 裁剪

| 方法 | 说明 |
|------|------|
| `readPixel(x, y)` / `readPixelRGB(x, y)` | 读像素 |
| `readRect()` / `readRectRGB()` | 读区域 |
| `scroll(dx, dy)` | 滚动 |
| `copyRect(dst_x, dst_y, w, h, src_x, src_y)` | 复制区域 |
| `setClipRect()` / `getClipRect()` / `clearClipRect()` | 裁剪区 |
| `setScrollRect()` / `getScrollRect()` / `clearScrollRect()` | 滚动区 |
| `setAddrWindow()` / `setWindow()` | 设置写入窗口 |

---

## 8. 性能相关

| 方法 | 说明 |
|------|------|
| `startWrite()` / `endWrite()` | 批量绘制时减少总线开销 |
| `beginTransaction()` / `endTransaction()` | 事务控制 |
| `writePixels()` / `pushPixels()` | 批量写像素 |
| `writePixelsDMA()` / `pushPixelsDMA()` | DMA 写像素 |
| `initDMA()` / `waitDMA()` / `dmaBusy()` | DMA 控制 |
| `display()` / `waitDisplay()` / `displayBusy()` | EPD 刷新（电子纸） |
| `setAutoDisplay(flg)` | 自动刷新 |

---

## 9. 颜色工具（静态方法）

| 方法 | 说明 |
|------|------|
| `color332(r, g, b)` | RGB → 8-bit |
| `color565(r, g, b)` | RGB → 16-bit |
| `color888(r, g, b)` | RGB → 24-bit |
| `color16to8()` / `color8to16()` | 16/8-bit 互转 |
| `color16to24()` / `color24to16()` | 16/24-bit 互转 |
| `swap565()` / `swap888()` | 字节序转换 |

---

## 10. 颜色常量

定义于 `M5GFX.h`（`m5gfx::ili9341_colors` 命名空间），可直接使用：

```cpp
M5Cardputer.Display.fillScreen(RED);
M5Cardputer.Display.setTextColor(WHITE, BLACK);
```

| 常量 | 说明 |
|------|------|
| `BLACK` / `WHITE` | 黑 / 白 |
| `RED` / `GREEN` / `BLUE` | 红 / 绿 / 蓝 |
| `YELLOW` / `CYAN` / `MAGENTA` | 黄 / 青 / 品红 |
| `ORANGE` / `PINK` / `BROWN` | 橙 / 粉 / 棕 |
| `NAVY` / `DARKGREEN` / `DARKCYAN` | 深色系 |
| `MAROON` / `PURPLE` / `OLIVE` | 深红 / 紫 / 橄榄 |
| `LIGHTGREY` / `DARKGREY` | 浅灰 / 深灰 |
| `GOLD` / `SILVER` / `SKYBLUE` / `VIOLET` | 金 / 银 / 天蓝 / 紫罗兰 |

---

## 11. 文件系统支持

需先 `#include <SD.h>` 或 `#include <SPIFFS.h>` 再 `#include <M5GFX.h>`，然后：

| 方法 | 说明 |
|------|------|
| `setFileStorage(fs)` | 绑定文件系统 |
| `clearFileStorage()` | 清除文件系统绑定 |
| `loadFont(fs, path)` | 从 FS 加载字体 |
| `drawJpgFile(fs, path, ...)` | 从 FS 绘制 JPG |

---

## 12. 本项目常用写法

```cpp
#include "M5Cardputer.h"

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.println("Ready!");
}

void loop() {
    M5Cardputer.update();
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.printf("Key: %s\n", "a");
    M5Cardputer.Display.invertDisplay(true);
}
```

---

## 相关别名

在 `M5Cardputer.h` 中：

```cpp
M5GFX &Display = M5.Display;
M5GFX &Lcd     = Display;   // 与 Display 相同
```
