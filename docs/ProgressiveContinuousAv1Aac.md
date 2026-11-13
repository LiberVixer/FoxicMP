# Непрерывный copy-remux обычного MP4 с AV1 и AAC-LC

SDK 1.91, приложение 0.2.0. Отдельный режим `continuous-av1-aac-window-v1`
для Linux Qt, Windows Qt и Windows Classic. Сохраняются AV1 video-only,
HEVC/H.264 modes и действующий API. Harvester не изменяется.

## Вызов

```python
from progressive_continuous import ContinuousRemux, AV1_AAC_MODE

preview = ContinuousRemux(client, session_directory, mode=AV1_AAC_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)  # только записанные и flush ranges/SHA
if preview.status()["playbackReady"]:
    preview.play(title="AV1/AAC preview", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Это последовательность вызовов, не цикл загрузчика. Writer публикует bytes,
подтверждённые ranges/SHA, revisions и heartbeat; только он сообщает complete.
SDK не скачивает URL, не делает remux, не создаёт таймеров и не повторяет `play`
при потере ответа. Для восстановления использовать `playback_status`.
[Правила владения, leases и receipts](ProgressiveContinuousSdk.md) сохраняются.

Нужна точная capability `continuousRemux.profiles.continuous-av1-aac-window-v1`,
включая video admission и PCM-window fields. Наличие AV1 в обычном decoder
или native fMP4, а также старый AV1 video-only mode новый профиль не разрешают.
Worker/cache/decoder согласуют mode и codec; runtime проверяется на каждый job.
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-av1-aac.json)
содержит placeholder length и пустые ranges, без медиа. Writer заменяет длину,
duration/identities и публикует только записанные bytes. В SDK ZIP имя примера —
`progressive-v2-continuous-av1-aac.json`.

## Входной профиль

- Ordinary MP4 с head moov, один известный scalar resource, две required tracks
  (video и audio) с обязательными `codecHint=av1` и `codecHint=aac-lc`.
  Оба `containerHint=mp4`; общий resource обязателен. Известный VOD ≤1 h,
  timeline 1/10000000, offset 0; без live, segment list и presentation window.
- AV1 Main8/Main10, 4:2:0, av01, single-layer. Остальные video admission поля
  [SDK 1.90](ProgressiveContinuousAv1.md) сохраняются: размерные low-overhead OBU,
  неизменная sequence configuration, один displayed Frame OBU/sample,
  первый keyframe с PTS 0, PTS=DTS. Hidden/show_existing/still frames,
  Frame Header/Tile Group и packet reorder не включены.
- AAC-LC mono/stereo, 44.1/48 kHz, 1024 samples/packet. Начальная задержка
  кодека ровно 1024 samples. Положительная presentation length ≤1 h задаёт
  точную обрезку конца; final partial AAC packet допускается. Audio может
  идти первым и быть короче video, включая одну presentation sample.
- Объявленный video sample count 1–50000 сверяется при complete. Потеря
  последнего video sample возвращает `incomplete-stream-av1-samples`;
  неполный AAC stream отклоняется независимо от video. Ложный demux EOF
  не заменяет writer complete.

## Звук и часы

Worker перед первым output byte публикует immutable PCM-window:
`rawBegin=1024`, `rawEnd=1024+presentationSamples`, timestamp interval
`[0,presentationSamples)`, encoded end и end padding. Managed parent и cache
повторно проверяют plan/hash; decoder должен подтвердить его применение.
Один output cache создаёт согласованные video/audio views и reader leases.
Video-only AV1 cache по-прежнему отвергает audio window.

Compressed video/audio payload, рациональные PTS/DTS и video durations
сохраняются. Выход — append-only fMP4 с edit lists и точной общей movie scale.
Никакого encoder/resampler или timestamp reset отдельных потоков нет.
PCM adapters отбрасывают priming/end padding и сохраняют presentation clock
при initial position, growing/queued seek и natural EOF. `max_interleave_delta`
250 ms ограничивает очередь mux timestamps при короткой audio track;
это не гарантия задержки физического звукового выхода.

Общие бюджеты прежние: input/read ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 packets суммарно, ≤16 MiB/packet, один active job, 8 pending updates,
8 retained jobs, heartbeat 30 s, lifetime 1 h. Большие и иные AV1-файлы,
допустимые обычному player, могут не пройти bounded progressive profile.

## Проверка и границы

[Отчёт с точными exit codes и SHA](ProgressiveContinuousAv1AacValidation-2026-11-13.json)
различает worker, PCM consumer и ordinary public-player acceptance трёх
редакций. Fixture generator — `tests/progressive_stream_av1_aac_test.py`:
8/10 bit, 25/30000/1001, mono/stereo, обе частоты, audio-first/one-sample/short
sound. Worker fixtures 24 s, PCM fixtures 5 s; каждая положительная public-player
fixture 24 s играет ≥10 s до complete. Partial output проверяется по original
packet prefix и clocks; full video — по decoded frame hashes.

PCM probes проверяют реальные Qt и Classic software trim adapters, включая
seek к последней sample и усечённый decoder output. Это не измерение physical
A/V sync, output latency, GPU/HDR или оборудования. Classic в SSH использует
private Null Video Renderer (Uncompressed); visible desktop output не аттестован.
Общие R01/R15/R17 и остальная container/codec/layout matrix остаются открытыми.
