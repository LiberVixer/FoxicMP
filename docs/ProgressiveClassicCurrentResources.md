# Classic: текущая сборка, 100 циклов и 12 настоящих heartbeat timeout

Проверенная отдельная сборка Classic прошла **100 измеряемых циклов +4
прогревочных и 12 последовательных paused expiry**, native exit **0**.
На протяжении всей серии использовались те же два приватных процесса.
[Измерения, лимиты, SHA и native exit](ProgressiveClassicCurrentResourcesValidation-2026-11-07.json).
Это текущий ограниченный video-only результат, не объяснение исторического
heartbeat-12 resource FAIL и не завершение общей стресс-приёмки.

## Что проверено

Обычный полный Classic EXE 0.2.0 с bundled SDK 1.84, без product hooks.
Сборка уже содержит DWM redraw fix и изоляцию continuous jobs от обычной
истории файлов; её SHA: `5e74cbf3393df202c3d9704f05fa682d486689660ff4ec9a9ade03b9b86444a7`.
Опубликованные assets 1.84 и установленные Windows EXE не заменялись.

Local MP4/H.264 video-only, partial source 12 из 24 s. По 25 циклов:
Stop, producer cancel при паузе, readerRelease, дописывание до complete
с seek/resume/Stop. Параллельно второй процесс играет независимый completed
900 s source. Это контроль независимости playback, не просмотр всех 900 s
и не 900 s растущей загрузки. Audio/AAC и физический A/V здесь не проверялись.

Проверены playing с движущейся позицией и decoder reader, worker exit,
положительные filesClosed receipts, исчезновение cache после release,
forget, неизменность source SHA и отказ поздним writer events.
После lifecycle контрольный playback закрыт, затем снят heartbeat baseline.
Все 12 expiry используют реальный 30000 ms deadline без подмены часов
и без Stop/Cancel до доказанного автоматического release.

## Измерения

| Наблюдаемый максимум | Результат | Лимит |
|---|---:|---:|
| Ready → playing | 0.657 s | <3 s |
| Команда → released/sourceFilesClosed | 0.313 s | <2 s |
| API request | 0.203 s | <2 s |
| Heartbeat helper automatic release | 0.141 s | <2 s |
| Первый error reply → первый released reply | 0.141 s | <2 s |

Последний интервал измеряется между клиентскими ответами, не от внутреннего
момента истечения deadline. Значение 0 означает оба признака в одном ответе.
Полные baseline и все 12 resource snapshots записаны до assertions.

| Lifecycle, max циклов 31–40 → max последних 10 | Первый процесс | Второй процесс |
|---|---:|---:|
| Handles | 524 → 521 | 1042 → 1038 |
| Threads | 16 → 15 | 34 → 33 |
| Средний CPU последних 10, ядра | 0.423 | 0.051 |

Прежние resource budgets сохранены: lifecycle handles +8, threads +2
к циклам 31–40 и +8 к первым 10; RSS/private +256 MiB, средний CPU ≤1 ядра.
Все heartbeat snapshots уложились в немедленные +8 handles /+2 threads
и +256 MiB относительно baseline после закрытия контрольного playback.
Первый и второй процессы выполняют разные роли; их абсолютные handles
друг с другом как показатель утечки не сравниваются. Observer пассивный,
20 ms, дополнительных player RPC не добавляет, ошибок observer нет.

## Границы результата

[Исходный heartbeat-12 FAIL](ProgressiveContinuousLifecycleStress.md)
сохраняется: стенд тогда не записал baseline и неуспешный handles snapshot.
Новый PASS не устанавливает его причину. [DWM Event leak исправлен и отдельно
проверен](ProgressiveDwmEventLifetime.md), связь с тем исходным отказом неизвестна.
[AAC cold-baseline thread FAIL](ProgressiveClassicThreadStability.md) относится
к другому профилю и этим video-only прогоном не переклассифицируется.
Общие stress/OS I/O faults, AAC layouts и длинные A/V серии остаются открытыми.

## Воспроизведение и сохранность

CLI: `tests/progressive_continuous_observation_probe.py --mode lifecycle
--edition classic --cycles 100 --heartbeat-repeats 12` с двумя `--player`,
`--runtime-directory`, `--worker`, `--reader-runner` и новым пустым
`--report-directory`. Все десять SDK-модулей загружены из проверенного
stage/integration до импорта стенда; их SHA включены в отчёт.

Windows-прогон выполнен интерактивной одноразовой задачей; native exit
сохранён отдельно от exit SSH driver. Задача удалена. Приватные копии имели
ContextMenuPrompted=1 / ContextMenuEnabled=0 до запуска; Formats не открывались.
Обе тестовые копии удалены. Все **583** защищённых файла и **158** HKCU
command/icon entries неизменны. Установленные профили, микшер, desktop,
Harvester и настройки Windows не менялись. Изменения этого этапа только
документационные; новая сборка/переустановка Linux не требуется.
