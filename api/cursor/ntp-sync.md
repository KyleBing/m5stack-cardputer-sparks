# NTP 校时（Cursor 拉取前）

> 非 HTTP JSON 接口。Cursor 在连上 WiFi 后、请求用量 API **之前**调用 `quickSyncTime()`。  
> 实现：`src/app_cursor.cpp` → `quickSyncTime()`  
> 同类逻辑：[`api/time/ntp-sync.md`](../time/ntp-sync.md)（Time App）

## 作用

事件 `timestamp`、图表按本地日/小时分桶、`billingCycleEnd` 剩余天数都依赖系统时钟。未校时（`time() <= 1700000000`）时 last / 图表会失败（如 `time sync`）。

## 调用方式（Arduino / ESP-IDF SNTP）

```cpp
configTzTime(getAppTimezone(), "ntp.aliyun.com", "pool.ntp.org");
// 轮询 time(nullptr) > 1700000000，最多约 4s（20 × 200ms）
```

| 项 | 值 |
|----|-----|
| 时区 | `getAppTimezone()`（`config.json` 的 `time.timezone`，缺省 `CST-8`） |
| 主 NTP | `ntp.aliyun.com` |
| 备 NTP | `pool.ntp.org` |
| 有效阈值 | Unix 秒 `> 1700000000`（约 2023-11 之后） |
| 超时 | Cursor：约 4s；Time App：更长（含 `time.windows.com`） |

## 与 Time App 的差异

详见 [../time/ntp-sync.md](../time/ntp-sync.md)。

| | Cursor `quickSyncTime` | Time `trySyncNtpTime` |
|--|------------------------|------------------------|
| 服务器 | aliyun + pool.ntp.org | + `time.windows.com` |
| 写 RTC | 否 | 有硬件 RTC 时写入 UTC |
| 写 config 时区 | 否 | 成功后 `saveAppConfigTimezone` |
| 触发 | 每次 fetch 前（已有 WiFi） | 首次 / 按 `r`；含连 WiFi |

## 前置条件

- WiFi 已连接（Cursor fetch task 里 `ensureCursorWifi` 成功后）
- 射频休眠策略已按项目 `applyWifiRadioSleepPolicy` 处理

## 返回

无 JSON。成功：系统 `time()` / `localtime_r` 可用；失败：后续依赖本地时钟的请求可能报 `time sync` 或日期异常。
