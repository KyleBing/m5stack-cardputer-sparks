# Curves 方程曲线

进入方式：主菜单 `g` → Games `7`

九种常见曲线的实时采样连线。可调幅度 `a`、频率 `b`、相位 `p`，并开关相位动画。

## 截图

<div class="shot-row">

![curves-sin](/shots/app_games_curves_sin.png)
![curves-lissa](/shots/app_games_curves_lissa.png)

</div>

## 快捷键

| 按键 | 作用 |
|------|------|
| `1`–`9` | 切换曲线 |
| `-` `=` | 幅度 `a` − / +（0.2–2.5） |
| `,` `.` | 频率 `b` − / +（0.2–4.0） |
| `q` `e` | 相位 `p` − / + |
| `Space` / `GO` | 开关动画（`RUN` / `PAUSE`） |
| `r` | 重置参数并恢复动画 |
| `h` | Help |

## 曲线一览

| 键 | 名称 | 公式 |
|----|------|------|
| `1` | SIN | `a*sin(bx+p)` |
| `2` | COS | `a*cos(bx+p)` |
| `3` | PARA | `a*x^2` |
| `4` | CUBIC | `a*x^3` |
| `5` | EXP | `a*e^(bx)` |
| `6` | LOG | `a*ln(bx)` |
| `7` | CIRCLE | `x^2+y^2=a^2` |
| `8` | HEART | cardioid |
| `9` | LISSA | `a*sin(bt), a*sin(ct+p)` |

## 说明

- 左上显示曲线名与公式，右上为参数与 `RUN` / `PAUSE`。
- 动画开启时相位随频率自动推进；坐标系居中，曲线以青色折线绘制。
- 参数曲线（圆 / 心形 / 李萨如）采样更密（180 点），函数曲线为 120 点。
