# Один растущий WebM с VP8 и Opus

SDK 1.45 расширяет [shared WebM](ProgressiveMuxedWebm.md) профилем
`growing-muxed-webm-vp8-opus` для Classic и обеих Qt-редакций. Видео и звук
читаются из одного SHA-256 resource через две виртуальные WebM-дорожки.
Blocks/payload сохраняются без перекодирования и промежуточных media.

## Согласование

Точная capability:
`schema2Playback.muxedPlayback.webm.vp8.mode = growing-muxed-webm-vp8-opus`.
Старая VP9 capability, shared AV1, separate/segmented VP8 и общая muxed
галочка этого режима не разрешают. SDK проверяет mode до start/update,
изменения last snapshot и uncertain.

У обеих required scalar-дорожек — `containerHint: webm-muxed` и один
`source.resourceId`. Video обязательно объявляет `codecHint: vp8`; audio —
Opus, hint можно явно указать как `codecHint: opus`. Без video codecHint
сохраняется прежний default VP9, поэтому VP8 actual codec будет отвергнут.
Пример — `progressive-v2-muxed-webm-vp8-vod.json`. Начальные пустые ranges и
readiness=false заменяются реальными подтверждёнными bytes/SHA/readiness;
IDs/duration должны принадлежать своей загрузке.

Остаются прежние условия: известный VOD, timebase 1/10000000, нулевое
смещение времени, ровно один ресурс и две required-дорожки, непрерывный
подтверждённый префикс от 0, одинаковые state/readiness. Без внешнего init,
catalog, multi-audio и live. Opus mono/stereo 48 kHz, mapping family 0,
согласованные CodecDelay/pre-skip и SeekPreRoll=80 ms. Length может быть
неизвестна до complete; заголовок Segment после публикации не переписывается.

## VP8 и реальные данные

Используются действующие WebMInitialization/WebMIndex. Codec ID — `V_VP8`,
8-bit 4:2:0, CodecPrivate отсутствует. Плеер не выводит codec из имени файла:
actual TrackEntry обязан совпасть с заявленным профилем. Новый mode не меняет
прежние VP9/AV1 mode или default без codecHint.

Начальный packet должен быть displayed keyframe с корректной сигнатурой,
допустимой версией 0–3, ненулевой первой секцией внутри packet, размерами
по TrackEntry и без масштабирования. Фактические тестовые записи используют
version 0; версии 1–3 этим этапом отдельно не декодировались. Проверка header
не является полной entropy-проверкой каждого VP8 frame. Декодирование
проверяется отдельно движками/FFmpeg.

Основания разметки: [VP8 Data Format, RFC 6386](https://www.rfc-editor.org/rfc/rfc6386.html)
и [Matroska V_VP8 mapping](https://www.matroska.org/technical/codec_specs.html#v_vp8).
Alpha/BlockAdditions, interlace и encryption этим профилем не добавляются.

Сохраняются целые finite Clusters, SimpleBlock/BlockGroup без lacing,
исходные timestamps и DiscardPadding. Info/выбранная TrackEntry и Cluster
headers проецируются в памяти; реальные blocks читаются из общего файла.
Неполный head/Cluster ждёт update; explicit complete с оборванным элементом
становится ошибкой. Ошибка media закрывает обе проекции. Ошибка metadata до
admission сохраняет старый сеанс. CodecHint при update менять нельзя.

Ограничения прежние: read 64 KiB, init/metadata по 1 MiB, Cluster и прирост
prefix по 16 MiB, 8192 Clusters, 32768 top-level elements, 65536 children
на Cluster, 2 миллиона samples на дорожку. Unknown-size Segment разрешён,
unknown-size Cluster нет. Дробный DefaultDuration округляется отдельно
для sample end до 100 ns; manifest duration envelope допускает до 1 ms.

## Проверка и границы

[Фактические результаты](ProgressiveMuxedWebmVp8Validation-2026-10-31.md)
включают native packet hashes, PTS/DTS и decoded video/PCM; stereo/mono,
дробную частоту, known/unknown Segment/resource length, partial Clusters,
immutable prefix, shared lease, header faults и сохранение старых profiles.
Qt/Classic проверяют playback до complete, buffer, pause/update, backward и
forward seek, natural EOF/release. Harvester не меняется.

Этот этап не добавляет Vorbis в shared WebM, дополнительные дорожки,
смену codec configuration, live или произвольный Matroska. Реальные сетевые
источники, многочасовые нагрузки, предельная память и физическая A/V sync
требуют отдельной приёмки. SDK ZIP — клиент, примеры и документация без EXE;
для нового профиля нужен плеер, собранный из соответствующего тега.
