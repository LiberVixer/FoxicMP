# Непрерывный copy-remux обычного AV1 MP4

SDK 1.90, приложение 0.2.0. Явный профиль `continuous-av1-video-v1` для
Linux Qt, Windows Qt и Windows Classic. Прежние H.264/HEVC/AAC modes
сохраняются. Harvester не изменяется.

## Вызов

```python
from progressive_continuous import ContinuousRemux, AV1_MODE

preview = ContinuousRemux(client, session_directory, mode=AV1_MODE)
preview.prepare(snapshot, "media")
# Writer публикует только записанные ranges/SHA и heartbeat.
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="AV1 preview", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Это последовательность API, не загрузчик. Неполученные bytes требуют следующего
writer update; SDK сам не скачивает, не remux-ит и не создаёт таймеров. После
потери ответа `play` опрашивать `playback_status`, не повторять запуск.
Сохраняются [владение, leases и receipts](ProgressiveContinuousSdk.md).

Нужна точная capability `continuousRemux.profiles.continuous-av1-video-v1`.
Наличие AV1 в native fMP4/WebM или обычном декодере этого режима не разрешает.
Согласованные player/worker builds обязательны; runtime проверяется на каждый job.
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-av1-video.json)
содержит placeholder length 1048576 и пустые ranges, без медиа. Реальные
length/duration/identities, SHA-256 blocks и flush bytes публикует writer.
В SDK ZIP пример называется `progressive-v2-continuous-av1-video.json`.

## Допустимый вход

- Ordinary MP4 с head moov, один scalar resource известной неизменной длины,
  ровно одна required video track; обязательны `containerHint=mp4`, `codecHint=av1`.
  Нет audio, live, segmented input или timestamp offset. Известная duration
  положительна и ≤1 h, session timebase 1/10000000, без presentation window.
- AV1 Main, 8/10-bit 4:2:0, sample entry `av01`, single-layer. Проверяются
  `av1C` version/marker/reserved bits, sequence header и совпадение объявленной
  глубины с форматом demux/decoder probe. Still/reduced-still picture запрещены.
- Low-overhead OBU с size field. Допустимы Sequence Header, Temporal Delimiter,
  Metadata, Frame и Padding (types 1,2,5,6,15), не более 128 OBU/sample.
  Extension byte допускает только нулевые spatial/temporal IDs и reserved bits.
  Sequence Header обязан быть в первом sample, последующие повторения должны
  побайтно совпадать с initialization. `NEW_EXTRADATA` запрещена.
- Ровно один Frame OBU и один отображаемый кадр на sample, без hidden frames,
  `show_existing_frame`, separate Frame Header/Tile Group или packet reorder.
  Первый sample — keyframe с PTS 0; все samples имеют PTS=DTS и положительную
  duration. MP4 index объявляет 1–50000 samples; complete с недостающим последним
  sample отвергается как `incomplete-stream-av1-samples`.

Profile не охватывает произвольный AV1 с alt-ref/hidden frames. Такие файлы
могут играть как обычные завершённые файлы; progressive admission этого mode
их отклоняет. AV1 + AAC подключается отдельным [paired mode SDK 1.91](ProgressiveContinuousAv1Aac.md).

## Сохранность и бюджеты

Выход — append-only fMP4: исходные compressed samples, рациональные
PTS/DTS/durations и 8/10-bit frames сохраняются. Нет encoder/resampler,
сетевого fallback или media bytes в public IPC. Managed AVIO читает только
confirmed ranges с SHA. EOF до writer complete не считается завершением.

Общие пределы прежние: input/read ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 packets, ≤16 MiB/packet, один active job, 8 pending updates, 8 retained
jobs, heartbeat 30 s, lifetime 1 h. Сохраняются initial position и bounded
published-prefix/queued seek SDK 1.85–1.87. Большой файл может не пройти этот
bounded профиль, даже если обычный плеер его открывает.

## Проверка

[Отчёт с точными кодами выхода и SHA](ProgressiveContinuousAv1Validation-2026-11-13.json)
отдельно учитывает native worker и ordinary public-player проверки трёх
редакций. Fixture generator: `tests/progressive_stream_av1_test.py`,
Main8/Main10 ×25/30000/1001, 24 s. Partial output сверяется по samples, clocks
и decoded frame hashes до writer complete. Если FFmpeg не выводит packet
`duration` для AV1 fMP4, oracle читает исходные `trun/trex`, без оценки по fps.

Classic в SSH-сеансе использует частный Null Video Renderer (Uncompressed);
проверены DirectShow decoding и управление, видимый desktop output не
аттестуется. Новый профиль video-only; он не доказывает physical A/V sync,
GPU/HDR или готовность всей R01. Исторические неудачи тестового стенда
сохраняются отдельно от итоговых прогонов.


[Поставка и проверка установленной Linux-копии](ProgressiveContinuousAv1Delivery-2026-11-13.json):
clean SDK 1.90 из указанного implementation commit, атомарная замена EXE/worker,
backup и неизменный INI. Ещё 22 сценария (AV1, прежний AAC, source-close)
прошли с собственным SDK установленного плеера. Windows установки не менялись.
Этот source SDK и проверенные кандидаты не объявляются новым итоговым release.
