#!/usr/bin/env python3
"""从已运行 Config 网页的设备拉取 M5GFX 烘焙的 .rgb565 到本地 data/。

流程：
  1. 设备上 Icons 应用按 b，或 POST /bake-rgb565（需 Config 网页在线）
  2. 本脚本按已知路径下载 .rgb565

用法：
  python scripts/pull_rgb565_from_device.py http://192.168.1.20
  python scripts/pull_rgb565_from_device.py http://192.168.1.20 --bake
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import urllib.request
except ImportError:
    print("需要 Python 3", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"

DEVICE_NAMES = [
    "airpurifier",
    "bslamp2",
    "camera",
    "cooker",
    "default",
    "fan",
    "fryer",
    "juicer",
    "lamp2",
    "light",
    "plug",
    "sensor_ht",
    "wifispeaker",
]
IR_NAMES = ["ac_cool", "ac_heat", "ac_dry", "ac_fan"]


def rel_paths() -> list[str]:
    paths: list[str] = []
    for n in DEVICE_NAMES:
        paths.append(f"icon/device/{n}.rgb565")
        paths.append(f"icon/device/{n}_active.rgb565")
        paths.append(f"icon/device/{n}_25w.rgb565")
        paths.append(f"icon/device/{n}_active_25w.rgb565")
    for n in IR_NAMES:
        paths.append(f"icon/ir/{n}.rgb565")
        paths.append(f"icon/ir/{n}_active.rgb565")
    paths.append("logo_60.rgb565")
    paths.append("logo_50.rgb565")
    return paths


def http(url: str, method: str = "GET", timeout: float = 120.0) -> bytes:
    req = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def main() -> int:
    ap = argparse.ArgumentParser(description="拉取设备上 M5GFX 烘焙的 RGB565")
    ap.add_argument("base", help="设备 Config 根地址，如 http://192.168.1.20")
    ap.add_argument("--bake", action="store_true", help="先 POST /bake-rgb565")
    args = ap.parse_args()
    base = args.base.rstrip("/")

    if args.bake:
        print("POST /bake-rgb565 ...")
        body = http(f"{base}/bake-rgb565", method="POST", timeout=180.0)
        print(body.decode("utf-8", errors="replace"))

    ok = 0
    miss = 0
    for rel in rel_paths():
        url = f"{base}/{rel}"
        dest = DATA / rel
        try:
            data = http(url, timeout=30.0)
        except Exception as e:
            miss += 1
            print(f"miss {rel}: {e}")
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        ok += 1
        print(f"ok   {rel} ({len(data)} bytes)")

    print(f"done: saved={ok} miss={miss}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
