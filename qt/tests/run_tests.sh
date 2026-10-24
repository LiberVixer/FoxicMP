#!/usr/bin/env bash
# Run UI/audio tests without using the desktop's windows or default audio sink.
set -euo pipefail
build_dir=$(realpath "${1:?Usage: run_tests.sh BUILD_DIR MEDIA_DIR [CTEST_OPTIONS...]}")
media_dir=$(realpath "${2:?Usage: run_tests.sh BUILD_DIR MEDIA_DIR [CTEST_OPTIONS...]}")
shift 2
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
for command in xvfb-run Xvfb xauth dbus-run-session pulseaudio pactl ctest; do
  command -v "$command" >/dev/null || { echo "Missing test dependency: $command" >&2; exit 2; }
done
if [[ "${FOXICMP_TEST_PRIVATE_BUS:-0}" != 1 ]]; then
  # PulseAudio also owns a D-Bus name, so isolate the server before starting it.
  exec dbus-run-session -- env FOXICMP_TEST_PRIVATE_BUS=1 \
    bash "${BASH_SOURCE[0]}" "$build_dir" "$media_dir" "$@"
fi
python3 "$script_dir/check_environment.py" --media "$media_dir" --devices "$media_dir/devices"
audio_dir=$(mktemp -d /tmp/foxicmp-test-audio.XXXXXX)
audio_pid=
cleanup() {
  if [[ -n "$audio_pid" ]]; then
    kill "$audio_pid" 2>/dev/null || true
    wait "$audio_pid" 2>/dev/null || true
  fi
  rm -rf -- "$audio_dir"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
sink_name=foxicmp_test_$$
# No hardware discovery or stream-restore module: the user's server and its
# remembered application mute/volume values are never accessed by these tests.
pulseaudio -n --daemonize=no --use-pid-file=no --exit-idle-time=-1 \
  --log-target="file:$audio_dir/server.log" \
  -L "module-native-protocol-unix socket=$audio_dir/native auth-anonymous=1" \
  -L "module-null-sink sink_name=$sink_name" &
audio_pid=$!
export PULSE_SERVER="unix:$audio_dir/native"
for ((attempt = 0; attempt < 50; ++attempt)); do
  if pactl info >/dev/null 2>&1; then break; fi
  if ! kill -0 "$audio_pid" 2>/dev/null; then break; fi
  sleep .1
done
if ! pactl info >/dev/null 2>&1; then
  cat "$audio_dir/server.log" >&2
  exit 2
fi
export FOXICMP_TEST_MEDIA="$media_dir"
export PULSE_SINK="$sink_name"
export FOXICMP_TEST_PULSE_SOURCE="$sink_name.monitor"
export LIBGL_ALWAYS_SOFTWARE=1
export QT_QPA_PLATFORM=xcb
xvfb-run -a ctest --test-dir "$build_dir" --output-on-failure "$@"
