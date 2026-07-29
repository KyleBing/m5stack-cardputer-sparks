# Options 选项

主菜单按键：`o`

本机设置：屏幕亮度、反色、提示音与音量、Time 默认模式、日历每周起始日、红外默认等。修改会写入 `config.json`（音量有防抖落盘）。

## 截图

**screen / sound / clock / infrared**

<div class="shot-row">

![options-screen](/shots/app_options_screen.png)
![options-sound](/shots/app_options_sound.png)
![options-clock](/shots/app_options_clock.png)
![options-ir](/shots/app_options_ir.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 切换模块 / 行 |
| ← → | 焦点在标签与数值间移动 |
| `-` `=` | 减小 / 增大当前值 |
| `Tab` | 在确认与数值等焦点间跳转 |

## 使用说明

常见项：

| 配置路径 | 含义 |
|----------|------|
| `screen.brightness` | 背光亮度 |
| `screen.invert` | 屏幕反色（立即生效） |
| `sound.time_key` | 时间页按键音 |
| `sound.mijia_on_off` | 米家开关提示音 |
| `sound.volume` | 喇叭音量 0–100 |
| `system.week_start` | 日历每周起始日：`sunday` / `monday` |
| `time.default` / `time.pure` | Time 默认模式 / Pure |
| `infrared.*` | 红外默认类别与品牌 |

也可用 [Config](./config) Web 修改同一套配置。
