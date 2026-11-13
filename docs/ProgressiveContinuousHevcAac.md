# Continuous HEVC + AAC-LC в ordinary MP4

SDK 1.89, приложение 0.2.0. Один растущий обычный MP4 с HEVC-видео и AAC-LC
аудио проходит copy-remux в append-only fMP4 и две decoder projections.
Режим отдельный от [HEVC video-only SDK 1.88](ProgressiveContinuousHevc.md) и
H.264/AAC SDK 1.84; прежние defaults/modes и API сохраняются. Harvester не меняется.

## Вызов и допуск

Нужна точная capability `continuousRemux.profiles.continuous-hevc-aac-window-v1`.
SDK проверяет одновременно HEVC Main/Main10 contract и AAC PCM-window contract.
Общая поддержка HEVC, AAC или video-only режима не разрешает эту пару.

```python
from progressive_continuous import ContinuousRemux, HEVC_AAC_MODE

preview = ContinuousRemux(client, session_directory, mode=HEVC_AAC_MODE)
preview.prepare(snapshot, "media")
# Writer публикует только записанные/flush ranges с SHA и продолжает heartbeat.
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="HEVC + AAC preview", activate=False)
# Неполученная цель ждёт update, не инициирует сетевой запрос.
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Это схема вызовов, не загрузочный цикл. Update/heartbeat не запускают просмотр;
play остаётся отдельным commit. После потери ответа восстанавливать status,
не повторять playback command. Только writer сообщает complete/error/cancel.

[Metadata example](../tests/fixtures/progressive-v2/continuous-hevc-aac.json):
один scalar resource, два required track: `kind=video, codecHint=hevc` и
`kind=audio, codecHint=aac-lc`, оба `containerHint=mp4`. Оба codec hints
обязательны. IDs, length и duration в примере синтетические, ranges пусты.
Заменить их реальными и публиковать SHA-256 blocks только для подтверждённых bytes.

## Ограниченный профиль

- Head moov, известные length/duration VOD ≤1 h, timebase 1/10000000,
  timestampOffset 0. Один self-contained ordinary MP4, без live/window,
  внешних init/segments/catalog и смены configuration.
- HEVC Main/Main10, 8/10-bit 4:2:0, single-layer hvc1/hev1, четыре байта NAL
  length, по одному VPS/SPS/PPS. Первый video packet — IRAP/key с PTS 0;
  отрицательный DTS и B-frame reorder ≤16 сохраняются. Видео может быть второй
  физической дорожкой: первый AAC packet с preroll не считается HEVC-пакетом.
- AAC-LC: mono/stereo, 44100/48000 Hz, canonical ASC без SBR/PS, 1024 samples
  на encoded frame. Presentation samples известны из MP4 index/edit list,
  initial skip 1024. Последний presentation frame может быть частичным;
  audio разрешено быть короче video, включая один единственный sample.
- Бюджеты прежние: source/read ≤128 MiB, shared/output cache ≤64 MiB,
  ≤50000 суммарных packets, ≤16 MiB/packet, один active job,
  8 pending updates/retained jobs, heartbeat 30 s, lifetime 1 h.

Worker сохраняет hvc1/hev1, compressed video/audio packets и обе исходные
шкалы PTS/DTS. Нет encoder/resampler и отдельного обнуления clocks. AAC
presentation window передаётся и подтверждается родителем до первых media bytes.
Cache повторно проверяет hvcC/ASC, track ID/rate и полное покрытие fragment
payload. HEVC/AAC mode не принимает AVC init и наоборот.

## Точный звук и завершение

После remux AAC packet может кодировать больше samples, чем входит в
presentation. Raw PCM fMP4 без применения окна не является эталоном конечной
длины. Immutable window задаёт rawBegin/rawEnd, timestampBegin/timestampEnd,
encodedEnd/endPadding; public status содержит rate/channels/presentationSamples.
Обе редакции Qt и Classic используют существующие sample-window adapters.
Плеер не маскирует лишние samples изменением duration или сменой аудиодорожки.

Short audio имеет собственный подтверждённый endpoint и не блокирует оставшееся
video. Общая доступная граница до него остаётся согласованной по обеим required
дорожкам. Неполные packets/headers ждут writer update, а не становятся EOF.
Счётчик HEVC sample index проверяется против **video** packets, а не суммарных
AAC+HEVC packets. Поэтому поддержка audio-first не отключает защиту SDK 1.88 от
false complete при потере последнего video sample. AAC endpoint также сверяется
с immutable presentation plan. Ошибка не становится успешным output complete.

Initial position, growing/queued seek, pause/resume и reader release наследуют
прежний контракт. Отмена paused playback с pending seek закрывает обе decoder
views, убирает цель и подтверждает source/output receipts. В Classic исправлен
латентный status race: Cancel/Forget, readerRelease и смена playback intent
теперь очищают ожидающую цель синхронно, до закрытия графа, без ожидания UI timer.
Исправление применяется ко всем continuous modes, не только HEVC/AAC.

## Проверка и границы

Worker suite: шесть профилей Main/Main10, hvc1/hev1, 44.1/48 kHz mono/stereo,
25 и 30000/1001 fps, audio-first, one-sample и short audio. Независимые packet
SHA/rational clocks и decoded video hashes сохраняются; PCM сравнивается в
явно объявленном presentation interval. Отдельный consumer suite проверяет
точное число/содержимое samples и seek windows на Qt/Classic без аудиоустройств.

Ordinary player API: ≥10 s настоящего просмотра до complete в каждом из шести
положительных профилей, physical append, partial moov, pause, initial/growing/
queued seek, natural EOF, cancel с pending seek, wrong hints/codecs/config,
truncated complete, release/forget и unchanged source. Classic использует
частный Null Video Renderer (Uncompressed), без видимого desktop output;
это не аттестация монитора. Qt-проверки проводятся отдельно на обеих ОС.

В [отчёте](ProgressiveContinuousHevcAacValidation-2026-11-13.json) сохранены
36 worker rows, 36 PCM consumer rows, 39 public-player API rows, 112 regression
rows прежних режимов и 18 дополнительных HEVC video-only worker checks.
38 SDK unit tests прошли. Все принятые harness/native build exits равны 0;
ожидаемые media errors worker возвращает с exit 1. Исходные failed runs и
Classic pending-seek baseline сохранены отдельно, не считаются PASS.
Hardware A/V sync, GPU/HDR, full codec/container matrix и final release остаются
отдельными пунктами. HE-AAC, multichannel, HEVC/Opus continuous, generic MOV/MKV,
live/config changes и другие R01 rows не добавляются этим профилем.
