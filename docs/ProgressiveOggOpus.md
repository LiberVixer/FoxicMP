# Progressive Ogg Opus

SDK 1.71, приложение 0.2.0. Отдельный профиль `standalone-ogg-opus-v1`
для Classic, Windows Qt и Linux Qt. Harvester не изменяется.

## Согласование

Проверить capability фактического EXE:
`schema2Playback.standalone.oggOpus`, `version=1`,
`mode=standalone-ogg-opus-v1`, `profiles=["standalone-ogg-opus"]`.
Нужна одна обязательная audio-дорожка `containerHint=ogg-opus`,
`codecHint=opus`, один resource с SHA-256 подтверждённых ranges,
непрерывный prefix, известная конечная duration VOD и `timestampOffset=0`.
Начальная длина resource может быть неизвестна.
[Пример](../tests/fixtures/progressive-v2/ogg-opus.json).

Vorbis в Ogg этим профилем не поддерживается. Прежние Opus/Vorbis WebM
и Opus fMP4 сохраняют свои capabilities и правила.

## Проверяемый профиль

- Ogg version 0, один serial, последовательные page numbers от нуля,
  первая BOS page содержит ровно OpusHead. Пустые страницы в этот профиль не входят. Проверяется CRC всей страницы
  с полиномом `0x04c11db7`, включая lacing/header/body.
- OpusHead version 1, mono/stereo, mapping family 0, decode clock 48 kHz.
  Pre-skip 0–3840 samples, кратный трём для точного целого CodecDelay в ns.
  Output gain и исходный OpusHead сохраняются.
- OpusTags и continued packets проверяются с bounded lengths. Заголовки
  завершаются на страницах до audio, в пределах 1 MiB начала файла.
  Не более 1 MiB на packet, 250000 audio packets/pages и 16 MiB за update.
- Начальный granule origin 0. На обычных audio pages granule соответствует
  сумме длительностей завершённых пакетов. EOS может обрезать только
  последний packet, не весь последний packet; granule не регрессирует.
- Chaining, multiplexing, смена serial/configuration, packet loss,
  другие mapping families, live и sparse ranges не входят в этот профиль.

Правила framing основаны на [RFC 3533](https://www.rfc-editor.org/rfc/rfc3533),
pre-skip/granules/end trim — на [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845).

## Передача в плеер

Общий parser создаёт виртуальный single-track WebM, не перекодируя Opus.
Завершённые packets ссылаются на исходные физические ranges Ogg; пакет,
разделённый между страницами, имеет несколько spans. Сохраняются порядок,
payload, pre-skip, gain и EOS trim через DiscardPadding. TimestampScale
500000 ns точно представляет разрешённые длительности Opus packets;
seek preroll — 80 ms. Цифровая PCM-проверка не обещает sample-exact UI seek.

Неполная page/packet ждёт следующей публикации. До первого полного audio
packet возвращается `waiting_initialization` без оставшегося reader.
EOS page сама не заменяет writer complete: полный prefix остаётся
ожидающим источником до завершения writer. При complete требуются EOS,
целая последняя page и отсутствие незавершённого packet/лишних данных.

Все физические чтения проходят ResourceSource/SHA. Повреждение CRC при
update закрывает источник для последующего чтения. Original не изменяется;
весь файл во временное хранилище не копируется. Reader сохраняется при
pause/seek/update и освобождается после последнего владельца.
OpusTags проверяются как bounded header, но их комментарии автоматически
не импортируются в UI: для заголовка/глав используется metadata writer.

## Проверка и границы

[Native conformance](../tests/progressive_ogg_opus_conformance.py) проверяет
payload hashes, clocks, совпадение PCM с независимым decode исходного Ogg,
continued audio/tags, gain и односэмпловый EOS trim. Negative cases включают
CRC, sequence/serial, granules, headers и partial initialization.
[Отчёт](ProgressiveOggOpusValidation-2026-11-02.json) содержит отдельные
результаты трёх редакций, Windows/Linux SDK и санитайзеров.

Не закрыты Ogg Vorbis, остальные rates/layouts/containers и
[общая приёмка ТЗ](ProgressiveCompletionChecklist.md). Проверка цифрового
PCM и software audio не подтверждает физическую A/V-задержку или drift.
