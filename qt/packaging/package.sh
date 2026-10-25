#!/usr/bin/env bash
set -euo pipefail
build_dir=$(realpath "${1:?Usage: package.sh BUILD_DIR OUTPUT_DIR}")
output_dir=$(realpath -m "${2:?Usage: package.sh BUILD_DIR OUTPUT_DIR}")
mkdir -p "$output_dir"
stage_dir=$(mktemp -d)
trap 'rm -rf "$stage_dir"' EXIT
# Use the configured build version, not a second release number in this script.
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo 'Cannot read a three-component project version from CMakeCache.txt.' >&2
  exit 2
fi
folder=FoxicMPQt-$version-linux-x64
cmake --install "$build_dir" --strip --prefix "$stage_dir/$folder"
# Fail the package step early when the staged executable is not runnable on
# the build image. The archive intentionally uses Mint's system Qt/libmpv/
# FFmpeg libraries, so this catches an incomplete CI dependency installation
# instead of publishing a package that fails immediately after extraction.
actual_version=$(QT_QPA_PLATFORM=offscreen "$stage_dir/$folder/foxicmp-qt" --version)
if [[ "$actual_version" != "FoxicMPQt $version" ]]; then
  echo "Executable version does not match the package version: $actual_version" >&2
  exit 1
fi
if ldd "$stage_dir/$folder/foxicmp-qt" 2>&1 | grep -q 'not found'; then
  echo 'Runtime dependency check failed: ldd reported a missing library.' >&2
  exit 1
fi
# Never include developer profiles or playback history. Normalize archive
# metadata so two builds from the same tree produce the same release checksum.
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
  -C "$stage_dir" -cf - "$folder" | gzip -n > "$output_dir/$folder.tar.gz"
(cd "$output_dir" && sha256sum "$folder.tar.gz" > "$folder.sha256")
