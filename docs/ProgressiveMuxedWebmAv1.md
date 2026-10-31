# Один растущий WebM с AV1 и Opus

SDK 1.44 расширяет [shared WebM](ProgressiveMuxedWebm.md) профилем
`growing-muxed-webm-av1-opus` для Classic и обеих Qt-редакций. Сохраняются
один физический resource, две virtual track views, SHA-256, публикация
целых Clusters и чтение исходных blocks без перекодирования.

## Согласование

Нужна точная capability:
`schema2Playback.muxedPlayback.webm.av1.mode = growing-muxed-webm-av1-opus`.
Старые `webm.mode = growing-muxed-webm-vp9-opus`, `muxed: true`, раздельный
AV1 и segmented AV1 этого разрешения не дают. Python `V2PreviewSession`
отвергает неподдержанный AV1 до изменения last snapshot, вызова start/update
и установки uncertain.

Manifest использует прежний `containerHint: webm-muxed`. Обязательно
`codecHint: av1` у video. Звук — Opus; его hint можно явно указать как
`codecHint: opus`. `av1.codecHintRequired` относится к video. Без video codecHint
остаётся прежний профиль VP9; AV1 не выбирается по расширению/догадке.
Пример — `progressive-v2-muxed-webm-av1-vod.json` в SDK. Начальные пустые
ranges и readiness=false надо заменить подтверждёнными bytes/digest и
реальной готовностью; IDs/duration принадлежат загрузчику.

Все прежние условия shared WebM сохраняются: известный VOD, timebase
1/10000000, timestampOffset=0, ровно две required scalar-дорожки одного
ресурса, непрерывный подтверждённый префикс, одинаковые state/readiness,
без external init, catalog и live. Opus mono/stereo 48 kHz, mapping 0,
CodecDelay/pre-skip и SeekPreRoll=80 ms. Режим VP9 и его capability сохранены.

## AV1 и проверка данных

Codec ID — `V_AV1`. CodecPrivate содержит ограниченный
AV1CodecConfigurationRecord (av1C): Main profile, main tier, 8/10 bit,
4:2:0, без presentation delay. Нужен исходный Sequence Header OBU; первый
packet должен содержать совпадающий sequence и ключевой frame. Используется
действующий `WebMInitialization`/`WebMIndex`, без второго AV1 parser.

Основания разметки: [Matroska codec mappings](https://www.matroska.org/technical/codec_specs.html)
и [AV1CodecConfigurationRecord](https://aomediacodec.github.io/av1-isobmff/).
Проверка framing/configuration не заменяет entropy decoding. Декодирование
и фактические параметры проверяются отдельно движками/FFmpeg.

Заявленный video codec обязан совпадать с actual codec в TrackEntry;
VP9-hint на AV1 и AV1-hint на VP9 отвергаются. Прежний codecHint нельзя
менять при update: нужна новая сессия. Ошибка нового media закрывает обе
проекции; metadata rejection до admission оставляет старый сеанс читаемым.

Пределы прежние: read 64 KiB, init/metadata по 1 MiB, прирост префикса и
Cluster по 16 MiB, 8192 Clusters, 32768 top-level elements, 65536 children
на Cluster, 2 миллиона samples на дорожку. Unknown-size Segment допустим,
unknown-size Cluster нет. Дробный DefaultDuration сохраняется с отдельным
округлением sample end до 100 ns; duration envelope допускает до 1 ms.

## Проверка и границы

Нативная проверка сравнивает encoded hashes, PTS/DTS, decoded video и PCM;
8/10-bit, дробную частоту, known/unknown Segment/resource lengths, partial
Clusters, immutable prefix, SHA и leases. Qt/Classic проверяют буферизацию,
pause/update, seek, complete и release. Для 10-bit native decoded video
используется yuv420p10le. [Фактический отчёт](ProgressiveMuxedWebmAv1Validation-2026-10-31.md).

Промежуточный MP4 создаётся только при генерации тестовых AV1-данных:
FFmpeg требует AV1 extradata до записи WebM header. Затем исходные packets
копируются в тестовый muxed WebM. Сам плеер промежуточные media не создаёт.

Этот этап не добавляет VP8/Vorbis в shared WebM, multi-audio, смену codec
configuration, live, реальные сетевые источники или многочасовую физическую
A/V-приёмку. YouTubeHarvester и установленные профили не меняются. SDK ZIP
содержит клиент, примеры и документацию; новый Windows EXE надо собрать
из соответствующего исходного тега.
