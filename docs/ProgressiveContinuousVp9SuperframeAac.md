# Непрерывный MP4 copy-remux: VP9 superframes/alt-ref и AAC-LC

SDK 1.95, приложение 0.2.0. Новый явный mode
`continuous-vp9-superframe-aac-window-v1` для Linux Qt, Windows Qt и Classic.
Это paired-профиль: ограничения [indexed VP9 video-only](ProgressiveContinuousVp9Superframe.md)
объединены с [точным AAC PCM-window](ProgressiveContinuousVp9Aac.md).
Прежние modes/defaults сохраняются. YouTubeHarvester не изменяется.

```python
from progressive_continuous import ContinuousRemux, VP9_SUPERFRAME_AAC_MODE

preview = ContinuousRemux(client, session_directory, mode=VP9_SUPERFRAME_AAC_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="VP9 alt-ref / AAC preview", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Нужна точная capability выбранного mode с video admission и всеми PCM-window
fields. Наличие VP9/AAC decoder или двух прежних modes не разрешает новый путь.
Writer публикует только записанные/flush ranges и SHA, revisions/heartbeat/complete.
SDK не скачивает URL, не делает remux и не передаёт media bytes через IPC.
После потерянного play-ответа используется playback_status, без повторного запуска.
Сохраняются [владение и reader receipts](ProgressiveContinuousSdk.md).
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-vp9-superframe-aac.json)
содержит placeholder length и пустые ranges. В ZIP имя примера —
progressive-v2-continuous-vp9-superframe-aac.json. Writer заменяет identities,
length/duration и публикует только подтверждённые bytes.

## Вход и согласование дорожек

- Ordinary faststart MP4, один scalar resource, две required tracks vide/soun:
  codecHint=vp9 и codecHint=aac-lc, оба containerHint=mp4. Подтверждённый head moov
  ≤1 MiB/32 top-level atoms до mdat; один vp09/vpcC и один mp4a/ASC.
- VP9 Profile0/8-bit или Profile2/10-bit, 4:2:0, vpcC version 1. PTS=DTS,
  фиксированные coded/render размеры, один показанный кадр/sample. В indexed sample
  1–8 кадров: до семи скрытых inter frames, показанный кадр последним. Первый sample
  — один показанный keyframe с PTS 0. Raw single-frame samples тоже допустимы.
- Show_existing, hidden key/intra-only, standalone hidden, несколько показанных
  кадров/sample, динамические размеры и изменения codec/config отклоняются.
  Разбор индекса/заголовков совпадает с SDK 1.94; это prefix admission,
  не полная проверка compressed entropy.
- AAC-LC mono/stereo, 44.1/48 kHz, 1024 samples/packet. Начальный skip ровно 1024,
  положительный presentation sample count задаёт точную обрезку конца.
  Audio может идти первым и быть короче video, включая одну presentation sample.
- Known duration >0 и ≤1 h, timeline 1/10000000, offset 0; без live, segments,
  presentation window и дополнительных дорожек. Обе дорожки ссылаются на один resource.

До output bytes worker передаёт immutable PCM-window: rawBegin=1024,
rawEnd=1024+presentationSamples, timestamp interval [0,presentationSamples),
encoded end и padding. Один shared cache проверяет plan/hash и создаёт согласованные
video/audio views; decoder подтверждает применение окна при запуске и seek.
Video-only indexed mode по-прежнему не принимает audio, прежний VP9/AAC mode
по-прежнему не принимает superframes. Выбор нового режима явный.

Copy-remux сохраняет compressed payload вместе с superframe index, рациональные
PTS/DTS и video durations; encoder/resampler и network fallback отсутствуют.
Movie timescale согласован с двумя clocks; max_interleave_delta=250 ms ограничивает
mux queue при коротком audio. Начальный skip/end padding применяются один раз
PCM adapters. Потерянный последний video sample и неполный AAC обнаруживаются
раздельно. Demux EOF до writer complete не завершает источник.

Общие бюджеты прежние: input/worker reads ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 packets суммарно, ≤16 MiB/packet, один active job, восемь pending updates,
восемь retained jobs, heartbeat 30 s и lifetime 1 h. Большой обычный MP4 может
играть штатным decoder, но не пройти этот bounded progressive mode.

## Проверки и границы

[Отчёт с SHA и exit codes](ProgressiveContinuousVp9SuperframeAacValidation-2026-11-14.json)
разделяет worker, PCM consumer, ordinary public-player API и прежние режимы
каждой редакции. Реальные двухпроходные fixtures обязаны содержать indexed
superframes в каждой положительной строке: 8/10-bit, 25/30000/1001, mono/stereo
обеих частот, audio-first/one-sample/short audio.

Проверяются payload/clocks, финальные video hashes и декодированные неполные
prefixes, точное число PCM samples, seek к последней sample и отказ при усечённом
PCM output. Public-player fixtures 24 s играют до writer complete ≥10 s по
локальным monotonic clocks и продвигают media position ≥10.1 s. Проверяются
initial/growing/queued seek, pause, pending-seek cancel, EOF/release/forget.
Qt PCM-probe проверяет player filter adapter; Classic PCM-probe проверяет
native AAC trim/flush/replace decoder component и отказ при truncation, а не
захватывает output графа. Ordinary Classic graph state/control проверяются отдельно.
PCM fixtures имеют длительность 5 s. Они проверяют обрезку/данные и не измеряют
физическую A/V синхронизацию или output latency.

Неизменённый header/index parser сохраняет отдельное доказательство
[ASan/UBSan/MSVC SDK 1.94](ProgressiveContinuousVp9SuperframeValidation-2026-11-14.json):
новый отчёт связывает его по SHA, не выдаёт прежний запуск за повторный тест.
Classic проверяется через private Null Video Renderer (Uncompressed) в SSH;
visible desktop output, GPU/HDR и оборудование не аттестуются. Другие контейнеры,
show_existing/intra-only, прочие audio layouts и общие R01/R15/R17 остаются открытыми.

Первый Linux PCM-прогон сохранил FAIL: прежнее расписание 1/2/3/4 s оставляло
ключевой кадр на 4 s недописанным в пятисекундном источнике с GOP 2 s, поэтому
до complete закрывался только один fragment. Для этого режима PCM-публикации
перенесены на 1.5/2.5/3.5/4.75 s; требование минимум двух distinct incomplete
fragments и exact decoded-prefix проверка сохранены. Product binaries не менялись.
[Снимок прежнего теста](ProgressiveVp9SuperframeAacBeforePrefixSchedule-2026-11-14.py.txt)
связывает первоначальные worker reports с их фактическим test source; обычные
24 s worker fixtures используют прежнее расписание 4/8/12/20 s.


[Поставка и проверка установленной Linux-копии](ProgressiveContinuousVp9SuperframeAacDelivery-2026-11-14.json):
clean SDK 1.95 из implementation commit, атомарная замена EXE/worker,
резервная копия и неизменный INI. Ещё 26 сценариев (VP9 superframes/AAC, прежний indexed VP9 video-only,
source-close) прошли с собственным SDK установленного плеера. Установки Windows
не менялись. Source SDK и проверенные кандидаты не являются новым итоговым release.
