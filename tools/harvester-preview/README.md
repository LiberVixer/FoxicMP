# Standalone progressive-preview adapter

This directory preserves the exact Harvester adapter sources used to validate
FoxicMP's progressive Windows playback. It is an opt-in command-line test tool;
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

Local adapter tests (FFmpeg on PATH):

```sh
python -m unittest tests.test_progressive_playback tests.test_progressive_http tests.test_progressive_hls
```

The protocol test subset omits the two full-Harvester launcher/downloader tests;
those require its GUI repository and remain tested there. Adapter modules themselves
are unchanged copies of the validated sources.
