# Qt: writable profiles and Windows runtime paths

Уточнение 8 ноября: прежний native TEMP probe не имел longPathAware manifest.
[Контроль с manifest Classic и частичное исправление registry](ProgressiveWindowsTempManifest.md)
разделяет OS fallback и GetTempPathW timeout; полный long-TEMP playback открыт.

7 ноября 2026. Приложение 0.2.0, SDK 1.84; API, endpoints и defaults не менялись.
Этот этап закрывает ограниченный writable-profile срез. Полное progressive ТЗ
и точный Windows TEMP320/1100 остаются открытыми.
[Машинный отчёт](ProgressiveWindowsProfilePathsValidation-2026-11-07.json).

## Исправления

В штатной QtCore 6.12.0 создание QLockFile использовало native extended path,
а удаление и повторное чтение lock — исходную строку WinAPI. При INI длиннее
260 символов первый QSettings sync возвращал NoError, но оставлял lock;
следующая запись могла зависнуть. Контрольный тест штатной DLL: native exit 2,
две длинные строки FAIL. Отключать atomic sync или межпроцессную блокировку
не стали: обе гарантии сохранены.

Частная QtCore собирается из точного Qt v6.12.0 commit
`025bdad181de241e81bf853c8a2d7bf3d19261a9` с проверенными патчами MSYS2
и [локальным исправлением](../tools/runtime/qt-6.12.0-long-lock-paths.patch).
Удаление и чтение lock используют QFileSystemEntry::nativeFilePath.
Дополнительно fixed-size буферы Windows tempPath заменены динамическими:
возвращённый требуемый размер больше не используется для чтения короткого массива.
Исправление сохраняет системный выбор временного каталога.

В приложении native CreateFile ProfileLease и MoveFileEx получают extended
Unicode-путь; логические имена Qt/профилей, запрет перезаписи и guards сохранены.
Linux-ветви не изменились. Classic на этом этапе не менялся.

Источник механизма: [QLockFile Qt 6.12.0](https://github.com/qt/qtbase/blob/v6.12.0/src/corelib/io/qlockfile_win.cpp),
[QSettings atomic sync](https://doc.qt.io/qt-6/qsettings.html),
[GetTempPath2W и размер буфера](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppath2w).

## Проверено

| Маршрут | Результат и граница |
|---|---|
| Qt Windows full main/CLI | 6 случаев, native 0: профиль обычный/320/1100; TEMP240 точно; TEMP320/1100 с измеренным системным fallback |
| Qt Linux full main/CLI | 3 профиля обычный/320/1100, каждый native 0; частный X11/D-Bus/PulseAudio |
| QSettings Windows и Linux | По 3 строки, QtTest 5 PASS включая init/cleanup, native 0: три atomic sync, отсутствие orphan lock, независимый reader, два writer и merge, Unicode/bytes/int и нетронутое значение |
| Windows TEMP component | 3 контекста, native 0; 240 принят, 320/1100 проверяют фактический системный root, а не запрошенный |
| Windows port | 8 рабочих строк + init/cleanup = 10 PASS, native 0: в том числе реальный long move и no-overwrite |
| Windows legacy adapter | 5 строк + init/cleanup = 7 PASS, native 0: complete/failed/long320/long1100/provider-restart |
| DLL/упаковка | x86_64, все 6920 stock exports сохранены, 216 feature flags совпали; private deploy и standalone --version native 0 |

В full main/CLI проверены buffering/playing/paused, pause/resume, append/complete,
явный provider release при живом main, ошибка malformed session в stderr,
нормальное завершение main через отдельный ordinary WAV и сохранность INI sentinel.
IPC-состояние playing не выдаётся за новое измерение decoder clock или физического A/V.
Длины 320/1100 — целевые минимумы каталога; точные длины в JSON/логах.
Финальные настройки проверены отдельными процессами, а не read-only discovery.

## Сохранённые отказы и открытые границы

* Строгие прогоны TEMP320 ожидавшие exact root завершились native 1.
  На этой машине прямые GetTempPathW и GetTempPath2W с буфером 32768
  выбрали `C:\Users\User`, включая запрос с extended prefix. Исправленная
  QtCore следует этому результату. PASS с fallback не закрывает exact long TEMP.
  При TEMP240 реально использованы lease/IPC-lock пути длиннее 260 символов.
* При изменении TEMP самого родительского Python запуск задерживался внутри
  Popen/_winapi.CreateProcess даже для короткого системного/native child.
  Child-only env запускался; native CreateProcess с длинным env также запускался.
  Причина не локализована. Это отдельный открытый маршрут внешнего launcher.
* Диагностический Classic long-TEMP прогон завершился native 1 на ожидании
  playing (waiting-downloader). Контроль с обычным TEMP дошёл после playback
  до неверного требования наличия reader registry и тоже завершился 1:
  использованный старый v1 contract не включает opt-in readerLease.
  Ни один из этих прогонов не принят как Classic long-TEMP PASS или доказанная
  регрессия. Этот диагностический драйвер не включён в штатные тесты.
* Сохранены configure exit 1 из-за отсутствующих частных CppWinRT headers,
  затем configure/build 0 после их добавления. Первая PowerShell попытка
  прервалась NativeCommandError без native exit receipt; её код не выдумывается.
* Windows port/adapter оставили offscreen/sizeHints и trailing wakeUp warnings;
  native exits 0. Эти предупреждения не скрыты и не объявлены исправленными.
* Предельные компоненты, все rename/error/EOF combinations, физический A/V,
  общая stress/format/source матрица и интеграция Harvester остаются открытыми.

## Воспроизведение сборки и изоляция

[Подготовка overlay](../tools/runtime/prepare_qtcore_long_paths.py) проверяет архив,
11 MSYS2 patches, CppWinRT headers и локальный patch по SHA-256. Повторная
подготовка из свежего каталога дала тот же overlay SHA-256
`53d5e057997dd6c513dfb69502e37555486361f80ec7fb00c241a7f8e63f3cb2`.
Это доказательство воспроизводимости исходного overlay, не битовой
воспроизводимости линкера. [Сборка Windows](../tools/runtime/build_qtcore_long_paths.ps1)
создаёт только частную Core. Глобальная MSYS2 DLL не заменяется.
[Инструкция упаковки](../qt/packaging/windows/README.ru.md) требует metadata,
совпадающие DLL/stock SHA, local patch pin, архитектуру и feature/export проверки;
лицензии, патч и инструменты входят в пакет. Неверный patch pin отклонён
отдельным negative control до создания Destination. Полная повторная сборка через новый
wrapper не заявляется: принятые configure/build выполнены эквивалентными
зафиксированными командами; wrapper разобран PowerShell parser без ошибок.

Установленный Linux main уже совпадает с пересобранным
`ec395df9c0b54e124107d2b6263890919fb092654230e5905c87f4e1c3b721ca`:
лишней замены нет, INI SHA сохранён. Все 583 protected SHA и 158 HKCU entries
Classic совпали до/после. Семь одноразовых задач удалены, совпадающих тестовых
процессов не осталось. Шесть точных orphan artifacts двух неудачных прогонов
удалены только после проверки PID/birth и содержимого; чужие runtime-файлы
не очищались. Личные профили, Windows-установки, опубликованные release assets,
настройки Windows и Harvester не менялись.
