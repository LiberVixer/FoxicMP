# Проверка progressive VP9 — 27 октября 2026

[SDK 1.16 и границы профиля](ProgressiveVp9.md). База:
`552095f6f8dca1c6b7d58cba532b45205889ced4`.
[Машинный отчёт с SHA-256, состояниями и exit codes](ProgressiveVp9Validation-2026-10-27.json).
YouTubeHarvester не изменялся.

## Результаты

| Стенд | Результат |
|---|---|
| Linux Qt | API 69 PASS, legacy/source 19 PASS, native media 100; 3 CTest группы, exit 0 |
| Windows Qt | API 70 PASS, native media 100; 2 CTest группы, exit 0 |
| Windows Classic | 8 SDK групп, семь медиавариантов, async close 5, legacy playback, maintenance 3, opening 4; exit 0 |
| Native media ASan/UBSan | 100 PASS, leak detection включён; итоговый exit 0 |
| FragmentIndex ↔ FFprobe | 6 тестов, все прежние кодеки и VP9 8/10-bit; exit 0 |
| Python SDK / family contract | 35 / 2 теста; exit 0 |

QtTest totals включают init/cleanup, пропусков нет. Linux UI/audio используют
частные X11/D-Bus/PulseAudio. Windows Qt использует отдельные offscreen профили;
Linux source suite не входит в Windows CTest.

Первоначальный ASan harness имел **exit 1**: VP9-фикстуры ещё перезаписывались
генератором, Python получил недостаточное число фрагментов до запуска VP9-case.
После завершения генерации тот же инструментированный EXE прошёл 100 случаев,
**exit 0**. Код плеера не менялся; первоначальный exit сохранён в JSON.
Все итоговые сквозные прогоны используют завершённые одни и те же фикстуры.

## Просмотр до завершения записи

Во всех трёх редакциях проверены H.264, HEVC8, HEVC10, AV1-8, AV1-10, VP9-8 и
VP9-10. Новый VP9 corpus: Profile 0/2, 320×180, 25 fps, 20 секунд, отдельный
AAC-LC 48 kHz; encoder lookahead=25 и auto-alt-ref=1. Это не квалификация всех
coding tools, 4K realtime, GPU или физических устройств.

Общий wire-сценарий проверяет probe без оставшихся leases, start/idempotence,
writer/reader guards, cancel/restart, публикацию 5→12 фрагментов и позицию ≥10 с,
пока оба физических файла ещё меньше полной записи. У каждой дорожки есть
реальные decoder-read bytes; mediaMode/videoCodec соответствуют профилю.
Затем проходят переименование закреплённых файлов, complete, естественный
finished и release после закрытия всех readers.

Qt на обеих ОС подтверждает меняющийся decoded framebuffer **до complete**,
video-format=vp9, decoder pixel format=yuv420p или yuv420p10. Это проверка
декодированных кадров с программным декодером. Classic проверен через его
DirectShow graph/status и чтение payload, без оптического измерения монитора.
Физическая синхронизация звука и вывод HDR/10-bit этим не измерены.

## Защита заголовков и индекса

100 native media случаев включают прежние H.264/HEVC/AV1 и новый VP9: vpcC
length/version/flags, соответствие profile/depth, level/chroma/colour fields,
нулевой init-data size, неверный hint, encrypted entry, dref, ожидание диапазона,
перезапись init, cancel и удержание файлов. Подтверждено определение без hint.
ASan/UBSan инструментирует общий медиамост и ResourceSource; внешние
Qt/libmpv/DirectShow библиотеки не инструментированы этим тестом.

FragmentIndex сравнивает каждый packet offset/size/PTS/DTS/sync с FFprobe,
время — с допуском 1 мкс. Старый v1/legacy, HEVC-only и AV1-only допуски
отвергают VP9; только явный проверенный v2 VP9 допуск разрешает vp09.
Проверка vpcC не заменяет декодирование всего VP9 bitstream и integrity payload.

## Сборки и установка

GCC Linux, MinGW Windows Qt и MSVC Classic: exit 0. Для Classic пересобраны
DSUtil, BaseSplitter, MP4Splitter и приложение. SHA тестовой копии совпал с
итоговым EXE. UTF-16 INI отключал context-menu prompt/registration; команды
и иконки установленного Classic в HKCU до/после совпали. Одноразовая тестовая
задача удалена, LastTaskResult=0.

Linux EXE заменён атомарно с резервной копией; INI сохранил свой хеш. Точные
пути, SHA и capabilities сохранены в JSON. Просмотр не останавливался ради
замены. Windows установки и профили не заменялись. Версия приложения остаётся
0.2.0; новые возможности определяются capabilities/хешем, версия SDK — 1.16.

Остались Opus, WebM/MKV, muxed/remux, metadata/live, реальный Harvester,
физическая A/V-синхронизация и 30-минутный дрейф. Проверки оборудования и Wayland
по указанию пользователя исключены. Полное ТЗ ещё не закрыто.
