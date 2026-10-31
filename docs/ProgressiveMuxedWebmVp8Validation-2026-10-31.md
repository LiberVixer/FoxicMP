# Проверка shared WebM VP8/Opus — 31 октября 2026

SDK 1.45 добавляет [VP8 + Opus в одном растущем WebM](ProgressiveMuxedWebmVp8.md)
в Classic, Windows Qt и Linux Qt. Прежние VP9 и AV1 сохранены. Версия
приложения — 0.2.0. YouTubeHarvester не изменён.

| Среда | Проверка | Финальный exit code |
|---|---|---|
| Linux Qt / GCC, private X11/D-Bus/PulseAudio | 2 VP8 + 6 VP9/AV1 + 5 прежних runtime-сценариев, повтор VP8 | Исходная оболочка 143; повтор 0 |
| Windows Qt / MinGW, offscreen | Те же 8 + 5 сценариев | 0 |
| Windows Classic / MSVC, отдельная безопасная копия | 2 VP8 + 6 VP9/AV1 + 3 shared fMP4 + v1 directory/API | 0 |
| Linux GCC native | 56 сценариев shared WebM | 0 |
| Windows MinGW native | Те же 56 сценариев | 0 |
| Windows MSVC native | Те же 56, с OS replacement protection | 0 |
| Linux ASan/UBSan, leak detection | Те же 56, без диагностики sanitizers | 0 |
| Native compatibility, Linux / Windows | Media, WebM segment stream, shared fMP4 | 0 / 0 |
| Python SDK v1/v2, family contract | 15 + 31 + 2 теста | 0 |

## Playback и сравнение данных

На каждую редакцию — два новых VP8-сценария: 25 fps/stereo Opus и
30000/1001 fps/mono Opus с неизвестной до complete длиной resource.
Opus 48 kHz. Повторно пройдены шесть прежних VP9/AV1-сценариев; всего
24 shared WebM runtime-сценария, из них шесть новых VP8.

Проверены partial header/waiting initialization, недостаточный буфер,
воспроизведение до complete, pause/update, backward seek в паузе, resume,
ожидание полного Cluster без EOF, forward seek после дозагрузки, natural
EOF и release. Reader ID сохраняется. Qt проверяет changing framebuffer
hashes, actual VP8/VP9/AV1 codec, pixel format, decoded Opus rate/channels.
Classic проверяет настоящий graph, продвижение позиции и decoder I/O
обеих дорожек. Отдельного capture pixels и физического аудио Classic не было.

Каждый native toolchain сравнивает 21374 access units из 14 fixtures:
по пять VP9/AV1 (7225 units на codec) и четыре VP8 (6924 units). VP8:
4 секунды 25 fps (100+201 packets), дробная частота (120+201), known-size
Segment (100+201), 80 секунд (2000+4001). Encoded packet hashes, PTS/DTS и
decoded video/PCM совпали с исходными общими файлами. Максимальная разница
PTS/DTS — 0 × 100 ns. Для старых 10-bit VP9/AV1 используется yuv420p10le.
Это не измерение физической синхронизации выхода.

Сохраняются исходные blocks, timebase, fractional DefaultDuration и
DiscardPadding. Проверены arbitrary byte cuts, immutable virtual prefix,
обратные byte reads, один physical lease, удержание до последнего владельца
и EOF только после complete. Предельная память/throughput и многочасовая
нагрузка не измерялись.

## Ошибки и совместимость

Actual VP8 с VP9-hint либо без video hint отвергается; VP9 с VP8-hint
тоже отвергается. Отсутствующий hint по-прежнему означает legacy VP9.
Проверены семь повреждений начального VP8 packet: interframe вместо key,
невидимый кадр, reserved version, сигнатура, ширина, масштаб и выход первой
секции за packet. После неуспешного constructor reader leases не остаются.
Положительные VP8 fixtures используют version 0; допустимые parser версии
1–3 этим этапом отдельно не декодировались.

Повторно пройдены прежние fault-сценарии: AV1 configuration/initial sequence,
wrong actual codec, undeclared track, lacing, regression timestamps,
нулевой TrackNumber, оба допустимых Opus BlockDuration варианта, неверная
BlockDuration, SHA rewrite, truncate, file replacement, partial complete
и cancellation. Windows защищает pinned file от replacement до release;
Linux обнаруживает выполненную подмену. Смена codecHint при update
отвергается до admission и сохраняет прежний индекс.

Python SDK требует точный VP8 mode. VP9, shared AV1, segmented VP8 либо
общий profiles/muxed flag не разрешают VP8 автоматически. При отказе нет
start/update, изменения last или uncertain. Vorbis с VP8 не принимается.
Прежние VP9/AV1 profile negotiation остаются рабочими.

В обеих Qt-сборках повторно пройдены v1 writer, growing TS, shared fMP4
AVC/AAC, separate WebM VP8 и separate WebM AV1: по 7/7 QTest checks с
init/cleanup. Shared WebM — по 10/10. Native compatibility — media,
WebM segment stream и shared fMP4: три suites на каждой ОС. С новым
shared WebM — четыре; в Linux новый suite запущен отдельно, в Windows
включён в CTest. Classic прошёл три shared fMP4 и оба v1 directory/API
сценария.

Первый Linux native test был запущен до завершения линковки. Build получил
exit 1 (Text file busy); преждевременный тест остановлен с exit 143.
Затем сборка завершена до повторного запуска полного native suite.
Финальные exit codes — 0, исходные 1/143 сохранены в JSON. Пользовательский
плеер и просмотр не останавливались.

Полный Linux UI-прогон напечатал 10/10 QTest и успешный CTest, после чего
внешний процесс завершился с exit 143 при teardown. Источник сигнала не
установлен. Оставшихся приватных UI/audio процессов не было. Оба VP8-сценария
повторены в свежем приватном окружении: 4/4 QTest с init/cleanup и exit 0.
Исходный 143 сохранён отдельно; установка проверяет успешность повтора.

Внешняя SSH-команда Windows также была отмечена инструментом как exit 143,
тогда как SSH-helper сохранил remote exit 0. Нулевые коды build/UI/native
и успешные завершения в логах проверены отдельно и сохранены в JSON.

Нефатальные сообщения Qt teardown о закрытии частного D-Bus и
QEventDispatcherWin32::wakeUp, если присутствуют в логах, перечислены
в JSON; завершение процессов проверено отдельно по exit code.

## Установка и границы

Linux EXE заменён атомарно, backup — `foxicmp-qt.before-sdk-1.45-20261031T073932Z`. Сохранены 203
сопутствующих файлов. SHA-256 текущего INI до/после совпадает. Версия и
capabilities установленного EXE сверены с проверенной сборкой. Просмотр
не прерывался.

Установленные Windows EXE/INI не заменялись. Classic тестировался с частным
INI, ContextMenuPrompted=1 и ContextMenuEnabled=0, с проверкой неизменности
HKCU shell commands/icons до/после. Одноразовая задача и staging удалены;
tested EXE совпадает по SHA с итоговой build-сборкой.

[Полные результаты, exit codes и SHA-256](ProgressiveMuxedWebmVp8Validation-2026-10-31.json).
Этот этап не подтверждает shared Vorbis, дополнительные tracks,
live/config changes, реальные сетевые источники, многочасовую физическую
A/V-приёмку, hardware или Wayland. Ограничения shared WebM сохранены.
SDK ZIP — Python-клиент, examples и документация, без EXE/установщиков.
