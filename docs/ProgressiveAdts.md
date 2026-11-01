# SDK 1.64: самостоятельный растущий ADTS AAC-LC

Профиль `standalone-adts-aac-lc` воспроизводит один растущий MPEG-4 ADTS
resource с одним настоящим required audio track. Поддерживаются заголовки
AAC-LC (Audio Object Type 2), mono/stereo и 44100/48000 Hz. Harvester не изменён.

Перед start проверить `schema2Playback.standalone.adts`: `version=1`,
`mode=standalone-adts-aac-lc-v1`, `standalone-adts-aac-lc` в `profiles`.
Использовать `kind=audio`, `codecHint=aac-lc`, `containerHint=adts`,
`required=true` и один `source.resourceId`. Пример: `progressive-v2-adts.json`.
Обычная поддержка AAC или fMP4 не заменяет эту отдельную capability.
Нужны новые сборки из source commit SDK; ZIP не содержит установщиков/EXE/DLL.

Нужны VOD, известная duration в 100 ns, timestampOffset=0, непрерывный
подтверждённый prefix и SHA-256 каждого опубликованного блока. Начальная
физическая длина может быть неизвестной. Duration относится к закодированным
сэмплам источника: при complete она должна совпасть с `frames*1024/sampleRate`
с точностью до одного 100 ns tick. Приблизительная целая длительность сервиса
не заменяет это значение. Неизвестная duration этим профилем не добавляется.

Принимаются MPEG ID 0, layer 0, protection_absent=1, один raw data block на
frame, неизменные profile/rate/channel configuration. Frame length включает
семибайтовый header и должен быть от 8 до 8191 bytes. ID3/APE tags, MPEG-2,
CRC-protected ADTS, несколько raw blocks, PCE/channel configuration 0,
HE-AAC/SBR/PS, смена configuration и другие rates/layouts этим срезом не
заявляются. Полный синтаксис AAC payload проверяет штатный decoder; допуск
по ADTS header сам по себе не доказывает декодируемость произвольного payload.

Полевая структура проверена по первичным реализациям FFmpeg:
[ADTS header parser](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adts_header.c)
и [ADTS muxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/adtsenc.c).
Парсер ограничивает размеры и число frames, сверяет configuration каждого
header и никогда не сканирует за подтверждённым prefix. Неполный первый frame
возвращает `waiting_initialization` без удержания reader. Последующие неполные
headers/payload ждут update; ошибочный header не пропускается поиском syncword.
Усечённый complete или посторонние trailing bytes — ошибка, не успешный EOF.

Семибайтовые transport headers не нужны MP4 decoder: в память помещаются
`mp4a/esds` с AudioSpecificConfig и `moof/mdat` headers, а AAC payload читается
из исходных ResourceSource ranges с SHA-256. Нет перекодирования, изменения
оригинала, копии всего медиа и фиктивного видео. FLAC и ADTS используют общие
scalar audio bridge и MP4 projection; их profile validation остаётся раздельной.

Status сообщает `mediaMode=standalone-adts-aac-lc`, `audioContainer=adts`,
`decoderContainer=fmp4`, `audioCodec=aac-lc` и `adtsFrames`. Decoder byte counter
относится к виртуальному MP4. Каждый AAC packet имеет duration 1024 samples;
ADTS не передаёт MP4 edit list/gapless trim. Encoder delay и padding не
выдумываются: проекция сохраняет PCM, получаемый из самого native ADTS.

Ограничения: initial prefix и каждое приращение до 16 MiB, до 250000 frames,
один resource/track, без external init, segments, catalog и live. Pause,
подтверждённый seek, chapters, tail wait, update, complete, natural EOF и
release используют прежний API и readerId; пороги буфера не меняются.

[Проверки трёх редакций](ProgressiveAdtsValidation-2026-11-01.md) сравнивают
AAC payload по SHA-256, packet clocks и PCM до/после проекции. AAC является
lossy: PCM сравнивается с декодированным native ADTS, а не с сигналом до AAC
кодирования. Это не измерение физического аудиовыхода. Прочие форматы и
реальная интеграция Harvester остаются в [чеклисте](ProgressiveCompletionChecklist.md).
