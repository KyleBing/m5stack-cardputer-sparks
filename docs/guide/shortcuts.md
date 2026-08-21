# 全局快捷键

多数界面共用下列按键；各 App 内还有专属快捷键，见对应文档页。

## 导航

| 按键 | 作用 |
|------|------|
| 字母键（主菜单） | 进入对应 App |
| `ESC` / `GO`（侧键） | 返回主菜单；Help 打开时先关闭 Help（Keyboard 用 **长按 BtnGO** 退出） |
| `h` | 任意界面打开 / 关闭 Help（Keyboard 为 `Fn` + `h`） |
| `;` `,` / ↑ / ← | 上一页 |
| `.` `/` / ↓ / → | 下一页 |
| `[` `]` | 部分列表/宫格翻页（以各 App tip / Help 为准） |

## 截图

| 按键 | 作用 |
|------|------|
| `Fn` + `s` | 将当前屏幕保存为 PNG |

- 优先写入 TF 卡；否则写入 Flash LittleFS：`/shot/<组>_<app>_<功能>.png`（如 `exi2c_gps_live.png`；重名加 `_002`）
- 空间不足时**不删除**已有截图，仅提示 `no space for shot`
- 成功后屏幕反色闪一下；提示音由 Options → Sound → `screenshot`（`sound.screenshot`）控制
- 在 [Config](/apps/config) Web 的 `/shots` 可预览、单张下载 / 删除、清空 TF / Flash
- Info → Storage 按 `c` 可清除全部截图（`y` 确认 / `n` 取消）

## 顶栏

主菜单与多数 App 顶栏会显示 WiFi / BLE 状态、电池、分页圆点等信息；返回一律用 `ESC` / `GO`，顶栏不再画返回图标。

## 提示音

部分操作（如米家开关、时间按键、截图）可通过 [Options](/apps/options) 或 Config Web 开关提示音，并调节喇叭音量 `sound.volume`（0–100）。
