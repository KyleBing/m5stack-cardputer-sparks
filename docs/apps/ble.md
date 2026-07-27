# BLE

进入方式：主菜单 `h` → Hardware Test `6`

扫描附近蓝牙低功耗设备，列表展示名称、地址与类型分类。

## 截图

**待扫描**

<div class="shot-row">

![ble-scan](/shots/app_ble_scan.png)

</div>

**设备列表**

<div class="shot-row">

![ble-list](/shots/app_ble_list.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| `s` | 开始 / 重新扫描 |
| 方向键 · `;,.` `/` | 翻页 |
| `[` `]` | 翻页（Help 中说明；主 tip 可能不显示） |
| `h` | Help（设备类型说明） |

## 使用说明

1. 进入后按 `s` 扫描，结果分页列出。
2. 列表用小号字体；序号与类别用不同颜色区分。
3. Help 中说明常见类型：普通设备 / beacon / ble-svc 等。
4. 米家 BLE 传感器请在 [Mijia](./mijia) 内对具体设备按 `r` 扫描，本 App 为通用附近设备浏览。
