# Один растущий WebM с Vorbis

SDK 1.46 добавляет VP8/Vorbis, VP9/Vorbis и AV1/Vorbis в Classic, Windows Qt
и Linux Qt. Используется [shared WebM](ProgressiveMuxedWebm.md): один
SHA-256 resource, две virtual track views, исходные blocks без перекодирования
и промежуточных media. Прежние Opus modes и defaults сохраняются.

## Согласование

Нужны обе части capability:
`schema2Playback.muxedPlayback.webm.vorbis.mode = growing-muxed-webm-vorbis`
и точная пара в `vorbis.profiles`: `growing-muxed-webm-vp8-vorbis`,
`growing-muxed-webm-vp9-vorbis` либо `growing-muxed-webm-av1-vorbis`.
Separate Vorbis, Opus modes и общая muxed-галочка этого не разрешают.
SDK проверяет пару до start/update и изменения last/uncertain.

Manifest: `containerHint: webm-muxed`, два required scalar track одного
ресурса; явные video `codecHint: vp8|vp9|av1` и audio `codecHint: vorbis`.
Без audio hint остаётся прежний Opus; без video hint — VP9 для прежнего
Opus-профиля. Actual codec обязан совпасть с hint. Пример в SDK —
`progressive-v2-muxed-webm-vorbis-vod.json`; заменить IDs/duration и
публиковать реальные bytes/SHA/readiness, начальные пустые ranges не готовы.

Остальные условия прежние: VOD с известной duration, timebase 1/10000000,
нулевой timestampOffset, непрерывный подтверждённый префикс от 0, одинаковые
state/readiness, finite Clusters, без external init, catalog и live.
Whole-file length может быть неизвестна до complete. Опубликованный header
не переписывается при завершении.

## Vorbis и время

Codec ID — `A_VORBIS`, mono/stereo 48 kHz. CodecPrivate содержит три Vorbis I
header в Xiph framing: identification, comment и setup. Применяются текущие
ограничения VorbisConfiguration: до 1 MiB, bounded codebooks/references,
floor 1, проверенные mapping/modes/framing; parser не заменяет entropy decoder.
Основания: [Xiph Vorbis I](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) и
[Matroska A_VORBIS](https://www.matroska.org/technical/codec_specs.html#a_vorbis).

Длительность packet берётся из mode/window размеров с состоянием предыдущего
блока. Этот state сохраняется между Clusters и копируется вместе с candidate
index при update; переинициализация audio state на каждом Cluster не нужна.
Исходные timestamps сохраняются, sample end отдельно округляется до 100 ns.

CodecDelay допускается от нуля до половины наибольшего блока из header.
Он должен соответствовать целому числу samples с floor/ceil nanosecond
rounding. Это включает 128 samples = 2666666/2666667 ns, которые muxer может
записать для startup discard. Значение сохраняется в virtual header и
вычитается из sample timestamps. Неявное исправление bytes не выполняется.
Vorbis SeekPreRoll в metadata остаётся нулевым. Classic использует свой
существующий decoder preroll 200 ms для seek; Qt сохраняет свой demuxer seek.

BlockDuration — проверяемый hint: полный mode packet либо audible remainder,
округлённый до Track Ticks. Packet/window duration остаётся источником
длительности, DiscardPadding вычитается ровно один раз. Неправдоподобная
BlockDuration, padding больше packet и нецелый/чрезмерный CodecDelay
отвергаются. Это также применяется к старым separate Vorbis-профилям.

## Чтение, ошибки и границы

Info/selected TrackEntry и Cluster headers строятся в памяти; полный исходный
SimpleBlock/BlockGroup читается из общего файла. Неполный head/Cluster ждёт
update; explicit complete с оборванным элементом — ошибка. Ошибка новых
media/источника закрывает обе проекции. Metadata rejection до admission
сохраняет старый сеанс. Codec hints/configuration при update не меняются.

Пределы: read 64 KiB, init/metadata по 1 MiB, Cluster и прирост prefix по
16 MiB, 8192 Clusters, 32768 top-level elements, 65536 children на Cluster,
2 миллиона samples на дорожку. Unknown-size Segment разрешён,
unknown-size Cluster нет. Duration envelope допускает до 1 ms погрешности
container ticks. Layout/index snapshots копируются; предельные memory и
throughput требуют отдельного измерения.

[Фактические проверки](ProgressiveMuxedWebmVorbisValidation-2026-10-31.md)
охватывают три video codec, mono/stereo, дробную частоту, 10-bit VP9/AV1,
startup delay, обе BlockDuration conventions, padding, partial publication,
SHA/leases, pause/seek/complete/release и прежние профили. Тестовый звук
с импульсами упражняет смену Vorbis block modes. Для AV1 только генератор
fixtures использует промежуточный MP4, чтобы получить extradata до WebM header;
плеер media-файлы не создаёт.

Этот этап не добавляет multi-audio, произвольные sample rates/channels,
live/config changes, arbitrary Matroska или другие кодеки shared fMP4.
Реальные сетевые источники, многочасовые нагрузки и физическая A/V sync
остаются отдельной приёмкой. Harvester не меняется. SDK ZIP содержит клиент,
примеры и документацию; новый EXE надо собрать из соответствующего тега.
