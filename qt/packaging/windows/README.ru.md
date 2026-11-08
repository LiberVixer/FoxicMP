# FoxiqMP qt 0.2.0 — Windows x64

[Установщик EXE](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0-installers.1/FoxicMPQt-0.2.0-windows-x64-setup.exe) ·
[MSI x64](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0-installers.1/FoxicMPQt-0.2.0-windows-x64.msi).
Оба формата устанавливают один и тот же MSI-продукт для текущего пользователя,
сохраняя профиль рядом с EXE. [Устройство и сборка установщиков](../../../packaging/windows/README.md).

Готовый portable-пакет: [FoxicMPQt-0.2.0-windows-x64.zip](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0-installers.1/FoxicMPQt-0.2.0-windows-x64.zip).
[Контрольная сумма SHA-256](https://github.com/LiberVixer/FoxicMP/releases/download/v0.2.0/FoxicMPQt-0.2.0-windows-x64.sha256).
Распакуйте его в доступную для записи папку и запустите `FoxicMPq.exe`.
В архиве нет пользовательского INI; сохраняйте свой профиль при обновлении.
Полный Windows Qt тест выпуска: 22 passed, 0 failed, exit 0 на Windows 11 x64.

В 0.2.0 общий каталог содержит 43 команды; Classic сохраняет собственные
defaults и клавиши. Сохранённые настройки не заменяются. Эталон организации
меню и настроек — Linux Qt.

В 0.1.72 сообщения поверх видео (OSD) выключены по умолчанию.
Сохранённый выбор в существующих профилях сохраняется.

В 0.1.71 Qt-версии получили имя FoxiqMP qt, зелёный акцент и отдельные
логотипы платформ с подписью #LiberVixer. Имя общего проекта — FoxicMP.
Имена файлов программы и профилей сохранены для совместимости.

В 0.1.70 убрана системная обводка Windows 11; окно можно перемещать за видео
левой кнопкой мыши. Новые профили получают сохранённые настройки интерфейса,
управления, звука, видео и субтитров из настроенного плеера. История, плейлисты,
координаты окна и согласие на регистрацию контекстного меню не копируются.
Существующие профили при обновлении сохраняются. Исправлена загрузка сохранённого
параметра видимости субтитров.

Нативная Qt-версия плеера на Qt 6 и libmpv. Запуск: `FoxicMPq.exe`.
Это отдельное приложение: FoxicMP Classic, MPC и MPC-BE не требуются.
Настройки, история, плейлисты и избранное сохраняются в `FoxicMPQt.ini`
и папках рядом с программой. Параметр `--portable` принудительно выбирает
эту папку; `--profile ПУТЬ` открывает отдельный профиль.

Все необходимые DLL входят в поставку. Системные Qt, MSYS2, Python
и пакеты кодеков устанавливать не нужно. FFmpeg включён для превью,
миниатюр и экспорта. yt-dlp используется для интернет-источников.

В настройках «Форматы» можно зарегистрировать собственное приложение
Windows, добавить пункт FoxicMP Qt в контекстное меню видео и включить
проверку пути при запуске. Действия выполняются только по кнопке.
Для назначения плеером по умолчанию откройте параметры Windows через
кнопку на этой странице. Настройки других плееров не изменяются.
Ярлыки отдельных профилей создаются в формате Windows `.lnk`.

Воспроизведение, интерфейс, библиотека, субтитры, фильтры, Web-управление,
снимки и остальные общие функции используют один исходный код с Linux.
Отдельно реализованы Windows IPC, блокировки профиля, операции с файлами,
чтение ICC-профиля монитора и действия завершения воспроизведения.

Ограничения первого Windows-переноса:
- API недокачанных файлов Harvester v1 доступен через локальный Windows named pipe; Linux использует Unix-сокет.
- Второй аудиовыход Windows использует отдельный WASAPI/libmpv PCM-декодер и программно следует основному. Физическая задержка между устройствами не измерена.
- Linux MPRIS и дублирование звука через PulseAudio в Windows не предлагаются.
- Захват V4L2/ALSA, аналоговое ТВ, DVB и управление физическими устройствами
  не перенесены. Проверка специального оборудования отложена.

Для сборки используйте MSYS2 UCRT64, пакеты GCC, CMake, Ninja, Qt 6 Base,
libmpv и lcms2. Из оболочки UCRT64:

```
cmake -S qt -B windows-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build windows-build -j 8
```

Прямая CMake-сборка использует runtime окружения. Для проверенной поставки
Windows 6.12.0 обязательны частные исправленные QtCore и winpthreads ниже.

Сначала подготовьте частную QtCore по инструкции ниже.
Из Windows PowerShell можно использовать штатный скрипт; пути компиляторов
нормализуются для CMake, включая аргумент `C:\...`:

```powershell
./qt/packaging/windows/build.ps1 -ToolchainDirectory C:\msys64 -BuildDirectory C:\build\FoxicMP-qt -QtCoreRuntimeDirectory C:\build\FoxicMP-qtcore -Jobs 8
./qt/packaging/windows/deploy.ps1 -BuildDirectory C:\build\FoxicMP-qt -ToolchainDirectory C:\msys64 -Destination C:\packages\FoxicMPQt-0.2.0-windows-x64 -YtDlpExecutable C:\tools\yt-dlp.exe -YtDlpLicense C:\tools\yt-dlp-licenses.txt
```

`Destination` должна быть новой папкой. Для поставки онлайн-видео передавайте
оба файла yt-dlp, как в примере; необязательный аргумент не скачивает yt-dlp
автоматически. Упаковывайте подготовленную папку без личного `FoxicMPQt.ini`.

Входной поток stdin поддерживает потоковые контейнеры (например, MKV или
fast-start MP4). Обычный MP4 с индексом в конце требует перемотки и через
неперематываемый канал не воспроизводится; такой файл открывайте по пути.

Скрипт `qt/packaging/windows/deploy.ps1` собирает автономную папку из
CMake install, windeployqt и рекурсивного анализа импортируемых DLL.
Ему нужны BuildDirectory, ToolchainDirectory (папка msys64) и новая Destination.
Параметры YtDlpExecutable и YtDlpLicense добавляют автономный yt-dlp с лицензиями.
Сборочная среда в поставку не включается. Версии зависимостей записаны
в `qt-dependencies.json`; лицензии библиотек находятся в `licenses`.

Проверки Windows: `foxicmp-windows-tests.exe` запускается на доступном рабочем
столе с FOXICMP_TEST_MEDIA, указывающим на корпус из `qt/tests/generate_media.py`.
Проверяются профили, архивы, ярлыки, IPC, CLI, TLS, шесть кодеков, перемотка,
субтитры, настройки и stdin. Используются временные профили с выключенным
звуком. Системные ассоциации и действия выключения компьютера тест не меняет.

## Частная winpthreads DLL

`build.ps1` дополнительно запускает `build_winpthread.py` (нужны UCRT Python, GCC,
windres и objdump). Он скачивает архив mingw-w64 commit
`63ebe6cdb3ec4d5c586b7f0f0c03dd502e44c296`, проверяет SHA-256
`94469ea48f974f9a4f7d8b7050cb7eceaa92c1f941ed3b6b8137591ba19491de`
и применяет точный `winpthreads-detached-spin.patch`. Исправляется освобождение
TSD spinlock при выходе detached thread; прежние exports сверяются со штатной DLL.
Источник: https://github.com/mingw-w64/mingw-w64/tree/63ebe6cdb3ec4d5c586b7f0f0c03dd502e44c296/mingw-w64-libraries/winpthreads .

Библиотека и `winpthread-build.json` создаются только в build directory.
Toolchain/MSYS2 и установленные плееры не изменяются. Повторная сборка может
использовать проверенный архив через `build_winpthread.py --archive PATH`.
`deploy.ps1` требует metadata с совпадающим DLL SHA-256 и включает исправленную
DLL после автоматического поиска зависимостей. Лицензии upstream сохраняются
в `licenses`, patch и build script — в `licenses/winpthreads-fix`.
Без metadata упаковка прерывается, чтобы не вернуть утечку штатной DLL.

Проверки MinGW CTest: `progressive-pthread-detached`,
`progressive-pthread-joined`, `progressive-player-lifecycle`.
Методика и границы: [100 циклов плеера](../../../docs/ProgressivePlayerLifecycle.md).


## Частная QtCore: длинные writable profiles

Для текущего MSYS2 UCRT64 Qt 6.12.0-1 штатный QLockFile оставляет lock при
длинном INI. Исправление сохраняет QSettings atomic sync и блокировки между
процессами. Динамические Windows tempPath buffers дополнительно исключают
чтение за пределом массива. Текущий patch также сохраняет точный длинный TMP/TEMP/USERPROFILE обычного
пользователя при длине >=MAX_PATH; пустые переменные пропускаются, короткий TMP
остаётся приоритетным. Для LocalSystem сохраняется native SystemTemp branch.
[Новая exact-TEMP проверка и границы](../../../docs/ProgressiveQtExactTempPaths.md);
[исторический fallback/lock этап](../../../docs/ProgressiveWindowsProfilePaths.md).

На исходном Linux-хосте нужны Python 3.12+ и GNU patch. Используйте свежие
work directory и overlay output:

```bash
python3 tools/runtime/prepare_qtcore_long_paths.py --work /tmp/foxicmp-qtcore-source --overlay /tmp/foxicmp-qtcore-overlay.tar
```

Вывод содержит три пути: sourceArchive, overlay, cppWinRTArchive. Передайте
эти файлы Windows-сборщику (например, через общую папку). Архивы, 11 патчей
MSYS2 и overlay проверяются по закреплённым SHA. Пакет CppWinRT даёт только
частные headers/лицензии. Затем в Windows PowerShell:

```powershell
./tools/runtime/build_qtcore_long_paths.ps1 -ToolchainPrefix C:\msys64\ucrt64 -WorkDirectory C:\build\FoxicMP-qtcore -SourceArchive C:\sources\qtbase-everywhere-src-6.12.0.tar.xz -SourceOverlay C:\sources\foxicmp-qtcore-overlay.tar -CppWinRTArchive C:\sources\cppwinrt-3.0.260818.1-3.pkg.tar.zst -Jobs 3
```

Скрипт собирает только Core и создаёт Qt6Core.dll, qtcore-longpaths-build.json
и qtcore-licenses в WorkDirectory. Валидатор сверяет x86_64, все stock exports,
216 feature flags и SHA исправленных исходников. Нужен соответствующий
MSYS2 Qt6 6.12.0-1 toolchain; чужая версия может не пройти сравнение.
Глобальная DLL и установленный плеер не заменяются.

Передайте WorkDirectory в build.ps1 как QtCoreRuntimeDirectory (пример выше).
После build.ps1 deploy.ps1 по умолчанию берёт проверенную DLL из BuildDirectory;
можно явно передать -QtCoreRuntimeDirectory C:\build\FoxicMP-qtcore.
Preflight проверяет patch pin, DLL/stock SHA и результаты ABI/feature проверки
до создания Destination. Патч, source pin, инструменты и Qt/CppWinRT licenses
попадают в licenses/qtcore-longpaths-fix. Не копируйте поверх эту DLL штатную
Qt6Core.dll после упаковки.

Тест foxicmp-settings-path-tests / CTest settings-paths проверяет повторные
atomic sync, удаление lock, чтение отдельным процессом и concurrent merge
на Unicode-путях 120/320/1100. Проверка полного main использует
 tests/windows/progressive_long_runtime_test.py; тест TEMP-компонента
 tests/windows/settings_temp_paths_test.py по умолчанию требует exact root,
а --observe-system-temp явно фиксирует fallback и не объявляет long TEMP готовым.


Для строгой проверки актуального DLL используйте
`tests/windows/progressive_long_runtime_test.py --require-exact-temp`:
обычный режим сохраняет совместимость с историческими fallback-прогонами.
`tests/windows/qt_temp_player_test.py` проверяет actual Player/decoder при
TEMP240/320/1100, `tests/windows/qt_temp_policy_test.py` — приоритеты, пустую
переменную и относительный путь. Native DLL SHA и exits приведены в
[отчёте](../../../docs/ProgressiveQtExactTempPathsValidation-2026-11-08.json).
Все команды выполняют проверки на частных профилях и сохраняют parent environment.
Ссылки на опубликованные 0.2.0 пакеты в начале страницы остаются историческими
immutable assets; новый runtime в них не подменялся.
