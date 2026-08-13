# Mini Games 小游戏集合

主菜单按键：`g`

十四个全屏互动小应用的集合，通过数字键或字母快捷键快速进入。选择页每页 8 项，用 `[` `]` 或方向键翻页，数字键按当前页从 `1` 重新编号；字母快捷键**不依赖当前页**即可直达。子游戏中按 `ESC` / `GO` 返回游戏选择页；在选择页再次按下才返回主菜单。

## 截图

**选择页**

<div class="shot-row">

![games-menu-001](/shots/app_games_menu_001.png)
![games-menu-002](/shots/app_games_menu_002.png)

</div>

## 第 1 页

| 按键 | 游戏 | 操作 |
|------|------|------|
| `1` / `c` | [Coin Toss 硬币](./coin-toss) | `Space` / `GO` 或摇晃设备抛掷 |
| `2` | [Double Pendulum 双摆混沌](./double-pendulum) | `Space` / `GO` 重置，`r` 随机初始姿态 |
| `3` / `w` | [Prize Wheel 抽奖轮](./prize-wheel) | 长按 `Space` / `GO` 蓄力旋转，摇晃中等力度；`-` / `=` 调整项目数（2–12，转动中亦可，会重置） |
| `4` / `d` | [Dice 骰子](./dice) | 长按 `Space` / `GO` 蓄力或摇晃投掷，`-` / `=` 调整骰子数 |
| `5` | [Newton Cradle 牛顿摆](./newton-cradle) | `1`–`3` 释放钢球，`Space` / `GO` 重放，`r` 重置 |
| `6` | [Neon FX](./neon-fx) | `EASD`、`m`、`c`、`-` / `=`、`r`；`Space` / `GO` 脉冲闪光 |
| `7` | [Curves 方程曲线](./curves) | `1`–`9` 切换曲线，`-` / `=` 幅度，`,` / `.` 频率，`q` / `e` 相位，`Space` / `GO` 动画开关 |
| `8` / `m` | [Minesweeper 扫雷](./minesweeper) | 方向键移动，`]` / `Space` 挖开 / 和弦，`[` / `f` 插旗，`1`–`3` 难度，`b` 记录 |

## 第 2 页

| 按键 | 游戏 | 操作 |
|------|------|------|
| `1` / `s` | [Snake 贪吃蛇](./snake) | 方向键 / `EASD` 转向，`Space` 暂停，`m` 撞墙 / 穿墙，`-` / `=` 速度 |
| `2` / `l` | [Conway Life 生命游戏](./conway-life) | `Space` 运行 / 暂停，`n` 单步，`r` 随机，`1`–`6` 图案，`Enter` 编辑格子 |
| `3` / `x` | [MATRIX 代码雨](./matrix) | `-` / `=` 调速，`Space` / `GO` 脉冲爆发，`r` 重排 |
| `4` / `v` | [WAVE 丝波](./wave) | `-` / `=` 调速，`c` 配色，`Space` / `GO` 幅度脉冲 |
| `5` / `t` | [PCLOCK 粒子时钟](./particle-clock) | `Space` / `GO` / `r` 重排，`m` 切换 `HH:MM` / 带秒 |
| `6` | [LISSA 利萨茹](./lissa) | `-` / `=` 调速，`c` 配色，`Space` / `GO` 相位脉冲 |

## 字母快捷键

| 按键 | 游戏 |
|------|------|
| `c` | Coin |
| `w` | Wheel |
| `d` | Dice |
| `m` | Minesweeper |
| `s` | Snake |
| `l` | Life |
| `x` | MATRIX |
| `v` | WAVE（`w` 已用于 Wheel） |
| `t` | PCLOCK |

## 通用快捷键

| 按键 | 作用 |
|------|------|
| `1`–`8` | 在游戏选择页进入当前页对应游戏 |
| 字母（见上） | 任意页直达对应游戏 |
| `[` `]` / 方向键 | 选择页翻页 |
| `h` | 打开当前游戏的独立帮助 |
| `ESC` / `GO` | 子游戏返回选择页；选择页返回主菜单 |

游戏画面不常驻显示操作 tip，所有控制说明集中在各自的 `h` 帮助页。硬币、抽奖轮和骰子使用 BMI270。侧边物理键 `GO`（BtnA）与 `Space` 在抛掷、蓄力、重放、脉冲、挖开等操作上同效（返回主菜单仍用 `ESC` / `GO` 键盘映射）。

扫雷与贪吃蛇的成绩保存在 Flash（`/mines_rec.dat`、`/snake_rec.dat`），重启与刷新固件都不会丢失。
