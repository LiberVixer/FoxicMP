# SDK 1.86: повторные переходы по подтверждённому кэшу

9 ноября 2026. [Результаты и исходные отказы](https://github.com/LiberVixer/FoxicMP/blob/main/docs/ProgressiveGrowingSeekValidation-2026-11-09.json).

Расширение ограниченного continuous-remux маршрута всех трёх редакций FoxicMP.
Harvester не изменяется. [SDK 1.85 initial position](ProgressiveInitialPosition.md)
сохраняется, прежние SDK control-запросы остаются complete-only.

```python
preview.control('pause')
preview.control('seek', seconds=2, allow_growing=True)
# Дождаться playback_status: нужная positionSeconds и state paused/playing.
```

Положительная возможность объявлена в `continuousRemux.growingSeek`:
version 1, field `continuousPlaybackControl.allowGrowing`, range
`published-presentation-prefix`, unavailable `waiting-range`, maxSeeksPerPlayback
16, decoderMayBuffer true. SDK проверяет отдельный immutable снимок этого
descriptor; без него explicit growing control отклоняется до отправки.
`allow_growing` — строгий bool, допустим только для команды seek; false не
попадает в JSON. Raw поле `allowGrowing` тоже допустимо только для seek.

## Позиция и состояние

Target — конечное число 0 ≤ seconds < 3600 и строго раньше source duration.
До output complete оно также должно быть строго раньше `indexedEndSeconds`;
для проверки позиции производится округление вверх до 100 ns. Эта граница
вычислена по проверенным полным фрагментам и PTS-интервалам обеих дорожек,
не по размеру файла. Конечный полностью индексированный AAC не ограничивает
более длинное видео. Classic дополнительно проверяет своё decoder-ready окно.

Неподтверждённая позиция возвращает `waiting-range`: текущие position/pause,
playbackId и budget не меняются. SDK не публикует данные, не скачивает target
и не повторяет запрос автоматически. При достаточном покрытии декодер всё ещё
может ждать дополнительный buffer/reference data. Ответ seek не является
доказательством готового кадра: клиент проверяет status и error.

Qt переоткрывает декодер поверх тех же protected cache projections, с прежним
случайным URI и playback identity. Старые callbacks прерываются штатным loadfile;
originals и worker не переоткрываются заново ради скачивания. AAC получает свежее
точное PCM-окно. Сохраняются pause intent, заголовок и скорость окна.
Classic использует штатный seek того же DirectShow graph и AAC re-arm.

Во время Qt reload status временно buffering/decoder-opening, затем вновь
paused или playing. Следующую команду перехода следует отправлять после
применения предыдущей; промежуточный control может вернуть not-ready.
Stop/Cancel должны закрывать сеанс и readers, даже если reload ещё готовится.
Потерянный seek-ответ нельзя автоматически дублировать: это ещё одна попытка;
сначала проверить positionSeconds и remainingGrowingSeeks.

## Ограничение первого контракта

До complete принято не больше 16 growing seek-попыток на один playbackId;
счётчик виден как `remainingGrowingSeeks`. Отказ range не расходует его,
принятый Qt reload расходует. Причина — существующая квота reader receipts:
переоткрытия создают новые записи, старые proofs сохраняются до закрытия job.
Это явный bounded контракт, а не неограниченная часовая seek/stress гарантия.

Следующий growing seek после исчерпания возвращает `continuous-seek-limit`
до изменения текущего воспроизведения. После output complete действует прежний
seek без этого счётчика; не нужно перезапускать приложение или менять профиль.
Старые `control('seek', seconds=...)` без opt-in до complete по-прежнему дают
`continuous-seek-unavailable`, даже в новом runtime.

В Qt таймлайн показывает подтверждённую часть и использует этот же guarded
путь для щелчка/drag. Незагруженная позиция не принимается; после исчерпания
16 reload-попыток растущая шкала блокируется до complete. Classic сохраняет
штатную прямую перемотку своего таймлайна, которая не создаёт Qt reload receipts;
лимит API на growing control при этом общий.

## Границы проверки

Внутренний repeat-тест выполняет 1 → 2 → 0,5 → 1 s, pause/publish/complete,
natural EOF и filesClosed. Для Qt PCM сравнивается побайтно с теми же
переоткрытиями обычных завершённых файлов. Прямые in-place Qt seek дали
различный PCM на Linux при равной длине; исходный отказ сохраняется,
публичный Qt путь использует reload вместо ослабления допуска.

Classic repeat-тест проверяет точное число отсчётов, временные метки и NV12-хеши
всех кадров после последнего seek. Он **не доказывает побайтные PCM values**:
после серии in-place seek меняется PNS decoder history, а полный trace
закодированных пакетов этого графа не записывался. Прежние start/completed
seek oracle-проверки сохранены без увеличения допусков.

Физическая A/V синхронизация, многочасовая/всеформатная stress матрица,
автоматическое ожидание неподтверждённой цели внутри playback и безопасное
расширение retention/seek quota остаются отдельными задачами ТЗ.

## Конечный runtime-срез

Обычные main EXE: по 51 сценарию каждой редакции, всего 153. Из них 66 — новые
growing scenarios, включая шесть AAC-профилей, default/reordered H.264,
16 переходов и отказ 17-го, непринятый target, сохранение identity/pause,
Stop/Cancel сразу после seek. Ещё 87 — прежние control и initial-position
вызовы с неизменённым SDK 1.85. Конечные driver exits 0.

Декодеры: 18 repeat cases, по шесть каждой редакции; Qt проверен конечным
Player-кодом, Classic — private DirectShow graph. Полный Window/Player suite
обеих Qt ОС прошёл по 16 QtTest cases, включая настоящие mouse clicks по шкале,
доступный и недоступный target, паузу/продолжение, сохранение скорости и title,
reader release, замену и producer failure. GCC/ASan-UBSan/MinGW/MSVC выполнили
по 28 admission checks, Python helper — 34 unit checks. Итого до установки 349
конечных cases/checks. Первый Linux direct-seek PCM failure остаётся FAIL;
он не включён в это число успешных проверок. Отдельная ошибка PS wrapper
с посимвольным splat исправлена до запуска Classic, её exit 2 сохранён.

Classic main использует Null Video Renderer (Uncompressed) только в частных
профилях; экранный renderer этим не проверен. В конце Windows QtTest записал
`QEventDispatcherWin32::wakeUp: Failed to post a message`, native exit остался 0;
это не доказательство полной shutdown/latency/stress приёмки. Результаты Linux
не подменяют Windows-прогоны. Все первые отказы/предупреждения и process exits
сохранены в отчёте. Новых внешних таймеров или задач планировщика не создавалось.

Установленный Linux EXE, worker и integration SDK-кандидат 1.86 обновлены;
после замены ещё 22 сценария прошли с driver exit 0. Всего этот срез содержит
371 успешный case/check. Замена каждого файла атомарна, прежние файлы сохранены
в `_bin/FoxicMPQt-linux-backup-before-sdk186-20261109T091137Z`. Из 584 защищённых
файлов 573 совпали, 11 — ожидаемые обновления Linux приложения/SDK; новый
`ProgressiveGrowingSeek.md` сохранён отдельно. INI и прочие личные файлы не
изменились, 158 HKCU shell entries совпали. Windows-установки не заменялись.
Это проверенный кандидат из рабочего дерева, не новый публичный binary release.

SDK 1.87 отдельно добавляет [явную очередь ожидающей позиции](ProgressiveQueuedSeek.md).
Поведение вызовов 1.86 без нового opt-in остаётся прежним.
