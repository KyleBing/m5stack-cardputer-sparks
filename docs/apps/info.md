# Info 信息

主菜单按键：`i`

只读系统信息，共 6 页：Memory / Storage / Chip / Fw / Net / Run。

## 截图

**Memory / Storage**

<div class="shot-row">

![info-memory](/shots/app_info_memory.png)
![info-storage](/shots/app_info_storage.png)

</div>

**Chip / Firmware / Network / Runtime**

<div class="shot-row">

![info-chip](/shots/app_info_chip.png)
![info-fw](/shots/app_info_firmware.png)
![info-net](/shots/app_info_network.png)
![info-run](/shots/app_info_runtime.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| `[` `]` | 上一页 / 下一页 |
| `c`（仅 Storage） | 清除全部截图（再按 `y` 确认 / `n` 取消） |
| 方向键等翻页键 | 翻页 |

底栏显示 `N/6` 页码；Storage 页另有 `c clear`。

## 使用说明

| 页 | 内容 |
|----|------|
| Memory | Heap / PSRAM / Sketch / LittleFS 用量与进度条 |
| Storage | 本地 Flash（LittleFS）与 TF 卡已用 / 剩余；截图张数；`c` 清除截图；无 TF 时显示 n/a |
| Chip | 芯片型号、特性 |
| Fw | 固件版本、编译时间等 |
| Net | WiFi / IP / RSSI 等 |
| Run | 运行时长等相关 |

排查内存或配网问题时优先看 Memory 与 Net。字段含义、分配机制与常见不足场景见 [内存说明](/dev/memory)。
