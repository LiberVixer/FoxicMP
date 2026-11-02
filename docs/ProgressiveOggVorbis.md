# Progressive Ogg Vorbis

SDK 1.72, приложение 0.2.0. Отдельный профиль `standalone-ogg-vorbis-v1`
для Classic, Windows Qt и Linux Qt. Harvester не изменяется.

## Согласование

Проверить capability фактического EXE:
`schema2Playback.standalone.oggVorbis`, `version=1`,
`mode=standalone-ogg-vorbis-v1`, `profiles=["standalone-ogg-vorbis"]`.
Одна обязательная audio-дорожка `containerHint=ogg-vorbis`,
`codecHint=vorbis`, один resource, непрерывный подтверждённый prefix,
SHA-256, известная конечная duration VOD и `timestampOffset=0`.
Начальная длина resource может быть неизвестна.
[Пример](../tests/fixtures/progressive-v2/ogg-vorbis.json).

Прежние Ogg Opus и WebM Opus/Vorbis сохраняют свои профили. Наличие
поддержки Vorbis в WebM само не включает Ogg Vorbis.

## Ограниченный профиль

- Ogg v0, один serial, последовательные page numbers от нуля. Непустые
  страницы; проверяются CRC, lacing, BOS/EOS и continued packets.
- Vorbis I, mono/stereo 48 kHz, floor 1, bounded codebooks/setup по
  существующему валидатору Vorbis. Identification header занимает
  первую BOS page; comment/setup headers завершаются до audio pages.
  Пакет setup завершает свою страницу.
- Не более 1 MiB headers/начала файла и на packet, 250000 pages/audio
  packets и 16 MiB за update. Большие comments проверяются по lengths,
  но не импортируются автоматически в UI; используется metadata writer.
- Granule origin 0 без начальной обрезки/положительного сдвига. На обычных
  audio pages granule равен числу полностью обработанных сэмплов.
  EOS может обрезать часть последнего packet; complete требует целого
  EOS без незавершённого packet или хвостовых данных.
- Chaining/multiplexing, потеря страниц, смена serial/configuration,
  другие rates/layouts, floor 0, live и sparse ranges не входят в профиль.

Основание: [Vorbis I, Ogg encapsulation](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html)
и [Ogg framing](https://xiph.org/ogg/doc/framing.html).

## Время и цифровой звук

Общий parser создаёт виртуальный single-track WebM без перекодирования.
Identification/comment/setup складываются в Xiph CodecPrivate, payload
audio packets ссылается на исходные physical ranges. Continued packet
имеет несколько spans; все физические чтения проходят ResourceSource/SHA.

Первый audio packet только подготавливает окно decoder и не прибавляет
сэмплов к granule. Для следующих packet число сэмплов вычисляется как
`(previousBlock + currentBlock) / 4`; проверяются mode/window transitions.
Время первого подготовительного packet — 0, остальных — на исходной
звуковой шкале. TimestampScale 100 ns ограничивает округление packet clocks
одним tick; конец передаётся через DiscardPadding, без двойной обрезки.
Начальный sample discard в этом профиле отсутствует.

Для seek Classic сохраняет существующий Vorbis decoder preroll 200 ms,
Qt использует обычный WebM/libmpv-тракт. Точность UI seek в один sample
не заявляется. При pause/seek/update reader остаётся тем же.
Неполная страница/packet ждёт update; до готовой initialization и двух
audio packets — `waiting_initialization` без оставшегося reader.
Даже полный EOS prefix ожидает writer complete, а не даёт ранний EOF.

## Проверки

[Native conformance](../tests/progressive_ogg_vorbis_conformance.py)
сравнивает hashes packets, clocks и PCM с независимым decode исходного
Ogg; включает mono/stereo, разные encoder quality, шумовой сигнал с
продолжениями packet, большие comments и односэмпловую конечную обрезку.
Проверяются malformed headers/CRC/granules, partial initialization,
повреждение опубликованных байтов, ошибочный append и освобождение readers.
Итоги ОС/редакций и exit codes — в
[отчёте](ProgressiveOggVorbisValidation-2026-11-02.json).

Это цифровая и software audio проверка. Физическая A/V-задержка/drift,
длительный stress, остальные audio profiles и реальная интеграция
Harvester остаются в [общем чеклисте](ProgressiveCompletionChecklist.md).
