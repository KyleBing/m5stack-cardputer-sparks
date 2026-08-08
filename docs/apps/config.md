# Config 配网

主菜单按键：`u`

通过设备自发 AP 或者 连接WiFi， 设备变成一个 Web 服务器，同局域网电脑可以通过访问 cardputer ip 或者 ap 地址访问这个配置网页。

能实现 设备、编组、WiFi、亮度、红外默认、截图下载等操作，这些配置最终都被保存在 `config.json` 配置文件中。这个文件位于设备 flash 的存储区中。

## 截图

**LAN / AP / Help**

<div class="shot-row">

![config-lan](/shots/app_config_lan.png)
![config-ap](/shots/app_config_ap.png)
![config-help](/shots/app_config_help.png)

</div>

## 快捷键

| 状态 | 按键 | 作用 |
|------|------|------|
| 连接中 / AP | `a` | 跳过 LAN，切到 AP 热点 |
| LAN 已就绪 | `a` | 切换为 AP 热点模式 |
| 失败 | 重新进入 `u` | 重试 |
| 任意 | `h` | Help |
| Help 内 | `a` | AP |
| Help 内 | `l` | 重试 LAN |

返回菜单：`ESC` / `GO`。

## 使用说明

1. 进入 App 后优先尝试用已保存的 WiFi 连 LAN；失败或按 `a` 则开 SoftAP。
2. 屏上会显示 IP 或热点 SSID；用手机 / 电脑浏览器访问该地址。
3. Web 常见入口：
   - 设备与编组编辑
   - `/ac-auto`：空调自动化（`ac_auto`）
   - `/wifi`：多 WiFi 档案（最多 5 条）与 Active
   - `/shots`：截图预览、单张下载 / 删除、清空 TF / Flash
   - `/about`：固件版本信息
   - RGB565 烘焙：`POST /bake-rgb565`（现场生成图标 bake 文件；说明见 [图片处理与烘焙](/dev/images)）
4. 修改保存后写入 LittleFS；部分项（如反色、音量）会立即生效。

配网完成后可按 `ESC` 回菜单，再进 [Mijia](./mijia) / [WiFi](./wifi) / [AC Auto](./ac-auto) 使用。


## 米家设备管理

通过网页管理工具，还可以手动添加一些设备信息，设置一些设备分组。

<img alt="web-config-mijia-devices" src="https://github.com/user-attachments/assets/63a85026-83ec-458f-b945-0bfe19dd0c49" />

<img alt="web-config-mijia-device-group" src="https://github.com/user-attachments/assets/4a143e74-a2f3-445f-8ee7-0cc6ec53f8c5" />

## 空调自动化（`/ac-auto`）

Web 导航 **空调自动化** 对应配置键 `ac_auto`：选 BLE 温湿度计、开/关温度阈值、过滤次数，以及开机时的红外品牌 / 模式 / 设定温度 / 风速。

设置表单下方有**运行机制**说明，要点：

1. 设备主菜单按 `n` 进入 [AC Auto](./ac-auto)，再按 `t` 启动 AUTO 后才会发红外。
2. 温度 **>** `on_temp` 连续 `filter` 次 → 开空调；**<** `off_temp` 连续 `filter` 次 → 关空调。
3. 落在两阈值之间时清空连续计数（滞回），避免临界温度反复开关。
4. BLE 进入 App 即按「听约 6 分钟 / 歇约 4 分钟」节奏收数，与 AUTO 开/关无关。

完整键位与界面说明见 [AC Auto 空调自动化](./ac-auto)。

