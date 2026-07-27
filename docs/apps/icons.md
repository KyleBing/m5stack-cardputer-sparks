# Icons 图标

进入方式：主菜单 `h` → Hardware Test `4`

浏览固件内 UI / 米家设备 / 红外 AC 等图标资源（含 off/on 与风速档）。

## 截图

<div class="shot-row">

![icons](/shots/app_icons.png)
![icons-ir-fan](/shots/app_icons_001.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| 方向键 · `;,.` `/` | 上一页 / 下一页 |
| `h` | Help |

## 使用说明

1. 分页查看已打包进 LittleFS 的图标。
2. 含设备图标与 IR AC 模式 / 风速资源。
3. 现场 RGB565 烘焙已改为 Config Web `POST /bake-rgb565`，本 App 不再提供 `b` 烘焙。流程与性能说明见 [图片处理与烘焙](/dev/images)。
