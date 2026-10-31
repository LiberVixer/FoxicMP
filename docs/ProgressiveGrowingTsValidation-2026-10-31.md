# Проверка одного растущего TS — 31 октября 2026

SDK 1.41 добавляет [growing TS VOD](ProgressiveGrowingTs.md) в Classic,
Windows Qt и Linux Qt. Один файл с AVC/AAC дописывается без сегментации;
декодеры получают два виртуальных MP4 view. Версия приложения — 0.2.0.
YouTubeHarvester не изменён.

| Среда | Проверка | Финальный exit code |
|---|---|---|
| Linux Qt / GCC, частные X11/D-Bus/PulseAudio | 3 новых playback-сценария + 5 прежних | 0 |
| Windows Qt / MinGW, offscreen | Те же 3 + 5 сценариев | 0 |
| Windows Classic / MSVC, отдельная безопасная копия | 3 новых сценария + прежний v1 directory/API | 0 |
| Linux GCC native | 17 growing TS проверок | 0 |
| Windows MinGW native | Те же 17 проверок | 0 |
| Windows MSVC native | Те же 17 проверок | 0 |
| Linux ASan/UBSan с leak detection | Те же 17, без ошибок sanitizers | 0 |
| Python SDK v1/v2, family contract | 15 + 27 + 2 теста | 0 |

## Реальное воспроизведение

По три новых сценария на редакцию, всего девять:
Main 25 fps/B-frames + stereo AAC 48 kHz; High 30000/1001 fps + stereo AAC
44.1 kHz с неизвестной длиной файла; Main с audio preroll 10 ms.

Проверены initial prefix из 17 байт и `waiting_initialization`, недостаточный
буфер, декодирование до complete, pause/update без самопроизвольного resume,
backward seek в паузе, resume, ожидание safe tail без EOF, forward seek после
дозагрузки, natural EOF/release и неизменный reader ID. Qt дополнительно
проверяет меняющиеся framebuffer hashes, H.264/yuv420p и AAC sampling
rate/channels. Classic проверяет настоящий DirectShow graph, ход позиции и
decoder I/O обеих дорожек; его pixels/физический аудиовыход не захватывались.

Прежние v1 writer, отдельный TS, совместные TS-сегменты, fMP4 H.264/AAC и
WebM VP9/Opus проверены на обеих Qt-сборках. Classic отдельно прошёл прежний
v1 directory/API сценарий. Native compatibility охватывает 12 suites на ОС.

## Байты, временные метки и ошибки

Каждый из четырёх native toolchain прогонов сравнивает 872 access units
(100+189, 120+174, 100+189), включая SHA каждого encoded payload.
Публикации заканчиваются посреди PSI/PES header, TS-пакета и GOP. Проверены
неизменность прежнего virtual MP4 prefix, FragmentIndex, backward bytes,
неизвестная длина, общий physical file/lease и release последнего владельца.

После FFmpeg-декодирования video/PCM hashes совпадают с исходным continuous
TS. Для preroll reference PCM обрезается на 480 samples. Максимальное
фактическое отличие PTS/DTS от ffprobe — 120 × 100 ns (12 µs), на AAC
44.1 kHz. Это сравнение media timestamps, не физическая A/V sync.

Ошибки нового audio PES, continuity и SDT CRC закрывают обе проекции,
сохраняя прежнюю revision/index только для диагностики. Неполный последний
пакет ждёт при downloading, но отвергается при явном complete. Проверены SHA
rewrite, усечение, cancellation, отсутствие частично выданных bytes и утечек
leases. Ошибка metadata до admission сохраняет рабочий старый сеанс.

## Исправления проверки и точные коды

Первый Classic-прогон завершился с кодом 1: тест требовал position ≥12 s,
хотя Classic штатно оставляет около секунды до safe tail; при available
12.941 s позиция была 11.950 s. Проверка теперь требует близость к фактически
доступному концу, без изменения поведения плеера. Повтор прошёл с кодом 0.

Первое совместное выполнение старых Qt-сценариев обнаружило одинаковые
временные имена у separate/muxed TS: второй сценарий читал старый control
файл. Каталоги теперь включают media mode. Повторные новые и compatibility
сценарии обеих Qt-сборок прошли полностью: 5/5 и 7/7 QTest checks соответственно.

Первый Linux native CTest дал код 8: 11/12 suites прошли, WebM не нашёл fixture
из-за отсутствующего абсолютного `FOXICMP_TEST_MEDIA`. Повторена только эта
suite с правильным environment, код 0. Изначальный код 8 сохранён в JSON;
все 12 suites успешно проверены совокупностью исходного и целевого прогонов.
Windows native CTest сразу прошёл 12/12, код 0.

## Установка и границы

Linux EXE заменён атомарно с backup
`foxicmp-qt.before-sdk-1.41-20261031T040226Z`. Сохранены 199 сопутствующих
файлов; текущий INI до/после совпадает по SHA-256. Версия и capability
установленной копии сверены с проверенной сборкой. Просмотр не прерывался.

Установленные Windows-приложения не заменялись. Classic тестировался с
частным INI, `ContextMenuPrompted=1` и `ContextMenuEnabled=0`; команды/иконки
его установленного HKCU context menu сверены до/после. Обе одноразовые задачи
и staging-копии, включая первый неудачный прогон, удалены. Итоговый tested
EXE совпадает по SHA-256 с build EXE.

[Полные результаты, точные коды и SHA-256](ProgressiveGrowingTsValidation-2026-10-31.json).
Muxed fMP4/WebM, live/config changes, произвольные TS-кодеки/программы, реальные
сайты, многочасовая/физическая A/V-приёмка, предельный размер индекса, hardware
и Wayland этим этапом не проверены и не объявляются готовыми.
