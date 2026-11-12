# Непрерывный copy-remux обычного HEVC MP4

SDK 1.88, приложение 0.2.0. Новый явный профиль для Linux Qt, Windows Qt и
Windows Classic. Он расширяет continuous-путь SDK 1.82–1.87; прежние H.264
modes, defaults, AAC PCM-window, prepare/play и reader receipts сохраняются.
Harvester не изменяется.

## Выбор и вызов

Проверить `continuousRemux.profiles.continuous-hevc-reordered-video-v1`:
`videoCodec=hevc`, `videoOnly=true`, `audio=false`, `bFrames=true`,
`explicitSelectionRequired=true`, точные HEVC поля ниже. Наличие HEVC у native
fMP4 или у обычного декодера этого режима не разрешает. Runtime worker и
допустимость конкретного файла проверяются для каждого job.

```python
from progressive_continuous import ContinuousRemux, HEVC_MODE

preview = ContinuousRemux(client, session_directory, mode=HEVC_MODE)
preview.prepare(snapshot, "media")
# Writer публикует ranges/SHA и heartbeat. Это не запускает playback.
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="HEVC preview", activate=False)
# Тот же профиль поддерживает start_seconds, growing/queued seek SDK 1.85–1.87.
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
# Только writer подтверждает complete; EOF не выводится из длины файла.
preview.close(timeout=2)
preview.forget(timeout=2)
```

Это иллюстрация последовательности, не цикл загрузчика. Неполученный range
требует следующего writer update; SDK не скачивает, не remux-ит и не создаёт
таймеров. Нельзя повторять `play` после потерянного ответа: использовать
`playback_status`. Действуют [правила владения SDK](ProgressiveContinuousSdk.md).

[Пример metadata](../tests/fixtures/progressive-v2/continuous-hevc-video.json)
не содержит медиа: length 1048576 — placeholder, ranges пусты. Заменить length,
duration и identities реальными, публиковать только записанные/flush bytes с
SHA-256. В SDK ZIP пример называется `progressive-v2-continuous-hevc-video.json`.

## Точный входной профиль

- Один ordinary MP4 с head moov, один scalar resource с известной неизменной
  длиной и одна required video track. `containerHint=mp4`, **обязательный**
  `codecHint=hevc`; пустой hint не выбирает HEVC автоматически.
- HEVC Main/Main10, 8/10-bit 4:2:0, single-layer. Sample entry hvc1 или hev1,
  hvcC version 1, four-byte NAL lengths; ровно по одному VPS, SPS и PPS.
  Для hvc1 обязательны complete parameter-set arrays. Проверяются framing,
  reserved bits, profile/depth/chroma и NAL headers. Декодер подтверждает формат
  8/10-bit; он должен совпасть с hvcC declaration.
- Первый пакет — keyframe с PTS 0 и IRAP picture. B-frames и отрицательный
  initial DTS сохраняются, reorder delay ≤16. Положительная известная duration
  ≤1 h, session timebase 1/10000000, timestampOffset=0, без live/window.
  Head sample index должен объявлять 1–50000 пакетов; EOF принимается только
  после получения ровно этого количества. Обрезанный на границе пакета файл
  возвращает `incomplete-stream-hevc-samples`, даже если demuxer сообщил EOF.
- Parameter sets в hev1 media могут повторять initialization побайтно;
  изменённые VPS/SPS/PPS и `NEW_EXTRADATA` отвергаются. Video-only mode не
  создаёт audio track, PCM-window или audio output.

hvc1/hev1, compressed samples, рациональные PTS/DTS/durations и глубина
сохраняются в выходном append-only fMP4. Нет encoder/resampler, сетевого fallback
или передачи media bytes через public IPC. Все bytes читает managed AVIO
через прежние confirmed ranges и SHA; writer owns originals.

Бюджеты прежние: input/read ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 packets, ≤16 MiB/packet, один active job, 8 pending updates,
8 retained jobs, heartbeat 30 s, lifetime 1 h. Большой MP4, допустимый обычному
плееру, может не пройти этот bounded профиль.

## Ожидание, ошибки и жизненный цикл

Неполные head/index/config/sample bytes дают worker waiting и не означают EOF.
Cache публикует только проверенные init и полные moof/mdat. Один immutable
codec identity проходит через parent, cache, decoder source и Classic splitter.
Повторная индексация Classic разрешает HEVC только для этого mode; AVC modes
не расширяются.

Неверный/пустой hint, дополнительная audio track или неподходящий metadata
layout отвергаются до source admission: `unsupported-stream-source`.
Actual AVC при HEVC hint — `unsupported-stream-codec`, неподходящий hvcC —
`unsupported-stream-hevc-configuration`, неподходящее packet framing/initial
picture — `unsupported-stream-hevc-packet`, смена initialization в media —
`unsupported-stream-configuration-change`. Ошибки SHA, writer и I/O сохраняют
свои причины. Отказ admission не разрешает playback; поздняя ошибка завершает
уже начатый job с причиной и не становится успешным complete.

Prepare/play разделены, update/heartbeat не возобновляют остановленный просмотр.
Initial position и growing seek используют опубликованный presentation prefix;
неполученная цель с opt-in остаётся `waiting-range`. EOF — только после writer
complete, проверки producer digest и cache finalization. Close/Cancel/Forget
используют прежние source/output receipt и retirement пути.

## Проверка и границы

Worker suite `progressive-stream-remux-hevc` использует четыре 24-second
fixtures Main/Main10 × hvc1/hev1, 25 и 30000/1001 fps, B-frames. Сравниваются
полные и растущие выходные fragments: packet SHA, точные рациональные clocks,
decoded frame hashes с сохранением Main10. Negative cases охватывают старые
AVC modes, damaged hvcC/NAL, initial non-IRAP, changed in-band PPS и отмену.

`progressive-continuous-hevc-playback` запускает обычный Qt player на частных
профилях через SDK/IPC. Тот же сценарий исполняется на Windows Qt/Classic:
partial head/moov, физический append, ≥10 s просмотра до complete, initial
position, pause, growing/queued seek, natural EOF, source/output release и
cache removal. Сохранённый output сверяется с оригиналом отдельно от renderer.
В Classic через SSH используется явный частный `--classic-null-video`:
штатный DirectShow graph декодирует в Null Video Renderer (Uncompressed),
без вывода на монитор. Такой результат не подтверждает видимый renderer.
Контроль прежней сборки с обычным renderer в том же сеансе также не стартовал;
этот отказ сохранён отдельно, не превращён в PASS.

Результаты сборок и фактические exits сохраняются в
`ProgressiveContinuousHevcValidation-2026-11-12.json` после окончательных прогонов.
Это не измерение физического выхода/GPU/HDR и не итоговая release-приёмка.

Не добавляются HEVC+AAC, AV1/VP9 continuous, generic MOV/M4A/Matroska,
tail-moov/sparse media, смена качества/конфигурации, multiaudio или live.
[R01 и остальные пакеты](ProgressiveRemainingWork.md) остаются открытыми.
