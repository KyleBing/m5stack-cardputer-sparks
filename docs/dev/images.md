# 图片处理与 RGB565 烘焙

针对 Cardputer（ESP32-S3 + SPI RGB565 屏）固件中的图标资源：源文件用 PNG 维护，运行时优先用设备上烘焙出的 `.rgb565` 直推，避免每次 `drawPngFile` 解码。

> 下文相对耗时为经验量级，非精确 benchmark；同尺寸对比以 **RGB565 已在 RAM、`pushImage`** 为 **1×**。

## 为什么要这样做

面板最终都是 **RGB565**。若每次重绘都走 PNG，流程是：

```
LittleFS 读文件 → zlib 解压 → PNG filter 还原 → 颜色/透明处理 → 写屏
```

慢主要慢在 **CPU 解码**，不是几十像素本身。列表翻页、空调模式切换、米家宫格等高频重绘时，体感会明显卡顿。

预生成 `.rgb565` 后：

```
LittleFS 读原始像素 → pushImage（直接写屏）
```

无 zlib、无现场 Alpha 混合，Flash / SPI 读写也更轻。透明区在烘焙时已合成到黑底（本项目 UI 多为黑底），画质与「屏上用 M5GFX 解 PNG」一致。

**禁止**在电脑上用 Pillow 等离线转 565：解码/混色与 LovyanGFX/pngle 不一致，屏上容易发脏。必须在设备上用 M5GFX 解码再导出像素。

## 系统中的图片路径

| 资源 | 源文件（LittleFS） | 运行时优先 | 绘制入口 |
|------|-------------------|------------|----------|
| 设备图标 | `/icon/device/*.png`（含 `_active`、`_25w`） | `.rgb565` → PNG | `src/app_device_icons.cpp` |
| 空调模式 / 风速 / 信号 | `/icon/ir/*.png` | RAM 缓存 → `.rgb565` → PNG | `src/app_ir.cpp` |
| 品牌 logo | `/icon/brand/*.png` | `.rgb565` → PNG | `src/app_ir.cpp` |
| Logo | `/logo_60.png`、`/logo_50.png` | `.rgb565` → PNG | `drawAppLogo60` 等 |
| 箭头 / WiFi / 电池等 | 无图文件 | 矢量绘制 | `src/app_icons.cpp` |

网页 Config 预览仍用 **PNG**（浏览器友好）；固件绘制优先 bake 文件。

源图放在仓库 `data/`，经 `pio run -t uploadfs` 进设备 Flash。bake 产物可拉回 `data/` 一并打包，这样用户烧录后无需再现场烘焙。

## Bake 过程

### 设备端在做什么

```
fillRect 黑底
  → drawPngFile（M5GFX 解码 + 透明混合到黑底）
  → readRect（读回屏上 RGB565）
  → 写入同名 .rgb565
```

对应 API：`bakePngToRgb565File` / `bakeAllPngIconsToRgb565`（`include/app_device_icons.h`）。

批量烘焙覆盖：

- `/icon` 下所有子目录中的 `.png`
- `/logo_60.png`、`/logo_50.png`

### .rgb565 文件格式

8 字节头部 + 裸像素：

| 偏移 | 长度 | 内容 |
|------|------|------|
| 0 | 4 | magic `R565` |
| 4 | 2 | 宽（uint16 小端） |
| 6 | 2 | 高（uint16 小端） |
| 8 | w×h×2 | RGB565 像素，逐行 |

头部带宽高，非正方形图标（品牌 logo 66×20、风速 34×30 等）同样可以烘焙。读取端 `loadRgb565File` 会校验 magic 与宽高，不匹配时回退现场解 PNG。

触发方式：Config Web **`POST /bake-rgb565`**（返回 `{"ok":true,"baked":N}`）。Icons App 已不再提供按键 `b` 现场烘焙。

### 推荐工作流

前提：固件已烧录、LittleFS 里有 PNG、设备 WiFi 在线（Config 网页可用）。

```bash
# 1. 上传含 PNG 的 data/（若刚改图）
pio run -e m5stack-cardputer -t uploadfs

# 2. 设备上烘焙，再拉回 data/
python scripts/pull_rgb565_from_device.py <设备IP> --bake
```

只触发烘焙：

```bash
curl -X POST http://<IP>/bake-rgb565
```

之后把拉回的 `.rgb565` 提交进仓库，下次 `uploadfs` 即带上 bake 文件。

### 这样做的作用（小结）

1. **刷新更快**：高频 UI 走 `pushImage`，避免反复 zlib 解码。
2. **画质可靠**：像素来自设备上的 M5GFX，与真实上屏一致。
3. **可回退**：缺 bake 时仍解 PNG，开发期改图不必立刻 bake。
4. **资源可复用**：bake 一次写入 `data/`，所有用户烧录即享加速。

## 各绘制路径对比

同尺寸下，常见路径大致是：

**RGB565 直推 ≫ ARGB8888 Alpha 混合 ≫ 每次现场解 PNG**

| 优先级 | 做法 | 适用 |
|--------|------|------|
| 要最快 | 预生成 **RGB565**（透明烤进黑底）→ `pushImage` | 底色固定为黑的 UI 图标（本项目主路径） |
| 要透明叠任意底 | 预生成 **ARGB8888** → `pushAlphaImage` | 需要半透明/抗锯齿叠在多变底色上 |
| 要省 Flash、改图方便 | 保留 **PNG**，首次解码后 **RAM 缓存** | 数量少、可接受首次卡顿 |
| 不推荐反复 | 每次 `drawPngFile` | 列表翻页、按键闪烁等高频重绘 |

### PNG（`drawPngFile`）

- 有 Alpha 时由 M5GFX/LovyanGFX 做混合，画质通常最好、也最省 Flash。
- **每次**重绘都走全流程就会明显卡。

### 预生成 RGB565（`.rgb565`）

- **不支持透明通道**：透明区需提前合成到固定底色。
- 手写转换若处理 Alpha/抗锯齿不当，观感会差于库解码 PNG。

### 预生成 ARGB8888（`.argb8888`）

```
LittleFS 读原始像素 → pushAlphaImage（读底色 + 混合 + 写回）
```

- **跳过 zlib**，保留完整 Alpha。
- 字节序需与 `lgfx::argb8888_t` 一致：**B, G, R, A**。
- 比 RGB565 慢，主要因为 **逐像素 Alpha**，其次是 4 字节/像素。
- 本仓库当前固件主路径已改为 RGB565 bake；ARGB 可作为备选方案理解。

### PNG 解码一次 + RAM 缓存

```
首次：drawPngFile → readRect 存 RGB565
之后：pushImage
```

- 画质对齐库解码；重绘接近 RGB565 直推。
- 占 RAM：`宽 × 高 × 2 × 槽位数`（例如空调若干张 30×30 约十余 KB）。
- IR App 对模式图标采用进入时预缓存，切模式直接 `pushImage`，减少闪烁。

## 相对耗时（同分辨率）

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

## Flash / RAM 占用（同图）

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

本仓库 LittleFS 分区约 **1.5 MB**。设备图标全套原始像素约数百 KB 量级；若同时保留 PNG + bake，体积会叠加。

**RAM 策略：**

- **少量固定图标**（如空调模式）：可常驻缓存。
- **大量设备图标**：按需从 Flash 读，不全量常驻。

## 选型建议

1. **黑底 UI、要极致刷新**：RGB565 预生成 + `pushImage`（必要时 RAM 缓存当前页）——本项目默认。
2. **要透明/抗锯齿、底色可能不纯黑**：ARGB8888 + `pushAlphaImage`。
3. **图很少、想少维护资源**：PNG + 首次解码缓存即可。
4. **列表里很多张大图**：优先减小尺寸（如 `_25w`）+ bake；再考虑页级缓存，而不是全库常驻。

## 相关 API（M5GFX / LovyanGFX）

| API | 用途 |
|-----|------|
| `drawPngFile(FS, path, ...)` | 从文件系统解码 PNG |
| `pushImage(x, y, w, h, rgb565*)` | 推送 RGB565，无 Alpha |
| `pushAlphaImage(x, y, w, h, argb8888*)` | 推送带 Alpha 的 32bpp 并混合 |
| `readRect(...)` | 读回屏上像素，便于 bake / 解码缓存 |

## 相关入口

- Config：`POST /bake-rgb565`（见 [Config](/apps/config)）
- 脚本：`scripts/pull_rgb565_from_device.py`
- 实现：`src/app_device_icons.cpp`、`src/app_ir.cpp`、`src/app_web.cpp`
