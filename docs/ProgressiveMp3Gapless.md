# Progressive MP3: полезная звуковая шкала в Qt

SDK 1.68, приложение 0.2.0. Отдельный профиль `standalone-mp3-gapless-v1`
работает в Windows Qt и Linux Qt. Он применяет проверенные encoder delay/padding
к цифровому PCM-выходу, включая перемотку растущего файла. Classic сохраняет
прежние `mp3`/`mp3-indexed` и отвечает `unsupported_media` на новый профиль.
Harvester не изменяется.

## Согласование с загрузчиком

Клиент запрашивает capabilities запущенного EXE. Нужен
`schema2Playback.standalone.mp3Gapless` с `version=1`,
`mode=standalone-mp3-gapless-v1`, `timeline=audible-samples`,
`gaplessTrim=true`, `encoderTagCrcRequired=true` и
`pcmWindowAdapter=qt-lavfi-absolute-samples-v1`.
`V2PreviewSession` проверяет этот контракт до открытия preview.

В SDK manifest поле `schema2Playback` сохраняет общий контракт, включая
Classic; `qtSchema2Playback` описывает дополнение Qt. Фактические capabilities
конкретного EXE имеют приоритет над описанием SDK. ZIP не обновляет плеер.

Пример структуры: [mp3-gapless.json](../tests/fixtures/progressive-v2/mp3-gapless.json).
Его placeholders длительности и SHA необходимо заменить сведениями настоящего
файла. Одна обязательная audio-дорожка, `codecHint=mp3`,
`containerHint=mp3-gapless`, один ресурс с SHA-256 опубликованных ranges;
без segments и отдельного initialization resource. Допускается неизвестная
длина растущего ресурса. Требуются известная конечная длительность VOD,
`timestampOffset=0` и главы на полезной звуковой шкале.

## Поддерживаемый звук и время

MPEG-1 Layer III, mono/stereo, 44,1/48 kHz, без MPEG CRC; Xing/Info с полным
36-байтным LAME/Lavc/Lavf encoder tag revision 0 и верным tag CRC.
Счётчики index header, frame bounds, bit reservoir и SHA проверяются независимо.
Тег с неверным CRC, неизвестный encoder или VBRI не дают этот профиль.
Прежний encoded-sample профиль продолжает принимать свои допустимые файлы.

Для `F` звуковых MPEG frames, delay `E`, padding `P`, rate `R`:

```
N = F * 1152
begin = E + 529
end = N - P + 529
audibleSamples = N - E - P
duration100ns = floor(audibleSamples * 10000000 / R)
```

Политика 529 относится к проверенному FFmpeg MPEG-1 Layer III декодеру libmpv.
Она не переносится автоматически на DirectShow или произвольный внешний decoder.
Нужны `P >= 529`, положительная полезная длина и валидный CRC.
Tag CRC не заменяет SHA звуковых данных и не подтверждает music CRC.

Compressed payload сохраняется в виртуальном MP4 без перекодирования и изменения
исходника. Внутреннее время MP4 остаётся coded; индекс и UI вычитают смещение
`begin`. Частная первая PCM-ступень обрезает абсолютные samples перед пользовательской
цепочкой обработки. `gaplessTrimApplied=true` в активном status требует фактически
загруженного частного фильтра; один только bridge возвращает `false`.

Перемотка округляется к ближайшему полезному sample. Допустимая граница при
загрузке PCM-окна — последний sample; запрос конечной длительности ограничивается
им. Декодирование начинается с frame-aligned preroll не менее 250 ms, после чего
фильтр отбрасывает лишние samples. Перезагрузка сохраняет reader и выбор паузы.

Когда writer опубликовал все байты, но ещё не сообщил complete, bridge удерживает
последний fragment, содержащий полезный конец. Это предотвращает ранний EOF от
`atrim`. Complete открывает хвост тому же reader; только затем возможен штатный EOF.

## Проверки и границы

[Результаты](ProgressiveMp3GaplessValidation-2026-11-02.md) разделяют native,
реальное progressive-воспроизведение, цифровой PCM-контроль и Classic.
Fixtures создаёт настоящий libmp3lame 3.100; PCM сверяется с независимым
декодированием исходного MP3. Нет утверждения о физическом выходе, A/V drift,
непроверенных версиях decoder, MPEG-2/2.5, других rates, CRC-protected MPEG frames,
VBRI gapless, live и реальном Harvester. Остальная матрица ТЗ остаётся открытой.
