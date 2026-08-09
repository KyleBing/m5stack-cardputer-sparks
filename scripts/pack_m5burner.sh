#!/usr/bin/env bash
# 编译固件 + LittleFS，生成 M5Burner zip 与可一键刷的 merged.bin
#
# LittleFS 固定打入 config.example.json，不会把本地 data/config.json（密钥等）打进发布包；
# 打包结束后会原样恢复你的本地测试配置。
#
# 用法:
#   ./scripts/pack_m5burner.sh           # 编译并打包
#   ./scripts/pack_m5burner.sh --skip-build  # 跳过固件编译，仍会用 example 重打 FS
#
# 输出:
#   dist/m5burner/                 # M5Burner 目录结构
#   dist/Sparks-<ver>.zip          # 可导入 / 发布的 zip
#   dist/sparks_merged.bin         # esptool 从 0x0 整片写入

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENV_NAME="m5stack-cardputer"
BUILD_DIR="${ROOT}/.pio/build/${ENV_NAME}"
META_SRC="${ROOT}/m5burner/m5burner.json"
VERSION_H="${ROOT}/include/app_version.h"
DIST="${ROOT}/dist"
OUT_DIR="${DIST}/m5burner"
FW_DIR="${OUT_DIR}/firmware"

BOOT_APP0="${HOME}/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

# ESP32-S3 / StampS3（partitions/no_ota_8MB.csv）烧录地址
ADDR_BOOTLOADER=0x0
ADDR_PARTITIONS=0x8000
ADDR_BOOT_APP0=0xe000
ADDR_FIRMWARE=0x10000
ADDR_LITTLEFS=0x340000


SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    -h|--help)
      sed -n '2,15p' "$0"
      exit 0
      ;;
    *)
      echo "未知参数: $arg" >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "$META_SRC" ]]; then
  echo "缺少 $META_SRC" >&2
  exit 1
fi
if [[ ! -f "$VERSION_H" ]]; then
  echo "缺少 $VERSION_H" >&2
  exit 1
fi

# 选一个能跑的 Python（避开 Windows Store stub / 失效的 python3）
pick_python() {
  local cand
  for cand in \
    "$(command -v python.exe 2>/dev/null || true)" \
    "$(command -v python3.exe 2>/dev/null || true)" \
    "$(command -v python 2>/dev/null || true)" \
    "$(command -v python3 2>/dev/null || true)"
  do
    [[ -n "$cand" ]] || continue
    if "$cand" -c 'print(1)' >/dev/null 2>&1; then
      echo "$cand"
      return 0
    fi
  done
  echo "未找到可用的 Python（python/python3）" >&2
  return 1
}
PYTHON_BIN="$(pick_python)"

# 版本/作者等以 include/app_version.h 为准，打包时写入 m5burner.json
# 用多行 read（兼容 macOS bash 3.2，不用 mapfile）
{
  read -r VERSION
  read -r UPDATE_TIME
  read -r AUTHOR
  read -r EMAIL
  read -r WEBSITE
} < <("$PYTHON_BIN" - "$VERSION_H" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()

def grab(name: str) -> str:
    m = re.search(rf'static constexpr const char\*\s+{name}\s*=\s*"([^"]*)"', text)
    if not m:
        raise SystemExit(f"app_version.h 缺少 {name}")
    return m.group(1)

for key in ("APP_VERSION", "APP_UPDATE_TIME", "APP_AUTHOR", "APP_EMAIL", "APP_WEBSITE"):
    print(grab(key))
PY
)
# Git Bash / Windows 读入可能带 CRLF
VERSION="${VERSION//$'\r'/}"
UPDATE_TIME="${UPDATE_TIME//$'\r'/}"
AUTHOR="${AUTHOR//$'\r'/}"
EMAIL="${EMAIL//$'\r'/}"
WEBSITE="${WEBSITE//$'\r'/}"
if [[ -z "$VERSION" || -z "$AUTHOR" ]]; then
  echo "从 $VERSION_H 解析版本失败" >&2
  exit 1
fi
ZIP_NAME="Sparks-${VERSION}.zip"
echo "==> 版本来自 app_version.h: ${VERSION} (${UPDATE_TIME}) by ${AUTHOR}"

# 发布包用 example，绝不带入本地测试 config.json
CFG_DATA="${ROOT}/data/config.json"
CFG_EXAMPLE="${ROOT}/config.example.json"
# 备份必须在 data/ 外，否则 buildfs 会把 packbak 打进发布包
CFG_BAK="${ROOT}/.pack_config.json.bak"
CFG_HAD_LOCAL=0

restore_local_config() {
  if [[ -f "$CFG_BAK" ]]; then
    mv -f "$CFG_BAK" "$CFG_DATA"
    echo "==> 已恢复本地 data/config.json"
  elif [[ "$CFG_HAD_LOCAL" -eq 0 ]]; then
    # 打包前本来就没有本地配置，清掉临时拷贝的 example
    rm -f "$CFG_DATA"
  fi
}

prepare_example_config() {
  if [[ ! -f "$CFG_EXAMPLE" ]]; then
    echo "缺少 $CFG_EXAMPLE" >&2
    exit 1
  fi
  if [[ -f "$CFG_DATA" ]]; then
    CFG_HAD_LOCAL=1
    mv -f "$CFG_DATA" "$CFG_BAK"
  fi
  cp "$CFG_EXAMPLE" "$CFG_DATA"
  echo "==> LittleFS 使用 config.example.json（本地 config 已暂存）"
}

cd "$ROOT"
trap restore_local_config EXIT

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo "==> 编译固件"
  pio run -e "$ENV_NAME"
fi

# 无论是否 skip-build，都用 example 重打 LittleFS，避免旧产物夹带密钥
prepare_example_config
echo "==> 打包 LittleFS (data/ + example config)"
pio run -e "$ENV_NAME" -t buildfs
restore_local_config
trap - EXIT

for f in bootloader.bin partitions.bin firmware.bin littlefs.bin; do
  if [[ ! -f "${BUILD_DIR}/${f}" ]]; then
    echo "缺少产物: ${BUILD_DIR}/${f}（先编译或去掉 --skip-build）" >&2
    exit 1
  fi
done
if [[ ! -f "$BOOT_APP0" ]]; then
  echo "缺少 boot_app0.bin: $BOOT_APP0" >&2
  exit 1
fi

echo "==> 组装 M5Burner 目录"
rm -rf "$OUT_DIR"
mkdir -p "$FW_DIR"
# 模板 + app_version.h 字段 → 发布用 m5burner.json
"$PYTHON_BIN" - "$META_SRC" "${OUT_DIR}/m5burner.json" \
  "$VERSION" "$UPDATE_TIME" "$AUTHOR" "$EMAIL" "$WEBSITE" <<'PY'
import json, sys
src, dst, ver, updated, author, email, website = sys.argv[1:8]
meta = json.load(open(src, encoding="utf-8"))
meta["version"] = ver
meta["author"] = author
# description 附带更新日期与联系方式，便于 M5Burner 展示
base = meta.get("description") or "M5Stack Cardputer multi-app firmware."
meta["description"] = f"{base} v{ver} ({updated}) · {author} · {email} · {website}"
json.dump(meta, open(dst, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
open(dst, "a", encoding="utf-8").write("\n")
PY
cp "${BUILD_DIR}/bootloader.bin"  "${FW_DIR}/bootloader_${ADDR_BOOTLOADER}.bin"
cp "${BUILD_DIR}/partitions.bin"  "${FW_DIR}/partitions_${ADDR_PARTITIONS}.bin"
cp "$BOOT_APP0"                   "${FW_DIR}/boot_app0_${ADDR_BOOT_APP0}.bin"
cp "${BUILD_DIR}/firmware.bin"    "${FW_DIR}/firmware_${ADDR_FIRMWARE}.bin"
cp "${BUILD_DIR}/littlefs.bin"    "${FW_DIR}/littlefs_${ADDR_LITTLEFS}.bin"

echo "==> 生成 zip"
rm -f "${DIST}/${ZIP_NAME}"
# zip 内顶层即 m5burner.json + firmware/，便于直接导入
(
  cd "$OUT_DIR"
  if command -v zip >/dev/null 2>&1; then
    zip -r "${DIST}/${ZIP_NAME}" m5burner.json firmware
  elif command -v tar >/dev/null 2>&1; then
    tar -a -c -f "${DIST}/${ZIP_NAME}" m5burner.json firmware
  else
    echo "缺少 zip 或 tar，无法生成 ${DIST}/${ZIP_NAME}" >&2
    exit 1
  fi
)

# 找能 import serial 的 Python（裸 python3 / Windows Store stub 常缺 pyserial）
pick_esptool_python() {
  local cand
  for cand in \
    "$(command -v python 2>/dev/null || true)" \
    "$(command -v python3 2>/dev/null || true)" \
    "$(command -v python.exe 2>/dev/null || true)" \
    "$(command -v python3.exe 2>/dev/null || true)" \
    "${HOME}/.platformio/penv/Scripts/python.exe" \
    "${HOME}/.platformio/penv/bin/python" \
    "${USERPROFILE:-}/.platformio/penv/Scripts/python.exe"
  do
    [[ -n "$cand" ]] || continue
    if "$cand" -c 'import serial' >/dev/null 2>&1; then
      echo "$cand"
      return 0
    fi
  done
  return 1
}

# 找 esptool：PATH → PlatformIO tool-esptoolpy（用带 pyserial 的 Python 调用）
ESPTOOL=()
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool)
else
  ESPTOOL_PY="$(pick_esptool_python || true)"
  if [[ -f "${HOME}/.platformio/packages/tool-esptoolpy/esptool.py" && -n "$ESPTOOL_PY" ]]; then
    ESPTOOL=("$ESPTOOL_PY" "${HOME}/.platformio/packages/tool-esptoolpy/esptool.py")
  elif [[ -n "$ESPTOOL_PY" ]] && "$ESPTOOL_PY" -c "import esptool" >/dev/null 2>&1; then
    ESPTOOL=("$ESPTOOL_PY" -m esptool)
  fi
fi

MERGED="${DIST}/sparks_merged.bin"
if [[ ${#ESPTOOL[@]} -gt 0 ]]; then
  echo "==> merge_bin -> ${MERGED}"
  "${ESPTOOL[@]}" --chip esp32s3 merge_bin \
    -o "$MERGED" --flash_mode dio --flash_freq 80m --flash_size 8MB \
    "$ADDR_BOOTLOADER" "${BUILD_DIR}/bootloader.bin" \
    "$ADDR_PARTITIONS" "${BUILD_DIR}/partitions.bin" \
    "$ADDR_BOOT_APP0"  "$BOOT_APP0" \
    "$ADDR_FIRMWARE"   "${BUILD_DIR}/firmware.bin" \
    "$ADDR_LITTLEFS"   "${BUILD_DIR}/littlefs.bin"
else
  echo "警告: 未找到 esptool，跳过 merged.bin" >&2
fi

echo
echo "完成:"
echo "  M5Burner 目录: ${OUT_DIR}"
echo "  M5Burner zip:  ${DIST}/${ZIP_NAME}"
if [[ -f "$MERGED" ]]; then
  echo "  合并固件:      ${MERGED}"
  echo "  他人刷机:      esptool.py --chip esp32s3 -p PORT write_flash 0x0 ${MERGED}"
fi
echo
echo "版本信息来源: include/app_version.h（发版只改该头文件即可）"
