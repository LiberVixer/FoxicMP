"""Run on an interactive Windows desktop. All profiles and media stay in --work.

Example: python windows_playback.py --harvester-root E:\\test\\Harvester
  --exe E:\\test\\FoxicMP64.exe --work E:\\test\\progressive --case stall
Requires video.mp4/audio.m4a and ffprobe packet JSON in --work (90s test signals).
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
import subprocess
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path


def start_player(arguments):
    # Scheduled tasks may pass SW_HIDE to descendants. The measured player must show its own window.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 1
    return subprocess.Popen(arguments, startupinfo=startup)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--harvester-root", required=True)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--case", default="smoke", choices=["smoke", "stall", "reverse", "stop", "cancel", "generation", "multi", "crash", "closewait", "onecomplete", "endpending", "dub", "seek", "providercrash", "writercrash", "truncate"])
    parser.add_argument("--fragmented",action="store_true")
    args = parser.parse_args()
    import faulthandler
    faulthandler.dump_traceback_later(90 if args.case == "stall" else 45, repeat=True)
    sys.path.insert(0, args.harvester_root)
    from scripts.progressive_session import Session, read_json
    from scripts.progressive_pipe import Provider
    work = Path(args.work)
    user = C.WinDLL("user32", use_last_error=True)
    user.EnumWindows.argtypes = [C.c_void_p, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user.IsWindow.argtypes = [W.HWND]
    user.IsWindowVisible.argtypes = [W.HWND]
    user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def windows(pid):
        found = []
        @callback
        def visit(handle, _):
            actual = W.DWORD()
            user.GetWindowThreadProcessId(handle, C.byref(actual))
            if actual.value == pid:
                title, kind = C.create_unicode_buffer(2048), C.create_unicode_buffer(256)
                user.GetWindowTextW(handle, title, 2048)
                user.GetClassNameW(handle, kind, 256)
                found.append((handle, kind.value, title.value))
            return True
        user.EnumWindows(visit, 0)
        return found

    def command(proc, value):
        for handle, kind, title in windows(proc.pid):
            if title and ("FoxicMP" in kind or "MediaPlayerClassic" in kind):
                user.PostMessageW(handle, 0x111, value, 0)
                return
        raise AssertionError(f"Player main window missing: {windows(proc.pid)}")

    def quit_player(proc):
        for handle, _, _ in windows(proc.pid):
            user.PostMessageW(handle, 0x10, 0, 0)
        try:
            proc.wait(8)
        except subprocess.TimeoutExpired:
            proc.kill()
            raise AssertionError("Player did not close within eight seconds")

    def wait(check, seconds=20, description="condition"):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            result = check()
            if result:
                return result
            time.sleep(.2)
        raise AssertionError(f"Timeout waiting for {description}")

    media = [work / ("video-fragmented.mp4" if args.fragmented else "video.mp4"), work / ("audio-fragmented.m4a" if args.fragmented else "audio.m4a")]
    packets = [json.loads(p.with_suffix(".packets.json").read_text())["packets"] for p in media]
    run = work / "runs" / uuid.uuid4().hex[:8]
    session = Session(run, ["video.mp4.part", "audio.m4a.part"], [p.stat().st_size for p in media],900000000 if args.fragmented else 0)
    if args.fragmented:
        session.document["timestampOffset100ns"]=10000000;session.publish()
    writer = [(run / name).open("wb", buffering=0) for name in ("video.mp4.part", "audio.m4a.part")]
    prefix = [0, 0]

    def write_to(index, seconds, finished=True):
        count = media[index].stat().st_size if seconds >= 90 else max(int(p["pos"]) + int(p["size"]) for p in packets[index] if float(p.get("dts_time", p["pts_time"])) < seconds)
        if count < prefix[index]:
            raise AssertionError("Test writer attempted regression")
        with media[index].open("rb") as source:
            source.seek(prefix[index])
            writer[index].write(source.read(count - prefix[index]))
        prefix[index] = count
        session.progress(index, count, media[index].stat().st_size, finished and seconds >= 90)

    result = {"fragmented":args.fragmented,"case": args.case, "run": str(run), "events": []}
    result['player_sha256']=hashlib.sha256((Path(args.exe)).read_bytes()).hexdigest()
    processes = []
    provider = None
    helper = owner = None
    try:
        if args.case == "dub":
            processes.append(start_player([args.exe, "/new", str(media[0]), "/dub", str(media[1])]))
            time.sleep(6)
            observed = windows(processes[0].pid)
            assert not any(kind == "#32770" and user.IsWindowVisible(handle) and title for handle, kind, title in observed), observed
            assert any("FoxicMP" in kind or "MediaPlayerClassic" in kind for _, kind, _ in observed), observed
            result["windows"] = [(kind, title) for _, kind, title in observed]
        else:
            write_to(0, (16 if args.fragmented else 12) if args.case != "reverse" else 3)
            write_to(1, (16 if args.fragmented else 12) if args.case != "smoke" else 3)
            if args.case == "writercrash":
                owner=subprocess.Popen([sys.executable,"-c","import time; time.sleep(600)"])
            provider = Provider(session.path,owner=owner.pid if owner else 0)
            if args.case == "providercrash":
                helper=subprocess.Popen([sys.executable,str(Path(args.harvester_root)/"scripts"/"progressive_download.py"),
                                         "--provider",str(session.path),str(os.getpid()),str(run/"stop")])
            else:
                threading.Thread(target=provider.run, daemon=True).start()
            wait(lambda: (run / "provider.json").exists(), description="provider ready")
            processes.append(start_player([args.exe, "/new", "/harvester-session", str(session.path)]))
            def status(proc=processes[0]):
                try:
                    return read_json(run / f"player-{proc.pid}.json")
                except (OSError, ValueError):
                    return {}
            def show_main():
                for h, kind, title in windows(processes[0].pid):
                    if title and ("FoxicMP" in kind or "MediaPlayerClassic" in kind):
                        C.set_last_error(0)
                        shown=user.SetWindowPos(h, W.HWND(-1), 100, 100, 800, 550, 0x0040)
                        result['window_state']={'hwnd':h,'visible':bool(user.IsWindowVisible(h)),
                                                'set_position':bool(shown),'error':C.get_last_error()}
                        return user.IsWindowVisible(h)
                return False
            wait(show_main, description="visible test window")
            def playing():
                value = status()
                return value if value.get("type") == "playing" and value.get("position100ns", 0) >= 2 * 10**7 else None
            if args.case in {"smoke", "reverse"}:
                time.sleep(3)
                value = status()
                result["events"].append(value)
                assert value.get("type") == "buffering" and value.get("position100ns", 0) < 10**7, (value, windows(processes[0].pid))
                write_to(1 if args.case == "smoke" else 0, 25)
                write_to(0 if args.case == "smoke" else 1, 25)
            if args.case in {"providercrash","writercrash","truncate"}:
                command(processes[0],887)
            value = wait(playing, description="progressive playback before completion")
            wait(show_main, description="visible loaded test window")
            result["events"].append(value)
            assert session.document["state"] == "downloading"
            assert prefix[0] < media[0].stat().st_size and prefix[1] < media[1].stat().st_size
            if args.case == "providercrash":
                before=status()["position100ns"]
                helper.kill();helper.wait(8)
                time.sleep(5)
                assert processes[0].poll() is None
                assert read_json(run/"leases.json")["leases"],"Broken pipe released a live reader"
                assert all((run/t["path"]).exists() for t in session.document["tracks"].values())
                # The dead provider cannot update player JSON. Reconnect a replacement,
                # then measure the first new position against the last live observation.
                old_status_time=(run/f"player-{processes[0].pid}.json").stat().st_mtime_ns
                helper=subprocess.Popen([sys.executable,str(Path(args.harvester_root)/"scripts"/"progressive_download.py"),
                                         "--provider",str(session.path),str(os.getpid()),str(run/"stop")])
                observed=wait(lambda: status() if (run/f"player-{processes[0].pid}.json").stat().st_mtime_ns>old_status_time else None,
                              description="replacement provider receives status")
                assert abs(observed["position100ns"]-before)<2*10**7,observed
                resumed=wait(playing,description="provider restart resumes same graph")
                result["events"].extend([observed,resumed])
                result["provider_outage_seconds"]=5
            elif args.case == "writercrash":
                owner.kill();owner.wait(8)
                wait(lambda: read_json(session.path)["state"]=="failed",description="writer death publishes failure")
                wait(lambda: not provider.leases.items,description="writer failure releases graph")
                assert processes[0].poll() is None
                assert all((run/t["path"]).exists() for t in session.document["tracks"].values())
                result["failure_state"]=read_json(session.path)["state"]
            elif args.case == "truncate":
                writer[0].truncate(0)
                session.publish()
                wait(lambda: not provider.leases.items,description="lost confirmed bytes closes graph")
                assert processes[0].poll() is None
                result["confirmed_bytes_removed"]=True
            elif args.case == "seek":
                write_to(0, 25)
                write_to(1, 25)
                wait(lambda: status().get("readyEnd100ns", 0) > 22 * 10**7,
                     description="expanded common buffer published")
                command(processes[0], 888)
                wait(lambda: status().get("type") == "paused", description="manual pause")
                before = status()["position100ns"]
                command(processes[0], 902)
                value = wait(lambda: status() if status().get("position100ns", 0) > before + 4 * 10**7 else None,
                             description="seek within common buffer")
                before = value["position100ns"]
                command(processes[0], 904)
                time.sleep(1)
                value = status()
                assert abs(value["position100ns"] - before) < 5 * 10**6, value
                assert value["type"] == "paused" and value["position100ns"] < value["readyEnd100ns"], value
                result["events"].append(value)
                command(processes[0], 887)
                wait(playing, description="manual playback resumed")
            elif args.case == "stall":
                buffered = wait(lambda: status() if status().get("type") == "buffering" else None, seconds=20, description="common buffer exhausted")
                result["events"].append(buffered)
                position = buffered["position100ns"]
                # An actual 60-second no-data gap: heartbeat continues, no EOF/reopen.
                until = time.monotonic() + 60
                while time.monotonic() < until:
                    value = status()
                    assert value.get("type") == "buffering", value
                    assert abs(value["position100ns"] - position) < 2 * 10**7, value
                    assert processes[0].poll() is None
                    time.sleep(1)
                write_to(0, 90)
                write_to(1, 90)
                result["events"].append(wait(playing, description="resume after sixty second gap"))
            elif args.case in {"multi", "crash"}:
                processes.append(start_player([args.exe, "/new", "/harvester-session", str(session.path)]))
                wait(lambda: len(provider.leases.items) == 2, description="two player leases")
                second = processes.pop()
                if args.case == "crash":
                    second.kill()
                    second.wait(8)
                else:
                    quit_player(second)
                wait(lambda: len(provider.leases.items) == 1, description="one remaining player lease")
                write_to(0, 90)
                write_to(1, 90)
                session.state("merging")
                session.state("complete")
                assert (run / "video.mp4.part").exists()
            elif args.case == "onecomplete":
                write_to(0, 90)
                wait(lambda: status().get("type") == "buffering", seconds=20, description="audio prefix exhausted while video complete")
                value = status()
                assert value["position100ns"] < value["readyEnd100ns"] + 10**7, value
                assert session.document["tracks"]["audio"]["state"] == "downloading"
                assert session.document["tracks"]["video"]["state"] == "complete"
                result["events"].append(value)
                write_to(1, 90)
                result["events"].append(wait(playing, description="both tracks continue after audio catches up"))
            elif args.case == "endpending":
                write_to(0, 90, finished=False)
                write_to(1, 90, finished=False)
                # Jump near the index end while the producer has not confirmed EOF.
                # The disposable Defaults profile uses 20s large / 1s small jumps.
                wait(lambda: status().get("readyEnd100ns", 0) >= 89 * 10**7,
                     description="full packet index published before seeking")
                command(processes[0], 888)
                wait(lambda: status().get("type") == "paused", description="pause before deterministic seeks")
                for _ in range(4):
                    before = status()["position100ns"]
                    command(processes[0], 904)
                    wait(lambda: status().get("position100ns", 0) > before + 10 * 10**7,
                         description="large seek acknowledged")
                for _ in range(8):
                    if status().get("position100ns", 0) >= 88 * 10**7 or status().get("type") == "buffering":break
                    before = status()["position100ns"]
                    command(processes[0], 900)
                    wait(lambda: status().get("position100ns", 0) > before + 5 * 10**6,
                         description="small seek acknowledged")
                command(processes[0], 887)
                value = wait(lambda: status() if (status().get("type")=="buffering" if args.fragmented else status().get("completionWaiters")==2) else None,
                             seconds=15, description="both splitters awaiting explicit track completion")
                result["events"].append(value)
                assert all(t["state"] == "downloading" for t in session.document["tracks"].values())
                assert len(provider.leases.items) == 1
                command(processes[0], 890)
                wait(lambda: not provider.leases.items, description="Stop cancels both completion waits")
                assert processes[0].poll() is None
            elif args.case == "closewait":
                wait(lambda: status().get("type") == "buffering", seconds=20, description="both readers waiting before Close")
                closed = processes.pop()
                quit_player(closed)
                wait(lambda: not provider.leases.items, description="Close releases waiting readers")
            elif args.case == "cancel":
                session.state("cancelled")
                wait(lambda: not provider.leases.items, description="cancel releases graph")
            elif args.case == "generation":
                from scripts.progressive_session import atomic_json
                changed = read_json(session.path)
                changed["generation"] += 1
                changed["revision"] += 1
                atomic_json(session.path, changed)
                wait(lambda: not provider.leases.items, description="generation change closes old graph")
            elif args.case == "stop":
                wait(lambda: status().get("type") == "buffering", seconds=20, description="reader waiting before Stop")
                command(processes[0], 890)
                wait(lambda: not provider.leases.items, description="Stop releases waiting readers")
                assert processes[0].poll() is None
            result["windows"] = [(kind, title) for proc in processes for _, kind, title in windows(proc.pid)]
            try:
                from PIL import ImageGrab
                for handle, kind, _ in windows(processes[0].pid) if processes else []:
                    if kind == "FoxicMP":
                        rect = W.RECT()
                        user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
                        user.GetWindowRect(handle, C.byref(rect))
                        ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom)).save(work / f"{args.case}.png")
                        break
            except ImportError:
                pass
        for proc in list(processes):
            quit_player(proc)
            processes.remove(proc)
        if provider:
            wait(lambda: not (read_json(run/"leases.json")["leases"] if args.case=="providercrash" else provider.leases.items),
                 description="all leases released")
        result["passed"] = True
    except BaseException:
        result["passed"] = False
        result["error"] = traceback.format_exc()
        result["windows"] = [[(kind, title) for _, kind, title in windows(proc.pid)] for proc in processes]
        raise
    finally:
        for proc in processes:
            try:
                quit_player(proc)
            except Exception:
                proc.kill()
        if provider:
            provider.stop.set()
        for child in [helper,owner]:
            if child and child.poll() is None:child.terminate();child.wait(8)
        for stream in writer:
            stream.close()
        serialized=json.dumps(result, indent=2, ensure_ascii=False)
        (run / "result.json").write_text(serialized, encoding="utf-8")
        (work / f"result-{args.case}{'-fragmented' if args.fragmented else ''}.json").write_text(serialized, encoding="utf-8")


if __name__ == "__main__":
    main()
