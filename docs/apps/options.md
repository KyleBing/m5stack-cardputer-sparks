# Options 选项

主菜单按键：`o`

本机设置分为 screen、sound、clock、calendar、infrared。界面为左侧分类栏 + 右侧字段区；时区 / 品牌等枚举再进选择页。修改写入 `config.json`（音量有防抖落盘）。Header 标题为 **Options**。

## 截图

**screen / sound / clock / infrared**

<div class="shot-row">

![options-screen](/shots/app_options_screen.png)
![options-sound](/shots/app_options_sound.png)
![options-clock](/shots/app_options_clock.png)
![options-ir](/shots/app_options_ir.png)

</div>

## 快捷键

### 侧栏（分类焦点）

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 选择分类 |
| Enter / → | 进入右侧字段编辑 |

### 字段区

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 选择设置行 |
| `-` `=` | 减小 / 增大当前值（开关直接翻转） |
| Enter | 开关：翻转；枚举项：进入选择页 |
| `` ` `` / ← | 返回侧栏 |

### 选择页（时区、品牌、default 等）

| 按键 | 作用 |
|------|------|
| ↑ ↓ | 选择选项 |
| Enter | 确认并返回字段区 |
| `` ` `` / ← | 取消并返回字段区 |

侧栏再按 `` ` `` 回主菜单。screen 字段内可用 `0`–`9` 快速设亮度。

## 使用说明

常见项：

| 配置路径 | 含义 |
|----------|------|
| `screen.brightness` | 背光亮度 |
| `screen.invert` | 屏幕反色（立即生效） |
| `sound.time_key` | 时间页按键音 |
| `sound.mijia_on_off` | 米家开关提示音 |
| `sound.screenshot` | 截图提示音（`Fn+s`） |
| `sound.volume` | 喇叭音量 0–100 |
| `time.default` / `time.timezone` | Time 默认模式 / POSIX 时区 |
| `calendar.week_start` | 日历每周起始日：`sunday` / `monday` |
| `infrared.*` | 红外默认类别与品牌 |

也可用 [Config](./config) Web 修改同一套配置。
