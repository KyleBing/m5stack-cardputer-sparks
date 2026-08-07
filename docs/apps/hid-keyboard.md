# Keyboard

主菜单按键：`k`

将 Cardputer 作为 **USB** 或 **BLE** HID 键盘 / 鼠标，向主机发送按键。本 App **不能**用 `ESC` 退回菜单，请**长按 BtnGO**。

标题不显示（主界面无 header）。左侧胶囊显示 Fn / Aa / Opt / Ctrl / Alt；中间大号回显最近按键；底栏为连接状态、绿色设备名与黄底槽号。

## 截图

**主界面 / USB / 鼠标**

<div class="shot-row">

![hid-main](/shots/app_hidkeyboard_main.png)
![hid-usb](/shots/app_hidkeyboard_usb_mode.png)
![hid-mouse](/shots/app_hidkeyboard_mouse_mode.png)

</div>

**主机列表 / 特殊键**

<div class="shot-row">

![hid-hosts](/shots/app_hidkeyboard_pair_list.png)
![hid-001](/shots/app_hidkeyboard_001.png)
![hid-special](/shots/app_hidkeyboard_specailkey.png)

</div>

## 快捷键

### 模式与退出

| 按键 | 作用 |
|------|------|
| `Fn` + `u` | USB HID |
| `Fn` + `b` | BLE HID |
| **长按 `Fn`** | 开关 IMU→鼠标指针 |
| `Fn` + `p` | BLE 主机列表（切换 / 配对） |
| **短按 BtnGO** | 开 / 关 BLE 主机列表（同 `Fn` + `p`） |
| **长按 BtnGO** | 退出回主菜单（并断开 BLE） |
| `Fn` + `h` | Help |

### IMU 鼠标（长按 Fn 开启）

开启后字母不作字符发给主机（作点击）；数字调灵敏度；其余功能键仍可发。

| 按键 | 作用 |
|------|------|
| 左半字母 `qwerty asdfg zxcv`（`ygv` 属左） | 左键 |
| 右半字母 `uiop hjkl bnm`（`uhb` 属右） | 右键 |
| `1`–`9` / `0` | 灵敏度 1..10 |

界面居中显示鼠标图标，右侧为灵敏度条。BLE 下若指针不动，请在主机端忘掉设备后重新配对（报告描述已含鼠标）。

传输方式和灵敏度会保存到 `config.json` 的 `hid_keyboard` 配置中，下次进入应用时自动恢复。IMU 鼠标开关属于临时状态，每次进入应用时默认关闭。

### BLE 主机列表（`Fn+p` / 短按 BtnGO）

最多保存 **5** 台已配对主机；同时只连接一台。

| 按键 | 作用 |
|------|------|
| `1`–`5` / `;` `,` `.` `/` | 选择槽位 |
| Enter / Space | 切换到该主机（断开当前；目标机需在蓝牙里点一下 `Cardputer KB`） |
| `n` | 新配对（需有空槽；会拒绝旧主机抢连） |
| `r` | 重命名当前槽位别名（Enter 保存，空名则恢复显示 MAC，`` ` `` 取消） |
| Backspace | 删除当前槽位配对 |
| `p` / `h` / 短按 BtnGO | 关闭列表 |

### Fn 层（Help 第 2 页）

| 按键 | 作用 |
|------|------|
| `` ` ``（裸按） | Esc（发给主机） |
| `Fn` + Backspace | Delete |
| `Fn` + `;` `,` `.` `/` | 方向键 |
| `Fn` + `1`–`0` | F1–F10 |
| `Fn` + `-` `=` | F11 / F12 |
| `Fn` + `A`/`a` | CapsLock |
| `Fn` + 修饰键 | 右侧修饰键映射（见 Help） |

## 使用说明

1. 用数据线连电脑选 USB，或 `Fn+b` 开 BLE 键盘后在主机端配对（`Fn+p` → `n`）。
2. 多台主机：`Fn+p` 或短按 **BtnGO** 打开列表，选槽后 Enter 切换。`reconnecting #N` 表示正在等目标电脑自动回连（多数几秒内成功；不行再在蓝牙里点一下 `Cardputer KB`）。
3. 新配对按 `n`：会拒绝旧主机抢连，再在新电脑上搜索配对。
4. 普通字符直接敲击；`` ` `` 直接发 Esc；其它功能键走 Fn 层。
5. 长按 `Fn` 开 IMU 鼠标；`ygv` 左键 / `uhb` 右键，`1`–`0` 调灵敏度；非字母功能键仍可发。
6. 退出务必**长按 BtnGO**（会断开 BLE）；短按只是开关主机列表；`` ` `` 已改为发给主机的 Esc。
7. BLE 键鼠复合：升级固件后若 IMU 指针无效，主机端忘掉 `Cardputer KB` 再配对。
