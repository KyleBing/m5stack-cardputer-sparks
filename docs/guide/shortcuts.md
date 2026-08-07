# 全局快捷键

多数界面共用下列按键；各 App 内还有专属快捷键，见对应文档页。

## 导航

| 按键 | 作用 |
|------|------|
| 字母键（主菜单） | 进入对应 App |
| `ESC` / `GO`（侧键） | 返回主菜单（Keyboard 用 **BtnGO** 退出） |
| `;` `,` / ↑ / ← | 上一页 |
| `.` `/` / ↓ / → | 下一页 |
| `[` `]` | 部分列表/宫格翻页（以各 App tip / Help 为准） |

## 截图

| 按键 | 作用 |
|------|------|
| `Fn` + `s` | 将当前屏幕保存为 PNG |

- 优先写入 TF 卡；否则写入 Flash LittleFS：`/shot/app_<界面>_NNN.png`
- 空间不足时自动删除最旧截图腾地方
- 成功后屏幕反色闪一下；提示音由 Options → Sound → `screenshot`（`sound.screenshot`）控制
- 在 [Config](/apps/config) Web 的 `/shots` 可预览、下载、清空 TF / Flash 分区截图

## 顶栏

主菜单与多数 App 顶栏会显示 WiFi / BLE 状态、电池、分页圆点等信息；返回一律用 `ESC` / `GO`，顶栏不再画返回图标。

## 提示音

部分操作（如米家开关、时间按键、截图）可通过 [Options](/apps/options) 或 Config Web 开关提示音，并调节喇叭音量 `sound.volume`（0–100）。
