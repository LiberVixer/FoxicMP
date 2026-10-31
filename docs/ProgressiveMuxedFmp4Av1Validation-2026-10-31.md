# Shared AV1/AAC-LC fMP4: проверка SDK 1.48

Дата: 31 октября 2026. Приложение 0.2.0; протокол v1 и schemaVersion 2
сохранены. [Контракт](ProgressiveMuxedFmp4Av1.md),
[полный machine-readable отчёт](ProgressiveMuxedFmp4Av1Validation-2026-10-31.json).

## Реальное воспроизведение

| Редакция | Main 8-bit, 25 fps | Main 10-bit, 25 fps | Main 10-bit, 30000/1001 |
| --- | --- | --- | --- |
| Linux Qt | PASS | PASS, yuv420p10le | PASS, yuv420p10le |
| Windows Qt | PASS | PASS, yuv420p10le | PASS, yuv420p10le |
| Windows Classic | PASS | PASS | PASS |

Девять сценариев используют один растущий physical resource:
partial initialization → buffering → дополнительные подтверждённые bytes →
playing до complete → pause → update без снятия pause → backward seek →
resume → ожидание неполного tail → complete → forward seek → natural EOF →
last-owner release. Reader identity сохраняется; seek проверяется на 2/17 s.
Контролируются decoder I/O, state и отсутствие преждевременного EOF.

Обе Qt-сборки дополнительно проверяют меняющиеся кадры, фактический AV1
video-format, 10-bit decoder pixel format, AAC stereo и sample rate:
48 kHz в первых двух profiles, 44.1 kHz в fractional profile. Для него длина
ресурса первоначально неизвестна. QTest: по 5 passed, 0 failed, CTest exit 0.
Classic подтверждает работающий граф и decoder I/O; отдельного захвата его
пикселей/физического звука этот прогон не делает.

## Native и ошибки

По 35 проверок проходят GCC/Linux, MinGW/Windows, MSVC/Windows и
GCC+ASan/UBSan/Linux с leak detection. Каждый прогон сравнивает 6623 access
units в четырёх реальных FFmpeg fixtures: три 4-секундных и Main 8-bit
80 секунд. Packet SHA совпадают, максимальное расхождение PTS/DTS — 0 единиц
100 ns; decoded video и PCM SHA совпадают. Main10 сравнивается в yuv420p10le,
без снижения глубины. Fixtures используют delay_moov и lag-in-frames 0.
Все эти прогоны завершаются с exit 0.

Проверены partial moof/mdat, arbitrary byte cuts, immutable prefix,
backward reads, один physical resource/lease и закрытие последнего владельца.
Отвергаются несовпадение actual codec/hint, повреждённые av1C marker/version,
profile/level/depth/chroma, presentation delay, неправильные OBU flags/type,
varint/size, отсутствие Sequence Header и несовпадение sequence profile.
Отклонённая metadata сохраняет прежний index/bytes. SHA rewrite и cancel
закрывают обе проекции; incomplete initialization освобождает временный reader.

## Совместимость и изоляция

- Python: v1 SDK 15, v2 SDK 34, family contract 2 — всего 51, exit 0.
  Первый пробный v2 прогон вернул exit 1 из-за опечатки при редактировании
  negative test: неподдержанный WebM AV1 заменили уже поддержанным VP9.
  Исходный negative case восстановлен; окончательный набор прошёл полностью.
- Linux/Windows: четыре native suites — media, новый AV1 fMP4, прежние
  HEVC fMP4 и AVC fMP4. Итоговые CTest exit 0.
- Обе Qt-сборки: v1 writer, growing TS, shared AVC fMP4, shared HEVC Main10
  fractional fMP4, shared VP8/VP9/AV1 Opus WebM и optional Vorbis;
  по 10 QTest passed, 0 failed, exit 0.
- MSVC: дополнительно 22 HEVC и 21 AVC shared-fMP4 проверки;
  ASan/UBSan: дополнительно 21 AVC проверка, exit 0.
- Classic: дополнительно три AVC, три HEVC, три VP8/VP9/AV1 Opus WebM
  сценария и v1 sessionDirectory. У тестовой копии был собственный INI с
  ContextMenuPrompted=1 и ContextMenuEnabled=0; installed shell commands/icons
  до/после совпали.
- Одноразовая задача Classic и staged copy удалены после проверки совпадения
  SHA тестированного и собранного EXE. Установленные Windows EXE/INI
  не заменялись. YouTubeHarvester не менялся.
- Linux EXE обновлён атомарно; прежний сохранён для отката. SHA профиля до/после
  совпадает, 206 остальных файлов сверены, просмотр ради замены не останавливался.

Предыдущие частные AV1/WebM artifacts перенесены в архив перед запуском
новых проверок. Отчёт принимает только логи и exit files новее текущего
изменения исходников: старые результаты не засчитываются. Точные exit codes,
SHA исходников/сборок/профиля, backup path и предупреждения teardown — в JSON.
Qt Win32 wakeUp и private D-Bus предупреждения не подменяют результаты тестов:
учитываются фактический exit процесса и QTest assertions/totals.

## Оставшиеся границы

Это ограниченный known-duration VOD profile с двумя required scalar tracks,
нулевым offset и неизменными head moov/configuration. Нет VP9/Opus в shared
fMP4, произвольного CMAF, live, обычного MP4 с moov в хвосте или multi-audio.
Максимальный index/memory/throughput и многочасовые нагрузки не измерены.
Сравнение timestamps/decoded hashes **не измеряет** физическую A/V sync.
Реальные сайты и остальная приёмка остаются в [чеклисте](ProgressiveCompletionChecklist.md).
SDK ZIP содержит клиент, примеры и документацию; новые EXE собираются из тега.
