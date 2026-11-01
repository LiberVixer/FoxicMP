# SDK 1.54: весь растущий обычный MP4 с индексом в начале

Общий профиль Linux Qt, Windows Qt и Windows Classic:
`schema2Playback.muxedPlayback.mp4.mode=growing-muxed-mp4-h264-aac-lc`.
Это непрерывное native-progressive воспроизведение одного обычного fast-start
MP4, включая дозагрузку, ожидание и настоящий EOF writer. Лимита 128 пакетов
из [RAM preview SDK 1.53](ProgressivePreparedPreview.md) здесь нет.
Другие контейнеры и общий FFmpeg remux этим профилем не добавляются;
Harvester не меняется.

## Вызов

Используется существующий v2 API start/update/complete/heartbeat/cancel/release
и `V2PreviewSession`. Новая capability согласуется отдельно от fMP4.
Две логические дорожки ссылаются на один физический resource:

```json
[
  {"id":"video","kind":"video","required":true,"codecHint":"h264",
   "containerHint":"mp4-muxed","state":"downloading",
   "source":{"resourceId":"mux"},"initializationReady":true},
  {"id":"audio","kind":"audio","required":true,"codecHint":"aac-lc",
   "containerHint":"mp4-muxed","state":"downloading",
   "source":{"resourceId":"mux"},"initializationReady":true}
]
```

Ресурс имеет известный неизменный `length`, относительное имя, подтверждённые
непрерывные ranges и SHA-256. Порядок публикации, identity/generation/revision,
heartbeat и release — прежний [контракт v2](ProgressiveManifestV2.md).
Пример полного metadata-конверта: `progressive-v2-indexed-mp4-vod.json` в SDK.
Он синтетический, не содержит настоящего медиа и не запускается сам по себе.

```python
from progressive_v2 import V2PreviewSession

preview = V2PreviewSession(client, session_directory, activate=False)
preview.publish(snapshot)       # waiting-initialization, если head index ещё нет
# После append и публикации новых подтверждённых ranges / SHA:
preview.publish(next_snapshot)  # тот же reader и текущая позиция
preview.heartbeat()
# Только writer подтверждает complete полного файла:
preview.publish(complete_snapshot)
preview.close(timeout=10)
```

Старый плеер с одной fMP4 capability новый SDK helper отклоняет до старта.
`backendRemuxProbe` и `preparedRemuxPreview` сохраняют прежние контракты.

## Профиль и проекция

- Один self-contained MP4, head ftyp/moov, необязательные head free boxes,
  один **явно заданного размера** mdat до конца файла. Moov в конце,
  несколько mdat, trailers и нулевой размер mdat сюда не входят.
- Ровно одна H.264/avc1 и одна AAC-LC/mp4a дорожка; обычные stts/stsz/stsc,
  stco либо co64, необязательные ctts/stss/sdtp. Проверенные fixtures:
  Main/High, B-frames, без B-frames, 30000/1001 и VFR.
- Время сохраняется из исходных sample tables и edit lists. PTS/DTS не
  обнуляются раздельно; композиционные offsets и AAC preroll сохраняются.
  Media edit задаёт границу presentation видео. Выходящие за неё packet durations
  обрезаются; если это разрывает decode cadence, следующий fragment получает
  исходный tfdt. Исходные DTS следующих пакетов не сдвигаются. Для AAC
  сохраняются точные stts durations, включая последний trim.
- Разрешён обычный AAC roll group: одна запись roll=-1 для всех аудиопакетов.
  Её описание переносится в projected init, mapping — в каждый traf.
  Другие sample groups, encryption, внешние dref и cross-track refs отклоняются.
- VOD с известной duration, timebase 1/10000000 и нулевым session offset.
  До 1 MiB head metadata, 250000 samples на дорожку, 16 MiB на sample
  и на одну публикацию новых физических байтов; read chunks до 65536 bytes.
  Это ограниченный профиль, не обещание всех допустимых ISO BMFF вариантов.

В памяти строятся две append-only индексные fMP4-проекции. В них находятся headers,
индексы и spans исходных sample payloads. Media bytes остаются в исходном файле
и читаются через прежний `ResourceSource` с ranges/SHA и reader receipts.
Нет encoder/resampler, промежуточного медиафайла, URL или сетевого fallback.
Старые virtual bytes не меняются при append, декодер не переоткрывается.

Qt получает отдельное обычное MP4-представление аудио с полными stts/co64 и
исходным edit/trim. Его packet offsets совпадают с индексной проекцией;
служебные moof представлены как free. Это сохраняет точный конечный AAC discard
и на Windows FFmpeg. Classic продолжает читать исходные пакеты через свою
fMP4-индексацию. Ни сжатые payloads, ни позиции DTS не меняются.
Projected init ограничен 4 MiB; входной head index по-прежнему — 1 MiB.

Пакет попадает в проекцию только целиком после подтверждения всех его байтов.
Будущие offsets из moov допустимы как **индекс**, но не разрешают чтение payload.
Физический размер файла не заменяет публикацию writer. Когда все физические
байты уже доступны, но complete ещё не опубликован, EOF всё равно не выдаётся.

`videoContainer/audioContainer=mp4`, `decoderContainer=fmp4` (индекс/видео),
`qtAudioDecoderContainer=mp4` в capability,
`physicalResources=1`, стабильный mediaMode и readerId доступны через status.
Поле `fragments` считает синтетические projected fragments, а не исходные moof.
Pause/seek/resume, buffering, metadata/chapters и освобождение используют
существующие платформенные адаптеры; menu/defaults/profile не меняются.

## Границы

Это закрывает ограниченный fast-start MP4 путь к просмотру всего файла,
а не общий непрерывный remux произвольного контейнера. Video-only/audio-only,
moov-at-tail/приоритетные ranges, sparse out-of-order media и широкий набор
кодеков остаются отдельными пунктами [чеклиста](ProgressiveCompletionChecklist.md).
Co64 разбирается с bounded offsets; фактическая многогигабайтная приёмка этим
этапом не объявляется. Физический A/V drift и реальный Harvester не аттестуются.
