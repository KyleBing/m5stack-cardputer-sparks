# Radio 收音机

主菜单按键：`r`

左侧 Grove 上的 FM 收音机。买现成 **TEA5767** 或 **RDA5807M** 四针模块即可（I2C `0x60` 或 `0x10`/`0x11`），共用一套界面。声音从**模块耳机孔**输出。

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

耳机插模块自带的 3.5 mm 孔，声音不走机身喇叭。天线一般已做在板上。接好后 [ExI2C](./i2c) 应看到 TEA5767 `0x60`，或 RDA5807M `0x10` / `0x11`。

> TEA5767 没有芯片音量，用耳机旋钮；RDA5807M 用主界面 `-=` 或 Tuner → Volume。

## 快捷键

完整说明见 `h` Help（多页）。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `←` `→` | 步进调谐 / 停止扫描 |
| `↑` `↓` | 搜台 / 换方向 |
| `-` `=` | RDA：音量 · TEA：步进调谐 |
| `[` `]` | 上一 / 下一已存电台 |
| `a` | 自动搜台并保存 |
| `m` / `o` | 静音 / 强制单声道 |
| `l` | 电台列表 |
| `t` | 打开 / 关闭 Tuner |
| `i` | RDS 信息（仅 RDA） |
| `1`–`0` | 跳到对应电台槽 |

电台列表内：方向键选择，`Enter` 调谐并退出，`r` 改名，`n` 添加当前频率，`d` / Backspace 删除，`p` 置顶。

## Tuner

主界面按 **`t`** 打开 Tuner（设置列表），再按 `t` 或 `ESC` 关回播放页。改动会立刻写入芯片并保存。

| 按键 | 作用 |
|------|------|
| ↑ ↓ / `;` `.` 等 | 上下选一项 |
| `Enter` / `Space` / `=` | 下一项取值 |
| `-` | 上一项取值 |
| `t` | 关闭 Tuner |
| `h` | Tuner Help |

### TEA5767 各项

| 项 | 取值 | 说明 |
|----|------|------|
| Band | EU / JP | EU 87.5–108 MHz；JP 76–91 MHz |
| Deemph | 50us / 75us | 去加重：欧日 50µs，北美 75µs |
| Seek | Soft / Chip | Soft=软件步进搜台；Chip=芯片硬件搜台（SM） |
| Stop | Lo / Mid / Hi | 硬件搜台停台门限（SSL，信号越严越不容易停） |
| Inject | High / Low | 本振注入边（HLSI）；串台时可换一边 |
| SMute | On / Off | 弱台软静音 |
| HiCut | On / Off | 弱台削高音降噪（HCC） |
| SNC | On / Off | 立体声噪声抑制 |
| MuteLR | Off / L / R | 静音左或右声道 |

### RDA5807M 各项

比 TEA 多频段、步进、音量和 RDS：

| 项 | 取值 | 说明 |
|----|------|------|
| Band | EU / JP / Wide / East / Low | EU 87–108；JP 76–91；Wide 76–108；East 65–76；Low 50–76 |
| Step | 100k / 200k / 50k / 25k | 调谐步进 |
| Deemph | 50us / 75us | 同上 |
| Seek | Soft / Chip | 同上 |
| SeekTh | 0–15 | 硬件搜台门限 |
| Wrap | On / Off | 搜到频段尽头是否绕回 |
| Volume | 0–15 | 芯片音量 |
| Bass | On / Off | 低音增强 |
| SMute | On / Off | 弱台软静音 |
| SBlend | On / Off | 弱立体声混成单声道 |
| RDS | On / Off | 欧洲 RDS |
| RBDS | On / Off | 北美 RBDS |
| AFC | On / Off | 自动频率控制，一般保持 On |

主界面 `i` 可看 RDS（PS / RT 等），仅 RDA 有效。

## 使用说明

1. 把现成 4 pin 模块插到**左侧 Grove**（G2=SDA、G1=SCL），耳机插模块孔。  
2. 主菜单按 `r` 进入。顶栏显示芯片名；刻度盘绿点=已存电台，青点=当前电台。  
3. 方向键搜台，或 `a` 自动扫描；`l` 管理电台。  
4. 按 `t` 进 Tuner 改频段 / 搜台方式（见上表）。  
5. 退出 App 时芯片 standby，省电。
