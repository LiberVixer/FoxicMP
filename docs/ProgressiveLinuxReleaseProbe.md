# Linux: диагностика задержки освобождения continuous preview

7 ноября 2026. **Первоначальный Linux release >2 s остаётся OPEN.**
Новый стенд проверен на обычном истечении heartbeat и на отдельной управляемой
блокировке реестра читателей. Production code, EXE, SDK и настройки не менялись.
[Результаты, исходные ошибки, SHA и коды завершения](ProgressiveLinuxReleaseProbeValidation-2026-11-07.json).

## Что добавлено

`tests/progressive_linux_release_probe.py` оборачивает прежний публичный
heartbeat-сценарий `paused`, не сокращая deadline 30000 ms и бюджет release 2 s.
Загружаются именно десять SDK-модулей из указанного пакета. Плееры получают
отдельные профили; второй endpoint продолжает свой heartbeat независимо.

Наблюдатель не добавляет RPC к плееру. Он сохраняет:

- Существующие запросы/ответы с началом и концом каждого вызова в `rpc.jsonl`.
- Начальный `/proc` snapshot и снимки около deadline с периодом 50 ms:
  PID/starttime, TID, comm, wchan, syscall, schedstat и пути открытых media/receipt FD.
- Привязку Linux x86-64 syscall numbers к локальному системному заголовку.
- Отказы доступа к `/proc`, исчезновение потоков между чтениями и ошибки observer.
- SHA EXE, SDK и тела теста до сценария; JSONL сбрасывается на диск по мере записи.

Снимки нескольких потоков не атомарны. Само наблюдение и запись диагностических
файлов могут влиять на расписание; это диагностический стенд, не эталонный
benchmark. Причина первичного conformance отказа сохраняется даже при вторичной
ошибке observer cleanup. Каталог отчёта должен быть новым.

## Обычные повторы

Сохранённый EXE исторического случая имеет SHA
`a778a55088e8c2a1ca6a9565ca4158b4ac7769d4023ffe63013b1e57a13cba42`.
Текущий проверенный EXE —
`876f2a7be70eeb17812e69cd2c3660bbecc7b0ec14c22c7a39ea38eb256fc4ff`.
Для старого EXE использованы текущие совместимые runtime/worker/SDK: это
сравнение executable, **не восстановление всех условий старого сеанса**.

Сначала выполнены два paused expiry на каждом EXE, затем ещё один текущего.
После уточнения обработки исключений observer выполнен основной повтор:

| Основной сценарий | Native Python exit | Результат |
|---|---:|---|
| Прежний EXE, обычный paused expiry | 0 | Release/filesClosed в пределах 2 s |
| Текущий EXE, обычный paused expiry | 0 | Release/filesClosed в пределах 2 s |
| Текущий EXE, удержание `.guard` на 3 s | 1 | Deadline release нарушен; исходный FAIL сохранён |

Итого **семь обычных paused случаев PASS**, включая дополнительные повторы.
У предыдущих дополнительных запусков отдельно записан CTest exit; прямой
native Python exit не записывался и в отчёте не выдумывается. У основного повтора
каждый child returncode сохранён отдельной обёрткой. Смешанная CTest-серия имеет
**exit 8**, поскольку намеренно воспроизведённый отказ не помечен `WILL_FAIL`
и не превращён в успешную latency-приёмку.

## Управляемое воспроизведение блокировки

Опция `--registry-hold-seconds 3` — отдельная fault injection, по умолчанию 0.
Тест открывает только `.guard` своего случайного private registry с `O_NOFOLLOW`,
захватывает `LOCK_EX` за 0.5 s до deadline и держит его 3 s. Фиксируются имя,
device/fileId, моменты lock/unlock. В `finally` блокировка снимается, FD закрывается,
поток теста завершается. Установленные endpoint/registry не используются.

Во всех трёх таких попытках наблюдался тот же класс отказа: source/cache не
подтверждали release в течение 2 s после первого `writer-heartbeat-timeout`.
Это заданное препятствие для сохранения receipts; оно не доказывает причину
первоначального отказа без инъекции.

В основной попытке `/proc` показывает **два потока player в
`locks_lock_inode_wait`**, syscall **flock(fd, LOCK_EX)**. По открытым FD оба
указывают на тот же private `.guard`, который удерживает тест. Флаг неблокирующего
захвата отсутствует. Эти наблюдения согласуются с единственным явным
`guard.lock(true)` в shared reader code — `ReaderLease::filesClosed()` в
`shared/progressivereaderlease.h:285`. Это сопоставление с исходниками;
C++ call stack в данном эксперименте не восстанавливался.

Публичный status уже сообщает `writer-heartbeat-timeout`, но сохраняет
`released=false`, `sourceFilesClosed=false`. Во всех сохранённых 2-секундных
release-trace ответах положительного освобождения нет. Это консервативное
поведение: закрытие native handle и подтверждение durable receipt — разные
стадии. Обходить блокировку и выдавать успешный release без квитанции нельзя.

API остаётся отзывчивым: в основном fault-повторе максимальный наблюдаемый RPC
менее 0.49 s. Следовательно, здесь воспроизведена задержка подтверждения очистки,
а не доказанное зависание GUI. После снятия блокировки выполняется штатный
failure cleanup через SDK close/forget; это уже не измерение автоматического
release до вмешательства клиента.

## Сохранность и границы

583 защищённых EXE/INI/SDK/архивных файла совпали до/после. Профили, системный
микшер и сеанс рабочего стола не менялись. Все GUI/audio запуски — через
`qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio; Wayland не проверялся.
Windows и Classic не запускались, новые результаты для них не заявляются.
Harvester не менялся. Production build и переустановка Linux не нужны.

Исходный исторический FAIL остаётся OPEN: в нём нет записи владельца lock или
снимков потоков, поэтому нынешнюю инъекцию нельзя объявить его восстановлением.
Следующий естественный отказ теперь можно отличить по стадиям: ожидание
регистрационного lock, file I/O, ожидание callback/reader либо задержка
планировщика. Успешные повторы и контролируемая блокировка не закрывают общий
2-секундный бюджет, зависший OS I/O или полную stress/A/V приёмку.

## Запуск

Добавить в отдельный CTest driver вызов:

```text
python3 -B tests/progressive_linux_release_probe.py \
  --player /absolute/runtime/foxicmp-qt \
  --reader-runner /absolute/build/foxicmp-progressive-reader-tests \
  --sdk-directory /absolute/runtime/integration \
  --report-directory /absolute/new-report --repeat 2
```

Затем запускать driver через `qt/tests/run_tests.sh DRIVER MEDIA`, с доступными
частными test dependencies в PATH. Для отдельного fault-воспроизведения добавить
`--registry-hold-seconds 3 --repeat 1`, использовать новый каталог и сохранить
его ненулевой exit. Порог heartbeat и release не увеличивать.


## Последующий срез — 12 ноября 2026

[Закрытие output reader переработано](ProgressiveReceiptClose.md): registry receipt I/O больше не держит cache mutex, остальные native output handles закрываются независимо. [Новая проверка](ProgressiveReceiptCloseValidation-2026-11-12.json) сохраняет deliberate 3s-lock release FAIL; обычный и 0.8s-lock expiry прошли. Исторический первоначальный отказ этим не атрибутирован, original-source/зависший OS I/O отдельно остаются OPEN.
