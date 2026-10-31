# Один растущий MPEG-TS файл

Профиль `growing-muxed-mpegts-h264-aac-lc` расширяет schema 2 без изменения
команд IPC или формата старых сеансов. Он предназначен для **локального VOD
файла**, который загрузчик дописывает в конец. YouTubeHarvester этим изменением
не модифицируется; сетевой протокол и получение потока остаются его задачей.

## Согласование и manifest

До `start` проверить
`schema2Playback.segmentedPlayback.mpegts.growing.mode` в `capabilities`.
Поддержка `mpegts`, `mpegts-muxed`, старого `segmentedPlayback` или успешный
diagnostic probe другого профиля не заменяют эту проверку. Python
`V2PreviewSession` выполняет её до изменения собственного состояния.

Manifest содержит:

- ровно один resource с обычным локальным именем, например `mux.part`;
- ровно два required logical track: `video` / `h264` и `audio` / `aac-lc`;
- у обоих `containerHint: "mpegts-growing"`, одинаковый
  `source: {"resourceId": "mux"}`, readiness и writer state;
- без `initializationResourceId`, `source.segments` и `segmentCatalog`;
- `integrity.algorithm: "sha256"` и digest всех подтверждённых байтов;
- только непрерывный подтверждённый префикс с offset 0, без дыр;
- известную положительную duration, timebase `1/10000000`, `live: false`;
- общий `timestampOffset`, точно соответствующий переводу исходной 90 kHz
  временной метки в 100 ns ticks. Он не меняется в пределах сеанса.

Длина resource может быть неизвестна (`null`) до завершения. Если длина уже
опубликована, менять её нельзя. Пример основных полей дорожек:

```json
[
  {"id":"video","kind":"video","required":true,"codecHint":"h264",
   "containerHint":"mpegts-growing","state":"downloading",
   "initializationReady":true,"source":{"resourceId":"mux"}},
  {"id":"audio","kind":"audio","required":true,"codecHint":"aac-lc",
   "containerHint":"mpegts-growing","state":"downloading",
   "initializationReady":true,"source":{"resourceId":"mux"}}
]
```

Сначала записать и flush новые байты, затем обновить ranges и SHA-блоки,
увеличить revision и отправить `update`. `protect_snapshot(..., previous=...)`
сохраняет границы и digest уже опубликованных блоков. Старые байты, SHA,
имя файла, его inode/file ID, источник дорожек и clock остаются неизменными.
При `complete` установить конечную длину, полное покрытие ranges и состояния
resource, обеих дорожек и writer в `complete`.

## Transport, кодеки и ожидание

Поддерживается один program: AVC PID + ADTS AAC PID, PCR на AVC PID.
PAT/PMT находятся в самом растущем файле, повторяются без изменения. Обычный
SDT PID 17 допускается: PSI envelope/CRC проверяются, содержимое не поступает
декодеру. Необъявленные ES, scrambled packets, ошибки continuity/CRC,
discontinuity, смена PAT/PMT и rollover часов отвергаются.

AVC: progressive 8-bit 4:2:0 Baseline/Main/High, постоянная частота с явным VUI
timing, AUD, одна access unit на PES, закрытые GOP, SPS/PPS перед IDR.
AAC-LC: MPEG-4 ADTS без CRC и multiple raw blocks, mono/stereo, 32/44.1/48 kHz.
Конфигурация кодеков не меняется. B-кадры и небольшой audio preroll допустимы.
Другие TS-потоки требуют отдельного расширения профиля.

Последний неполный 188-байтовый пакет и незавершённый PES сохраняются в
ограниченном состоянии парсера между revision. AAC публикуется целыми PES;
видео — после закрытия GOP следующим IDR. Это позволяет проверить порядок
PTS/DTS B-кадров до передачи декодеру. Поэтому доступный timeline может
отставать от физически записанного хвоста примерно на один GOP.

Даже полностью записанный файл с `downloading` не означает EOF.
Неоконченный хвост ждёт `update`; окончательный неполный packet/PES/codec frame
при явном `complete` становится ошибкой. До первого полного AVC GOP и AAC PES
возвращается `waiting_initialization`; `V2PreviewSession` позволяет повторить
публикацию без неопределённого ownership.

## Общая реализация трёх редакций

`HarvesterTSGrowing.h` хранит incremental transport state.
`progressivetsgrowing.h` читает подтверждённые SHA ranges через ResourceSource
и создаёт два индекса поверх **одного** физического файла/lease.
`TsPlaybackInput` сохраняет прежний segmented TS интерфейс.

`TsMp4Bridge` предоставляет два виртуальных fMP4 view без перекодирования и
создания промежуточных медиафайлов. Classic передаёт их MP4Splitter;
обе Qt-сборки — libmpv. Раздельные виртуальные имена нужны декодерам для
различения логических video/audio view и не являются файлами на диске.
Статус сообщает `mediaMode: "growing-muxed-mpegts-h264-aac-lc"`, `muxed: true`,
`segments: false`, `decoderContainer: "fmp4"`.

Один read не превышает 64 KiB; новый префикс на start/update — 16 MiB,
PES — 4 MiB, staged GOP — 16 MiB / 65536 access units, PAT/PMT init — 1 MiB.
Индекс ограничен 2 миллионами samples/ranges. Отмена проверяется между reads.
Ошибка нового audio/video parser или источника закрывает обе проекции;
частично построенный индекс не публикуется. Ошибка metadata до admission
сохраняет предыдущую рабочую revision. Release подтверждается после
закрытия последнего владельца общего physical resource.

## Проверка и границы

Нативный сценарий: `progressive-growing-ts-native` /
`tests/progressive_growing_ts_conformance.py`. Он проверяет произвольные
byte cuts, включая PSI/PES/GOP, hashes каждого access unit, конечный MP4 index,
PTS/DTS, backward reads, decoded video/PCM, unknown length, preroll,
SHA rewrite/truncation, ошибки AAC/continuity/SDT CRC, cancellation и leases.

Сквозной сценарий: `progressive-growing-ts-playback` для обеих Qt-сборок;
Classic: `tests/windows/progressive_family_test.py --growing-ts-playback-only`.
Подробные фактические результаты относятся к отдельному отчёту проверки.

Это не общий HLS/live demuxer. Muxed fMP4/WebM, несколько программ или
аудиодорожек в TS, MPEG-2/HEVC в TS, смена конфигурации и live остаются за
границами профиля. Синтетические TS fixtures не заменяют интеграционные
проверки реального загрузчика и длительные измерения физической A/V-синхронизации.
