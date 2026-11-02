# Progressive WAV / PCM16

SDK 1.70, приложение 0.2.0. Отдельный профиль `standalone-wav-pcm16-v1`
для Windows Classic, Windows Qt и Linux Qt. Harvester не изменяется.

Клиент проверяет capability фактического EXE:
`schema2Playback.standalone.wav`, `version=1`,
`mode=standalone-wav-pcm16-v1`, `profiles=["standalone-wav-pcm16"]`.
Требуется одна обязательная audio-дорожка `codecHint=pcm-s16le`,
`containerHint=wav`, один ресурс с SHA-256 подтверждённых ranges,
известная конечная duration VOD и `timestampOffset=0`. Размер ресурса
в manifest может быть неизвестен до complete, но RIFF/data sizes в самом
WAV должны быть известны и неизменны с начала сессии.
[Пример публикации](../tests/fixtures/progressive-v2/wav-pcm16.json).

## Поддерживаемый профиль

- Little-endian RIFF/WAVE, format tag 1, PCM 16 бит, mono/stereo,
  44.1/48 kHz; проверяются byte rate и block alignment.
- Один `fmt ` размером 16 или 18 байт (нулевой extension), затем один
  конечный `data`. Перед данными допускаются `JUNK`, `LIST`, `bext`
  с RIFF padding, не более 64 chunks и 1 MiB metadata.
- Ровно известные RIFF/data sizes; целое число PCM frames, duration
  соответствует их числу. Максимум 250000 packets по 1024 samples;
  последний packet может быть короче. Append не более 16 MiB за update.
- Подтверждённый непрерывный prefix. Неполный header/первый packet даёт
  `waiting_initialization`; последующий неполный packet ждёт update.
  Complete с усечёнными данными или лишними байтами отклоняется.

RIFF chunks и WAVE format fields следуют документации
[Microsoft RIFF](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-)
и [WAVEFORMATEX](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex).

## Передача в декодер

Общий bounded parser строит виртуальный fMP4 с `sowt` sample entry.
PCM bytes не преобразуются и не перекодируются: packet payload ссылается
на исходные диапазоны WAV. Все физические чтения, включая metadata,
проходят через ResourceSource и проверку SHA. Оригинал не изменяется,
временная копия всего файла не создаётся.

FragmentIndex допускает этот PCM sample entry только по явному opt-in
подтверждённого standalone PCM-профиля. Classic использует штатный
MP4 splitter и обычную цепочку uncompressed audio. Qt использует общий
progressive source и libmpv. Перемотка использует packet index с блоками
до 1024 samples; точность seek в один sample этим профилем не обещается.

Полный prefix до writer complete остаётся незавершённым источником:
достижение доступной границы означает ожидание, а не EOF. Reader сохраняется
при pause/seek/update и освобождается после закрытия последнего владельца.

## Проверка и ограничения

[Native conformance](../tests/progressive_wav_conformance.py) сравнивает
payload, packet clocks и декодированный PCM с независимыми исходными
integer samples, проверяет partial writes, malformed headers и release.
Файлы создаёт стандартный Python WAVE writer; результат независимо читает
FFmpeg. [Отчёт](ProgressiveWavValidation-2026-11-02.json) содержит отдельные
результаты Windows/Linux, runtime-проверки трёх редакций и exit codes.

RF64/BW64/RIFX, неизвестные/переписываемые RIFF sizes, float, 24/32-bit,
WAVE_FORMAT_EXTENSIBLE, compressed WAV, другие частоты/каналы, metadata
после `data`, sparse ranges и live остаются отдельными этапами.
Этот профиль не закрывает всю audio matrix и численные критерии
[общего чеклиста](ProgressiveCompletionChecklist.md).
