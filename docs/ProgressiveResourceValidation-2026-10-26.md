# I/O ресурсов v2: результаты проверки

26 октября 2026. [Exit codes, SHA-256 и первичные сбои](ProgressiveResourceValidation-2026-10-26.json).
[Контракт SDK 1.5](ProgressiveResourceIo.md), [общий план](ProgressiveIntegrationPlan.md).
Проверено чтение локальных подтверждённых байтов; decoder/remux v2 не подключены.

| Проверка | Exit code | Результат |
|---|---|---|
| Linux Qt, Windows Qt, Classic | 0 каждый | Три сборки приложения |
| Native ResourceSource, GCC/Linux | 0 | 24 сценария |
| Native ResourceSource, MinGW/Windows и MSVC/Windows | 0 каждый | По 22 сценария; symlink cases POSIX отдельно на Linux |
| Native I/O и асинхронный worker, ASan/UBSan | 0 | 24 сценария, detect_leaks=1 |
| Linux Qt, private X11/D-Bus/PulseAudio | 0 | 4 CTest-группы; API 19 passed, 0 failed/skipped |
| Windows Qt, native/offscreen, частные профили | 0 | 4 CTest-группы; API 20 passed, 0 failed/skipped |
| Classic, отдельная копия и одноразовая задача | 0 | 11 I/O API-сценариев при активном v1 playback, reader contract, 62 validate, 100 RPC и SDK/writer |
| Python SDK | 0 | 20 тестов: прежние 12 и 8 ResourceProbe |
| Семейный контракт | 0 | 2 теста |

Нативный корпус читает настоящие файлы, а не эмулятор I/O. Проверены границы дыр,
объединение соседних диапазонов, short read, пробуждение после publish, timeout
без EOF, cancel, paused writer, неизвестная длина, явный complete, позднее появление
файла, ошибка отсутствующего complete-ресурса, rename/замена имени при закреплённом
дескрипторе, truncation, перезапись последнего read-окна, invalid/stale generation,
writer failure, большие offset и переполнение, retired resources, leases до
последнего владельца, добавление другого ресурса и запрет hardlinks.
На обеих ОС создан sparse-файл с одним записанным байтом выше 4 ГиБ; прочитан
именно подтверждённый байт, соседний пропуск не прочитан. Большие offsets >2^53
проверены как точные метаданные/ожидание; реальный файл такого размера не создавался.

Один публичный Python-контракт из 11 сценариев запускается во всех трёх редакциях
и в нативном worker под санитайзерами. Он проверяет асинхронный старт, идемпотентный
requestId, запрет конфликтующего replay/замены/forget живой задачи, отзывчивость
управления во время ожидания, обновление диапазона, cancel/release, timeout,
частичное чтение без ложного allReadable, явный EOF и неизменность media.
Reader-квитанции закрываются до положительного release, затем явно подтверждаются.

Обе Qt-редакции сохраняют тот же объект действующего progressive-источника во время
I/O-проверки. Classic перед ней запускает отдельный v1 fMP4-источник, дожидается
playing, после неё подтверждает прежний active session/reader ID, затем закрывает
его по reader ID. SDK/writer отдельно воспроизводит >=10 секунд до complete,
завершается штатным finished и files-closed. Точные показания — в JSON.

В полных Qt-прогонах сохранены прежние indexed/fMP4 sessionDirectory, leases,
legacy/provider и writer-сценарии; Windows Qt включает provider reconnect.
Это не измерение физического A/V и не тест реальных YouTube/Rutube/VK источников.

Первый standalone Linux build нашёл отсутствующий include atomic; исправлен.
Standalone MSVC нашёл Windows macro min, исправлен переносимым вызовом std::min.
Первые Windows I/O/Classic прогоны отвергли созданные текущим повышенным процессом
файлы с default owner Administrators. Теперь проверяется именно OS token owner
наряду с user SID; произвольные группы не разрешаются, private registry ACL прежняя.
После исправлений выполнены финальные native и трёхредакционные API прогоны.
Коды исходных неудачных попыток сохранены отдельно от финального PASS в JSON.

Linux EXE обновлён атомарно, прежний сохранён для отката. INI до/после совпадает по
SHA-256 из JSON; просмотр не останавливался. SDK/docs обновлены отдельно. Версия
приложения остаётся 0.2.0; различать сборку следует по capabilities и SHA-256.
Рабочие Windows EXE/INI и установщики 0.2.0 не заменялись. У Classic тестовой копии
ContextMenuPrompted=1/ContextMenuEnabled=0, HKCU регистрация установки до/после
совпала. Одноразовая задача удалена. Harvester не менялся и не запускался.

Ограничения: ResourceSource ещё не подключён к demux/decode/remux. Сверка содержимого
охватывает только последнее успешное read-окно каждого файла; полной integrity-
гарантии всех опубликованных байтов нет. Live-compaction/удаление ресурсов, codec-
матрица, реальные источники и длительный A/V drift остаются следующими этапами.
Wayland и оборудование исключены пользователем; дополнительных таймеров нет.
