# Radio 收音机

进入方式：主菜单 `e` → [EX I2C](./ex-i2c) `1` / `r`

左侧 Grove 上的 FM 收音机。买现成 **TEA5767** 或 **RDA5807M** 四针模块即可（I2C `0x60` 或 `0x10`/`0x11`），共用一套界面。声音从**模块耳机孔**输出。芯片接线与驱动 API 见 [EX I2C](./ex-i2c)。

进入后探测左侧 Grove（Ex_I2C）。无芯片时显示 `NO MOD`。

## 截图

**播放 / 无模块 / 电台列表**

<div class="shot-row">

![radio-playing](/shots/app_radio_playing.png)
![radio-no-module](/shots/app_radio_no_module.png)
![radio-stations](/shots/app_radio_station_list.png)

</div>

## 模块与接线

买**现成的 4 pin 成品板**即可，TEA5767 和 RDA5807M 都不用自己焊。淘宝 / 模块店搜 `TEA5767` 或 `RDA5807M`，选已经引出 **SDA / SCL / VCC / GND** 四针、板上带 3.5 mm 耳机孔的那种，杜邦线或 Grove 转接线插上就能用。

不要买需要自己焊排针、天线或音频座的裸板。

### 用哪一个口

插到机身**左侧 Grove**（HY2.0-4P），不要用背面 EXT。屏幕朝自己、键盘在下时，左侧那颗 4 针橡胶座就是 **Ex_I2C**。从上到下：

| 顺序（上→下） | 针脚 | 作用 |
|---------------|------|------|
| 1 | GND | 地 |
| 2 | 5V | 电源 |
| 3 | **G2** | **SDA** |
| 4 | **G1** | **SCL** |

### 四针对接

模块四针顺序因商家而异，按丝印**信号名**对到左侧 Grove，不要按从左到右硬数：

| 模块 4 pin | 左侧 Grove |
|------------|------------|
| **SDA** | **G2** |
| **SCL** | **G1** |
| **VCC** / VDD | **5V** |
| **GND** | **GND** |

耳机插模块自带的 3.5 mm 孔，声音不走机身喇叭，也**不支持 AirPods / 蓝牙耳机**。很多模块把耳机线当天线，请用有线 3.5 mm 耳机或音箱。接好后 [ExI2C](./i2c) 应看到 TEA5767 `0x60`，或 RDA5807M `0x10` / `0x11`。

> TEA5767 没有芯片音量，用耳机旋钮；RDA5807M 用主界面 `-=`（信号条旁 5×3 黄格）或 Tuner → Volume。

## 快捷键

完整说明见 `h` Help（多页）。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `←` `→` | 搜台（前一台 / 后一台信号）；同向再按停止 |
| `↑` `↓` | 频率微调 |
| `-` `=` | RDA：音量 · TEA：步进调谐 |
| `[` `]` | 上一 / 下一已存电台 |
| `a` | 自动搜台并保存 |
| `m` / `o` | 静音 / 强制单声道 |
| `l` | 电台列表 |
| `t` | 打开 / 关闭 Tuner |
| `i` | RDS 信息（仅 RDA） |
| `1`–`0` | 跳到对应电台槽 |

电台列表内：方向键选择，`Enter` 调谐并退出，`r` 改名，`n` 添加当前频率，`d` / Backspace 删除，`c` 清空全部并保存，`p` 置顶。

## Tuner

主界面按 **`t`** 打开 Tuner（设置列表），再按 `t` 或 `ESC` 关回播放页。改动会立刻写入芯片并保存。

| 按键 | 作用 |
|------|------|
| ↑ ↓ / `;` `.` 等 | 上下选一项 |
| Tab / Shift+Tab | 下一项 / 上一项 |
| `Enter` / `Space` / `=` | 下一项取值 |
| `-` | 上一项取值 |
| `t` | 关闭 Tuner |
| `h` | Tuner Help |

### TEA5767 各项

| 项 | 取值 | 说明 |
|----|------|------|
| Band | Europe / Japan | Europe 87.5–108 MHz；Japan 76–91 MHz |
| De-emphasis | 50 us / 75 us | 去加重：欧日 50µs，北美 75µs |
| Seek mode | Software / Hardware | Software=软件步进搜台；Hardware=芯片硬件搜台 |
| Seek stop | Low / Mid / High | 硬件搜台停台门限（信号越严越不容易停） |
| Injection | High / Low | 本振注入边；串台时可换一边 |
| Soft mute | On / Off | 弱台软静音 |
| High cut | On / Off | 弱台削高音降噪 |
| Noise cancel | On / Off | 立体声噪声抑制 |
| Channel mute | Off / Left / Right | 静音左或右声道 |

### RDA5807M 各项

比 TEA 多频段、步进、音量和电台数据：

| 项 | 取值 | 说明 |
|----|------|------|
| Band | 87-108 / 76-91 / 76-108 / 65-76 / 50-76 | 对应欧 / 日 / 宽 / 东欧 / 低端频段 |
| Step | 100 kHz / 200 kHz / 50 kHz / 25 kHz | 调谐步进 |
| De-emphasis | 50 us / 75 us | 同上 |
| Seek mode | Software / Hardware | 同上 |
| Seek threshold | 0–15 | 硬件搜台 SNR 门限（默认 8；越大越严。停台后还会校验有效台） |
| Seek wrap | On / Off | 搜到频段尽头是否绕回 |
| Volume | 0–15 | 芯片音量 |
| Bass boost | On / Off | 低音增强 |
| Soft mute | On / Off | 弱台软静音 |
| Soft blend | On / Off | 弱立体声混成单声道 |
| Radio data | On / Off | 欧洲电台数据（RDS） |
| US radio data | On / Off | 北美电台数据（RBDS） |
| Auto freq | On / Off | 自动频率控制，一般保持 On |

主界面 `i` 可看电台数据（站名 / 电台文本等），仅 RDA 有效。

## 使用说明

1. 把现成 4 pin 模块插到**左侧 Grove**（G2=SDA、G1=SCL），耳机插模块孔。  
2. 主菜单按 `r` 进入。顶栏显示芯片名；刻度盘绿点=已存电台，青点=当前电台。  
3. 方向键搜台，或 `a` 自动扫描；`l` 管理电台。  
4. 按 `t` 进 Tuner 改频段 / 搜台方式（见上表）。  
5. 退出 App 时芯片 standby，省电。
