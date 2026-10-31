# Один растущий WebM с VP9 и Opus

SDK 1.43 добавляет профиль `growing-muxed-webm-vp9-opus` для Classic и обеих
Qt-сборок. Один physical resource содержит видео и звук; decoder получает
два single-track virtual WebM view без перекодирования или промежуточных
медиафайлов. Harvester не изменяется.

## Согласование и manifest

Проверять `schema2Playback.muxedPlayback.webm.mode`. Поддержка отдельных
WebM, совместного MP4/TS или старый `muxed: true` не заменяют эту capability.
`V2PreviewSession` проверяет новый профиль до изменения собственного состояния.

Нужны ровно два required scalar track: `video/vp9` и `audio/opus`, оба с
`containerHint: "webm-muxed"`, одинаковыми state/readiness и
`source: {"resourceId": "mux"}`. Resource ровно один, с обычным локальным
именем и обязательным `integrity.algorithm: sha256`; подтверждённые ranges
составляют непрерывный префикс от 0. Без external initialization,
`source.segments`, catalog, optional audio и subtitles.

VOD с известной положительной duration, `live: false`, timebase `1/10000000`
и `timestampOffset: "0"`. Длина resource может быть `null` до завершения.
При дозаписи сохраняются file ID, имя, старые bytes/SHA/границы блоков и
идентичность дорожек. Порядок writer: write/flush → `protect_snapshot` с
предыдущим snapshot → revision++ → update. Complete требует конечной длины,
полного покрытия и complete у writer, resource и обеих дорожек.

Пример начального manifest — `progressive-v2-muxed-webm-vod.json` в SDK.
Заменить IDs/duration значениями своего ролика, затем публиковать реальные
bytes/digest и readiness. Этот ZIP сам по себе не обновляет старый EXE.

## Контейнер и время

Основой разметки служат [WebM Container Guidelines](https://www.webmproject.org/docs/container/)
и [RFC 9559](https://www.rfc-editor.org/rfc/rfc9559.html). Конкретный профиль
плеера ограничен по сравнению с полным набором Matroska/WebM:

- DocType `webm`, один Segment известного либо неизвестного размера;
  его опубликованный header не переписывается при complete.
- Info до Tracks/Cluster; `TimestampScale` ровно 1 ms. Ровно два TrackEntry
  с разными ненулевыми TrackNumber/TrackUID, без linked segments, внешних
  codec references, encryption/compression, alpha и interlaced video.
- VP9 Profile 0/8-bit либо 2/10-bit, 4:2:0; bounded video DefaultDuration.
  Дробная частота допускается: nanosecond DefaultDuration сохраняется,
  sample end отдельно округляется вниз до 100 ns, без накопления drift.
- Opus mono/stereo 48 kHz, mapping family 0, согласованные CodecDelay и
  OpusHead pre-skip, SeekPreRoll 80 ms. Небольшой audio preroll допустим.
- Только конечные bounded Clusters, SimpleBlock/BlockGroup без lacing;
  первое видео должно быть keyframe. Необъявленные track IDs и regression
  timestamps отвергаются. DiscardPadding в этом профиле неотрицательный.
- Packet duration Opus берётся из TOC; padding вычитается один раз.
  BlockDuration может описывать полный packet либо audible remainder,
  округлённый до Track Ticks. Другие значения отвергаются. Это сохраняет
  чтение файлов, которые разные версии muxer записывают по-разному.

Opus TOC/padding и nanosecond DefaultDuration обрабатываются тем же
`WebMIndex` в старых отдельных/segment WebM профилях. Изменение расширяет
приём дробных DefaultDuration и корректных rounded Opus duration hints;
старые сценарии проверяются повторно.

## Проекция и ожидание

`HarvesterWebMMuxed.h` выбирает свою TrackEntry и blocks для каждого view.
Info/codec initialization и block timestamps сохраняются. Segment получает
неизвестный virtual размер; Cluster header и timestamp перестраиваются,
а исходные SimpleBlock/BlockGroup с payload, references и padding читаются
прямо из подтверждённых диапазонов общего файла.

Original SeekHead/Cues/Tags и Cluster CRC/Position/PrevSize не переносятся:
их исходные byte positions/покрытие перестали бы соответствовать view.
SDK metadata/title/chapters применяются отдельно; перемотка использует
индекс плеера и поддержанную работу backend с virtual stream.

Cluster проверяется для обеих проекций до публикации candidate index.
Кластер только одной дорожки допустим, например audio tail. До полного
head и первых packets обеих дорожек возвращается `waiting_initialization`.
Неполные EBML header/body/Cluster ждут update, а explicit complete с
незаконченным элементом становится ошибкой. Все записанные bytes с
downloading не означают EOF.

`progressivemuxscalar.h` общий для shared fMP4 и WebM: один physical
ResourceSource/lease, SHA, atomic publication и last-owner release.
`progressivemuxwebm.h` задаёт профиль. Ошибка новых данных/источника закрывает
обе проекции; metadata rejection до admission оставляет старый сеанс
читаемым. Windows может запрещать replacement pinned file до release;
Linux обнаруживает выполненную подмену по file identity.

Пределы: read 64 KiB; init/metadata по 1 MiB; Cluster и прирост префикса на
start/update по 16 MiB; 8192 Clusters, 32768 top-level elements,
65536 children на Cluster, 2 миллиона index samples на дорожку. Модель
duration допускает до 1 ms погрешности envelope из-за container ticks.
Это admission limits, не стресс-тест таких размеров. Layout/index copies
при update и worst-case memory/throughput требуют отдельного измерения.

## Проверки и границы

`progressive-mux-webm-native` сравнивает encoded packet hashes, PTS/DTS и
decoded video/PCM, partial Clusters, оба Opus BlockDuration варианта, seek
bytes, SHA/rewrite/truncation/replacement, cancellation и shared lease.
`progressive-mux-webm-playback` проверяет обе Qt-сборки, Classic — отдельную
безопасную копию с `--mux-webm-playback-only`.
[Фактический отчёт](ProgressiveMuxedWebmValidation-2026-10-31.md).

VP8/AV1/Vorbis в совместном файле, multi-audio, unknown-size Clusters,
arbitrary Matroska fields, codec changes/live, реальные сайты и длительная
физическая A/V-синхронизация остаются отдельными этапами. Их действующие
раздельные профили сохраняют свою поддержку и ограничения.

SDK 1.44 добавляет [отдельно согласуемый AV1/Opus](ProgressiveMuxedWebmAv1.md)
в том же общем ресурсе. Прежний VP9 mode и его ограничения сохраняются.

SDK 1.45 добавляет [отдельно согласуемый VP8/Opus](ProgressiveMuxedWebmVp8.md).
VP9 и AV1 сохраняют собственные mode.
