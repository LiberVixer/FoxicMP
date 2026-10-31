# Shared VP9/AAC-LC fMP4: проверка SDK 1.49

Дата: 31 октября 2026. Приложение 0.2.0; протокол v1 и schemaVersion 2
сохранены. [Контракт](ProgressiveMuxedFmp4Vp9.md),
[полный machine-readable отчёт](ProgressiveMuxedFmp4Vp9Validation-2026-10-31.json).

## Реальное воспроизведение

| Редакция | Profile 0, 8-bit, 25 fps | Profile 2, 10-bit, 25 fps | Profile 2, 10-bit, 30000/1001 |
| --- | --- | --- | --- |
| Linux Qt | PASS | PASS | PASS |
| Windows Qt | PASS | PASS | PASS |
| Windows Classic | PASS | PASS | PASS |

Девять сценариев используют один растущий physical resource:
partial initialization → buffering → дополнительные подтверждённые bytes →
playing до complete → pause → update без снятия pause → backward seek →
resume → ожидание неполного tail → complete → forward seek → natural EOF →
last-owner release. Reader identity сохраняется; seek проверяется на 2/17 s.
Контролируются decoder I/O, state и отсутствие преждевременного EOF.

Обе Qt-сборки проверяют меняющиеся кадры, фактический VP9 video-format,
10-bit decoder pixel format, AAC stereo и sample rate: 48 kHz в первых двух
profiles, 44.1 kHz в fractional profile. Для него длина ресурса первоначально
неизвестна. QTest: по 5 passed, 0 failed, CTest exit 0.
В логах libmpv pixel format называется `yuv420p`/`yuv420p10`; в native
сравнении FFmpeg используется raw `yuv420p`/`yuv420p10le`. Отчёт JSON берёт
Qt-названия из наблюдаемых логов.
Classic подтверждает работающий граф и decoder I/O; отдельного захвата его
пикселей/физического звука этот прогон не делает.

## Native и ошибки

По 34 проверки проходят GCC/Linux, MinGW/Windows, MSVC/Windows и
GCC+ASan/UBSan/Linux с leak detection. Каждый прогон сравнивает 6623 access
units в четырёх реальных FFmpeg fixtures: три 4-секундных и Profile 0
80 секунд. Packet SHA совпадают, максимальное расхождение PTS/DTS — 0 единиц
100 ns; decoded video и PCM SHA совпадают. 10-bit сравнивается в yuv420p10le,
без снижения глубины. Fixtures используют delay_moov, lag-in-frames 25 и
auto-alt-ref 1. Все эти прогоны завершаются с exit 0.

Проверены partial moof/mdat, arbitrary byte cuts, immutable prefix,
backward reads, один physical resource/lease и закрытие последнего владельца.
Отвергаются несовпадение actual codec/hint, отсутствие/обрезанный vpcC,
неправильные version/flags, Profile 1/3, invalid level, depth mismatch,
12-bit, 4:2:2/4:4:4/reserved chroma, reserved primaries/transfer/matrix,
RGB matrix при 4:2:0 и непустая initialization data.
Отклонённая metadata сохраняет прежний index/bytes. SHA rewrite и cancel
закрывают обе проекции; incomplete initialization освобождает временный reader.

## Совместимость и изоляция

- Python: v1 SDK 15, v2 SDK 35, family contract 2 — всего 52, exit 0.
- Linux/Windows: пять native suites — media, новый VP9 fMP4, прежние
  AV1, HEVC и AVC fMP4. Итоговые CTest exit 0.
- Обе Qt-сборки: v1 writer, growing TS, shared AVC fMP4, shared HEVC/AV1
  Main10 fractional fMP4, shared VP8/VP9/AV1 Opus WebM и optional Vorbis;
  по 11 QTest passed, 0 failed, exit 0.
- MSVC: дополнительно 35 AV1, 22 HEVC и 21 AVC shared-fMP4 проверки;
  ASan/UBSan: дополнительно 21 AVC проверка, exit 0.
- Classic: дополнительно три AVC, три HEVC, три AV1, три VP8/VP9/AV1 Opus
  WebM сценария и v1 sessionDirectory. У тестовой копии был свой INI с
  ContextMenuPrompted=1 и ContextMenuEnabled=0; installed shell commands/icons
  до/после совпали.
- Одноразовая задача Classic и staged copy удалены после проверки совпадения
  SHA тестированного и собранного EXE. Установленные Windows EXE/INI
  не заменялись. YouTubeHarvester не менялся.
- Linux EXE обновлён атомарно; прежний сохранён для отката. SHA профиля до/после
  совпадает, 207 остальных файлов сверены, просмотр ради замены не останавливался.

Первый preflight установки вернул exit 1 до изменения файлов: timestamp раннего
SDK exit record на shared filesystem оказался примерно на семь секунд старше
времени начала прогона. Защита отказала в замене; 35 SDK tests повторены с exit 0,
после чего установка завершилась с exit 0. Эта история сохранена в JSON.

Тестовые artifacts используют отдельный VP9/fMP4 namespace; отчёт принимает
только логи и exit files новее текущего изменения исходников. Точные exit
codes, SHA исходников/сборок/профиля, backup path и предупреждения teardown —
в JSON. Qt Win32 wakeUp и private D-Bus предупреждения не подменяют результаты
тестов: учитываются фактический exit процесса и QTest assertions/totals.

## Оставшиеся границы

Это ограниченный known-duration VOD profile с двумя required scalar tracks,
нулевым offset и неизменными head moov/configuration. Нет Opus в shared
fMP4, произвольного CMAF, live, обычного MP4 с moov в хвосте или multi-audio.
Максимальный index/memory/throughput и многочасовые нагрузки не измерены.
Сравнение timestamps/decoded hashes **не измеряет** физическую A/V sync.
Реальные сайты и остальная приёмка остаются в [чеклисте](ProgressiveCompletionChecklist.md).
SDK ZIP содержит клиент, примеры и документацию; новые EXE собираются из тега.
