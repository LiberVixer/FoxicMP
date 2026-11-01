# SDK 1.66: MP3 с Xing/Info/VBRI и явным encoded-sample timeline

Новый профиль `standalone-mp3-indexed` принимает MPEG-1 Layer III mono/stereo
44100/48000 Hz с первым non-audio Xing/Info/VBRI frame. Он дополняет
[профиль MP3 без индексного header](ProgressiveMp3.md), контракт которого не меняется.
Harvester не изменён. Для функции нужны сборки из source commit SDK;
Python ZIP не заменяет EXE/установщики.

Последующее [расширение SDK 1.67](ProgressiveMp3Tag.md) добавляет фактическую
проверку encoder tag CRC и analysis-only trim plan. Описанный ниже status
`encoderTagCrcVerified=false` относится к исходному SDK 1.66; encoded timeline
и отсутствие gapless trim сохраняются и в 1.67.

Проверить `schema2Playback.standalone.mp3Indexed`: `version=1`,
`mode=standalone-mp3-indexed-v1`, `standalone-mp3-indexed` в `profiles`,
`timeline=encoded-samples`, `gaplessTrimApplied=false`. Использовать
`containerHint=mp3-indexed`, `codecHint=mp3`, `kind=audio`, `required=true`,
один `source.resourceId`. Пример: `progressive-v2-mp3-indexed.json`.
SDK отдельно отклоняет capability с другой семантикой timeline/trim.

Duration в manifest — `declaredFrames*1152/sampleRate` с точностью до одного
100 ns tick. Это полная длина закодированных audio samples после исключения
первого non-audio index frame, включая encoder delay/padding. Округлённая или
уже gapless-обрезанная длительность сервиса не заменяет это значение.
Заголовок позволяет узнать число frames до завершения загрузки.
Нужны known-duration VOD, нулевой timestampOffset, timebase 1/10000000,
непрерывный подтверждённый prefix и SHA-256. Initial resource length может
быть неизвестной. Header с нулевыми/изменяемыми placeholder counters при
параллельном кодировании не входит в профиль: подтверждённые bytes неизменяемы.

Поддержаны:

- Xing/Info с обязательными frames/bytes counters и известными flags 0–3;
  необязательные bounded TOC (100 неубывающих bytes) и quality field.
- VBRI version 1 с counters и bounded TOC: 1–1000 entries, width 1–4,
  положительные scale/frames-per-entry и полное покрытие byte count.
- Index frame с нулевой MPEG side information. Он пропускается decoder,
  а настоящие MP3 frames передаются без изменения. Rate/channels стабильны.
- Byte counter означает MPEG span, включая index frame и исключая ID3.
  На complete проверяются оба counters; при дозагрузке превышение counters
  немедленно отклоняется. TOC не используется для чтения неподтверждённых bytes:
  доступный seek строится по разобранным настоящим frames.

Обычные MPEG frame bounds, reservoir и ID3v2.3/v2.4/ID3v1 следуют прежнему
MP3-профилю. Начальный prefix/append до 16 MiB, metadata до 1 MiB,
до 250000 audio frames. Сохраняются CBR/VBR без CRC, 250 ms seek preroll,
pause, update, tail wait, natural EOF и release с тем же readerId.

Status содержит `mediaMode=standalone-mp3-indexed`, `audioContainer=mp3-indexed`,
`decoderContainer=fmp4`, `mp3Frames` и `mp3Index`: type, declaredFrames,
declaredBytes, encoder, encoderDelayPresent, encoderDelay, encoderPadding,
`gaplessTrimApplied=false`, `encoderTagCrcVerified=false`,
`timeline=encoded-samples`. Для известных LAME/Lavc/Lavf identifiers
12-bit delay/padding переносится как raw metadata. Неизвестный encoder не
получает выдуманного trim; LAME tag CRC не объявляется проверенным.
Физические bytes при этом защищены общим ResourceSource SHA-256.

Первичные источники: [MP3 demuxer FFmpeg](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mp3dec.c)
и [LAME VbrTag](https://github.com/lameproject/lame/blob/master/libmp3lame/VbrTag.c).

## Граница gapless

Этот профиль **не применяет gapless trim**. Эксперимент с MP4 edit list дал
714240 PCM bytes вместо 705600 bytes native gapless MP3; ручная выборка
соответствующих samples из полного decode совпала с native PCM. Поэтому
одна передача edit list не засчитана как реализация gapless. Нужны отдельные
проверенные decoder/output adapters Qt и Classic, включая начало, конец,
seek и точное число output samples. Изменённый экспериментальный MP4 не
используется в production.

Проверка сохранности нового профиля сравнивает сжатые audio frames и PCM
полного encoded audio span, без gapless-применения. Это не обещание равенства
обычному native gapless output. [Отчёт трёх редакций](ProgressiveMp3IndexedValidation-2026-11-01.md)
разделяет реальные FFmpeg Xing/Info и независимо собранные VBRI fixtures;
реальный корпус Fraunhofer VBRI и физическая аудиоприёмка остаются открытыми.
MPEG-2/2.5, Layers I/II, CRC, free-format, другие rates, live и общий остаток
ТЗ перечислены в [чеклисте](ProgressiveCompletionChecklist.md).
