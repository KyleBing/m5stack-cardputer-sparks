# CHANGELOG

本文件记录 M5Stack Cardputer 固件项目的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，按日期从新到旧排列。

---

## 2026-08-13

### 新增

- **Radio**（`r`）：Grove FM 收音机，**TEA5767** 与 **RDA5807M** 共用一套 UI；电台列表 / 自动搜台（`S`）/ 长按连续调谐；RDA 支持音量、Tuner 设置与 RDS
- **Vocab**（`l`）：英语单词学习；LittleFS `/vocabulary` 多词库；顺序 / 随机未会词；空格 / 回车标记已会并自动保存
- **Vocab 已会概览**（`i`）：按词库顺序把每个词画成格子，绿色=已会，黄点=当前词
- **词库**：内置四级乱序词汇表 `data/vocabulary/3 四级-乱序.txt`

### 改进

- **Font**：去掉 Header / 底栏 tip；左上角 Font2 显示序号与名称；右上角红框标出单字占用，框下 5px 显示宽×高；正文为数字 + pangram；上下滚动、左右 / `[ ]` 翻字体；Time 风格 Help；列表只保留实测可用字体
- **Radio / Vocab Help**：Time 风格全屏 Help；ESC 先关 Help / 电台列表 / 搜台，不直接退出 App
- **I2C**：InI2 / ExI2 展示 Grove / EXT 引脚图（彩色焊盘 + 白字）；扫描列表按芯片名实际宽度排 role，避免 `RDA5807M` / `TEA5767` 等长名被截断；ExI2C 补收音机芯片提示
- **Vocab UI**：主页不显示词库名；`line` / `known` 与进度条改到底栏；主单词 FreeMono 12；释义按宽度折行（行距 +2px）；`i` 概览顶栏 known 左侧、词库名贴右；未标识格子改暗灰
- **Vocab 操作**：`Tab` 等同 `r` 随机未会词；标记已会后 2 秒自动切词（跟随上次 Random / 顺序）；加载文案从左上角 5px 起
- **Mini Games / Hardware Test**：任意页可用字母快捷键直接打开子项
- **文档**：同步 I2C / Hardware Test / Mini Games 中英文页

---

## 2026-08-12

### 改进

- **Battery**：未对时先按开机 uptime 小时暂存电量，NTP/RTC 同步后回填到曲线；浅睡 RAM pending 保留，深睡仍会丢
- **文档**：功能目录补充基础操作（`ESC` / `GO` 回菜单、任意界面 `h` 开 Help、翻页 / 截图）；Hardware Test 子程序链到对应 App 页；入门与全局快捷键同步中英文

---

## 2026-08-09 — v1.11

### 新增

- **Config Web 中英双语**：顶栏 `中文 | EN`；`?lang=` / Cookie `cp_lang` / `Accept-Language` 解析语言（默认中文）；各配置页文案随语言切换

### 改进

- **分区表**：改用 `partitions/no_ota_8MB.csv`，去掉 OTA `app1` 双槽；LittleFS **1.5 MB → 4.69 MB**（偏移 `0x340000`），固件区仍为 3.19 MB；打包 / Release / README 同步；改分区后需重刷固件 + FS
- **WiFi**：扫网手动连接缓存 BSSID/信道并周期重发 `begin`，降低扫网后关联超时 / `NO_SSID`；密码页与连接页不再依赖仍存活的 scan 结果
- **Time**：NTP 同步中恢复全屏无 header（与其它 Time 模块一致）
- **版本**：固件 / M5Burner 元数据统一为 **v1.11**（`APP_VERSION` / `m5burner.json`）；`APP_UPDATE_TIME` → `2026.08.09`
- **文档**：同步英文截图总览条目顺序与现有截图文件名；README 补充任意界面按 `h` 查看 Help
- **文档 / Time**：补充中英文时钟展示省电策略（空闲约 1 分钟主循环降频、Big Clock 刷新、与 Sleep 对比）

---

## 2026-08-08

### 改进

- **Header**：共享顶栏在 STA 已连接时重新显示 WiFi 信号图标（右→左：电池 / WiFi / BLE），并随 RSSI 刷新
- **Time**：NTP 同步中改用共享 header，联网期间可看到 WiFi 图标；同步结束后回到全屏时钟并停止刷状态区
- **版本**：`APP_UPDATE_TIME` → `2026.08.08`（v1.10）
- **文档**：补米家灯 / 风扇 / 炸锅 / 插座与 AC Auto 运行态截图；同步中英文 App 页与截图总览

---

## 2026-08-07

### 改进

- **Keyboard**：传输方式 / IMU 灵敏度仅退出 App 时写入 `config.json`，设置过程不落盘，避免卡顿
- **截图改存 PNG**：`Fn+s` 直接流式编码 PNG（M5GFX miniz），体积远小于旧 BMP；Config `/shots` 预览兼容旧 `.bmp`
- **截图反馈**：成功后屏幕反色闪一下；提示音可由 Options / Config Web 的 `sound.screenshot` 开关
- **空调自动化 Web**：品牌 / 模式 / 风力 / 传感器选择改为 radio
- **空调自动化**：进入后不再自动倒计时息屏；按 `S` 启动 5s 倒计时后息屏进入 AUTO；亮屏后再按 `S` / BtnA / 空闲 15s 可再灭屏
- **空调自动化 BLE**：息屏最长连听 6 分钟以覆盖约 5 分钟一发的温湿度广播；收到读数后短突发攒 filter，再 nap 4 分钟省电；亮屏仍短扫
- **空调自动化 UI**：倒计时 / 温湿度 / 曲线局部刷新，避免整屏闪；Config 标题与 `cfg` 副标题空一格；Config tip 改用横向并排上下箭头徽章
- **图标**：新增横向并排上下箭头 `drawIconArrowUpDownFlat` / `drawArrowUpDownFlatBadge`（适合 tip 高度）
- **进入提示**：进 AC Auto 释放 BLE 时显示 `Entering.`（退出仍为 `Exiting.`）

### 新增

- **`sound.screenshot`**：截图成功/失败提示音开关（默认开）；Options → Sound 与 Config Web 系统设置可调

### 修复

- **空调自动化**：AUTO 亮屏后无法再灭屏（`S` 仅首次启动）；二次 `Display.sleep` 补 `waitDisplay` 与亮度兜底

---

## 2026-08-06

### 新增

- **空调自动化（`n` / AC AUTO）**：可选 BLE 温湿度计；温度 `> on` 连续 filter 次开空调、`< off` 连续 filter 次关空调（默认 29/26/3）；按 `S` 启动后息屏自动工作，任意键亮屏；展示开关机次数、当前温湿度与 12h 曲线（每分钟一点）
- **配置入口**：App 内 `c` 配置页；Options 侧栏 `ac auto`；Config Web 顶栏「空调自动化」(`/ac-auto`)；参数含传感器、阈值、过滤次数、品牌/模式/设定温度/风力（`config.json` → `ac_auto`）
- **共享红外发送** `irSendAc()`：Infrared 与空调自动化共用

---

## 2026-08-04 — v1.10

### 改进

- **Options**：侧栏分类 + 右侧字段 / 选择页；header 标题改为 `Options`；模块覆盖 screen / sound / clock / calendar / infrared
- **Time**：Uptime / Clock / Countdown / Stopwatch 始终全屏无 header；切模式时左上角短暂显示模式名
- **Header**：子界面顶栏不再绘制右侧返回图标；状态图标与分页圆点靠右对齐；返回仍用 `ESC` / `GO`
- **Hub 卡片**：Mini Games / Hardware Test 序号徽章左移 1px，与标题间距更匀
- **Neon FX**：FPS 叠字改为画在 canvas 上再 `pushSprite`，避免二次绘制闪烁；文案改为 `fps: N`
- **版本**：固件 / M5Burner 元数据统一为 **v1.10**（`APP_VERSION` / `m5burner.json`）
- **文档**：补 MATRIX / WAVE / PCLOCK / LISSA 中英文页与侧栏；同步 Mini Games（14 项）、Time（去 Pure 切换）、Hardware Test / Mic 分页键位、Options 侧栏导航；`APP_UPDATE_TIME` → `2026.08.04`

---

## 2026-08-03

### 新增

- **米家快速切换 keymap**：按 Cardputer 真实 4×14 正方形键位绘制占用图（彩色=已绑定）；进 App 时若已配置任意热键优先显示；按对应键直达设备
- **Mini Games 视觉效果**（第 2 页）：`MATRIX` 代码雨、`WAVE` 多层贝塞尔丝波、`PCLOCK` 粒子时钟、`LISSA` 利萨茹曲线；无标题叠字，`-` / `=` 调速，`Space` / BtnA 脉冲
- **公共点阵字** `drawDotText` / `measureDotTextWidth1x`：Font0 1× 渲染后再按 scale 画方块并留 1px 缝，IR 与 Time Big Clock 共用
- **Infrared AC 电源图标**：顶排第 3 列显示 `ac_power` / `ac_power_active`，随开关态刷新
- **米家插座电源图标**：右栏用 `/icon/power[_active].png` 显示开关态；开关变化会触发面板重绘
- **Icon Demo**：补 `ir ac_power` 与通用 `power` 图标对

### 改进

- **米家快捷选择**：顶部正方形键盘占用图；下方横排已绑定设备名，热键字母 2× 着色（名中无该字母则前置），其余 1×；去掉底栏 tip
- **Time**：Uptime / Clock 默认即全屏 pure，去掉 `p` 切换与配置项 `time.pure`（Settings / Config Web / example 同步删除）
- **Time 省电**：Clock-like 模式无操作满 1 分钟后主循环 1s 一拍并对齐整秒；有操作时约 30ms；Big Clock（仅 HH:MM）活跃时 15s 检查一次
- **Time UI**：切模式时左上角短暂显示模式名；Big Clock 改用点阵大字，不再单独画秒
- **Infrared**：AC 点阵字改为调用公共 `drawDotText`

### 修复

- **LISSA**：去掉拖尾残影；相位 / 形变与曲线参数分离，消除 `a(t)·t` 导致的越转越快，速度档重新生效

---

## 2026-07-31

### 新增

- **Minesweeper 扫雷**（Games `8`）：EASY / NORMAL / HARD 三个难度；首挖及其邻格必定无雷；数字 0 连锁展开；数字格上 `Space` 和弦一次翻开邻格；`f` 插旗、`r` 重开、`1`–`3` 切难度；方向键长按连发移动光标
- **扫雷记录**：按难度保存最快通关时间、胜 / 总场次、最长连胜与当前连胜（`/mines_rec.dat`），`b` 打开记录页，破纪录时结果牌显示 `NEW BEST!`
- **Snake 贪吃蛇**（Games 第 2 页 `1`）：40 × 20 场地，蛇身按距头部渐暗；每 5 个果实刷出限时金色果实（+5）；`m` 切换撞墙 / 穿墙，两种模式最高分分开保存（`/snake_rec.dat`）；`-` / `=` 调速度档，转向带一步缓存
- **Conway Life 生命游戏**（Games 第 2 页 `2`）：60 × 30 环形网格 B3/S23；细胞按存活代数着色；内置滑翔机 / 滑翔机枪 / 脉冲星 / 轻型飞船 / R-五连体 / 橡实六个图案；`Enter` 编辑格子、`n` 单步、`-` / `=` 调速；自动识别 `STILL` / `OSC` / `DEAD` 并停止推进
- **WiFi 配置列表**：进 App 直接看已存网络（每页 3 张卡），方向键选中、`Enter` 连接、`Backspace` 删除；扫描列表改为每页 4 行带分隔线；header 标题随当前阶段变化
- **WiFi 连接 / 失败页**：连接中显示滑动进度条、倒计时与 AP 信息；失败后停在结果页并给出原因，可重试 / 改密码 / 回扫描
- **`[` `]` 翻页**：新增 `getBracketNavDelta()`；主菜单、Mini Games、Display、Hardware Test、Icons / Font 演示、米家列表统一支持
- **IMU 倾斜操控**：`app_common` 新增 `imuTiltPoll()` 共用助手，把倾斜转成上下左右离散方向，带中立姿态校准、死区迟滞、主轴判定与连发节流；判定基于「重力方向相对校准姿态转过的角度」（校准时在垂直于重力的平面内建立屏幕上 / 右基向量，运行时取重力的垂直分量投影），避免端得斜时某个方向顶到 ±1g 量程尽头而无法触发
- **倾斜指示点**：`IMU` 徽章右侧小方框内的点跟随倾斜偏移，可直观确认四个方向均被识别
- **Snake IMU 转向**：`i` 开关倾斜转向，顶栏亮金色 `IMU` 徽章；按 `i` 时以当前握持姿态为中立位，无板载 IMU 时提示 `NO IMU`
- **扫雷 IMU 光标**：`i` 开关倾斜移动光标，像鼠标一样保持倾斜就一直走、倾得越多走得越快（260ms → 45ms 一格），顶栏同样有 `IMU` 徽章标识介入状态
- **Infrared**：全屏遥控页（无 header）；品牌 logo（`data/icon/brand`）；发送态信号图标（`send_active` / `send_inactive`）
- **RGB565 头部**：`R565` + 宽高，支持非正方形 bake（品牌 66×20、风速 34×30、信号 57×38 等）

### 改进

- **WiFi 密码页**：输入时只重绘编辑区，退出改用 `Fn+Q`，字母键全部留给密码
- **Config**：新增 `removeAppConfigWifi()`，删除某条配置后自动回落到剩余的第一条
- **Connectivity**：等待期间重发 `WiFi.begin`；关射频前等 deauth 落定，避免重连一直挂到超时
- **HID Keyboard**：BtnGO 轻按开关主机列表、长按退出；删除配对由 `d` 改为 `Backspace`
- **Infrared**：品牌 / 类别改为离开 App 时才落盘，阻塞期间显示 `Loading` / `Saving` 提示
- **Infrared**：AC / TV 按设计稿重排按键与温度 / 品牌区；发射后信号图标短暂变红
- **Bake**：递归 `/icon` 全部子目录；结束后屏上提示并自动回到 Config
- **拉取脚本**：按本地 PNG 推导路径；支持只填 IP；拒绝无头部的旧格式文件
- **Config Web**：`/icon/` 下任意子目录资源可直接访问
- **资源**：设备 / IR / Logo 重新烘焙；风速与部分设备 PNG 更新；`icon/brand` 下所有 logo 统一加 `brand_` 前缀（如 `brand_midea`、`brand_aux`），顺带规避 Windows 保留文件名 `aux.*`
- **config**：`week_start` 只认 `calendar.week_start`，不再兼容 / 清理旧的 `system.week_start`
- **Mini Games**：Hub 增至 10 项占两页；hub help 改为说明翻页与分页清单，不再逐条罗列
- **Mini Games**：移除子游戏内未在帮助中说明的 `0` 返回选择页快捷键，返回统一用 `ESC` / `GO`
- **扫雷相邻插旗 / 排雷键**：新增 `[` 插旗、`]` 排雷，两键相邻，一只手就能来回切换，原来的 `f` / `Space` 保持可用
- **上键改 `e`**：Cardputer 键盘是整齐网格，`s` 正上方是 `e` 而不是 `w`，扫雷 / 贪吃蛇 / 生命游戏 / Neon FX 的字母方向键统一改成 `EASD`
- **IMU 倾斜方向修正**：屏幕上方对应机身 `-y` 轴，屏幕右方改为由「屏幕上方 × 校准时的重力方向」推出，修掉往上倾却往左走的 90° 错位
- **文档**：WiFi / HID Keyboard / Infrared 中英文页同步新键位；新增扫雷 / 贪吃蛇 / 生命游戏中英文页与侧边栏入口

---

## 2026-07-30

### 改进

- **config 归属**：`timezone` → `time.timezone`，`system.week_start` → `calendar.week_start`；加载兼容旧路径，保存写新路径并清理旧键
- **Options**：新增 calendar 模块（week）；clock 保留 default / tz / pure
- **Infrared**：应用内 `t` / `Tab` 切换后延后写入 config（先刷新界面，再合并落盘，避免卡顿）
- **Config Web**：系统页按 time / calendar 分栏，不再写顶层 `timezone` / `system.week_start`
- **Mini Games**：Hub 分页（每页最多 8 项）+ header 分页圆点
- **Battery**：浅睡 / 深睡类型记入日志

---

## 2026-07-29

### 新增

- **Calendar**（`a`）：单月日历网格，今日高亮；`,` / `.` 翻月，`-` / `=` 翻年，`t` 回今天
- **Keyboard**：裸按 `` ` `` 直接发 Esc；主界面无 header；左侧胶囊 Fn/Aa/Opt/Ctrl/Alt；居中大号按键回显；长按 Fn 开关 IMU（居中鼠标图标 + 右侧灵敏度条；字母 ygv|uhb 左右点击，非字母功能键仍可发；`1`–`0` 调灵敏度）；底栏状态 / 绿色设备名 / 黄底槽号

### 改进

- **Mic**：从主菜单移入 Hardware Test（`h` → `9`），与其它硬件测试并列
- **Config 网页 · 系统**：按 config 分项左侧 list（系统 / 屏幕 / 声音 / Time / 红外 / 键盘）+ 右侧设置面板；补 `hid_keyboard`（传输方式、IMU 灵敏度）
- **Keyboard BLE 鼠标**：复合 HID 报告 + 通知启用 / Report Reference 可读；PnP 版本 bump（旧配对需重配）
- **Keyboard 卡键**：每帧同步键位（不依赖 isChange）；松开报告优先发送；退出 / 切模式前强制全键抬起
- **内容区布局**：`APP_CONTENT_Y` 贴齐 header 下沿铺背景；文本 / 卡片用 `APP_CONTENT_INSET_Y` 保留上内边距，避免翻页留缝
- **Hub 卡片**：主菜单 / Mini Games / Hardware Test 共用网格常量；Games / Test 子项恢复主题色边框（浅暖金 / 浅冷青，弱于徽章主色）
- **Time**：日期行补星期简写；Pure Big 时钟在大字右下角显示秒

### 修复

- **回主页破音**：`showMenu` 不再无条件 `leaveMorseApp` 卸喇叭；`releaseSpeakerQuiet` 已 hold 时跳过，避免重复 `gpio_reset` 触发 NS4168 破音
- **Hardware Test**：回 hub 时重置 `hardwareTestMode`，避免 ESC 后仍走子 app 盖住菜单

---

## 2026-07-28 — Docs

### 新增

- **Mini Games 子页**：补 Coin Toss / Double Pendulum / Prize Wheel / Curves 中英文独立介绍（快捷键与说明）

### 改进

- **文档侧栏**：Mini Games 七项齐全（中 / 英）；Display / IMU / Font / Icons / RGB LED / BLE / I2C 归入 Hardware Test；集合页表格补子页链接

---

## 2026-07-27 — v1.03

### 新增

- **Mini Games**（`g`）：硬币、双摆、抽奖轮、骰子、牛顿摆、Neon FX、曲线七合一选择页；子游戏内 `ESC` / `GO` 回选择页
- **Info Storage 页**：本地 Flash（LittleFS）与 TF 卡已用 / 剩余；无 TF 时显示 n/a
- **Neon FX**（`y`）：全屏 8-bit 调色板高帧率动画（Vortex / Plasma / Tunnel）+ 软渲染立方体；左上角实测 FPS
- **Dice**（`z`）：桌面物理骰子；IMU 晃动强度驱动滚动幅度；停稳后最短弧缓动面朝上；金色 TOTAL 结果牌
- **Newton Cradle**（`q`）：五球牛顿摆；固定摆长约束、重力、近弹性冲量碰撞与抛光钢球光照
- **Time Pure 大号时钟**：Pure 模式下按 `b` 切换自动适配屏幕的最大默认字体时:分时钟
- **文档**：中 / 英文 Mini Games、Info Storage、Neon FX、Dice、Newton Cradle 页；功能目录、入门主菜单表、VitePress 侧栏同步

### 改进

- **Prize Wheel**：长按 `Space` / `GO` 蓄力旋转；转动中 `-` / `=` 改格数会重置状态；金色 `#N` 结果牌并高亮中奖格
- **Dice**：停稳后排布与点数按屏幕从左到右顺序；`TOTAL` 结果牌保持不变；`GO` 与空格同效蓄力投掷
- **Mini Games**：硬币 / 双摆 / 曲线等触发同步响应侧边 `GO`（BtnA）；牛顿摆重放、Neon 脉冲等同效
- **Font**：演示列表去掉不可用的 efontCN 占位项
- **Dice**：骰子与点数放大；空格改为带 `POWER` 状态条的长按蓄力投掷，力度提升；缩短停止缓动与结果排列等待，放大并拉开最终点数展示

---

## 2026-07-23 — Docs EN

### 新增

- **英文文档站**：VitePress `locales`（中文为 root，英文为 `/en/`）；全量翻译首页、入门、快捷键、全部 App 页、截图总览与开发文档；导航栏可切换语言
- **README**：补充中 / 英文档入口（`/en/`）

### 改进

- **Keyboard 文档**：补主机重命名截图 `app_hidkeyboard.png`

---

## 2026-07-23 — v1.01

### 新增

- **HID Keyboard 主机别名**：`Fn+p` 列表按 `r` 可给已配对槽位起名（最多 16 字，NVS `hidkb`）；列表 / 状态 / peer 行优先显示别名，空名回退 MAC
- **文档截图总览**（`/apps/shots`）：无侧栏宽页平铺全部实拍，图下标注文件名；顶栏 / 功能目录可进
- **文档实拍截图**：各 App 页替换旧占位图为 `app_*.png` 设备实拍（含 Time Pure、Options 分栏、HID 多屏等）
- **HID Keyboard 多主机 BLE**：最多保存 5 台已配对主机；`Fn+p` 主机列表切换 / 新配对 / 删除；同时只连一台；BLE 名 `Cardputer KB`
- **Keyboard 输入区**：右上角 2× 槽位号；peer 行显示别名或 MAC

### 改进

- **README / 文档品牌**：README 加 Sparks logo；文档首页 hero 用大 logo；Config 文档补充米家设备 / 分组网页管理截图
- **Keyboard BLE 配对**：区分新配对 / 已知槽回连的认证时序；单边 bond 时提示 `plz forget on device` 并停广播拒连，避免坏密钥狂连
- **Sleep 截图热键**：浅睡确认页 `Fn+s` 可截图，不再被 `s`→深睡抢走
- **Keyboard 命名**：菜单短名 `KB`、标题 `Keyboard`；源码 / 文档 / 截图由 `hid-kb` 统一为 `hid-keyboard`
- **Keyboard 退出**：BtnGO 退出前清内容区显示 `Exiting.`，再完整 `deinit` 释放 BLE 内存
- **米家退出**：离开 App 时若 BLE 仍开着，提示 `Exiting.` 并 `deinit` 协议栈，避免仅停扫后常驻耗电
- **米家炸锅倒计时**：每秒只刷状态行右侧剩余时间，避免整栏重绘闪烁
- **倒计时到点**：`Time up` / `x OK` 贴在大字下方，不再占底栏

### 修复

- **Keyboard paired 状态**：从主机列表切回输入界面时，清屏后未失效状态缓存，偶发不画绿色 `paired`；重绘前清空 `g_drawn_link_status`
- **Keyboard 主机列表**：切换 / 新配对时的重连与白名单、断开竞态、列表闪烁；连接成功后自动关闭列表
- **Header BLE 图标**：退出后 parked / 未初始化栈不再误显图标

---

## 2026-07-22

### 新增

- **GitHub Pages 文档站**：`.github/workflows/docs.yml` 在 push `main` 时自动 build VitePress 并部署；产物不进仓库；在线地址 https://kylebing.github.io/m5stack-cardputer-sparks/
- **米家炸锅本地倒计时**：刷新拿到 `left-time` 后在状态行右侧本地倒数（不每秒问设备）；操作后约 1s 再回读状态
- **开发文档 · 内存说明**（`docs/dev/memory.md`）：Heap / PSRAM / 碎片与 Max Alloc、固件分配习惯、常见 lowmem 场景、Info Memory 数值解读
- **开发文档 · 图片处理与烘焙**（`docs/dev/images.md`）：从 wiki 迁入 VitePress「开发」侧栏
- **米家设备 Token 获取**（`docs/apps/mijia-token.md`）
- **VitePress 文档**（`docs/`）：功能目录与各 App 简介 / 快捷键 / 使用说明；截图预留 `docs/public/shots/{app}-{子功能}.png`；米家页含 model 分类表与设备图标；对应固件 **v1.0.0** 文档起点（`npm run docs:dev`）
- **多 WiFi 配置**：`wifis[]` + `wifi_active`（最多 5 条）；WiFi App / Config Web `/wifi` 均可管理；其它 App 只用当前 active
- **Infrared AC Auto 模式图标**：`ac_auto` / `ac_auto_active`；模式栏改为上 3 下 2
- **Infrared AC 风速图标**：顶栏显示当前档（`ac_fan_auto` / `min` / `low` / `med` / `high` / `max`），进 App 时预缓存
- **屏幕反色持久化**：`screen.invert`；Options / Config Web 可改，开机与 Web 保存后立即生效

### 改进

- **README 刷机说明**：esptool / M5Burner 下载、串口连接、整片 / 程序 / 资源三种烧录指令与常见问题；链到在线文档
- **Release 说明**：GitHub Release 正文同步完整刷机步骤（与 README 一致）
- **Cursor / 米家 / Time 文档**：补充更新机制、后台运行说明（`docs/apps/cursor.md`、`mijia.md`、`time.md`）
- **VitePress `base`**：CI 使用 `/m5stack-cardputer-sparks/`（与 GitHub Pages 仓库名一致）；修复资源 404；favicon 随 base 拼接
- **Time 倒计时到点**：红色 `00:00:00` + `UP!` 横幅；响铃时红白闪烁；底栏 `Time up` + `x cancel`；标题强调 `UP`
- **文档侧栏**：拆「开发」模块；米家控制 / 系统与信息分组调整；docs dev 端口 `3123`
- **Icons**：补入 IR AC 模式（off/on）与风速六档资源页
- **BLE**：去掉 info 页；入口 `[S] scan` 用 x2 + key badge；翻页支持 `[]`（仅 Help 说明，tip 不显示）
- **BLE**：操作 tip 移到底栏并加 Help（设备类型说明）；列表改用 Font0 x1；序号橙色、类别黄色；收紧条目上下行间距
- **配置键统一**：`Infrared` → `infrared`；`brightness` 迁入 `screen.brightness`（兼容旧顶层 `brightness` / 大写 `Infrared`）
- **WiFi 配置兼容**：旧 `"wifi":{ssid,password}` 加载时自动迁移；Config Web `/wifi` 列表增删改与设 Active
- **Mic 退出嗡嗡**：`Mic.end` 后 `reclaimAndReleaseSpeakerQuiet`（Speaker begin→静音 end 再 hold），把 G43 从 PDM 残余抢回；进 Help 关麦同路径
- **米家概览缓存**：`mijiaOverviewUi` 按 `device_count` 进 App 时分配、离开释放，不再开机常驻 50 台
- **设备图标绘制**：RGB565 1:1 按行推屏；缩放临时 `malloc`，去掉静态整图 scratch
- **开机截图恢复**：不再开机扫 `LittleFS.usedBytes()`（图标多时可达数秒）；腾空间改到真正截图保存时
- **Icons**：去掉 `b` 现场烘焙；烘焙仅走 Config `POST /bake-rgb565` / `pull_rgb565_from_device.py --bake`

### 移除

- **Mic 录音**：去掉 TF 录音、`/audioRecord` 列表播放/删除、WiFi/NTP 校时文件名；Mic 仅保留实时波形 + VU + 增益
- **Log App**（`Fn+i`）与 `app_log` 模块
- **Cursor LittleFS 诊断日志**：`/cursor.log` / `/cursor.err` 写入与 Config 查看入口；开机不再写 err 面包屑
- **`scripts/png_to_rgb565.py`**：改由设备端 M5GFX 烘焙 + pull 脚本拉取

---

## 2026-07-21

### 新增

- **Info App**（主菜单 `i`）：从 `main` 拆到 `app_info`；Memory 进度条（Heap / PSRAM / Sketch / LittleFS）与 Chip / Fw / Net / Run 翻页
- **Config Web `/wifi` / `/about`**：WiFi 独立页；关于页展示固件版本等信息
- **喇叭音量**：`sound.volume`（0~100，默认 25）；Options → Sound → `volume`；Config Web 可调；Mic 列表播放时 `-=` 实时调节
- **Mic 录音列表**（`l`）：扫描 TF `/audioRecord`；选中播放 / 停止 / Backspace 删除；播放中只刷进度行
- **Infrared AC 模式图标**：制冷 / 制热 / 除湿 / 送风（含 active）
- **图标 RGB565 烘焙**：设备 / IR / Logo 预生成 `.rgb565`；绘制优先 bake 文件，缺失回退 PNG；Config `POST /bake-rgb565` 现场烘焙；`scripts/pull_rgb565_from_device.py` 拉取到 `data/`

### 改进

- **Options 音量**：加减立刻同步内存；写盘防抖且失败保持脏标记；其它配置 RMW 前先落盘，避免音量被打回
- **取消提示音自动静音**：不再播完 `releaseSpeakerQuiet`，去掉静音预热音，减轻冷启动破音
- **Mic TF 挂载**：进 App 预挂载；`SD.begin` 失败重试，并兼容已被其它模块挂上的卡，修复首次录音误报 `no SD`
- **Countdown 闹钟破音**：响铃期间保持功放，避免每声后 `releaseSpeakerQuiet` 再冷启动；排程改用播完后的 `millis()`
- **开机破音**：`releaseSpeakerQuiet` 仅在 Speaker 已运行时 stop/end，未 begin 只拉低脚
- **开机变慢**：去掉开机写 `/cursor.err` 面包屑；`initBatteryLog` 挪到首屏菜单之后
- **Config `/shots`**：TF 与 Flash 分区展示；分别「清空 TF 截图」/「清空 Flash 截图」（`/shots/clear-tf`、`/shots/clear-flash`），不再混清
- **Config Web**：顶栏 Tab 高亮；内容卡片布局；导航拆出 WiFi / 关于等入口
- **Infrared**：AC/TV 按键统一为上下叠排样式；屏高紧时 Auto+Fan 同行；右栏贴边距
- **喇叭脚拉低**：开机与 Mic 进出时 `releaseSpeakerQuiet`（卸 I2S + hold）；列表播过后不再 `Mic.begin`（避免 PDM 时钟灌进功放 LRCLK）；退出列表整页重绘 Record
- **Mic**：列表与 header 留白；选中条对齐字形；列表模式关麦；播完保持喇叭脚拉低；回示波器若本会话播过音则显示 `mic paused`，按 `R` 再开麦
- **M5Burner 元信息**：`m5burner.json` 更名为 Sparks，仓库链到 `m5stack-cardputer-sparks`；打包产物改为 `Sparks-<ver>.zip` / `sparks_merged.bin`
- **M5Burner 打包（Windows）**：Git Bash 下 `python3`/`python` 自动回退；strip CRLF 避免版本号污染 JSON；无 `zip` 时用 `tar -a` 打 zip
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
