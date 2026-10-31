# Проверка shared fMP4 — 31 октября 2026

SDK 1.42 добавляет [один растущий fMP4 AVC/AAC](ProgressiveMuxedFmp4.md) в
Classic, Windows Qt и Linux Qt. Два virtual moov/traf view читают общий mdat
через ResourceSource. Версия приложения — 0.2.0. Harvester не изменён.

| Среда | Проверка | Финальный exit code |
|---|---|---|
| Linux Qt / GCC, private X11/D-Bus/PulseAudio | 3 новых + 6 прежних runtime-сценариев | 0 |
| Windows Qt / MinGW, offscreen | Те же 3 + 6 сценариев | 0 |
| Windows Classic / MSVC, отдельная безопасная копия | 3 новых + прежний v1 directory/API | 0 |
| Linux GCC native | 21 новый сценарий | 0 |
| Windows MinGW native | Те же 21 сценарий | 0 |
| Windows MSVC native | Те же 21 сценарий с OS replacement protection | 0 |
| Linux ASan/UBSan, leak detection | Те же 21, без ошибок sanitizers | 0 |
| Python SDK v1/v2, family contract | 15 + 28 + 2 теста | 0 |
| Native compatibility, Linux / Windows | По 13 suites | 0 / 0 |

## Playback и bytes

По три новых runtime-сценария на редакцию, всего девять: Main 25 fps/B-frames
+ stereo AAC 48 kHz; High 30000/1001 fps + stereo AAC 44.1 kHz с неизвестной
длиной resource; Main с delayed moov и двумя edit lists.

Проверены partial initial header и `waiting_initialization`, недостаточный
буфер, игра до complete, pause/update, backward seek в паузе, resume,
ожидание конца полного fragment без EOF, forward seek после дозагрузки,
natural EOF/release и постоянный reader ID. Qt дополнительно проверяет
changing framebuffer hashes, H.264/yuv420p, decoded AAC rate/channels.
Classic проверяет настоящий граф, продвижение позиции и decoder I/O
обеих дорожек; отдельный capture pixels/физического аудио не проводился.

Каждый native toolchain сравнивает 6623 access units: 100+189, 120+174,
100+189 и 2000+3751. Последний вариант длится 80 секунд. Проверяются SHA
всех packet payload, FragmentIndex, immutable virtual prefix, partial
moof/mdat, arbitrary byte cuts, backward reads, complete/EOF и один shared
physical lease до release последнего владельца.

Virtual fMP4 после FFmpeg-декодирования даёт те же video/PCM hashes, что
исходный muxed MP4. PTS/DTS после пересчёта сохранённых media timebase
совпадают: максимальная измеренная разница — 0 × 100 ns. Проверены signed
composition offset B-кадров и сохранение edit lists; это не измерение
физической синхронизации выхода.

## Отказ и совместимость

Ошибки mfhd sequence, неизвестный track ID, data_offset за mdat и пересечение
audio/video payload закрывают обе проекции без публикации частичного
кандидата. Внешние dref, непустые nonfragmented sample tables и неверный
actual codec отвергаются. Проверены SHA rewrite, усечение, cancellation,
неполный mdat при explicit complete, сохранность старого сеанса при
ошибке metadata и отсутствие leases после неуспешного constructor/release.

Первый Windows MSVC fault-прогон дал exit 1: тест пытался заменить файл,
который ОС запрещает заменять, пока он pinned. Уточнён тест: Windows должна
сохранить исходное чтение при запрещённой замене, а после release разрешить
замену. Linux допускает подмену, затем reader обнаруживает новый file ID и
закрывает обе проекции. Итоговые GCC/MinGW/MSVC/ASan прогоны прошли; исходный
код 1 сохранён в JSON. Защита Windows не ослаблялась.

Ранний fault fixture заканчивался за байт до первого полного mdat, поэтому
constructor корректно ждал initialization вместо открытия здорового
baseline. Fixture исправлен: один полный fragment и байт следующего moof.

На обеих Qt-сборках прошли прежние v1 writer, separate TS, muxed TS segments,
growing TS, separate fMP4 H.264/AAC и WebM VP9/Opus — 8/8 QTest checks с
init/cleanup. Новые Qt-сценарии — 5/5 с init/cleanup. Native compatibility
прошла 13/13 на каждой ОС, включая старые fMP4/WebM/TS и catalog/staging.
Classic отдельно проверен прежним v1 directory/API.

## Установка и границы

Linux EXE заменён атомарно с backup
`foxicmp-qt.before-sdk-1.42-20261031T043355Z`. Сохранены 200 сопутствующих
файлов. SHA-256 текущего INI до/после совпадает; версия и capability
установленного EXE сверены с проверенной сборкой. Просмотр не прерывался.

Установленные Windows EXE/INI не заменялись. Classic тестировался с частным
INI, `ContextMenuPrompted=1` и `ContextMenuEnabled=0`; его установленные HKCU
shell commands/icons сверены до/после. Одноразовая задача и staging-копия
удалены. Tested Classic EXE совпадает по SHA-256 с итоговым build EXE.

[Полные результаты, exit codes и SHA-256](ProgressiveMuxedFmp4Validation-2026-10-31.json).
Muxed WebM, дополнительные кодеки muxed MP4, произвольный CMAF/HLS,
live/config changes, реальные сайты, многочасовая физическая A/V-приёмка,
предельная ёмкость индекса, hardware и Wayland этим этапом не проверены.
Whole mdat сохраняется в каждом virtual view: возможны лишние prefetch reads;
максимальная память/производительность отдельно не измерены.
