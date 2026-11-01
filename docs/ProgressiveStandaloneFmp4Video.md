# SDK 1.61: самостоятельное HEVC/AV1/VP9-видео в растущем fMP4

Linux Qt, Windows Qt и Windows Classic воспроизводят один настоящий required
видеопоток из одного scalar fMP4 resource, без ожидания аудиодорожки.
Harvester не изменяется.

Проверить `schema2Playback.standalone`: `version=1`, `mode=standalone-fmp4-v1`
и точный профиль `standalone-fmp4-hevc`, `standalone-fmp4-av1` или
`standalone-fmp4-vp9` в `profiles`. Наличие кодека в общем `videoCodecs` либо
в парном/совместном fMP4 не означает поддержку отдельного видео.
H.264, AAC-LC, Opus и отдельные WebM capabilities сохраняются.

[Модель standalone](ProgressiveStandalone.md): `kind=video`, `required=true`,
`containerHint=fmp4`, явный `codecHint=hevc/av1/vp9`, один `source.resourceId`.
Известная duration VOD, timebase 1/10000000, timestampOffset=0, непрерывный
подтверждённый prefix с SHA-256. Начальная длина файла может быть неизвестной.
Нужен head moov перед media fragments; segments, внешний init, каталоги и live
этим профилем не добавляются.

- HEVC: Main/Main10 8/10-bit 4:2:0, hvc1/hev1, четырёхбайтовые NAL lengths,
  полный VPS/SPS/PPS; для hvc1 обязательны complete parameter-set arrays.
- AV1: Main 8/10-bit 4:2:0, av01, конфигурация av1C с sequence header;
  применяются существующие ограничения OBU/profile/depth/chroma.
- VP9: Profile 0/8-bit и Profile 2/10-bit 4:2:0, vp09, строгая vpcC FullBox
  version 1, допустимые color metadata и отсутствие codec init payload.

Плеер использует существующие codec validators, fragment index и штатные
MP4 splitter/decoder либо libmpv. Исходник не переписывается, не remux/transcode.
В Qt открыт только `/track/0`; отсутствующее аудио не открывает audio decoder
или audio output. Status содержит один video `mediaTracks`, `singleTrack=true`,
`physicalResources=1`, пустые audioCodec/audioContainer и нулевые audio bytes.

Pause, подтверждённый seek, обновление prefix, complete, natural EOF и reader
release работают через прежний API. ReaderId сохраняется при update/seek.
Неполный moof/mdat ждёт новые bytes и не превращается в EOF; EOF разрешается
только после complete. Тестовый writer выбирает целые mdat/fragments по
независимым packet clocks и публикует достаточную реальную длительность.

Примеры метаданных: `progressive-v2-standalone-fmp4-hevc.json`,
`progressive-v2-standalone-fmp4-av1.json`, `progressive-v2-standalone-fmp4-vp9.json`.
SDK не содержит медиа или EXE/DLL/установщиков. Нужны сборки из source commit
этого SDK. [Результаты трёх редакций](ProgressiveStandaloneFmp4VideoValidation-2026-11-01.md).

Проверены 25 и 30000/1001 fps, HEVC B-frames, VP9 alt-reference encoding,
8/10-bit decoder output, pause/seek/tail/EOF. Эти проверки не аттестуют GPU/HDR,
монитор или физические выходы. Прочие аудиокодеки/контейнеры, segments/live,
произвольные sparse media и реальная интеграция Harvester остаются в чеклисте.
