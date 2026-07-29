# 功能目录

按主菜单分类浏览。每个 App 文档均包含：**简介与截图**、**快捷键**、**使用说明**。

截图命名：`docs/public/shots/app_{app}_{子功能}.png`（实拍自设备 `Fn+s`）。全部实拍见 [截图总览](/apps/shots)（无侧栏宽页）。

## 主菜单

更多说明见 [入门 · 主菜单](/guide/getting-started#主菜单)。



<div class="shot-row">

![menu-001](/shots/app_menu_page1.png)

![menu-002](/shots/app_menu_page2.png)

</div>

## 智能家居

| 键 | App | 说明 |
|----|-----|------|
| `m` | [Mijia](./mijia) | 米家局域网内控制 |
| — | [获取设备 Token](./mijia-token) | 用云端工具导出 token / ble.key，转成 `config.json` 格式 |
| `x` | [Infrared](./infrared) | TV / AC 红外遥控（GPIO44） |

## 网络与配置

| 键 | App | 说明 |
|----|-----|------|
| `u` | [Config](./config) | 通过 AP、LAN 局域网 web 配置固件配置 config.json |
| `w` | [WiFi](./wifi) | 连接、扫描、切换已保存 wifi（最多 5 条） |

## 时间与电源

| 键 | App | 说明 |
|----|-----|------|
| `t` | [Time](./time) | 钟表功能，系统运行时长 / 时钟 / 倒计时 / 秒表 |
| `a` | [Calendar](./calendar) | 单月日历网格，翻月 / 翻年，今日高亮 |
| `p` | [Battery](./battery) | 实时电量，过去12小时的电量变化图表 |
| `s` | [Sleep](./sleep) | 进入浅睡 / 深睡 |

## 效率工具

| 键 | App | 说明 |
|----|-----|------|
| `c` | [Cursor](./cursor) | Cursor 用量摘要、日、星期、月用量图表 |
| `k` | [Keyboard](./hid-keyboard) | USB / BLE HID 键盘 |
| `j` | [Morse](./morse) | 摩斯电码发声、点划高亮与实时波形 |

## 系统与信息

| 键 | App | 说明 |
|----|-----|------|
| `o` | [Options](./options) | 系统层面的配置，屏幕亮度、声音、时间、红外 |
| `i` | [Info](./info) | 查看 内存 / 存储 / 芯片 / 固件 / 网络 / 运行信息 |
| `v` | [Version](./version) | 版本 关于 |

## 硬件调试与演示

| 键 | App | 说明 |
|----|-----|------|
| `g` | [Mini Games](./mini-games) | [硬币](./coin-toss)、[双摆](./double-pendulum)、[抽奖轮](./prize-wheel)、[Dice](./dice)、[牛顿摆](./newton-cradle)、[Neon FX](./neon-fx)、[曲线](./curves) |
| `h` | [Hardware Test](./hardware-test) | Display、IMU、Font、Icons、LED、BLE、I2C、[Mic](./mic) |
