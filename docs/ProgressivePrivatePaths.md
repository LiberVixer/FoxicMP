# Progressive: длинные служебные пути и legacy-манифесты

Исправлен Windows I/O для private reader registry, continuous cache и его
журнала. Финальные компонентные проверки прошли на Linux и Windows; Classic
также прошёл запуск через `/harvester-session` с длинным манифестом. Qt проверен
через производственный `openProgressiveSession` в GUI-стенде.
[Результаты, native exits, SHA и сохранённые отказы](ProgressivePrivatePathsValidation-2026-11-07.json).
Приложение остаётся 0.2.0, SDK — 1.84; публичный протокол не меняется.

## Что исправлено

На прежних Windows binaries чтение при registry/cache длиной 331/328 символов
отказывало с `reader_registry_error`; короткий контрольный случай проходил.
Общий lease/cache/journal I/O теперь передаёт native Windows API динамический
Unicode extended path, сохраняя логическое имя. Прежние owner/ACL, no-reparse,
no-hardlinks, identity, квоты и положительные filesClosed receipts сохранены.
На private roots не перенесён более строгий fixed-drive фильтр исходных медиа:
это изменило бы прежнюю политику хранения служебных данных.

Промежуточная сборка с одним лишь префиксом отказала при recovery scan свежего
длинного cache. Вместо `std::filesystem::directory_iterator` используется
`FindFirstFileW/FindNextFileW` с RAII-закрытием и прежними лимитами. Зафиксированный
отказ не объявляется доказательством конкретной внутренней ошибки libstdc++.
Создание, чтение, rename журнала, удаление receipt/output и конечного каталога
используют те же native пути. Дополнительно прошли штатные cache/recovery
регрессии на обычных путях: реальные owner death, активные readers, повреждённые
журналы, подмены и bounded scan. Это отдельная проверка; полный long-path
rename/recovery matrix остаётся открытым.

## Проверенные границы

| Проверка | Linux Qt | Windows Qt / общий Windows I/O | Windows Classic |
|---|---|---|---|
| Native registry/cache, 3 длины | 150/147, 354/351, 1120/1117 | 178/175, 331/328, 1148/1145 | тот же общий I/O; отдельный playback ниже |
| Reader conformance | 14 проверок | 13 проверок | общий native runner |
| Resource conformance | 30 проверок | 30, включая реальные symlinks | общий native runner |
| Cache regression / restart recovery, обычные пути | 31 /33 | 31 /31 | общий native runner |
| Legacy growing манифест, Unicode/пробелы | GUI: 320/1100 | GUI: 320/1100 + provider restart | literal CLI: манифесты 349/1142 |
| Ordinary continuous, штатный короткий TEMP | 1 контрольный случай | 1 случай | 1 случай |

Каждый из шести native long-root случаев выполняет реальное range-read,
reader receipt/release/forget и работу MP4 worker: частичный источник,
публикация продолжения, output read, complete, отмена, filesClosed, очистка cache
и асинхронный forget. Эти roots намеренно ASCII: узкий `main(int,char**)`
тестовых CLI не квалифицирует Unicode argv. Unicode проверен отдельно в
source/manifest путях.

Qt legacy GUI-стенд наблюдает decoder `time-pos > .2` на неполном файле,
append/complete и закрытие pinned reader до исчезновения provider lease.
Итоговые QTest totals — 6 Linux /7 Windows, включая init/cleanup; это 4/5
сценариев, а не 6/7 независимых воспроизведений. Windows завершился native 0,
но после PASS оставил предупреждение `QEventDispatcherWin32::wakeUp` о неверном
window handle; исходный лог сохранён.

Classic literal CLI подтверждает decoder time ≥10 s до complete, последующий
append, Stop с освобождением lease при живом плеере и штатные app/provider
exit 0. Используются только отдельные копии с `ContextMenuPrompted=1` и
`ContextMenuEnabled=0`; страница «Форматы» не открывается.

## Что остаётся открытым

- **Windows с длинным TEMP.** Полный запуск Qt и Classic при process-only
  TEMP/TMP/TMPDIR длиной 320 символов не прошёл: deadline 120 s. Независимая
  диагностика Qt показывает родительский Python внутри `_winapi.CreateProcess`
  при конструировании `Popen`, до возврата discovery-ответа; диагностический
  timeout — 124. Причина пока не локализована. Длина 1100 на Windows этим не
  квалифицирована. Успешный native registry/cache тест не заменяет эту проверку.
- **Запись длинного Qt-профиля в Windows.** Первый GUI-стенд разместил профиль
  рядом с длинным манифестом и получил QLockFile `.rmlock` warnings/зависание.
  Пришлось завершить только тестовый процесс, native 4294967295. Итоговый
  manifest-тест использует независимый короткий профиль. Read-only discovery
  длинного профиля с обычным TEMP прошёл, но сохранение настроек этим не доказано.
- **Literal Qt `--harvester-session`.** Дополнительный эксперимент не принят:
  Linux-стенд ошибочно ждал Windows-файл `player-PID.json`, а Windows не получил
  `playing` до deadline. Причина Windows не установлена; оба exit 1 сохранены.
  Его предположение об автоматическом выходе после EOF также не проверено.
  Успешный Qt GUI-адаптер не объявляется проверкой полного main/CLI lifecycle.
- Предельные длины компонентов/пути, Unicode private CLI argv, полный rename
  matrix, прочие filesystem capabilities и общая A/V/stress-приёмка.

На Linux два отдельных запуска с TEMP 320/1100 прошли, но XDG runtime не
удлинялся: registry/cache оставались короткими. Это проверка TEMP/source/profile
маршрута, а не доказательство длинного cache у обычного GUI-процесса.
Диагностические экспериментальные стенды сохранены локально в `_bin`, их
отказы включены в JSON и не подменены успешными повторами.

## Воспроизведение и сохранность

Компонентный срез:

```sh
python3 tests/progressive_private_paths_test.py \
  --resource-runner RESOURCE_RUNNER --controller-runner CONTROLLER_RUNNER \
  --worker REMUX_WORKER --report-directory NEW_DIRECTORY
```

Classic: `tests/windows/progressive_legacy_long_paths_test.py --player EXE
--fixtures MEDIA --report-directory NEW_DIRECTORY`. Qt: функция
`legacyProviderPublishesWriterStateAndReleases` в `foxicmp-progressive-api-tests`;
Linux GUI/audio запускаются только через `qt/tests/run_tests.sh` с private
X11/D-Bus/PulseAudio. Для isolated TEMP-диагностики существующий v2-драйвер
получил `--runtime-parent PATH` и `--source-lengths`; environment меняется только
внутри тестового процесса. XDG runtime и настройки рабочего стола не меняются.

Classic пересобраны Session и ClassicProgressiveApi, Qt — штатным CMake.
Linux main пересобран и побайтово совпал с установленным
`015a9485bcbeba5e22561a63a73b652c4fcacfc007f813c34cf4990b3cc50dca`:
повторная замена того же EXE не нужна. INI и прежние rollback-файлы сохранены.
Все 583 защищённых SHA и 158 HKCU command/icon entries неизменны. После
проверок удалены 12 owned one-shot tasks и проверенные временные артефакты;
оставшийся после принудительного завершения provider остановлен штатно с exit 0.
Windows установки, опубликованные release assets и Harvester не изменялись.
