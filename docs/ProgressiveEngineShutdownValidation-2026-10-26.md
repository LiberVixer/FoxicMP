# Проверка native teardown — 26 октября 2026

[SDK 1.12](ProgressiveEngineShutdown.md). Базовый commit
`aa8b5e366ad94f713306c4f0eaa90ab770f0119e`.
[Машинный отчёт и SHA-256](ProgressiveEngineShutdownValidation-2026-10-26.json).
Harvester и установленные Windows EXE/профили не менялись.

## Результаты

| Стенд | Проверки | Итог |
|---|---|---|
| Linux Qt native lifetime | 7 QtTest результатов (5 сценариев + init/cleanup), финальное повторение | exit 0 |
| Windows Qt native lifetime | Те же 7; финальное повторение | exit 0 |
| Linux Qt ASan/UBSan lifetime | Инструментированы Player, MpvLifetime, AudioMirror, stream factories и Source-код; 7 PASS | exit 0 |
| Общая retirement-очередь | 6 проверок GCC / MinGW / MSVC; GCC ASan+UBSan с leak detection | exit 0 |
| Classic graph-message wait | Win32 sent handshake, отложенный posted command, timeout не completion; MinGW и MSVC | exit 0 |
| Linux Qt regression | API 63 PASS, legacy/source 19, controls 11; финально Player 24 PASS/1 SKIP, AudioMirror 5 PASS | См. точный порядок ниже |
| Windows Qt regression | API 64 PASS, v2 playback 3; AudioMirror 3 PASS/2 SKIP | exit 0 |
| Classic | SDK/reader/resource/v2/preparation/publication/registry regression, legacy playback, maintenance 3 и opening 4 случая | exit 0 |
| Python SDK / family contract | 35 / 2 | exit 0 |

QtTest числа включают init/cleanup. Linux — частные X11/D-Bus/PulseAudio.
Windows — отдельные профили; WASAPI/физические устройства не квалифицированы.
Два старых Windows AudioMirror-теста пропущены без гарантированного WASAPI endpoint;
новая lifetime-проверка вторичного decoder на null backend прошла и на Windows.
Player continuous playback пропущен как необязательный длительный soak.

Общий Linux прогон завершился с CTest **8** из-за одного старого queuePlayback
ожидания; API, source и controls в нём прошли. Причина и исправление фикстуры ниже.
После последнего уточнения порядка teardown (render context free перед client API
shutdown) повторены native lifetime, AudioMirror и Player на Linux, lifetime и
Win32 helper на Windows, а также ASan/UBSan lifetime. Финальные эти группы — exit 0.
После добавления asynchronous Stop перед retirement повторены lifetime, AudioMirror
и v2 playback на обеих ОС и ASan/UBSan lifetime: exit 0. Полный Player suite
24 PASS/1 SKIP предшествовал этому последнему Stop-уточнению.
Это не выдаётся за повтор всего API после последних уточнений.

## Что проверяет новый lifetime-стенд

- Обычное видео реально воспроизводится до удаления Player. Native teardown
  остановлен внутри retirement worker до render/core free. Удаление QWidget
  занимает <200 мс в управляемом сценарии, UI timer продолжает работать.
- При задержке остаются живы native handle, software render context и обе stream
  factories. Callback target уже null. Принудительные поздние wakeup/redraw
  работают с MpvLifetime и не обращаются к удалённому QWidget.
- На обоих декодерах idle-active становится true при заблокированном teardown:
  asynchronous Stop отправлен до помещения в очередь. Это не reader release.
- AudioMirror реально декодирует на null backend, затем удаляется без ожидания
  заблокированного worker. Его native handle/streams живы до teardown.
- Полный published fMP4-сеанс с reader lease воспроизводится до удаления Player.
  Source удерживается native core; пока teardown задержан, local filesClosed и
  reader released остаются false. После возврата native destroy и фактического
  закрытия Source подтверждается reader released=true.
- 16 active/closing engine slots учитываются независимо от Source pool. Следующий
  Player отклоняется до создания native core. После очистки повторный Player
  создаётся; неверная startup option не оставляет занятого engine slot.

Замеры <200 мс относятся к test gate на native teardown, не к произвольному GL,
драйверу или всему Window/QProcess shutdown.

ASan/UBSan повторяют реальные late-callback / Source-lease сценарии. Shared Qt/libmpv
не пересобраны с sanitizer; GUI leak detection отключён для внешних библиотек.
Для чистого C++ retirement-контракта leak detection включён и пройден. Проверка
не заявляет отсутствие утечек во всех драйверах/библиотеках приложения.

## Classic

GraphMessageWait проверен настоящим hidden message-only HWND и native worker,
который делает SendMessageTimeout в UI и только затем сигнализирует event.
Handshake получен, заранее posted команда остаётся необработанной до завершения
ожидания; unsignaled event возвращает timeout, не success. Проверены MinGW и MSVC.

Полный Classic regression прошёл на финальном EXE. Retained CLI/provider дошёл до
position100ns=109514537 при readyEnd=120000000 до complete, затем readyEnd=200000000,
Close и lease release. Дополнительно повторены три maintenance-сценария с реальной
доиндексацией и четыре открытия с OS oplock/Stop/Close/deadline.

Финальная тестовая копия SHA совпала со сборкой. INI заранее отключал контекстную
регистрацию; Formats не открывалась. HKCU команды/иконки установленного Classic
совпали до/после. One-shot test task удалена, LastTaskResult=0. Кооперативный wait
не выдаётся за detached Classic teardown или полноценный UI input pump.

## Промежуточные ошибки

Первый Linux build завершился с 1: при введении native lifetime остались лишняя
вложенная forward declaration и незакрытый try-блок конструктора. Исправлены;
финальные Linux/Windows сборки exit 0.
Первый lifetime-тест дал CTest 8 (2 FAIL): ошибочно использовал weak_ptr.expired
как признак native destruction. При custom deferred deleter control block уже
может быть expired, пока native объект жив. Тест теперь проверяет отдельный
closed token, реальные pointers во время gate и reader protocol после закрытия.

Первый общий Linux regression дал exit 8: queuePlayback ожидал перехода к файлу
из chapters.mkv по Next, хотя согласованное с Classic поведение сначала выбирает
главу. Для теста порядка файлов первый элемент заменён отдельной chapter-free
копией. Навигационный код и пользовательские настройки не менялись; финальный
Player suite — 24 PASS / 1 optional SKIP, exit 0.

## Установка и оставшиеся границы

Linux EXE заменён атомарно, прежний сохранён для отката, просмотр не прерывался.
INI до/после: `ac9387b8d09f63317ed8ea3b681abad96e5abe8890400d4db6540017e3acde75`.
Версия приложения остаётся 0.2.0; расширения различаются capabilities/SHA.

GL cleanup сохраняет context owner thread. Полноценный async Classic close требует
close-state machine и удержания окна; он ещё не реализован. FramePreview/QProcess
и иные синхронные UI API не квалифицированы этим этапом. Нет гарантии прерывания
любого native I/O за фиксированный срок. Codec/live/remux, real Harvester и
физические A/V-измерения не расширены. SDK ZIP не заменяет установщики 0.2.0.
