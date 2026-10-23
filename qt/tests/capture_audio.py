"""Capture only a private PulseAudio monitor; save levels/times, never raw audio."""
import ctypes as c
import json
import math
import signal
import sys
import time
from pathlib import Path


def capture(device, output):
    class Spec(c.Structure):
        _fields_ = [("format", c.c_int), ("rate", c.c_uint32), ("channels", c.c_uint8)]

    class Attributes(c.Structure):
        _fields_ = [(name, c.c_uint32) for name in ("maxlength", "tlength", "prebuf", "minreq", "fragsize")]

    pulse = c.CDLL("libpulse-simple.so.0")
    pulse.pa_simple_new.argtypes = [c.c_char_p, c.c_char_p, c.c_int, c.c_char_p, c.c_char_p,
                                    c.POINTER(Spec), c.c_void_p, c.POINTER(Attributes), c.POINTER(c.c_int)]
    pulse.pa_simple_new.restype = c.c_void_p
    pulse.pa_simple_read.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_int)]
    pulse.pa_simple_get_latency.argtypes = [c.c_void_p, c.POINTER(c.c_int)]
    pulse.pa_simple_get_latency.restype = c.c_uint64
    pulse.pa_simple_free.argtypes = [c.c_void_p]
    error = c.c_int()
    samples = (c.c_int16 * 192)()  # 4 ms at 48 kHz.
    spec = Spec(3, 48000, 1)  # PA_SAMPLE_S16LE.
    attrs = Attributes(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, c.sizeof(samples))
    stream = pulse.pa_simple_new(None, b"FoxicMP validation", 2, device.encode(), b"Private monitor",
                                 c.byref(spec), None, c.byref(attrs), c.byref(error))
    if not stream:
        raise RuntimeError(f"Cannot capture private monitor: {error.value}")
    running = True

    def stop(*_):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    try:
        with Path(output).open("w", buffering=1) as log:
            previous = 0
            while running:
                if pulse.pa_simple_read(stream, samples, c.sizeof(samples), c.byref(error)) < 0:
                    raise RuntimeError(f"Monitor read failed: {error.value}")
                latency = pulse.pa_simple_get_latency(stream, c.byref(error))
                if latency == 2**64 - 1:
                    raise RuntimeError(f"Monitor latency failed: {error.value}")
                end = (time.monotonic_ns() - latency * 1000) // 100
                begin = end - 40000
                flags = 1 if previous and abs(begin - previous) > 300000 else 0
                log.write(f"a,{begin},{end},{max(abs(value) for value in samples)},{flags}\n")
                previous = end
                Path(output + ".ready").touch()
    finally:
        pulse.pa_simple_free(stream)


if __name__ == "__main__":
    if sys.argv[1] == "--analyze":
        expected = float(sys.argv[3])
        if not math.isfinite(expected):
            raise ValueError("Invalid expected offset")
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests" / "harvester"))
        from windows_av import analyze  # The level/time analysis is platform independent.
        original = Path(sys.argv[2])
        video = original.read_text().splitlines()
        audio = Path(str(original) + ".audio").read_text().splitlines()
        # Stop may truncate a final flash/beep pair. Analyze only the common
        # capture interval, leaving 800 ms for the largest tested track offset.
        end = min(int(video[-1].split(',')[2]), int(audio[-1].split(',')[2])) - 8000000
        clipped = Path(str(original) + ".analysis")
        for rows, target in [(video, clipped), (audio, Path(str(clipped) + '.audio'))]:
            target.write_text('\n'.join(row for row in rows if int(row.split(',')[2]) <= end))
        result = analyze(clipped, expected, [int(v) for v in sys.argv[4:]])
        result['capture_end_guard_ms'] = 800
        Path(sys.argv[2] + ".json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result))
        if result["max_abs_interval_ms"] > 100:
            raise SystemExit("A/V error exceeds 100 ms")
    else:
        capture(sys.argv[1], sys.argv[2])
