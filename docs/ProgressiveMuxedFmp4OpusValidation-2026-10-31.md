# Shared fMP4 Opus: проверка SDK 1.50

Дата: 31 октября 2026. Приложение 0.2.0; протокол v1 и schemaVersion 2
сохранены. [Контракт](ProgressiveMuxedFmp4Opus.md),
[полный machine-readable отчёт](ProgressiveMuxedFmp4OpusValidation-2026-10-31.json).

## Реальное воспроизведение

| Пара | Profiles в каждой редакции | Linux Qt | Windows Qt | Classic |
| --- | --- | --- | --- | --- |
| H.264/Opus | stereo 25 fps; mono 30000/1001 | PASS | PASS | PASS |
| HEVC/Opus | Main8 hvc1 stereo 25 fps; Main10 hev1 mono 30000/1001 | PASS | PASS | PASS |
| AV1/Opus | Main8 stereo 25 fps; Main10 mono 30000/1001 | PASS | PASS | PASS |
| VP9/Opus | Profile0 stereo 25 fps; Profile2 mono 30000/1001 | PASS | PASS | PASS |

Всего 24 сценария. Каждый использует один растущий physical resource:
partial initialization → buffering → дополнительные подтверждённые bytes →
playing до complete → pause → update без снятия pause → backward seek →
resume → ожидание неполного tail → complete → forward seek → natural EOF →
last-owner release. Reader identity сохраняется; seek проверяется на 2/17 s.
Контролируются decoder I/O, state и отсутствие преждевременного EOF.
Длина ресурса fractional profiles первоначально неизвестна.

Обе Qt-сборки проверяют меняющиеся кадры, фактические video codecs/depth,
Opus 48 kHz и чередование stereo/mono. QTest: по 10 passed, 0 failed,
CTest exit 0. Имена pixel formats и аудиопараметры в JSON взяты из логов,
а не подставлены из заявленной capability. Native raw 10-bit сравнивается
в yuv420p10le.
Classic подтверждает работающий граф и decoder I/O; отдельного захвата его
пикселей/физического звука этот прогон не делает.

## Native и ошибки

По 41 проверке проходят GCC/Linux, MinGW/Windows, MSVC/Windows и
GCC+ASan/UBSan/Linux с leak detection. Каждый прогон сравнивает 8489 access
units в девяти реальных FFmpeg fixtures: восемь 4-секундных profiles и
H.264/Opus 80 секунд. Packet SHA совпадают, максимальное расхождение PTS/DTS —
0 единиц 100 ns; decoded video и PCM SHA совпадают. Mono/stereo используют
разные входные сигналы; stereo имеет разные частоты в двух каналах.
Все эти прогоны завершаются с exit 0.

Проверены partial moof/mdat, arbitrary byte cuts, immutable prefix,
backward reads, один physical resource/lease и закрытие последнего владельца.
Отвергаются ложные AAC/Opus hints, Opus без явного video hint, неправильные
dOps version/channels/mapping/length, неверные sample-entry channels/size/rate,
не-48-kHz mdhd, отсутствие edit, несовпадение media edit и pre-skip, non-unit rate.
Отдельные положительные проверки сохраняют согласованный pre-skip 0/480,
output gain и информационный input rate 24 kHz; это проверка bytes/header,
а не измерение слышимого gain. Default fixtures используют pre-skip 312.

Отклонённая смена audio codec hint сохраняет прежние index/bytes.
SHA rewrite и cancel закрывают обе проекции; incomplete initialization
освобождает временный reader. Encoder fixtures используют delay_moov,
20-ms Opus packets и исходные отрицательные timestamps; принудительное
смещение до нуля отключено только в генераторе Opus fixtures.

## Совместимость и изоляция

- Python: v1 SDK 15, v2 SDK 36, family contract 2 — всего 53, exit 0.
  Новая проверка проходит exact pair/mode negotiation, отсутствие наследования
  от separate/WebM/AAC, обязательный video hint и сохранение default AAC.
- Linux/Windows: шесть native suites — media, новые Opus пары и прежние
  VP9, AV1, HEVC, AVC shared fMP4. Итоговые CTest exit 0.
- Обе Qt-сборки: v1 writer, growing TS, четыре shared AAC video profiles,
  shared VP8/VP9/AV1 Opus WebM и optional Vorbis;
  по 12 QTest passed, 0 failed, exit 0.
- MSVC: дополнительно 34 VP9, 35 AV1, 22 HEVC и 21 AVC shared-fMP4 проверки;
  ASan/UBSan: дополнительно 21 AVC проверка, exit 0.
- Classic: дополнительно четыре AAC profiles (AVC, HEVC Main10, AV1 Main10,
  VP9 Profile2) и v1 sessionDirectory. У тестовой копии был свой INI с
  ContextMenuPrompted=1 и ContextMenuEnabled=0; installed shell commands/icons
  до/после совпали.
- Одноразовая задача Classic и staged copy удалены после проверки совпадения
  SHA тестированного и собранного EXE. Установленные Windows EXE/INI
  не заменялись. YouTubeHarvester не менялся.
- Linux EXE обновлён атомарно; прежний сохранён для отката. SHA профиля до/после
  совпадает, 208 остальных файлов сверены, просмотр ради замены не останавливался.

Artifacts используют отдельный Opus/fMP4 namespace. Отчёт принимает только
логи и exit files новее изменения текущего исходного profile header.
Точные exit codes, SHA исходников/сборок/профиля, backup path и предупреждения
teardown — в JSON. Qt Win32 wakeUp и private D-Bus предупреждения не подменяют
результаты тестов: учитываются фактический exit процесса и QTest assertions/totals.

## Оставшиеся границы

Закрыта ограниченная матрица общего растущего fMP4 с AAC/Opus и четырьмя
видеокодеками. Known-duration zero-offset VOD, два required scalar tracks,
неизменный head moov/configuration и полный moof/mdat остаются обязательными.
Нет universal demux/remux, произвольного CMAF, live, обычного MP4 с moov в хвосте,
multi-audio, дополнительных layouts/rates. Предельные index/memory/throughput
и многочасовые нагрузки не измерены. Сравнение timestamps/decoded hashes
**не измеряет** физическую A/V sync. Реальные сайты и остальная приёмка
остаются в [чеклисте](ProgressiveCompletionChecklist.md). SDK ZIP содержит
клиент, примеры и документацию; новые EXE собираются из тега.
