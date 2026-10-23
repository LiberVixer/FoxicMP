#!/usr/bin/env bash
# Run UI/audio tests without using the desktop's windows or default audio sink.
set -euo pipefail
build_dir=$(realpath "${1:?Usage: run_tests.sh BUILD_DIR MEDIA_DIR [CTEST_OPTIONS...]}")
media_dir=$(realpath "${2:?Usage: run_tests.sh BUILD_DIR MEDIA_DIR [CTEST_OPTIONS...]}")
shift 2
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
for command in xvfb-run Xvfb xauth dbus-run-session pactl ctest; do
  command -v "$command" >/dev/null || { echo "Missing test dependency: $command" >&2; exit 2; }
done
python3 "$script_dir/check_environment.py" --media "$media_dir" --devices "$media_dir/devices"
if ! pactl info >/dev/null 2>&1; then
  pulseaudio --start --exit-idle-time=-1
fi
sink_name=foxicmp_test_$$
module=$(pactl load-module module-null-sink "sink_name=$sink_name")
trap 'pactl unload-module "$module" >/dev/null 2>&1 || true' EXIT
export FOXICMP_TEST_MEDIA="$media_dir"
export PULSE_SINK="$sink_name"
export FOXICMP_TEST_PULSE_SOURCE="$sink_name.monitor"
export LIBGL_ALWAYS_SOFTWARE=1
export QT_QPA_PLATFORM=xcb
dbus-run-session -- xvfb-run -a ctest --test-dir "$build_dir" --output-on-failure "$@"
