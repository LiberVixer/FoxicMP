# Continuous-remux: очистка кэша при нормальном выходе

Исправлен порядок завершения Linux Qt, Windows Qt и Classic. Ранее фоновое
удаление continuous jobs могло не успеть закончиться до выхода процесса: handles
закрывались, а journaled cache оставался до следующего запуска. Это зафиксировано
[исходным прогоном](ProgressiveContinuousShutdownValidation-2026-11-04.json).
[Новые результаты, SHA и exit codes](ProgressiveContinuousExitCleanupValidation-2026-11-04.json).

## Изменение

Общий controller получил `settleOnExit(timeout)`. После уничтожения окна, API и
владельцев декодера Qt вызывает его между выходом из event loop и возвратом из
main. Classic вызывает его в ExitInstance после уничтожения frame/graph/API.
Максимум ожидания в обоих адаптерах — **1500 ms**. Обычные деструкторы, Stop и
замена видео по-прежнему отправляют работу в фон и не получают нового ожидания.
Если continuous jobs никогда не создавались, новый путь возвращается сразу,
не создавая retirement workers только ради выхода.

Retirement завершается только после существующих проверок настоящего закрытия
source/output readers, квитанций и идентичности private cache. Срок ожидания не
считается release proof. При его превышении выводится диагностика, журнал остаётся
для консервативного recovery следующего запуска. Кэш с живым reader принудительно
не удаляется. Никаких TTL-удалений, новых таймеров, UI-настроек или API actions нет.

## Проверка

Усилен `tests/progressive_continuous_shutdown_conformance.py`: опция
`--require-clean-normal-exit` требует отсутствия старого cache **после выхода и до
повторного запуска**. Проверяются восемь сценариев каждой редакции: нормальное и
принудительное закрытие при preparation, playing, paused и buffering.

Все 24 сценария прошли, включая отсутствие кэша после всех двенадцати нормальных
выходов. Нормальное закрытие заняло 0,062–0,235 s. После crash сохранён прежний
консервативный recovery; независимый процесс продолжает играть с теми же IDs,
writer дописывает настоящий partial, а новый явный Play использует новые IDs.
Проверки original SHA, старых событий, OS worker exit и receipts сохранены.

Обновлён native controller-тест: удерживаемый reader препятствует завершению
`settleOnExit(50 ms)` и удалению кэша; после настоящего close следующий wait
успешен. Деструктор самого controller остаётся быстрым. Этот сценарий входит в
19 controller rows Windows MinGW и Linux GCC. Общий код также собран MSVC в
настоящем Classic, проверенном отдельными IPC/decoder сценариями.

Полный выбранный набор регрессий Qt прошёл отдельно на Linux и Windows:
по 12 CTest-групп, оба exit code 0. Включены reader/controller, GUI playback и
retirement, registry UI, bounded remux, continuous API/playback, SDK unit/playback,
паузы writer 10/30/60 s и настоящий heartbeat timeout. Это не замена отдельной
Classic-проверке: для него выполнены сборка MSVC и восемь IPC/decoder close/crash
сценариев выше.

Linux UI/audio проверяется через `qt/tests/run_tests.sh` с приватными
X11/D-Bus/PulseAudio. Classic использует защищённые отдельные EXE/INI, без открытия
«Форматов»; команды/иконки установленного контекстного меню сверяются до/после.
Windows-установки и их настройки не заменяются; Harvester не меняется.

## Граница результата

Это проверка ограниченного локального MP4/H.264 video-only continuous профиля,
а не обещание очистки при произвольно зависшем filesystem/driver. Timeout с живыми
или недоказанными readers сохраняет журнал. Остальные codec/container profiles,
worker crash/mid-I/O/races, внешние surviving readers и stress matrix открыты.
Первоначальный Linux heartbeat release >2 s также остаётся открытым: новый wait
при выходе процесса не изменяет путь автоматического Stop на ручной паузе.
Полная численная и интеграционная приёмка ТЗ не объявляется завершённой.
