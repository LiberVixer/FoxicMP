# Проверка shared WebM AV1/Opus — 31 октября 2026

SDK 1.44 добавляет [AV1 Main 8/10-bit с Opus в одном растущем WebM](ProgressiveMuxedWebmAv1.md)
в Classic, Windows Qt и Linux Qt. Режим VP9 SDK 1.43 сохранён. Версия
приложения — 0.2.0. YouTubeHarvester не изменён.

| Среда | Проверка | Финальный exit code |
|---|---|---|
| Linux Qt / GCC, private X11/D-Bus/PulseAudio | 3 AV1 + 3 VP9 + 8 прежних runtime-сценариев | 0 |
| Windows Qt / MinGW, offscreen | Те же 6 + 8 сценариев | 0 |
| Windows Classic / MSVC, отдельная безопасная копия | 3 AV1 + 3 VP9 + 3 shared fMP4 + v1 directory/API | 0 |
| Linux GCC native | 38 сценариев shared WebM | 0 |
| Windows MinGW native | Те же 38 сценариев | 0 |
| Windows MSVC native | Те же 38, с OS replacement protection | 0 |
| Linux ASan/UBSan, leak detection | Те же 38, без диагностики sanitizers | 0 |
| Native compatibility, Linux / Windows | По 14 suites | 0 / 0 |
| Shared fMP4, MSVC / ASan/UBSan | По 21 прежней проверке | 0 / 0 |
| Python SDK v1/v2, family contract | 15 + 30 + 2 теста | 0 |

## Воспроизведение и точность данных

На каждую редакцию — три новых AV1-сценария: Main 8-bit/stereo Opus,
Main 10-bit/stereo Opus и 30000/1001 fps/mono Opus с неизвестной до complete
длиной resource. Opus 48 kHz. Три прежних VP9-сценария проверены повторно;
всего 18 shared WebM runtime-сценариев, из них девять новых AV1.

Проверены partial header и waiting initialization, недостаточный буфер,
воспроизведение до complete, pause/update, backward seek в паузе, resume,
ожидание целого Cluster без EOF, forward seek после дозагрузки, natural
EOF и release; reader ID сохраняется. Qt проверяет changing framebuffer
hashes, реальный AV1/VP9 codec, pixel format yuv420p либо yuv420p10le,
decoded Opus rate/channels. Classic проверяет настоящий graph,
продвижение позиции и decoder I/O обеих дорожек. Отдельного capture pixels
или физического аудио Classic не выполнялось.

Каждый native toolchain сравнивает 14450 access units: пять VP9 и пять AV1
fixtures. Для каждого codec — 4 секунды 8-bit (100+201 packets), 10-bit
(100+201), дробная частота (120+201), known-size Segment (100+201) и
80 секунд (2000+4001). Encoded packet hashes, PTS/DTS и decoded video/PCM
совпали с исходным общим файлом. Для 10-bit используется yuv420p10le.
Максимальная разница PTS/DTS — 0 × 100 ns. Это не измерение физической
синхронизации выхода.

Сохранены исходные blocks, timebase, fractional DefaultDuration и
DiscardPadding. Проверены arbitrary byte cuts, immutable virtual prefix,
обратное byte reading, единый physical lease, удержание до последнего
владельца, EOF только после complete. Предельные memory/throughput и
многочасовая нагрузка не измерены.

## Отказы и совместимость

Заявленный codec обязан совпадать с actual TrackEntry: AV1 с VP9-hint и
VP9 с AV1-hint отвергаются. Неверные marker/version и non-Main av1C, а также
несовпадение Sequence Header configuration с первым packet не допускаются.
Смена codecHint при update отвергается до admission с сохранением старого
индекса. Первый AV1 keyframe/sequence проверяет существующий WebMIndex.

Повторно пройдены fault-сценарии shared WebM: undeclared track, lacing,
regression timestamps, wrong actual codec, нулевой TrackNumber, оба
законных Opus BlockDuration варианта и несогласованный duration, SHA rewrite,
truncate, file replacement, partial head/Cluster при complete и cancellation.
Windows запрещает replacement pinned файла до release; Linux обнаруживает
выполненную подмену. Оба поведения проверены без ослабления защиты.

Python SDK не принимает AV1 по VP9 capability, общей muxed-галочке или
segmented AV1 mode. Отказ не вызывает start/update и не изменяет last либо
uncertain. Неподдержанные codec hints отвергаются, включая AVC-only shared
fMP4. Точный AV1 mode разрешает новый профиль; VP9 остаётся совместимым.

В обеих Qt-сборках повторно пройдены v1 writer, growing TS, separate TS,
muxed TS segments, shared fMP4 AVC/AAC, separate fMP4 AVC/AAC, separate WebM
VP9 и separate WebM AV1: по 10/10 QTest checks с init/cleanup. Shared WebM —
по 8/8 с init/cleanup. Native suites — по 14/14. Classic дополнительно
прошёл три shared fMP4 и оба v1 directory/API сценария.

Начальная прямая генерация AV1+Opus WebM завершилась с FFmpeg exit 183:
configuration AV1 должна быть доступна до WebM header. Fixture теперь
создаёт video-only delayed-moov MP4, затем копирует исходные packets в
WebM, как существующие separate AV1-тесты. Это только подготовка тестовых
данных, плеер промежуточные media не создаёт. Первый fake SDK-тест дал
exit 1 из-за отсутствующего SHA-256 в имитации capabilities; добавлено
реальное требование integrity, финальные 30 SDK v2 тестов прошли. Исходные
коды сохранены в JSON. После Qt-прогонов были сообщения teardown о закрытии
частного D-Bus и QEventDispatcherWin32::wakeUp. Процессы завершились с
exit 0; наличие этих сообщений также записано в JSON.

## Установка и границы

Linux EXE заменён атомарно, backup — `foxicmp-qt.before-sdk-1.44-20261031T072025Z`. Сохранены 202
сопутствующих файлов. SHA-256 текущего INI до/после совпадает; версия и
capabilities установленного EXE сверены с проверенной сборкой. Просмотр
не прерывался.

Установленные Windows EXE/INI не заменялись. Classic тестировался с частным
INI, ContextMenuPrompted=1 и ContextMenuEnabled=0, с проверкой неизменности
HKCU shell commands/icons до/после. Одноразовая задача и staging удалены;
SHA tested EXE совпадает с итоговым build EXE.

[Полные результаты, exit codes и SHA-256](ProgressiveMuxedWebmAv1Validation-2026-10-31.json).
Этот этап не подтверждает shared VP8/Vorbis, multi-audio, live/config changes,
реальные сетевые источники, многочасовую физическую A/V-приёмку, hardware
или Wayland. Существующие ограничения shared WebM сохранены. SDK ZIP
содержит Python-клиент, examples и документацию, без Windows EXE/установщиков.
