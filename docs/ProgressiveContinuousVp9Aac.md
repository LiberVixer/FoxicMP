# Непрерывный copy-remux обычного MP4 с VP9 и AAC-LC

SDK 1.93, приложение 0.2.0. Явный paired mode
`continuous-vp9-aac-window-v1` для Linux Qt, Windows Qt и Windows Classic.
Прежние VP9 video-only, H.264/HEVC/AV1 modes сохраняются. Harvester не изменяется.

## Вызов

```python
from progressive_continuous import ContinuousRemux, VP9_AAC_MODE

preview = ContinuousRemux(client, session_directory, mode=VP9_AAC_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)  # только записанные/flush ranges и SHA
if preview.status()["playbackReady"]:
    preview.play(title="VP9/AAC preview", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Это последовательность API, не загрузчик. Writer публикует bytes, ranges/SHA,
revisions и heartbeat; только он сообщает complete. SDK не скачивает URL,
не делает remux и не создаёт таймеров. При потере ответа `play` использовать
`playback_status`, а не повторный запуск. Сохраняются
[правила владения и reader receipts](ProgressiveContinuousSdk.md).

Нужна точная capability `continuousRemux.profiles.continuous-vp9-aac-window-v1`,
включая video admission и все PCM-window fields. Наличие VP9 в native
fMP4/WebM или обычном decoder, а также video-only mode этого не разрешают.
Runtime проверяется на каждый job; нужны согласованные player/worker builds.
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-vp9-aac.json)
содержит placeholder length и пустые ranges, без медиа. Writer заменяет
length/duration/identities и публикует только подтверждённые bytes.
В SDK ZIP имя примера — `progressive-v2-continuous-vp9-aac.json`.

## Допустимый вход

- Ordinary MP4 с подтверждённым head moov ≤1 MiB/32 top-level atoms до `mdat`.
  Один known scalar resource и две required tracks: video с `codecHint=vp9`
  и audio с `codecHint=aac-lc`; оба `containerHint=mp4`, общий resource обязателен.
  Положительная known duration ≤1 h, timeline 1/10000000, offset 0;
  без live, segment lists и presentation window.
- VP9 Profile0/8-bit или Profile2/10-bit 4:2:0, vp09. Ограничения
  [video-only профиля](ProgressiveContinuousVp9.md) сохраняются: vpcC version 1,
  fixed coded/render size, PTS=DTS, один displayed frame/sample, без
  hidden/show_existing/indexed superframes. Первый sample — keyframe с PTS 0.
- AAC-LC mono/stereo, 44.1/48 kHz, 1024 samples/packet, начальный skip ровно 1024.
  Presentation sample count положителен и задаёт точную обрезку конца;
  final partial packet допускается. Audio может идти первым и быть короче
  video, включая одну presentation sample.
- Head parser различает `vide`/`soun`, проверяет число и sample entry обоих
  tracks; vpcC читается независимо от отсутствующей video extradata FFmpeg.
  AAC требует непустую ASC и собственную codec/config validation.
- Объявленный video sample count сверяется при complete. Потерянный последний
  video sample возвращает `incomplete-stream-vp9-samples`; AAC completeness
  проверяется отдельно. Demux EOF до writer complete не считается завершением.

Source window/codec и immutable track layout нельзя менять внутри job.
Обычный завершённый файл может играть, хотя этот bounded профиль его отклоняет.

## Звук, часы и сохранность

До первого output byte worker передаёт immutable PCM-window с
`rawBegin=1024`, `rawEnd=1024+presentationSamples`, timestamp interval
`[0,presentationSamples)`, encoded end и end padding. Parent/cache повторно
проверяют plan/hash; decoder подтверждает применение окна. Один shared cache
создаёт согласованные video/audio views. VP9 video-only cache по-прежнему
отклоняет audio window; paired mode выбирается явно.

Выход — append-only fMP4 без encoder/resampler. Compressed video/audio payload,
рациональные PTS/DTS и video durations сохраняются; initial AAC skip/end padding
применяются один раз PCM adapters при обычном запуске и seek.
Movie timescale задаётся точно для двух clocks, `max_interleave_delta=250 ms`
ограничивает mux timestamp queue при короткой audio track. Это не обещание
физической output latency или A/V sync. Media bytes не идут по public IPC,
сетевого fallback нет.

Общие бюджеты прежние: input/read ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 packets суммарно, ≤16 MiB/packet, один active job, 8 pending updates,
8 retained jobs, heartbeat 30 s, lifetime 1 h. Обычный допустимый плееру большой
MP4 может не пройти этот профиль. Head reread входит в общий read budget.

## Проверки

[Отчёт с exit codes и SHA](ProgressiveContinuousVp9AacValidation-2026-11-14.json)
разделяет worker, PCM consumer и ordinary public-player проверки трёх редакций.
Fixture generator — `tests/progressive_stream_vp9_aac_test.py`: 8/10-bit,
25/30000/1001, mono/stereo обеих частот, audio-first/one-sample/short audio.
Положительные public-player fixtures 24 s играют до writer complete; PCM fixtures 5 s
проверяют точное число presentation samples, seek до последней sample и отказ
на усечённом decoder output. Superframe negative fixture действительно содержит
такие пакеты после двухпроходного encode и последующего mux с AAC.

Проверка неполного playback ждёт и продвижения media position, и минимум 10 s
по локальным monotonic clocks. Они измеряются отдельно с наблюдением RPC:
позиция из ответа не является синхронным timestamp начала локального интервала.
Исходный Windows Qt FAIL сохранён: retained файл оставался неполным и
`outputComplete=false`; не прошёл старый wall-duration assertion. Точное старое
elapsed не было записано, поэтому отчёт не приписывает ему неподтверждённую причину.
[Снимок исходного теста](ProgressiveVp9AacApiBeforeWallBudget-2026-11-14.py.txt)
позволяет проверить этот gate отдельно от исправленного измерения.

Classic в SSH использует private Null Video Renderer (Uncompressed):
DirectShow decoding/control проверяются, visible desktop output не аттестован.
PCM probes не являются physical A/V, output latency, GPU/HDR или полной
R01/R15/R17 приёмкой. Wayland и отложенное оборудование не возобновляются.


[Поставка и проверка установленной Linux-копии](ProgressiveContinuousVp9AacDelivery-2026-11-14.json):
clean SDK 1.93 из implementation commit, атомарная замена EXE/worker,
резервная копия и неизменный INI. Ещё 25 сценариев (VP9/AAC, прежний H.264/AAC,
source-close) прошли с собственным SDK установленного плеера. Установки Windows
не менялись. Source SDK и проверенные кандидаты не являются новым итоговым release.
