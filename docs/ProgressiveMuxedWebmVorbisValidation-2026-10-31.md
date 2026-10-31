# Проверка shared WebM Vorbis — 31 октября 2026

SDK 1.46 добавляет [VP8/Vorbis, VP9/Vorbis и AV1/Vorbis](ProgressiveMuxedWebmVorbis.md)
в Classic, Windows Qt и Linux Qt. Версия приложения — 0.2.0. Прежние Opus
modes и defaults сохранены; YouTubeHarvester не изменён.

| Среда | Проверка | Финальный exit code |
|---|---|---|
| Linux Qt / GCC, private X11/D-Bus/PulseAudio | 6 Vorbis + 7 прежних runtime-сценариев | 0 |
| Windows Qt / MinGW, offscreen | Те же 6 + 7 сценариев | 0 |
| Windows Classic / MSVC, безопасная частная копия | 6 Vorbis + 3 Opus + 3 shared fMP4 + v1 directory/API | 0 |
| Linux GCC native | 42 Vorbis-проверки | 0 |
| Windows MinGW native | Те же 42 | 0 |
| Windows MSVC native | Те же 42 | 0 |
| Linux ASan/UBSan, leak detection | Те же 42, без диагностики sanitizers | 0 |
| Native compatibility, Linux / Windows | Legacy media, WebM segments, shared fMP4, 56 Opus checks | 0 / 0 |
| MSVC / ASan+UBSan compatibility | По 6 коротких Opus и 21 shared fMP4 checks | 0 / 0 |
| Python SDK v1/v2, family contract | 15 + 32 + 2 теста | 0 |

## Воспроизведение и данные

По шесть новых runtime-сценариев на редакцию, всего 18: VP8 stereo и
30000/1001 fps mono, VP9 8-bit stereo/10-bit mono, AV1 8-bit stereo/10-bit
mono. Во всех вариантах Vorbis 48 kHz. Для дробной частоты длина resource
неизвестна до complete. Тестовый сигнал содержит импульсы и тихие интервалы,
чтобы упражнять смену размеров Vorbis block modes.

Проверены waiting initialization, partial prefix/buffering, игра до complete,
pause/update, backward seek в паузе, resume, ожидание полного Cluster без
EOF, forward seek после дозагрузки и natural EOF/release. Reader ID остаётся
тем же. Qt подтверждает changing framebuffer hashes, actual video/audio
codecs, pixel format, decoded channels/rate. Classic проверяет реальный
graph, продвижение позиции и decoder I/O обеих дорожек; отдельный capture
pixels и физического аудио не выполнен.

Каждый native toolchain сравнивает 9924 access units из восьми fixtures:
семь коротких вариантов по 4 секунды (шесть раз 100+264 packets и один
120+264) и 80 секунд VP9/Vorbis (2000+5356). Есть known-size и unknown-size
Segment/resource. Packet hashes, PTS/DTS и decoded video/PCM совпали с
исходным общим WebM; для 10-bit используется yuv420p10le. Максимальная
разница PTS/DTS — 0 × 100 ns. Это сравнение original/projected FFprobe clock,
а не физическая синхронизация выхода и не требование полного совпадения
Vorbis sample end с квантованной millisecond длительностью FFprobe.

Проверяются arbitrary byte cuts, immutable virtual prefix, backward byte
reads, prefix waiting, complete/EOF, один physical lease, retain/release
последнего владельца и отсутствие промежуточных файлов у плеера. State
Vorbis предыдущего окна сохраняется между Clusters и candidate updates.

## Заголовки, задержка и обрезка

Проверены явные video/audio hints, отказ при Vorbis actual/Opus hint и
Opus actual/Vorbis hint. Отдельно проверены Xiph count, identification
signature/version/channels/rate/block sizes/framing, comment framing,
setup codebook sync и header packet вместо audio. Неуспешный constructor
не оставляет reader lease. Metadata rejection при смене audio hint сохраняет
прежний индекс и чтение; SHA rewrite закрывает обе проекции.

Windows FFmpeg записывает CodecDelay 2666667 ns для 128 startup samples;
Linux FFmpeg может его не записывать. Допускается целое число samples с
floor/ceil nanosecond rounding, до половины наибольшего блока из header.
В каждом toolchain явно испытаны нуль, 2666666 и 2666667 ns. Нецелый sample,
чрезмерная задержка и Vorbis metadata SeekPreRoll отвергаются. Исходный
CodecDelay сохраняется в virtual header, sample timestamps учитывают его.

Windows FFmpeg также записывает на хвосте audible BlockDuration вместе с
DiscardPadding. Источник длительности теперь — packet/window mode;
padding вычитается один раз. Явные тесты full-packet и audible-remainder
hints с padding дают одинаковую длину первого sample 16666 × 100 ns.
Несогласованный hint и padding больше packet отвергаются. Прежний допуск
Vorbis для coarse timestamp scales сохранён; shared-профиль требует 1 ms.

Первые Windows native-прогоны дали exit 1 из-за startup delay, затем из-за
BlockDuration. Соответствующие Qt CTest-прогоны дали exit 8. Частный
предварительный Linux UI-прогон и preliminary sanitizer run остановлены для
исправления исходников; коды остановки 143 сохранены отдельно. Финальные
EXE пересобраны и прошли полную проверку с exit 0. Первичные 1/8/143 не
подменены успешными результатами и находятся в JSON.

## Совместимость

Qt дополнительно прошёл v1 writer, growing TS, shared fMP4 AVC/AAC,
shared VP8/VP9/AV1 с Opus и separate optional Vorbis: семь сценариев, по
9/9 QTest checks с init/cleanup. Новые Vorbis-сценарии — по 8/8. Classic
прошёл три Opus, три shared fMP4 и оба v1 directory/API варианта.

Native compatibility проверена на Linux/Windows: legacy media, WebM segment
stream, shared fMP4 и полный прежний shared Opus suite с 56 проверками.
С Vorbis это пять suites на ОС; в Linux новый Vorbis запускался отдельно.
MSVC и ASan/UBSan дополнительно прошли короткие VP8/VP9/AV1 Opus-варианты
(6 checks) и 21 shared fMP4 check. Прежние профили не объявляются заново
полностью проверенными на каждом возможном источнике.

Python SDK требует group Vorbis mode и точную video/audio pair. Отказ
происходит до start/update и изменения last/uncertain; Opus defaults
сохраняются. Нефатальные Qt teardown-сообщения, если присутствуют, перечислены
в JSON; process exit проверяется отдельно.

## Установка и границы

Linux EXE заменён атомарно, backup — `foxicmp-qt.before-sdk-1.46-20261031T082054Z`. Проверена неизменность
204 сопутствующих файлов, включая текущий INI. Его SHA-256 до/после
совпадает; версия и capabilities установленных bytes сверены с проверенной
сборкой. Просмотр не прерывался.

Установленные Windows EXE/INI не заменялись. Classic использовал частный
INI, ContextMenuPrompted=1 и ContextMenuEnabled=0. HKCU shell commands/icons
сверены до/после. Одноразовая задача и staging удалены; SHA tested EXE
совпадает с итоговым build EXE.

[Полные результаты, exit codes и SHA-256](ProgressiveMuxedWebmVorbisValidation-2026-10-31.json).
Это ограниченный VOD-профиль, не live/config changes, multi-audio или
произвольный Matroska/Vorbis. Предельные memory/index/throughput, реальные
сетевые источники, многочасовая физическая A/V-приёмка, hardware и Wayland
этим этапом не проверены. SDK ZIP содержит клиент, examples и документацию,
без EXE/установщиков. Только генератор AV1 fixtures использует MP4 для
получения extradata; плеер промежуточные media не создаёт.
