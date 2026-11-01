# SDK 1.55: обычный MP4 с индексом в конце

Одинаковый ограниченный профиль трёх редакций: Linux Qt, Windows Qt и
Windows Classic. Harvester не изменяется. Контракты SDK 1.54 и старше сохранены.

## Выбор режима

Проверить `schema2Playback.muxedPlayback.mp4.tailIndex`:
`version=1`, `mode=tail-moov-mp4-v1`,
`playbackMode=growing-tail-mp4-h264-aac-lc`, `containerHint=mp4-tail`.
В обеих required scalar tracks — H.264 и AAC-LC — указать `containerHint=mp4-tail`
и один общий `source.resourceId`. Остальная модель, identity/revision, SHA-256,
heartbeat и release — прежний [контракт](ProgressiveManifestV2.md).
Синтетический metadata-конверт: `progressive-v2-tail-mp4-vod.json` в SDK.

Профиль наследует [обычный MP4 SDK 1.54](ProgressiveIndexedMp4.md): один явно
заданный mdat, известные неизменные length/duration, VOD, обычные sample tables,
H.264/avc1 + AAC-LC/mp4a. Метаданные ограничены суммарно 1 MiB и 16 top-level atoms.
Допустимы ftyp, moov, free и один mdat; size=0, несколько mdat, посторонние
trailers, encrypted/external references и остальные codec pairs отклоняются.

Подтвердить можно максимум два диапазона: непрерывное начало файла и один
диапазон метаданных **после конца mdat**. Начало второго диапазона внутри
media payload запрещено. Это не поддержка произвольных островков media данных.
Размер sparse файла, future offsets в moov и наличие физических байтов сами
по себе не разрешают чтение. В decoder-проекцию попадают только целые samples
из подтверждённого непрерывного начала. SHA проверяется перед чтением.

## Ожидание индекса и подсказка загрузчику

`V2PreviewSession.publish(snapshot)` возвращает `state=waiting-index`, если
не хватает опубликованного заголовка или moov. Reader не считается запущенным;
неуспешная попытка не оставляет открытые handles/неподтверждённые receipts.
После публикации метаданных повторить publish с новой revision. Дальнейшие
update/pause/seek/buffering/complete сохраняют readerId и позицию.
Отсутствие целых samples при готовом индексе — прежнее waiting-initialization.

Для получения точных рекомендуемых диапазонов служит существующий
`resourceProbe` с новой взаимоисключающей query `index:{"resourceId":"mux"}`,
пустым `reads:[]`, без `media`/`segment`. Отдельная capability:
`schema2Playback.muxedPlayback.mp4.tailIndex.indexProbe`.
Она **analysisOnly**, не запускает playback и ничего не скачивает.

```python
from progressive_resources import ResourceProbe
from progressive_v2 import V2PreviewSession

probe = ResourceProbe(client)
probe.start_index(snapshot, directory, 'mux', wait_milliseconds=0)
reply = probe.wait()
index = reply['index']
if index['code'] == 'waiting-index':
    for item in index['priorityRanges']:
        # Рекомендация собственному загрузчику, не команда плеера:
        # item: resourceId, offset, length, reason, advisory=True
        pass
probe.forget()                 # подтвердить закрытые reader receipts
# Записать/flush байты, обновить ranges + SHA-256 + revision и только затем:
preview = V2PreviewSession(client, directory, activate=False)
preview.publish(next_snapshot)
```

`index.mode=mp4-index-locator`, `analysisOnly=true`, `playbackSupported=false`.
`priorityRanges` содержит максимум одну рекомендацию, offset/length — точные
десятичные строки, длина до 1 MiB. `reason=mp4-atom-header` запрашивает до 16
байтов заголовка; `mp4-index` — целый ограниченный metadata atom.
Загрузчик может отклонить рекомендацию; плеер продолжит ожидание.
mdat пропускается по объявленному размеру, его payload не сканируется.

При `code=data` рекомендации пусты; возвращаются `moovOffset`, `moovBytes`,
`mdatEnd` и `tailIndex`. Это результат **анализа индекса**, а не доказательство
поддержки codec/profile, готовности samples или воспроизведения.
`allReadable=true` относится только к query. Не использовать его как playing.

`waitMilliseconds` — 0..10000; внутри ожидания `resourceProbeUpdate` публикует
новую revision, `resourceProbeCancel` отменяет ожидание и убирает рекомендации,
`resourceProbeForget` подтверждает release. SDK проверяет принадлежность ответа
request/session/generation/resource и границы подсказки; чужие диапазоны,
URL/path fields и неканонические offsets не передаются загрузчику.

## Декодирование и границы этапа

Применяются те же две проекции, исходные timestamps, AAC edit/trim и Qt audio
metadata view, что в SDK 1.54. Перекодирования, промежуточного media файла,
записи в исходник или сетевого fallback нет. EOF подтверждает writer.complete
полного файла, а не достижение доступной границы.

[Проверки](ProgressiveTailMp4Validation-2026-11-01.md) разделяют native equality,
реальное воспроизведение трёх редакций и sanitizer. Произвольные sparse media,
общий FFmpeg remux, audio/video-only, остальные codecs/containers, live,
реальные события Harvester и длительная физическая A/V-приёмка остаются
в [чеклисте](ProgressiveCompletionChecklist.md).
