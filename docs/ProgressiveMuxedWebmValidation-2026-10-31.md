# Проверка shared WebM — 31 октября 2026

SDK 1.43 добавляет [один растущий WebM VP9/Opus](ProgressiveMuxedWebm.md)
в Classic, Windows Qt и Linux Qt. Два виртуальных single-track WebM читают
исходные blocks общего файла без перекодирования. Версия приложения —
0.2.0. YouTubeHarvester не изменён.

| Среда | Проверка | Финальный exit code |
|---|---|---|
| Linux Qt / GCC, private X11/D-Bus/PulseAudio | 3 новых + 7 прежних runtime-сценариев | 0 |
| Windows Qt / MinGW, offscreen | Те же 3 + 7 сценариев | 0 |
| Windows Classic / MSVC, частная безопасная копия | 3 новых, 3 shared fMP4 и прежний v1 directory/API | 0 |
| Linux GCC native | 23 новых сценария | 0 |
| Windows MinGW native | Те же 23 сценария | 0 |
| Windows MSVC native | Те же 23, с OS replacement protection | 0 |
| Linux ASan/UBSan, leak detection | Те же 23, без диагностики sanitizers | 0 |
| Native compatibility, Linux / Windows | По 14 suites | 0 / 0 |
| Shared fMP4, MSVC / ASan/UBSan | По 21 прежней проверке | 0 / 0 |
| Python SDK v1/v2, family contract | 15 + 29 + 2 теста | 0 |

## Playback и подтверждённые bytes

По три новых сценария на редакцию, всего девять: VP9 Profile 0/8-bit со
stereo Opus, Profile 2/10-bit со stereo Opus и 30000/1001 fps с mono Opus
и неизвестной до complete длиной resource. Во всех вариантах Opus 48 kHz.

Проверены partial header и `waiting_initialization`, недостаточный буфер,
игра до complete, pause/update, backward seek в паузе, resume, ожидание
полного Cluster без преждевременного EOF, forward seek после дозагрузки,
natural EOF/release и постоянный reader ID. Qt дополнительно подтверждает
смену framebuffer hashes, VP9/yuv420p либо yuv420p10le и реальные decoded
Opus rate/channels. Classic проверяет настоящий graph, продвижение позиции
и decoder I/O обеих дорожек; capture pixels и физического аудио не выполнен.

Каждый native toolchain сравнивает 7225 access units: 100+201, 100+201,
120+201, 100+201 и 2000+4001. Последний вариант длится 80 секунд. Среди
fixtures есть Segment известного и неизвестного размера. Проверяются
encoded packet SHA, PTS/DTS, decoded video/PCM hashes, arbitrary byte cuts,
immutable virtual prefix, обратные byte reads, единый physical lease и
release после последнего владельца. PTS/DTS совпали с ffprobe: максимум
разницы 0 × 100 ns. Для 10-bit используется raw yuv420p10le без потери бит.
Это не измерение физической синхронизации выхода.

DefaultDuration с дробными nanoseconds округляется отдельно для каждого
sample end, без накопления ошибки. Поддержаны оба встречающихся варианта
Opus BlockDuration: полный packet и округлённый audible remainder при
DiscardPadding. Длительность берётся из Opus TOC; padding вычитается один
раз. Оба варианта дают одинаковый индекс, несогласованный hint отвергается.
Manifest duration envelope допускает до 1 ms из-за container ticks.

## Отказы и регрессии

Проверены undeclared track ID, lacing, regression timestamps, неправильный
actual codec и нулевой TrackNumber. SHA rewrite, truncate, cancellation и
незавершённый Cluster при explicit complete закрывают обе проекции. Ошибка
metadata/profile до admission сохраняет прежний сеанс. Частичный head ждёт
новой публикации; всё записанное при downloading не считается EOF.

Windows запрещает replacement pinned file до release: прежнее чтение
остаётся рабочим, затем замена разрешена. Linux допускает replacement,
который reader обнаруживает по file identity и закрывает обе проекции.

Начальные runtime-прогоны завершились с exit 8, MSVC native — с exit 1:
Windows FFmpeg записывает rounded audible BlockDuration, тогда как Linux
FFmpeg может не записывать это поле. Устранено повторное вычитание padding;
оба варианта теперь проверяются явно. Первый sanitizer-прогон остановился
на `assert groups` (exit 1), потому что fixture предполагал наличие
BlockDuration. Исправлена генерация обоих вариантов с пересборкой Cluster;
финальный прогон завершился с exit 0 без диагностики ASan/UBSan. Исходные
коды сохранены в JSON, успешные результаты их не подменяют.

Первый частный Classic-прогон содержал устаревшую проверку
`decoderContainer=fmp4`; исправлено на `webm`. Его временная задача и копия
удалены перед повторной проверкой. Финальная удалённая очистка прошла,
но локальная SSH-обёртка получила exit 1 из-за удвоенного `_bin` в имени
выходного файла. Отдельная read-only проверка подтвердила удаление задачи
и staging, а также совпадение SHA тестового EXE; её exit code — 0.

После выделения общего scalar bridge с fMP4 повторно пройдены прежние
v1 writer, growing TS, separate TS, muxed TS segments, muxed fMP4 AVC/AAC,
separate fMP4 AVC/AAC и separate WebM VP9/Opus: по 9/9 QTest checks с
init/cleanup на обеих ОС. Новые Qt-сценарии — по 5/5. Native compatibility
прошла 14/14 на каждой ОС. Shared fMP4 дополнительно проверен MSVC и
ASan/UBSan, по 21 проверке. Classic прошёл три прежних shared fMP4 и оба
v1 directory/API сценария: обычные и fragmented файлы. После успешных
Qt compatibility-прогонов наблюдались сообщения teardown: Windows
QEventDispatcherWin32::wakeUp и закрытие частного Linux D-Bus. Процессы
завершились с exit 0; эти сообщения сохранены в JSON.

## Установка и границы

Linux EXE заменён атомарно; предыдущий EXE сохранён как
`foxicmp-qt.before-sdk-1.43-20261031T054106Z`. Сохранены 201 сопутствующих файлов. SHA-256 текущего INI
до/после совпадает. Версия и capabilities установленного EXE совпадают с
проверенной сборкой. Просмотр не прерывался.

Установленные Windows EXE/INI не заменялись. Classic тестировался с частным
INI, `ContextMenuPrompted=1` и `ContextMenuEnabled=0`; команды и иконки
установленного HKCU context menu сверены до/после. Одноразовая задача и
staging удалены, tested Classic EXE совпадает по SHA с итоговым build EXE.

[Полные результаты, exit codes и SHA-256](ProgressiveMuxedWebmValidation-2026-10-31.json).
VP8/AV1/Vorbis в совместном WebM, дополнительные tracks, live/config changes,
unknown-size Clusters, реальные сайты, многочасовая физическая A/V-приёмка,
hardware и Wayland этим этапом не проверялись. Layout/index snapshots
копируются при публикации; предельные память, ёмкость и throughput требуют
отдельного измерения. SDK ZIP содержит клиент, examples и документацию;
он сам по себе не обновляет Windows EXE.
