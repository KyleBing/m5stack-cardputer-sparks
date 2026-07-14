# CHANGELOG

本文件记录 M5Stack Cardputer 固件项目的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，按日期从新到旧排列。

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
