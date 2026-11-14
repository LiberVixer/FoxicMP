# R01: подготовка continuous-входа WebM/VP9

14 ноября 2026. Исходная база SDK 1.95, приложение 0.2.0.
Это проверенный план следующего профиля. Новый public mode ещё не объявлен,
код плееров/worker и установленные приложения на этом этапе не обновляются.
YouTubeHarvester не изменяется.

## Что уже есть и зачем нужен отдельный путь

[Native standalone WebM](ProgressiveStandaloneWebmVideo.md) уже воспроизводит
VP9 Profile0/8-bit и Profile2/10-bit, включая дробный DefaultDuration.
[Native muxed WebM](ProgressiveMuxedWebm.md) уже согласует VP9/Opus;
отдельно существуют VP8/AV1/Vorbis профили. Их ограничения и поддержку сохраняем.

В текущем `WebMIndex` каждый Cluster должен иметь конечный размер. Между тем
[Matroska RFC 9559, Cluster](https://www.rfc-editor.org/rfc/rfc9559.html#name-cluster-element)
разрешает unknown-sized Cluster. Подготовленные примеры с такими кластерами
декодируются FFmpeg и копируются в fMP4 без изменения video payload/clocks,
но отвергаются существующим native index. Это конкретное расширение входа,
а не повторная реализация уже работающего finite-Cluster native режима.

Continuous добавит отдельные worker/shared fMP4 cache и explicit play commit,
с прежними growing/queued seek и lease semantics. Само обычное открытие WebM,
FFmpeg copy reference или наличие native capability его не включают.

## Что измерено

[Машинный отчёт](ProgressiveWebmVp9RemuxPreflight-2026-11-14.json) содержит отдельные
результаты Linux, Windows Qt и Windows Classic worker builds, SHA диагностических
index runners, команд, source/header/test SHA и точные exits.

На каждой платформе 12 строк: Profile0/8-bit и Profile2/10-bit × 25/30000/1001 fps
× finite/unknown-sized Clusters, плюс missing DefaultDuration, реальный alt-ref,
VP9/Opus и Matroska DocType. Основные источники — 24 s. Live-compatible header
создаётся `-live 1`; это форма EBML, а не тест live playback. Session планируется VOD.

- 25 fps: четыре finite/unknown строки сохраняют packet SHA, рациональные PTS/DTS
  и durations, все decoded frame hashes; то же проверено для физически обрезанных
  префиксов, заканчивающихся после полных blocks/Clusters до конца исходника.
- Native index принимает finite-Cluster строки и отвергает unknown-sized Clusters.
  Его payload/start clocks сверены с независимым ffprobe; end вычисляется отдельно
  по nanosecond DefaultDuration с округлением в 100 ns. Это проверка parser/index,
  не новое доказательство playback существующих приложений.
- При 30000/1001 fps наблюдается важная граница. В исходном WebM TimestampScale
  равен 1 ms и packet durations представлены по этой сетке. В fMP4 часть sample
  durations меняется с 33 на 34 ms при сохранении PTS/DTS и compressed payload.
  Native nanosecond DefaultDuration тоже не тождественен этим packet durations.
  Сохранять все три трактовки времени как одну без отдельного решения нельзя.
- Missing DefaultDuration оставляет часть packet durations неопределённой до
  demux inference; это не основание выдавать угаданное время за сохранённое.
- Alt-ref control действительно содержит hidden-leading/indexed packets.
  MP4/WebM mapping и duration/no-display правила требуют отдельной приёмки;
  первый новый профиль их не разрешает. Уже существующие MP4 modes SDK 1.94/1.95
  сохраняют своё разрешение indexed superframes.
- VP9/Opus control не является отрицательным тестом всего native muxed WebM.
  Здесь вызывается только single-track inspector, а copy oracle намеренно
  извлекает video и отбрасывает audio. В Linux reference с FFmpeg 6.1.1 video PTS/DTS сдвинулись на 7 ms;
  в Windows references с FFmpeg 9.0.2 сдвига не было. Toolchain/source differences
  здесь не изолированы как единственная причина. Это не доказательство
  remux/синхронизации пары и не разрешение переносить origin из общего audio context.
- Matroska DocType способен пройти общий FFmpeg demuxer, но не становится WebM.
  Будущий WebM mode должен отклонять его явно, как и native WebM admission.

Существующий worker принимает только `formatHint=mp4`. На каждом build отдельно
проверены отказ `formatHint=webm`, отказ предложенного mode и отказ старого
MP4/VP9 mode на WebM bytes при неполном подтверждённом prefix. Каждый ожидаемый
отказ возвращает native exit 1, без output media; отсутствие capability проверено.
Это не тест нового worker WebM parser: его пока нет.

Физически обрезанный prefix для copy oracle имеет собственный EOF. Это позволяет
проверить packet/pixel fidelity reference, но не означает правильное ожидание
writer update, ≥10 s просмотра, source receipts или real decoder graph.
Плееры Qt/Classic, desktop/audio session и Harvester в этой серии не запускались.

## Первый контракт для реализации

Предложенное имя `continuous-webm-vp9-video-v1` зарезервировано только в плане.
SDK 1.95 его не принимает и не рекламирует. После реализации и приёмки:

| Область | Начальное ограничение |
| --- | --- |
| Модель | Один required video/vp9 track, containerHint=webm, scalar resource с SHA/ranges от 0; external init, segments, audio и optional tracks не допускаются |
| Длина/время | Известная положительная resource length ≤128 MiB; VOD duration ≤1 h, timebase 1/10000000, timestampOffset=0, без presentation window/live |
| Часы | Только 25 fps: DefaultDuration=40000000 ns, TimestampScale=1000000 ns, первый PTS/DTS=0 и шаг строго 40 ms; положительные packet durations строго 40 ms |
| Полнота | Timeline duration кратна 400000 ticks; ожидаемое число video samples выводится из неё и проверяется при complete, максимум 50000 packets |
| Контейнер | DocType webm, один unknown-size Segment; head Info/Tracks до Cluster, без index-seek в неопубликованный tail; finite и unknown-sized Clusters |
| Blocks | Только complete SimpleBlock без lacing/invisible/reserved flags, один отображаемый VP9 кадр на packet; BlockGroup и его duration/reference semantics пока отдельный этап |
| Codec | Profile0/8-bit либо Profile2/10-bit, 4:2:0, первый показанный keyframe; фиксированные coded/render dimensions, без hidden/show_existing/superframes/config changes/encryption |
| Выход | Append-only fMP4/vp09, неизменные compressed bytes и точные PTS/DTS/durations, без encoder/resampler и network fallback |
| Бюджеты | Head ≤1 MiB/32 элементов, Cluster span ≤16 MiB, ≤8192 Clusters/32768 top-level elements/65536 children per Cluster, master depth ≤8, packet ≤16 MiB, output/shared cache ≤64 MiB; прежние worker read/job/update/heartbeat/lifetime budgets не повышаются |

Known-size Segment, другие fps/variable clocks, BlockGroup/lacing, отсутствующие
или противоречивые duration hints, alt-ref/show_existing, audio/Opus и обычный
Matroska не включать автоматически. Эти ограничения касаются нового continuous
mode; native WebM и прежние MP4 modes продолжают работать по своим capabilities.

## Работа по слоям

1. **Модель/controller/SDK.** Новый enum/mode, точные container/codec/clock fields,
   capability с `explicitSelectionRequired`; прежние MP4 defaults и admissible
   layouts не расширять. До feature acceptance mode остаётся необъявленным.
2. **EBML admission.** Отдельный bounded scanner проверяет DocType, Info, TrackEntry,
   DefaultDuration/scale и complete Block bodies до публикации output. Для unknown
   Cluster хранит master hierarchy/счётчик bytes и распознаёт sibling boundaries
   по EBML-структуре, не по поиску байтовой сигнатуры внутри compressed payload.
   Незавершённый header/block ждёт update; writer complete с ним — ошибка.
   Ограничить количество top-level/Cluster children, глубину и metadata allocations.
   Finite Cluster native parser не ослаблять ради этого opt-in.
3. **Worker input/protocol.** Разрешить `formatHint=webm` только новому mode и выбрать
   Matroska/WebM demuxer после проверки DocType. WebM не предоставляет MP4 `nb_frames`
   и head vpcC: убрать эти MP4 assumptions только в новой ветке. Parent передаёт
   immutable expected video-sample count из timeline отдельным mode-scoped start
   field; строгая прежняя форма четырёх полей MP4 worker start сохраняется.
   EOF без writer complete остаётся ожиданием; потерянный последний sample не
   превращается в успешное окончание валидного, но укороченного WebM.
4. **VP9 configuration.** Сверить первый bitstream keyframe, TrackEntry/dimensions,
   optional VP9 CodecPrivate TLV и реальные codec parameters. Получить проверяемый
   output vpcC; отсутствие input vpcC не заполнять значениями из codecHint.
   Каждый packet проходит прежний strict displayed-frame validator; width/render,
   depth/profile/range contradictions и hidden packets — явный отказ.
5. **Output/cache/decoder.** Пронести vp9 identity через shared cache, оба индекса,
   Qt и Classic adapter. Сохраняются видео-only decoder view, release receipts,
   initial/growing/queued seek. Наличие общего VP9 cache само по себе не разрешает
   новый input container или неподтверждённые bytes.
6. **Приёмка.** Все три ordinary player builds: 24 s fixtures, ≥10 s monotonic и
   ≥10.1 s media до complete; complete и unknown Cluster boundaries, publication
   внутри EBML header/Block/packet, exact clocks/frames на prefixes и полном output.
   Ошибки: missing last sample, trailing/truncated Block, forged frame count,
   неверный DocType/codec/profile/clock, lacing/extra audio/hidden/config changes,
   writer fail/cancel, SHA/replacement, bounded limits. Pause/initial/growing/queued
   seek, pending cancel, EOF/release/forget и прежние MP4/VP9/AAC regressions.
   Classic — private INI с ContextMenuPrompted=1/ContextMenuEnabled=0 и HKCU audit;
   Linux UI/audio — только частные X11/D-Bus/PulseAudio через run_tests.sh.
7. **Поставка после приёмки.** Обновить capability/SDK/matrix, затем Linux EXE/worker
   атомарно с backup и сохранением текущего INI. Этот подготовительный документ
   не требует bump версии, сборки плеера или повторной установки.

## Воспроизведение подготовки

Диагностический runner использует production index и CLI из репозитория:

```sh
g++ -std=c++17 -O2 -I src tests/harvester/fragment_index.cpp -o /tmp/fox-webm-index
python3 -B tests/progressive_webm_remux_preflight.py \
  --worker _bin/vp9-superframe-aac-linux-stage/foxicmp-remux-stream-worker \
  --index-runner /tmp/fox-webm-index \
  --report-directory _bin/webm-vp9-preflight-local
```

Windows-проверки компилируют тот же CLI MinGW и MSVC; используют Qt/Classic
workers из отдельных stage. Скрипт сохраняет исходные WebM, prefix и fMP4
references, packet clock tables и framemd5. Для внешних команд сохраняются
command/stdout/stderr и exit; управляемые отказы worker записаны как structured
result/exits и read/output accounting. Эти media artifacts остаются в `_bin`, в Git — исходник
генератора и отчёт с SHA. [Container guidelines](https://www.webmproject.org/docs/container/)
и [Matroska timestamp notes](https://www.matroska.org/technical/notes.html)
задают container semantics; выбранные ограничения являются политикой этого
будущего режима, а не общим запретом формата.
