## 刷机说明

### 下载固件

| 文件 | 用途 |
|------|------|
| `sparks-${TAG}-merged.bin` | 整片烧录（推荐首次刷机 / M5Burner），地址 `0x0` |
| `sparks-${TAG}-firmware.bin` | 仅更新程序，地址 `0x10000` |
| `sparks-${TAG}-littlefs.bin` | 仅更新资源（图标等），地址 `0x340000` |

### 刷写工具

安装 **esptool**：

```bash
pip install -U esptool
```

也可使用 [M5Burner](https://docs.m5stack.com/en/download) 图形界面烧录 `*-merged.bin`（地址填 `0x0`）。

### 连接设备

USB 连接 Cardputer-ADV，确认串口后替换下方命令中的 `PORT`：

- Windows：`COM3` 等（设备管理器查看）
- macOS：`/dev/cu.usbmodem*`
- Linux：`/dev/ttyACM0` 或 `/dev/ttyUSB0`

### 刷写指令

**首次刷机 / 完全重置**（整片烧录，会格式化存储区）：

```bash
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \
  --flash_mode qio --flash_freq 80m --flash_size 8MB \
  0x0 sparks-${TAG}-merged.bin
```

**常规升级**（保留 WiFi、配置等用户数据，仅更新程序）：

```bash
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \
  --flash_mode qio --flash_freq 80m --flash_size 8MB \
  0x10000 sparks-${TAG}-firmware.bin
```

**仅更新资源**（LittleFS，一般无需单独操作）：

```bash
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \
  --flash_mode qio --flash_freq 80m --flash_size 8MB \
  0x340000 sparks-${TAG}-littlefs.bin
```

### PlatformIO 本地编译

```bash
pio run -e m5stack-cardputer -t upload      # 上传固件
pio run -e m5stack-cardputer -t uploadfs    # 上传 LittleFS
```

### 常见问题

- 如未识别设备，请确认已安装 USB 驱动（详见设备说明书）。
- 刷写失败或设备启动异常，可按住设备按键复位后重试。
- 建议升级前备份重要配置；完全重置请使用 `*-merged.bin`。
