# Подготовка интеграции Harvester: этапы и границы готовности

Основание — [ТЗ от 25 октября 2026](ProgressiveTechnicalSpec-2026-10-25.ru.md).
Документ ТЗ сохранён без изменения требований. Работы этого репозитория относятся
только к FoxicMP; исходники, кнопки и настройки YouTubeHarvester не изменяются.
Названия F-01…F-04 ниже относятся к пунктам аудита в ТЗ, а не к семейным этапам F1…F7.

## Порядок реализации

| Очередь | Результат | Статус |
|---|---|---|
| 1 / F-03 | Каталог привязан к успешному start; update/complete наследуют его, другой каталог отклоняется, новый сеанс ничего не наследует. Общий regression для трёх редакций | Выполнено: Linux Qt, Windows Qt и Classic прошли общий indexed/fMP4 сценарий, exit 0; [отчёт](ProgressiveV1Validation-2026-10-25.md) |
| 2 / F-02, F-04 | Стабильный SDK, writer-стенд, согласованные запуск/discovery, работающий legacy/provider адаптер Windows Qt и миграция | Реализованы SDK v1, offline writer, release acknowledgement и native Windows Qt legacy/provider. [Проверка](ProgressiveSdkValidation-2026-10-25.md): Linux Qt 15, Windows Qt 16 тестов, Classic cold SDK + 100 RPC, exit 0. Расширенная модель читателей/диапазонов и реальная интеграция Harvester остаются следующими этапами |
| 3 | Отдельная расширенная схема: ресурсы/диапазоны/сегменты, точные 64-битные числа, writer/playback отдельно, reader/release | Частично: модель v2/validate, runtime v1 и [leases закреплённых ресурсов v2 в I/O-слое](ProgressiveResourceIo.md). В трёх редакциях есть отдельный асинхронный resourceProbe; добавлен [первый медиапрофиль v2](ProgressiveMediaV2.md); безопасное удаление live-ресурсов ещё не подключено |
| 4 / F-01 | Управляемое I/O + backend demux + progressive-remux, H.264/HEVC/VP9/AV1 и AAC/Opus, затем вся обязательная матрица | Частично: чтение подтверждённых диапазонов, ожидание/отмена, late resources, short read/EOF и I/O-диагностика. Добавлено реальное playback v2 двух fMP4 H.264/AAC-LC дорожек через ResourceSource. SDK 1.14 добавляет [HEVC Main/Main10 + AAC-LC fMP4](ProgressiveHevc.md) в schema 2 трёх редакций; SDK 1.15 — [AV1 Main 8/10-bit + AAC-LC fMP4](ProgressiveAv1.md). SDK 1.16 — [VP9 Profile 0/2 8/10-bit + AAC-LC fMP4](ProgressiveVp9.md). SDK 1.17 — [Opus mono/stereo + все четыре видеокодека в fMP4](ProgressiveOpus.md). SDK 1.18 добавляет [отдельные WebM VP9/Opus дорожки](ProgressiveWebM.md). SDK 1.19 — [AV1 Main 8/10-bit + Opus в WebM](ProgressiveWebMAv1.md). SDK 1.20 — [смешанные пары fMP4/WebM](ProgressiveMixedContainers.md) в обоих направлениях. SDK 1.21 — [VP8 и Vorbis mono/stereo в WebM](ProgressiveVp8Vorbis.md). SDK 1.22 — [несколько аудиодорожек и поздние полные UTF-8 SRT](ProgressiveMultiTrack.md). SDK 1.23 — [перемотка Qt внутри подтверждённого участка](ProgressiveSeek.md) без прежнего лимита 10 секунд. SDK 1.24 — [изоляция ошибок optional SRT](ProgressiveOptionalSrt.md). SDK 1.25 — [отложенное подключение полного optional audio](ProgressiveOptionalAudio.md) в трёх редакциях. Полная integrity-проверка, универсальный demux/remux и оставшаяся codec-матрица не реализованы |
| 5 | Несколько аудио/субтитров, главы, metadata, HLS/DASH timeline, live | Частично: SDK 1.22 добавляет до восьми required audio и четырёх optional полных UTF-8 SRT, SDK 1.25 — optional audio после полной загрузки. SDK 1.26 добавляет metadata в status; SDK 1.27 исправляет фактические главы/title в UI, очищение и независимые revision сеансов. Частично скачанная выбранная audio, HLS/DASH timeline и live ещё не реализованы; [контракт](ProgressiveMultiTrack.md) |
| 6 | Общий conformance-корпус, реальные источники, длительные/ресурсные тесты, три сборки; затем подключение реального Harvester отдельной задачей | Не пройдено |

Ближайшие работы после SDK 1.22: расширить Qt multi-audio seek за пределы
первых 10 секунд недокачанного источника; изолировать ошибку optional SRT от A/V
(сейчас повреждённый complete SRT отклоняет start/update), добавить ожидание
только выбранного optional audio, затем применить поздние главы и общие metadata.
Далее — segmented HLS/DASH timeline и live. Полное ТЗ остаётся открытым.

## Что не считать готовой интеграцией

`post-download` — ограничение, а не поддержка просмотра во время скачивания.
Текущий общий ответ `native-progressive-rejected` не заменяет будущие отдельные
reasonCode ожидания индекса/инициализации/диапазона и окончательной ошибки.
Открытие готового файла не закрывает положительную строку progressive-матрицы.

SDK 1.7 переносит [первичную подготовку probe/start через IPC](ProgressivePreparation.md)
из UI; SDK 1.8 добавляет [update/complete и разделение блокировок](ProgressivePublication.md)
для опубликованных v1/v2. SDK 1.9 выносит [IPC реестра и уничтожение Source](ProgressiveRetirement.md)
в workers с ограниченной очередью. SDK 1.10 выносит [GUI/CLI legacy opening](ProgressiveLegacyOpening.md)
из UI и отменяет применение позднего результата. SDK 1.11 выносит [файловые проверки
и индексацию legacy](ProgressiveLegacyMaintenance.md) из metadata mutex UI.
SDK 1.12 выносит [Qt native core/software-renderer teardown](ProgressiveEngineShutdown.md)
в ограниченную очередь; Classic сохраняет COM owner и пропускает sent handshakes.
SDK 1.13 добавляет [close-state machine progressive Classic](ProgressiveClassicClose.md):
STA сохраняет COM-владение, UI откладывает start/exit, Stop отменяет ожидающий start.
Остались GL cleanup, обычные/UI-owned Classic графы, ресурсы больших индексов и другие файловые пути;
требование ТЗ про весь I/O пока не закрыто.
Наличие FFmpeg/libmpv/DirectShow не подтверждает всю матрицу ТЗ.

Без сквозной проверки из реального Harvester в каждой из трёх редакций нельзя
объявлять общую совместимость готовой. Физическая синхронизация A/V, 30-минутный
дрейф и ресурсные пределы остаются непроверенными в рамках этого этапа.

## Расширенная модель: текущая поставка

[Отчёт этапа](ProgressiveModelValidation-2026-10-25.md) отделяет проверку структуры
снимка от воспроизведения. `validate` не открывает файлы, не создаёт читателя и не
заменяет действующий источник. Общий C++-корпус проверяет точные числа, диапазоны,
неизменность опубликованных сегментов, поздние главы и модель нескольких читателей.
В SDK 1.4 модель связана с настоящими OS-идентичностями, дескрипторами и приватными
квитанциями player-owned v1. [Проверки runtime](ProgressiveReaderValidation-2026-10-25.md)
включают удержание после Stop, замену поколения, независимых читателей и crash.
SDK 1.5 добавляет [I/O-слой v2](ProgressiveResourceIo.md) с привязкой поздних файлов,
подтверждёнными диапазонами, ожиданием/отменой и асинхронным стендом трёх редакций.
[Результаты](ProgressiveResourceValidation-2026-10-26.md) отделяют байтовое чтение от
готовности декодера. SDK 1.6 добавляет [проверенный сквозной медиамост](ProgressiveMediaV2.md)
для двух отдельных fMP4 H.264/AAC-LC; SDK 1.14 расширяет его [HEVC Main/Main10](ProgressiveHevc.md), SDK 1.15 — [AV1 Main 8/10-bit](ProgressiveAv1.md). SDK 1.16 — [VP9 Profile 0/2](ProgressiveVp9.md). SDK 1.17 — [Opus mono/stereo](ProgressiveOpus.md). SDK 1.18 добавляет [WebM VP9/Opus](ProgressiveWebM.md), SDK 1.19 — [AV1/Opus в WebM](ProgressiveWebMAv1.md). SDK 1.20 — [смешанные пары fMP4/WebM](ProgressiveMixedContainers.md) в обоих направлениях. SDK 1.21 — [VP8 и Vorbis mono/stereo в WebM](ProgressiveVp8Vorbis.md). SDK 1.22 — [несколько аудиодорожек и поздние полные UTF-8 SRT](ProgressiveMultiTrack.md). SDK 1.23 добавляет [перемотку Qt в подтверждённом участке](ProgressiveSeek.md); SDK 1.24 — [изоляцию optional SRT](ProgressiveOptionalSrt.md). Ближайшие шаги — optional audio и поздние главы/metadata; затем расширение backend demux/remux и полная проверка integrity;
добавление codecHint само по себе не добавляет кодек.

## Общий regression каталога v1

`tests/progressive_v1_conformance.py` выполняет одинаковый wire-сценарий через
поставляемый `ProgressiveClient` для fragmented и indexed MP4:

1. Относительные Unicode-имена, `start` с абсолютным каталогом.
2. Ошибочный каталог и неверный тип поля не принимаются; действующий сеанс жив.
3. Неудачная замена не наследует каталог и не закрывает активный сеанс.
4. Несколько `update` без каталога и с эквивалентным нормализованным каталогом.
5. Реальный `playing` до `complete`, переименование, дописывание, `complete`
   без каталога и естественный `finished`.
6. После завершения и отмены новый сеанс без каталога отвергается.

Qt запускает этот сценарий из `qt/tests/progressiveapi_test.cpp` на обеих ОС.
Classic — из `tests/windows/progressive_family_test.py` в отдельной копии с INI,
отключающим регистрацию контекстного меню. Classic-тест сравнивает команды/иконки
установки в HKCU до и после. Фикстуры генерируются
`qt/tests/generate_media.py`, Harvester и сеть для этого regression не нужны.

При изменении `HarvesterSession.h` пересобирать не только `DSUtil` и приложение,
но и зависимые `BaseSplitter` и `MP4Splitter` (либо выполнить обычную сборку
с зависимостями). Ускоренная сборка с `BuildProjectReferences=false` и прежними
статическими библиотеками оставляет несовместимую раскладку класса в reader.
