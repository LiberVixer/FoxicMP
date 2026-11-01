# SDK 1.57: самостоятельные audio-only и video-only потоки

Общий профиль Linux Qt, Windows Qt и Windows Classic: один настоящий required
медиапоток и один ресурс. Отсутствующая вторая дорожка не создаётся и не участвует
в буферизации, декодировании или EOF. Harvester не изменяется.

Проверить `schema2Playback.standalone.version=1`,
`mode=standalone-fmp4-v1` и нужный профиль из `profiles`:

- `standalone-fmp4-aac-lc`: одна AAC-LC/mp4a аудиодорожка;
- `standalone-fmp4-opus`: [Opus mono/stereo 48 kHz](ProgressiveStandaloneOpus.md), добавлен SDK 1.58;
- `standalone-fmp4-h264`: одна H.264/avc1 видеодорожка.

Это ограниченный scalar fMP4 путь с head moov и внутренними fragments.
VOD, известная duration, timebase 1/10000000, timestampOffset=0, один отдельный
resource с непрерывным подтверждённым prefix и SHA-256. Длина файла может быть
неизвестной до complete; длительность ролика должна быть известна.
Segments, внешний init, каталоги, live, optional tracks, субтитры и другие
codec/container combinations этим профилем не добавляются.

## Модель

```json
"tracks": [
  {"id":"main","kind":"audio","required":true,
   "codecHint":"aac-lc","containerHint":"fmp4",
   "initializationReady":true,"state":"downloading",
   "source":{"resourceId":"media"}}
]
```

Для video-only поменять kind на `video`, codecHint на `h264`.
Остальные поля сессии и resource — прежняя [модель v2](ProgressiveManifestV2.md).
Metadata-конверты в SDK: `progressive-v2-standalone-audio.json` и
`progressive-v2-standalone-video.json`. Они не содержат медиа и сами не играют.

```python
from progressive_v2 import V2PreviewSession

preview = V2PreviewSession(client, directory, activate=False)
preview.publish(snapshot)
# После записи/flush: новая revision, ranges и неизменяемые SHA blocks.
preview.publish(next_snapshot)
preview.heartbeat()
# Только writer.complete полного ресурса разрешает настоящий EOF.
preview.publish(complete_snapshot)
preview.close(timeout=10)
```

SDK проверяет отдельную capability до start. Отсутствующий init даёт
waiting-initialization; неполные media samples/fragment ждут новые байты,
не становятся успешным EOF. Четырёхсекундный prefix буферизуется; прежние
start/resume/pause thresholds сохраняются. Ручная pause, seek, complete,
heartbeat и release работают через существующие адаптеры.

## Движки и наблюдение

Внутреннее представление содержит ровно одну Track. Её media kind хранится
отдельно от номера слота: slot 0 у audio-only означает audio. Classic использует
штатный MP4 splitter/AAC или AVC decoder; Qt открывает только primary stream
`/track/0`. Второй URL/file/decoder не добавляется. Для video-only Qt не открывает
аудиовыход; для audio-only нет видеопотока.

Status: `singleTrack=true`, `physicalResources=1`, один элемент `mediaTracks`
с фактическим kind, стабильный mediaMode из списка profiles. Поля отсутствующего
codec/container пусты, счётчик decoder bytes отсутствующего kind равен нулю.
Reader identity сохраняется при публикации/перемотке; natural EOF по-прежнему
освобождает файлы. Общая модель leases/readerRelease/readerForget не меняется.

Проверенные источники: AAC mono 48 kHz с неизвестной начальной длиной,
AAC stereo 44.1 kHz, AVC Main с B-frames и VFR. Результаты каждой ОС/редакции,
native byte/index/lifecycle checks и ASan/UBSan — в [отчёте](ProgressiveStandaloneValidation-2026-11-01.md).
Недоступный audio-device у video-only проверен в Qt; физический стенд Classic
не аттестуется. Полная audio codec matrix, прочие контейнеры/самостоятельные
потоки, live и длительная физическая A/V-приёмка остаются в чеклисте.

SDK 1.59 добавляет [самостоятельное WebM Opus/Vorbis-аудио](ProgressiveStandaloneWebm.md)
через отдельный `schema2Playback.standalone.webm`; поля fMP4 capability выше сохраняются.

SDK 1.60 добавляет [самостоятельное видео WebM VP8/VP9/AV1](ProgressiveStandaloneWebmVideo.md)
через отдельный `schema2Playback.standalone.webmVideo`, сохраняя fMP4/audio capability.

SDK 1.61 добавляет [самостоятельное HEVC/AV1/VP9-видео fMP4](ProgressiveStandaloneFmp4Video.md)
через точные profiles в существующем standalone-fmp4-v1.
