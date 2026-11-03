# Progressive: 100 циклов настоящего плеера в каждой редакции

Проверка от 3 ноября 2026 года, приложение 0.2.0, контракт SDK 1.81.
Машиночитаемые измерения, SHA-256 исходников/бинарных файлов и точные exit codes:
[ProgressivePlayerLifecycleValidation-2026-11-03.json](ProgressivePlayerLifecycleValidation-2026-11-03.json).
Это дополнительные maintenance-исправления в текущих исходниках. Уже опубликованный
SDK 1.81 ZIP и его release assets не заменялись; новые установщики ещё не опубликованы.

## Что проверяется

Один процесс каждой редакции выполняет четыре прогревочных и 100 измеряемых
циклов. Каждый раз создаются новая сессия, reader и отдельный локальный исходник.
Чередуются четыре RF64-профиля: PCM16/PCM24, stereo 48 kHz, обычный и sparse hint.
Изначально подтверждены четыре секунды из двадцати, затем двенадцать; исходник
остаётся недокачанным. Sparse hint здесь не заменяет отдельную проверку
[физических дыр за 4 GiB](ProgressiveLargeSparseRf64.md).

Каждый цикл действительно воспроизводит минимум 0,25 секунды: подтверждаются
playing, позиция и decoder I/O. В Qt дополнительно наблюдаются реальные codec,
частота, число каналов и время libmpv; всего 104 decoder observations на ОС.
Каждый десятый цикл проверяет pause → seek 2 s → resume.

Остановка инициируется командой **кнопки Stop**, после чего read-only status
должен вернуть `filesClosed` менее чем за две секунды. SDK reader release/forget
отправляется только после этого подтверждения. После завершения teardown
замеряются RSS/private bytes, handles и threads; проверяется SHA исходника,
затем файл и его каталог удаляются. Поэтому успешное API принятия команды
само по себе не считается ни воспроизведением, ни освобождением файла.

Все 300 измеряемых циклов прошли; вместе с прогревом — 312 сессий.
Фактические значения по каждой редакции находятся в JSON-отчёте.

| Редакция | Циклы | Старт, медиана | Stop → filesClosed, max | Handles после закрытия | Exit |
|---|---:|---:|---:|---:|---:|
| Linux Qt | 100 + 4 | 2.238 s | 0.150 s | 36–36 | 0 |
| Windows Qt | 100 + 4 | 0.419 s | 0.066 s | 1646–1654 | 0 |
| Windows Classic | 100 + 4 | 0.375 s | 0.094 s | 501–504 | 0 |

Бюджеты были заданы **до исправления обнаруженных утечек** и не ослаблялись:
максимум handles последних десяти циклов не выше максимума циклов 31–40 +8;
threads — +2 к установившемуся уровню и +8 к раннему; рост RSS/private ≤256 MiB.
Это критерии конкретного прогона, а не доказательство отсутствия любых утечек.
CPU time также сохраняется в каждом измерении; длительный CPU stress здесь
не оценивается. Отдельные источники метрик Linux/Windows указаны в отчёте.

## Найденные и исправленные дефекты

**Classic:** до исправления два Thread HANDLE терялись при каждом открытии
и остановке. Process-local handle tracing и символы PDB указали на
`CAMThread::Create` → `CBaseSplitterFilter::Pause`. Splitter и output pin
обнуляли `m_hThread` перед ответом EXIT, оставляя kernel reference незакрытым.
Теперь общий локальный helper `BaseSplitterThread.h` делает atomic exchange
и `CloseHandle`. Он сохраняет прежний порядок публикации выхода; если
`CAMThread::Close` уже забрал HANDLE, повторного закрытия нет. Сторонний
подмодуль BaseClasses не изменяется: чистый recursive checkout содержит
всё исправление в исходниках FoxicMP.

**Windows Qt:** до исправления сохранялись семь Event HANDLE на цикл,
при стабильном числе потоков. Отдельный тест из 100 detached pthreads с TSD
воспроизвёл +100 handles в штатном `libwinpthread-1.dll`. Его detached exit
освобождал `p_clock`, но пропускал `pthread_spin_destroy(&tv->spin_keys)`
перед повторным использованием структуры потока. Добавлен именно этот вызов.
После исправления отдельные detached и joined тесты дают нулевой прирост,
а 100 циклов настоящего Qt-плеера укладываются в прежний бюджет.

Private DLL собирается из [pinned upstream source](https://github.com/mingw-w64/mingw-w64/tree/63ebe6cdb3ec4d5c586b7f0f0c03dd502e44c296/mingw-w64-libraries/winpthreads)
с проверкой SHA-256 архива и точного patch. Сохранены все 159 исходных export names
(всего 172); зависимости — Windows/UCRT, без MSYS/Cygwin и self-import.
Две независимые сборочные папки дают одинаковый DLL SHA-256
`4118e2fc18e5edb2a2074cdf27ada9bdc42645d6659c43f7ae2ea7baa215a123`.
Фиксированные image base/timestamp и prefix maps исключают зависимость от пути
сборки. `build.ps1` создаёт DLL только в build directory, а `deploy.ps1`
проверяет metadata/hash и включает её в portable package после windeployqt.
Системная/toolchain DLL и установленный Windows Qt не заменялись.

## Регрессии и воспроизведение проверки

Qt CTest: `progressive-player-lifecycle` на обеих ОС; на MinGW Windows также
`progressive-pthread-detached` и `progressive-pthread-joined`. Linux запускается
через `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio.
Общий writer: `tests/progressive_player_lifecycle.py`; Classic adapter:
`tests/windows/progressive_family_test.py --player-lifecycle-cycles 100`.
Используются отдельные профили, выключенный звук, software output теста.

После исправлений Windows Qt дополнительно прошёл growing AAC stereo и H.264;
Classic — growing H.264/AAC, AAC mono и RF64 PCM24 stereo, включая pause/seek,
complete/EOF/release. `classic_normal_lifecycle.py` отдельно проверил пять
Stop/Play обычного завершённого видео через штатный MPC remote clock API,
без progressive API. Сборки, конечные тесты и упаковка завершились с exit 0.
Portable Qt проверен с чистым Windows PATH командой `--version`.

Первичные ошибки тестового transport (unwrapped SDK status и Windows file
sharing), неудачные прогоны до исправлений и stacks сохранены в отчёте.
Immutable acknowledgement/decoder files устранили гонку QSaveFile; настоящий
Qt event loop сохраняет штатную доставку событий. Диагностика с одним лишь
исправлением event loop **не** устранила Windows leak и не засчитана как fix.
Опциональные Windows trace hooks не включены в конечных 100-cycle runs;
публичный отчёт содержит module-relative stacks без kernel addresses.

Проверены неизменность установленных EXE/INI обеих Windows-редакций, Linux
EXE/INI и всех 158 Classic shell command/icon entries. Временные Classic задачи,
staging-копии и оставшийся тестовый QSaveFile temp удалены после завершения
процессов. Harvester не менялся. Linux production EXE совпадает с установленным;
новый Linux тестовый harness не требует замены пользовательского плеера.

## Что остаётся

Это 100 смен источника/декодера в одном приложении, а не 100 холодных запусков.
Короткие циклы не заменяют десятисекундную positive matrix и 30-минутный A/V
прогон. Ещё открыты длительные RAM/cache/temp/CPU испытания, несколько readers,
crash/disk-full/recovery, остальные codec/container combinations и итоговая
поставка. Полный перечень — [чеклист](ProgressiveCompletionChecklist.md).
EOF на предельном RF64 16 GiB и физическая синхронизация A/V здесь не проверялись.
