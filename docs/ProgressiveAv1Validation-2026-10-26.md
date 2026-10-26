# Проверка progressive AV1 — 26 октября 2026

[SDK 1.15 и ограничения профиля](ProgressiveAv1.md). База:
`ad5baee2d42eaea3d5841f3c2638abd90065bb94`.
[Машинный отчёт: SHA-256 EXE/фикстур, состояния и точные exit codes](ProgressiveAv1Validation-2026-10-26.json).
YouTubeHarvester не изменялся.

## Результаты

| Стенд | Результат |
|---|---|
| Linux Qt | Полный API 67 PASS, legacy/source 19 PASS; native media после исправления теста 70 PASS |
| Windows Qt | Полный API 68 PASS и native media 70; 2 CTest группы, exit 0 |
| Classic | 8 SDK групп, пять медиапрофилей, async close 5, legacy playback, maintenance 3 и opening 4; exit 0 |
| Native media ASan/UBSan | 70 PASS, leak detection включён; exit 0 |
| FragmentIndex ↔ FFprobe | 5 тестов, AV1 8/10-bit и прежние кодеки/негативные случаи; exit 0 |
| Python SDK / family contract | 35 / 2; exit 0 |

QtTest totals включают init/cleanup. Linux использует частные X11/D-Bus/PulseAudio;
Windows Qt — отдельные offscreen профили. Windows CTest не содержит Linux source
suite, поэтому число групп отличается.

Первый общий Linux CTest завершился **exit 8**: native negative test искал первое
av01 и менял бренд внутри ftyp вместо sample entry. Эта мутация не делала источник
зашифрованным, поэтому ожидание отказа было неверным. Поиск ограничен moov.
API 67 и source 19 в том общем прогоне прошли; затем native media выполнен отдельно
повторно: 70 PASS, CTest **exit 0**. Код плеера между этими прогонами не менялся.
Первоначальные standalone native/ASan имели exit 1 по той же тестовой причине;
итоговые оба — exit 0. Общий первоначальный exit 8 не переименован в 0.

## Реальное воспроизведение недокачанных файлов

Единый wire-сценарий теперь выполняется во всех редакциях для пяти профилей:
H.264, HEVC8, HEVC10, AV1-8 и AV1-10. Новый AV1 corpus — Main, 320×180,
25 fps, 20 секунд, encoder lag-in-frames=0, отдельный AAC-LC 48 kHz.
Это не квалификация всех AV1 coding tools или 4K realtime.

Сценарий проверяет probe без оставшихся leases, start/idempotence, writer/reader
identity guards, cancel/restart и новый reader. После публикации двенадцати
фрагментов позиция проходит десять секунд, пока оба физических файла меньше полной
записи и writer остаётся downloading. Есть реальные decoder I/O bytes каждой
дорожки, mediaMode/videoCodec совпадают с профилем. Далее — переименование,
дописывание, complete, естественный finished и фактическое release всех readers.

Qt дополнительно подтверждает меняющийся decoded framebuffer **до complete**;
фактический video-format=av1, decoder pixel format — yuv420p или yuv420p10.
Это прошло на Linux и Windows с программным декодированием. Main10 HEVC также
сохранён в regression. Измерения физического экрана или звукового выхода нет.
Classic подтверждён его DirectShow graph/status и decoder-read bytes, а не
оптической записью монитора.

## Валидатор и индекс

Native media корпус включает marker/version/profile/level/chroma, reserved bits,
неподдержанный presentation delay, отсутствие Sequence Header в configuration,
OBU header/type/size/LEB128 overrun, несовпадение profile, encrypted entries,
external/invalid dref, неверный hint, ожидание диапазона, init rewrite и отмену.
ASan/UBSan инструментирует этот общий C++ путь с leak detection; внешние
Qt/libmpv/DirectShow библиотеки в этом прогоне не инструментированы.

Индекс проверяется против FFprobe по каждому packet offset/size/PTS/DTS/sync flag,
с допуском 1 мкс для времени. Без явного AV1 допуска такой файл отвергается;
старый HEVC допуск также не разрешает AV1. V1/legacy scope не расширился случайно.
Полный AV1 bitstream/SPS parser не заменяется ограниченной проверкой av1C/OBU.

## Сборки и установка

GCC Linux, MinGW Qt Windows и MSVC Classic: сборки exit 0. В Classic пересобраны
DSUtil, BaseSplitter, MP4Splitter и приложение; хеш отдельной тестовой копии
совпал с итоговым EXE. Тестовый UTF-16 INI содержит ContextMenuPrompted=1 и
ContextMenuEnabled=0, HKCU команды/иконки установки не изменились. Одноразовая
интерактивная задача удалена, LastTaskResult=0.

Linux EXE обновлён атомарно с резервной копией. Версия продукта остаётся 0.2.0,
новые возможности определяются capabilities и хешем; номер SDK — 1.15.
INI сохранил SHA-256
`ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
Windows установки и профили не заменялись.

Не закрыты физическая A/V-синхронизация/30-минутный дрейф, HDR passthrough и
10-bit вывод Qt, WebM/MKV/VP9/Opus, remux/live/metadata, настоящий Harvester,
оборудование и Wayland.
