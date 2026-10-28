# Подтверждённая байтовая перемотка Qt — SDK 1.23

Этап относится к Linux Qt и Windows Qt, к schema-2 источнику с несколькими
аудиодорожками. Classic сохраняет существующий native индекс и в этом этапе
не изменяется. YouTubeHarvester не изменяется.

## Поведение

Предел Qt в 10 секунд из SDK 1.22 снят. Для перехода нужна подтверждённая
доступность всех required A/V дорожек: target не выходит за availableSeconds
с обычным защитным отступом 0,2 секунды; у готового источника — за duration.
Незагруженная область отклоняется до вызова декодера, текущий просмотр сохраняется.
Вблизи хвоста декодер может ждать дополнительный preroll/буфер. Во время перехода
буферизация считается от цели seek, а не от старого кадра после переоткрытия;
из такого ожидания можно уйти другим seek. Qt status добавляет `seeking` и
`seekTargetSeconds` (последнее только во время перехода). `positionSeconds`
сохраняет фактическую позицию декодера, а не подменяется запрошенной целью.

Qt разрешает stream callbacks выполнять byte seek только внутри текущего
подтверждённого префикса. Read по-прежнему не выдаёт неопубликованные байты;
незавершённый хвост ожидает update/cancel и не превращается в EOF. Неизвестная
длина остаётся неизвестной, известная ожидаемая длина не разрешает чтение хвоста.

Для multi-audio loadfile получает `demuxer-lavf-o=fflags=+ignidx`. Это позволяет
MOV demuxer завершить открытие после первого media-фрагмента, сохранив seekable
AVIO; предварительное сканирование растущего файла до конца больше не требуется.
Собственные проверки ResourceSource/FragmentIndex остаются включены. Основание:
[FFmpeg MOV parser](https://github.com/FFmpeg/FFmpeg/blob/n6.1/libavformat/mov.c),
условие AVFMT_FLAG_IGNIDX после found_moov/found_mdat.

Для этого режима `cache=no`, `demuxer-readahead-secs=0` и
`demuxer-seekable-cache=no` отключают спекулятивное чтение до растущего хвоста.
Декодер читает по потребности воспроизведения, а запас данных и пауза по-прежнему
определяются подтверждённой временной шкалой Source. Это не меняет общие
настройки кэша пользователя и не подменяет ожидание хвоста EOF/ошибкой.
Основание: [параметры демультиплексора mpv](https://mpv.io/manual/stable/#options-demuxer-readahead-secs).

При каждом multi-audio seek Qt переоткрывает только потоки декодера на том же
закреплённом Source. Это освобождает demuxer, уже ожидающий новые байты на хвосте.
Новые потоки открываются с позиции 0 и с выбранной аудиодорожкой; после loaded
и фактического появления SRT в track-list выполняется обычный байтовый mpv seek
к цели. Ненулевой loadfile start до выбора потоков не используется.
Готовые SRT подключаются последовательно, чтобы номера дорожек оставались
стабильными. Повторные seek во время открытия объединяются в последнюю цель;
новый выбор audio/subtitle обновляет ожидающий запрос. Session/generation/readerId,
выбор audio/subtitle, скорость и пользовательская пауза сохраняются. Native seek может разбирать предыдущие подтверждённые фрагменты:
скорость перехода на больших файлах зависит от backend и носителя.

## Capabilities и совместимость

`qtMultiAudioSeek.mode` — `confirmed-byte-seek`, `confirmedRangesOnly:true`.
Существующие version-1 команды, schema-2 снимки и Python V2PreviewSession сохраняются.
Числовое `maxTargetSecondsWhileIncomplete` сохранено для прежних клиентов; теперь
оно равно 922337203686 — округлённому вверх пределу signed int64 временной шкалы
в единицах 100 ns. Это верхняя граница формата, а фактическая доступность определяется
availableSeconds/duration текущего сеанса. `completeSourceByteSeek:true` сохраняется.

Legacy/provider и двухдорожечные сеансы сохраняют прежний способ открытия и
ограничения packet cache; этот этап не объявляет их произвольную новую перемотку.

## Проверка

[Результаты Linux Qt и Windows Qt](ProgressiveSeekValidation-2026-10-28.md).
После генерации `qt/tests/generate_media.py MEDIA_DIR` отдельный CTest-сценарий
называется `progressive-confirmed-seek`. Linux: `qt/tests/run_tests.sh BUILD_DIR
MEDIA_DIR -R progressive-confirmed-seek -V`; Windows Qt: отдельный профиль,
`QT_QPA_PLATFORM=offscreen`, `FOXICMP_TEST_MEDIA=MEDIA_DIR`, затем
`ctest --test-dir BUILD_DIR -R progressive-confirmed-seek -V`.
Стенд использует локальный 90-секундный VOD с физически отсутствующим хвостом,
выбор вторичного звука/SRT, forward/backward/paused seek и переход из буферизации на хвосте, запрет незагруженной
области, append/rename, завершение и reader release. Это не проверка физической
задержки аудиоустройства, длинного дрейфа, 4K/HDR или реальной интеграции Harvester.
