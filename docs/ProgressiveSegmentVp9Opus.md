# Сегментные VP9 и Opus в fMP4 — SDK 1.36

Linux Qt, Windows Qt и Classic поддерживают VP9 Profile 0 (8-bit) и Profile 2
(10-bit), 4:2:0, с AAC-LC; также H.264, HEVC, AV1 и VP9 с Opus mono/stereo.
Это расширение [сегментного VOD reader](ProgressiveSegmentPlayback.md), а не
загрузка HLS/DASH по URL. Harvester не меняется. Старые установщики 0.2.0
новых capabilities не содержат.

## Профили и согласование

В `capabilities.schema2Playback.segmentedPlayback.profiles` добавлены:

- `segmented-fmp4-vp9-aac-lc`;
- `segmented-fmp4-h264-opus`;
- `segmented-fmp4-hevc-opus`;
- `segmented-fmp4-av1-opus`;
- `segmented-fmp4-vp9-opus`.

Сохраняются прежние H.264/HEVC/AV1 с AAC. `mode` остаётся
`segmented-fmp4-h264-aac-lc` для совместимости. Профиль выбирается по фактической
паре кодеков; наличие VP9/AAC не обещает VP9/Opus. `V2PreviewSession` проверяет
явные codecHint и capability пары до start. Пустые hints допускают определение
из init, но окончательное решение всегда принимает media bridge.

## Реальные заголовки и временная шкала

VP9 проверяет `vp09`/`vpcC`, version/flags, profile/level/bit-depth, chroma и
color metadata. Profile 1/3, 12-bit и неподдерживаемая chroma не включены.
Противоречивые profile/depth и неизвестные конфигурации отклоняются.

Opus проверяет `Opus`/`dOps`, version, 1/2 channels, mapping family 0,
48 kHz sample/media clock и соответствие `pre-skip` единственному unit-rate
media edit. Input rate в dOps остаётся информационным параметром, как в
прежнем scalar-профиле. Multi-channel/mapping family 1 этим этапом не включены.

Начальный отрицательный audio PTS от pre-skip сохраняется; временная шкала
не сдвигается второй раз. Длительности берутся из trun, метаданные сегментов
сверяются с индексированными пакетами. При перемотке существующий backend
учитывает 80 ms Opus preroll. AAC сохраняет прежние правила.

HEVC/AV1/VP9/Opus разрешаются внутреннему segment index только после проверки
реального init общим bridge. Публичный `resourceProbe.segment` сохраняет
прежний диагностический H.264/AAC-профиль.

## Поведение и границы

Постоянная конфигурация, две required-дорожки, отдельные полностью подтверждённые
init/media, append-only цепочка, последовательные sequence, нулевые offsets и
VOD с известной длительностью. Пропуск скрывает готовый хвост. Пауза сохраняется
при update, перемотка доступна внутри подтверждённого участка, недокачанный хвост
означает buffering. Complete разрешает естественный EOF; удаление ресурсов
допустимо после filesClosed/release всех читателей.

Объединённый файл не создаётся. Используются прежние ResourceSource, SHA-256
при объявленных digest и закреплённые идентичности файлов. Пределы сохраняются:
64 ресурса, 1 МиБ init, 16 МиБ media, 65536 samples на сегмент, JSON 65536 байт.

Не включены WebM-сегменты, TS, muxed A/V, live/sliding window, смена init/codec,
timestamp rebasing и снятие ограничения длинных списков ресурсов. Проверенное
10-bit декодирование не подтверждает HDR/GPU или физическую A/V задержку.

[Проверки и матрица сочетаний](ProgressiveSegmentVp9OpusValidation-2026-10-30.md).
