# WiFi

主菜单按键：`w`

进入后优先显示已保存档案列表（一页 **3** 条均分高度）；扫网列表一页 **4** 条。顶栏标题随页面变化：`WiFi Switcher` / `WiFi Scanner` / `WiFi Password` / `WiFi Connect` / `WiFi Failed`。可在档案间切换、扫描附近热点并输入密码连接。配置支持最多 **5** 条 `wifis[]`，由 `wifi_active` 指定当前档案。

## 截图

**已保存档案**

<div class="shot-row">

![wifi-saved](/shots/app_wifi_saved.png)

</div>

**扫网**

<div class="shot-row">

![wifi-scanner](/shots/app_wifi_scanner.png)

</div>

**输入密码**

<div class="shot-row">

![wifi-password](/shots/app_wifi_input_password.png)

</div>

## 快捷键

| 状态 | 按键 | 作用 |
|------|------|------|
| 已保存列表 | `;` `,` `.` `/` / 方向键 | 上下移动选中项（跨页循环） |
| 已保存列表 | Enter | 连接选中档案 |
| 已保存列表 | Backspace | 删除选中档案 |
| 已保存列表 | `1`–`3` | 选当前页档案并连接 |
| 已保存列表 | `[` / `]` | 整页翻页 |
| 已保存列表 | `r` | 重连当前 active |
| 已保存列表 | `s` | 进入扫网 |
| 扫网列表 | `1`–`4` · `,` / `.` | 选热点 / 翻页 |
| 扫网列表 | `w` / Esc | 回到已保存列表 |
| 输入密码 | `Fn`+`Q` | 回到扫网列表 |
| 输入密码 | 字母数字 · Enter | 连接 |
| 输入密码 | Del / Backspace | 删字 |
| 连接中 | `1`–`3` | 改连其它档案 |
| 连接中 | `r` / `c` | 重试 / 取消 |
| 失败页 | `r` | 用原密码重试 |
| 失败页 | `p` | 回密码页改密码 |
| 失败页 | Enter / `w` / Esc | 回扫网列表 |
| 任意 | `h` | Help |

## 使用说明

1. **进入**：直接打开已保存列表；光标默认落在当前 active 档案上（黄框）。若有 active 且未连上，对应项旁显示橙色小字 `connecting`，连上后右侧显示信号图标，下方显示 IP。
2. **切档案**：上下键移动光标，Enter 设为 active 并连接；也可用 `1`–`3` 直接选当前页某项。
   **删档案**：Backspace 删除光标所在档案；若删的正是当前连着的网络会同时断开。
3. **Saved ↔ Scan**：`s` 进入扫网；扫网页 `w` / Esc 回到已保存列表。SSID 后的 `*` 表示该热点需要密码。
4. **Scan → Password**：选中热点后输入密码，Enter 连接；成功后写入配置并回到已保存列表（与 Config Web `/wifi` 同源）。
5. **连接页**：显示目标热点卡片、来回滑动的进度条、剩余秒数（最长 **10s**）以及信号 / 加密 / 信道详情。
6. **失败页**：认证失败、找不到 AP 或超时会停在失败页，红框内提示原因（如 `wrong password?`），**不会自动退出**——按 Enter（或 `w` / Esc）回扫网列表，`r` 重试，`p` 回密码页改密码。手动连接失败不会再回到 switcher，也不会把 active 档案标成 `timeout`。
7. 其它 App 只使用当前 **active** 档案，不会自动轮询多 SSID。

旧版顶层 `"wifi":{ssid,password}` 启动时会自动迁移为 `wifis[]`。
