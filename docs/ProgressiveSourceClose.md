# Continuous remux: закрытие исходника без блокировки status/cancel

12 ноября 2026. Общий код Classic/Windows Qt/Linux Qt, версия 0.2.0,
SDK 1.87 без изменения публичного контракта.
[Полный отчёт, сохранённые FAIL, SHA и native exits](ProgressiveSourceCloseValidation-2026-11-12.json).

## Причина и границы

После [исправления output reader](ProgressiveReceiptClose.md) оставался другой
путь: `StreamRemuxSession::run()` вызывал `source.reset()` под mutex сессии.
Деструктор `ResourceSource` закрывал pinned files и записывал filesClosed
receipts. Если `.guard` занят, ожидание записи блокировало native status/cancel
и обновление controller snapshot. Старый snapshot мог продолжать показывать
producerCode=waiting после отмены.

Контролируемый native тест на неполном H.264/AAC источнике удерживает настоящий
private registry lock на 3 s. До исправления status ждал около 2.85 s.
Контроль обычного main EXE также показывает устаревший waiting после Stop/Cancel.
Это воспроизводимая причина блокировки, не доказанная причина исторического
единичного Linux >2 s без сохранённого владельца/стека.

## Изменение

Worker фиксирует последний source snapshot и переносит исключительное владение
источником под mutex, затем отпускает mutex. Native media handles всех pins
закрываются до начала записи первой квитанции. Durable receipt persistence
выполняется вне session mutex; status/cancel остаются доступны. Входящие publish
не получают доступ к уже переданному источнику. Поздний heartbeat на пути
успешного EOF не превращает завершение источника в ошибку.

`filesClosed` и `done` выставляются только после завершения прежнего receipt
пути. При ожидании нет ложного подтверждения release, restart, нового потока,
таймера или очистки пользовательских настроек. Cache writer может оставаться
открытым до подтверждения исходника; тест отличает этот writable handle от
read-only decoder handles и original source fd.

Это не отмена зависшего OS disk I/O и не универсальная гарантия durable release
за 2 s при lock, который удерживается дольше двух секунд. Original receipt I/O
всё ещё может ждать, но больше не держит mutex continuous-remux status/cancel.

## Проверки

Native GCC: status во время lock — 0.283 ms вместо 2.851 s, cancel —
0.209 ms; original FD уже закрыт, sourceFilesClosed остаётся false до unlock.
Тот же сценарий прошёл ASan/UBSan, MinGW и MSVC.

| Обычный EXE | Stop / Cancel | Window close | Release после unlock |
| --- | ---: | ---: | ---: |
| Linux Qt | ≤0.207 s | ≤1.594 s | ≤0.044 s |
| Windows Qt | <0.001 s (разрешение часов) | ≤1.594 s | ≤0.048 s |
| Classic | <0.001 s (разрешение часов) | ≤1.563 s | ≤0.047 s |

Обе нативные Windows-сборки и GCC проверяют дополнительный успешный EOF:
физический append остатка исходника, явный writer complete, ожидание receipt
под registry lock и настоящий heartbeatWhileRunning. Во время ожидания done/
filesClosed=false, heartbeat не продлевает сессию; после unlock итог остаётся
code=data с положительными files-closed receipts. Это не mock native EOF.

У Linux прошли native resources, 17 remux-parent cases, 25 I/O-boundary cases/
505 checks, 22 managed AAC и 34 cache cases, Window/commit tests, 64 public
AAC/seek/races/shutdown scenarios и 100 измеряемых lifecycle cycles. Строгий
memory gate 32/8 MiB прошёл: рост early→late ≤4.88 MiB, late/steady ≤1.31 MiB.
Отдельно прошёл настоящий 30000 ms paused heartbeat ordering.
Windows Qt и Classic прошли по 64 прежних public cases плюс source-close3 и
прежний receipt-close3. Все конечные test drivers/native cases — exit0.

Linux EXE атомарно обновлён с backup/неизменным INI и личными файлами.
Windows-установки сохранены, 163 HKCU shell entries совпали до/после. Из 587
защищённых файлов изменён только Linux EXE. Отдельные Classic copies имеют
ContextMenuPrompted=1/ContextMenuEnabled=0; Formats/Apply не открывались.
На установленной копии дополнительно прошли source-close3, receipt-close3 и
AAC9 с native exits0; результаты включены в итоговый JSON.

Сохранены ошибки исходного стенда: повторное имя private source folder исправлено
на UUID; импорт SDK для standalone native test исправлен; первоначальный oracle
ошибочно считал retained cache writer декодерным reader. После исправления
проверяется фактический access mode FD, отсутствие original/decoder handles и
своевременная публикация producer cancellation. Старый EXE проваливает исправленный
oracle на stale producerCode; исправленный проходит. Ненулевые exits не переписаны.

## Воспроизведение

```text
python3 -B tests/progressive_source_close_contention.py --runner CACHE_TEST_RUNNER --worker STREAM_WORKER --report-directory NEW_REPORT
python3 -B tests/progressive_aac_package_test.py --edition qt --scenario source-close --player NORMAL_PLAYER --reader-runner READER_RUNNER --sdk-directory SDK_DIRECTORY --expected-audio-output pulse --report-directory NEW_REPORT
```

Windows Qt: output=wasapi. Classic: edition=classic,
`--classic-null-video --expected-audio-output null` с отдельными ContextMenuPrompted=1
и ContextMenuEnabled=0. Linux — только `qt/tests/run_tests.sh` с private
X11/D-Bus/PulseAudio. CMake: progressive-source-close-contention и
progressive-continuous-source-close.

Исторические неизвестные причины, actual OS I/O cancellation, полная
codec/COM/renderer/reentry/stress и физическая A/V приёмка остаются OPEN.
Harvester не меняется; hardware/Wayland не возобновляются.
