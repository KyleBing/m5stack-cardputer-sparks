# Infrared 红外

主菜单按键：`x`

通过 GPIO44 发射红外，支持 **TV** 与 **AC** 两类遥控。默认类别与品牌来自配置 `infrared`。

## 截图

**TV**

<div class="shot-row">

![infrared-tv-sony](/shots/app_ir_tv_sony.png)
![infrared-tv-lg](/shots/app_ir_tv_lg.png)

</div>

**AC**

<div class="shot-row">

![infrared-ac-mi](/shots/app_ir_ac_mi.png)
![infrared-ac-gree](/shots/app_ir_ac_gree.png)
![infrared-ac-help](/shots/app_ir_ac_help.png)

</div>

## 快捷键

主界面无底栏 tip（按键印在面板上）；完整说明见 `h` Help。

| 按键 | 作用 |
|------|------|
| `h` | Help |
| `Tab` | 切换品牌 |
| `t` | TV ↔ AC |
| `p` | 电源 |
| `-` | TV：音量− · AC：温度− |
| `=` | （对称调节，见面板） |
| `[` | TV：频道相关 |
| **BtnGO** / `Space` / `Enter` | 发送当前动作 |
| TV：`m` / `i` | Mute / Input 等 |
| AC：`m` / `f` | 模式 / 风速 |

具体动作以屏上按键垫与 Help 为准。

## 使用说明

### TV

支持品牌：Samsung、Sony、LG、Panasonic、NEC、Xiaomi、Hisense。

常见动作：Power、Vol±、Mute、Ch±、Input。选好品牌与动作后发送。

> Xiaomi 使用小米电视专用 Xiaomi IR 协议（多数 Mi TV / Mi Box）；部分机型仅蓝牙遥控、无红外接收。Ch± 对应 Page Up/Down。
>
> Hisense 使用 NEC（customer `04FB`，官方 discrete / VIDAA 常用码）；部分老款 EN* 遥控为 `00BF` 址，无响应时可试通用 NEC 品牌。

### AC

支持品牌：Midea、Gree、Haier、AUX、Hisense、Xiaomi。

Midea 有些无响应的，改成 Xiaomi 品牌就能控制了。

- 模式图标：制冷 / 制热 / 除湿 / 送风 / Auto  
- 顶栏风速图标：auto / min / low / med / high / max  
- 调节温度、模式、风速后发送整帧状态  

默认项可在 [Options](./options) 或 Config Web 修改：`infrared.default`（`tv`/`ac`）、`tv_brand`、`ac_brand`。在红外界面用 `t` / `Tab` 切换后只在退出应用时写入 config，避免操控卡顿。进入时显示 `Loading...`，退出写盘时显示 `Saving config...`，保存完成后再回主页。

> 红外协议因机型而异；无效时请换品牌或确认发射头对准接收窗。
