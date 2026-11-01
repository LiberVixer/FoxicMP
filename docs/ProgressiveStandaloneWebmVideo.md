# SDK 1.60: самостоятельное видео VP8/VP9/AV1 в растущем WebM

Linux Qt, Windows Qt и Windows Classic воспроизводят один настоящий required
видеопоток из одного scalar WebM resource. Аудиодорожка, второй decoder URL
и аудиовыход для этого потока не требуются. Harvester не изменяется.

Проверить `schema2Playback.standalone.webmVideo`: `version=1`,
`mode=standalone-webm-video-v1`, нужный профиль в `profiles`:
`standalone-webm-vp8`, `standalone-webm-vp9`, `standalone-webm-av1`.
Наличие общего WebM video codec либо standalone WebM audio недостаточно.
Поля `standalone.webm` и standalone fMP4 сохраняются без смены режима.

Модель [standalone](ProgressiveStandalone.md): `kind=video`, `required=true`,
`containerHint=webm`, явный `codecHint=vp8/vp9/av1`, один `source.resourceId`.
Известная duration VOD, timebase 1/10000000, timestampOffset=0, непрерывный
подтверждённый prefix и SHA-256. Начальная длина файла может быть неизвестной.
Metadata-примеры SDK: `progressive-v2-standalone-webm-vp8.json`,
`progressive-v2-standalone-webm-vp9.json`, `progressive-v2-standalone-webm-av1.json`.
Они не содержат медиа и самостоятельно не играют.

- VP8: 8-bit, versions 0–3, без CodecPrivate; первый отображаемый keyframe,
  корректные dimensions/scaling и ограниченная первая partition.
- VP9: Profile 0/8-bit и Profile 2/10-bit, 4:2:0; начальный keyframe и
  соответствие packet header/CodecPrivate, если он присутствует.
- AV1: Main 8/10-bit, 4:2:0; обязательная конфигурация с sequence header,
  совпадение sequence в первом packet и keyframe/frame OBU.

Применяются существующие WebMInitialization/WebMIndex: один TrackEntry,
известные размеры Cluster, отсутствие lacing, ограниченный DefaultDuration.
Дробная длительность кадра хранится в наносекундах; start определяется packet
clock, end округляется отдельно в 100 ns. Не предполагается, что один Cluster
равен одной секунде: muxer может разделять Cluster по размеру данных.
Тестовый writer публикует целые исходные Cluster до нужного времени по
независимому ffprobe PTS. Порог буфера плеера не меняется.

Status: `singleTrack=true`, `physicalResources=1`, один video `mediaTracks`,
`audioCodec`/`audioContainer` пусты и decoder audio bytes равны нулю.
Pause/seek/update/complete/EOF/reader release используют прежний API;
readerId при обновлении/перемотке сохраняется. Неполные Cluster ждут новые
байты; EOF возможен только после complete ресурса.

Исходный WebM не переписывается и не транскодируется плеером. В Qt проверяются
реальный decoder, 8/10-bit pixel format, меняющиеся framebuffer hashes и
отсутствие audio track при недоступном тестовом audio-device. Classic проверяется
через штатный Matroska splitter/decoder/DirectShow graph и native команды.
Это не аттестация GPU/HDR, монитора или физических выходов.
[Результаты каждой редакции](ProgressiveStandaloneWebmVideoValidation-2026-11-01.md).

Возможности представлены соседними C++ raw literals, чтобы не превышать
ограничение одного токена MSVC. SDK собирает их в один JSON; manifest сверяется
с фактической capability плеера. Wire API от этого не меняется.

Другие самостоятельные codec/container profiles, segments/live,
произвольные sparse media и реальная интеграция Harvester остаются отдельными
пунктами [чеклиста](ProgressiveCompletionChecklist.md).
