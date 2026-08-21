# NFC

进入方式：主菜单 `e` → [Grove](./ex-i2c) `4` / `n`

左侧 Grove 上的 **Unit NFC（ST25R3916）**：读 / 写 13.56 MHz 卡、NDEF / 历史卡模拟、读卡历史。走 `Ex_I2C`（G2=SDA、G1=SCL），与 Radio / I2C 扫描同一口。

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
| Reader | 左侧状态图标；右侧大号 UID、ATQA/SAK、卡型、USR/TOT；`blk` 点阵与进度条对齐（绿=成功 / 红=失败）；翻页仅列出可读块 Hex |
| History | 最近最多 12 条（GPS 风格列表 + 滚动条） |
| Detail | 单条完整记录，可再写回卡片或模拟 |
| Rename | 改历史条目名称 |
| Emulation | 默认 NDEF 文本标签，或完整模拟历史中的 Type2 卡 |

## 快捷键

完整说明见 `h` Help（多页）。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `r` | Reader：扫描直到读到卡；History：重命名选中项 |
| `w` | 把当前 payload 写入卡片（Detail 也可写） |
| `o` | 开关「读卡自动进历史」 |
| `l` | 打开 / 关闭 History |
| `e` | Reader：默认 NDEF 文本模拟；History / Detail：用选中记录的 UID + 完整 dump 模拟；模拟中再按退出 |
| ↑ ↓ 等 | Reader dump 翻页；History 选条目 |
| `Enter` | History → Detail |
| `ESC` | 模拟 / Detail → History → Reader（再按回 Grove） |

默认写卡内容为 `Cardputer NFC`（NDEF Text）。受保护扇区无正确密钥时读 / 写会失败。

读到 **MIFARE Classic** 时，会先用默认 KeyA `FFFFFFFFFFFF` 认证并读 **blk 4**（第 1 扇区第 0 块），再继续整卡 dump：

| 结果 | 含义 |
|------|------|
| `default key` | 扇区仍是出厂默认密钥 |
| `Auth Error` | 密钥已改（常见于物业门禁卡） |
| `n/a (not Classic)` | 非 Classic，跳过探测 |

状态行与 `key` 行都会显示该结果，并写入 History。

## 模拟范围

- **支持**：MIFARE Ultralight 系列、NTAG 2xx（Type 2）。读卡时保存完整页镜像；History / Detail 按 `e` 按原 UID + 内存回放。
- **不支持**：MIFARE Classic / Plus / DESFire 等（库只实现 Type2 监听）。此类记录会提示 `type not emulatable`。

## 使用说明

1. Unit NFC 插左侧 Grove，主菜单 `e` → `n`（或 `4`）。  
2. `r` 贴卡扫描；成功后看 UID / 类型 / 密钥探测 / NDEF，箭头翻 dump。  
3. `o` 控制是否自动记入 History；`l` 浏览，`Enter` 看详情，`r` 改名，`w` 写回，`e` 模拟。  
4. Reader 下 `e` 开默认文本标签模拟；History / Detail 下 `e` 克隆已存 Type2 卡；再按 `e` 或 `ESC` 退出模拟。  
5. 退出 App 会停扫描 / 模拟并释放 I2C。
