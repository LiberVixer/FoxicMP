# Progressive VP9 в fMP4

SDK 1.16 расширяет schema 2 профилем separate-fmp4-vp9-aac-lc для Linux Qt,
Windows Qt и Windows Classic. YouTubeHarvester не изменяется. Прежние
[H.264](ProgressiveMediaV2.md), [HEVC](ProgressiveHevc.md) и [AV1](ProgressiveAv1.md)
сохраняются вместе с публичным Python интерфейсом.

## Поддержанный профиль

Один required video VP9 Profile 0 (8-bit) или Profile 2 (10-bit), 4:2:0,
и один required audio AAC-LC, каждый в отдельном fMP4. Нужны VOD, известная
duration, timebase 1/10000000, неотрицательный timestampOffset, подтверждённый
префикс от нуля и head moov не более 1 МиБ. Для видео codecHint=vp9,
containerHint=fmp4; пустой codecHint определяет кодек по файлу. Неверный непустой
hint отклоняется, ресурс остаётся привязан к своей OS-идентичности.

Проверяются sample entry vp09 и единственный vpcC: FullBox version 1, flags 0,
фиксированная запись 12 байт с FullBox-заголовком и нулевой длиной codec init data.
Профиль должен соответствовать bit depth; принимаются chromaSubsampling 0/1,
full/limited range, определённые уровни и level 0 (undefined). Разрешены colour
primaries 1,2,4–12,22; transfer characteristics 1,2,4–18; matrix coefficients
1,2,4–14. RGB matrix 0 несовместима с принятым 4:2:0. Иные/зарезервированные
значения этого раннего профиля отвергаются. Внешние dref, encrypted entries,
12-bit, Profile 1/3, 4:2:2/4:4:4 и vpcC version 0 не поддержаны этим мостом.

Структура сверена с [WebM Project: VP Codec ISO Media File Format Binding](https://www.webmproject.org/vp9/mp4/).
Ограничения поддержанного профиля уже полного стандарта: в частности, VP9
Profile 2 также допускает 12-bit, но текущий progressive профиль — только 10-bit.
Пакеты и superframes разбирает штатный backend; общий валидатор не заменяет
VP9 decoder и не проверяет криптографическую целостность payload.

## Discovery и работа загрузчика

schema2Playback.videoCodecs добавляет vp9, profiles — separate-fmp4-vp9-aac-lc,
vp9Profiles=[0,2], vp9BitDepths=[8,10], vp9SampleEntries=["vp09"]. Старый mode
остаётся separate-fmp4-h264-aac-lc для совместимости. В status videoCodec=vp9,
mediaMode=separate-fmp4-vp9-aac-lc. Верхнеуровневый v1 videoCodecs и старые
schema-1/legacy пути не расширяются.

Загрузчик использует прежний V2PreviewSession.publish/heartbeat/close и публикует
подтверждённые ranges. Qt читает через libmpv/FFmpeg MOV, Classic — через MP4Splitter
и DirectShow. FragmentIndex и Classic splitter разрешают vp09 только после
проверенного v2-моста. Нет нового decoder, перекодирования, сетевого загрузчика
или изменения входных файлов со стороны плеера.

Сохраняются буферизация, cancel/restart, переименование закреплённых файлов,
complete/EOF и release после фактического закрытия всех читателей.

## Проверки и оставшиеся задачи

[Отчёт трёх редакций](ProgressiveVp9Validation-2026-10-27.md) отделяет реальные
декодированные кадры Qt от проверки DirectShow graph/status Classic. VP9 корпус
содержит Profile 0/2, 320×180, 25 fps, 20 секунд, отдельный AAC-LC; включены
encoder lookahead и auto-alt-ref. Это не квалификация всех coding tools,
разрешений, GPU/HDR или физической синхронизации звука.

Opus, WebM/MKV, muxed A/V, audio-only/video-only, универсальный demux/remux,
сегменты HLS/DASH, live, применение metadata v2 и настоящий Harvester ещё впереди.
Поддержка VP9 в fMP4 не означает поддержку недокачанного WebM.
