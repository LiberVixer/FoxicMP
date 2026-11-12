# Закрытие continuous readers при занятом реестре квитанций

12 ноября 2026. FoxicMP Classic и обе FoxiqMP qt, версия 0.2.0,
контракт/SDK 1.87 без изменений. [Отчёт с исходными FAIL, кодами завершения,
SHA и трассами](ProgressiveReceiptCloseValidation-2026-11-12.json).

## Найденная причина

`StreamOutputStore::Reader::close()` закрывал native media handle, затем
вызывал блокирующий `ReaderLease::filesClosed()` под mutex кэша. Внешнее
удержание `.guard` задерживало status и закрытие следующих readers.
Это конкретная воспроизводимая причина блокирования, не доказанная причина
исторического единичного release >2 s без сохранённого владельца lock.

Новый native тест удерживает private registry lock три секунды, запрашивает
закрытие двух readers и одновременно status. До исправления status занял
2.967 s, второй native media FD оставался открыт. На текущем обычном Linux
EXE воспроизведён также Stop с удерживаемым output FD и последующим timeout
SDK close; отрицательные коды 1/CTest8 сохранены.

## Исправление

Файлы закрываются под mutex кэша; lease переносится в ограниченную очередь
не более MaxReaders. Запись receipts выполняется отдельно от cache mutex с
неблокирующим захватом registry lock. Lease/его lock сохраняются до успешной
записи. Повтор использует существующий controller worker и cache retirement;
нового потока, периодического таймера или изменения пользовательского профиля нет.

`released` не становится true по одному закрытию файла. Пока квитанция не
сохранена, activeReaders остаётся консервативным, cleanupCode сообщает
`reader-receipt-pending`; cache не удаляется. После снятия lock квитанции
подтверждаются штатным files-closed и cache освобождается. Если приложение
закрыто раньше, восстановление требует настоящего OS process-exit proof;
оно не подменяется files-closed. Прежние блокирующие original-source пути
этим изменением не объявлены исправленными.

## Проверки

Native GCC, MinGW и MSVC проверяют тот же настоящий lock; GCC повторён с
ASan/UBSan. На Linux status занял 0.477 ms, оба native output FD закрыты,
квитанции во время lock оставались отрицательными. После снятия lock release
занял около 9 ms. Reader registry не заменялся и не обходился.

Обычные main EXE всех трёх редакций проверены на Stop, Cancel, WM_CLOSE,
параллельно продолжавшийся второй endpoint и восстановление private profile.

| Редакция | Stop / Cancel command | Normal window close | Release после unlock |
| --- | ---: | ---: | ---: |
| Linux Qt | 0.244 / 0.009 s | 1.588 s | ≤0.023 s |
| Windows Qt | 0.016 / <0.001 s | 1.625 s | ≤0.032 s |
| Classic | <0.001 / <0.001 s | 1.547 s | ≤0.017 s |

Субмиллисекундные Windows значения ограничены разрешением часов стенда,
не означают нулевую стоимость. Бюджет RPC под lock — 0.5 s, native close/status
— 0.5 s, window close — 2 s. Начальный Windows harness с SDK timeout 0.2 s
получил named-pipe deadline timeout; заменён прямой cancel acknowledgement,
а application budgets сохранены. Финальная матрица повторена полностью.

Прошли Linux AAC/queued-seek/races/shutdown, native managed AAC/cache,
Window/commit проверки и 100 измеряемых AAC lifecycle cycles. Строгий memory
gate 32/8 MiB сохранён: total рост ≤6.03 MiB, late/steady ≤2.74 MiB.
Windows Qt и Classic прошли по 64 прежних public cases плюс три новых.
Детерминированные 25 cache I/O cases/505 checks также прошли.

Настоящий paused heartbeat 30000 ms без fault прошёл. При удержании registry
на 0.8 s автоматический release уложился в 2 s. При удержании на 3 s строгий
release gate закономерно получил FAIL (native Python1 / aggregate CTest8);
он не объявлен PASS и не помечен WILL_FAIL. После снятия lock cleanup штатный.
Прерывания дополнительных запусков без child exit отдельно перечислены в JSON;
они не считаются завершёнными проверками.

Linux main EXE заменён атомарно с backup, INI и личные файлы сохранены.
На установленной Linux-копии ещё 3 contention и 9 AAC cases прошли с exit0.
Windows-установки не заменялись; их профили и все 163 текущих HKCU shell keys
совпали до/после. Из 587 защищённых файлов меняется только Linux EXE.
Тесты Classic использовали отдельные INI с ContextMenuPrompted=1 и
ContextMenuEnabled=0; Formats/Apply не открывались. Harvester не менялся.

## Воспроизведение

```text
python3 -B tests/progressive_receipt_close_contention.py --runner CACHE_TEST_RUNNER --worker STREAM_WORKER --report-directory NEW_REPORT
python3 -B tests/progressive_aac_package_test.py --edition qt --scenario receipt-close --player NORMAL_PLAYER --reader-runner READER_RUNNER --sdk-directory SDK_DIRECTORY --expected-audio-output pulse --report-directory NEW_REPORT
```

Для Windows Qt output=wasapi; для Classic edition=classic,
`--classic-null-video --expected-audio-output null`. Linux UI/audio запускаются
только через `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio.
В CMake зарегистрированы progressive-receipt-close-contention и
progressive-continuous-receipt-close, обе проверки прошли.

## Оставшиеся границы

Исторический необъяснённый release >2 s, original-source receipt wait,
зависший native file I/O, полная codec/COM/renderer/race/stress matrix и
физическая A/V синхронизация остаются OPEN. Положительный ответ о приёме
Cancel не равен подтверждённому освобождению. Внешний lock дольше бюджета
не позволяет обещать durable receipt раньше его снятия.
