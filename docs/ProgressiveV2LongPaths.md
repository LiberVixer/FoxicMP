# Длинные source paths: schema 2 и continuous-remux

Проверены оба маршрута в трёх редакциях: **18 случаев playback** — по три
длины source directory для schema 2 и continuous-remux на каждой редакции.
Все финальные Python test subprocess завершились с **exit 0**. В Windows Qt
устранён подтверждённый отказ на 320-символьном каталоге; Classic и Linux
проходили эти маршруты до изменения и проверены повторно после сборки.
[Точные EXE/worker SHA, квитанции, отказы и exits](ProgressiveV2LongPathsValidation-2026-11-07.json).

Приложение 0.2.0, bundled SDK 1.84. Wire API, capabilities, форматы и настройки
не менялись. Это следующий срез после [schema 1](ProgressiveLongPaths.md),
а не общая приёмка всех видов длинных путей.

## Исправление общего I/O

`resource_detail::Directory` в Windows проверял компоненты через обычные
GetFileAttributesW и открывал обычный DOS-путь. На проверенной Windows Qt
первый 220-char случай проходил, следующий 320-char start отказывал.
При continuous тот же отказ возникал до запуска worker: workerId=0,
sourceFilesClosed=true, error state вместо готовности к playback.
Сохранены оба исходных FAIL с Python exit 1.

Общий Resource I/O теперь использует `local_path_detail::Canonical/Native`
для внутренних Unicode check/open. Он также больше не проверяет промежуточное
`C:` как drive-relative текущий каталог вместо полного prefix. Внешний путь
остаётся DOS; existing 8192-byte bound, fixed drive, owner/token owner,
no-reparse, regular file, no-hardlinks, file identity, SHA-256 и final-name
checks сохраняются. После открытия хранится прежний pinned final directory.
Это не обход разрешений и не разрешение UNC/network sources.

Linux-ветка не менялась: там Resource I/O уже использует pinned openat walk.
Проверенная Linux-сборка побайтно совпала с установленной.

## Что делал тест

Каталоги 220, 320 и 1100 символов, Unicode и пробелы, максимальное полное
имя source-файла **1111 символов**. Обычные полные Qt/Classic EXE с отдельными
профилями, один и тот же SHA стенда во всех финальных сериях.

**Schema 2:** два отдельных fMP4 файла H.264/AAC-LC физически записаны до 12 s
из 20 s. Требуются playing, позиция >0,2 s, неполный source и decoder I/O
обоих треков. Затем остатки дописываются, публикуются SHA/complete и проверяется
закрытие. Положительный released + reader.filesClosed proof сохраняется до
SDK forget; остальные сохранившиеся квитанции тоже должны быть положительными.
Перед удалением проверяется точное совпадение source bytes с fixture.

**Continuous:** обычный MP4/H.264 video-only, 12 s из 24 s. Prepare/ready,
живой worker с удержанием его OS identity, явный Play и playing с decoder reader.
После физического append и publish требуется outputComplete с прежним workerId;
затем Stop/close, positive released/sourceFilesClosed и все filesClosed receipts,
фактический OS exit worker, forget и отсутствие remux cache. Источник удаляется
только после этих подтверждений и сравнения всех байтов.

Позиция >0,2 s подтверждает actual playback этого среза; тест не смотрит все
20/24 s и не измеряет физическую A/V синхронизацию или слышимость аудио.
Используется обычный короткий registry/cache root, его пути сохранены в JSON.
Длинные source roots не выдаются за проверку длинного cache/temp root.

## Регрессии и ошибки стенда

- По **30 native Resource I/O cases** Linux и Windows: реальные bytes/ranges,
  ожидания, publish, cancellation, идентичность, EOF, leases, hardlinks и
  настоящие file/directory symlinks. Новый opt-in `--require-symbolic-links`
  включает эти два случая и в Windows. Ошибка создания ссылки — FAIL,
  не скрытый skip; обычные прежние Windows CI-запуски без flag не меняются.
- **40 прежних schema-1 cases**: 14 Classic, 14 Windows Qt, 12 Linux Qt,
  все exit 0. Это отдельные регрессии, а не ещё 40 новых schema-2 сценариев.
- В Linux build-tree worker отличался по SHA от установленного release worker.
  Поэтому дополнительно пройдены **три continuous случая** точной комбинации
  main EXE и установленного worker. Worker и SDK на установке не заменялись.

В первом Linux-запуске новый стенд потребовал непустой registry уже после SDK
close/forget и вызвал несуществующий `ContinuousRemux.update`. Это ошибки
стенда, исходный CTest exit 8 и оба traceback сохранены. Исправлено на хранение
уже полученного positive proof и вызов `publish`. Пустая enumeration сама
по себе не считается освобождением. Финальный стенд не ослабляет filesClosed.

Первая попытка SSH запуска -File была отправлена до создания одноразового
скрипта и не создала задачу. Повтор после завершения подготовки создал одну
задачу baseline; финальная серия — другую. SSH/task exit не подменяет native
exit каждого Python subprocess. Helper после проверки освобождения завершает
только собственные уже пустые тестовые PID; это не проверка штатного app Exit 0.

## Сборки и сохранность

Classic: повторно скомпилированы Session и ClassicProgressiveApi — оба translation
units, включающих изменённое Resource I/O; class layout и PCH не менялись.
Qt/worker/native CLI targets собраны штатным CMake. Windows проверены отдельными
сборками, установленные EXE и INI не заменялись.

CLI стенда: `python3 tests/progressive_v2_long_paths_test.py --edition qt|classic
--route schema2|continuous --player EXE --reader-runner EXE --sdk-directory DIR
--fixtures DIR --report-directory NEW_DIR`. Десять SDK-модулей загружаются из
указанного пакета с проверкой происхождения. Linux UI/audio — только через
`qt/tests/run_tests.sh` с private X11/D-Bus/PulseAudio. Windows — interactive
одноразовые задачи; **обе удалены**. Classic получает ContextMenuPrompted=1 /
ContextMenuEnabled=0 до первого запуска; Formats не открывается.

Проверено совпадение всех **583 защищённых SHA** и **158 HKCU command/icon entries**
до/после. Проверенный Linux EXE синхронизирован атомарно с backup; его SHA остался
015a9485bcbeba5e22561a63a73b652c4fcacfc007f813c34cf4990b3cc50dca, INI и capabilities
сохранены. Прежняя rollback-копия также сохранена. Harvester, опубликованные
assets, микшер, мониторы и настройки Windows не менялись.

Остаются другие cache/temp/registry root варианты, legacy pipe/file-manifest
entry, предельные длины компонентов/пути и расширенная rename matrix. Общая
long-path/security/OS fault/stress приёмка остаётся открытой; Wayland и
отложенное оборудование не проверялись.
