# Classic: отмена ожидания диапазона, sample и complete

11 ноября 2026. Продолжение [native I/O-проверок](ProgressiveClassicNativeIo.md).
Изменены только Windows Classic session/reader adapters. App 0.2.0, публичный
SDK 1.87, Qt-код и Harvester сохранены.
[Native результаты, exits и fingerprints](ProgressiveClassicRangeWaitValidation-2026-11-11.json).

## Исправленные границы

Предыдущий этап защищал уже начатый ReadFile. Однако SyncRead мог сначала
ждать ещё не опубликованный диапазон в WaitRange. Этот цикл видел лишь
состояние events: полный BeginFlush→EndFlush между проверками терялся,
и старый запрос оставался зависим от будущей публикации данных.

WaitForCompletion мог вернуть успех после отмены на границе готовности.
WaitForSample мог вернуть S_OK/S_FALSE после cancel/flush/breaker и даже
записать найденный sample в буфер вызывающего кода. Один сброшенный flush
event не позволял отличить старое ожидание от нового.

Теперь SyncRead и WaitForCompletion передают своё захваченное поколение
flush в WaitRangeGuarded. Оно проверяется в каждой итерации и перед возвратом
готового результата. Старая C++ точка входа WaitRange сохранена как adapter
с прежней сигнатурой; wire API/capabilities не менялись. Empty guard прямого
C++ вызова не обещает запоминать pulse чужого event — поколение принадлежит
CHarvesterFileReader и передаётся его guarded-вызовами.

WaitForSample тоже запоминает поколение, получает sample в локальную переменную
и переносит его вызывающему коду только после последней проверки. E_ABORT и
S_FALSE не меняют caller sample. E_ABORT из ожидания диапазона не запускает
payload ReadFile и не меняет byte buffer. Обычные ready/EOF результаты
сохранены. Это граница commit результата, не обещание отозвать уже возвращённый
результат при отмене, поступившей позже.

Счётчик completionWaiters теперь удерживается объектом области видимости:
WaitForCompletion снимает его при выходе из функции. Во всех native сценариях
проверены значение 1 внутри completion wait и 0 после возврата.

Уже существовавшие интервалы ожидания сохранены: 100 ms у range/complete и
50 ms у sample. Активные events будят wait сразу; прошедший полный flush cycle
виден следующей проверке поколения. Это не эксплуатационный latency SLA:
время планирования потока, индексирования и другой I/O этим не ограничено.
Новых периодических таймеров или scheduler tasks нет.

## Воспроизводимый стенд

Fixture открывает настоящие schema-1 fMP4 video/audio files через OpenPublished.
Сначала физически записаны и подтверждены только пять media fragments.
После начального refresh watcher остановлен, чтобы задать точный порядок.
Никакие file handles не подменяются. Оставшиеся bytes дописываются в те же
файлы и подтверждаются штатным Publish с revision 2/state complete.
Независимый context удерживает отдельные handles/lease тех же track files.

Private compile-only hooks располагаются перед ожиданием и непосредственно
перед возвратом ready/EOF результата. В normal targets этих callbacks нет.
Это задаёт реальную границу session/reader, а не случайный порядок двух IPC.

38 случаев:

- SyncRead, WaitForCompletion и WaitForSample: waiting/ready × обычное
  продолжение, source cancel, flush, breaker и полный flush cycle — 30.
- Полный flush cycle одновременно с публикацией на waiting boundary — 3.
- WaitForSample на подтверждённом EOF: control и четыре вида прерывания — 5.

Проверяются HRESULT, отсутствие native read при отмене ожидания, неизменность
sample/byte buffers, новые вызовы после reset, баланс completionWaiters,
независимый reader, filesClosed/forget двух readers и возможность удалить
оба временных track files. Исходные video/audio fixtures совпадают до/после.

До исправления 17 случаев возвращали неверный результат или зависели от
rescue publication. У трёх waiting-pulse случаев стенд публиковал остаток
через две секунды, чтобы завершить старый запрос. Это FAIL, не разрешённый
fallback. В исправленном прогоне ни одному случаю rescue не требуется.
Двухсекундный предел проверяет отсутствие зависимости от будущих данных;
он не измеряет физическую A/V синхронизацию или decoder latency.

| Проверка | Случаи | Native / collector exit | Результат |
| --- | ---: | --- | --- |
| Инструментированный source до исправления | 38 | 1 / 0 | 17 FAIL, ожидаемое воспроизведение |
| Исправленный missing-range/sample/complete | 38 | 0 / 0 | PASS |
| Прежние payload/SyncRead границы | 31 | 0 / 0 | PASS |
| Прежние native regular-file/pending-pipe границы | 38 | 0 / 0 | PASS |
| Настоящий DirectShow/MTA graph | 12 | все 0 / 0 | PASS, 4200 FFmpeg-matched кадров |
| Normal EXE: queued AAC/video/reordered API | 11 + 6 + 6 | все 0 | PASS |
| Normal EXE: schema-1 indexed/fragmented graph | 2 | player 0 / driver 0 | PASS |

## Сборка и сохранность

Normal EXE собран отдельно без FOXICMP_PRIVATE_CLASSIC_READ_TEST. Явно
пересобраны HarvesterSession.obj и AsyncReader.obj; private namespace в них
отсутствует. Layout reader/session в этом этапе не менялся. Остальные normal
objects/static dependencies взяты из предыдущей проверенной сборки; это
инкрементальная сборка, не clean rebuild всех библиотек. Test EXE явно
связываются со свежими instrumented objects вместо архивных копий.

Private INI до первого запуска содержит ContextMenuPrompted=1 и
ContextMenuEnabled=0. Public API regression использует отдельные копии normal
Classic с null audio и Null Video Renderer. Все 587 защищённых файлов, включая
Linux EXE/INI, и 158 HKCU shell entries совпали до/после. Windows установленные EXE/INI
не заменяются. Qt не менялся, нового Linux бинарника нет, переустановка Linux
не требуется. Harvester, микшер, дисплеи и пользовательский сеанс не менялись.

Первый fixture выбрал индекс после последнего sync sample вместо первого
неопубликованного sample: шесть waiting cases не доходили до своего hook.
В трассе наблюдалось 21/38 FAIL, collector exit 1; этот прогон не используется
как доказательство 21 дефекта плеера. Исправленный fixture определяет первый
неопубликованный индекс через SampleAt; тот же oracle затем воспроизвёл 17 FAIL.
Также наблюдался незавершённый перенос guard из-за опечатки локального
edit-script: partial build exit 0, native 14 FAIL, collector exit 1. После
исправления пересобраны оба production units, этот прогон не засчитан PASS.

При сохранении обнаружено расхождение: после перемещения каталогов отчётов
на общей файловой системе и повторного запуска с прежними именами ранние
stdout содержат более поздние результаты. Причина не установлена. Первичные
строки 21/14 FAIL не уцелели; сохранены их native/collector exits 1, source
fingerprints и driver tracebacks. Несогласованный stdout не используется
как доказательство этих ранних отказов. Итоговые red/fixed прогоны повторены
в новых уникальных каталогах без перемещения, их stdout/result.json/exits
проверены на согласованность.

## Воспроизведение

Нужны обычные Classic x64 static dependencies, MSVC x64 Developer Command
Prompt, Windows Python и FFmpeg в PATH. Из корня checkout:

```bat
tests\windows\build_classic_range_wait_probe.cmd _bin\classic-range-wait-probe
python -B tests\windows\classic_range_wait_test.py --runner _bin\classic-range-wait-probe\wait.exe --report-directory _bin\classic-range-wait-results
```

Build script создаёт безопасный wait.ini до запуска. Вывод — новый private
каталог, не каталог установленного плеера. Для воспроизведения pre-fix в
отдельной temporary source copy применить
`git apply --unidiff-zero tests/fixtures/classic-range-wait/pre-fix.patch`,
пересобрать fixture отдельно и передать driver `--expect-pre-fix`.
Reverse patch проверен на точное совпадение байтов исходников red-сборки.

## Открытые границы

Этот этап не задаёт принудительный порядок внутри CMainFrame/STA message pump
или public IPC graph reentry. Отдельного принудительного блокирования AAC
projections/другого v2 bridge, зависшего disk/device driver, полного
codec/superseding/replacement/crash/race/stress и физической A/V приёмки нет.
Нормальные main API regressions не заменяют внутренние barriers. Внешний breaker
по-прежнему должен оставаться валидным/signaled до завершения запроса; его
короткий pulse не получает внутреннего flush generation. Старые outliers,
quota 16 и receipt retention сохранены. Полное ТЗ остаётся открытым.
