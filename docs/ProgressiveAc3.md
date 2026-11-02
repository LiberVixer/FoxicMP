# Progressive AC-3

SDK 1.73, приложение 0.2.0. Отдельный профиль `standalone-ac3-v1`
для Classic, Windows Qt и Linux Qt. Harvester не изменяется.

## Согласование

Проверить capability фактического EXE:
`schema2Playback.standalone.ac3`, `version=1`,
`mode=standalone-ac3-v1`, `profiles=["standalone-ac3"]`.
Одна обязательная audio-дорожка `containerHint=ac3`, `codecHint=ac3`,
один resource, непрерывный подтверждённый prefix с SHA-256,
известная конечная duration VOD и `timestampOffset=0`.
Начальная длина resource может быть неизвестна.
[Пример](../tests/fixtures/progressive-v2/ac3.json).

Это AC-3 syncframes. E-AC-3 не включается этой capability и остаётся
отдельным этапом. Поддержка завершённого файла обычным decoder не
подтверждает progressive-профиль.

## Проверяемый профиль

- Big-endian syncword `0x0b77`, bitstream ID 8, 48 kHz, mono или stereo
  (`acmod=1/2`), без LFE. Dual mono, многоканальные layouts, другие rates,
  bitstream IDs и byte-swapped framing вне профиля.
- Шесть блоков/1536 samples на frame. Стандартные битрейты от 32 до
  640 kbit/s; bitrate code, channel mode и bsmod неизменны в сессии.
  Reserved surround mode отклоняется.
- Не более 2560 bytes/frame, 250000 frames и 16 MiB за update.
  ID3/другие внешние headers и trailing bytes не допускаются.
- Для каждого полного frame проверяются обе CRC полиномом `0x8005`:
  первая область до границы 5/8 frame и оставшаяся область. Syncword
  не входит в CRC. Граница рассчитывается в 16-bit words по стандарту.

Framing/CRC основаны на
[ETSI TS 102 366](https://www.etsi.org/deliver/etsi_TS/102300_102399/102366/01.02.01_60/ts_102366v010201p.pdf),
пункты 4 и 6.10.1. Выходная конфигурация `dac3` соответствует исходному
header; payload frames остаётся неизменным.

## Передача и время

Общий bounded parser строит виртуальный fMP4 с sample entry `ac-3` и
`dac3`. Каждый packet содержит целый исходный syncframe; время считается
по 1536 samples на 48 kHz. Все физические чтения, включая header/CRC,
проходят ResourceSource/SHA; original не переписывается, временная копия
всего файла не создаётся. Encoder delay не удаляется: это coded-sample
timeline, отдельного gapless trim здесь нет.

FragmentIndex и Classic MP4 splitter допускают AC-3 только по явному
проверенному standalone-профилю. Qt использует общий source/libmpv-тракт,
Classic — штатную audio-цепочку. Bitstream/HDMI/S/PDIF и физические выходы
этой функцией не проверяются.

Неполные initial header/frame дают `waiting_initialization` без
оставшегося reader. Последующий неполный frame ждёт update; его bytes не
передаются decoder. Полный prefix без writer complete не становится EOF.
При complete требуются только целые frames и согласованная duration.
CRC/configuration error при append закрывает источник для дальнейшего
чтения. Reader сохраняется при pause/seek/update, затем освобождается
после последнего владельца. Seek индексируется по frames; sample-exact
UI seek не обещается.

## Проверки и границы

[Native conformance](../tests/progressive_ac3_conformance.py) использует
независимый FFmpeg encoder, сравнивает packet hashes/clocks и PCM с
decode исходного AC-3. Проверяются все объявленные битрейты, четыре
growing mono/stereo profiles, partial frames, malformed headers/CRC,
смена конфигурации и release. Итоговые результаты трёх редакций и
санитайзеров — в [отчёте](ProgressiveAc3Validation-2026-11-02.json).

E-AC-3, дополнительные rates/layouts, live/sparse/segments, физическая
A/V-приёмка, длительный stress и реальная интеграция Harvester остаются
в [общем чеклисте](ProgressiveCompletionChecklist.md).
