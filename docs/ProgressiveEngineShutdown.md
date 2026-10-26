# Завершение decoder/core и ожидание Classic graph

SDK 1.12. Изменения FoxicMP, без правок YouTubeHarvester.
Следующий этап: [SDK 1.13 — async progressive Classic close](ProgressiveClassicClose.md);
ниже сохранено описание исходного этапа 1.12. Общий Qt-код применяется
на Linux и Windows; Classic сохраняет свой COM/DirectShow путь владения.

## Qt: native lifetime отдельно от QWidget

Stop/loadfile уже используют asynchronous libmpv commands. Новый этап переносит
блокирующий `mpv_terminate_destroy` из деструкторов Player и AudioMirror в отдельную
ограниченную process-owned очередь. Освобождение программного renderer также
выполняется в worker. Команды AudioMirror теперь отправляются асинхронно; синхронные
property get/set в остальном API не объявляются неблокирующими.

`MpvLifetime` сохраняет handle, software render context, stream factories и текущий
ProgressiveSource до окончания native teardown. Прежде чем удалить QWidget, UI
отключает его как цель callback, отменяет чтение, отправляет asynchronous Stop
и передаёт владение в очередь. Даже если cleanup задержан, штатно работающий core
получает запрос остановки до начала native teardown; это ещё не подтверждение
закрытия файлов.
Wakeup/redraw userdata относится к MpvLifetime, а не к удалённому Player. Поздний
wakeup не обращается к QWidget; queued Qt-вызовы привязаны к его QObject-контексту.
Мьютекс callback-цели не удерживается при вызове libmpv, чтобы не создавать
reentrant lock dependency.

Для этой очереди максимум **16** active/queued/closing cores и до **2** workers.
Резервирование происходит до mpv_create; при исчерпании новый decoder получает
ошибку, не создаёт ещё один native core. Очередь независима от SourceRetirement
(32 Source / 2 workers), обе используют общий шаблон ограниченного retirement.
Это лимиты количества объектов, не общий предел памяти.

GL renderer освобождается в потоке владельца с нужным current OpenGL context.
Перенос GL free в произвольный worker был бы нарушением контракта libmpv.
Новые изменения **не** объявляют это GL-ожидание устранённым. Контракт сверялся с
локально используемыми libmpv headers: `client.h` (thread safety,
mpv_terminate_destroy) и `render.h` (Threading, software/GL и context_free).

```json
{
  "engineRetirement": {
    "maxEngines": 16,
    "workerLimit": 2,
    "nativeDestroyOffUi": true,
    "softwareRendererFreeOffUi": true,
    "openGlRendererFreeOffUi": false,
    "callbackDataRetained": true,
    "processOwned": true
  }
}
```

Это capability обеих Qt-редакций. Она не присваивается Classic.
Неуспешная инициализация также освобождает native core в этой очереди.
Нет UI shutdown join очереди; при завершении всего процесса остаётся OS/PID recovery,
а не обещание опустошить любой зависший native job за фиксированное время.

## Classic: служебные сообщения при ожидании, исходное COM-владение

В ожидании abort начального открытия жёсткий WaitForSingleObject заменён на
GraphMessageWait. Он, как и прежнее ожидание закрытия, пропускает sent-message
handshakes, нужные STA/DirectShow. Общий helper используется в обоих ожиданиях.
Posted commands, timers, opens и quit не извлекаются из очереди, чтобы не запускать
новый граф внутри уничтожения прежнего.

Командная строка, пришедшая как sent message во время abort/close, копируется
из borrowed payload и откладывается как owned posted request. Остатки owned
payload очищаются при уничтожении окна. Повторное закрытие не входит в abort wait
рекурсивно; закрытие окна откладывается до выхода из текущего close. Master API-запросы в этой фазе отклоняются, новые открытия не входят в старый graph.
Штатные страницы, темы, defaults и регистрации Classic сохраняются.

```json
{
  "graphShutdown": {
    "owner": "classic-sta",
    "abortOpenWaitPumpsSentMessages": true,
    "closeWaitPumpsSentMessages": true,
    "nativeTeardownDetached": false,
    "fullInputPump": false
  }
}
```

**Classic teardown остаётся синхронным для вызывающего кода.** Само освобождение
графа остаётся в его штатном STA worker/владельце с живым CMainFrame; обычный UI
input не получает полного асинхронного обслуживания в этом ожидании. Следующий
шаг для Classic — отдельная close-state machine с удержанием окна и отложенным
start/exit; его нельзя заменить detach потока, продолжающего обращаться к окну.

## Release и границы

Логическое исчезновение shared_ptr/weak_ptr не доказывает физическое уничтожение
объекта: custom deleter уже мог поместить его в очередь. Native closed token в
тестах означает возврат libmpv teardown; reader released требует ещё фактического
закрытия файлов Source и подтверждения reader protocol. В Qt текущий Source
удерживается core до окончания native teardown. Timeout/Stop/удалённый QWidget
не дают загрузчику разрешение удалять файлы.

Весь Window shutdown не квалифицирован как неблокирующий: GL cleanup, некоторые
QObject/helper/QProcess cleanup, иные синхронные API, native I/O/драйверы остаются
отдельными границами. Физическое оборудование/Wayland не возвращаются в проверки.
Codec/live/remux-матрица и реальный Harvester не расширялись.
[Проверки и хеши трёх сборок](ProgressiveEngineShutdownValidation-2026-10-26.md).
