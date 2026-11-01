# SDK 1.59: самостоятельное Opus/Vorbis-аудио в растущем WebM

Linux Qt, Windows Qt и Windows Classic используют одну настоящую required
аудиодорожку и один scalar resource. Видеодорожка и второй decoder URL
не создаются. Harvester не изменяется.

Перед публикацией проверить `schema2Playback.standalone.webm`:
`version=1`, `mode=standalone-webm-audio-v1`, а также точный профиль
`standalone-webm-opus` или `standalone-webm-vorbis` в `profiles`.
Общая поддержка WebM/Opus/Vorbis и старый `standalone-fmp4-v1` этого не обещают.
Существующие поля standalone fMP4 сохраняют свои значения; WebM согласуется
через отдельную вложенную capability.

Используется [модель standalone](ProgressiveStandalone.md): `kind=audio`,
`required=true`, `containerHint=webm`, явный `codecHint=opus` или `vorbis`.
Один `source.resourceId`, без external init, segments и каталогов. VOD с
известной duration, timebase 1/10000000, timestampOffset=0, непрерывный
подтверждённый prefix и SHA-256. Начальная длина ресурса может быть неизвестной.
Примеры метаданных: `progressive-v2-standalone-webm-opus.json` и
`progressive-v2-standalone-webm-vorbis.json`; медиа в SDK не включено.

Контейнер сохраняется целиком, без remux/transcode. Применяется штатная
проверка WebMInitialization/WebMIndex: EBML header, один TrackEntry с audio
kind, известные размеры Cluster и отсутствие lacing. Неполный Cluster ждёт
новые байты, а не означает EOF. Временные метки и длительности пакетов
сохраняются; подтверждённые bytes проверяются до декодера.

- Opus: mono/stereo 48 kHz, OpusHead version 1, mapping family 0,
  CodecDelay совпадает с pre-skip, SeekPreRoll 80 ms.
- Vorbis I: mono/stereo 48 kHz, Xiph identification/comment/setup headers,
  floor 1 и ограниченные codebooks. CodecDelay ограничен половиной большего
  блока и целым числом samples. Применяется существующий decoder preroll
  200 ms; длительности определяются packet modes, discard padding учитывается
  один раз. Состояние блоков сохраняется между Cluster.

Status: `singleTrack=true`, `physicalResources=1`, один audio `mediaTracks`,
`videoCodec`/`videoContainer` пусты, `audioContainer=webm` и соответствующий
mediaMode из profiles. Pause, seek в подтверждённой области, расширение prefix,
complete, natural EOF и reader release сохраняют прежний API. ReaderId при
update/seek не меняется. Для EOF нужен complete всего ресурса.

Classic определяет audio kind независимо от номера source slot и применяет
аудиоперемотку/preroll также к standalone slot 0. Qt открывает `/track/0` и
проверяет реальный audio codec/rate/channel count. Физические аудиовыходы не
аттестуются. Результаты — в [отчёте](ProgressiveStandaloneWebmValidation-2026-11-01.md).

Этим этапом не добавлены standalone WebM video, прочие аудиокодеки/контейнеры,
segments/live, произвольные sparse media и реальная интеграция Harvester.
