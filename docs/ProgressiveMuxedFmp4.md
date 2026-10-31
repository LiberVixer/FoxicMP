# Один растущий fMP4 с видео и звуком

SDK 1.42: профиль `growing-muxed-fmp4-h264-aac-lc` для Classic и обеих
Qt-редакций. Он дополняет отдельные fMP4/WebM дорожки и TS-профили без
изменения `start/update/complete`, reader receipts или старых manifest.
YouTubeHarvester не изменяется.

## Согласование

Проверять `schema2Playback.muxedPlayback.fmp4.mode`. Прежние флаги
`muxed: true`, поддержка отдельных fMP4 или совместного TS этого профиля
не подразумевают. Python `V2PreviewSession` проверяет capability и shared
scalar форму до изменения last/reader/uncertain state.

Manifest schema 2 содержит ровно два required track (`video/h264` и
`audio/aac-lc`) с `containerHint: "fmp4-muxed"` и одинаковым
`source: {"resourceId": "mux"}`. Ровно один resource с обычным локальным
именем и SHA-256 всех подтверждённых байтов; ranges образуют непрерывный
префикс от 0. Initialization находится в том же файле: без external init,
`source.segments`, `segmentCatalog`, optional tracks и subtitles.

Нужны известная положительная duration, `live: false`, timebase `1/10000000`,
`timestampOffset: "0"`; timing/edit list внутри MP4 сохраняются.
Readiness и state обеих логических дорожек одинаковы. Длина resource может
быть `null` до complete; однажды объявленная длина не меняется.

Порядок writer: записать и flush bytes, расширить ranges, вызвать
`protect_snapshot(..., previous=...)`, увеличить revision, отправить update.
Старые bytes, digest/границы блоков, имя/file ID и clock неизменны.
Для complete объявить конечную длину, полное покрытие ranges и complete
у writer, resource и обеих дорожек. Пример —
`progressive-v2-muxed-fmp4-vod.json` в SDK; его IDs/duration заменить
значениями конкретного VOD.

## Поддержанный MP4

- Один head `ftyp/moov`, ровно один video `trak` с AVC `avc1` и один audio
  `trak` с AAC-LC `mp4a`, независимые ненулевые track ID и два `trex`.
- Пустые nonfragmented sample tables. Разрешён только self-contained
  `dref/url` и sample data-reference index 1: ссылки на внешние файлы запрещены.
- Цепочка `moof/mdat`; каждый moof содержит ровно два traf, по одному на
  объявленную дорожку. В tfhd требуется default-base-is-moof без explicit
  base; в первом trun каждой дорожки нужен явный signed data_offset.
- Последовательный mfhd без rollover, неизменные sample description и
  конфигурация, непрерывный decode clock. В начале видео нужен keyframe.
- Все samples лежат в соответствующем mdat; их общий набор покрывает
  payload без пересечений, пропусков и неизвестной третьей дорожки.
- B-кадры, signed composition offset, разные media timebase и штатные edit
  lists сохраняются. Возможен небольшой audio preroll.
- Bounded `free` допускается; конечный `mfra` игнорируется в виртуальных
  представлениях. Прочие структуры, включая arbitrary CMAF/HLS metadata,
  требуют отдельной проверки/расширения, а не неявного fallback.

До полного head moov и первого полного A/V fragment возвращается
`waiting_initialization`. Неполный atom header, moof или mdat при downloading
не публикует новые samples. Даже все записанные bytes с downloading не дают
EOF; нужен явный complete. Окончательно неполный fragment/trailer — ошибка.

## Виртуальные представления и защита

`HarvesterMP4Muxed.h` строит два immutable layout. В каждом moov оставляет
свою trak/trex, в moof — свой traf и пересчитанные trun offsets. Оригинальный
mdat представлен прямым диапазоном общего physical resource, целиком, без
его перекодирования или записи отдельной копии. Неописанные в trun samples
другой дорожки не выбираются декодером. Это может давать лишние reads при
prefetch; оптимизация I/O и многочасовая стоимость отдельно не измерены.

`progressivemuxmp4.h` подключает layout к ResourceSource. Обе logical views
имеют один physical file/lease; decoder получает отдельные виртуальные имена
`video.fmp4` и `audio.fmp4`, не создаваемые на диске. Classic использует
MP4Splitter, Qt — libmpv. Статус: `mediaMode` нового профиля, `muxed: true`,
`segments: false`, `physicalResources: 1`, per-track decoder I/O counters.

Candidate layout/index публикуется после проверки обеих traf и всего
mdat. Ошибка нового audio/video fragment или источника закрывает обе
проекции; прежняя revision остаётся для диагностики. Ошибка metadata до
admission сохраняет старое чтение. Cached verified headers не заменяют
проверки SHA реальных payload reads. Отмена проверяется между Source reads;
release подтверждается после закрытия последнего владельца.

В Windows pin может запрещать OS replacement открытого resource. В этом
случае проверяется исходное чтение до release и разрешённая замена после.
В Linux уже выполненная замена обнаруживается по file identity и закрывает
обе проекции. Удаление данных до reader release в обоих случаях запрещено
контрактом загрузчика.

Пределы admission: read chunk 64 KiB; init и moof по 1 MiB; mdat и добавка
подтверждённого префикса на start/update по 16 MiB; 8192 fragments,
32768 top-level atoms, 2 миллиона index samples на дорожку. Это ограничения
парсера, не результат стресс-теста таких размеров. Metadata/index snapshots
копируются при update; медленный синхронный disk read нельзя прервать внутри
самого системного вызова.

## Проверки и границы

`progressive-mux-mp4-native` сравнивает реальные packet hashes, PTS/DTS,
decoded video/PCM, partial moof/mdat, immutable prefix, backward reads, SHA,
rewrite/truncate/replacement, cancellation и last-owner release.
`progressive-mux-mp4-playback` проверяет Qt; Classic использует отдельную
безопасную копию и `--mux-mp4-playback-only`.
[Фактические результаты](ProgressiveMuxedFmp4Validation-2026-10-31.md).

Этот этап не включает HEVC/AV1/VP9/Opus в muxed MP4, muxed WebM, post-download
обычного MP4 с moov в хвосте, multi-audio, live/config changes или приёмку
реальных сайтов и многочасовой физической A/V sync. Соответствующие прежние
**раздельные** профили продолжают работать в своих границах.

SDK 1.47 расширяет этот shared layout [профилем HEVC Main/Main10 + AAC-LC](ProgressiveMuxedFmp4Hevc.md)
с отдельной capability. Описание AVC mode выше сохраняет исходные границы SDK 1.42.

SDK 1.48 добавляет [AV1 Main 8/10-bit + AAC-LC](ProgressiveMuxedFmp4Av1.md)
с отдельной capability, сохраняя AVC/HEVC profiles.

SDK 1.49 добавляет [VP9 Profile 0/2 8/10-bit + AAC-LC](ProgressiveMuxedFmp4Vp9.md)
с отдельной capability, сохраняя AVC/HEVC/AV1 profiles.

SDK 1.50 добавляет [Opus mono/stereo с четырьмя видеокодеками](ProgressiveMuxedFmp4Opus.md)
с отдельным согласованием каждой пары и сохранением AAC-LC defaults.
