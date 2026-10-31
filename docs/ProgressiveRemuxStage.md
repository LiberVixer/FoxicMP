# SDK 1.52: локальный copy-remux ограниченного MP4-фрагмента

Первый этап локальной подготовки из пункта 2
[чеклиста](ProgressiveCompletionChecklist.md). Общий код Linux Qt, Windows Qt и
Classic выполняет настоящий remux через FFmpeg, **без перекодирования**.
Это подготовка ограниченного фрагмента в памяти, **не подключённый playback**, не
непрерывная remux-сессия и не завершение всего progressive-ТЗ. Harvester не меняется.

## Вызов и согласование

Новая независимая capability `backendRemuxProbe`:
`version=1`, `mode=libavformat-avio-mp4-stage-v1`, `preparationOnly=true`,
`playbackSupported=false`, `gaplessEditsSealed=true`. Наличие worker/библиотек
проверяется на запрос, как в [диагностике SDK 1.51](ProgressiveBackendDemux.md).
Сама диагностика без remux сохраняет прежний контракт и `analysisOnly=true`.

```python
from progressive_resources import ResourceProbe

probe = ResourceProbe(client)
probe.start_remux(snapshot, session_directory, "clip", "mp4",
                  packet_limit=128, wait_milliseconds=5000)
result = probe.wait()
media = result["media"]
if media["code"] == "data":
    stage = media["remux"]
    assert media["preparationOnly"] and not media["playbackSupported"]
    assert stage["retained"] and not stage["transcoded"]
probe.forget()
```

Дополнение к прежнему `resourceProbe`: пустой `reads` и
`media={resourceId, formatHint, packetLimit, remux:"mp4-clip"}`.
Запросы segment/raw reads с этим режимом не смешиваются. SDK проверяет отдельную
capability до отправки задания и запоминания его identity; requestId/recovery,
update, cancel и forget работают по прежнему защищённому протоколу.

## Поддерживаемый срез

- Один MP4/M4A/MOV resource с обязательными SHA-256 и подтверждёнными ranges.
- Не более одной H.264-видеодорожки и одной AAC-LC-аудиодорожки; допустимы также
  video-only и audio-only. Дорожки не выбрасываются молча.
- AAC mono/stereo, 44,1 или 48 kHz, 1024 samples/frame, media timebase 1/sample_rate.
- Presentation start включённых дорожек — 0; исходные отрицательные decode/preroll
  timestamps при этом сохраняются. Ненулевой start не сбрасывается автоматически.
- Не более 128 входных пакетов. Для видео без перестановки кадров фрагмент может
  быть подготовлен до complete. Видео с перестановкой кадров принимается только
  при подтверждённом EOF внутри пакетного лимита, чтобы не обрезать неполную GOP.

Это ограниченный проверенный набор. HEVC/AV1/VP9, другие кодеки/контейнеры,
сложные edit lists, длинные B-frame источники и непрерывное расширение подготовленного
выхода сюда не добавляются одним объявлением capability. Прежние отдельные
native-progressive профили этих кодеков остаются доступны по своим контрактам.

## Результат и срок жизни

Успех: `media.code=data`, `analysisOnly=false`, `preparationOnly=true` и
`playbackSupported=false`. В `media.remux` возвращаются container=`mp4`, bytes,
SHA-256, количество пакетов, `sourceEof`, `packetLimited`, `transcoded=false`,
`storage=bounded-private-memory`, `gaplessEditsSealed=true`, `retained` и исходные
конечные presentation ticks дорожек в согласованном movie timescale (`edits`).
Пакеты/временные метки исходника по-прежнему описываются в `media.packets`.

`sourceEof=false, packetLimited=true` означает лишь готовый ограниченный фрагмент,
а не подготовку всего ролика. Пока не добавлен playback adapter, ни этот результат,
ни исторический `allReadable=true` не разрешают запуск нового remux playback.

Результат хранится в отдельном immutable RAM buffer задания. Внутренний
`ResourceProbeManager::stagedRemux(probeId)` предоставляет его платформенному
коду; текущий публичный IPC возвращает **описание и хеш, без media bytes/пути**.
Тестовый native CLI умеет выгрузить buffer для ffprobe; в API плеера такой команды нет.

Исходные файловые handles закрываются до `released=true`. Готовая RAM-копия
независима от входного файла. `forget` освобождает ссылку задания на buffer после
подтверждения reader receipts; `retained=false` виден в последующем статусе.
Отмена после успеха также удаляет привязку stage и переводит задание в cancelled.
Внутренняя shared_ptr-ссылка, если её удерживает adapter, имеет обычный срок жизни
C++ объекта; в текущем UI такого потребителя ещё нет. Удалять оригиналы плеер не умеет.

## Подготовка и сохранение звука

Worker читает только через прежний range/SHA resource I/O. Параметры дорожек
копируются `avcodec_parameters_copy`, пакеты — `av_interleaved_write_frame`;
PTS/DTS переводятся только между timebase. Не включаются encoder, resampler,
фильтры или независимое обнуление дорожек.

Выход — **обычный законченный MP4-фрагмент**, а не fMP4. Worker использует
seekable custom AVIO в своей ограниченной памяти: FFmpeg может дописать индекс и
edit list с точным окончанием звука. Только после trailer/flush законченные байты
передаются родителю. Родитель независимо сверяет их длину и SHA-256 перед публикацией.
Ни входные, ни промежуточные media-файлы на диске для этого не создаются/изменяются.

Причина выбора: проверка FFmpeg на Windows выявила потерю AAC end padding в
фрагментированном выходе; обычный MP4 сохраняет его. Тест сравнения декодированного
звука не ослаблялся: проверяется исходник и выход напрямую, без обрезки фильтрами.
Общий movie timescale выбран точно из timebase дорожек, без округления AAC до
миллисекунд. Если clock/time range нельзя представить в ограничениях, запрос
завершается ошибкой.

Использованы стандартные API из [официального примера remuxing](https://ffmpeg.org/doxygen/trunk/remuxing_8c-example.html)
и [документация MOV/MP4](https://ffmpeg.org/ffmpeg-formats.html).
Собственный универсальный demuxer или MP4 writer не добавлялся.

## Пределы и ошибки

Read budget — 16 MiB логических входных bytes, выход — не более 16 MiB,
1–128 пакетов, 1–2 дорожки. Пределы worker по времени/памяти и отсутствие
передаваемых путей/URL/дополнительного I/O сохраняются из SDK 1.51.
Материализация обоих RAM buffers и внутренних структур FFmpeg потребляет больше
памяти, чем размер одного выхода. Проверка SHA может читать целый integrity block.
Дисковое чтение родителя остаётся `cancellableDiskIo=false`; worker timeout не
является обещанием времени ответа зависшего файлового хранилища.

Waiting не выдаёт незаконченный stage. Update с новой подтверждённой revision
повторяет подготовку; отмена/ошибка/нарушение SHA удаляет временный RAM buffer.
Неподдерживаемые codecs, start/timebase, неполная последовательность B-кадров,
повреждённые или зашифрованные пакеты не заменяются перекодированием либо ложным успехом.
Основные дополнительные коды: `unsupported-remux-format`, `unsupported-remux-codec`,
`unsupported-remux-aac-config`, `unsupported-remux-clock`, `unsupported-remux-timescale`,
`unsupported-remux-reorder`, `unsupported-remux-encryption`, `unsupported-remux-trim`,
`remux-incomplete-stream`, `backend-output-limit` и ошибки header/packet/trailer.

## Что дальше

[Проверки трёх редакций](ProgressiveRemuxStageValidation-2026-10-31.md).
Следующий шаг — управляемый playback adapter с продолжением подготовки по мере
загрузки и классификацией native-progressive/progressive-remux/waiting/post-download/
unsupported. Публичного пути к RAM-фрагменту или автоматического fallback пока нет.
SDK ZIP содержит клиент и документы, без EXE/DLL/установщиков; установщики 0.2.0
этот этап не включают. Сборка worker и доставка runtime — как в SDK 1.51.
