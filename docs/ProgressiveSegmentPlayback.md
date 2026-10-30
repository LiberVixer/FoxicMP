# Воспроизведение цепочки fMP4-сегментов — SDK 1.33

Linux Qt, Windows Qt и Classic принимают ограниченный профиль
`segmented-fmp4-h264-aac-lc` через существующие `probe`, `start`, `update`,
`complete`, `heartbeat` и reader/release. YouTubeHarvester не меняется.
Плеер читает локальные файлы, опубликованные загрузчиком; URL/HLS/DASH-манифесты
он не скачивает. Публичные установщики 0.2.0 этого расширения не содержат.

## Согласование

Нужна новая сборка нужной редакции. Проверять наличие
`capabilities.schema2Playback.segmentedPlayback.mode == "segmented-fmp4-h264-aac-lc"`.
`segmentProbeVersion=1` означает только прежнюю диагностику одного сегмента и
не доказывает поддержку воспроизведения. `V2PreviewSession` проверяет capability
перед стартом. Старые scalar-resource профили и диагностический API сохраняются.

## Допустимый профиль

- VOD, известная длительность, timebase 1/10000000, session/segment
  `timestampOffset=0`. Ровно две required-дорожки: H.264 video и AAC-LC audio
  в отдельных fMP4-ресурсах. Фактические codec headers проверяются.
- Отдельный неизменный init для каждой дорожки, не больше 1 МиБ, ровно один
  `ftyp` и `moov` (допускаются `free`). Init полностью подтверждён,
  resource `state=complete`, `initializationReady=true`.
- `track.source.segments` — append-only список. Каждый media resource уникален,
  не больше 16 МиБ. Для публикации байтов декодеру нужны известная полная длина,
  полное покрытие ranges и resource `state=complete`; корневой writer и дорожки
  при этом остаются `downloading` до окончания всего ролика.
- `sequence` последовательны, начало может быть любым положительным числом.
  `configurationId` постоянен, `discontinuity=false`, init не меняется.
  Presentation timeline непрерывен (допуск округления 10 тиков = 1 мкс).
  Первое видео начинается в пределах 0…1 с для frame reordering; AAC может
  иметь preroll до 1 с перед нулём и начинается не позже нуля + 1 мкс.
- `start/duration` описывают минимум PTS и максимум PTS+sample duration
  индексированных пакетов, с допуском 1 мкс. Последний укороченный AAC sample
  берётся из `trun`, а не заменяется полной длительностью кадра.
- Media содержит только `moof`, `mdat`, `styp`, `sidx`, `tfhd` с
  `default-base-is-moof` без absolute base offsets. Не больше 65536 samples
  на сегмент. Новые codec configurations требуют другого профиля и отклоняются.
- Действуют общие пределы schema 2: 64 ресурса, 128 сегментов на дорожку,
  JSON 65536 байт. Это ограниченный профиль, а не поддержка произвольной длины HLS.

## Чтение и завершение

Для каждой дорожки строится виртуальная цепочка `init + segment 1 + segment 2…`.
Объединённый файл на диск не записывается. Пропуск или недокачанный сегмент
останавливает доступный префикс: последующие готовые сегменты скрыты до
заполнения пропуска. При добавлении сегментов прежние byte offsets сохраняются.

Сегменты индексируются через общий `ResourceSource` до передачи декодеру;
SHA-256 проверяется при наличии integrity. Закреплённые файловые идентичности,
обнаружение усечения и reader receipts применяются ко всем открытым ресурсам.
Ошибка required-сегмента останавливает сеанс. Смена имени не переключает
закреплённый читатель на другой файл. Файлы освобождаются после последнего
внутреннего владельца; загрузчик должен дождаться `filesClosed/released`.

Старт требует прежнего общего буфера 10 с. Нехватка данных означает buffering,
а не EOF. Пауза сохраняется при update; перемотка доступна внутри подтверждённой
области. Qt использует seekable AVIO с `fflags=+ignidx` и чтением по потребности,
чтобы открытие или перемотка не зависали на недокачанном хвосте. Classic
использует существующий fragmented MP4 splitter. После полной публикации всех
ресурсов и terminal complete дорожек/writer разрешается естественный EOF.

Не включены TS, WebM-сегменты, muxed A/V, live/sliding window, удаление старых
сегментов, timestamp rebasing, смена init/codec, несколько audio и subtitles
в сегментном профиле. Возможности прежних scalar-resource профилей сохраняются.

Проверки и точные exit codes: [отчёт](ProgressiveSegmentPlaybackValidation-2026-10-30.md).

SDK 1.34 отдельно расширяет этот профиль [HEVC Main/Main10 с AAC-LC](ProgressiveSegmentHevc.md); остальные границы сохраняются.

SDK 1.35 отдельно добавляет [сегментный AV1 Main 8/10-bit с AAC-LC](ProgressiveSegmentAv1.md).

SDK 1.36 отдельно добавляет [VP9 8/10-bit и пары H.264/HEVC/AV1/VP9 с Opus mono/stereo](ProgressiveSegmentVp9Opus.md).
