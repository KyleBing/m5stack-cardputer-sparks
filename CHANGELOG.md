# CHANGELOG

本文件记录 M5Stack Cardputer 固件项目的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，按日期从新到旧排列。

---

## 2026-07-10

### 新增

- **Cursor** 应用（`x`）：拉取 Cursor 用量摘要、Auto/API 进度条、7/30 日柱状图；配置项 `cursor.api_key`
- 应用模块化拆分：`app_rtc`、`app_icon_demo`、`app_cursor` 等从 `main.cpp` 独立

### 改进

- **Web 配网**：暗黑主题；设备列表图标黑底；保存成功页增加「返回主页」按钮
- **WiFi**：进入时自动用已保存配置连接并显示 IP；`r` 刷新重连、`c` 更换网络；密码/连接提示二倍字体
- **Cursor**：周期用量先显示、图表事件后台加载；`used` 与 `reset` 同行分色；30 天柱状图均分屏宽、柱下稀疏日期标签
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

[//]: # (Commits: 27e5681 → 13bd301)
