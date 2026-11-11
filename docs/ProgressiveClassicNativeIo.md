# Classic: отмена ожидающего native I/O

11 ноября 2026. Следующий ограниченный этап после
[SyncRead/ReadV2 и graph reentry](ProgressiveClassicReadBoundaries.md).
Изменён файловый адаптер Windows Classic. App 0.2.0, SDK 1.87 и Qt-код
сохранены. [Результаты, native exits и SHA](ProgressiveClassicNativeIoValidation-2026-11-11.json).

## Найденная ошибка и исправление

Предыдущее исправление проверяло поколение flush до/после чтения. Однако при
ERROR_IO_PENDING адаптер мог бесконечно ждать event, уже сброшенный EndFlush.
Внутренний BeginFlush→EndFlush не обязан совпасть с моментом вызова wait.
Тогда E_ABORT появлялся только после завершения самого I/O.

Теперь SyncRead проверяет захваченное поколение перед ожиданием и повторяет
проверку после каждого 50 ms timeout. Активные cancel/flush/breaker events
продолжают будить wait сразу. Обнаруженное прерывание вызывает CancelIoEx
именно для текущего OVERLAPPED, затем GetOverlappedResult с ожиданием завершения.
Возврат E_ABORT происходит после этого подтверждения. Буфер запроса и event
не освобождаются, пока OS может ими пользоваться. При совпадении завершения
I/O с cancel приоритет E_ABORT сохраняется; ложный source error не записывается.

Это интервал проверки состояния в одном активном запросе, не периодическая
задача или новый таймер приложения. 50 ms не являются гарантией верхней
эксплуатационной latency: планирование потока и подтверждение отмены драйвером
могут занимать дольше.

Microsoft указывает, что [CancelIoEx не ждёт завершения операции](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
и OVERLAPPED нельзя освобождать раньше её завершения.
[Overlapped pipe I/O](https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output)
позволяет воспроизводимо получить настоящий ERROR_IO_PENDING без оборудования.
Эта проверка не доказывает отменяемость запроса неисправного дискового драйвера.

## Native стенд

Fixture открывает schema-1 source через настоящий OpenPublished с двумя
разными локальными fMP4 video/audio files. После начального refresh watcher
останавливается для задания точного порядка. Другой OpenPublished context
держит независимые file handles/reader lease и остаётся работоспособным.

20 regular-file случаев:

- Чтение 64 байт по offset 0 и 65536, а также 131072 байт по offset 0.
- На каждой границе после настоящего чтения: обычный успех, source cancel,
  flush, breaker и полный flush cycle.
- Физическое усечение video file до 64 байт после подтверждения metadata:
  пять тех же вариантов запроса 131072 байт. Без прерывания — E_FAIL и source
  error; с прерыванием — E_ABORT без ложного source error.

Ещё 18 случаев используют настоящий локальный Windows byte pipe с overlapped
server handle. **Только private friend** подставляет его вместо уже открытого
video handle; публичный start по-прежнему принимает обычные локальные файлы,
нового API для pipe нет. Это не реальный media source и не disk I/O stall.

ReadFile реально возвращает ERROR_IO_PENDING. В двух точках — сразу после
возврата ReadFile и прямо перед wait — заданы обычное завершение, cancel,
flush, breaker, полный flush cycle, одновременно completion/cancel, short read,
закрытие writer и закрытие writer вместе с flush. Hooks сохраняют исходный
GetLastError до выполнения тестового callback.

В каждой строке сохранены HRESULT, число pending reads/waits, итоговый OS code,
факт завершения OVERLAPPED до возврата, recovery после reset и закрытие/forget
двух readers. Строгий driver требует точные ERROR_OPERATION_ABORTED (995),
ERROR_BROKEN_PIPE (109) или ERROR_SUCCESS (0) своего сценария. Независимый
reader читает исходные 64 байта; оба временных track files можно удалить.

До исправления два flush-cycle случая требовали rescue write через две
секунды. Это механизм безопасного завершения провалившегося стенда, а не
допуск теста: `rescueWriteRequired=true` означает FAIL. После исправления
ни один случай rescue не использует. Двухсекундный предел проверяет отсутствие
зависимости от будущих данных writer; он не задаёт playback latency budget.

| Проверка | Случаи | Native / collector exit | Итог |
| --- | ---: | --- | --- |
| Инструментированный адаптер до исправления | 38 | 1 / 0 | 2 FAIL, ожидаемое воспроизведение |
| Исправленный адаптер, строгий OS oracle | 38 | 0 / 0 | PASS |
| Прежний ReadV2/SyncRead стенд на новом адаптере | 31 | 0 / 0 | PASS |
| Прежний реальный DirectShow/MTA graph | 12 | все 0 / 0 | PASS, 4200 кадров совпали с FFmpeg |
| Обычный EXE, public schema-1 IPC | 2 | player 0 / driver 0 | PASS |
| Обычный EXE, queued AAC/video/reordered API | 11 + 6 + 6 | все 0 | PASS |

## Обычный EXE и сохранность установок

Обычный Classic пересобран без private macro: новый AsyncReader.obj явно
связан с проверенными предыдущими normal app objects/static dependencies.
Layout CHarvesterFileReader в этом этапе не менялся; прежние HarvesterSession
и FGManager objects совместимы. Это инкрементальная сборка, не clean rebuild
всех библиотек. Private namespace отсутствует в normal AsyncReader.obj.

Public schema-1 проверка запускает временную копию обычного EXE с безопасным
INI, null audio и Null Video Renderer. Два сценария используют indexed MP4
с moov в начале и fragmented MP4: неполные файлы, расширение prefix,
переименование открытых файлов, complete и нормальный EOF. Проверяются также
неверные/stale sessionDirectory. Окно/IPC принадлежат тестовому процессу;
они не обращаются к установленному endpoint. Это проверка raw-file graph,
а не decoder-equivalent pipe substitution.

INI private copies до первого запуска содержит ContextMenuPrompted=1 и
ContextMenuEnabled=0. Все 587 защищённых файлов, включая установленный Linux
INI, и 158 HKCU shell entries совпали до/после этого этапа. Профиль не
откатывался к состоянию предыдущего этапа. Установленные Windows EXE/профили
не заменялись. Qt production
не менялся, нового Linux EXE нет, переустановка Linux не требуется. Harvester,
микшер/мониторы/сеанс и scheduler tasks этот этап не меняет.

Первичные setup/build отказы учтены отдельно: недоступный ещё не созданный
snapshot script, отсутствующий include SHA в fixture, отсутствие powershell.exe
в сокращённом test PATH; скалярный PowerShell argument вместо массива при
первом запуске строгого oracle. Они исправлены, их exits не засчитаны runtime PASS.
Повтор со строгим OS oracle выполнен на тех же native EXE с сохранением первых
результатов, а не только повторной интерпретацией старого stdout.

## Воспроизведение

Нужны обычные Classic x64 static dependencies, MSVC x64 Developer Command
Prompt, Windows Python и FFmpeg в PATH. Из корня checkout:

```bat
tests\windows\build_classic_native_io_probe.cmd _bin\classic-native-io-probe
python -B tests\windows\classic_native_io_test.py --runner _bin\classic-native-io-probe\native.exe --report-directory _bin\classic-native-io-results
python -B tests\windows\classic_raw_file_api_test.py --player PRIVATE_STAGE\FoxicMP64.exe --reader-runner READER_TEST_EXE --report-directory REPORT
```

Build script создаёт безопасный native.ini до первого запуска. Вывод — новый
private каталог, не каталог установленного плеера. Public API driver сам
копирует stage и настраивает только временную копию.

Для воспроизведения двух FAIL в отдельной temporary source copy применить
`git apply --unidiff-zero tests/fixtures/classic-native-io/pre-fix.patch`,
пересобрать fixture отдельно и передать read driver `--expect-pre-fix`.
Reverse patch проверен на точное совпадение байтов исходника red-сборки;
основной checkout остаётся исправленным.

## Что остаётся открытым

Не удерживался настоящий disk/device driver и не моделировался драйвер,
который не подтверждает CancelIoEx. Drain такого запроса всё ещё может
зависнуть; освобождать используемую OS память ради таймаута нельзя.

Этот этап покрывает уже подтверждённый диапазон и native ReadFile. Ожидание
ещё не опубликованного диапазона в WaitRange, WaitForSample/WaitForCompletion,
детерминированные after-read границы другого v2 bridge/AAC projections,
CMainFrame/STA/public IPC reentry, полная codec/crash/race/stress matrix и
физическая A/V синхронизация остаются отдельными проверками. Прежние outliers,
quota 16 и receipt retention не изменены. Public API tests используют реальные
interleavings и не задают внутренний порядок graph.
