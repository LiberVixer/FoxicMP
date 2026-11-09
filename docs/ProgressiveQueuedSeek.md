# SDK 1.87: ожидание подтверждённого диапазона

Продолжение [bounded growing seek 1.86](ProgressiveGrowingSeek.md) для Linux Qt,
Windows Qt и Classic. Harvester не изменяется. Это расширение узких continuous
MP4/H.264 и H.264/AAC-LC профилей, а не поддержка любых недокачанных контейнеров.

```python
preview.control('seek', seconds=11, allow_growing=True, wait_for_range=True)
# Новый запрос заменяет ожидающую цель; в памяти хранится одна цель.
preview.control('seek', seconds=12, allow_growing=True, wait_for_range=True)
preview.control('cancelSeek')
```

Новая capability `continuousRemux.queuedSeek` имеет точный descriptor:
`version:1`, `field:continuousPlaybackControl.waitForRange`,
`requiresGrowingOptIn:true`, `maxPending:1`, `superseding:latest-target`,
`cancelCommand:cancelSeek`, `previousPlaybackUntilCommit:true`.
SDK хранит отдельный immutable снимок descriptor и проверяет его до отправки.
`wait_for_range` и raw `waitForRange` — строгие bool; true требует seek и
`allowGrowing:true`. Старые запросы без этого параметра сохраняют поведение
1.85/1.86: неподтверждённый growing target отклоняется с `waiting-range`.

Позиция должна быть конечным числом 0 ≤ seconds < 3600 и раньше source duration.
Невалидный запрос сохраняет прежнюю ожидающую цель и её revision. Валидный
запрос сохраняет одну цель; текущие position, pause, speed и playback identity
до её применения не меняются. Воспроизведение может продолжаться до буферизации
на прежней позиции. Это не команда загрузчику скачать нужные байты.

При появлении подтверждённого PTS-prefix приложение само применяет переход,
даже если клиент не опрашивает playback status. Используются существующие
таймеры приложения: 100 ms Qt и имеющийся Classic progressive-open timer.
Новых внешних таймеров/задач планировщика нет. Classic дополнительно ждёт
своё decoder-ready окно. Qt использует тот же guarded reload, что 1.86.

Status добавляет:

- `pendingSeekSeconds`: ожидающая позиция или null;
- `seekRevision`: десятичная строка, увеличивается при принятии новой цели;
- `seekState`: none, waiting-range, applied, superseded, cancelled или error;
- `seekError`: причина терминального отказа ожидающего перехода.

`applied` означает, что backend принял переход. Готовность нужного кадра/звука
проверяется по position/state/error, как и в 1.86. Ожидание и замена цели не
расходуют `remainingGrowingSeeks`; применённый growing переход расходует одну
из прежних 16 попыток. После complete действует прежний completed seek.

`cancelSeek` убирает ожидающую цель, сохраняя текущее воспроизведение; повтор
без ожидающей цели ничего не меняет. Принятый обычный/API/UI seek отменяет
ожидающую цель как superseded. Stop, Cancel, закрытие, replacement и ошибка
источника исключают применение старой цели к следующему playback. Для уже
переданного декодеру перехода cancelSeek не выполняет обратный seek.

Потерянный ответ нельзя автоматически дублировать: новый запрос заменит цель
и увеличит revision. Сначала следует read-only `playback_status()`. Отдельная
идемпотентность seek по requestId и полная race/stress/latency приёмка пока не
заявлены. Ждать можно только при живом writer: heartbeat/timeout контракта
continuous remux сохраняются. Это не бессрочное удержание мёртвого сеанса.

Результаты проверки: [отчёт 9 ноября](https://github.com/LiberVixer/FoxicMP/blob/main/docs/ProgressiveQueuedSeekValidation-2026-11-09.json).
Физическая A/V синхронизация и аппаратный renderer этим этапом не проверяются.

## Проверенный срез 9 ноября

Обычные main EXE: 69 новых queue cases и 153 прежних случая с неизменёнными
SDK 1.85/1.86, всего 222. GCC/ASan-UBSan/MinGW/MSVC — по 44 admission checks;
Python helper — 36. Полный Window/Player suite обеих Qt ОС — по 16 cases,
включая реальный mouse seek, отменяющий ожидающую цель. Итого до установки 466;
конечные test drivers имеют exit 0. Внедрённые аварии принадлежащего тесту worker
и принудительное завершение idle test peers имеют собственные сохранённые
результаты; их не называют штатными exits 0.

Проверены pause/playing commit, superseding, cancelSeek, неверная цель без
изменения очереди, Stop/Cancel, ordinary-file replacement с поздним старым Stop,
настоящая авария owned worker и ошибка producer. Применение после публикации
проверено через таймер приложения без playback polling. Это ограниченный
последовательный срез, а не полная конкурентная/многочасовая приёмка.

Первоначальные отказы сохранены. Ошибка типа waitForRange первоначально давала
invalid_manifest вместо invalid_request — parser исправлен, assertion сохранён.
Первая replacement-фикстура ошибочно запрашивала второй remux job; теперь
проверяет штатный ordinary start/replace, не меняя one-job quota. Первый Classic
startup получил лишь 4,92 s decoder-ready при неизменённом пороге 10 s; очередь
тогда ещё не отправлялась. В другом прогоне цель 13,9 s оставалась за
подтверждёнными 13,2906667 s fractional stereo даже при полностью записанном
физическом файле. Конечная фикстура использует достаточно стартовых данных и
цель 12,5 s между измеренными prefix-границами; guards/таймауты не ослаблены.

Classic проверен частными профилями с Null Video Renderer (Uncompressed);
desktop renderer и физический A/V этим не проверены. Ручной Classic hook
проверен через API/SeekTo, отдельной mouse-автоматизации Classic нет. Windows
QtTest сохранил stderr `QEventDispatcherWin32::wakeUp: Failed to post a message`
после 16 PASS, native exit 0. Общие shutdown/resource/latency gates остаются
открытыми, прежние исторические отказы не переименованы в успешные.

Установленный Linux EXE/worker/SDK-кандидат 1.87 обновлён атомарно по файлам,
с backup `_bin/FoxicMPQt-linux-backup-before-sdk187-20261109T095844Z`.
Ещё 23 установленных сценария прошли; всего этот срез содержит 489 успешных
cases/checks. INI и остальные личные файлы сохранены. Из 585 защищённых файлов
574 совпали, 11 — ожидаемые Linux-обновления; новый документ добавлен отдельно.
158 HKCU shell entries совпали, Windows-установки сохранены. SDK-кандидат
проверен по 325 файлам и 10 imports; новый public binary release не создавался.
