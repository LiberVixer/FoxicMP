# Очередь перемотки: дополнительные AAC-профили и 100 циклов

11 ноября 2026. Расширение [конкурентных queued seek tests](ProgressiveQueuedSeekRaces.md)
после [исправления completion при аварии worker](ProgressiveSeekWorkerFailure.md).
Приложения/SDK modules не меняются: обычные три редакции 0.2.0 и SDK 1.87.
Harvester не затрагивается.
[Результаты, native exits, resource windows и сохранность](ProgressiveQueuedProfileStressValidation-2026-11-11.json).

## Дополнительные профили

Раньше этот queue/race/stress driver использовал AAC-LC stereo 48000 Hz,
H.264 BF3/30000/1001. Теперь тестовый launcher принимает необязательный
`--queued-profile`; он допустим только для queued-races и queued-stress.
Без опции сохраняется прежний генератор и набор действий.

| Профиль | Audio | Video | Presentation samples |
| --- | --- | --- | ---: |
| 48000-mono | AAC-LC, 48000 Hz, mono | H.264 BF2, 25 fps, 160×90 | 1151863 |
| 44100-stereo | AAC-LC, 44100 Hz, stereo | H.264 BF3, 30000/1001 fps, 160×90 | 1058263 |

Video длится около 24 s, audible audio = rate×24−137. Для каждого prepare
проверяются настоящий audioWindowReady, частота/channels и точное число
presentation samples; playing требует применённого PCM window adapter.
Это проверка metadata/adapter и реального playback, не sample-value oracle
или измерение физического аудиовыхода.

Данные физически уже находятся в локальном файле, но подтверждён только
prefix до packet на 12 s. Тест публикует новые ranges через публичный SDK.
Это рост подтверждённого диапазона, не настоящая сетевая загрузка и не тест
writer append/syscall внутри I/O. Исходные bytes не должны меняться.

## Действия и лимиты

Для каждой редакции и каждого профиля выполняется прежний набор из 36
парных command races: target/target, cancelSeek, Stop, cancel job, readerRelease,
ordinary replacement, producer failure, publish/cancel и publish/target.
Начало двух отдельных IPC-соединений синхронизируется barrier, смещения —
0 и ±3 ms. Это наблюдаемый порядок публичных команд; внутренний decoder/file
порядок не управляется и не считается полностью покрытым.

Затем те же два main PID проходят четыре warmup и 100 измеряемых циклов.
В каждом цикле новый job действительно играет и ставится на паузу;
16 пар замены/отмены недоступной цели не должны тратить quota или менять
decoder readers/позицию. После публикации выполняются два настоящих growing
seek, затем Stop, job cancel, активный readerRelease либо producer failure.
В первом измеряемом цикле проверяются все 16 применений, отказ следующему
queued seek и прежний completed seek после complete.

Закрытие требует released/sourceFilesClosed, положительные files-closed
receipts, forget, пустой cache и фактический выход именно своего worker.
Второй endpoint всё время сохраняет paused position, playbackId и readerIds.
Это независимость endpoint на паузе, а не параллельный длительный просмотр.

Наследуются прежние budgets, без увеличения ради новых профилей: paired RPC
<2 s, SDK close <2 s, late handles ≤ steady+8, late threads ≤ steady+2 и
early+8, RSS/private ≤ early+256 MiB, средний CPU последних 10 циклов ≤1 ядра.
Early=1–10, steady=31–40, late=91–100; snapshots сохраняются до assert.
Порог startup 3 s из общего BUDGET этим driver отдельно не применяется:
нельзя выдавать наличие поля за проверенную latency приёмку.

## Повторение

Использовать отдельную test copy Classic с безопасным INI, обычные Qt EXE,
native reader runner, FFmpeg/FFprobe, проверенные SDK 1.87 modules и новые
report directories. Пример одного сценария:

```sh
python3 -B tests/progressive_aac_package_test.py --edition qt --player /PRIVATE/foxicmp-qt --reader-runner /PRIVATE/foxicmp-progressive-reader-tests --sdk-directory /PRIVATE/integration --scenario queued-stress --queued-profile 44100-stereo --expected-audio-output pulse --report-directory /PRIVATE/NEW_REPORT
```

Для Linux команду регистрировать в частном CTest driver и запускать только
через `qt/tests/run_tests.sh` с private X11/D-Bus/PulseAudio. Windows Qt
проверяется отдельно с собственными plugins и private profile. Classic —
`--edition classic --classic-null-video --expected-audio-output null`,
ContextMenuPrompted=1 и ContextMenuEnabled=0 до первого запуска.
Formats/Apply не открываются; установленный INI не используется как тестовый.

## Результаты

Все 12 прогонов прошли: по 36 races и 100 измеряемых +4 warmup cycles двух
профилей каждой из трёх редакций. Всего 216 races, 600 измеряемых и 24 warmup;
10200 пар IPC с учётом warmup. Linux CTest — 4/4 PASS, Windows — все восемь
native Python drivers exit 0. Max paired RPC=0.537 s, max SDK close=0.813 s,
оба меньше прежних 2 s. Это наблюдаемые максимумы, не p95 или общий допуск.

Для основного процесса steady → late maxima:

| Редакция / профиль | Handles (Linux: fd) | Threads |
| --- | ---: | ---: |
| Linux Qt / 48000 mono | 39 → 39 | 61 → 61 |
| Linux Qt / 44100 stereo | 36 → 37 | 58 → 59 |
| Windows Qt / 48000 mono | 1697 → 1696 | 37 → 37 |
| Windows Qt / 44100 stereo | 1697 → 1692 | 37 → 36 |
| Classic / 48000 mono | 479 → 469 | 16 → 13 |
| Classic / 44100 stereo | 477 → 469 | 15 → 13 |

Memory/CPU budgets и второй endpoint тоже прошли; абсолютные показатели разных
ОС и ролей не сравниваются как мера утечки. Полные snapshots до assertions и
SHA исходных отчётов сохранены в JSON. Короткий конечный PASS не доказывает
ограниченность ресурсов при любом числе циклов.

Idle main peers закрываются cleanup harness через kill: Windows exit 1,
Linux -9, отдельно от driver exit 0. В Windows сохранены 556 worker exits 1
(отмена/внедрённая ошибка неполного источника) и четыре exits 0 при завершении
quota-control source. Linux pidfd доказывает завершение, но не предоставляет
native worker exit code; ему не приписывается 0. Каждый driver сохраняет
исходные файлы и требует положительные files-closed receipts до cleanup.

Все 587 защищённых файлов и 158 HKCU shell entries совпали до/после.
Установленный Linux EXE уже содержит исправление предыдущего этапа и здесь
не заменялся. Новых ошибок плеера в этих профилях не обнаружено.

## Границы

Два новых AAC-LC rate/channel/B-frame профиля не закрывают HE-AAC, любые layouts,
hardware/COM callbacks, зависший disk I/O, crash/power loss, multi-hour stress
или физический A/V drift. Classic проверяется обычным EXE с private null
renderers, Qt — штатным software/audio backend в тестовом окружении.
Wayland, физическое оборудование и меню дисков в этот этап не входят.

Проверяются готовые EXE предыдущего исправления; новые binary не создаются.
Linux тестирует установленный EXE через отдельные профили, Windows — отдельные
copies. INI/личные файлы, shell registration, микшер, дисплеи и desktop session
не меняются. Новая установка Linux не требуется. Scheduler tasks не создаются.
