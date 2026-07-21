#!/usr/bin/env python3
"""将透明 PNG 转为黑底 RGB565（尽量接近屏上 drawPngFile 观感）。

做法：
  1. Alpha 正确合成到黑底（straight alpha over black）
  2. Floyd–Steinberg 抖动再量化到 RGB565（减轻截断断层）

输出：与 PNG 同目录的 .rgb565，小端序 uint16，行优先。
用法：
  python scripts/png_to_rgb565.py data/icon/device
  python scripts/png_to_rgb565.py data/icon/ir --no-dither
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("需要 Pillow：pip install Pillow", file=sys.stderr)
    sys.exit(1)


def clamp8(v: float) -> int:
    if v < 0:
        return 0
    if v > 255:
        return 255
    return int(v)


def pack_rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def composite_on_black(img: Image.Image) -> Image.Image:
    """透明 PNG → 不透明黑底 RGB（保留抗锯齿边缘的暗过渡）。"""
    rgba = img.convert("RGBA")
    black = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    # alpha_composite：按 straight alpha 叠在黑底上，比手写 multiply 更稳
    return Image.alpha_composite(black, rgba).convert("RGB")


def rgb_to_rgb565_trunc(rgb: Image.Image) -> bytes:
    """无抖动：直接截断（快，边缘易脏）。"""
    w, h = rgb.size
    px = rgb.load()
    out = bytearray(w * h * 2)
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            struct.pack_into("<H", out, i, pack_rgb565(r, g, b))
            i += 2
    return bytes(out)


def rgb_to_rgb565_floyd_steinberg(rgb: Image.Image) -> bytes:
    """Floyd–Steinberg 抖动到 RGB565，减轻渐变/抗锯齿断层。"""
    w, h = rgb.size
    # 用 float 缓冲做误差扩散
    buf = [[list(map(float, rgb.getpixel((x, y)))) for x in range(w)] for y in range(h)]
    out = bytearray(w * h * 2)
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b = buf[y][x]
            r0, g0, b0 = clamp8(r), clamp8(g), clamp8(b)
            # 量化到 565 对应的 8bit 代表值（用通道中点近似）
            q_r = (r0 >> 3) * 255 // 31
            q_g = (g0 >> 2) * 255 // 63
            q_b = (b0 >> 3) * 255 // 31
            struct.pack_into("<H", out, i, pack_rgb565(r0, g0, b0))
            i += 2
            er, eg, eb = r - q_r, g - q_g, b - q_b

            def diffuse(xx: int, yy: int, factor: float) -> None:
                if 0 <= xx < w and 0 <= yy < h:
                    buf[yy][xx][0] += er * factor
                    buf[yy][xx][1] += eg * factor
                    buf[yy][xx][2] += eb * factor

            # 标准 FS 核：右 7/16，左下 3/16，下 5/16，右下 1/16
            diffuse(x + 1, y, 7 / 16)
            diffuse(x - 1, y + 1, 3 / 16)
            diffuse(x, y + 1, 5 / 16)
            diffuse(x + 1, y + 1, 1 / 16)
    return bytes(out)


def convert_one(src: Path, dither: bool) -> Path:
    rgb = composite_on_black(Image.open(src))
    data = rgb_to_rgb565_floyd_steinberg(rgb) if dither else rgb_to_rgb565_trunc(rgb)
    out = src.with_suffix(".rgb565")
    out.write_bytes(data)
    mode = "FS-dither" if dither else "trunc"
    print(f"{src.name} -> {out.name} ({rgb.size[0]}x{rgb.size[1]}, {len(data)} bytes, {mode})")
    return out


def collect_pngs(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for p in paths:
        if p.is_dir():
            files.extend(sorted(p.glob("*.png")))
        elif p.suffix.lower() == ".png":
            files.append(p)
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description="PNG → 黑底 RGB565（默认 Floyd–Steinberg 抖动）")
    ap.add_argument("paths", nargs="+", type=Path, help="PNG 文件或目录")
    ap.add_argument("--no-dither", action="store_true", help="关闭抖动，仅截断量化")
    args = ap.parse_args()
    pngs = collect_pngs(args.paths)
    if not pngs:
        print("未找到 PNG", file=sys.stderr)
        return 1
    dither = not args.no_dither
    for src in pngs:
        convert_one(src, dither=dither)
    return 0


if __name__ == "__main__":
    sys.exit(main())
