# Options 选项

主菜单按键：`o`

本机设置分为 screen、sound、clock、calendar、infrared：屏幕、声音、Time、日历与红外配置。修改会写入 `config.json`（音量有防抖落盘）。

采用两层（必要时三层）导航：模块列表 → 模块详情；时区 / 品牌等枚举再进选择页。

## 截图

**screen / sound / clock / infrared**

<div class="shot-row">

![options-screen](/shots/app_options_screen.png)
![options-sound](/shots/app_options_sound.png)
![options-clock](/shots/app_options_clock.png)
![options-ir](/shots/app_options_ir.png)

</div>

## 快捷键

### L1 模块列表

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 选择模块 |
| Enter / → | 进入模块详情 |

### L2 模块详情

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 选择设置行 |
| `-` `=` | 减小 / 增大当前值（开关直接翻转） |
| Enter | 开关：翻转；枚举项：进入选择页 |
| `` ` `` / ← | 返回模块列表 |

### L3 选择页（时区、品牌、default 等）

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 选择选项 |
| Enter | 确认并返回详情 |
| `` ` `` / ← | 取消并返回详情 |

L1 再按 `` ` `` 回主菜单。screen 详情内可用 `0`–`9` 快速设亮度。

## 使用说明

常见项：

| 配置路径 | 含义 |
|----------|------|
| `screen.brightness` | 背光亮度 |
| `screen.invert` | 屏幕反色（立即生效） |
| `sound.time_key` | 时间页按键音 |
| `sound.mijia_on_off` | 米家开关提示音 |
| `sound.volume` | 喇叭音量 0–100 |
| `time.default` / `time.timezone` | Time 默认模式 / POSIX 时区 |
| `calendar.week_start` | 日历每周起始日：`sunday` / `monday` |
| `infrared.*` | 红外默认类别与品牌 |

也可用 [Config](./config) Web 修改同一套配置。
