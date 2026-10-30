# AV1 Main 8/10-bit в сегментном fMP4 — SDK 1.35

Linux Qt, Windows Qt и Classic принимают новый профиль
`segmented-fmp4-av1-aac-lc`: отдельные AV1 video и AAC-LC audio, VOD с
постоянной конфигурацией. Поддержаны AV1 Main 8/10-bit, 4:2:0 и sample entry `av01`.
Harvester не меняется. Плеер читает локальные ресурсы загрузчика, не скачивая URL.
Прежние установщики 0.2.0 этой capability не содержат.

## Согласование и заголовки

Проверять наличие `segmented-fmp4-av1-aac-lc` в
`capabilities.schema2Playback.segmentedPlayback.profiles`. Прежний `mode`
остаётся `segmented-fmp4-h264-aac-lc` для совместимости. Наличие HEVC или
общего AV1 scalar-профиля само по себе не означает сегментный AV1.
`V2PreviewSession` проверяет capability до start при `codecHint:"av1"`.
Отсутствующий hint позволяет определить кодек из init; ложные H.264/HEVC hints
отклоняются после чтения фактического заголовка.

Общий media bridge проверяет `av1C`: marker/version, профиль и level, битовую
глубину, chroma, reserved bits, параметры presentation delay и bounded OBU.
Sequence header обязателен в конфигурации и согласуется с её профилем/глубиной.
12-bit, High profile, monochrome и неподдерживаемая chroma не включены.
Проверяются self-contained media reference и отсутствие шифрования.

AV1 индексация сегментов разрешается только после проверки initialization
общим bridge. Публичная single-segment диагностика `resourceProbe.segment`
сохраняет H.264/AAC-профиль и не расширяется этим этапом.

## Публикация и просмотр

Действуют правила [SDK 1.33](ProgressiveSegmentPlayback.md): две required-дорожки,
100 ns timebase, нулевые offsets, полностью подтверждённые отдельные init/media,
append-only список, последовательные sequence, постоянные codec/init и
`discontinuity=false`. Ролик и writer остаются downloading, пока новые media
ресурсы публикуются complete по отдельности.

Виртуальная цепочка не записывается объединённым файлом. Пропуск скрывает готовый
хвост; контролируемое чтение соблюдает ranges, файловую идентичность и SHA-256
при объявленных digest. Пауза сохраняется при update, перемотка идёт внутри
подтверждённого интервала, недокачанный хвост означает buffering, а после complete
разрешён естественный EOF. ReaderId сохраняется; закрытие файлов подтверждается
filesClosed/release. Прежние ограничения: 64 ресурса, 1 МиБ init, 16 МиБ media,
65536 samples на сегмент, JSON 65536 байт.

Не включены VP9/Opus в сегментном профиле, TS, WebM-сегменты, muxed A/V,
live/sliding window, смена init/codec и timestamp rebasing. Прежние H.264/HEVC
сегментные и scalar-resource профили сохраняются. Проверка 10-bit не подтверждает
HDR, GPU или физическую задержку аудио.

[Проверки всех трёх редакций](ProgressiveSegmentAv1Validation-2026-10-30.md).

SDK 1.36 отдельно добавляет [VP9 8/10-bit и пары H.264/HEVC/AV1/VP9 с Opus mono/stereo](ProgressiveSegmentVp9Opus.md).
