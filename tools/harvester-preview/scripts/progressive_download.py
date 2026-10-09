"""Opt-in yt-dlp adapter: one format choice, two native HTTP writers, usual merger."""
from __future__ import annotations
import concurrent.futures
import contextlib
import copy
import json
import os
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from scripts.progressive_session import Session, read_json


def report(path=None, ready=False, reason=""):
    print("[FoxicMP] " + json.dumps({"manifest": str(path) if path else "", "ready": ready, "reason": reason}, ensure_ascii=False), flush=True)


def eligible(info, params):
    formats = info.get("requested_formats") or []
    if len(formats) != 2 or info.get("is_live") or params.get("simulate") or params.get("skip_download"):
        return False
    if params.get("external_downloader") or params.get("allow_unplayable_formats"):
        return False
    video, audio = formats
    return all(f.get("protocol") in {"http", "https"} and not f.get("has_drm") and not f.get("fragments") for f in formats) and (
        video.get("ext") == "mp4" and str(video.get("vcodec", "")).startswith("avc1") and video.get("acodec") == "none"
        and audio.get("ext") == "m4a" and str(audio.get("acodec", "")).lower() == "mp4a.40.2" and audio.get("vcodec") == "none")



def eligible_hls(info,params):
    return (not info.get('requested_formats') and info.get('protocol')=='m3u8_native'
            and not info.get('is_live') and not info.get('has_drm')
            and isinstance(info.get('duration'),(int,float)) and info['duration']>0 and str(info.get('vcodec','')).startswith('avc1')
            and str(info.get('acodec','')).lower()=='mp4a.40.2'
            and not any(params.get(k) for k in ['simulate','skip_download','external_downloader','allow_unplayable_formats']))

def make_downloader_class(yt_dlp):
    from yt_dlp.downloader.http import HttpFD
    from yt_dlp.downloader.hls import HlsFD
    from yt_dlp.postprocessor.ffmpeg import FFmpegPostProcessor
    from yt_dlp.utils import DownloadError, prepend_extension, replace_extension

    class WrittenHttpFD(HttpFD):
        def sanitize_open(self, *args, **kwargs):
            stream, name = super().sanitize_open(*args, **kwargs)
            # HttpFD publishes its progress hook after write(). Unbuffered I/O makes
            # those bytes visible to other processes before the manifest is updated.
            mode = stream.mode
            stream.close()
            return open(name, mode, buffering=0), name

    class ProgressiveYDL(yt_dlp.YoutubeDL):
        def process_info(self, info):
            if eligible_hls(info,self.params):return self.process_hls_info(info)
            if not eligible(info, self.params):
                report(reason="Выбранные форматы не поддерживают предварительный просмотр")
                return super().process_info(info)
            previous = {key: copy.deepcopy(self.params.get(key)) for key in ("keepvideo",)}
            self._session = None
            self._parallel_done = False
            self._merged = False
            self._cancel = threading.Event()
            self._pair = [dict(f) for f in info["requested_formats"]]
            template = self.params["outtmpl"]["default"]
            directory = Path(os.environ.get("YTD_TEMP_DIR") or Path(template).parent).absolute() / "foxicmp-sessions" / uuid.uuid4().hex
            directory.mkdir(parents=True, mode=0o700)
            if os.name == "nt":
                from scripts.progressive_pipe import Win32
                Win32().protect_directory(directory)
            self.params["keepvideo"] = True
            self._directory, self._source = directory, dict(info)
            try:
                result = super().process_info(info)
                self._merged = info.get("__write_download_archive") is True and bool(info.get("filepath")) and Path(info["filepath"]).is_file()
                if self._merged:
                    self.to_screen(f"[download] Destination: {info['filepath']}")
                if self._session:
                    self._session.state("complete" if self._merged else "failed")
                    report(self._session.path, ready=self._merged and self._session.ready(), reason="" if self._merged else "Итоговая сборка не завершена")
                return result
            except BaseException:
                self._cancel.set()
                if self._session:
                    self._session.state("cancelled" if os.environ.get("YTD_STOP_FILE") and Path(os.environ["YTD_STOP_FILE"]).exists() else "failed")
                    report(self._session.path, reason="Загрузка отменена или завершилась ошибкой")
                raise
            finally:
                self.params.update(previous)
                self._session = None
                self._directory = None
                report(reason="Просмотр текущей загрузки завершён")

        def process_hls_info(self,info):
            self._hls_info=dict(info);self._hls_preview=None;self._cancel=threading.Event()
            try:
                result=super().process_info(info)
                if self._hls_preview and self._hls_preview.session.document['state']=='merging':
                    success=info.get('__write_download_archive') is True and bool(info.get('filepath')) and Path(info['filepath']).exists()
                    self._hls_preview.session.state('complete' if success else 'failed')
                return result
            except BaseException:
                if self._hls_preview:self._hls_preview.abort(self._stopping())
                raise
            finally:
                self._hls_info=None
                report(reason='Просмотр текущей загрузки завершён')

        def dl_hls(self,name,info):
            from scripts.progressive_hls import HlsPreview
            executable=FFmpegPostProcessor(self).executable
            if not executable:return super().dl(name,info)
            template=self.params['outtmpl']['default']
            directory=Path(os.environ.get('YTD_TEMP_DIR') or Path(template).parent).absolute()/'foxicmp-sessions'/uuid.uuid4().hex
            try:
                self._hls_preview=HlsPreview(directory,info['duration'],executable,provider_command,report,self._stopping)
            except (OSError,ValueError):
                report(reason='Локальный просмотр HLS недоступен; обычное скачивание продолжается')
                return super().dl(name,info)
            preview=self._hls_preview;owner=self
            class CachedHlsFD(HlsFD):
                def _append_fragment(fd,ctx,content):
                    if owner._stopping():raise DownloadError('Harvester download cancelled')
                    super()._append_fragment(ctx,content)
                    if not preview.error:
                        preview.feed(ctx['tmpfilename'],Path(ctx['tmpfilename']).stat().st_size)
            params=dict(self.params,nopart=True)
            fd=CachedHlsFD(self,params)
            def cancelled(event):
                if self._stopping():raise DownloadError('Harvester download cancelled')
            fd.add_progress_hook(cancelled)
            try:
                result=fd.download(name,info,False)
                if result[0]:preview.finish()
                else:preview.abort(self._stopping())
                return result
            except BaseException:
                preview.abort(self._stopping());raise

        def run_pp(self, pp, infodict):
            result = super().run_pp(pp, infodict)
            if getattr(self, "_session", None):
                moves = result.get("__files_to_move", {})
                for track in self._session.document["tracks"].values():
                    source = str(self._session.root / track["path"])
                    if source in moves:
                        # keepvideo alone still moves source files to the final folder.
                        # An explicit identical destination pins their paths in the session.
                        moves[source] = source
            return result

        def post_process(self, filename, info, files_to_move=None):
            result = super().post_process(filename, info, files_to_move)
            if getattr(self, "_session", None):
                desired = Path(super().prepare_filename(result))
                actual = Path(result["filepath"])
                if actual != desired:
                    desired.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(actual, desired)
                    result["filepath"] = str(desired)
            return result

        def prepare_filename(self, info_dict, dir_type="", **kwargs):
            if dir_type == "temp" and getattr(self, "_directory", None):
                return str(self._directory / ("media." + info_dict["ext"]))
            return super().prepare_filename(info_dict, dir_type, **kwargs)

        def _stopping(self):
            stop = os.environ.get("YTD_STOP_FILE")
            return bool(getattr(self,"_cancel",None) and self._cancel.is_set()) or bool(stop and Path(stop).exists())

        def dl(self, name, info, subtitle=False, test=False):
            if not subtitle and not test and getattr(self,'_hls_info',None) and info.get('format_id')==self._hls_info.get('format_id'):
                return self.dl_hls(name,info)
            if subtitle or test or not hasattr(self, "_pair") or self._session is None and self._parallel_done:
                return super().dl(name, info, subtitle, test)
            if info.get("format_id") not in {f["format_id"] for f in self._pair}:
                return super().dl(name, info, subtitle, test)
            if not self._parallel_done:
                # Use yt-dlp's own prepared filename to keep its merger bookkeeping.
                first = next(f for f in self._pair if f["format_id"] == info["format_id"])
                suffix = f".f{first['format_id']}.{first['ext']}"
                if not str(name).endswith(suffix):
                    raise DownloadError("Unsupported yt-dlp output naming for progressive playback")
                base = str(name)[:-len(suffix)]
                names = [Path(f"{base}.f{f['format_id']}.{f['ext']}") for f in self._pair]
                if any(p.parent != self._directory or len(str(p)) > 240 for p in names):
                    raise DownloadError("Progressive session paths must be local and at most 240 characters")
                self._session = Session(self._directory, [p.name for p in names], [None, None], int((self._source.get("duration") or 0)*10000000))
                self._provider = subprocess.Popen(
                    provider_command(self._session.path, os.getpid()),
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    close_fds=True, creationflags=0x08000000 if os.name == "nt" else 0)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    try:
                        if read_json(self._directory / "provider.json").get("ready"):
                            break
                    except (OSError, ValueError):
                        pass
                    if self._provider.poll() is not None:
                        raise DownloadError("FoxicMP session provider could not start")
                    time.sleep(.05)
                else:
                    raise DownloadError("FoxicMP session provider timeout")
                report(self._session.path, reason="Ожидание начальных индексов MP4/M4A")
                last_report = [0.0]
                def download(index):
                    params = dict(self.params)
                    params.update(nopart=True, continuedl=True, noprogress=True, http_chunk_size=0)
                    if params.get("ratelimit"):
                        params["ratelimit"] = max(1, int(params["ratelimit"]) // 2)
                    fd = WrittenHttpFD(self, params)
                    source = self._copy_infodict(self._source)
                    source.pop("requested_formats", None)
                    source.update(self._pair[index])
                    if source.get("http_headers") is None:
                        source["http_headers"] = self._calc_headers(source)
                    def progress(event):
                        if self._stopping():
                            raise DownloadError("Harvester download cancelled")
                        finished = event["status"] == "finished"
                        prefix = int(event.get("downloaded_bytes") or 0)
                        total = event.get("total_bytes")
                        if finished:
                            prefix = names[index].stat().st_size
                            total = total or prefix
                        # Keep every hook: metadata waits are independent of GUI progress.
                        self._session.progress(index, prefix, total, finished)
                        with self._session.lock:
                            if finished or time.monotonic() - last_report[0] >= .5:
                                ready = self._session.ready()
                                report(self._session.path, ready, "" if ready else "Ожидание индексов либо известной длины дорожек")
                                last_report[0] = time.monotonic()
                    fd.add_progress_hook(progress)
                    success, actual = fd.download(str(names[index]), source, False)
                    if not success:
                        raise DownloadError("One progressive track failed")
                    return success, actual
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                    futures = [pool.submit(download, i) for i in range(2)]
                    try:
                        for future in concurrent.futures.as_completed(futures):
                            future.result()
                    except BaseException:
                        self._cancel.set()
                        raise
                self._session.state("merging")
                self._parallel_done = True
            # Both calls from process_info use exactly the downloaded pair, no second HTTP request.
            return True, True

    return ProgressiveYDL


def progressive_arguments(command):
    """Remove only a recognized executable/interpreter prefix, preserving all options."""
    if not command:
        return None
    if command[1:3] == ["-m", "yt_dlp"]:
        return command[3:]
    if command[1:2] == ["--run-yt-dlp"]:
        return command[2:]
    if len(command) > 2 and command[1] == "--run-script" and command[2] in {"progressive_download", "progressive_download.py"}:
        return command[3:]
    if len(command) > 1 and Path(command[1]).name in {"progressive_download.py", "__main__.py", "yt-dlp.py"}:
        return command[2:]
    if Path(command[0]).name.lower() in {"yt-dlp", "yt-dlp.exe", "yt_dlp", "yt_dlp.exe"}:
        return command[1:]
    return None  # Unknown wrappers can add credentials/options internally: retain their behavior.


def provider_command(path, owner=0):
    prefix = [sys.executable, "--run-script", "progressive_download.py"] if getattr(sys, "frozen", False) else [sys.executable, str(Path(__file__).resolve())]
    return [*prefix, "--provider", str(path), str(owner), os.environ.get("YTD_STOP_FILE", "")]


def recover_providers(root):
    if not root.exists():
        return
    from scripts.progressive_pipe import Win32
    win = Win32()
    for manifest in root.glob("*/session.json"):
        try:
            state = read_json(manifest)
            try:
                provider = read_json(manifest.parent / "provider.json")
                actual = win.identity(provider["pid"])
                if actual is None or actual == provider.get("born"):
                    continue
            except (OSError, ValueError, KeyError):
                pass
            # A stopped writer must be retried as a fresh generation/session, never resumed in place.
            if state["state"] in {"downloading", "paused", "merging"}:
                state["state"] = "failed"
                state["revision"] += 1
                for track in state["tracks"].values():
                    if track["state"] != "complete":
                        track["state"] = "failed"
                from scripts.progressive_session import atomic_json
                atomic_json(manifest, state)
            subprocess.Popen(provider_command(manifest), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, close_fds=True, creationflags=0x08000000)
        except (OSError, ValueError, KeyError):
            continue


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if args and args[0] == "--provider":
        from scripts.progressive_pipe import Provider
        return Provider(Path(args[1]), int(args[2]), Path(args[3]) if len(args) > 3 and args[3] else None).run()
    import yt_dlp
    _, options, urls, ydl_opts = yt_dlp.parse_options(args)
    with make_downloader_class(yt_dlp)(ydl_opts) as ydl:
        return ydl.download_with_info_file(options.load_info_filename) if options.load_info_filename else ydl.download(urls)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
