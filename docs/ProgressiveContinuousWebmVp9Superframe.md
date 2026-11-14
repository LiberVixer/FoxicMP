# Непрерывный WebM/VP9: indexed superframes и alt-ref

SDK 1.97, приложение 0.2.0. Отдельный mode
`continuous-webm-vp9-superframe-video-v1` для Linux Qt, Windows Qt и Classic.
Расширяет [WebM VP9 video-only 25 fps](ProgressiveContinuousWebmVp9.md)
проверкой [indexed VP9 packets](ProgressiveContinuousVp9Superframe.md).
Прежний строгий WebM mode сохраняется и отклоняет indexed/hidden packets.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_SUPERFRAME_MODE

preview = ContinuousRemux(client, session_directory,
                          mode=WEBM_VP9_SUPERFRAME_MODE)
preview.prepare(snapshot, "media")
preview.publish(next_snapshot)
if preview.status()["playbackReady"]:
    preview.play(title="WebM VP9 alt-ref", activate=False)
preview.control("seek", seconds=12, allow_growing=True, wait_for_range=True)
preview.close(timeout=2)
preview.forget(timeout=2)
```

Точная capability выбранного mode обязательна. Writer публикует только
подтверждённые ranges/SHA и revisions/heartbeat/complete. SDK не скачивает URL,
не передаёт media bytes через IPC и не запускает play автоматически при update.
После потерянного play reply используется playback_status, без повторного запуска.
[Metadata-пример](../tests/fixtures/progressive-v2/continuous-webm-vp9-superframe.json)
содержит placeholder length/ranges: writer заменяет identities, length/duration
и публикует только записанные и flush bytes. В ZIP имя примера —
progressive-v2-continuous-webm-vp9-superframe.json.

## Допустимые packets

Один scalar resource, одна required VP9 video track, containerHint=webm.
DocType webm, unknown Segment, finite/unknown Clusters и SimpleBlock без lacing.
Profile0/8-bit или Profile2/10-bit 4:2:0, фиксированные coded/render размеры,
точные 25 fps, TimestampScale=1000000 ns, DefaultDuration=40000000 ns.
Timeline 1/10000000, offset 0, положительная duration кратна 400000 ticks.
Expected sample count duration/400000 обнаруживает пропавший целый последний block.

В packet допускаются 1–8 кадров: до семи скрытых inter frames, затем ровно один
показанный кадр. Indexed storage проверяется по двум markers, длинам каждого
кадра и точной сумме payload bytes. Raw single-frame packets также допустимы.
Первый packet — один показанный keyframe с PTS 0; PTS=DTS, шаг 40 ms.
Block key flag согласуется с первым frame header и demuxer packet map.
Запрещены standalone hidden, show_existing, hidden key/intra-only, несколько
показанных кадров, динамические размеры и изменения profile/depth/range.
Это проверка структуры/заголовков, не полная проверка compressed entropy.

Worker копирует весь compressed packet вместе с superframe index в fragmented
MP4 vp09/vpcC. Encoder/resampler отсутствуют; timestamps и durations сохраняются.
Последовательный EBML reader допускает hidden frames только при явном новом
mode. Native WebM index и его finite-Cluster ограничения не меняются.
Физический конец ресурса не считается EOF до writer complete; первая ошибка
AVIO фиксируется до завершения worker.

Бюджеты базового WebM режима сохраняются: head ≤1 MiB/32 elements, depth ≤8,
Cluster ≤16 MiB, ≤8192 Clusters, ≤32768 top-level elements после head,
≤65536 children/Cluster; input ≤128 MiB, shared output/cache ≤64 MiB,
≤50000 packets, ≤16 MiB/packet. Audio, fractional clocks, live, segments и
presentation windows этим mode не включаются.

## Проверка и следующие границы

[Отчёт с SHA и точными exit codes](ProgressiveContinuousWebmVp9SuperframeValidation-2026-11-14.json)
разделяет новые worker/player проверки, структурные пределы и прежние режимы
трёх редакций. Каждая положительная двухпроходная fixture должна содержать
реальные indexed superframes: 8/10-bit × finite/unknown Clusters, 24 s/600 packets.
Проверяются exact payload/clocks, decoded final и incomplete prefixes, ≥10 s
monotonic и ≥10.1 s media до complete, pause/seek/pending cancel/EOF/release/forget.
Отдельно отвергаются ошибочные index lengths, hidden/show_existing и конфигурации.

Classic использует отдельный INI и private null renderer; это не аттестация
видимого renderer или физической A/V синхронизации. YouTubeHarvester и Windows
установки не изменяются. WebM/Opus, fractional clocks и полная R01 остаются
открытыми; source SDK и кандидаты не являются итоговым бинарным release.

Регрессия queued replacement теперь записывает ordinary state до/после команды
старому playback и требует продвижения ≥0.25 s в пределах 2 s при сохранении
того же playing session. Первый Windows Qt FAIL одиночного 150 ms наблюдения
сохранён; исходный terminal snapshot не был записан, причина того события
не приписывается плееру или таймеру. [Прежний тест](ProgressiveQueuedSeekBeforePositionWait-2026-11-14.py.txt) сохранён для аудита.
