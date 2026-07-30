# NTP 校时流程与约定（Time App）

> 请求/返回字段模板见 [ntp-request.json](./ntp-request.json)、[ntp-response.json](./ntp-response.json)。  
> 实现：`trySyncNtpTime` / `syncClockTimeIfNeeded`（`app_rtc.cpp`）

## 请求（SNTP）

- **UDP/123**，48 字节包，`mode = 3`（client）
- 主机（依次）：`ntp.aliyun.com` → `pool.ntp.org` → `time.windows.com`
- 发起：`configTzTime(getAppTimezone(), ...)`
- 时区字符串：POSIX TZ，如 `CST-8`（只影响本地显示，NTP 本身是 UTC）

应用层不组 JSON，无 HTTP body。

## 返回（固件消费）

协议栈把服务器 `transmitTimestamp` 转成系统 UTC 后：

1. `getLocalTime(&tm)` → 本地 `struct tm`
2. 可选写入硬件 RTC（**UTC**）
3. `saveAppConfigTimezone(tz)` → 写入 `config.json` 的 `time.timezone`
4. UI：`HH:MM:SS` + `YYYY-MM-DD`，顶部标签 `NTP` 或之后读时钟为 `RTC`

有效 epoch：`> 1600000000`。总超时：`RTC_SYNC_TIMEOUT_MS` = **10s**（含连 WiFi）。

## 触发

| 时机 | 行为 |
|------|------|
| 首次进 Clock 且无有效时间 | 自动 WiFi + NTP |
| 已同步过 | 跳过，用 RTC/系统时钟 |
| 按 `r` | `force=true`，再校一次 |

## 对照 Cursor

Cursor：`quickSyncTime`，仅 2 台服务器、~4s、不写 RTC。见 [../cursor/ntp-sync.md](../cursor/ntp-sync.md)。
