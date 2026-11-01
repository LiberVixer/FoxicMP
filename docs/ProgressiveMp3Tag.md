# SDK 1.67: проверка LAME Info Tag и план PCM

Расширение [индексного MP3 SDK 1.66](ProgressiveMp3Indexed.md) для всех трёх
редакций. Семантика playback не меняется: duration/seek остаются по полным
encoded samples, `gaplessTrimApplied=false`. Новая часть — проверка
метаданных и **анализ** потенциальных границ обрезки.

В capability `schema2Playback.standalone.mp3Indexed.encoderTagValidation`
объявлены `version=1`, `mode=lame-info-tag-crc16-v1`, `opportunistic=true`,
`requiredForPlayback=false` и `trimPlan.analysisOnly=true`.
Все значения capabilities SDK 1.66 сохранены; добавлен только этот объект.
Исторический `mp3Indexed.encoderTagCrcVerified=false` остаётся консервативной
гарантией профиля: проверенная CRC не требуется для encoded playback и
не обещается для каждого источника. Фактический результат читается в status.

## CRC и совместимость

Для известных identifiers `LAME`, `Lavc`, `Lavf` и revision 0 проверяется
полный 36-byte encoder tag. CRC-16/IBM: отражённый полином `0xa001`, init 0,
без final XOR. Покрытие начинается с sync первого MPEG frame и заканчивается
перед последними двумя bytes encoder tag CRC; ID3 не входит. Stored CRC
читается big-endian. Нулевое значение может быть корректным.

CRC покрывает header, side info, Xing/Info counters/TOC, encoder delay/padding
и прочие поля до CRC. Она **не проверяет весь MP3**: Music CRC не вычисляется,
декодируемый PCM и отправитель этим checksum не аутентифицируются.
За чтение физических bytes отвечает прежний SHA-256 ResourceSource.

Mismatch, неизвестный encoder/revision или неполный полный tag не разрешают
trim plan. Они не меняют прежнее решение о допустимости encoded playback.
Его структурные ограничения, SHA, counters и duration продолжают проверяться.
VBRI не получает выдуманных LAME delay/padding.

В проверенных FFmpeg mono fixtures записанная CRC не совпадает с расчётом
по LAME: FFmpeg muxer считает 190 bytes также при более коротком mono header.
FoxicMP сообщает mismatch, не переписывает файл и не считает его вообще
непригодным для воспроизведения. Настоящие libmp3lame tags проверены отдельно.

## Status

В `mp3Index`:

- `encoderTagCrcVerified`: фактическое совпадение CRC для поддержанного tag.
- `encoderTag`: `version=1`, `state`, `complete`, `revision`, `crcChecked`,
  `crcVerified`, `musicCrcVerified=false`. При выполненном расчёте также
  `storedCrc16`, `calculatedCrc16`, `coveredBytes`.
- `state`: `valid`, `crc-mismatch`, `truncated`, `unsupported-revision`,
  `unsupported-encoder` или `absent` для VBRI.
- `trimPlan`: `analysisOnly=true`, `ready`, `reasonCode`, `decoderPolicy`,
  `decoderDelaySamples`, `sampleRate`, `codedSamples`.

Для `ready=true` дополнительно возвращаются decimal strings `beginSample`,
`endSample`, `audibleSamples`, `audibleDuration100ns` и
`decoderTimestampOffset100ns`. Округление ticks — вниз, без float.
Это данные для будущего адаптера, не фактические duration/position плеера.

## Расчёт

Политика `ffmpeg-mpeg1-layer3-529-v1` соответствует
[проверенному PCM-эталону](ProgressiveMp3GaplessReference.md). Decoder delay
529 задаётся явно и не переносится автоматически на внешний decoder Classic.
Пусть `N=declaredFrames*1152`, `E=encoderDelay`, `P=encoderPadding`, `D=529`:

```text
begin = E + D
end = N - P + D
audibleSamples = N - E - P
```

Требуются проверенный tag revision 0, rates 44100/48000 Hz, `P>=D` и
`E+P<N`. Иначе `ready=false`, без фиктивных нулевых границ.
Причины включают состояния tag, `encoder-delay-unavailable`,
`unproven-decoder-delay`, `unsupported-sample-rate`, `unproven-trim-window`.

Существующая duration SDK 1.66 **не заменяется** `audibleDuration100ns`.
Будущий gapless-контракт должен отдельно согласовать audible time,
chapters, available boundary и coded decoder clock. Пока не использовать
`trimPlan.ready` как обещание поддержки gapless playback.

## Проверка и оставшаяся работа

[Отчёт](ProgressiveMp3TagValidation-2026-11-01.md) разделяет 16 media profiles,
настоящий libmp3lame encoder и искусственно повреждённые tags. Проверяется
native PCM для каждого валидного плана, неизменность bytes, reader release
и прежнее воспроизведение всех трёх редакций.

[Qt PCM adapter](ProgressiveQtPcmWindow.md) уже имеет собственные проверки
выхода. Подключение плана к progressive controller, growing gapless playback
и независимый Classic PCM/output adapter остаются открытыми.
Harvester не менялся; Python SDK ZIP не обновляет EXE/установщики.

Первичные источники: [LAME VbrTag](https://github.com/lameproject/lame/blob/master/libmp3lame/VbrTag.c),
[LAME CRC tables](https://github.com/lameproject/lame/blob/master/libmp3lame/VbrTagCrc.h),
[FFmpeg MP3 muxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mp3enc.c).
