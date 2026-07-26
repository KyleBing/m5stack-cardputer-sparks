# 入门

Sparks 是跑在 [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) 上的多应用固件。主界面按字母键进入对应 App，`ESC` / `GO` 返回菜单。

## 烧录与配置

1. 使用 PlatformIO 或 [M5Burner](https://docs.m5stack.com/en/download) 烧录固件与 LittleFS。
2. 参考根目录 `config.example.json`，将设备列表、WiFi、Cursor token 等写入 `data/config.json` 后执行 `uploadfs`，或通过 **Config** App 的 Web 页面在线编辑。
3. 开机进入主菜单 **Sparks**，按对应字母进入功能。

## 主菜单

<div class="shot-row">

![menu-page1](/shots/app_menu_page1.png)
![menu-page2](/shots/app_menu_page2.png)

</div>

| 键 | 短名 | 应用 | 键 | 短名 | 应用 | 键 | 短名 | 应用 |
|----|------|------|----|------|------|----|------|------|
| `m` | Mij | [Mijia](/apps/mijia) | `u` | Cfg | [Config](/apps/config) | `w` | WiFi | [WiFi](/apps/wifi) |
| `t` | Time | [Time](/apps/time) | `s` | Slp | [Sleep](/apps/sleep) | `o` | Opt | [Options](/apps/options) |
| `i` | Inf | [Info](/apps/info) | `p` | Bat | [Battery](/apps/battery) | `c` | Cur | [Cursor](/apps/cursor) |
| `v` | Ver | [Version](/apps/version) | `j` | Mor | [Morse](/apps/morse) | `x` | IR | [Infrared](/apps/infrared) |
| `k` | KB | [Keyboard](/apps/hid-keyboard) | `g` | IMU | [IMU](/apps/imu) | `l` | LED | [RGB LED](/apps/rgb-led) |
| `r` | Mic | [Mic](/apps/mic) | `y` | FX | [Neon FX](/apps/neon-fx) | `z` | Dice | [Dice](/apps/dice) |
| `q` | Phys | [Newton Cradle](/apps/newton-cradle) |  |  |  |  |  |  |
| `b` | BLE | [BLE](/apps/ble) | `d` | Disp | [Display](/apps/display) | `a` | Icn | [Icons](/apps/icons) |
| `f` | Fnt | [Font](/apps/font) | `n` | InI2 | [InI2C](/apps/i2c) | `e` | ExI2 | [ExI2C](/apps/i2c) |

菜单可翻页： 对应键盘上的上下左右 箭头键，无需按 **Fn** 。

## 下一步

- [功能目录](/apps/) — 按分类浏览全部 App  
- [全局快捷键](/guide/shortcuts) — 截图、翻页、返回等
