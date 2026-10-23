#!/usr/bin/env python3
"""Lease one private PulseAudio-compatible combined sink while stdin is open."""
import argparse
import json
import os
import re
import select
import signal
import subprocess
import sys
import uuid


def pactl(*args):
    result = subprocess.run(["pactl", *args], capture_output=True, text=True,
                            timeout=2, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.strip()[:400] or "pactl failed")
    return result.stdout.strip()


def items(kind):
    # pactl 16 JSON omits module indices; the short module table includes them.
    if kind == "modules":
        result = []
        for line in pactl("list", "short", "modules").splitlines():
            fields = line.split("\t")
            if len(fields) >= 3 and fields[0].isdecimal():
                result.append({"index": int(fields[0]), "name": fields[1], "argument": fields[2]})
        return result
    result = json.loads(pactl("--format=json", "list", kind))
    if not isinstance(result, list):
        raise RuntimeError("Invalid pactl JSON")
    return result


def device_name(value):
    if not value.startswith("pulse/"):
        raise ValueError("Choose a PulseAudio/PipeWire output")
    name = value[6:]
    if not re.fullmatch(r"[A-Za-z0-9_.:-]{1,256}", name) or name.startswith("foxicmp_dual_"):
        raise ValueError("Invalid output name")
    return name


class Route:
    def __init__(self):
        self.name = "foxicmp_dual_%d_%s" % (os.getpid(), uuid.uuid4().hex[:12])
        self.module = None
        self.pair = None
        self.healthy = False

    def clear(self):
        # Verify ownership again: a restarted server can reuse numeric IDs.
        if self.module is not None:
            for module in items("modules"):
                if (module.get("index") == self.module
                        and module.get("name") == "module-combine-sink"
                        and "sink_name=" + self.name in module.get("argument", "").split()):
                    pactl("unload-module", str(self.module))
                    break
        self.module = None
        self.pair = None
        self.healthy = False

    def update(self, primary, secondary):
        sinks = {s["name"] for s in items("sinks")}
        if primary == "auto":
            main = pactl("get-default-sink")
            if main.startswith("foxicmp_dual_"):
                main = self.pair[0] if self.pair else ""
        else:
            main = device_name(primary)
        other = device_name(secondary)
        if main not in sinks or other not in sinks:
            raise RuntimeError("An output is unavailable")
        if main == other:
            raise RuntimeError("Choose two different outputs")
        if self.healthy and self.pair == (main, other) and self.name in sinks:
            return
        self.clear()
        module = pactl("load-module", "module-combine-sink", "sink_name=" + self.name,
                       "slaves=" + main + "," + other,
                       "sink_properties=device.description=FoxicMP_dual_output")
        if not module.isdecimal():
            raise RuntimeError("Invalid module ID")
        self.module = int(module)
        self.pair = (main, other)
        self.healthy = True


def run(primary, secondary):
    route = Route()
    previous = None
    try:
        while True:
            try:
                route.update(primary, secondary)
                message = {"state": "ready", "device": "pulse/" + route.name,
                           "primary": route.pair[0], "secondary": route.pair[1]}
            except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
                # Emit fallback before unloading the route, so the client has a
                # chance to move its stream. Retain ownership for later cleanup.
                route.healthy = False
                message = {"state": "fallback", "reason": str(error)[:400]}
            if message != previous:
                print(json.dumps(message), flush=True)
                previous = message
            readable, _, _ = select.select([sys.stdin.buffer], [], [], 2)
            if readable and not sys.stdin.buffer.readline():
                break
    finally:
        try:
            route.clear()
        except (RuntimeError, ValueError, OSError, subprocess.SubprocessError):
            pass  # A disconnected server has already discarded its modules.


def interrupted(*_):
    raise SystemExit(0)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--primary", required=True)
    parser.add_argument("--secondary", required=True)
    args = parser.parse_args()
    run(args.primary, args.secondary)
