# Standalone progressive-preview adapter

This directory contains the Harvester adapter used to validate FoxicMP's
progressive Windows playback, extended with an experimental Linux provider.
It is an opt-in command-line test tool;
it does not enable or modify the Harvester GUI. It contains no cookies, media,
FFmpeg binaries, or packaged Harvester executable.

Use Python 3.11 or later on Windows, `yt-dlp==2026.8.19`, `certifi==2026.7.22`,
and an installed FFmpeg/FFprobe (validation used 9.0.2). Create a virtual environment
and install those Python packages, then run from this directory:

```powershell
$env:YTD_TEMP_DIR = 'E:\FoxicMP-test\temp'
python scripts\progressive_download.py URL -f VIDEO_FORMAT+AUDIO_FORMAT --ffmpeg-location E:\FFmpeg\bin -o E:\FoxicMP-test\finished.mp4
```

Replace URL/format IDs with compatible H.264/AAC-LC choices. For continuous muxed
HLS/VOD, select its single format ID. A `[FoxicMP]` stdout event with `ready:true`
contains the local manifest to open:

```powershell
E:\FoxicMP-test\player\FoxicMP64.exe /new /harvester-session E:\FoxicMP-test\temp\foxicmp-sessions\SESSION\session.json
```

Keep the portable player/profile in a disposable test directory. Close all players
before deleting session files. Successful sessions retain their pinned source tracks
until every reader releases them; failed sessions stay available for diagnosis.
The usual selected-quality download, FFmpeg stream-copy merger, and download archive
remain the final-output path. See ../../docs/HarvesterProgressive.md for protocol,
limits, validation and failure handling.

The snapshot originates from the progressive-preview work in
https://github.com/LiberVixer/YouTubeHarvester and is included with FoxicMP under
GPL-3.0-or-later. The Harvester GUI integration intentionally remains disabled.

Linux / Qt 0.1.10: run the same adapter with Python 3.11+, yt-dlp and FFmpeg.
Its provider automatically selects authenticated Unix IPC on Linux. Open a
ready single-track H.264/AAC MP4/M4A pair with:

```sh
./foxicmp-qt --harvester-session /path/to/session.json
```

Qt accepts fragmented MP4 and ordinary fast-start MP4 with known byte lengths
and a complete `moov` before media data. Tail metadata is rejected. The local
cached-HLS adapter produces fMP4 without a second download; DRM, live and
discontinuities remain unsupported.
The reader starts with ten shared seconds, waits through missing data, preserves
manual pause, and restricts seeking to confirmed packets in its bounded cache.
Qt software trials cover 0/±70/±240 ms track offsets, flash/beep timing after
buffering and seeking, and public YouTube, Rutube HLS and VK downloads.
The mode remains experimental; physical speaker/display latency and GPU/Wayland
still need hardware trials.
Linux IPC adds a per-reader `readerId` so two readers within the same player
process cannot release one another's lease. A `leaseAck: true` attachment requests
an `attached` reply after the lease is persisted; Qt waits for this reply before
opening the source files. Older clients still receive only heartbeats.
Restarted providers retain unknown
persisted reader leases until process exit rather than guessing that files are free.

Local adapter tests (FFmpeg on PATH):

```sh
python -m unittest tests.test_progressive_playback tests.test_progressive_http tests.test_progressive_hls tests.test_progressive_unix
```

The protocol test subset omits the two full-Harvester launcher/downloader tests;
those require its GUI repository and remain tested there. The Windows provider
is retained; Linux dispatch and lifetime checks are covered separately.
