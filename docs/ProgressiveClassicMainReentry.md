# Classic: главное окно, публичный IPC и повторный вход в seek

11 ноября 2026. Проверочный этап после
[границ чтения v2/AAC](ProgressiveClassicProjectionIo.md).
App 0.2.0 и публичный SDK 1.87 сохранены; Harvester не менялся.
[Native exits, трассы, fingerprints и сохранность](ProgressiveClassicMainReentryValidation-2026-11-11.json).

## Что проверено

Предыдущий graph fixture имел собственный MTA owner и подкласс source filter.
Теперь запускается настоящий CMainFrame с обычными ClassicProgressiveApi,
continuous queue transaction, FilterGraph и внутренним MP4 splitter. Source
создаётся через реальный remux worker и публичный SDK. Сначала подтверждена
только часть 24-секундного H.264 BF0 файла. Обычное воспроизведение начинается,
плеер ставится на паузу, запрашивается ожидающий seek на 21 секунду. Остальные
bytes физически уже существуют, но становятся доступны только после publish.

В специальной сборке есть четыре compile-only точки:

1. `frame-before`: перед final CanCommitQueuedSeek/SetPositions в CMainFrame.
2. `graph-before`: внутри настоящего вызова SetPositionsInternal splitter,
   до его основной работы и блокировки.
3. `graph-after`: при выходе из того же stack frame после освобождения локов.
4. `frame-after`: после успешного graph SetPositions, до проверки актуальности
   транзакции и последующих UI side effects в CMainFrame.

На каждой точке проверены cancelSeek, новая цель 1 секунда, cancel/requeue
той же цели 21 секунда и вложенный timer poll. Всего 16 основных случаев.
Через штатный named-pipe endpoint проходят настоящие публичные команды;
не подменяются ContinuousPlayback и ответы API. Вложенный poll запускается
адресным sent WM_TIMER только главного окна принадлежащего тесту процесса.

Во всех трассах caller совпал с владельцем HWND, CoGetApartmentType вернул
S_OK/APTTYPE_MAINSTA (3), то есть [главный STA приложения](https://learn.microsoft.com/en-us/windows/win32/api/objidlbase/ne-objidlbase-apttype). Отдельный CGraphThread
(`FoxicMP graph STA`), выполняющий open/close, не выдаётся за этот поток:
исследуемый synchronous seek проходит на владельце главного окна.

## Важная граница стенда

Для воспроизводимости private barrier **сам обрабатывает вложенные запросы**,
пока внешний seek находится в стеке. Он извлекает только
WM_CLASSIC_PROGRESSIVE_REQUEST для своего HWND; PeekMessage также допускает
обычную обработку sent messages, включая явный timer poll теста.
[Фильтр PeekMessage относится к queued messages](https://learn.microsoft.com/en-us/windows/win32/winmsg/messages-and-message-queues),
а sent messages того же потока могут обрабатываться системой независимо от
этого диапазона; driver адресует только свой HWND. Другие posted команды и
таймеры стенд явно не извлекает. WM_QUIT — исключение фильтра PeekMessage;
его поступление внутри barrier этим тестом не проверяется.

Это принудительный adversarial reentry, а не доказательство, что обычный
COM или renderer сам открывает такую петлю. Обычный main loop не менялся,
shipping nested pump не добавлялся. Не моделируется любой возможный порядок
COM callbacks, произвольное уничтожение graph, Stop/replacement/exit внутри
stack frame, hardware renderer или чужие окна. Barrier с пределом 15 секунд
сам снимается при сбое driver; его timeout не является успешной проверкой.

## Результаты и квота

Все 16 сценариев прошли; отдельный предварительный smoke также прошёл.
Повтор собран воспроизводимым tracked build helper и проверен отдельно.

- Отмена до handoff не передаёт seek графу и сохраняет 16 слотов.
- Новая revision до handoff заменяет старую: исполняется только новая цель,
  остаётся 15 слотов.
- После передачи старого seek graph нельзя отозвать уже принятую операцию:
  отмена сохраняется, старый completion не записывает `applied`, остаётся 15.
- Новая цель или новая revision той же численной цели внутри/после graph call
  сохраняется и применяется следующим внешним проходом; остаётся 14 слотов.
- Вложенный timer poll не начинает вторую транзакцию: глубина frame и graph
  не превышает 1, ревизия/квота внутри barrier ещё не изменены commit старой цели.

Проверяется тот же playbackId, номер revision, pending/state, фактическая
paused position у применённой цели, остаток квоты и trace глубины/потока.
После снятия барьера driver дожидается выхода внешнего FrameScope, чтобы
не принять промежуточный nested reply за окончательный результат.

После каждого сценария SDK close даёт released/sourceFilesClosed, forget
подтверждает удаление job. Основной тестовый процесс выходит с native exit 0;
Peer cleanup проверяет оставшиеся reader receipts и забывает только закрытые.
Профили/временные runtime paths принадлежат этим private copies.

## Исключение private кода из обычной сборки

Hooks существуют только при `FOXICMP_PRIVATE_CLASSIC_SEEK_TEST`. Этот macro
не добавлен в shipping projects. Включения header без macro пусты; normal
main/api/splitter preprocessing до/после сравнен по C++ tokens в MSVC с
отключённым PCH. Все три пары совпали. Checker также прошёл свои 13 controls,
включая literal/whitespace и leaked-hook negative checks. Private идентификаторы
и строка переменной окружения отсутствуют в normal preprocessed output.

Header/guards не меняют normal layout или API. Общие cache/decoder headers
не получают конфликтующих inline definitions. Для instrumented EXE явно
пересобраны MainFrm, ClassicProgressiveApi и BaseSplitter; PCH main/api
отключён, свежий splitter object явно перекрывает archived implementation.
Остальные objects/static dependencies — из coherent normal build предыдущего
этапа. Это инкрементальная private сборка, не clean rebuild всех библиотек.

Первый preprocessing launcher остановился из-за MSVC warning в PowerShell
ErrorActionPreference=Stop: macro сначала задавался, затем снимался /U.
Driver exit 1 не засчитан PASS; перед повтором macro убран из flags, native
stderr не превращается в исключение до сохранения LASTEXITCODE. Также первый
вариант disabled header оставлял лишний `#pragma once`; он перемещён внутрь
private ifdef. Итоговое token comparison проверяет конечный normal вариант.
Для предварительного full-text diff был остановлен только собственный
необязательный процесс (exit 143); вывод этой попытки не заменяет token proof.

## Сохранность

Все private Classic INI до первого запуска содержат ContextMenuPrompted=1 и
ContextMenuEnabled=0. Форматы/Apply не открывались. Null audio и Null Video
Renderer настроены только во временных copies. Все 587 защищённых файлов
и 158 HKCU shell entries совпали до/после. Настройки установленного Classic,
Windows Qt, Linux Qt и прежние defaults не нормализовались автоматически.

Обычные application binaries не пересобирались и не устанавливались: normal
C++ tokens не изменились. Нового Linux binary нет, переустановка не требуется.
Harvester, desktop mixer/мониторы/сеанс и scheduler tasks не менялись.
Периодические/одноразовые задачи не создавались.

## Воспроизведение

Нужны coherent normal Classic x64 objects/static libraries, normal player
stage с worker/DLL, original MSVC compile response files MainFrm и API, его
link response file, x64 Developer Command Prompt, Python и FFmpeg/FFprobe.

```bat
python -B tests\windows\build_classic_main_reentry_probe.py --normal-objects NORMAL_OBJECTS --normal-player NORMAL_STAGE\FoxicMP64.exe --main-rsp MAIN_RSP --api-rsp API_RSP --link-rsp LINK_RSP --output NEW_PRIVATE_BUILD
python -B tests\windows\classic_main_reentry_test.py --player NEW_PRIVATE_BUILD\player\FoxicMP64.exe --reader-runner READER_TEST_EXE --report-directory NEW_REPORT
```

Build helper сохраняет native exit/log каждого compile/link, создаёт безопасный
INI. Output/report должны быть новыми private каталогами. Driver сам создаёт
отдельные copies, привязывает endpoint и HWND к точному PID. Переменная окружения
FOXICMP_PRIVATE_SEEK_DIRECTORY существует только у owned private test process.
Обычный EXE её не читает и не получает новых CLI/API capabilities.

Полная codec/AAC/physical-I/O/COM/renderer/replacement/crash/race/stress matrix,
физическая A/V синхронизация, старые resource/latency outliers и расширение
bounded quota/receipt retention остаются открытыми. Этот этап подтверждает
устойчивость текущей транзакции в заданных main/graph reentry boundaries;
новая production ошибка в этих 16 случаях не найдена.
