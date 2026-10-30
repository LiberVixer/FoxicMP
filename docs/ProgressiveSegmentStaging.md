# Staging и индекс одного сегмента — SDK 1.32

Linux Qt, Windows Qt и Classic предоставляют асинхронную диагностику одной
секции schema 2 через прежний `resourceProbe`. Это первый путь чтения и demux
сегмента через `ResourceSource`; он не запускает декодер, не собирает общий
HLS/DASH stream и не заменяет уже открытое воспроизведение. Harvester не меняется.
В SDK 1.32 capability `schema2Playback.segments=false` сохранялся. SDK 1.33 отдельно добавляет [ограниченный playback](ProgressiveSegmentPlayback.md); этот диагностический запрос по-прежнему не запускает декодер.

## Запрос

Нужна новая сборка с `capabilities.segmentProbeVersion=1`. Старые запросы с
`reads` сохраняют прежний смысл. Новая форма требует пустой `reads` и selector:

```json
{"version":1,"action":"resourceProbe","sessionDirectory":"/local/job","session":{},"reads":[],"segment":{"trackId":"video","index":"0","demux":true},"waitMilliseconds":5000}
```

`session` в примере заменяется полным schema-2 снимком. `index` — каноническая
строка 0…127, индекс в append-only списке `track.source.segments`. `demux`
по умолчанию true. Для staging произвольных локальных байтов без индекса можно
передать false. `requestId`, idempotence, discovery, cancel, update, forget и
reader receipts остаются от `resourceProbe`; смешивать segment и raw reads нельзя.

```python
from progressive_resources import ResourceProbe

probe = ResourceProbe(client)
probe.start_segment(snapshot, directory, "video", 0,
                    demux=True, wait_milliseconds=5000)
reply = probe.wait()  # terminal state И фактически закрытые файлы
segment = reply["segment"]
probe.forget()        # подтверждение закрытых reader receipts
```

Успешное чтение: `state:finished`, `allReadable:true`, `segment.code:data`,
`bytesStaged`, `packetCount`, `sha256` объединённых init+media, IDs обоих ресурсов.
По IPC не возвращаются байты или список пакетов. `mode:resource-io-only` и
`playbackSupported:false` обязательны, в том числе при успешном demux.

## Правила чтения

- Init и media читаются только при известной полной длине и покрытии всех байтов.
  Unknown length/дыры дают `waiting-range`, ещё не объявленный сегмент —
  `waiting-segment`, отсутствие готового init — `waiting-initialization`.
  Тайм-аут ожидания не является EOF или готовностью к воспроизведению.
- Init берётся из текущего сегмента, последнего предыдущего явного init либо
  track-level `initializationResourceId`. Правила configuration/discontinuity
  проверяет общий parser. Один resource не может быть одновременно init и media.
- Init ограничен 1 MiB, media — 16 MiB. Превышение даёт `segment-too-large`,
  а не бесконечное ожидание. Пустой ресурс не считается готовым сегментом.
- Чтения до 64 KiB идут через pinned `ResourceSource` с прежними проверками
  identity, owner, truncation, last-read и, если объявлена, SHA-256 integrity.
  Ошибка/отмена не возвращают частично собранный результат.
- Stager владеет своим source; metadata и I/O обновляются одним способом.
  Ошибочный snapshot не меняет активный snapshot. Lease снимается после закрытия
  реальных дескрипторов, а не по таймеру или получению cancel.

## Границы demux

Индексируется один отдельный single-track fMP4 H.264/AVC либо AAC sample entry,
с init и целыми moof/mdat. Требуются 100 ns timebase и `default-base-is-moof`
без абсолютного base-data-offset. Максимум 65536 пакетов; bound проверяется до
разбора таблицы пакетов. Некорректные структуры, неподдержанный контейнер/кодек,
шифрование и незавершённые фрагменты не выдаются за успешный индекс.

Используется существующий `harvester::FragmentIndex` из исходников FoxicMP,
без нового FFmpeg-процесса, перекодирования или записи временного remux-файла.
Пакетные времена пока остаются временем исходного init/tfdt с edit list;
перенос на общую presentation timeline и переинициализация декодера между
configuration/discontinuity — следующие этапы. Проверка sample entry не является
проверкой всех codec parameters или подтверждением успешного декодирования.
TS, muxed tracks, WebM segments, live compaction и последовательный playback
этим этапом не включаются.

Проверки: `progressive-segment-staging` (native байты/пакеты, сверка с ffprobe),
`progressive-segment-api-native`, `progressive-segment-api` (Qt на обеих ОС),
`tests/windows/progressive_family_test.py --segment-only` (отдельный Classic).

Результаты трёх редакций и точные exit codes: [отчёт](ProgressiveSegmentStagingValidation-2026-10-30.md).
