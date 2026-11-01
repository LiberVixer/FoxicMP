# SDK 1.65: самостоятельный растущий MP3

Профиль `standalone-mp3` принимает один растущий MPEG-1 Layer III resource
с одним required audio track. Поддержаны mono/stereo, 44100/48000 Hz,
CBR/VBR с табличным bitrate 32–320 kbps, protection bit=1 (без CRC).
Harvester не изменён. Нужны сборки из source commit SDK, а не только Python ZIP.

Перед start проверить `schema2Playback.standalone.mp3`: `version=1`,
`mode=standalone-mp3-v1`, `standalone-mp3` в `profiles`. Передавать
`containerHint=mp3`, `codecHint=mp3`, `kind=audio`, `required=true` и один
`source.resourceId`. Пример SDK: `progressive-v2-mp3.json`.

Нужны известная VOD duration, timebase 1/10000000, timestampOffset=0,
непрерывный подтверждённый prefix и SHA-256. Начальная физическая длина может
быть неизвестной. Duration должна соответствовать `frames*1152/sampleRate`
с точностью до одного 100 ns tick; округлённая длительность сервиса не подходит.
Неполный первый tag/header/frame возвращает `waiting_initialization` без
удержания reader. Поздние неполные структуры ждут update; ошибочный header
не пропускается поиском следующего syncword. Complete с усечёнными данными
или посторонним хвостом отклоняется.

Парсер проверяет MPEG version/layer/protection, bitrate/rate/emphasis, длину
каждого frame и неизменные rate/channels. Допускаются изменения bitrate и
режима stereo/joint-stereo без изменения числа каналов. `main_data_begin`
из side information не может ссылаться до уже подтверждённых audio bytes;
первый frame должен иметь нулевую ссылку назад. Максимум reservoir — 511 bytes.
Entropy/Huffman decoding и полная проверка side information остаются decoder.
Первичные реализации для сверки: [MPEG audio header decoder FFmpeg](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/mpegaudiodecheader.c)
и [MP3 demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mp3dec.c).

Разрешены один начальный ID3v2.3/v2.4 tag до 1 MiB и конечный ID3v1 tag
ровно 128 bytes. Проверяются version/flags, synchsafe size и соответствие
v2.4 footer исходному header по [ID3v2.4 structure](https://id3.org/id3v2.4.0-structure).
Содержимое tags пропускается как opaque metadata, не извлекается в интерфейс
и не передаётся audio decoder. Исходник, включая tags, не переписывается.

Xing/Info/VBRI headers этим профилем явно отклоняются: их gapless/index
семантика требует отдельного этапа. Нет выдуманного encoder delay/padding
или обрезания PCM. MPEG-2/2.5, Layers I/II, free-format, CRC-protected MP3,
прочие rates, повторные/внутренние ID3, APE, смена configuration и live не заявлены.

Полные MP3 frames, включая MPEG header и side information, читаются через
ResourceSource в виртуальный MP4 `mp4a/esds`, objectTypeIndication=0x6b.
Служебные boxes находятся в памяти. Нет перекодирования, копии всего медиа,
фиктивного видео и записи в оригинал. Штатный Classic MP4 splitter допускает
этот MPEG audio descriptor только для проверенного standalone MP3-сеанса;
обычное воспроизведение и прочие progressive profiles не расширяются этим gate.

MP3 использует 250 ms preroll при seek: Classic начинает с более ранних
целых audio packets, Qt устанавливает file-local `hr-seek-demuxer-offset`.
Декодер восстанавливает reservoir до точки показа. Значение Qt проверяется
во время реального playback. Проверка PCM после перезапуска decoder
подтверждает сохранность последующего звука; она не является измерением
физического звука непосредственно в момент перехода.

Status: `mediaMode=standalone-mp3`, `audioContainer=mp3`, `decoderContainer=fmp4`,
`audioCodec=mp3`, `mp3Frames`. Pause, confirmed seek, chapters, ожидание tail,
update, complete, natural EOF и release сохраняют readerId и прежний API.
Лимиты: initial prefix/append до 16 MiB, до 250000 frames; header-derived
frame size ограничен 1441 bytes. Общие пороги буфера не меняются.

[Проверки трёх редакций](ProgressiveMp3Validation-2026-11-01.md) сравнивают
packet hashes/clocks и PCM с native MP3. Для lossy MP3 это сравнение до/после
проекции, а не с сигналом до кодирования. SDK не содержит EXE/DLL/установщиков
и медиа. Другие варианты и реальное подключение Harvester остаются в
[общем чеклисте](ProgressiveCompletionChecklist.md).
