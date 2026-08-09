# Time 时间

主菜单按键：`t`

四个子模式：**Uptime**、**Clock**、**Countdown**、**Stopwatch**。进入后始终全屏（无 header），切模式时左上角短暂显示模式名。

## 截图

**Uptime / Clock / Countdown / Stopwatch**

<div class="shot-row">

![time-uptime](/shots/app_time_up.png)
![time-clock](/shots/app_time_ntp.png)
![time-countdown](/shots/app_time_cd.png)
![time-stopwatch](/shots/app_time_sw.png)

</div>

**大号时钟 / Help / 倒计时结束**

<div class="shot-row">

![time-big](/shots/app_time_big.png)
![time-help](/shots/app_time_help.png)
![time-up](/shots/app_time_001.png)

</div>

## 快捷键

### 模式切换（Help 汇总）

| 按键 | 作用 |
|------|------|
| `u` | Uptime 运行时长 |
| `t` | Clock 时钟 |
| `c` | Countdown 倒计时 |
| `s` | Stopwatch 秒表 |
| `r` | 同步时间 / 重置（视模式） |
| **BtnGO** | 开始 / 暂停 / 继续 |
| `h` | Help |

### Uptime

| 按键 | 作用 |
|------|------|
| `h` | Help |

### Clock

| 按键 | 作用 |
|------|------|
| `r` | NTP 同步（需 WiFi） |
| `b` | 切换大号点阵时钟（仅 HH:MM） |
| `h` | Help |

### Countdown · 设置 SETUP

| 按键 | 作用 |
|------|------|
| 方向键 | 调节时分秒字段 |
| `0`–`9` | 数字输入 |
| **BtnGO** | 开始 |
| `h` | Help |

### Countdown · 运行 / 暂停

| 按键 | 作用 |
|------|------|
| **BtnGO** | 暂停 / 继续 |
| `r` | 重置 |
| `h` | Help |

### Stopwatch

| 按键 | 作用 |
|------|------|
| **BtnGO** | 开始 / 暂停 / 继续 |
| `r` | 重置 |
| `h` | Help |

## 使用说明

1. 默认进入模式由配置 `time.default`（如 `up`）决定；时区见 `time.timezone`（如 `CST-8`）。
2. 时钟依赖 RTC；有网时可 `r` 做 NTP。
3. Clock 下 `b` 进入 Big Clock（仅时分点阵大字，无秒）。
4. **Countdown / Stopwatch** 可在离开 Time App 或切换到其它子模式后继续计时，详见下文。

## 省电（时钟展示）

Time **不会**自动关屏、调暗背光，也不会进入 ESP 浅睡 / 深睡。长时间展示时钟时的省电靠 **主循环降频**；真正休眠请用主菜单 [Sleep](./sleep)。

### 适用与不适用

| 场景 | 是否降频 |
|------|----------|
| **Uptime** / **Clock**（含 Big Clock） | 是：无操作满约 **1 分钟**后进入慢循环 |
| Help 打开时 | 否：保持较快轮询 |
| NTP 同步进行中 | 否：保持约 **30ms** 轮询，便于超时与重连 |
| **Countdown** / **Stopwatch** | 否：需更高刷新，不参与空闲降频 |

### 空闲慢循环（约 1s 一拍）

无按键 / 切模式等活动满 **60s** 后（`TIME_IDLE_SLOW_MS`）：

- 主循环 `delay` **对齐下一整秒**（约 1s 一拍），按键约在 1s 内响应。
- `updateRtcApp` 轮询从约 **30ms** 改为 **1000ms**。
- 普通 Clock / Uptime 界面仍按 **1s** 刷新秒位；秒显示不跳秒。

有操作时（Uptime / Clock）：主循环约 **30ms** 一拍，避免空转耗电，同时保持跟手。

任意 Time 内按键、切模式会重置空闲计时；打开 Help 时不进入慢循环。

### Big Clock 刷新

| 状态 | 重绘检查间隔 | 说明 |
|------|--------------|------|
| 有操作 | 约 **15s** | 仅 HH:MM，不必每秒重画 |
| 已进入空闲慢循环 | 约 **1s** | 整分切换更及时 |

### 与 Sleep 的关系

| 能力 | Time 时钟展示 | [Sleep](./sleep) |
|------|---------------|------------------|
| 降 CPU 轮询 | ✓ 空闲约 1s 一拍 | — |
| 关屏 / 背光 0 | ✗ | ✓ 进入睡眠时 |
| ESP light / deep sleep | ✗ | ✓ |

适合当桌面时钟长时间挂着：留在 Uptime 或 Clock（可用 `b` 大字），约 1 分钟后自动慢循环；要更深省电再进 Sleep。

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
