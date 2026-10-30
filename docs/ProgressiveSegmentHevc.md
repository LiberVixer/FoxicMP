# HEVC Main/Main10 в сегментном fMP4 — SDK 1.34

Linux Qt, Windows Qt и Classic расширяют [сегментный VOD-профиль](ProgressiveSegmentPlayback.md):
отдельная HEVC video и AAC-LC audio, media mode `segmented-fmp4-hevc-aac-lc`.
Поддержаны Main 8-bit и Main10 10-bit, 4:2:0, sample entries `hvc1` и `hev1`.
Это расширение локального reader плеера; Harvester не меняется и сеть плееру
не передаётся. Выпущенные ранее установщики 0.2.0 этой capability не содержат.

## Согласование и проверка

Нужен профиль `segmented-fmp4-hevc-aac-lc` в
`capabilities.schema2Playback.segmentedPlayback.profiles`. Поле `mode` остаётся
`segmented-fmp4-h264-aac-lc` для совместимости с SDK 1.33; само это поле не
обещает HEVC. `V2PreviewSession` проверяет HEVC capability до отправки start
с явно указанным `codecHint:"hevc"`.

Плеер проверяет реальный `hvcC`: профиль, битовую глубину/4:2:0, reserved bits,
длину NAL, наборы параметров и их границы, self-contained media reference и
отсутствие шифрования. Для `hvc1` нужны полные массивы параметров; `hev1`
допускает неполные. Отсутствующий codecHint допускает определение из init;
ложный H.264 hint не превращает HEVC-байты в поддержанный H.264.

Общий bridge проверяет codec initialization до разрешения HEVC индексации
сегментов. Прежняя публичная диагностика `resourceProbe.segment` сохраняет
свой H.264/AAC-профиль: HEVC playback не объявляет расширение этого API.

## Поведение и границы

Сохраняются правила SDK 1.33: две required-дорожки, VOD с известной длительностью,
100 ns timebase и нулевыми offsets, отдельные полностью подтверждённые init/media,
постоянная codec configuration и append-only цепочка без discontinuity.
Пропуск скрывает последующие готовые сегменты; чтение идёт через закреплённый
ResourceSource, с проверкой объявленных SHA-256, без объединённого файла на диске.

Перемотка ограничена подтверждённым участком, пауза переживает update,
недокачанный хвост означает buffering, EOF разрешён после complete. ReaderId
сохраняется; ресурсы можно удалять только после подтверждённого filesClosed/release.
Общие пределы 64 ресурса, 1 МиБ init, 16 МиБ media и 65536 samples на сегмент
не увеличиваются.

Не включены AV1/VP9/Opus в сегментном профиле, TS, muxed A/V, live, смена init/codec,
timestamp rebasing и HDR-вывод. Возможности старых scalar-resource профилей
сохраняются. Проверка 10-bit декодирования не является подтверждением HDR или GPU.

[Проверки трёх редакций](ProgressiveSegmentHevcValidation-2026-10-30.md).
