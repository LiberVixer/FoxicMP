# Continuous AAC 1.84: циклы и растущий файл

Проверен ограниченный профиль публичного `continuous-h264-aac-window-v1`:
H.264 с тремя B-frames, 30000/1001 fps, AAC-LC stereo 48 kHz. В каждой редакции
выполнены **100 измеряемых + 4 прогревочных цикла** и отдельный сценарий
128-секундного физически растущего MP4 с настоящей паузой writer 60 секунд.
[Измерения, SHA, exit codes и исходники стенда](ProgressiveContinuousAacStressValidation-2026-11-06.json).
Использованы обычные production EXE и их десять SDK-модулей из пакетов 1.84.
Код приложений, выпуски, установки и настройки не менялись. Сверены все 583
защищённых файла и 158 HKCU shell entries. Одноразовая Windows-задача удалена.

## Результаты

| Редакция | Циклы / growing, exit | Max ready → Play | Max release цикла | Resume после 60 s | Max RPC |
|---|---:|---:|---:|---:|---:|
| Linux Qt | 0 / 0 | 0.603 s | 0.250 s | 0.244 s | 0.351 s |
| Windows Qt | 0 / 0 | 0.516 s | 0.422 s | 0.250 s | 0.407 s |
| Windows Classic | 0 / 0 | 0.672 s | 0.250 s | 0.453 s | 0.110 s |

Интервалы измерены клиентом в этих прогонах. Это не гарантированные latency
для произвольного источника, физического устройства или загруженной ОС.

## Циклы

На одних и тех же двух приватных player PID проверяются по 25 операций Stop,
producer Cancel, output readerRelease и complete → seek/resume → Stop. В каждом
цикле физически создан новый partial (12 s из 24 s), проверены настоящий
`playing`, позиция >0,2 s, decoder reader, подключённый audio и применение
PCM-окна. Qt подтверждает AAC decoder и обычный audio backend: Linux `pulse`
на частном sink, Windows `wasapi` с volume 0. Classic использует частный
Null Audio Renderer (Uncompressed). Звук тестов пользователю не выводится.

Проверяются новые reader IDs, exit дочернего worker, закрытие decoder, source
и output filesClosed receipts, удаление кэша и возможность удалить partial
после сверки SHA. Независимый endpoint играет свой completed AAC source
(600 s) с прежними playback/readers и растущей позицией. Это контроль
независимости, а не просмотр всего 600-секундного файла.

Бюджеты прежнего lifecycle стенда оставлены без повышения: handles/fds последних
10 циклов ≤ max циклов 31–40 +8; threads ≤ steady +2 и early +8; RSS/private ≤
первых 10 +256 MiB; средний CPU последних 10 ≤ одного ядра. Ready → настоящий
Play <3 s, release <2 s, каждый записанный RPC <2 s. В отчёте сохранены все
циклы, прогрев, worker metrics и exit proofs, а также десять последовательных
окон памяти. Linux private metric — RssAnon; Windows — PrivateUsage.
Успех этих порогов не доказывает отсутствие всех малых утечек или ограничение
peak memory/CPU всей ОС. Повторные heartbeat expiry в этом срезе не выполнялись.

## Одна растущая загрузка и два viewers

Два endpoints читают один append-only source; writer публикует 27 revisions.
После физического дописывания первая публикация намеренно отложена на 2 s:
`maximumReadEnd` остаётся в ранее подтверждённом префиксе. При паузе записи
60 s сохраняются worker/playback/readers, нет ложного EOF. Первый viewer
вручную ставится на паузу и не возобновляется от update; второй входит в
buffering и возвращается в playing за <2 s после публикации новых данных.
Первый viewer закрывается и открывается с новыми IDs; второй продолжает работу.

После complete сверяются все video payload/PTS/DTS/duration и audio
payload/PTS/DTS с оригиналом, а также PCM presentation length 6143863 samples.
Последняя длительность encoded AAC может включать padding: её равенство
presentation length здесь не заявляется; это окно применяется decoder adapter.
Затем выполняются seek к 126 s, настоящий EOF, release/forget и удаление кэша.
RSS/private, handles/threads, CPU и RPC измеряются с прежними бюджетами;
raw samples, packet proof digests и публикации закреплены SHA в отчёте.

## Воспроизведение проверки

`tests/progressive_aac_package_test.py` принимает новые сценарии
`--scenario aac-lifecycle` и `--scenario aac-growing`, а также `--edition`,
`--player`, `--reader-runner`, `--sdk-directory`, `--report-directory`.
Он закрепляет все десять SDK modules за указанным пакетом и создаёт отдельные
профили; для Classic делает две копии с выключенной регистрацией контекстного
меню **до первого запуска**. Установленные HKCU command/icon проверяются до/после.

Linux запускается через `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio;
Windows — одноразовой интерактивной задачей текущего пользователя, удалённой
после проверки. Выбор платформы в Qt не подменяет реальную проверку другой ОС.
Generic video-only режимы прежних stress helpers остаются по умолчанию;
AAC подключается явно через package driver. Частные отчёты `_bin/aac-stress-*`
не входят в поставляемый SDK и не добавляют hooks в EXE.

## Открытые границы

Это один rate/layout/fps и небольшой кадр 160×90; 128 s source и примерно
две минуты наблюдения не заменяют многочасовой soak, HD/4K или 30-min A/V.
Физическая синхронизация звука и изображения не измерялась. Остальные
AAC rates/layouts, AVIO/OS faults, cache/temp matrix и общая stress-приёмка
остаются открытыми. Прежние Linux release >2 s и воспроизведённый рост Event
на внешнем MPCVR в Classic этим новым успешным профилем не объяснены и не закрыты.
Пассивное чтение modules подтвердило загрузку MpcVideoRenderer64.ax обоими
частными Classic PID во время этой серии; это один снимок, а не трассировка
каждого цикла или установление причины старого роста Event.
YouTubeHarvester, микшер, пользовательский desktop и Wayland не менялись.
