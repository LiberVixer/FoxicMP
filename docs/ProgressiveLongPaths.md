# Длинные локальные пути: schema 1 в трёх редакциях

Исправлен progressive playback по длинным локальным путям в обеих Windows
редакциях. Classic больше не ограничивает этот путь 240 символами и не
применяет к `sessionDirectory`/абсолютному имени файла старый лимит обычных
строк в 255 байт. Linux Qt дополнительно запрещает ссылки на родительские
каталоги, а не только на сам файл.

**Итог: 40 успешных сценариев** — 14 Classic, 14 Windows Qt, 12 Linux Qt;
Windows Python subprocess exit 0, Linux private CTest exit 0.
[Измерения, сохранённые отказы, SHA и установка](ProgressiveLongPathsValidation-2026-11-07.json).
Приложение 0.2.0, bundled SDK 1.84; wire API и capabilities не менялись.
Это проверка schema 1, не заявление о завершении всех long-path маршрутов.

## Изменения

Общий Windows helper `shared/progressivelocalpath_win.h` нормализует DOS-путь
динамическим буфером и добавляет `\\?\` только для внутренних файловых вызовов.
В IPC остаются обычные DOS-пути. Буфер final-name также больше не ограничен
MAX_PATH; handle закрывается при любой ошибке проверки. Сохраняются fixed-drive,
ADS, reparse-point, regular-file и final-name checks. Проверка владельца в
адаптерах сохранена. Компоненты с неоднозначными завершающими точками/пробелами
не получают нового смысла от расширенного namespace.

Для опубликованных Classic-путей установлен отдельный лимит **8192 UTF-8 байта**,
как в Qt; лимит прочих legacy string fields не расширяется. Внутренний Windows
helper ограничен 32760 UTF-16 units, с запасом для префикса и NUL. Это предел
валидации, а не доказательство поддержки физических файлов на максимальной
длине. Реально проверенный максимум — **1111 символов** полного имени файла.

Выбор Unicode API и внутреннего префикса следует [правилам Microsoft для длинных
путей](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation);
размер буфера учитывает [различие return value GetFullPathNameW при успехе и
недостаточном буфере](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew).
Windows policy не менялась: LongPathsEnabled=1 до и после финальной серии.
При выключенной policy отдельный прогон не выполнялся.

В Linux `ProgressiveFile::open` теперь идёт от открытого `/` через `openat` с
O_DIRECTORY/O_NOFOLLOW для каждого родительского каталога, а затем открывает
сам файл с O_NOFOLLOW. Каждый промежуточный fd закрывается. Это устраняет
наблюдённое принятие directory symlink и не использует фиксированный PATH_MAX
буфер. Обычное открытие видео пользователем за пределами progressive adapter
этой правкой не меняется.

## Настоящий playback и отрицательные случаи

На каждой редакции проверены каталоги длиной 220, 320 и 1100 символов с
Unicode и пробелами. Для каждой длины — относительные и абсолютные имена
дорожек, обычный IPC, два настоящих файла MP4 H.264 / M4A AAC:

- Физически записано 80% каждого файла. Плеер должен перейти в playing,
  позиция должна превысить 0,2 s, complete остаётся false.
- Остаток физически дописывается, затем writer публикует complete.
- После cancel обязательна положительная released/filesClosed квитанция.
  Только затем readerForget и удаление файлов; перед удалением байты сверяются
  с исходным fixture. Одного успешного probe недостаточно.
- Отдельно отклоняются URL, NUL, путь сверх wire-limit, file symlink и
  directory symlink; Windows дополнительно отклоняет ADS и UNC.
- После всех отказов тот же endpoint успешно открывает ещё один валидный
  источник и выдаёт положительную квитанцию закрытия.

Ссылки реально создаются в каталоге теста. Ошибка их создания — setup failure,
не скрытый skip. Аудиодорожка участвует в playback; слышимость и физическая
A/V синхронизация здесь не измерялись.

## Сохранённые исходные отказы

| Сборка/этап | Результат до исправления | Что установлено |
|---|---|---|
| Windows Qt | Python exit 1 после двух 220-char контролей, первый 320-char start отказал | GetFileAttributesW вызывался без внутреннего extended prefix |
| Classic | Python exit 1 уже на Unicode 220-char root | Общий String reader отвергал >255 UTF-8 bytes; дополнительный 240-char и MAX_PATH барьеры подтверждены исходником |
| Linux Qt, расширенный negative test | CTest exit 8, directory symlink принят | O_NOFOLLOW прежнего open защищал только последний компонент |

Ошибки и промежуточные журналы не переписаны успешными результатами. Самый
первый Linux-срез без проверок ссылок проходил; он не засчитывается как
приёмка исправления directory symlink. Финальные 40 случаев выполняются одним
SHA стенда на проверенных точных EXE, в новых каталогах отчётов.

## Установка, воспроизведение и границы

CLI: `python3 tests/progressive_long_paths_test.py --edition qt|classic
--player EXE --reader-runner EXE --sdk-directory DIR --fixtures DIR
--report-directory NEW_DIR`. Десять SDK-модулей заранее загружаются из указанного
пакета с проверкой происхождения. Linux запускается через
`qt/tests/run_tests.sh` в частных X11/D-Bus/PulseAudio; Windows — через
интерактивные одноразовые задачи. Все три задачи удалены, тестовые копии и
каталоги очищены. Classic до первого запуска получает ContextMenuPrompted=1 /
ContextMenuEnabled=0; Formats не открывается.

Linux EXE обновлён атомарно, прежний сохранён в
`foxicmp-qt.before-long-paths-20261107T095427Z`; INI SHA не изменился, CLI version
и capabilities совпали до/после. Из 583 защищённых файлов изменён только этот
EXE, остальные 582 сохранены. Сохранены также 158 HKCU command/icon entries,
Windows установки, опубликованные релизные assets и Harvester. Микшер,
мониторы и настройки Windows не менялись.

Windows проверены отдельными сборками, установленные EXE не заменялись.
Classic собран повторной компиляцией Session и линковкой с прежними проверенными
объектами: class layout и PCH не менялись. Qt собраны штатным CMake target.
Это совместимое исправление без нового SDK-контракта.

После этого этапа [проверены source roots schema 2 и continuous-remux](ProgressiveV2LongPaths.md).
Открыты другие cache/temp/registry roots, legacy pipe/file-manifest entry,
предельные длины компонентов/полного имени и расширенные варианты переименования.
Эти срезы не закрывают общую long-path/security/OS I/O fault/stress-приёмку.
Wayland и отложенное оборудование не проверялись.
