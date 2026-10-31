# Shared HEVC/AAC-LC fMP4: проверка SDK 1.47

Дата: 31 октября 2026. Приложение 0.2.0; протокол v1 и schemaVersion 2
сохранены. [Контракт](ProgressiveMuxedFmp4Hevc.md),
[полный machine-readable отчёт](ProgressiveMuxedFmp4HevcValidation-2026-10-31.json).

## Реальное воспроизведение

| Редакция | Main/hvc1, 25 fps | Main10/hev1, 30000/1001 | Main/hev1, delayed moov/edit list |
| --- | --- | --- | --- |
| Linux Qt | PASS | PASS, yuv420p10le | PASS |
| Windows Qt | PASS | PASS, yuv420p10le | PASS |
| Windows Classic | PASS | PASS | PASS |

Каждый из девяти сценариев использует один растущий physical resource:
partial initialization → buffering → публикация дополнительных bytes →
playing до complete → pause → update без снятия pause → backward seek →
resume → ожидание неполного tail → complete → forward seek → natural EOF →
last-owner release. Reader identity сохраняется; перемотка идёт в опубликованные
2/17 секунд. Проверяются decoder I/O и отсутствие ошибочного EOF.

Обе Qt-сборки дополнительно проверяют фактически меняющиеся кадры,
`video-format`, глубину Main10 и AAC stereo: 48 kHz либо 44.1 kHz для
fractional profile. QTest: по 5 passed, 0 failed, CTest exit 0.
Classic подтверждает работающий граф и decoder I/O; отдельного захвата его
пикселей/физического звука в этом прогоне нет.

## Native и ошибки

По 22 проверки проходят GCC/Linux, MinGW/Windows, MSVC/Windows и
GCC+ASan/UBSan/Linux с leak detection. Каждый прогон сравнивает 6623 access
units в четырёх реальных FFmpeg fixtures: три 4-секундных и Main 80 секунд.
Packet SHA совпадают, максимальное расхождение PTS/DTS — 0 единиц 100 ns;
decoded video и PCM SHA совпадают. Main10 сравнивается в 10-bit формате,
без снижения глубины. Санитайзеры завершаются с exit 0.

Проверяются partial moof/mdat, произвольные cuts, immutable virtual prefix,
backward reads, один lease и его закрытие после последнего владельца.
Несовпадение actual codec и hint, неправильный hvcC version/profile/NAL length,
chroma/depth/array type, неполные hvc1 parameter arrays отвергаются.
Отклонённая смена codec hint не повреждает ранее опубликованный index/bytes.

Первый Linux native и первый sanitizer прогоны вернули exit 1: Linux FFmpeg
добавляет в delayed-moov HEVC пустую `sdtp`, которой нет в Windows fixture.
Разрешён только вариант без entries с version/flags 0; непустая/некорректная
таблица по-прежнему отвергается. После исправления пересобраны все редакции
и native runners. Начатый общий Linux native batch был прерван без сохранённого
финального exit code; batch повторён, с результатами четырёх успешных suites и ошибкой пути fixtures
в пятой. Эта ошибка настройки запуска описана ниже и проверена повторно.

## Совместимость и изоляция

- Python: v1 SDK 15, v2 SDK 33, family contract 2 — всего 50, exit 0.
- Windows: пять native suites — media, segment WebM, shared AVC fMP4, shared
  WebM и новый HEVC fMP4 — CTest exit 0. В Linux четыре suites прошли; segment
  WebM завершился FileNotFoundError из-за относительного пути fixtures, общий
  CTest exit 8. Только этот тест повторён с абсолютным `FOXICMP_TEST_MEDIA`: exit 0.
  Успешные четыре suites не повторялись после исправления команды запуска.
- Обе Qt-сборки: v1 writer, growing TS, shared AVC fMP4, shared VP8/VP9/AV1
  Opus WebM, optional Vorbis; по 9 QTest passed, 0 failed.
- MSVC: дополнительно 21 прежняя AVC fMP4 и 6 Opus compatibility checks;
  ASan/UBSan: дополнительно 21 прежняя AVC fMP4 проверка, exit 0.
- Classic: дополнительно три AVC fMP4, три VP8/VP9/AV1 Opus WebM сценария и
  v1 sessionDirectory. Тестовая копия имела свой INI с отключённой регистрацией;
  shell commands/icons установленного Classic до/после совпали.
- Одноразовая тестовая задача Classic и staged copy удалены после проверки
  совпадения SHA тестированного и собранного EXE. Установленные Windows EXE/INI
  не заменялись. YouTubeHarvester не менялся.
- Linux EXE обновлён атомарно после проверок, прежний сохранён для отката.
  SHA профиля до/после совпадает; остальные личные файлы сверены.

Точные exit codes, SHA сборок/исходников/профиля, backup path и наблюдаемые
предупреждения teardown записаны в JSON. Предупреждения Qt Win32 wakeUp и
частного D-Bus не считаются самостоятельным доказательством успеха: учитываются
утверждения тестов, QTest totals и фактический exit процесса.

## Оставшиеся границы

Это ограниченный known-duration VOD profile с двумя required scalar tracks,
нулевым offset и неизменным head moov/configuration. Нет AV1/VP9/Opus в shared
fMP4, произвольного CMAF, live, обычного MP4 с moov в хвосте или multi-audio.
Максимальный index/memory/throughput и многочасовые нагрузки не измерены.
Совпадение packet clock и decoded hashes **не измеряет** физическую A/V sync.
Реальные YouTube/VK/Rutube и остальная приёмка остаются в
[чеклисте](ProgressiveCompletionChecklist.md). SDK ZIP содержит клиент,
примеры и документацию; новые EXE собираются из соответствующего тега.
