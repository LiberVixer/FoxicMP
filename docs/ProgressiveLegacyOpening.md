# Фоновое открытие legacy/provider источника

SDK 1.10, Linux Qt / Windows Qt / Windows Classic. Изменения только в FoxicMP;
YouTubeHarvester не изменяется. Сохраняются `--harvester-session` / `/harvester-session`,
manifest/provider wire-протокол, независимые профили и endpoints трёх редакций.

## Подготовка и применение

GUI/CLI-путь открытия legacy-сеанса больше не выполняет manifest read, provider
attach, открытие медиа и начальную индексацию в UI. Общий `SourceOpening<Source>`
владеет входами и результатом рабочего потока; worker не содержит указателей
на окно, API или незавершённые Win32 message payloads.

На окно допускается одна legacy-подготовка. Занятый слот отклоняет новый legacy-open;
это отдельный слот от IPC mediaPreparation и registry. Предыдущий просмотр остаётся
до успешной подготовки. Готовый результат применяется UI-потоком, который закрывает
прежнее медиа и подключает новый Source к mpv/DirectShow.

Stop, Close, смена файла/источника и закрытие окна запрещают применение старого
результата. Начало принятого IPC start также отменяет pending legacy-open, чтобы
порядок окончания I/O не переопределял новое намерение пользователя. Probe и update
действующего источника сами по себе новым просмотром не являются.

После 5 секунд результат больше не применяется. Это deadline допуска к playback,
а не принудительная отмена native I/O. Заблокированный worker продолжает владеть
своими объектами; слот занят до его фактического завершения. Возвратившийся поздно
Source освобождается через ограниченный [SourceRetirement](ProgressiveRetirement.md).
Окно не делает join рабочего открытия при уничтожении.

```json
{
  "legacyPreparation": {
    "offUi": true,
    "maxConcurrent": 1,
    "responseDeadlineMilliseconds": 5000,
    "cancellableDiskIo": false,
    "previousPlaybackUntilCommit": true
  }
}
```

Runtime `capabilities.legacyPreparationPending` показывает занятый слот, включая
отменённое открытие, которое всё ещё ждёт I/O. Это диагностика, не подтверждение
закрытия файлов. В read-only `--progressive-api-info` есть описание возможностей;
текущее состояние следует получать у запущенного экземпляра через IPC.

В Qt внутренний `Window::openProgressiveSession()` возвращает **допуск запроса**.
Окончательный результат приходит через `progressiveOpeningFinished(bool, error)`;
`progressiveOpening()` остаётся true до освобождения слота. Сигнал ошибки/отмены
может прийти раньше освобождения слота. Ошибка показывается в статусной строке,
не блокирует UI модальным окном. У Classic ошибки/таймаут также выводятся в статус.
Прямые низкоуровневые `ProgressiveSource::open` и `CHarvesterSession::Open`
сохраняют синхронный смысл; GUI вызывает их из worker.

## Release и границы

Отмена до attach может вообще не создавать provider lease. Если worker успел
подключиться, освобождение всё равно следует после закрытия ресурсов. Закрытое
окно, `legacyPreparationPending:false` и истёкший timeout не разрешают загрузчику
удалять файлы: сохраняются подтверждения provider/reader leases и OS recovery.
Для опубликованных SDK-сеансов действуют readerStatus/readerRelease.

Этот этап не выносит из UI все renderer/graph joins и не исправляет все старые
блокировки watch/refresh/notify legacy-источника. Синхронные внутренние C++ API и
часть CLI parsing/проверок файлов также не объявляются полностью свободными от I/O.
Нет гарантии прекращения любого системного read/open за 2 секунды.
Codec/live/remux-матрица прежняя; [полное ТЗ](ProgressiveIntegrationPlan.md) не закрыто.
[Стенды и хеши сборок](ProgressiveLegacyOpeningValidation-2026-10-26.md).
