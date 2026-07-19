#!/usr/bin/env bash
# 编译固件 + LittleFS，生成 M5Burner zip 与可一键刷的 merged.bin
#
# 用法:
#   ./scripts/pack_m5burner.sh           # 编译并打包
#   ./scripts/pack_m5burner.sh --skip-build  # 仅用已有产物打包
#
# 输出:
#   dist/m5burner/                 # M5Burner 目录结构
#   dist/CardputerApps-<ver>.zip   # 可导入 / 发布的 zip
#   dist/cardputer_merged.bin      # esptool 从 0x0 整片写入

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENV_NAME="m5stack-cardputer"
BUILD_DIR="${ROOT}/.pio/build/${ENV_NAME}"
META_SRC="${ROOT}/m5burner/m5burner.json"
DIST="${ROOT}/dist"
OUT_DIR="${DIST}/m5burner"
FW_DIR="${OUT_DIR}/firmware"

BOOT_APP0="${HOME}/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

# ESP32-S3 / StampS3 (default_8MB) 烧录地址
ADDR_BOOTLOADER=0x0
ADDR_PARTITIONS=0x8000
ADDR_BOOT_APP0=0xe000
ADDR_FIRMWARE=0x10000
ADDR_LITTLEFS=0x670000

SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    -h|--help)
      sed -n '2,12p' "$0"
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

VERSION="$(python3 -c "import json; print(json.load(open('$META_SRC'))['version'])")"
ZIP_NAME="CardputerApps-${VERSION}.zip"

cd "$ROOT"

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo "==> 编译固件"
  pio run -e "$ENV_NAME"
  echo "==> 打包 LittleFS (data/)"
  pio run -e "$ENV_NAME" -t buildfs
fi

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
cp "$META_SRC" "${OUT_DIR}/m5burner.json"
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
  zip -r "${DIST}/${ZIP_NAME}" m5burner.json firmware
)

# 找 esptool：PATH → PlatformIO tool-esptoolpy
ESPTOOL=()
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool)
elif [[ -f "${HOME}/.platformio/packages/tool-esptoolpy/esptool.py" ]]; then
  ESPTOOL=(python3 "${HOME}/.platformio/packages/tool-esptoolpy/esptool.py")
elif [[ -x "${HOME}/.platformio/penv/bin/python" ]]; then
  # 部分环境用 penv 的 python 才能 import esptool
  ESPTOOL=("${HOME}/.platformio/penv/bin/python" -m esptool)
fi

MERGED="${DIST}/cardputer_merged.bin"
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
echo "发布前请检查/更新: m5burner/m5burner.json 的 version、description"
