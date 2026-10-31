# Проверка совместных TS-сегментов — 31 октября 2026

SDK 1.40 добавляет [явный muxed TS профиль](ProgressiveMuxedTsPlayback.md)
во все три редакции. В каждом media-файле находятся AVC и AAC; декодеры
получают два logical view через общий виртуальный MP4. Версия приложения —
0.2.0. Harvester не изменён.

| Среда | Проверка | Итоговый exit code |
|---|---|---|
| Linux Qt, GCC, private X11/D-Bus/PulseAudio | 4 playback-сценария | 0 |
| Windows Qt, MinGW, offscreen | Те же 4 playback-сценария | 0 |
| Windows Classic, MSVC, отдельная безопасная копия | Те же 4 сценария | 0 |
| Linux GCC native | 8 real-media bridge + 24 transport/resource сценария | 0 |
| Windows MinGW native | Те же 8 + 24 | 0 |
| Windows MSVC native | Те же 8 + 24 | 0 |
| Linux ASan/UBSan, leak detection | Те же 8 + 24, без ошибок sanitizers | 0 |
| Python SDK v1/v2 и family contract | 15 + 26 + 2 теста | 0 |

## Плееры и media bytes

По четыре сценария на редакцию: Main 25 fps/B-frames + AAC stereo 48 kHz;
High 30000/1001 fps + AAC 44.1 kHz; Main с audio preroll 10 ms и отрицательным
video DTS; 80-секундный Main-каталог из 81 физического ресурса. Всего 12
новых playback-сценариев. Проверены gap/prefix, игра до complete,
pause/update, backward seek в паузе, resume, ожидание хвоста, forward seek
после публикации и natural EOF/release с сохранением reader identity.

Qt дополнительно проверяет H.264/yuv420p, decoded AAC sampling rate/channels
и изменение framebuffer hashes. Classic проверяет настоящий граф,
продвижение позиции и decoder I/O обеих дорожек; отдельный capture его pixels
или физического аудиовыхода не проводился.

Каждый нативный bridge-прогон сравнивает 6406 access units: 75+142, 90+131,
75+142 и 2000+3751. Сверены FragmentIndex, неизменность прежнего виртуального
префикса после append, backward bytes, complete/EOF и leases последнего
владельца. После FFmpeg-декодирования video/PCM hashes совпадают с оригинальным
мультиплексированным TS; preroll reference PCM обрезается на 480 samples.
Максимальное фактическое отличие PTS/DTS от ffprobe — 120 × 100 ns (12 µs)
на AAC 44.1 kHz. Это сравнение временных меток, не физической A/V sync.

## Transport, shared resources и отказ

24 новых сценария проверяют выбор PID по типу независимо от порядка ES в PMT,
два codec index над общими bytes, interleaving внутри video PES больше 64 KiB,
AAC перед первым video PCR, отсутствие третьего PID и строгий single-stream
профиль SDK 1.39. Дубли PID/типов, третий ES, HEVC и PCR на audio PID
отклоняются. Оба codec-прохода должны завершиться до публикации кандидата.

512 детерминированных повреждений дают 84 принятых framing-прохода и 428
отказов; принятие не доказывает entropy validity. Отдельные fault-injection
сценарии повреждают только AAC: constructor убирает все квитанции, а ошибка
update не публикует новый video index и закрывает чтение обоих представлений.
Ошибка metadata до изменения источника сохраняет прежний читаемый префикс.

Файлы общие физически: сначала открыты только init и один media, а не четыре
файла для двух дорожек. При работе с каталогом из 97 ресурсов/шести страниц
одновременно остаётся не больше восьми media/init leases. Retain/release
проверяет закрытие после последнего владельца. Повтор ресурса внутри одной
дорожки и shared resources в прежних профилях не разрешались.

При расширении тестового catalog producer обнаружено различие presentation
границ audio/video одного TS: выбор descriptor независимо по времени мог
дать разное число сегментов в двух views. Producer исправлен: готовность
общего физического ресурса публикует оба descriptor атомарно. В раннем
fault-injection тесте нулевой PES byte заменялся таким же нулём; теперь он
меняется на `X`, и проверка действительно доказывает отказ.

## Совместимость и установка

Linux прошёл 14 native suites, Windows — 11: прежние fMP4/WebM/раздельный TS,
staging/catalog/media и новые muxed сценарии; Linux отдельно проверяет catalog
model/I/O/cache. На обеих Qt-сборках также проверены прежний v1 writer,
раздельный TS, fMP4 H.264/AAC и WebM VP9/Opus. Classic отдельно прошёл v1
directory/API сценарий.

Linux EXE заменён атомарно. Backup:
`foxicmp-qt.before-sdk-1.40-20261031T025826Z`. INI до/после совпадает по SHA-256;
198 сопутствующих файлов сохранены, просмотр не прерывался. Установленные
capabilities сверены с проверенной сборкой.

Windows-установки не заменялись. Classic запускался с частным INI,
`ContextMenuPrompted=1` и `ContextMenuEnabled=0`. HKCU shell registration
проверена до и после; одноразовая задача и staging-копия удалены, tested EXE
совпадает по SHA-256 с итоговым build EXE.

[Полные результаты, exit codes и SHA-256](ProgressiveMuxedTsValidation-2026-10-31.json).
Один растущий файл с незавершённым PES, muxed fMP4/WebM, произвольный HLS/DASH,
live/config changes, реальные сайты, многочасовая/физическая A/V-приёмка,
предельная ёмкость индекса, hardware и Wayland не проверены и не объявляются
готовыми. Профиль требует PAT/PMT/ES/null PID; fixtures исключают SDT.
