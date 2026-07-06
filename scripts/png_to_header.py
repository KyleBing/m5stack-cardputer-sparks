# 将 PNG 转为 C 头文件，供 drawPng() 直接使用
Import("env")

import os

from pathlib import Path

PROJECT_DIR = env["PROJECT_DIR"]
SRC_PNG = Path(PROJECT_DIR) / "assets" / "img" / "logo.png"
OUT_H = Path(PROJECT_DIR) / "include" / "logo_png.h"
VAR_NAME = "logo_png"


def write_header() -> None:
    if not SRC_PNG.is_file():
        return

    data = SRC_PNG.read_bytes()
    lines = [
        f"// Auto-generated from assets/img/logo.png\n",
        "#pragma once\n\n",
        "#include <cstddef>\n",
        "#include <cstdint>\n\n",
        f"static const uint8_t {VAR_NAME}[] = {{\n",
    ]

    for index, byte in enumerate(data):
        if index % 12 == 0:
            lines.append("  ")
        lines.append(f"0x{byte:02x}, ")
        if index % 12 == 11:
            lines.append("\n")

    if len(data) % 12 != 0:
        lines.append("\n")

    lines.append("};\n\n")
    lines.append(f"static const size_t {VAR_NAME}_len = {len(data)};\n")
    OUT_H.write_text("".join(lines), encoding="utf-8")


write_header()
