# NFC

进入方式：主菜单 `e` → [EX I2C](./ex-i2c) `4` / `n`

左侧 Grove 上的 **Unit NFC（ST25R3916）**：读 / 写 13.56 MHz 卡、NDEF 模拟、读卡历史。走 `Ex_I2C`（G2=SDA、G1=SCL），与 Radio / I2C 扫描同一口。

> 需要 **Unit NFC**，不是 Unit RFID（U031 / `0x28`）。仅 13.56 MHz；受保护块需匹配密钥。

## 模块与接线

插到机身**左侧 Grove**（HY2.0-4P），按丝印对 **SDA / SCL / VCC / GND**：

| 模块 | 左侧 Grove |
|------|------------|
| **SDA** | **G2** |
| **SCL** | **G1** |
| **VCC** | **5V** |
| **GND** | **GND** |

进入后初始化 Unit；失败显示 `unit begin failed`。与 GPS 不能同时占 Grove（GPS 会把同脚切成 UART）。

## 界面

| 视图 | 说明 |
|------|------|
| Reader | 状态 / UID / 类型 / 卡信息 / NDEF；读后可翻页看 dump |
| History | 最近最多 12 条读卡记录（LittleFS） |
| Detail | 单条完整记录，可再写回卡片 |
| Rename | 改历史条目名称 |
| Emulation | 模拟 NDEF 标签（默认文本 `Cardputer NFC`） |

## 快捷键

完整说明见 `h` Help（多页）。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `r` | Reader：扫描直到读到卡；History：重命名选中项 |
| `w` | 把当前 payload 写入卡片（Detail 也可写） |
| `o` | 开关「读卡自动进历史」 |
| `y` | 打开 / 关闭 History |
| `e` | 进入 / 退出 NDEF 模拟 |
| ↑ ↓ 等 | Reader dump 翻页；History 选条目 |
| `Enter` | History → Detail |
| `ESC` | Detail → History → Reader（再按回 EX I2C） |

默认写卡内容为 `Cardputer NFC`（NDEF Text）。受保护扇区无正确密钥时读 / 写会失败。

## 使用说明

1. Unit NFC 插左侧 Grove，主菜单 `e` → `n`（或 `4`）。  
2. `r` 贴卡扫描；成功后看 UID / 类型 / NDEF，箭头翻 dump。  
3. `o` 控制是否自动记入 History；`y` 浏览，`Enter` 看详情，`r` 改名，`w` 写回。  
4. `e` 开标签模拟，用手机 NFC 可读到默认文本；再按 `e` 回 Reader。  
5. 退出 App 会停扫描 / 模拟并释放 I2C。
