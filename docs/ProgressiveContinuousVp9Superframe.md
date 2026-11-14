# Непрерывный copy-remux MP4: VP9 indexed superframes/alt-ref

SDK 1.94, приложение 0.2.0. Явный video-only mode
`continuous-vp9-superframe-video-v1` для Linux Qt, Windows Qt и Windows Classic.
Режимы SDK 1.92/1.93 по-прежнему отклоняют superframes; Harvester не изменяется.

```python
from progressive_continuous import ContinuousRemux, VP9_SUPERFRAME_MODE

preview = ContinuousRemux(client, session_directory, mode=VP9_SUPERFRAME_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="VP9 alt-ref preview", activate=False)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Нужен exact capability выбранного mode, а не наличие VP9 decoder или прежнего
video-only mode. Writer публикует только записанные/flush ranges и SHA, revisions,
heartbeat и complete. SDK не скачивает URL и не передаёт media bytes через IPC.
При потерянном ответе play используется playback_status, а не повторный запуск.
Сохраняются [правила владения](ProgressiveContinuousSdk.md) и все бюджеты
[строгого VP9 профиля](ProgressiveContinuousVp9.md).
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-vp9-superframe.json)
содержит placeholder length и пустые ranges; он не содержит видео или разрешения
читать неподтверждённые bytes. В ZIP имя — progressive-v2-continuous-vp9-superframe.json.

## Разрешённый профиль

- Ordinary faststart MP4, один required video track vp09/codecHint=vp9,
  containerHint=mp4, один scalar resource. Head moov ≤1 MiB/32 top-level atoms
  до mdat, строгий vpcC version 1; Profile0/8-bit или Profile2/10-bit, 4:2:0.
- Known duration >0 и ≤1 h, zero origin, timeline 1/10000000, offset 0;
  без live/segments/presentation window, audio и изменения конфигурации.
- PTS=DTS; один отображаемый кадр на MP4 sample. Raw single-frame samples
  разрешены. Несколько кадров в одном sample требуют согласованного superframe
  index: 1–8 положительных little-endian lengths, 1–4 bytes/length, точная сумма
  payload и совпадающие markers. maxFramesPerSample=8, maxHiddenFramesPerSample=7.
- В indexed sample перед последним отображаемым кадром допустимы только скрытые
  inter frames. Запрещены show_existing, скрытые keyframes/intra-only,
  standalone hidden samples, несколько показанных кадров и показанный не последним.
  Начальный sample содержит один показанный keyframe с PTS 0, включая indexed singleton.
- У каждого внутреннего кадра проверяются profile, показ/скрытие, заголовок
  key/inter, coded/render dimensions. Размеры фиксированы; первый keyframe
  инициализирует все reference slots, дальнейшие кадры сохраняют этот инвариант.
  Это prefix admission, не полная проверка compressed entropy; её отдельно
  проверяют реальные decode/fidelity tests.
- При неоднозначном compressed tail без согласованной пары markers применяется
  single-frame parsing; parser не угадывает расположение нескольких кадров.
  Для распознанного index неверные lengths/sum отклоняются явно.

Разбор заголовков сверялся с [эталонным libvpx, read_uncompressed_header и
setup_frame_size_with_refs](https://chromium.googlesource.com/webm/libvpx/+/refs/heads/main/vp9/decoder/vp9_decodeframe.c).
Внутренние кадры и index копируются без изменения; encoder/resampler отсутствуют.
Movie/sample clocks, fragment index, bounded growing/queued seeks и lease release
следуют прежнему video-only контракту. Demux EOF до writer complete не завершает job.

## Проверки и границы

[Отчёт, SHA и exit codes](ProgressiveContinuousVp9SuperframeValidation-2026-11-14.json)
разделяет native worker, public player и регрессии трёх редакций.
Fixtures создаются реальным двухпроходным libvpx-vp9 с lag/alt-ref, 8/10-bit и
25/30000/1001. Каждая положительная fixture обязана содержать indexed superframes.
Сравниваются исходные/выходные packet bytes, PTS/DTS/durations, vpcC и все
декодированные кадры, включая неполные output prefixes.

Public API отдельно проверяет ≥10 s по локальным monotonic clocks и продвижение
media position ≥10.1 s до writer complete, pause/initial/growing/queued seek,
EOF/release/forget и invalid metadata/media. Header/index tests включают malformed
lengths, максимум восемь кадров, показ/скрытие и fuzz под ASan/UBSan и MSVC.

Classic использует private Null Video Renderer (Uncompressed) через SSH:
DirectShow decode/control проверяются, видимый desktop output не аттестуется.
Physical A/V/output latency/GPU/HDR не заявляются. VP9 superframes + AAC,
show_existing/intra-only, другие контейнеры и общие R01/R15/R17 остаются открытыми.
