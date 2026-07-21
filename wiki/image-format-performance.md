# 图片格式与显示速度

> 针对 Cardputer（ESP32-S3 + SPI RGB565 屏）固件中的图标绘制。  
> 相对耗时为经验量级，非精确 benchmark；同尺寸对比以 **RGB565 已在 RAM、`pushImage`** 为 **1×**。

---

## 1. 结论先看

同尺寸下，常见路径大致是：

**RGB565 直推 ≫ ARGB8888 Alpha 混合 ≫ 每次现场解 PNG**

| 优先级 | 做法 | 适用 |
|--------|------|------|
| 要最快 | 预生成 **RGB565**（透明烤进黑底）→ `pushImage` | 底色固定为黑的 UI 图标 |
| 要透明叠任意底 | 预生成 **ARGB8888** → `pushAlphaImage` | 需要半透明/抗锯齿叠在多变底色上 |
| 要省 Flash、改图方便 | 保留 **PNG**，首次解码后 **RAM 缓存** | 数量少、可接受首次卡顿 |
| 不推荐反复 | 每次 `drawPngFile` | 列表翻页、按键闪烁等高频重绘 |

面板最终都是 **RGB565**；ARGB8888 / PNG 都会在上屏前变成 16-bit。

---

## 2. 各路径实际在做什么

### 2.1 PNG（`drawPngFile`）

```
LittleFS 读文件 → zlib 解压 → PNG filter 还原 → 颜色/透明处理 → 写屏
```

- 慢主要慢在 **CPU 解码**（zlib + filter），不是 30×30 像素本身。
- 有 Alpha 时由 M5GFX/LovyanGFX 做混合，画质通常最好、也最省 Flash。
- **每次**重绘都走全流程就会明显卡（空调模式切换、米家列表等）。

### 2.2 预生成 RGB565（`.rgb565`）

```
LittleFS 读原始像素 → pushImage（直接写屏）
```

- **无 zlib、无 Alpha 混合**；Flash / SPI 约为 ARGB 的一半。
- **不支持透明通道**：透明区需提前合成到固定底色（本项目 UI 多为黑底）。
- 手写转换若处理 Alpha/抗锯齿不当，观感会明显差于库解码 PNG；应用「与屏上一致」的像素（例如库解码后再落盘）画质才可靠。

### 2.3 预生成 ARGB8888（`.argb8888`）

```
LittleFS 读原始像素 → pushAlphaImage（读底色 + 混合 + 写回）
```

- **跳过 zlib**，保留完整 Alpha。
- 字节序需与 `lgfx::argb8888_t` 一致：**B, G, R, A**（见 `scripts/png_to_rgba8888.py`）。
- 比 RGB565 慢，主要因为 **逐像素 Alpha**，其次是 4 字节/像素的读写量。

### 2.4 PNG 解码一次 + RAM 缓存

```
首次：drawPngFile → readRect 存 RGB565
之后：pushImage
```

- 画质对齐库解码；重绘接近 RGB565 直推。
- 占 RAM：`宽 × 高 × 2 × 槽位数`（空调 8 张 30×30 ≈ 14 KB）。

---

## 3. 相对耗时（同分辨率）

| 路径 | 相对耗时 | 说明 |
|------|----------|------|
| RGB565 已在 RAM → `pushImage` | **1×** | 最快 |
| RGB565 从 Flash → `pushImage` | **约 1.5–3×** | 多 FS 读 |
| ARGB8888 已在 RAM → `pushAlphaImage` | **约 3–6×** | Alpha 混合 |
| ARGB8888 从 Flash → `pushAlphaImage` | **约 4–8×** | FS + Alpha |
| PNG 每次 `drawPngFile` | **约 10–40×** | zlib + 解码，波动大 |

### 绝对量级（体感参考）

**30×30：**

| 路径 | 大约 |
|------|------|
| RGB565 `pushImage`（RAM） | &lt; 1 ms |
| ARGB8888 `pushAlphaImage`（RAM） | 约 2–5 ms |
| PNG 现场解码 | 约 10–30+ ms |

**70×70：** 大约再乘 **5–6 倍**（按面积）。

若整页还有清屏、大量文字、布局计算，图标优化后「体感不明显」是正常的——瓶颈可能不在图标。

---

## 4. Flash / RAM 占用（同图）

公式（无压缩原始像素）：

- RGB565：`宽 × 高 × 2`
- ARGB8888：`宽 × 高 × 4`
- PNG：视压缩而定，通常远小于原始像素

| 尺寸 | RGB565 | ARGB8888 |
|------|--------|----------|
| 25×25 | ≈ 1.25 KB | ≈ 2.5 KB |
| 30×30 | ≈ 1.8 KB | ≈ 3.6 KB |
| 60×60 | ≈ 7.2 KB | ≈ 14.4 KB |
| 70×70 | ≈ 9.8 KB | ≈ 19.6 KB |

本仓库 LittleFS 分区约 **1.5 MB**（`default_8MB.csv` 的 spiffs）。设备图标全套 ARGB 约数百 KB 量级，空间一般够用；若同时保留 PNG + ARGB，体积会叠加上去。

**RAM 策略：**

- **少量固定图标**（如空调 8 张）：可常驻缓存。
- **大量设备图标**：用暂存缓冲按需从 Flash 读（不全量常驻），避免上百 KB～数 MB RAM。

---

## 5. 本项目现状（实现要点）

| 资源 | 格式优先 | 绘制入口 |
|------|----------|----------|
| 空调模式 `/icon/ir/` | `.argb8888`，回退 PNG | `src/app_ir.cpp`（可带槽位缓存） |
| 设备图标 `/icon/device/` | `.argb8888`，回退 PNG | `src/app_device_icons.cpp`（scratch 按需加载） |
| Logo `/logo_60` | `.argb8888`，回退 PNG | `drawAppLogo60` |
| Icons 应用中的设备图 | 同上（走 `drawDevicePngNative`） | `src/app_icon_demo.cpp` |
| 箭头 / WiFi / 电池等 | 矢量代码绘制 | `src/app_icons.cpp`（无图片文件） |

转换脚本：

```bash
python scripts/png_to_rgba8888.py data/icon/ir
python scripts/png_to_rgba8888.py data/icon/device data/logo_60.png data/logo_50.png
```

RGB565 转换脚本 `scripts/png_to_rgb565.py`：

- 用 `Image.alpha_composite` 叠到黑底（保留抗锯齿）
- 默认 **Floyd–Steinberg 抖动** 再量化到 565（避免直接 `>>` 截断发脏）
- `--no-dither` 可关掉抖动

若仍与屏上 PNG 有差：最齐的做法是设备上 `drawPngFile` → `readRect` 导出像素（与 M5GFX 解码完全一致）。

网页配置预览仍使用 **PNG**（浏览器友好）；固件绘制优先原始像素文件。

---

## 6. 选型建议

1. **黑底 UI、要极致刷新**：RGB565 预生成 + `pushImage`（必要时 RAM 缓存当前页）。
2. **要透明/抗锯齿、底色可能不纯黑**：ARGB8888 + `pushAlphaImage`。
3. **图很少、想少维护资源**：PNG + 首次解码缓存即可。
4. **列表里很多张大图**：优先减小尺寸（如 `_25w`）+ 原始像素；再考虑页级缓存，而不是全库常驻。

---

## 7. 相关 API（M5GFX / LovyanGFX）

| API | 用途 |
|-----|------|
| `drawPngFile(FS, path, ...)` | 从文件系统解码 PNG |
| `pushImage(x, y, w, h, rgb565*)` | 推送 RGB565，无 Alpha |
| `pushAlphaImage(x, y, w, h, argb8888*)` | 推送带 Alpha 的 32bpp 并混合 |
| `readRect(...)` | 读回屏上像素，便于「解码一次再缓存」 |

## 8. 如何得到「与 M5GFX 屏上一致」的 RGB565

PC 脚本（Pillow）无法复刻 LovyanGFX/pngle 的解码与混合，所以手转 565 容易发脏。

**正确做法：在设备上用 M5GFX 解码，再导出像素。**

```
drawPngFile（库解码 + 透明混合到黑底）
  → readRect（读回屏上 RGB565）
  → 写入 .rgb565
```

### 操作

1. 烧录含烘焙功能的固件，并 `uploadfs`（需有 PNG 源文件）。
2. 打开 **Icons** 应用，按 **`b`** 烘焙（左上角会闪一下解码过程）。
3. 或 Config 网页在线时：`POST http://<设备IP>/bake-rgb565`
4. 拉回工程 `data/`：

```bash
python scripts/pull_rgb565_from_device.py http://<设备IP> --bake
```

5. 之后固件绘制顺序为：**`.rgb565` → `.argb8888` → `.png`**。

API：`bakePngToRgb565File` / `bakeAllPngIconsToRgb565`（`app_device_icons.h`）。
