# Progressive Opus в fMP4

SDK 1.17 добавляет Opus mono/stereo в schema 2 Linux Qt, Windows Qt и Windows
Classic. Изменения относятся только к FoxicMP. YouTubeHarvester не изменяется.
Сохраняются [H.264](ProgressiveMediaV2.md), [HEVC](ProgressiveHevc.md),
[AV1](ProgressiveAv1.md), [VP9](ProgressiveVp9.md) и AAC-LC.

## Поддержанный профиль

Один required video H.264/HEVC/AV1/VP9 и один required audio Opus, каждый в
отдельном fMP4-ресурсе. Требуются VOD, известная duration, timebase 1/10000000,
неотрицательный timestampOffset, подтверждённый префикс от нуля, head moov
не более 1 МиБ. Для звука codecHint=opus, containerHint=fmp4. Пустой hint
определяет кодек по файлу; неверный непустой hint отвергается.

В аудио sample entry Opus version 0 нужен ровно один dOps: version 0, 11 байт,
ChannelMappingFamily=0, один или два выходных канала. Число каналов sample entry
совпадает с dOps, sample size=16, sample rate=48000<<16; media clock — 48000.
InputSampleRate в dOps информационное поле: не подменяет частоту декодирования.
Signed OutputGain передаётся штатному backend. Multichannel/mapping family 1/255,
внешние dref и шифрованные entries этим медиамостом не разрешены.

Для этого раннего профиля требуется один unit-rate edit в edts/elst, media_time
которого равен dOps PreSkip. Он переносит priming-пакеты перед началом шкалы,
включая отрицательные начальные PTS/DTS. Это не отрицательный timestampOffset
сеанса: тот по-прежнему не поддержан. Несовпадение edit/pre-skip, отсутствие
edit, несколько edits, некорректные length/version/flags/rate отвергаются.
Конфигурации и payload закрепляются прежними ResourceSource/reader leases.

Структура dOps и смысл pre-roll сверены с [Opus ISO BMFF draft 0.6.8](https://www.opus-codec.org/docs/opus_in_isobmff.html)
и практической записью [FFmpeg MOV muxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c).
Это ограниченный совместимый с проверенными FFmpeg-файлами профиль, а не заявление
соответствия всем требованиям старого draft. В частности, encoder PreSkip=312
samples принимается; поле PreSkip не используется как минимальная длительность
прогрева после произвольной перемотки.

## Начало и перемотка

Qt использует прежний libmpv/FFmpeg MOV demuxer, который обрабатывает конфигурацию
Opus, edit и свой packet cache. Classic сохраняет MP4Splitter/DirectShow и получает
пакеты из общего FragmentIndex. У обоих путей payload проходит ResourceSource.

При перемотке progressive Opus в Classic выбираются целые пакеты, покрывающие
не менее 80 мс перед запрошенной позицией, когда эти пакеты есть в источнике;
не отбрасываются пакеты до нуля timeline. Отрицательные относительно нового
segment пакеты декодируются как pre-roll. AAC и старые v1/legacy пути сохраняют
своё поведение. Общая функция выбора проверена на реальных индексах mono/stereo.
Это фиксированный 80 мс pre-roll, без отдельной квалификации произвольных roll groups.

Принятие конфигурации не заменяет разбор всего Opus bitstream, криптографическую
проверку payload или измерение физического выхода звука. Плеер не перекодирует
и не дописывает входные файлы.

## API и совместимость

schema2Playback.audioCodecs=["aac-lc","opus"], opusChannels=[1,2],
opusChannelMappingFamilies=[0], opusSampleRate=48000,
opusPrerollMilliseconds=80, opusEditListRequired=true.
К прежним profiles добавлены:

- separate-fmp4-h264-opus;
- separate-fmp4-hevc-opus;
- separate-fmp4-av1-opus;
- separate-fmp4-vp9-opus.

Старый schema2Playback.mode остаётся separate-fmp4-h264-aac-lc, поэтому
существующий Python SDK сохраняет discovery/запуск. Новые status содержат
audioCodec=opus либо aac-lc; прежние поля и AAC mediaMode сохранены.
Верхнеуровневый v1 audioCodecs и его кодеки не расширяются.

Загрузчик использует прежние V2PreviewSession.publish/heartbeat/close; для выбора
Opus проверяет schema2Playback текущего EXE. Отсутствие codecHint не является
разрешением непроверенного кодека: проверяется настоящий dOps/sample entry.

[Результаты трёх редакций](ProgressiveOpusValidation-2026-10-27.md) включают
декодирование до complete, переход назад, cancel/restart, переименование,
естественный EOF и фактическое release. Физическая A/V-синхронизация и
30-минутный дрейф этим этапом не квалифицируются.

WebM/MKV и Ogg, muxed A/V, audio-only/video-only, универсальный demux/remux,
сегменты HLS/DASH, live, metadata v2 и настоящий Harvester остаются впереди.
Opus в fMP4 не означает progressive WebM/Opus или приём URL плеером.
