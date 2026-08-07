# Cursor 用量

主菜单按键：`c`

通过配置中的 `cursor.token` 拉取 Cursor 用量：摘要、最近记录、日 / 周 / 月图表。

## 截图

**Summary / Latest**

<div class="shot-row">

![cursor-summary](/shots/app_cursor_summary.png)
![cursor-last](/shots/app_cursor_last.png)

</div>

**日 / 周 / 月图表**

<div class="shot-row">

![cursor-7d](/shots/app_cursor_7d.png)
![cursor-24d](/shots/app_cursor_24d.png)
![cursor-30d](/shots/app_cursor_30d.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| 方向键 · `;,.` `/` | 翻页 / 切视图 |
| `r` | 刷新数据 |
| `s` | 摘要 Summary |
| `u` | 最新 Latest |
| `d` / `w` / `m` | 日 / 周 / 月图表 |
| `[` `]` | 在记录间翻动 |
| **BtnGO** | 关屏（省电看图时） |
| `h` | Help |

## 使用说明

1. 在 `config.json` 或 Config Web 写入有效的 Cursor API token。
2. 需已连接 WiFi；首次请求会预解析 DNS，失败时自动重试。
3. Summary 看总量；`d`/`w`/`m` 看趋势；`u` 看最近几条。
4. 无网或 token 无效时界面会提示错误，检查 [WiFi](./wifi) 与配置后按 `r`。

## 更新机制

Cursor 与其它 App 不同：网络请求在 **FreeRTOS 后台 task** 中执行，主循环只负责按键与重绘，拉取过程中界面仍可响应操作；离开 App 或用户翻页/按 `r` 时会通过代数计数 **作废** 进行中的 task。

### 按需拉取

| 时机 | 拉取内容 |
|------|----------|
| 进入 App | NTP 校时 → `auth/me` → 周期用量（Summary） |
| 切到 `u` Latest | 最近 10 条 usage event（进入时不预拉） |
| 切到 `d` / `w` / `m` 图表 | 按页拉取事件并聚合（`pageSize=200`，流式解析） |

各视图数据 **分别缓存**；图表已有缓存时直接展示，同天数拉取进行中则显示倒计时 ETA。

### WiFi

- **用时才连**：仅在拉取前连 WiFi，结束后 **立刻断开**，不保持长连接。
- 拉取前预解析 DNS；TLS 偶发失败会自动重试。
- 内存不足（Heap / Max Alloc 低于门槛）时跳过 HTTPS 或建 task，界面提示 `auth lowmem`（不一定是 token 错）。详见 [内存说明](/dev/memory)。

### 空闲自动刷新

无按键操作后：

1. **满 1 分钟**：按当前页静默刷新一次（不显示 loading）。
2. **之后每 5 分钟**：重复静默刷新，直至再次按键（计时重置）。

| 当前页 | 静默刷新内容 |
|--------|--------------|
| Summary | 周期用量 |
| Latest | 最近记录列表 |
| 图表（已有缓存） | 重拉对应天数图表 |

静默刷新失败不打断当前展示；用户按 `r` 会触发带 loading 的主动刷新。

### 省电

- **无操作满 5 分钟**：主循环从 10ms 降为 **1s 一拍**；内容区右上角出现 **3×3 蓝点** 表示慢循环态。
- **BtnGO 关屏**：背光关闭，但后台仍按上述节奏刷新；任意键亮屏并用内存中最新数据重画。

### 手动刷新（`r`）

| 当前页 | 行为 |
|--------|------|
| Summary | 整页重拉（auth + 周期用量） |
| Latest | **软刷新**：保留旧列表，后台更新后再替换 |
| 图表 | 清除当前段缓存，带倒计时重新拉取 |

### 相关文档

- API 字段与端点：[api/cursor/README.md](https://github.com/KyleBing/m5stack-cardputer-sparks/blob/main/api/cursor/README.md)
- 诊断日志：`/cursor.log`、`/cursor.err`（Config Web 或 Log App 查看）
