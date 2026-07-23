# Version 版本

主菜单按键：`v`

显示固件版本、作者
## 截图

<div class="shot-row">

![version-main](/shots/app_version.png)

</div>


当前文档对应：

| 字段 | 值 |
|------|-----|
| 版本 | **{{APP_VERSION}}** |
| 更新日期 | {{APP_UPDATE_TIME}} |
| 作者 | {{APP_AUTHOR}} |

## 快捷键

| 按键 | 作用 |
|------|------|
| `r` / `R` | 刷新烟花动画 |

## 使用说明

版本字符串定义在 `include/app_version.h`（`APP_VERSION` / `APP_UPDATE_TIME`）。发版时只改该头文件；M5Burner 打包脚本会读取同一来源。
