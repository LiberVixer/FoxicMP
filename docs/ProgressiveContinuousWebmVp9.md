# Непрерывный WebM copy-remux: VP9, 25 fps

SDK 1.96, приложение 0.2.0. Отдельный явный mode
`continuous-webm-vp9-video-v1` для Linux Qt, Windows Qt и Classic.
[Предварительное сравнение](ProgressiveWebmVp9RemuxPlan.md) сохранено отдельно:
его результаты не подменяют проверки нового публичного пути.
YouTubeHarvester и настройки установленных Windows-плееров не меняются.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_MODE

preview = ContinuousRemux(client, session_directory, mode=WEBM_VP9_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="WebM VP9 preview", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Writer публикует только записанные и flush ranges с SHA, revisions, heartbeat и
подтверждением complete. SDK проверяет точную capability выбранного mode;
он не скачивает URL и не пересылает media через IPC. После потерянного play reply
используется playback_status, без повторного запуска.
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-webm-vp9.json)
содержит placeholder length и пустые ranges; writer заменяет identities,
length/duration и публикует только подтверждённые bytes.

## Вход и границы

Один scalar resource, одна required video track, codecHint=vp9,
containerHint=webm. DocType webm, Segment неизвестного размера, finite либо
unknown-size Clusters, только SimpleBlock без lacing/BlockGroup. Метаданные и
границы разбираются структурно; поиска сигнатур в compressed payload нет.
Native WebM index и его прежние finite-Cluster ограничения не меняются.

VP9 Profile0/8-bit или Profile2/10-bit, 4:2:0, один показанный кадр/block,
без hidden/alt-ref/superframes/show_existing. Первый block — keyframe, PTS 0;
дальше точный шаг 40 ms, PTS=DTS. TimestampScale=1000000 ns,
DefaultDuration=40000000 ns, фиксированные coded/render размеры без container
crop/display transformations. Configuration из header/keyframe согласуется с
каждым packet и параметрами demuxer. Это проверка структуры и заголовков,
не полная проверка compressed entropy. Output — vp09/vpcC в fragmented MP4,
без encoder/resampler; compressed payload, clocks и декодированные кадры сохраняются.

Timeline 1/10000000, offset 0, duration положительная и кратна 400000 ticks.
Ожидаемое число кадров duration/400000 передаётся worker отдельным mode-scoped
полем. Пропажа целого последнего block обнаруживается даже в unknown Cluster;
физический EOF не завершает источник до writer complete. Duration в Info,
если присутствует, должна совпадать с timeline. Audio, live, segments и
presentation windows в этот mode не входят.

Бюджеты: head ≤1 MiB/32 elements, metadata depth ≤8, Cluster ≤16 MiB,
≤8192 Clusters, ≤32768 top-level elements после head, ≤65536 children/Cluster;
input/worker reads ≤128 MiB, output/shared cache ≤64 MiB,
≤50000 video packets, ≤16 MiB/packet. Общие heartbeat/lifetime/job/update
ограничения сохраняются. Первая ошибка AVIO фиксируется и не заменяется
последующим повторным чтением FFmpeg.

## Проверки

[Отчёт с SHA и точными exit codes](ProgressiveContinuousWebmVp9Validation-2026-11-14.json)
разделяет worker, публичный player API и регрессии каждой редакции.
Положительные случаи: 8/10-bit × finite/unknown Clusters, 24 s/600 frames,
точные payload/PTS/DTS/durations, decoded final/incomplete prefixes и неизменный
источник. Плеер должен играть ≥10 s monotonic и пройти ≥10.1 s media до complete;
seek/pause/pending cancellation, EOF, release/forget проверяются отдельно.
Отказы включают чужой codec/DocType, fractional clock, audio/alt-ref, lacing,
неверные flags/track/config, пропущенный либо усечённый block и неверный sample count.
Диагностический parser проверяется с ASan/UBSan на Linux, MinGW и MSVC на Windows.

Classic проверяется с отдельным INI и private null renderer; это не измерение
физической A/V синхронизации и не приёмка видимого desktop renderer.
Прежние MP4 modes остаются самостоятельными и не принимают WebM автоматически.
WebM/Opus, fractional clocks, indexed superframes и другие контейнерные layouts
требуют отдельных профилей. R01 и полное ТЗ остаются открытыми.


[Поставка и проверка установленной Linux-копии](ProgressiveContinuousWebmVp9Delivery-2026-11-14.json):
clean SDK 1.96 из implementation commit, атомарная замена EXE/worker,
резервная копия и неизменный INI. Ещё 29 сценариев (WebM VP9 video-only, прежний indexed VP9/AAC,
source-close) прошли с собственным SDK установленного плеера. Установки Windows
не менялись. Source SDK и проверенные кандидаты не являются новым итоговым release.
