# I2C 扫描

进入方式：

- 主菜单 `h` → Hardware Test `7` / `i` — **InI2C** 内部总线  
- 主菜单 `h` → Hardware Test `8` / `e` — **ExI2C** 外部总线  
- 主菜单 `e` → [EX I2C](./ex-i2c) `2` / `i` — **EXI2**（与 Hardware Test ExI2 同一套扫描）

无 header。扫描地址 8–119，按行列出 `0x` 地址、芯片名与用途，并在标题旁显示 SDA / SCL。行前 4x4 圆点：绿色=地址有已知映射，灰色=未知。外接芯片接口与驱动 API 见 [EX I2C](./ex-i2c)。

- **InI2**：板载设备（Adv：`0x18` ES8311 codec、`0x34` TCA8418 键盘、`0x68` BMI270 IMU）。
- **ExI2**：Grove / EXT 外设；芯片名为常见模块猜测（如 `0x10/0x11` RDA5807M、`0x60` TEA5767）。

## 截图

**InI2C / ExI2C**

<div class="shot-row">

![i2c-in](/shots/app_hardware_ini2.png)
![i2c-ex](/shots/app_hardware_exi2.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| `h` | Help（In 为确定芯片；Ex 为引脚与猜测说明） |
| `r` | 重新扫描 |

进入 App 即执行扫描；`r` 可在不离开的情况下再扫。扫描可能短暂打开 Grove 收音机，扫完会 mute + standby，退出 App 时再停一次。

## 使用说明

1. **InI2**：确认板载 I2C（键盘 / IMU / codec）。  
2. **ExI2**：机身**左侧 Grove**（上→下 GND / 5V / G2=SDA / G1=SCL）或 EXT（G8=SDA G9=SCL）外设排查。收音机接线见 [Radio](./radio)。  
3. 无设备显示 `no device`；未知地址显示 `--` / `unknown`。
