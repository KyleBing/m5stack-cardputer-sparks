# AC Auto 空调自动化

主菜单按键：`n`

根据米家 BLE 温湿度计读数，在温度过高 / 过低时自动发红外开 / 关空调。配置保存在 `config.json` 的 `ac_auto` 对象中，可在本机 [Options](./options) 或 [Config](./config) Web（`/ac-auto`）修改。

## 截图

<div class="shot-row">

![acauto-main](/shots/app_acauto_main.png)
![acauto-config](/shots/app_acauto_config.png)

</div>

## 前置条件

1. 在 Config Web「设备」中已添加温湿度计（`model` 含 `sensor_ht` / `.ht.`），并填好 `ble.key`。
2. 红外发射头对准空调接收窗；品牌与 [Infrared](./infrared) AC 协议一致（无效时换品牌试）。
3. 配置好 `ac_auto`：传感器、`on_temp` / `off_temp`、`filter`、空调 brand / mode / temp / fan。

## 快捷键

| 按键 | 作用 |
|------|------|
| `t` | 启动 / 停止 AUTO（自动化开关） |
| `c` | 展示页 ↔ 本机配置页 |
| `s` / **BtnA** | 息屏；任意键或再按 BtnA 唤醒 |
| `r` | 清零 on / off 连续计数 |
| `p` | 切换假定的空调开 / 关状态（仅改图标，**不发红外**） |
| `h` | Help（3 页：键位 / 参数 / 机制） |
| `,` `.` `[` `]` | Help 内翻页 |
| 配置页 `;` `.` | 上下选行 |
| 配置页 `-` `=` | 改当前项 |

返回菜单：`ESC` / `GO`。

## 运行机制

进入 App 后会立刻按节奏听 BLE（与 AUTO 是否开启无关），有读数就更新温湿度与 12 小时温度曲线。

App **不探测**真实空调开关；进 App 时内部默认视为 **关机**。若空调实际已开，按 `p` 把右侧电源图标对齐为 ON，之后低温才会发关机红外。

只有按 `t` 点亮 **AUTO** 后，才会根据温度发红外：

| 条件 | 动作 |
|------|------|
| 温度 **>** `on_temp`，连续 `filter` 次，且当前为关机 | 发红外 **开机**（使用配置的 mode / temp / fan） |
| 温度 **<** `off_temp`，连续 `filter` 次，且当前为开机 | 发红外 **关机** |
| 温度在 `off_temp`～`on_temp` 之间 | 开 / 关连续计数清零（滞回，减少抖动开关） |

默认示例：`on_temp=29`、`off_temp=26`、`filter=3` → 高于 29℃ 连读 3 次才开，低于 26℃ 连读 3 次才关。

### BLE 节奏

多数温湿度计大约数分钟广播一次。App 内策略：

- 一轮最长监听约 **6 分钟**
- 收到有效读数后休眠约 **4 分钟** 再听
- 首包后再短听一会，方便攒够 `filter` 次

### 界面要点

- 顶栏：AUTO 状态、电量、BLE 监听指示
- 左：当前温度 / 湿度；右：累计开 / 关次数与空调电源图标
- 曲线：温度轨迹 + on / off 阈值横线

## 配置项（`ac_auto`）

| 字段 | 含义 | 典型范围 |
|------|------|----------|
| `sensor_id` | 选用的温湿度计设备 `id` | 与 `devices[]` 对应 |
| `on_temp` | 高于此温度计开空调（℃） | 16–40，默认 29 |
| `off_temp` | 低于此温度计关空调（℃） | 10–35，默认 26 |
| `filter` | 连续满足次数后才动作 | 1–10，默认 3 |
| `ac_brand` | 红外空调品牌 | midea / gree / … |
| `ac_mode` | 开机模式 | cool / heat / dry / fan / auto |
| `ac_temp` | 开机设定温度（℃） | 16–30 |
| `ac_fan` | 开机风速 | auto / min / low / med / high / max |

Web 配置页在设置表单下方也有同样的运行机制说明。
