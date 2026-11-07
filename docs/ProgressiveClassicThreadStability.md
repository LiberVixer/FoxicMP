# Classic AAC: происхождение дополнительных потоков и проверка после очистки

Причина наблюдавшегося 7 → 10 установлена в новом повторе того же строгого
сценария. На пятом paused expiry второй preparing endpoint снова превысил
мгновенный лимит +2, exit 1. Шесть потоков со стартом в EXE остались шестью;
число потоков со стартом в **ntdll!TppWorkerThread** выросло с одного до четырёх.
После 123 s простоя общее число потоков вернулось к **7**, а число дескрипторов —
к исходным **427**. Это временный прирост потоков пула Windows внутри процесса.
[Данные, native exits, точные адреса, SHA и воспроизведение](ProgressiveClassicThreadStabilityValidation-2026-11-07.json).

Исходный строгий прогон сохранён как **FAIL** и не переписан. Дополнительный
профиль с проверкой после очистки прошёл всю серию: пять функциональных случаев
и **12 последовательных paused expiry** на тех же двух PID, exit 0. Это
ограниченный профиль H.264 B3 / 30000/1001 + AAC-LC stereo 48 kHz, обычный
production EXE и bundled SDK 1.84; приложение 0.2.0.

## Фактический строгий повтор

| Замер второго процесса | Всего threads | Старт в EXE | TppWorkerThread | Handles |
|---|---:|---:|---:|---:|
| Baseline после пяти функциональных случаев | 7 | 6 | 1 | 427 |
| Пятый paused expiry | 10 | 6 | 4 | 433 |
| После 123 s простоя | 7 | 6 | 1 | 427 |

Число threads прочитано **до** инвентаризации. Для таблицы baseline/failure
выбраны инвентаризации с теми же PID и сохранёнными исходными метриками;
количество записей совпало с первоначальным счётчиком. Это два близких замера,
а не атомарный снимок всех параметров ОС. Последующие 20 пассивных замеров и
120 s простоя не превращают строгий FAIL в PASS. Процессы живы и имеют прежние
PID во время всей диагностики; suspend и product hooks не используются.

`TppWorkerThread` установлен по точному S_PUB32 адресу с нулевым displacement
в публичном PDB Microsoft. GUID совпадает с CodeView DLL, DBI age совпадает с
image age (1), PDBI age (4) не меньше image age — правило
[Microsoft PDB OpenValidate4](https://github.com/microsoft/microsoft-pdb/blob/master/PDB/dbi/pdb.cpp).
Секции PE/PDB по RVA совпали, OMAP отсутствует. DLL/PDB SHA, серверные URL и код
чтения MSF/DBI/S_PUB32 сохранены в JSON; сами системные DLL/PDB в репозиторий
не добавляются. DbgHelp в отдельной попытке вернул 126 и не использован для
окончательного вывода. Форматы сверены с [документацией LLVM](https://llvm.org/docs/PDB/DbiStream.html)
и [описанием CodeView Microsoft](https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h).

Пул выполняет callbacks приложения и управляет своими рабочими потоками;
один thread count в момент закрытия не определяет наличие устойчивой утечки.
[Описание пула Windows](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-pools).
Причина, по которой были поставлены конкретные callbacks, здесь по стеку не
устанавливалась. Доказан тип прироста и его возврат в данном профиле, а не
отсутствие любых callback/handle/memory leaks во всех случаях.

## Дополнительный профиль устойчивости

Старый `aac-heartbeat` сохраняет немедленный +2 и прежнее поведение. Новый
`aac-heartbeat-idle-stability` доступен только для Windows Classic:

- Все пять функциональных и 12 paused expiry обязательны, deadline настоящий
  30000 ms. Автоматический release/filesClosed, worker exit, decoder close,
  late events, SHA, cache removal и forget проверяются прежним helper.
- Handles +8, RSS/private +256 MiB, средний parent CPU ≤1 ядра, каждый RPC и
  наблюдаемый release <2 s сохраняются как немедленные ограничения.
- Каждое мгновенное превышение threads +2 записывается отдельно, вместе с PID,
  метриками и инвентаризацией. В итоговом result остаётся собственный
  `instantThreadGate`, даже когда дополнительный профиль успешен.
- После последнего close/forget два процесса наблюдаются ещё минимум 120 s.
  Во время этого ожидания нет status/heartbeat запросов или новых jobs.
  После интервала общее число threads должно быть ≤ baseline +2. Остальные
  ресурсы контролируются и во время простоя. Порог +2 не увеличивается;
  **момент проверки отличается от старого мгновенного критерия**.

Критерии и SHA нового теста сохранены до запуска; результат содержит ту же SHA.
Это новая проверка устойчивого роста, а не успешный повтор исходного gate.

| Метрика | Первый процесс | Второй процесс |
|---|---:|---:|
| Threads baseline | 15 | 7 |
| Peak threads после отдельных cases | 16 | 9 |
| Threads после простоя | 11 | 8 |

Мгновенный thread gate нового прогона: **PASS**, записано превышений: 0.
Простой: 123.359 s; max RPC 0.172 s; max error → release 0.203 s.

Серия подтверждает отсутствие остающегося после указанного интервала прироста
выше заданных лимитов в этом ограниченном профиле. Она не заменяет многочасовой
soak, все AAC layouts/rates, физическую A/V синхронизацию или проверку полного
stress/OS I/O fault matrix. Второй endpoint готовит source; независимый playing
endpoint уже проверялся в предыдущем cyclic/growing срезе.

## Воспроизведение и сохранность

Публичный CLI: `tests/progressive_aac_package_test.py --edition classic
--scenario aac-heartbeat-idle-stability` с `--player`, `--reader-runner`,
`--sdk-directory`, `--report-directory`. Код нового профиля —
`tests/progressive_continuous_aac_heartbeat_stability.py`; read-only Windows
инвентаризация — `tests/progressive_thread_observation.py`. По умолчанию остальные
сценарии и строгий heartbeat-критерий сохраняются. SDK и форматы API не менялись.

Проверены отдельные Classic-копии с ContextMenuPrompted=1 / ContextMenuEnabled=0
до запуска; Formats не открывались. Сохранены 583 защищённых файла, установленные
EXE/INI и 158 HKCU command/icon entries. Две собственные одноразовые задачи
удалены; микшер, мониторы и desktop не изменялись. Новых бинарников не было,
переустановка Linux не требовалась. Harvester не менялся.

Прежние MPCVR Event growth и Linux release >2 s — отдельные открытые случаи;
этот результат их не закрывает. Wayland и отложенное оборудование не проверялись.
