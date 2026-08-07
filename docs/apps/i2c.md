# I2C 扫描

进入方式：

- 主菜单 `h` → Hardware Test `7` — **InI2C** 内部总线  
- 主菜单 `h` → Hardware Test `8` — **ExI2C** 外部总线  

扫描地址 1–119，列出响应设备，并显示所用 SDA / SCL 脚。

## 截图

**InI2C / ExI2C**

<div class="shot-row">

![i2c-in](/shots/app_hardware_ini2.png)
![i2c-ex](/shots/app_hardware_exi2.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| `h` | Help（说明扫描范围与引脚） |

进入 App 即执行扫描；返回后可再进以重新扫描。

## 使用说明

1. **InI2C**：板载外设（如 IMU 等）地址确认。  
2. **ExI2C**：Grove / 扩展 I2C 设备排查。  
3. 无设备时应为空列表；接触不良会导致扫描不稳定。
