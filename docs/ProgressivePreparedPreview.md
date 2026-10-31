# SDK 1.53: воспроизведение подготовленного MP4-фрагмента из RAM

Продолжение [локальной подготовки SDK 1.52](ProgressiveRemuxStage.md) для Linux Qt,
Windows Qt и Windows Classic. Harvester не меняется. Новый API запускает один
готовый ограниченный фрагмент обычными декодерами плеера. Он **не** превращает
подготовку в непрерывное воспроизведение всего растущего MP4.

## Согласование и пример

Независимая capability `preparedRemuxPreview`: `version=1`,
`mode=prepared-remux-clip`, `playbackSupported=true`, `boundedClip=true`,
`continuousPlayback=false`, `requiresPreparedProbe=true`, `sourceEofDistinct=true`.
Прежняя `backendRemuxProbe` остаётся preparation-only: её успех сам по себе
не запускает playback. Старый плеер без новой capability не подходит.

```python
from progressive_resources import ResourceProbe
from progressive_preview import RemuxPreview

probe = ResourceProbe(client)
probe.start_remux(snapshot, session_directory, "clip", "mp4", packet_limit=128)
prepared = probe.wait()
if prepared["media"]["code"] != "data":
    raise RuntimeError(prepared["media"])

preview = RemuxPreview(client)  # Проверка отдельной capability.
preview.start(probe, replace=False, activate=False, title="Фрагмент загрузки")
status = preview.status()      # Принятый start ещё может быть buffering.
# После старта подготовку можно забыть: декодер держит собственную RAM-ссылку.
probe.forget()
# Управление возможно после перехода в playing/paused:
# preview.control("pause")
# preview.control("seek", 0.4)  # Только внутри durationSeconds фрагмента.
# preview.control("play")
preview.cancel()
preview.wait_released()
```

При исключении транспорта из `start` объект сохраняет requestId/probeId.
`preview.status()` восстанавливает ID **чтением**, без повторного старта.
`preview.cancel()` сначала восстанавливает только свой ID. Объект не принимает
ответ с чужими requestId/probeId/previewId и не отменяет чужое воспроизведение.

## IPC

Все сообщения имеют `version:1`. Новые действия:

- `previewRemux`: обязательные `probeId`, случайный 32-значный hex `requestId`;
  необязательные `replace=false`, `activate=false`, `title` (до 1024 UTF-8 bytes).
- `previewStatus`: ровно один из `previewId` и `requestId`.
- `previewControl`: `previewId`, `command=pause|play|seek`; `seconds` допустим
  только для seek, конечное число 0–3600, не больше длительности фрагмента.
- `previewCancel`: только свой `previewId`.

Повтор последнего start с тем же requestId и канонически одинаковым JSON
возвращает прежний previewId; изменение сообщения с тем же токеном даёт
`preview_conflict`. Запрос без explicit replace при занятом плеере —
`session_busy`. Другие ошибки: `prepared_clip_not_ready`, `unknown_preview`,
`preview_closed`, `invalid_request`, `prepared_clip_limit`. API хранит последнюю
preview identity; recovery старого запроса после следующего preview не обещается.
Область доступа — текущий пользователь и отдельный профиль плеера, как у старого API.

Статус: `mode`, `requestId`, `previewId`, `probeId`, `sourceSessionId`,
`state=buffering|playing|paused|finished|closed` (Qt также `error`), `active`,
`positionSeconds`, `durationSeconds`, `boundedClip`, `continuousPlayback`,
`sourceComplete`, `decoderBytesRead`, `readerCount`, `released`.
`decoderBytesRead` — decimal string. `ok` подтверждает команду/статус, а не кадр
или физический звук. Для управления следует дождаться playing/paused.

`finished` означает конец **фрагмента**. При `sourceComplete=false` исходник
ещё не завершён; никакие EOF, complete или ошибки загрузки writer не подделываются.
Продолжение скачивания не расширяет уже открытый buffer. Новый фрагмент требует
отдельной подготовки и явного запуска; бесшовность/сохранение позиции не обещаются.

## Данные, срок жизни и границы

Поддержка входа полностью наследуется от SDK 1.52: MP4/M4A/MOV, H.264 и/или
AAC-LC, подтверждённые ranges/SHA, максимум 128 packets и 16 MiB на buffer.
Новая команда принимает только opaque probeId текущей сохранённой подготовки.
Media bytes/путь не передаются через IPC и не пишутся во временный медиафайл.

Qt регистрирует закрытый `foxicmp-remux` stream callback; Classic передаёт
синхронный memory reader штатному MP4 splitter. Случайный внутренний URI не
разрешается в файловый путь, HTTP URL или скачивание. Чтение ограничено размером
buffer и порциями до 1 MiB. Registry содержит слабые ссылки, максимум 16 живых
фрагментов, включая ещё закрывающиеся декодеры. При смене/Stop/Close старый
контекст отменяется; уже открытый reader держит данные до фактического закрытия.
Qt сохраняет callback factory до возврата из native mpv destruction.

`released=true` требует нулевого числа native readers и завершённого/закрытого
состояния. Это освобождение **читателей**, а не обещание удаления последней
диагностической RAM-ссылки: API может хранить последний контекст до следующего
preview/закрытия приложения. Файловые reader receipts подготовки независимы;
прежний `ResourceProbe.forget()` подтверждает их обычным порядком.

Внутренние URI не добавляются в историю/позиции Qt; Classic открывает фрагмент
с `bAddRecent=false`. EOF preview не выполняет after-playback actions и не
переходит к следующему элементу очереди. Профиль пользователя не нормализуется.
Смена выбранных дорожек/скорости между разными подготовками и сохранение
исходных chapters/attachments этим этапом не добавляются.

Обычный native teardown Classic сохраняет прежние ограничения STA; общий
порог закрытия всех типов графов этим этапом не объявляется выполненным.
Физический A/V drift, многоминутный просмотр, расширенная codec matrix,
непрерывный remux/live и реальный Harvester остаются открытыми пунктами
[чеклиста](ProgressiveCompletionChecklist.md).
