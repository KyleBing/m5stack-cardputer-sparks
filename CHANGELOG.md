# CHANGELOG

本文件记录 M5Stack Cardputer 固件项目的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，按日期从新到旧排列。

---

## 2026-07-21

### 新增

- **喇叭音量**：`sound.volume`（0~100，默认 25）；Options → Sound → `volume`；Config Web 可调；Mic 列表播放时 `-=` 实时调节
- **Mic 录音列表**（`l`）：扫描 TF `/audioRecord`；选中播放 / 停止 / Backspace 删除；播放中只刷进度行
- **Infrared AC 模式图标**：制冷 / 制热 / 除湿 / 送风（含 active）
- **图标 RGB565 烘焙**：设备 / IR / Logo 预生成 `.rgb565`；绘制优先 bake 文件，缺失回退 PNG；Config `POST /bake-rgb565` 与 Icons `b` 可现场烘焙；`scripts/pull_rgb565_from_device.py` 拉取到 `data/`

### 改进

- **喇叭嗡嗡声**：开机与空闲时 `releaseSpeakerQuiet`（卸 I2S + `gpio_reset` 拉低并 hold）；提示音播完自动静音；列表播过后不再 `Mic.begin`（避免 PDM 时钟灌进功放 LRCLK）；退出列表整页重绘 Record
- **Mic**：列表模式关麦；播完保持喇叭脚拉低；回示波器若本会话播过音则显示 `mic paused`，按 `R` 再开麦
- **截图 / Config Web**：截图与文件相关页面体验继续完善
- **M5Burner 打包**：`version` / `author` / 描述中的版本信息统一取自 `include/app_version.h`，发版只改该头文件
- **IR 模式图标**：进入 App 时预缓存全部 `.rgb565`；切模式直接 `pushImage` 覆盖，去掉先清黑底造成的闪烁

---

## 2026-07-20

### 新增

- **截图**：任意界面 `Fn+s` 将当前屏存为 LittleFS `/shot/app_<界面>_NNN.bmp`；空间不足时自动删最旧一张腾地方
- **Config `/shots`**：缩略图预览、下载；显示 LittleFS 总容量 / 已占用 / 剩余与截图占用；支持一键清空
- **截图开机恢复**：上次启动崩溃则删最后一张；Flash 过紧时继续删到可用，避免截图撑满起不来
- **GitHub Release**：推送 `v*` tag 时自动编译并发布 `firmware` / `littlefs` / 含 FS 的 `merged` 全镜像（`.github/workflows/release.yml`）
- **Mijia 快捷键**：设备可配置 `hotkey`（a-z/0-9，`q` 保留）；`Q` 快速选择页、`Fn+Q` 编辑当前设备快捷键（冲突时 BtnA 确认替换）；列表/宫格名称旁显示彩色快捷键字母；Web 配网设备表增加快捷键列并去重
- **诊断日志**：Cursor HTTPS 失败写入 LittleFS `/cursor.log`（HTTP 错误码、heap、RSSI、max_alloc）；Config Web `/cursor-log` 查看；主菜单 `Fn+i` 打开 Log App 翻页浏览
- **错误轨 `/cursor.err`**：fail / lowmem / 负 HTTP 码与开机 `boot reset=...` 单独落盘；重启后仍可查；Log 默认 Err（`f` 切完整 log）；Config Web `/cursor-err`
- **Cursor 请求韧性**：连 WiFi 后预解析 DNS；传输层负错误自动重试；WiFi 超时放宽，减轻偶发 `auth -1/conn`

### 改进

- **截图清空**：删前关闭文件句柄，修复 Web「清空全部」删除 0 张的问题
- **菜单**：BMI App 显示名改为 **IMU**（快捷键仍为 `g`）
- **WiFi STA 生命周期**：统一到 `app_connectivity`（`ensureStaWifi` / `releaseStaWifi` / `forceShutdownStaWifi`）；用完立刻 `disconnect` + `WIFI_OFF`；同 SSID 已连则复用，避免无谓硬重启造成堆碎片
- **Cursor WiFi**：去掉用户操作后 1 分钟宽限保持；拉取结束与离开 App 一律立刻关射频；连网不再先 `WIFI_OFF`，仅错 SSID 时断开
- **Cursor 低内存防护**：HTTPS / 建 task 前检查 free heap 与 max_alloc；不足时跳过并提示 `auth lowmem`，避免误报 `auth -1/conn`；周期刷新已有 `user_id` 时跳过 `/api/auth/me`
- **Cursor 日志**：`/cursor.log` 超限改为保留尾部，不再整文件清空；Help 注明 `auth -1` 可能由低内存/碎片引起
- **Config**：堆过低时跳过 softAP，降低 Cursor 失败后再开配网导致重启的风险
- **Mijia**：离开 App 时立刻释放 WiFi
- **M5Burner 打包**：LittleFS 固定使用 `config.example.json`，不把本地 `data/config.json`（密钥等）打进发布包；打包结束后恢复本地配置；忽略 `data/config.json.packbak`

---

## 2026-07-19

### 新增

- **M5Burner 发布**：`m5burner/m5burner.json` 元信息；`scripts/pack_m5burner.sh` 一键编译并生成 M5Burner zip 与 `cardputer_merged.bin`（产物输出到 `dist/`）

### 改进

- **HID Keyboard**：底栏 tip 改为两排（`Fn+u`/`Fn+b` 切模式，`BtnA`/`Fn+p` 退出与配对）；标题统一为 `KB `
- **Infrared Help**：分栏标题改为蓝底黑字；栏名改为 `keymap` / `manual`
- **Config**：菜单与标题由 Config Setup 改为 Config；Ready 时 header 显示 AP / LAN；AP 模式内容区精简
- **Options Info**：翻页由 `-=` 改为 `[]`，底栏提示同步

---

## 2026-07-18

### 新增

- **Infrared 配置**：Options 与 Web 配置页可设置红外默认 TV / AC 功能块及电视、空调品牌；进入 Infrared 时自动应用，并写入 `Infrared` 配置对象
- **Battery 校时**：无有效时钟时在电池页后台连接 WiFi/NTP，不阻塞实时电量显示；校时完成后自动记录当前采样并显示历史图

### 改进

- **Settings**：应用更名为 **Options**；左侧栏加宽，`ir` 改为 `infrared`；Time 默认模块改用 Uptime / Clock / Countdown / Stopwatch 全称
- **Mijia Help**：单设备帮助改为 common / navigation / special 三栏；按设备类型展开完整键位，栏标题统一为蓝底黑字
- **Cursor last**：`[]` 翻记录提示从底栏移至页码右侧
- **Mic**：右侧能量条由线性振幅改为 -60～0 dBFS 对数比例，正常说话时更易观察
- **RGB LED**：显示并支持 `-` / `=` 调节共用背光亮度，退出后恢复原亮度
- **Morse**：放大当前字母和点划图案，频率信息改为紧凑小字
- **Icons**：Help 提示移至左下角

---

## 2026-07-17

### 新增

- **HID Keyboard**（`k`）：USB / BLE 双模 HID 键盘（`app_hid_kb`）；默认 BLE（`Cardputer KB`）不占烧录口；`Fn+u/b` 切 USB/BLE，`Fn+p` 重新配对；侧边 **BtnA** 退出
- **HID Keyboard** Fn 层：橙色功能键（Esc / Del / 方向 / F1–F12）；`Fn+Ctrl/Opt/Alt` → 右侧修饰键；`Opt` → Win/Cmd；`Fn+Aa` → Caps Lock；`Fn+h` 打开双页 `keymap / manual` 帮助
- **Battery**（`p`）：独立电量页（`app_battery`）；实时电量 / 电压（可读时显示电流与 VBUS）；近 24h 柱状图；LittleFS 整点采样（最多 7 天）；深睡/浅睡缺口线性补全
- **Settings Info**：原 Power 页迁出，改由 Battery App 承担
- **Cursor last**：最近 10 条请求（切页再拉）；每页 1 条（日期 / 大时间 / 模型 / token）；`[]` 翻记录；`Inc` 绿徽章
- **api/cursor/**：周期用量 / On-Demand / 事件列表请求响应模板与字段→UI 对照

### 改进

- **USB**：`ARDUINO_USB_MODE=0` 且 `CDC_ON_BOOT=0`；HID USB 模式进出时 OTG ↔ Serial/JTAG 切换，退出后可继续 `pio upload`
- **菜单**：删除 Speaker 与旧 Key 演示；HID Keyboard 从 `h` 移至 `k`，释放 `h`；RGB LED 入口改为 `l`；主页翻页改为局部刷新（内容区 + 分页圆点），避免整屏擦黑扫过电池时闪竖线
- **Mijia** 编组：成员全是灯时可组亮度（`-=` / `0`–`9`）；底栏开关/亮度进度局部刷新，不整页闪
- **Cursor**：图表分页改为流式解析（`pageSize=200`），降低 OOM / 空响应对失败；柱体外框更暗；日聚合按索引累加
- **Cursor**：底栏改为 On-Demand（`ond $used/$limit`）；reset 显示 `Nd | MM-DD`；字母快捷键 `s/u/d/w/m` 与方向键切页并存；空闲 1s 慢循环时内容区右上角 3×3 蓝点；`r` 软刷新 last
- **Battery**：底栏图例增加绿色 `now`（当前小时柱）
- **Infrared**：TV / AC 首行品牌与状态改为二倍字体
- **Morse**：频率行改为二倍字体
- **底栏 tip**：徽章后说明文字下移 1px，与徽章视觉对齐；`drawHelpHintRight` 支持 `y_offset`
- **Mijia**：空气净化器 / 榨汁机 active 图标 PNG 微调
- **Web 配网**：编组文案改为「米家设备编组」；Cursor 配置只保留 `token`，去掉旧 `api_key` 兼容读写
- **Help UI**：Config Setup、Time、Cursor、WiFi、HID Keyboard、RGB LED、Mic、Icons、内部/外部 I²C 与 Mijia 详情/宫格/编组统一为蓝色标题的 `keymap / manual` 双栏布局
- **Help 文案**：Time 说明倒计时/秒表在设备保持唤醒时后台运行；LED 说明与屏幕共用电源及高亮度要求；Icons 说明固件图标资源用途；WiFi 提示不可用时按 `c` 扫描切换网络

---

## 2026-07-16

### 新增

- **Mic**：独立模块（`app_mic`）；示波器折线波形 + 分段 VU + 手动增益（`-`/`=`）；`r` / **BtnA** 开始/停止录音；有 SD 时写入 `/audioRecord/*.wav`（16 kHz 单声道）；进 App 后台 WiFi/NTP 校时（状态行 `WiFi` 标识）；无卡提示 `no SD`；`h` Help
- **Cursor**：当天 **24h** 小时柱状图（只拉当天事件）；翻页顺序 usage → 24h → 7d → 30d

### 改进

- **Mic**：离屏双缓冲减少网格闪烁；状态行展示 LIVE/REC、时长、电平、增益
- **Time / Mijia / Sleep**：Header 改为主标题 + 次要色模式后缀（如 `Time CD`、`Mijia Grid`、`Sleep Light`）
- **Mijia**：榨汁机设备图标 PNG 资源更新
- **Time Pure**：按 `p` 先切界面再写 `config.json`，避免 FS 保存拖慢进入/退出
- **Time Uptime**：改用 `esp_timer` 从上电起算（light sleep 期间不停表）；Settings Info 同步
- **Cursor**：摘要标签改为 First Party / API；图表分页 `pageSize=500`；Header 显示电池与 `24h`/`7d`/`30d` 副标题；底栏 tip 去掉页名；24h 每 3 小时一个 label；无操作 5 分钟后主循环改为 1s 一拍

---

## 2026-07-15

### 改进

- **Settings**：亮度调节先改背光并刷新 UI，再写 `config.json`，按 `-=` 更跟手
- **Infrared**：Header 显示 `Infrared` + 青色 `TV`/`AC`（`t` 切换）；**Tab** 循环品牌；TV 键位改为 `P` 电源、`-=` 音量、`[]` 频道、`m`/`i` 静音/输入；Send 支持空格 / Enter / **BtnA**；Help 合并为单页双列（keys / notes）
- **Infrared**：去掉上下键字段导航与垫子选中态；TV 音量/频道按键布局改为减在左、增在右（对齐物理键）
- **Cursor**：摘要页进度条左右 padding；用量数值用标签蓝；拉取 WiFi 可按 `gen` 取消，离开后不再卡死连网
- **Mijia**：设备图标 PNG 资源更新（含 active / 25w）
- **Mijia** 灯：关着调节亮度 / 色温 / 色相时先开灯再设值，调节立即生效
- **Mijia** 风扇 / 净化器：调节风速或模式不推断电源；P5 风量支持 `0`–`9`（同灯亮度：`1`→10%…`0`→100%）

---

## 2026-07-14

### 新增

- **Infrared**：TV 改为遥控器垫 UI（对齐 AC）；顶部 **TV / AC** tab 切换（`t`）
- **Time**：秒表 / 倒计时可后台运行（按 `millis` 记起止点，本次上电有效）；倒计时到点强制切入 CD 界面并响铃
- **Settings**：**clock** 面板 — `default` / `tz`（常用时区预设）/ `pure`；`time.pure` 写入 `config.json`；Time 内 `p` 同步保存
- **Web 配网**：系统页可配置 Time 默认模块（`time.default`）
- **Cursor**：用量图表分页拉取改走 FreeRTOS 后台 task，主循环可取消；拉取中可复用 WiFi 会话

### 改进

- **Mijia** 温湿度：湿度宫格/控制页显示一位小数；温度/湿度数值改白字并对齐占位

---

## 2026-07-13

### 新增

- **Mijia** 设备编组（`device_groups`）：Web 配网「编组」页维护；设备端 `d` 进入 Groups，批量开关成员；BtnA 在编组内同步切换
- **Mijia** 温湿度设备图标 `sensor_ht`（含 active / 25w）；Icons 应用可预览；model 含子串时自动匹配
- **Settings**：Sound 增加米家开/关提示音（`sound.mijia_on_off`，`m` 切换）
- **Mijia BLE**：被动扫描 MiBeacon / 青萍广播；温湿度计（温度 / 湿度 / 电量）与人体 / 无线开关等事件设备；`r` 短扫刷新，主循环非阻塞 poll
- **配置**：设备支持 `name_zh`、`ble.key`（bindkey）；Web 配网表增加 BLE Key；显示名优先中文；`timezone`（POSIX TZ，默认 `CST-8`）
- **Countdown** 电子闹钟：到点哔-哔-歇（最多 30s），结束页 `x` 取消并回到设置
- **Settings**：左右分栏（Screen / Sound）；Sound 可开关 Time 按键声（`time_key_sound`，倒计时闹钟不受影响）
- **Infrared** 红外应用（`x`）：板载 GPIO44 发射；TV（Samsung / Sony / LG / Panasonic / NEC）常用短码；空调（美的 / 格力 / 海尔 / 奥克斯 / 海信 / 小米）状态帧；`t` 切类型，方向键切品牌/字段，Enter 发送，`h` Help
- **Mijia** 空气炸锅（`careli.fryer.*`）MIoT 控制：查询状态 / 目标温与时长；`o`/`i`/`t`/BtnA 开始或取消烹饪；`-`/`=` 调温；`[`/`]` 调时
- **Mijia** 灯色相（HSV）：`bslamp2` / `color8` / `color2` 支持 `j`/`k` 调节，控制页显示彩虹进度条
- **Mijia** 控制页 / Grid：侧键 **BtnA** 切换当前设备开关（同 `t`）

### 改进

- **Settings** 亮度改为 0~100 百分比显示与配置（硬件仍映射 0~255）
- **时区**：启动 / 唤醒后 `applyLocalTimezone`；RTC 按 UTC 存储、本地显示；NTP 同步写入 `config.json` 时区
- **Countdown**：修复全量重绘盖住左下角 `RUN`/`PAUSED`；去掉结束态 `Time's up!`；开始 / 暂停 / 重置音效对齐秒表
- **Mijia BLE**：前台后台多设备监听 + 读数缓存；`r` 聚焦扫脏包可继续听；温湿度控制页 KV 布局与 `Xs ago` / listening 状态；开关提示音
- **Mijia**：概览回车回控制页；离开米家停 BLE；回菜单调用 `leaveMijiaApp`
- **BLE / WiFi 共存**：Central-only 初始化与扫描会话互斥；按 BLE 状态配置 WiFi modem sleep，避免 ESP-IDF coexist abort
- **音频**：I2S/功放冷启动预热；统一 `playUiTone`；Time 按键声走 `playTimeKeyTone`；Morse 默认频率改为 1000Hz
- **Mijia** 宫格 / 控制页展示 BLE 温湿度与事件状态；BLE 设备不走后台 miIO 查询队列
- **Help**：各应用底栏 `h help` 统一右下角（`drawHelpHintRight`）；IR Help 标题 2x；IR 主界面 `type` 2x
- **Mijia** 炸锅开锅流程：先写温时长再 `start-cook`，失败回退自定义烹饪（含 `recipe-name`）；开关后回读状态，待机不算“开”，未进入烹饪时提示 `need wake?`
- **Mijia** Help：内容靠上排列；能排开用 2x，否则 1x；风扇 / 炸锅固定 1x
- **Mijia** 控制页布局：图标左右留白、信息区上边距；风扇 / 彩灯进度条改为紧凑 1x；宫格状态标签缩短为 ≤3 字符
- **Mijia** 返回提示文案改为 **ESC**（保留 header 返回箭头图标）
- **Mijia** 设备状态查询超时由 1s 放宽至 2s，减少炸锅等 MIoT 设备误判离线

---

## 2026-07-11

### 新增

- **Mijia** 灯色温调节：`[` `]` 步进 100K，进度条背景随当前冷暖度变色，白色填充标示档位
- **Mijia** 概览列表分页：`,` `.` 翻页、`1` `2` 快速选中当前页设备

### 改进

- **Mijia** 控制页设备标题贴内容区顶边；进度条去掉刻度线；左栏图标上移
- **Mijia** 设备 PNG 图标资源更新
- **BLE** 界面默认 Font0，仅扫描列表设备名使用 efontCN14 显示中文
- **Cursor** 图表加载显示倒计时（7 日约 11s、30 日约 30s）；加载中不再绘制空柱框，避免遮挡 header WiFi 图标
- **Settings** 亮度条与 Mijia 统一样式，按键说明改为 `drawKeyBadge` 徽章
- **Morse** 频率调节键改为 `-` / `=`（释放 `[` `]` 给 Mijia 色温）
- **Header** 状态图标局部刷新区域修正，WiFi 断开/连接后不再残影
- **BMI** X/Y 分列左右顶边，Z 靠右顶边，避免与参考圆重叠
- **Sleep** 浅睡/深睡提示贴内容区顶边
- 翻页键 `getMenuNavDelta` 移除 `[` `]`（保留方向键与 `;` `,` `.` `/`）

---

## 2026-07-10

### 新增

- **Morse** 应用（`f`）：按键发送摩斯码，图形化显示点划，`[` `]` 调节频率发声
- **Cursor** 三页视图：`[` `]` 切换用量摘要 / 7 日 / 30 日柱状图

### 改进

- **Countdown**：SETUP 阶段支持 `0-9` 快速数字输入；时间显示略偏上
- **Web 配网**：页面跟随系统 `prefers-color-scheme` 自适应亮/暗主题
- **Sleep**：休眠倒计时仅局部刷新秒数，不再整屏重绘
- **Cursor**：摘要页 1 分钟、图表页 10 分钟后台静默刷新（非首次不显示 loading）；柱状图左右边距 5px；底部 `drawKeyHintsRow` 操作说明
- **Cursor** 应用（`x`）：拉取 Cursor 用量摘要、Auto/API 进度条、7/30 日柱状图；配置项 `cursor.api_key`
- 应用模块化拆分：`app_rtc`、`app_icon_demo`、`app_cursor` 等从 `main.cpp` 独立

### 改进

- **Web 配网**：暗黑主题；设备列表图标黑底；保存成功页增加「返回主页」按钮
- **WiFi**：进入时自动用已保存配置连接并显示 IP；`r` 刷新重连、`c` 更换网络；密码/连接提示二倍字体
- **Mijia** 概览列表（`i`）：每页 2 台设备、图标缩放、循环切换；底栏 `drawKeyBadge` 按键说明
- **倒计时**：`;`/↑ 增加、`.`/↓ 减少、`,``/` 切换时/分/秒；`g` 开始/暂停/继续；按键徽章提示；时间区局部刷新（同秒表）
- **秒表**：`g` 开始/暂停、`r` 重置；RUN/PAUSED 状态与按键提示分离刷新；毫秒与时分秒按需重绘
- `config.example.json` 增加 `cursor.api_key` 示例字段

---

## 2026-07-08

### 新增

- **倒计时（Countdown）** 与 **秒表（Stopwatch）** 应用，加入主菜单
- Mijia 设备控制：**按住 H** 显示二倍字体操作帮助页，松开返回控制页
- Mijia 异步设备状态刷新，切换设备时不阻塞按键处理
- Web 配网非阻塞推进（`updateWebApp`），支持在设备端按键交互
- 共享翻页辅助函数 `getMenuNavDelta`（方向键 / `;` `,` `.` `/` `[` `]`）

### 改进

- Mijia 仅在无法连接/读取状态时，于 ON/OFF 后显示状态文字
- 修复 Mijia 按键无响应问题；控制页支持方向键切换设备
- Mijia 设备列表概览（`i` 键），支持分页滚动
- Mijia 设备配置上限由 8 提升至 50
- Icons 展示页：移除底部横线与图标边框，翻页说明简化为 `[` `]` 
- BMI 左栏十字坐标增加浅色同心参考圆
- Sleep 休眠倒计时说明改为二倍字体
- Web 配网页面与设备管理体验优化

### 开发

- 添加 VS Code / Cursor PlatformIO 扩展推荐（`.vscode/extensions.json`）

---

## 2026-07-07

### 新增

- **BLE** 应用：扫描、开关、信息页与按键徽章式操作说明
- **Icons** 图标展示应用，集中预览系统与 Mijia 图标
- 局域网 Web 配网（已连 WiFi 时用 STA IP，否则开 AP）
- 关屏前 **5 秒倒计时** 提示（Sleep）
- Mijia / 系统图标重绘：灯泡、四叶风扇、空气炸锅、CPU、电池等，适配小屏可读性

### 改进

- **Info** 与 **Mijia** 界面重设计（图标、标签、分页、按键提示）
- WiFi 按需连接：离开应用后断开，降低待机功耗
- **Time** 应用 NTP 同步与连接状态显示修复
- WiFi 列表全宽行、右对齐信号条、1–4 选择、二倍字体密码输入
- Mijia / Web 拆分为独立模块（`app_mijia`、`app_web` 等）
- 充电时电池图标：绿色强调、更大闪电符号、位置调整
- Keyboard 应用：末次按键持久显示与自适应居中字号
- 共享 UI 渲染抽取至 `app_common`（按键徽章、提示行、信息行等）

### 扩展

- Mijia 设备控制：亮度、风扇 P5（风速/摇头/模式）、通用风扇档位、净化器 F20
- WiFi 分页扫描与信号条 UI
- miIO 单次请求、2 秒超时、无重试，加快本地反馈

---

## 2026-07-06

### 新增

- 多应用启动器：字母键进入各硬件测试 / 工具应用
- 嵌入式矢量 Logo 与版本信息页
- 共享应用顶栏（`app_header`）与分页主菜单（无需 Fn 翻页）
- LittleFS 配置骨架：`/config.json` 加载 WiFi 与 Mijia 设备列表
- 示例配置 `config.example.json`
- Mijia 本地控制（yeelink `set_power`）与 AP 模式 Web 配网门户
- Settings 亮度条、分段电池指示（主菜单顶栏）
- 设计资源文件

### 改进

- 键盘演示扩展：显示控制、M5 API 参考文档
- 子界面标题使用菜单全名，统一 GO 返回提示
- Keyboard 修饰键显示简化
- 移除过时 PNG 转 Header 预构建脚本

### 初始

- PlatformIO 工程：M5Stack Cardputer 键盘演示，显示按键输入，按住 `a` 反色显示

---

[//]: # (Commits: HEAD → c3f0919)
