#!/usr/bin/env python3
"""从已运行 Config 网页的设备拉取 M5GFX 烘焙的 .rgb565 到本地 data/。

流程：
  1. uploadfs 放入 PNG（设备 WiFi / Config 网页需在线）
  2. 本脚本 --bake：POST /bake-rgb565（设备端 M5GFX 烘焙）
  3. 再按路径下载 .rgb565 到本地 data/

用法：
  python scripts/pull_rgb565_from_device.py 192.168.1.20
  python scripts/pull_rgb565_from_device.py 192.168.1.20 --bake
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
RGB565_MAGIC = b"R565"


def rel_paths() -> list[str]:
    """按本地 data/ 里的 PNG 推出待拉取的同名 .rgb565，与设备端烘焙范围保持一致。"""
    sources = sorted((DATA / "icon").rglob("*.png"))
    sources += sorted(DATA.glob("logo_*.png"))
    return [str(p.relative_to(DATA).with_suffix(".rgb565")) for p in sources]


def http(url: str, method: str = "GET", timeout: float = 120.0) -> bytes:
    req = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def main() -> int:
    ap = argparse.ArgumentParser(description="拉取设备上 M5GFX 烘焙的 RGB565")
    ap.add_argument("base", help="设备 IP 或 Config 根地址，如 192.168.1.20")
    ap.add_argument("--bake", action="store_true", help="先 POST /bake-rgb565")
    args = ap.parse_args()
    base = args.base.rstrip("/")
    # 只填写 IP 或主机名时默认使用 HTTP，同时兼容完整 URL
    if not base.lower().startswith(("http://", "https://")):
        base = f"http://{base}"

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
        # 旧格式无头部，落盘会让固件读取失败，直接判为缺失以便重新烘焙
        if not data.startswith(RGB565_MAGIC):
            miss += 1
            print(f"miss {rel}: 非 R565 头部（设备上是旧格式，需重新烘焙）")
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        ok += 1
        print(f"ok   {rel} ({len(data)} bytes)")

    print(f"done: saved={ok} miss={miss}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
