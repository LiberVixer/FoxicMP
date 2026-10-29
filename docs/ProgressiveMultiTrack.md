# Несколько аудиодорожек и SRT — SDK 1.22

Linux Qt, Windows Qt и Windows Classic используют один расширенный schema-2
контракт. Python V2PreviewSession, прежние двухдорожечные профили и v1 сохранены.
Harvester не изменяется. Возможности проверять у текущего EXE:
`schema2Playback.multiAudio`, `audioTrackLimit`, `subtitles`.

## Аудио

Один видеоресурс и от одной до восьми аудиодорожек. Каждая имеет свой ресурс,
`id`, `codecHint`, `containerHint`, `label`, `language`, `default`. Все медиадорожки
этого первого профиля должны быть required и иметь готовую инициализацию до
start. Доступная позиция — общий подтверждённый участок всех аудио и видео;
незагруженная аудиодорожка может задержать общий старт/буферизацию.
Независимое ожидание только выбранного optional audio остаётся отдельным этапом.

Кодеки/контейнеры сохраняют ограничения SDK 1.14–1.21. Допускается смешение
fMP4 AAC-LC/Opus и WebM Opus/Vorbis в одном сеансе. Не более одного default audio:
он располагается первым, остальные — в стабильном порядке ID. Смена набора
медиадорожек или default/order требует нового открытия; label/language могут
обновляться. Пользовательские настройки плеера не сбрасываются.

Qt показывает label/language в обычном меню аудио; дополнительные дорожки
передаются libmpv вместе с loadfile через экранированный audio-files path list. Classic подключает отдельный splitter каждой
дорожки к штатному AudioSwitcher; label/language добавляются в имя источника.
`audioSelection` в активном status сообщает фактические count и выбранный
нулевой index. `mediaTracks` содержит ID, index, codec/container, label/language,
default и decoderIoBytes каждого медиаресурса. Прежние videoCodec/audioCodec
и decoderIoBytes.video/audio сохраняют значение видео/первой аудиодорожки.

В SDK 1.23 Qt выполняет [байтовую перемотку внутри подтверждённого участка](ProgressiveSeek.md)
без лимита 0–10 секунд. Переоткрываются только потоки декодера на том же Source; выбор аудио/SRT,
sessionId/generation/readerId и пользовательская пауза сохраняются.
`qtMultiAudioSeek` сообщает режим `confirmed-byte-seek` и `confirmedRangesOnly:true`;
числовой maxTargetSecondsWhileIncomplete сохранён для совместимости, фактический
предел задаёт availableSeconds текущего источника. После готовности A/V доступна
вся длительность. Classic сохраняет собственный индекс и штатную перемотку;
его код в этом этапе не меняется.

## Субтитры

До четырёх optional (`required:false`) дорожек `kind:subtitle`,
`containerHint:srt`, `codecHint:srt` либо пустой. На этом этапе только UTF-8 SRT,
известный VOD и timestampOffset=0. Исходный файл должен завершиться полностью;
дописанный частичный cue не показывается. Незавершённая optional SRT может
отсутствовать при start и не задерживает A/V. После complete ресурс читается
через ResourceSource, проверяется и материализуется в приватный каталог плеера.
Плеер подключает поздний SRT без остановки текущего просмотра.

Ограничения: 512 KiB на файл, 4096 cues, строка до 4096 байт, numeric cue index,
HH:MM:SS,mmm, монотонные start, положительная длительность, конец в пределах VOD.
Проверяются UTF-8, NUL, framing и диапазоны. Копия нормализует CRLF и добавляет
UTF-8 BOM для одинакового распознавания в native и Qt backend. Исходник не меняется.
Копии имеют случайные имена и ACL/permissions текущего пользователя; удаляются
при последнем освобождении Source. Читатель исходного ресурса учитывается
отдельной квитанцией до закрытия. SDK 1.24 [изолирует ошибки optional SRT](ProgressiveOptionalSrt.md): испорченный
или отсутствующий SRT получает собственный errorCode/error, A/V продолжает
работать. Проверенная приватная копия сохраняется при последующей порче оригинала.
Backend-ошибки подключения также не закрывают A/V. Метаданные снимка и required
ресурсы сохраняют строгую проверку.

`subtitleTracks` сообщает id/label/language/default/forced, state/errorCode/error, ready и приватный
путь копии для backend. Поля не являются разрешением удалять исходник:
загрузчик по-прежнему ждёт released/filesClosed всех readers. В Classic также
доступна диагностика loadedSubtitleCount. Видимость субтитров и native renderer
подчиняются настройкам пользователя; defaults и профиль установки не меняются.

ASS/bitmap/embedded subtitles, потоковые cues, несколько video, optional audio,
сегменты/live и muxed/remux не добавлены. Общая metadataApplied остаётся false:
главы и общие metadata ещё не подключены.

[Проверки и хеши трёх сборок](ProgressiveMultiTrackValidation-2026-10-28.md).
Установщики 0.2.0 SDK не обновляет; нужны новые сборки с объявленными capabilities.

Backend: [списки параметров и внешние дорожки libmpv](https://mpv.io/manual/stable/#list-options).
