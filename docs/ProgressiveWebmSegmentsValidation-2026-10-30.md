# Проверка SDK 1.38 — 30 октября 2026

WebM whole-Cluster + Opus подключён к Windows Classic, Windows Qt и Linux Qt.
24 сценария реального playback: в каждой редакции пять inline-профилей
(VP8, VP9 8/10-bit, AV1 Main 8/10-bit) и три каталога (VP8, VP9 10-bit,
AV1 10-bit). Фикстуры 20 секунд, 43 ресурса; audio UI — stereo 48 kHz.

Проверены дырка подтверждённого префикса, запуск до complete, pause/update,
перемотка назад, ожидание хвоста вместо EOF, explicit complete и release.
Для каталога дополнительно: отсутствующая новая страница, повтор той же ревизии,
сохранение старого состояния, seek к 14 секундам и обратно к 2 секундам.
Qt проверяет меняющиеся кадры реального framebuffer, codec, pixel format,
10-bit и audio channels/rate. Classic использует настоящий Matroska/DirectShow
граф и опубликованные главы; это не пиксельное измерение Classic.

На обеих ОС совпали 100 native WebM-проверок: byte boundaries, append/EOF,
leases, init-only start, несколько Cluster в одном media, backward catalog reads,
восемь открытых файлов, отклонение неверных envelope/codec/clock/init,
нецелых/unknown-size Cluster, отсутствующего keyframe и configuration changes.
Linux: 95 основных + пять добавленных multi-Cluster проверок; Windows: 100 вместе.

Регрессия каждой ОС: 295 прежних fMP4 segment checks, 22 fMP4 catalog cases,
468 scalar media cases, 18 diagnostic staging checks. По четыре Qt compatibility
функции (v1, scalar VP9/Opus, segmented H.264/AAC и VP9/Opus), Classic v1
sessionDirectory, SDK v2 24 теста, SDK v1 15 тестов, family contract 2 теста.
Итоговые изменённые сценарии завершились с exit code 0.

Первый MSVC build завершился 1 из-за типа аргумента RapidJSON; исправлен и
пересобран. Первый Linux CTest завершился 8: четыре прежние группы прошли,
новый тест повреждённого завершённого init успел запуститься до окончания
пересборки. После пересборки все WebM-проверки прошли с кодом 0. Точные коды,
фазы, хеши и границы проверки сохранены в [JSON](ProgressiveWebmSegmentsValidation-2026-10-30.json).

Linux UI/audio проверены через частные X11/D-Bus/PulseAudio. Classic тестировался
в отдельной копии с безопасными ContextMenu flags; HKCU registration сравнивалась
до/после. Одноразовая Windows-задача и staging удалены. Установленные Windows
плееры и Harvester не менялись. Linux EXE заменён атомарно с резервной копией;
INI и все прежние личные файлы сверены по SHA-256 и сохранены.

Границы: нет физического измерения A/V sync или многочасовой приёмки WebM;
mono не был отдельной UI-фикстурой этого прогона. Segmented Vorbis, TS, muxed,
live/config changes и реальные источники остаются отдельными этапами.
