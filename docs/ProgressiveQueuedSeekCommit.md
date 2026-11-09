# Отложенный seek: граница передачи декодеру

Дата: 9 ноября 2026. App 0.2.0, публичный SDK 1.87 и его команды сохранены.
Работа относится к трём редакциям; Harvester не менялся.

## Исправление

При просмотре обработки очереди обнаружен риск повторного входа: завершение
старого seek могло удалить новую цель или затереть результат отмены. В Classic
ожидающая цель также снималась до вызова `SetPositions` без проверки HRESULT.
Предыдущий публичный IPC race-стенд не задавал эти внутренние границы.

Общий `ContinuousPlayback::applyQueuedSeek` фиксирует revision, цель и внутренний
счётчик изменений. Повторный опрос уже выполняющейся транзакции не передаёт
команду ещё раз. После readiness/observer callbacks перепроверяются владение,
подтверждённый диапазон, закрытие и ошибка источника. Адаптер проверяет этот
же допуск непосредственно перед командой декодеру.

Если отмена или замена цели произошла до передачи, старая команда не отправляется.
Если декодер уже принял её, отмена прекращает ожидание, но не обещает отменить
уже отправленный seek. Новая цель сохраняется и применяется следующим опросом.
Успешная старая команда расходует один слот; отказ не расходует слот и допускает
повтор. На время передачи последний слот резервируется от повторного входа;
лимит остаётся 16 growing seeks, completed output обходится без этого лимита.

Транзакция и её callbacks принадлежат GUI-потоку; это защита от повторного
входа, а не разрешение вызывать mutable playback state из чужих потоков.

Qt сохраняет переоткрытие того же continuous source для growing AAC, pause/speed
и захватывает callback именно старого источника. Classic сохраняет существующую
void `SeekTo` для обычного управления; отдельный `SeekQueuedTo` сообщает принятие
команды, удерживает прежний `IMediaSeeking` и проверяет HRESULT. Меню, оформление,
назначения клавиш и defaults этим изменением не затрагиваются.

## Доказательства и пределы

Общий нативный тест `tests/progressive_queued_commit_test.cpp` принудительно
вызывает отмену, новую/ту же цель, закрытие, потерю владения, обычный seek,
ошибку producer/retirement из output/producer/readiness callbacks. Отдельно
проверяются принятый/отклонённый handoff, повторный вход, исключения, quota и
освобождение резерва. Проверяются состояния и фактическое число submit-вызовов.

В настоящем Qt Window частный friend-only test hook ставит событие до передачи
или после принятия команды, до завершения транзакции. Восемь дополнительных
сценариев используют реальный mpv/worker и отдельные профили: cancel/supersede
до и после handoff, Stop, ordinary replacement, producer failure до handoff и последний доступный слот.
Это не публичная команда, capability, параметр CLI или SDK.

Classic использует ту же проверенную транзакцию; принудительного повторного
входа внутри настоящего DirectShow graph этот тест не создаёт. Проверки общего
компонента, Qt Window и публичного Classic IPC учитываются отдельно. Это
ограниченный commit-срез, а не доказательство полной I/O/codec/crash/race,
physical A/V, long-run или latency приёмки. Прежние необъяснённые отказы не сняты.

Первый стресс-прогон выявил ошибку новой резервации в Qt: обычный Window
availability callback отвергал собственный последний слот. Linux CTest exit 8,
Windows stress native exit 1; Windows diagnostic snapshot сохранил paused,
remaining quota 1, pending target 1 и прежнюю позицию .5005. Исправленный queued
путь проверяет собственный guard вместе с обычными Window readiness conditions;
ручные команды продолжают видеть занятый слот. Бюджеты и deadlines не расширены.
Отдельный восьмой Window-сценарий проверяет применение последнего слота и отказ
семнадцатой команды. [Итоговый отчёт](ProgressiveQueuedSeekCommitValidation-2026-11-09.json)
содержит SHA, все raw-report fingerprints, native exit codes и compact stress
rounds по общей схеме. Все конечные drivers имеют exit 0:

| Проверка | Результат |
| --- | --- |
| GCC, GCC ASan/UBSan/LSan, MinGW, MSVC shared test | По 115 checks |
| Настоящий Qt Window: Linux / Windows | По 28 cases, включая 8 новых |
| Ordinary main API, 3 редакции | 222 cases: 69 SDK 1.87 +153 прежних |
| Public IPC races, 3 редакции | 108 cases |
| Измеряемые циклы +warmup, 3 редакции | 300 +12 |
| Установленная Linux-копия после замены | 23 cases |

Прежние handles/threads/RSS/private/CPU/RPC/release budgets выполнены без
расширения. Основной процесс, steady→late handles/threads: Linux Qt
37→37/59→59, Windows Qt 1697→1692/37→36, Classic 479→469/16→13.
Оба endpoint каждой редакции прошли resource gates; сохранены все измеряемые
окна и reader filesClosed/forget proofs. Внедрённые ошибки и завершение test peers
учитываются своими process exits, а не объявляются штатными worker exits 0.

Один исправленный Linux-прогон был прерван signal 143 без native driver receipt;
его первые PASS не приняты за завершённый driver. Повторный одноразовый процесс
получил exit 0. Прерванный SSH launcher также не объявлен PASS: Windows native
receipts сохранены независимо. В Qt Windows после успешных тестов остаётся
`QEventDispatcherWin32::wakeUp: Failed to post a message`; native exit 0 и stderr
учтены отдельно.

Linux обновлён атомарной заменой EXE, с копией старого EXE вне установки и
проверкой прежних CLI version/capabilities. INI до/после совпал. Из 587 защищённых
файлов изменился только Linux EXE, 586 остальных сохранены; 158 HKCU shell
entries совпали. Windows установленные приложения, их профили, SDK-модули,
Harvester и настройки сеанса/микшера не изменены. Одноразовые test processes
завершились; Windows Scheduler/периодические задачи не создавались.


## Воспроизведение

Общий компонент после настройки CMake:

```sh
cmake --build BUILD --target foxicmp-progressive-queued-commit-tests
ctest --test-dir BUILD -R '^progressive-queued-commit$' --output-on-failure
```

Qt Window на Linux запускается через частное окружение проекта:

```sh
cmake --build BUILD --target foxicmp-continuous-player-tests
bash qt/tests/run_tests.sh BUILD MEDIA -R '^continuous-player$'
```

Публичные сценарии `queued-seek`, `queued-seek-video`, `queued-seek-reordered`,
`queued-races` и `queued-stress` остаются в `tests/progressive_aac_package_test.py`.
Для Classic тестовая копия должна иметь безопасный INI до первого запуска;
процедура из `AGENTS.md` обязательна. Windows и Linux имеют отдельные результаты.
