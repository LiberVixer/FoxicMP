#!/usr/bin/env bash
set -euo pipefail
build_dir=$(realpath "${1:?Usage: package.sh BUILD_DIR OUTPUT_DIR}")
output_dir=$(realpath -m "${2:?Usage: package.sh BUILD_DIR OUTPUT_DIR}")
mkdir -p "$output_dir"
stage_dir=$(mktemp -d)
trap 'rm -rf "$stage_dir"' EXIT
folder=FoxicMPQt-0.1.64-linux-x64
cmake --install "$build_dir" --strip --prefix "$stage_dir/$folder"
# Fail the package step early when the staged executable is not runnable on
# the build image. The archive intentionally uses Mint's system Qt/libmpv/
# FFmpeg libraries, so this catches an incomplete CI dependency installation
# instead of publishing a package that fails immediately after extraction.
QT_QPA_PLATFORM=offscreen "$stage_dir/$folder/foxicmp-qt" --version >/dev/null
if ldd "$stage_dir/$folder/foxicmp-qt" 2>&1 | grep -q 'not found'; then
  echo 'Runtime dependency check failed: ldd reported a missing library.' >&2
  exit 1
fi
# Never include developer profiles or playback history.
tar -C "$stage_dir" -czf "$output_dir/$folder.tar.gz" "$folder"
(cd "$output_dir" && sha256sum "$folder.tar.gz" > "$folder.sha256")
