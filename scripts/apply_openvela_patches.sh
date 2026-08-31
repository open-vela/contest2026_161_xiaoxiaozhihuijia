#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
contest_dir=$(cd "$script_dir/.." && pwd)
workspace=${1:-$(cd "$contest_dir/.." && pwd)}
apps_dir=$workspace/apps
vendor_dir=$workspace/vendor/sifli
apps_patch=$contest_dir/patches/apps_openvela_ble_probe.patch
vendor_patch=$contest_dir/patches/vendor_sifli_platform.patch
apps_base=dcc6a95c3b323e533c98fde8fb209f99e24f0fdd
vendor_base=f5bde0b5f99bfccf87faf791d8b51fdfcb963c9b

for path in "$apps_dir/.git" "$vendor_dir/.git" "$apps_patch" "$vendor_patch"; do
  if [[ ! -e "$path" ]]; then
    echo "missing required path: $path" >&2
    exit 1
  fi
done

if [[ $(git -C "$apps_dir" rev-parse HEAD) != "$apps_base" ]]; then
  echo "apps is not at dev-ai-contest-2026 baseline $apps_base" >&2
  exit 1
fi

if [[ $(git -C "$vendor_dir" rev-parse HEAD) != "$vendor_base" ]]; then
  echo "vendor/sifli is not at dev-ai-contest-2026 baseline $vendor_base" >&2
  exit 1
fi

if [[ -n $(git -C "$apps_dir" status --porcelain) ]]; then
  echo "apps worktree is not clean; refusing to modify it" >&2
  exit 1
fi

if [[ -n $(git -C "$vendor_dir" status --porcelain) ]]; then
  echo "vendor/sifli worktree is not clean; refusing to modify it" >&2
  exit 1
fi

echo "public branch: dev-ai-contest-2026"
echo "apps baseline: $apps_base"
echo "vendor/sifli baseline: $vendor_base"
sha256sum "$apps_patch" "$vendor_patch"

# Validate every patch before allowing either repository to change.
git -C "$apps_dir" apply --check "$apps_patch"
git -C "$vendor_dir" apply --check "$vendor_patch"

git -C "$apps_dir" apply "$apps_patch"
git -C "$vendor_dir" apply "$vendor_patch"

while read -r expected relative_path; do
  actual=$(sha256sum "$apps_dir/$relative_path" | awk '{print $1}')
  if [[ "$actual" != "$expected" ]]; then
    echo "source hash mismatch: apps/$relative_path" >&2
    exit 1
  fi
done <<'EOF'
de413a645740e84ffa29abc5f8a46b3dbd2530e6af804217f9b789f283df41ba examples/openvela_ble_probe/CMakeLists.txt
5a2ad5ee7601847ab8fa3e9f94dc25657ddd158f16d89b1a22227608ebaca1b0 examples/openvela_ble_probe/Kconfig
c04978af22a014c3935cb5b6ec99eafbe95adbbe8f7a2d50956070688bc37153 examples/openvela_ble_probe/openvela_ble_probe_main.c
e124aa2fdd2ff7c53e351c9f4b8fe93c6ef9555412a63e164b3f40dc7985b768 examples/openvela_ble_probe/probe_game.h
1dd83e80d87b4293f0e1e56d736389898ef02e1079c6b488a9cb7969ce86000f examples/openvela_ble_probe/probe_game.c
c42f4a77fa75691185b42cc0e949a6d28c2fdf895a05adaace91e685885689ff examples/openvela_ble_probe/probe_ui.h
bf57c63425df3038033763a8a1039090d1bec9655f36b4412c72440d9a61e426 examples/openvela_ble_probe/probe_ui.c
EOF

echo "patches applied and Probe source hashes verified"
