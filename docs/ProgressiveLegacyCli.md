# Qt: полный запуск через `--harvester-session`

Завершён следующий срез после [длинных служебных путей](ProgressivePrivatePaths.md):
**10 CLI-сценариев двух Qt-редакций**, по пять Linux/Windows, прошли с native
exit 0. Используется полный main EXE, реальный локальный provider из репозитория
и отдельный короткий профиль. Harvester не запускается и не изменяется.
[Точные результаты, пакеты, SHA и сохранённые отказы](ProgressiveLegacyCliValidation-2026-11-07.json).
Приложение — 0.2.0, SDK — 1.84; wire protocol не меняется.

## Подтверждённый Windows-дефект

Старый Windows Qt принимал attach и heartbeat от legacy provider, но его
publication worker вообще не отправлял текущий `playbackState`. Поэтому
загрузчик не получал `buffering`, `playing`, `paused` и `error`. Это
воспроизведено также с коротким source root: отсутствие `playing` в предыдущем
эксперименте не было доказательством отказа декодера или длинного пути.
Linux уже отправлял эти состояния; тест ошибочно ожидал на нём Windows-файл
`player-PID.json` вместо наблюдения реального Unix IPC.

Windows worker теперь копирует состояние под mutex и выполняет bounded pipe
I/O после его освобождения. UI `notify()` по-прежнему меняет только metadata.
Сбой записи закрывает повреждённое соединение и оставляет существующий
retry/reacquisition механизм; живые receipts не объявляются освобождёнными.
При writer failure/error состояние `error` отправляется перед завершением
worker. Окончательный `released` остаётся за закрытием последних media handles.
Сохранены owner/SID checks, revision/generation/reader identity и heartbeat
policy. Classic на этом этапе не менялся.

Обе Qt-редакции также выводят причину асинхронного отказа legacy opening в
UTF-8 stderr (`Progressive source: ...`). Ошибка остаётся видна в UI, окно
позволяет открыть другой файл. Код 0 вспомогательного forward CLI означает
принятие запроса; успешность открытия проверяется отдельно.

## Финальные проверки

| Сценарий полного Qt main | Linux | Windows |
|---|---|---|
| Growing → pause/resume → complete → Stop/release | манифесты 52/360/1152 chars | 80/344/1136 chars |
| Natural EOF после complete | один короткий root, 20 s | один короткий root, 20 s |
| Ошибка writer на неполном файле | один короткий root | один короткий root |

Во всех десяти случаях source реально неполный: сначала 5, затем 12 fMP4
фрагментов H.264/AAC-LC. Provider принимает PID/creation identity настоящего
плеера; наблюдаются `buffering`, `playing`, `paused` и повторный `playing`
после CLI-команд паузы/продолжения. Три source-root длины проверяются через
Stop; natural EOF и writer failure проверены отдельно только на коротких roots.

При complete writer физически дописывает файл и повышает revision. При failed
проверяется `error`, а источник остаётся точным 12-fragment префиксом. Проверка
байтов не заменяется одним размером или флагом состояния. Поле `sourceSha256`
в исходных CLI result rows содержит SHA полных fixture references; в failed
случае стенд сравнивает точные неполные байты, а не выдаёт этот SHA за хеш
физического partial-файла.

При Stop, natural EOF и writer failure наблюдается **явный** вызов provider
release при живом main, затем пустой lease set. OS reap не подменяет эту
квитанцию. После освобождения в том же main открывается намеренно испорченный
манифест: stderr получает причину ошибки, процесс остаётся жив.

Штатный main exit 0 проверяется отдельно: в то же окно передаётся обычный
односекундный WAV с `--after exit`. Progressive EOF сам по себе не обязан
закрывать приложение. Provider работает потоком внутри тестового Python,
а не отдельным процессом; проверены его shutdown/join и пустые connections,
точный Python exit сохранён отдельно.

Windows GUI-адаптер дополнительно повторил complete/failed/long320/long1100/
provider-restart: пять сценариев, QTest totals 7 PASS с init/cleanup, native 0.
После PASS сохранилось прежнее `QEventDispatcherWin32::wakeUp` warning;
лог приведён в JSON без переименования этого предупреждения в успешную проверку.

Здесь проверены настоящие playback-state уведомления полного main. Отдельного
измерения decoder position в этом CLI-стенде нет; его наличие проверял
предыдущий GUI-адаптер. Physical A/V sync, прочие форматы и общая stress-приёмка
этими десятью случаями не закрываются.

## Воспроизведение и установка

```sh
python3 tests/progressive_legacy_cli_paths_test.py \
  --player FULL_QT_EXE --fixtures MEDIA --report-directory NEW_DIRECTORY
```

По умолчанию — Stop и source roots 0/320/1100. Отдельно:
`--source-lengths 0 --finish natural` и `--source-lengths 0 --finish failed`.
Стенд только наблюдает socket/pipe reads и accepted lease callbacks локального
provider; payload и его production implementation не подменяются.
Linux GUI/audio запускаются через `qt/tests/run_tests.sh` с private
X11/D-Bus/PulseAudio; Windows — в interactive одноразовой задаче с offscreen Qt.

Linux установлен атомарно, SHA main теперь
`ec395df9c0b54e124107d2b6263890919fb092654230e5905c87f4e1c3b721ca`.
Прежний EXE сохранён для отката; INI SHA остался
`f3b64a705e0f841d915011c0728a541533151696f7020edc47e8499f099e7333`.
Просмотр не останавливался. Остальные 582 защищённых файла и 158 HKCU
command/icon entries неизменны, три owned one-shot tasks удалены, тестовых
процессов не осталось. Windows установки и опубликованные release assets
не заменялись.

Открыты прежние **Windows long TEMP startup** и **запись длинного Qt-профиля**
(QLockFile), предельные длины/компоненты, Unicode private CLI argv и полный
rename matrix. Успешные CLI-запуски с коротким TEMP/profile их не закрывают.
