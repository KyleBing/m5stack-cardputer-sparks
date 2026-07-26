# Time 时间

主菜单按键：`t`

四个子模式：**Uptime**、**Clock**、**Countdown**、**Stopwatch**；支持 Pure 纯净显示（隐藏 tip / 多余 UI）。

## 截图

**Uptime / Clock / Countdown / Stopwatch**

<div class="shot-row">

![time-uptime](/shots/app_time_up.png)
![time-clock](/shots/app_time_ntp.png)
![time-countdown](/shots/app_time_cd.png)
![time-stopwatch](/shots/app_time_sw.png)

</div>

**Pure 纯净显示**

<div class="shot-row">

![time-uptime-pure](/shots/app_time_up_pure.png)
![time-clock-pure](/shots/app_time_ntp_pure.png)
![time-countdown-pure](/shots/app_time_cd_pure.png)
![time-stopwatch-pure](/shots/app_time_sw_pure.png)

</div>

## 快捷键

### 模式切换（Help 汇总）

| 按键 | 作用 |
|------|------|
| `u` | Uptime 运行时长 |
| `t` | Clock 时钟 |
| `c` | Countdown 倒计时 |
| `s` | Stopwatch 秒表 |
| `p` | Pure 纯净显示开关 |
| `r` | 同步时间 / 重置（视模式） |
| **BtnGO** | 开始 / 暂停 / 继续 |
| `h` | Help |

### Uptime

| 按键 | 作用 |
|------|------|
| `p` | Pure |
| `h` | Help |

### Clock

| 按键 | 作用 |
|------|------|
| `r` | NTP 同步（需 WiFi） |
| `p` | Pure |
| `b` | Pure 模式下切换大号默认字体时钟（仅显示时、分） |
| `h` | Help |

### Countdown · 设置 SETUP

| 按键 | 作用 |
|------|------|
| 方向键 | 调节时分秒字段 |
| `0`–`9` | 数字输入 |
| **BtnGO** | 开始 |
| `p` / `h` | Pure / Help |

### Countdown · 运行 / 暂停

| 按键 | 作用 |
|------|------|
| **BtnGO** | 暂停 / 继续 |
| `r` | 重置 |
| `p` / `h` | Pure / Help |

### Stopwatch

| 按键 | 作用 |
|------|------|
| **BtnGO** | 开始 / 暂停 / 继续 |
| `r` | 重置 |
| `p` / `h` | Pure / Help |

## 使用说明

1. 默认进入模式由配置 `time.default`（如 `up`）决定；`time.pure` 可默认开启 Pure。
2. 时钟依赖 RTC；有网时可 `r` 做 NTP，时区见配置 `timezone`（如 `CST-8`）。
3. Pure 适合把 Cardputer 当桌面时钟 / 秒表使用。
4. **Countdown / Stopwatch** 可在离开 Time App 或切换到其它子模式后继续计时，详见下文。

## 后台运行

Uptime 与 Clock 仅在 Time 前台刷新显示；**Countdown** 与 **Stopwatch** 的状态保存在内存中，切换子模式或返回主菜单 **不会清零**。

### Countdown 倒计时

| 场景 | 行为 |
|------|------|
| 运行中切到 Clock / Uptime 等 | 计时继续；回到 Countdown 显示剩余时间 |
| 运行中返回主菜单或其它 App | 计时继续；**主循环** `pollCountdownBackground` 检测到期 |
| 到期时不在 Countdown 页 | **自动切入** Time App 的 Countdown 结束页 |
| 到期响铃 | 哔-哔-歇循环，最长 **30s**；不在 CD 页也会响（音量受 `sound.volume` 影响） |
| 停止响铃 | 结束页按 `x` 取消并回到设置；或 `r` 重置 |

暂停（PAUSED）时保存剩余毫秒；继续时按 `millis()` 重算结束时刻。离开 App **不会** 调用 `leaveCountdownApp` 停表——只有到期、重置或取消闹钟才会结束。

### Stopwatch 秒表

| 场景 | 行为 |
|------|------|
| 运行中切子模式 / 回主菜单 | `swRunning` 与累计时长保留；基于 `millis()` 继续计 |
| 再次进入 Stopwatch | 显示正确已计时长（含离开期间） |
| 前台刷新 | 运行中约 **30ms** 刷新一次，显示到 **1ms** |
| 重置 | 双击音效的 `r` 清零 |

秒表无全局到期弹窗；离开期间不刷新屏幕，但时间仍在走。

### 子模式切换

在 Time App 内按 `u` / `t` / `c` / `s` 切换子模式时，Countdown 与 Stopwatch 的 **运行态均保留**（`enterCountdownApp` / `enterStopwatchApp` 只重绘，不重置 phase）。

### Pure 模式

Pure 隐藏 header / 底栏 tip，Clock 中可按 `b` 切换自动适配屏幕的最大默认字体时钟；Countdown 到期页仍保留 **取消提示**；后台计时与响铃逻辑与非 Pure 相同。
