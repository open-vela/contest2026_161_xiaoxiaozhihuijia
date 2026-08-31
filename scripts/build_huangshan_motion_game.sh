#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
contest_dir=$(cd "$script_dir/.." && pwd)
workspace=${1:-$(cd "$contest_dir/.." && pwd)}
build_dir=$workspace/cmake_out/huangshan_motion_game_submission
board_config=$workspace/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh
frozen_config=$contest_dir/artifacts/huangshan_motion_game_v2_20260831/config/.config
apps_patch=$contest_dir/patches/apps_openvela_ble_probe.patch
vendor_patch=$contest_dir/patches/vendor_sifli_platform.patch
apps_base=dcc6a95c3b323e533c98fde8fb209f99e24f0fdd
vendor_base=f5bde0b5f99bfccf87faf791d8b51fdfcb963c9b

for path in "$workspace/nuttx/CMakeLists.txt" "$board_config/defconfig" \
            "$frozen_config" "$apps_patch" "$vendor_patch"; do
  if [[ ! -e "$path" ]]; then
    echo "missing required path: $path" >&2
    exit 1
  fi
done

if [[ -e "$build_dir" ]]; then
  echo "build directory already exists; refusing to overwrite: $build_dir" >&2
  exit 1
fi

if [[ $(git -C "$workspace/apps" rev-parse HEAD) != "$apps_base" ]] ||
   [[ $(git -C "$workspace/vendor/sifli" rev-parse HEAD) != "$vendor_base" ]]; then
  echo "public repositories are not at the recorded dev-ai-contest-2026 baselines" >&2
  exit 1
fi

for relative_path in \
  CMakeLists.txt Kconfig openvela_ble_probe_main.c probe_game.h probe_game.c \
  probe_ui.h probe_ui.c; do
  if [[ ! -f "$workspace/apps/examples/openvela_ble_probe/$relative_path" ]]; then
    echo "Probe patch is not applied: $relative_path is missing" >&2
    exit 1
  fi
done

echo "public branch: dev-ai-contest-2026"
echo "apps baseline: $apps_base"
echo "vendor/sifli baseline: $vendor_base"
sha256sum "$apps_patch" "$vendor_patch"

mkdir -p "$build_dir"
install -m 0644 "$frozen_config" "$build_dir/.config"

cmake -S "$workspace/nuttx" -B "$build_dir" -GNinja \
  -DBOARD_CONFIG="$board_config" \
  -DNUTTX_DEFCONFIG_SAVED:INTERNAL="$board_config/defconfig" \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"

grep -qx 'CONFIG_BSP_USING_I2C3=y' "$build_dir/.config"
grep -qx 'CONFIG_EXAMPLES_OPENVELA_BLE_PROBE=y' "$build_dir/.config"
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-8}"

firmware=$build_dir/nuttx.bin
if [[ ! -f "$firmware" ]]; then
  echo "build completed without expected firmware: $firmware" >&2
  exit 1
fi

echo "generated firmware: $firmware"
sha256sum "$firmware"
