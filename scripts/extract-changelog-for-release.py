#!/usr/bin/env python3
"""从 CHANGELOG.md 提取与 git tag 对应的版本段落，供 GitHub Release 使用。"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "CHANGELOG.md"


def extract_section(tag: str, text: str) -> str | None:
    """匹配 `## … — v1.13` 标题，取到下一个 `---` 分隔线之前。"""
    if not tag.startswith("v"):
        tag = f"v{tag}"

    pattern = rf"^## [^\n]*—\s*{re.escape(tag)}\s*$"
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines) if re.match(pattern, line)), None)
    if start is None:
        return None

    out: list[str] = []
    for line in lines[start + 1 :]:
        if line.strip() == "---":
            break
        out.append(line)

    section = "\n".join(out).strip()
    return section if section else None


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <tag>", file=sys.stderr)
        return 2

    tag = sys.argv[1].strip()
    if not CHANGELOG.is_file():
        print(f"missing {CHANGELOG}", file=sys.stderr)
        return 1

    text = CHANGELOG.read_text(encoding="utf-8")
    header_pattern = rf"^## [^\n]*—\s*{re.escape(tag if tag.startswith('v') else f'v{tag}')}\s*$"
    header = next(
        (line for line in text.splitlines() if re.match(header_pattern, line)),
        None,
    )
    body = extract_section(tag, text)

    if header is None or body is None:
        print(f"no CHANGELOG section for {tag}", file=sys.stderr)
        return 1

    print(header)
    print()
    print(body)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
