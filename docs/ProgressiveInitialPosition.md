# SDK 1.85: начальная позиция continuous playback

9 ноября 2026. [Проверка трёх редакций и исходные отказы](https://github.com/LiberVixer/FoxicMP/blob/main/docs/ProgressiveInitialPositionValidation-2026-11-09.json).

Новый opt-in для Windows Classic и Qt обеих ОС: поле `startSeconds` в
`continuousRemuxPlay`, Python `ContinuousRemux.play(start_seconds=...)`.
Без параметра передаётся прежний запрос; SDK 1.84 продолжает работать с нуля.
Harvester этим изменением не дорабатывается.

```python
status = preview.status()
target = 1.0
if status['playbackReady'] and status['indexedEndSeconds'] > target:
    preview.play(start_seconds=target)
```

Перед использованием положительной позиции проверяется
`capabilities.continuousRemux.initialPosition.version == 1`.
Descriptor задаёт `field=continuousRemuxPlay.startSeconds`,
`range=published-presentation-prefix`, `completeRange=source-duration`,
`pending=caller-retry`, `decoderMayBuffer=true`. SDK проверяет эту возможность
по собственному снимку capabilities; изменение публичного словаря её не включает.

Позиция должна быть конечным числом от нуля до 3600 s, строго раньше duration.
Для растущего output она также должна быть строго раньше подтверждённого
непрерывного PTS-покрытия. При проверке границы target округляется вверх до
100 ns; backend использует свою точность времени/кадра. Размер файла не
определяет доступность позиции. `indexedEndSeconds` и `durationSeconds` есть
в remux status, исходный `startSeconds` — в playback status.

Если target ещё не подтверждён, raw API возвращает `waiting-range`, не создавая
playback ticket и не меняя намерение воспроизведения. Тот же запрос можно
повторить после публикации данных. SDK предварительно проверяет status и
выдаёт RuntimeError без `play_request_id`; исходящий play при этом не отправлен.
Если сервер вернул явный `waiting-range` после этой предварительной проверки,
SDK сбрасывает непринятую попытку и допускает следующий play. При timeout
идентификатор сохраняется: сначала требуется read-only восстановление статуса.
До конца источника и после complete выход за duration даёт `invalid_request`.
Отказ открытия/неподтверждённая позиция не являются разрешением удалить originals.

Каждый job по-прежнему допускает один принятый playback attempt. Потерянный
ответ восстанавливается через `playback_status()`, а не повторный вызов play.
Запрос с тем же requestId и другой позицией — конфликт; числа 1 и 1.0 имеют
одинаковый fingerprint. Для замены текущего просмотра нужен прежний `replace=true`.
Проверка позиции выполняется до закрытия текущего media.

Qt открывает те же защищённые проекции с start/exact/preroll; Classic использует
штатный начальный rtStart. Декодер может ждать дополнительный буфер даже внутри
подтверждённого PTS-покрытия. Ни `playing`, ни аппаратный вывод не гарантируются
одним admission; надо следить за playback status/error и filesClosed receipts.

Это выбор начальной позиции, не новая команда перемотки играющего файла.
`continuousPlaybackControl seek` по-прежнему требует output complete;
`continuousRemux.seek` сохраняет `after-output-complete`. Повторные growing seek,
ожидание неподтверждённой цели внутри принятого playback и UI ещё выделены
отдельным пунктом чеклиста. Полная ТЗ/stress/физическая A/V приёмка не закрыта.

## Проверенные границы

Обычные main EXE трёх редакций: по 20 сценариев с новым SDK-кандидатом
(AAC 9, default H.264 5, reordered H.264 6) и по 20 прежних сценариев
с неизменённым SDK 1.84. Всего 120, конечные native driver exits 0.
AAC-срез проверяет шесть профилей 44,1/48 kHz mono/stereo, B16/fractional,
короткое audio, pause/publish/complete, прежний completed seek, stop/failure,
`waiting-range` без потребления requestId. Video-срезы также проверяют потерю
ответа и read-only recovery. Это ограниченные профили, не весь ТЗ.

Native parser/admission: по 20 проверок GCC, ASan/UBSan, MinGW и MSVC;
Python helper — 32 unit checks, включая immutable capability, старый default,
неподтверждённый target, ошибочные типы, race/retry и потерянный ответ.

Classic проверен с Null Video Renderer (Uncompressed) и Null Audio Renderer
только в частных профилях. Первый desktop-прогон остановило модальное сообщение
о невозможности загрузить MPC Video Renderer: thread snapshot и текст окна
сохранены. Этот отказ не является доказанным дефектом initial seek и не заменён
утверждением о готовности экранного renderer. Windows-установки не менялись.

Два других исходных отказа объяснены условиями теста: при target за конечным
одноотсчётным AAC Classic корректно не arm-ит PCM, а Qt может кратко показывать
`playing` на стартовой позиции до открытия AO. Проверки теперь учитывают
конкретную start-позицию и ждут продвижения на 0,15 s/ожидаемый audio output;
обычный старт с нуля сохраняет прежние требования. Отказы сохранены отдельно.

Linux EXE, worker и SDK-кандидат 1.85 обновлены после проверки, каждый файл
заменён атомарно, все файлы подготовлены до первой замены. Сохранены backup
и INI. Дополнительно 20 сценариев установленных EXE/worker/SDK прошли с exit 0.
Из прежних 583 защищённых файлов обновлены 11 файлов приложения/SDK;
остальные 572 совпали. Добавлены два SDK-документа; личные файлы не удалялись.
Все 158 HKCU shell entries совпали. Windows-установки не заменялись.

Общее число конечных checks/cases этого этапа — 252. SDK ZIP проверен по 323
файлам и десяти Python-модулям; standalone import exit 0. Это проверенный
кандидат из рабочей ветки (`sourceDirty=true`), не новый опубликованный release.
Classic main собран частным перелинкованием изменённого API с прежними проверенными
объектами/библиотеками; это не clean release build. Следующая поставка требует
отдельной сборки и проверки пакетов; старые опубликованные assets не менялись.
