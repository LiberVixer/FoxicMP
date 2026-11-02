# Progressive WAV PCM24

SDK 1.75, приложение 0.2.0. Отдельный профиль `standalone-wav-pcm24-v1`
для Classic, Windows Qt и Linux Qt. Прежний PCM16-профиль не расширяется
неявно. Harvester не изменяется.

## Согласование

Проверить фактическую capability EXE:
`schema2Playback.standalone.wavPcm24`, `version=1`,
`mode=standalone-wav-pcm24-v1`, `profiles=["standalone-wav-pcm24"]`.
Одна обязательная audio-дорожка `containerHint=wav-pcm24`,
`codecHint=pcm-s24le`, один resource, непрерывный подтверждённый prefix
с SHA-256, известная конечная duration VOD и `timestampOffset=0`.
Начальная длина resource может быть неизвестна.
[Пример](../tests/fixtures/progressive-v2/wav-pcm24.json).

`wav-pcm24` — явный API-профиль обычного RIFF/WAVE, а не новое файловое
расширение. Старый `containerHint=wav` требует PCM16 и не принимает PCM24.

## Проверяемый профиль

- Little-endian RIFF/WAVE с известными RIFF/data sizes; format tag 1,
  mono/stereo, 44.1/48 kHz, signed packed PCM24: три байта на канал.
  Один `fmt ` размером 16 либо 18 bytes с нулевым cbSize; один последний
  непустой `data`, выровненный по целым audio frames.
- `blockAlign=channels*3`, `byteRate=sampleRate*blockAlign`.
  Формула выравнивания соответствует описанию
  [WAVEFORMATEX](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatex).
  Этот ограниченный профиль принимает legacy tag-1 PCM24; поддержка
  WAVEFORMATEXTENSIBLE, включая valid bits/channel mask, здесь не объявлена.
- Допустимы bounded `JUNK`, `LIST`, `bext` до data, с RIFF padding.
  Не более 64 chunks, 1 MiB metadata, 250000 packets и 16 MiB за update.
  Packet содержит до 1024 audio frames, последний может быть короче.
- RF64, неизвестные RIFF/data sizes, extensible/float/PCM32, другие rates,
  более двух каналов, несколько data chunks и trailing bytes вне профиля.

## Передача и время

Общий PCM parser использует отдельную специализацию для 24 bits.
Виртуальный fMP4 передаёт исходные bytes без перекодирования через
QuickTime version-1 audio sample entry `in24`, `wave/enda=1`.
В базовом поле sample size остаётся 16, фактическая ширина явно задана
трёхбайтовыми packet/sample fields и типом `in24`. См.
[описания sound samples Apple](https://developer.apple.com/documentation/quicktime-file-format/sound_sample_data)
и [sample description v1](https://developer.apple.com/documentation/quicktime-file-format/sound_sample_description_version_1).
Это внутренний виртуальный поток для проверенных decoder builds.

FragmentIndex и Classic splitter разрешают `in24` только после явного
opt-in профиля PCM24. PCM16 сохраняет прежний `sowt` и отдельный opt-in.
Чтения проходят ResourceSource/SHA; original не меняется и полная копия
медиа не создаётся. Нижний значащий байт PCM сохраняется.

Неполные header/первый packet дают `waiting_initialization` без утечки
reader. Последующий неполный packet ждёт update. Полный prefix без writer
complete не становится EOF. Нечётный размер data у mono требует RIFF pad:
pad не становится PCM, complete проверяет весь RIFF.
Неправильное завершение закрывает источник для дальнейшего decoder read.
Подмена ранее подтверждённого блока выявляется перед выдачей его bytes
decoder; периодический bounded audit не является полным сканированием
всего файла за один вызов verify.

Reader сохраняется при pause/seek/update и освобождается после последнего
владельца. Clock строится по числу audio frames и sample rate. Точная
индексация packets не означает обещание sample-exact UI seek.

## Проверки и границы

[Native conformance](../tests/progressive_wav24_conformance.py) проверяет
четыре rate/channel сочетания с независимым Python WAVE writer и FFmpeg
decoder, исходные PCM bytes, packet clocks, partial writes, RIFF padding,
metadata, fmt18, malformed headers, late invalid complete и reader release.
Результаты трёх редакций и санитайзеров — в
[отчёте](ProgressiveWav24Validation-2026-11-02.json).

Физические аудиовыходы и bit-exact режим устройства не проверяются.
Остальные WAV layouts, полная audio matrix, длительный stress и реальная
интеграция Harvester остаются в
[общем чеклисте](ProgressiveCompletionChecklist.md).
