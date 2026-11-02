# Progressive E-AC-3

SDK 1.74, приложение 0.2.0. Отдельный профиль `standalone-eac3-v1`
для Classic, Windows Qt и Linux Qt. Harvester не изменяется.

## Согласование

Проверить capability фактического EXE:
`schema2Playback.standalone.eac3`, `version=1`,
`mode=standalone-eac3-v1`, `profiles=["standalone-eac3"]`.
Одна обязательная audio-дорожка `containerHint=eac3`, `codecHint=eac3`,
один resource, непрерывный подтверждённый prefix с SHA-256,
конечная duration VOD и `timestampOffset=0`.
Начальная длина resource может быть неизвестна.
[Пример](../tests/fixtures/progressive-v2/eac3.json).

## Проверяемый профиль

- Big-endian syncword `0x0b77`, bsid 16, независимый stream type 0,
  substream ID 0, 48 kHz, шесть блоков/1536 samples на frame.
  Mono/stereo (`acmod=1/2`), без LFE и dependent substreams.
- Постоянные битрейт, channel mode и bsmod. Допустимые битрейты:
  64, 128, 192, 384, 640 и 1024 kbit/s. Максимум 4096 bytes/frame,
  250000 frames и 16 MiB за update.
- Dialnorm, compression и информационные BSI читаются и сохраняются
  в исходном payload. Reserved surround/headphone mode отклоняются.
  Mixing metadata, additional BSI и JOC/Atmos вне профиля.
- Проверяется CRC каждого полного frame полиномом `0x8005` по всему
  frame после syncword, включая завершающее CRC-слово.
  ID3/другие внешние headers, trailing bytes и смена конфигурации запрещены.

Framing, BSI и `dec3` основаны на
[ETSI TS 102 366 V1.4.1, приложения E/F](https://www.etsi.org/deliver/etsi_ts/102300_102399/102366/01.04.01_60/ts_102366v010401p.pdf).
Обычная поддержка завершённого E-AC-3 файла не подтверждает этот
progressive-профиль: требуется отдельная capability.

## Передача и время

Общий bounded parser строит виртуальный fMP4 с sample entry `ec-3`
и `dec3`. Каждый packet содержит исходный syncframe без перекодирования;
время считается по 1536 samples на 48 kHz. Чтения проходят через
ResourceSource/SHA; original не переписывается, полная временная копия
не создаётся. Это coded-sample timeline без удаления encoder delay.

FragmentIndex и Classic MP4 splitter допускают E-AC-3 только по явному
проверенному standalone-профилю. Qt использует source/libmpv-тракт,
Classic — штатную программную audio-цепочку.

Неполные initial header/frame дают `waiting_initialization` без
оставшегося reader. Следующий неполный frame ждёт update и не передаётся
decoder. Prefix без writer complete не становится EOF. При complete
требуются целые frames и согласованная duration. Ошибка CRC/конфигурации
при append закрывает источник для дальнейшего чтения. Reader сохраняется
при pause/seek/update и освобождается после последнего владельца.
Seek индексируется по frames; sample-exact UI seek не обещается.

## Проверки и границы

[Native conformance](../tests/progressive_eac3_conformance.py) использует
независимый FFmpeg encoder, сверяет packet hashes/clocks и PCM с decode
исходного E-AC-3. Проверяются все шесть битрейтов в mono/stereo,
информационные BSI, четыре growing profiles, partial frames,
malformed headers/CRC, смена конфигурации и release.
Результаты приложений и санитайзеров — в
[отчёте](ProgressiveEac3Validation-2026-11-02.json).

Другие rates/layouts/bsid, dependent substreams, дополнительные metadata,
live/sparse/segments, bitstream/физические выходы, физическая A/V-приёмка,
длительный stress и реальная интеграция Harvester остаются в
[общем чеклисте](ProgressiveCompletionChecklist.md).
