# Progressive WAVEFORMATEXTENSIBLE PCM24

SDK 1.76, приложение 0.2.0. Отдельный профиль для Classic, Windows Qt и
Linux Qt; прежние WAV PCM16 и legacy PCM24 сохраняют свои capability.
Harvester не изменяется.

## Согласование

Проверить фактическую capability EXE:
`schema2Playback.standalone.wavExtensible24`, `version=1`,
`mode=standalone-wav-extensible-pcm24-v1`,
`profiles=["standalone-wav-extensible-pcm24"]`.
Одна обязательная audio-дорожка `containerHint=wav-extensible-pcm24`,
`codecHint=pcm-s24le`, один resource и непрерывный подтверждённый prefix
с SHA-256. Duration VOD конечная и известная, `timestampOffset=0`.
Начальная длина resource может быть неизвестна.
[Пример](../tests/fixtures/progressive-v2/wav-extensible-pcm24.json).

## Проверяемый профиль

- Little-endian RIFF/WAVE с известными RIFF/data sizes. Один `fmt ` ровно
  40 bytes, format tag `0xfffe`, cbSize 22, PCM SubFormat GUID
  `00000001-0000-0010-8000-00aa00389b71`.
- Container bits и valid bits равны 24; три байта на канал. Mono/stereo,
  44.1/48 kHz. Маска mono — `0x4` (front center), stereo — `0x3`
  (front left/right). Неизвестные/другие маски отклоняются.
- `blockAlign=channels*3`, `byteRate=sampleRate*blockAlign`; один последний
  непустой `data`, выровненный по целым audio frames. Нечётный data size
  требует RIFF pad, который не входит в PCM.
- Допустимы `JUNK`, `LIST`, `bext` до data с padding; максимум 64 chunks,
  1 MiB metadata, 250000 packets и 16 MiB за update. Packet содержит
  до 1024 audio frames.

Разделение container/valid bits, GUID и маски каналов основано на
[WAVEFORMATEXTENSIBLE Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible)
и [описании extensible formats](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/extensible-wave-format-descriptors).
Нынешний профиль не включает 20-in-24/24-in-32, float, многоканальные
layouts, cbSize больше 22, другие rates и RF64.

## Передача и время

Общий PCM parser использует отдельную extensible-специализацию, проверяя
заголовок до публикации первого packet. Виртуальный поток использует
тот же проверенный PCM24 `in24`/QuickTime v1 с `wave/enda=1`, что и
[legacy PCM24](ProgressiveWav24.md). Все три байта sample и исходный
порядок каналов сохраняются без перекодирования. Original не меняется;
физические чтения идут через ResourceSource/SHA без полной копии медиа.

Неполный header/первый packet даёт `waiting_initialization` без утечки
reader. Поздний неполный packet ждёт update; полный prefix до writer
complete не становится EOF. Complete проверяет весь RIFF, включая pad.
Clock и packet index строятся по числу audio frames и sample rate.
Pause/seek/update сохраняют reader, последний владелец освобождает его.
Неверный complete закрывает источник; подмена подтверждённого payload
выявляется перед выдачей изменённого блока decoder.

## Проверки и границы

[Conformance](../tests/progressive_wav_ext24_conformance.py) проверяет
четыре growing rate/channel profiles с WAV writer FFmpeg, отдельно
четыре результата независимого struct-based writer и исходный Python PCM
oracle. Проверяются packet bytes/clocks, точный PCM, GUID/valid bits/masks,
partial headers, metadata/data padding, SHA, EOF и release.
Итоги трёх редакций и санитайзеров — в
[отчёте](ProgressiveWavExtensible24Validation-2026-11-02.json).

Sample-exact UI seek, bit-exact физический аудиовыход, произвольные
extensible layouts, sparse/live/segments, полная audio matrix, длительный
stress и реальная интеграция Harvester остаются в
[общем чеклисте](ProgressiveCompletionChecklist.md).
