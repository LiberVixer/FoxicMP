# Непрерывный copy-remux обычного MP4 с VP9

SDK 1.92, приложение 0.2.0. Отдельный video-only mode
`continuous-vp9-video-v1` для Linux Qt, Windows Qt и Windows Classic.
H.264/HEVC/AV1 modes и paired AAC profiles сохраняются. Harvester не изменяется.

## Выбор и вызов

```python
from progressive_continuous import ContinuousRemux, VP9_MODE

preview = ContinuousRemux(client, session_directory, mode=VP9_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)  # только записанные/flush ranges и SHA
if preview.status()["playbackReady"]:
    preview.play(title="VP9 preview", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Это последовательность API, не загрузчик. Writer публикует bytes, revisions,
ranges/SHA и heartbeat; только он сообщает complete. SDK не скачивает URL,
не делает remux и не создаёт таймеров. После потерянного ответа `play`
опрашивать `playback_status`; повторный запуск не является восстановлением.
[Владение, leases и receipts](ProgressiveContinuousSdk.md) остаются прежними.

Нужна точная capability `continuousRemux.profiles.continuous-vp9-video-v1`.
Native fMP4/WebM VP9 и обычный decoder не разрешают этот режим автоматически.
Worker/cache/decoder согласуют codec и mode; runtime проверяется на каждый job.
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-vp9-video.json)
содержит placeholder length и пустые ranges, без медиа. Заменить длину,
duration/identities и SHA реальными. В SDK ZIP имя примера —
`progressive-v2-continuous-vp9-video.json`.

## Допустимый вход

- Один ordinary MP4 с head moov, один known scalar resource неизменной длины,
  одна required video track. Обязательны `containerHint=mp4`, `codecHint=vp9`.
  Нет audio, live, segment lists и presentation window; положительный known VOD
  ≤1 h, timeline 1/10000000, timestampOffset 0.
- VP9 Profile0/8-bit или Profile2/10-bit, 4:2:0, sample entry `vp09`.
  `vpcC` — FullBox version 1/flags 0, фиксированные 12 bytes с пустым init data.
  Проверяются profile/depth pair, level, chroma, range и допустимые color fields.
- Worker самостоятельно читает подтверждённый head: `ftyp`, допустимые
  free/skip/wide atoms и один `moov` до `mdat`, суммарно ≤1 MiB/32 atoms.
  FFmpeg не передаёт этот record в `extradata`; пустая extradata не заменяет
  проверку реального `vpcC`. Head читается только через managed ranges/SHA,
  затем тот же источник перематывается перед demux. Дополнительные reads
  входят в общий read budget.
- Первый sample — отображаемый keyframe с PTS 0. У всех packets PTS=DTS,
  положительная duration, один displayed frame/sample. Запрещены скрытые кадры,
  `show_existing_frame`, indexed superframes (включая singleton), profile/depth
  изменения и `NEW_EXTRADATA`. Coded и render dimensions фиксированы и
  совпадают с sample entry. Key headers сверяют depth/range и sync marker;
  inter headers проверяют reference-size или явные размеры.
- Head sample count 1–50000 сверяется при complete. Потеря последнего sample
  возвращает `incomplete-stream-vp9-samples`; demux EOF до writer complete не
  является завершением. Полная entropy/DPB decoding validation остаётся ролью
  decoder; bounded header admission не обещает исправить любой повреждённый VP9.

Обычный завершённый файл может воспроизводиться, даже если этот узкий
progressive профиль его отклоняет. VP9 + audio — отдельный следующий этап.

## Сохранность и пределы

Выход — append-only fMP4 без encoder/resampler. Compressed samples,
рациональные PTS/DTS/durations и decoded video hashes сохраняются.
В проверенных fixtures совпадает также `vpcC`; level declaration передаётся
muxer отдельно, поскольку probe оставляет его неопределённым.
Media bytes не передаются по public IPC, сетевого fallback нет.

Бюджеты прежние: input/read ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 packets, ≤16 MiB/packet, один active job, 8 pending updates, 8 retained
jobs, heartbeat 30 s, lifetime 1 h. Большой допустимый обычному player файл
может не пройти bounded remux. Initial position и published-prefix/queued seek
SDK 1.85–1.87 доступны с прежними явными opt-in.

## Проверки

[Отчёт с exit codes и SHA](ProgressiveContinuousVp9Validation-2026-11-13.json)
разделяет worker, ordinary public-player и прежние regression rows трёх редакций.
Fixtures — `tests/progressive_stream_vp9_test.py`: Profile0/Profile2 ×25/30000/1001,
24 s; положительные player fixtures играют ≥10 s до complete. Partial output
сверяется по original packet/frame prefix, final output — по exact payload,
рациональным clocks и decoded frame hashes. Packet duration берётся из ISO
`trun/trex`, если ffprobe не сообщает её, без оценки по fps.

Негативный файл с alt-ref создаётся двухпроходным libvpx. Тест проверяет
наличие реальных superframes в полученном MP4, затем отказ worker; одной
encoder option недостаточно. Ошибки раннего стенда сохранены в отчёте.
Bounds admission дополнительно проверяется ASan/UBSan и MSVC.
Classic в SSH использует private Null Video Renderer (Uncompressed):
DirectShow decoding/control проверены, visible desktop output не аттестован.
Этот video-only профиль не является physical A/V, GPU/HDR или общей R01/R17
приёмкой. Wayland и отложенное оборудование не проверяются.

Основание полей контейнера — [VP Codec ISO Media File Format Binding](https://www.webmproject.org/vp9/mp4/).
Порядок uncompressed header сверяется с [официальным VP9 decoder libvpx](https://chromium.googlesource.com/webm/libvpx/+/master/vp9/decoder/vp9_decodeframe.c).
