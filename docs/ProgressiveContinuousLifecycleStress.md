# Continuous SDK 1.82: 100 lifecycle cycles и повторные heartbeat timeout

Повторно проверен ограниченный local MP4/H.264 video-only continuous-remux профиль:
**по 100 измеряемых циклов + 4 прогревочных и по 12 настоящих heartbeat timeout
в Linux Qt, Windows Qt и Classic**. Всего 300 измеряемых lifecycle cycles,
12 прогревочных и 36 expiry scenarios. [Результаты, бюджеты, метрики и SHA](ProgressiveContinuousLifecycleStressValidation-2026-11-05.json).
Исправлен тестовый heartbeat helper: наблюдения IPC и OS exit больше не считаются атомарными.
Production code/EXE не меняются; приложение 0.2.0, SDK 1.82.
**Общая приёмка этого среза остаётся открытой:** в первом прогоне Classic
получен отказ resource budget после двенадцатого expiry. Успех повторного
прогона не объясняет этот отказ; подробности ниже.

## Текущая отдельная Classic-сборка, 7 ноября

[Строгий повтор 100 циклов +12 expiry](ProgressiveClassicCurrentResources.md)
на сборке с DWM/history fixes и bundled SDK 1.84 прошёл с native exit 0.
Лимиты не менялись; heartbeat baseline и каждый resource snapshot сохранены
до assert. Это новый video-only срез текущей сборки, не доказательство причины
первого отказа без baseline. Общая приёмка и AAC остаются отдельными задачами.

## Итоговые измерения повторного прогона

| Редакция | Max ready → Play | Max команда → release | Max heartbeat auto-release | Exit |
|---|---:|---:|---:|---:|
| Linux Qt | 0,494 s | 0,418 s | 0,239 s | 0 |
| Windows Qt | 0,531 s | 0,219 s | 0,110 s | 0 |
| Windows Classic | 0,672 s | 0,313 s | 0,234 s | 0 |

Это наблюдаемые интервалы клиента в данном тесте, не общий предел для всех
источников или физический A/V benchmark. Заранее заданные пороги не увеличены.

## Lifecycle

`tests/progressive_continuous_lifecycle_stress.py` использует те же два приватных
процесса на протяжении всей серии. В каждом цикле новый partial содержит
12 s из 24 s настоящего H.264. Prepare → Play подтверждается настоящими
`playing`, позицией >0,2 s и decoder reader. По 25 измеряемых циклов проверяют:

- Stop при воспроизведении;
- cancel producer при ручной паузе;
- readerRelease текущего output reader при воспроизведении;
- физическое дописывание source до конца, complete, seek/resume и Stop.

Готовность remux измеряется отдельно от запуска decoder. От ready до настоящего
Play — <3 s. От команды до released/sourceFilesClosed — <2 s; дополнительно
требуются нулевые decoder readers, OS exit worker, положительные source/output
filesClosed receipts, close/forget и отсутствие старого private cache. SHA source
проверяется до удаления собственного тестового каталога. Новые reader IDs не
совпадают с предыдущими. SDK close/Cancel выполняется в каждом цикле.

Второй процесс всё это время играет другой completed 900 s control source с
прежними playbackId/readerIds и растущей позицией. Это контроль независимости
просмотра, не приёмка 900 s растущей загрузки или декодирования всего файла.
После lifecycle его source/cache закрывается до начала следующей фазы.

## Бюджеты зафиксированы до запуска

Взяты прежние границы [100-cycle player lifecycle](ProgressivePlayerLifecycle.md):

| Показатель каждого parent player | Критерий |
|---|---|
| Handles/fds последних 10 циклов | ≤ max циклов 31–40 +8 |
| Threads последних 10 циклов | ≤ max циклов 31–40 +2; ≤ первых 10 +8 |
| RSS/private metric последних 10 | ≤ max первых 10 +256 MiB |
| Средний CPU последних 10 | ≤ одного логического ядра |
| Каждый записанный API request | <2 s |
| Ready → фактический playback | <3 s |
| Команда → released/sourceFilesClosed | <2 s |

Сохраняются live worker metrics и after-close metrics обоих parent процессов,
CPU time, memory source, handles/fds, threads, полные квитанции и длительности.
Linux private metric — RssAnon, Windows — PrivateUsage; они не сравниваются как
одна абсолютная величина. Память может расти внутри бюджета, что не доказывает
отсутствие всех малых утечек. Этот прогон не задаёт общий лимит RAM всей ОС или
peak CPU всех дочерних процессов.

## 12 реальных timeout после lifecycle

На тех же PID выполняются 12 последовательных paused heartbeat сценариев.
Deadline из capabilities — 30000 ms; часы не подменяются. После последнего
heartbeat отправляются обычные status-запросы; они не продлевают writer.
До доказанного release нет Stop/Cancel/readerRelease от стенда.

Повторно используется прежний heartbeat helper с его 2 s бюджетом. Дополнительно
проверяется интервал **от окончания первого ответа writer-heartbeat-timeout до
окончания первого ответа released + sourceFilesClosed** — также <2 s. Если оба
признака пришли в одном ответе, наблюдаемый интервал равен 0; это не означает
физически мгновенное закрытие. Внутреннее время от истечения producer deadline
этими клиентскими timestamp не измеряется.

Обёртка записывает start/end/duration существующих RPC обоих endpoints, их phase,
job/playback IDs, error/producer/cleanup/release fields и CPU time клиента.
Дополнительных запросов в критический путь не добавляется; файл полного trace
пишется после серии/при исключении. Прежний helper сохраняет status-only trace
до дополнительных failure diagnostics и cleanup. Поэтому можно различить
задержку целевого status, независимого endpoint или внешнюю паузу клиента.
В primary report включены критические окна; SHA полного runtime trace сохранён.

В итоговом повторе после каждого expiry подтверждены OS exit worker, decoder close, реальные
filesClosed receipts, cache removal и отказ поздним writer events. Handles,
threads и memory после этой фазы также ограничены относительно конца lifecycle.

## Первый отказ Classic и диагностические границы

Первый прогон: Linux CTest exit 0, Windows Qt exit 0, Classic exit 1,
общий Windows driver exit 1. В Classic все 100 lifecycle cycles и фактическое
освобождение после всех 12 heartbeat прошли; отказ возник затем при сравнении
моментального количества handles с baseline после lifecycle +8.
Первый вариант стенда не записывал baseline и неуспешный замер до assert.
Сохранены последние успешные метрики, исходный traceback и положительные
квитанции двенадцатого helper; точное превышение восстановить нельзя.

Отдельная диагностическая серия Classic: 40 cycles и четыре expiry, exit 0.
Трассировка handles включалась только в двух собственных тестовых процессах.
Количество handles обоих процессов 502 → 499; типизированные Event 80 → 78,
Thread 11 → 11. Это не воспроизведение отказа и не доказательство отсутствия
утечки: диагностика добавляла задержку перед замером и могла дать время очистке.
Classic закрывает graph асинхронно; различие между временными handles и утечкой
для первого отказа пока не установлено.

Улучшенный стенд сохраняет lifecycle measurements **до** проверки budgets,
heartbeat baseline и каждый замер **до** assert. При новом ресурсном отказе он
запишет ещё 20 пассивных замеров с шагом 100 ms. Эти замеры не могут превратить
FAIL в PASS; пороги +8/+2, deadlines и рабочие команды не изменены. Повтор
всех трёх редакций использует один SHA исходника. Baseline failure сохраняется
в primary report и не заменяется итоговыми PASS. Ни исправление production
кода, ни доказанная причина первоначального отказа здесь не заявляются.

## Гонка наблюдения heartbeat в тестовом helper

В повторе с подробным журналом Linux прошёл 100 lifecycle cycles и четыре
expiry; пятый остановился на проверке «worker ещё жив», exit CTest 8.
Последний status вернул ready/no error через 30,018 s от отправки heartbeat;
следом пришёл paused playback status, затем OS-проверка увидела завершённый
worker. Полученный ранее снимок IPC и более поздний OS exit не атомарны.
Такой порядок допустим на границе настоящих 30 секунд. Уже первая команда
failure-cleanup вернула writer-heartbeat-timeout, но этот release не считается
автоматическим успехом: cleanup начался после проваленного assert.

Helper теперь требует прежний workerId; выход OS-процесса раньше deadline
по-прежнему FAIL. На границе deadline следующий обычный status poll должен
подтвердить именно writer-heartbeat-timeout в прежнем окне <32 s после refresh.
Прежние <2 s release, decoder close, OS exit, receipts и late-event checks
сохранены. Нет добавленной команды Stop/Cancel и нет увеличения лимитов.
Все три полные серии повторены с исправленным helper. Failed-before-fix trace,
точные времена и исходный SHA сохранены отдельно от итоговых результатов.
Это исправление стенда, **не объяснение прежнего Linux release >2 s** и
не исправление production code.

## Воспроизведение и сохранность

Qt CTest — `progressive-continuous-lifecycle-stress`, timeout 1800 s, label stress.
CLI по умолчанию: --cycles 100 --heartbeat-repeats 12. Каталог отчёта
должен быть пустым: перед повтором сохранять предыдущий каталог в архив.
Один повтор Linux CTest завершился exit 8 после 100 cycles из-за оставшегося
heartbeat-01 (FileExistsError стенда); этот отказ сохранён отдельно. Итоговый
повтор выполнен в пустом каталоге; старый result.json из смешанного каталога
не считается результатом неуспешного запуска. Linux UI/audio работает
через qt/tests/run_tests.sh с частными X11/D-Bus/PulseAudio. Windows использует
отдельные временные portable copies. Для Qt реально загруженная private
libwinpthread-1.dll проверяется по пути/хешу двух owned PID; это исправленная
версия из предыдущего этапа. Системная/toolchain DLL не меняется.

Classic copies получают ContextMenuPrompted=1/ContextMenuEnabled=0 до первого
запуска; «Форматы» не открываются. До/после сверяются 158 HKCU shell entries,
одноразовая задача удаляется. Настройки/EXE установленных приложений, публичные
SDK ZIP и Harvester сохраняются. Установленный Linux EXE уже совпадает с
проверенным; test-only изменение не требует новой сборки или переустановки.

## Граница

Исходный единичный Linux heartbeat release >2 s **остаётся OPEN**. Новая серия
его не воспроизвела и не объясняет старый отказ, для которого не было детального
release trace. Исторические результаты не заменяются успешными повторами.

Следующие незакрытые диагностические шаги: воспроизвести Classic resource
budget с уже записанными baseline/failed/follow-up значениями и установить,
какие handles остаются после graph teardown; отдельно получить воспроизводимый
status-only trace исходной Linux задержки. До этого оба случая остаются OPEN.

Это cyclic stress одного codec/container и paused expiry. Одна длительная
растущая continuous загрузка, широкий concurrent/fault/cache schedule, бесконечно
зависший OS I/O, audio/B-frames, 30-min физический A/V и полная приёмка ТЗ остаются
отдельными пунктами. Дисковые меню, оборудование, Wayland и таймеры автоматизации
не затрагиваются.
