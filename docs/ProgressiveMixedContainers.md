# Progressive mixed MP4/WebM — SDK 1.20

Schema 2 поддерживает разные контейнеры двух отдельных дорожек в Linux Qt,
Windows Qt и Windows Classic. YouTubeHarvester не изменяется; Python API и
прежние профили сохраняются. Проверять `schema2Playback.mixedContainers` и
конкретный профиль в `schema2Playback.profiles` текущего EXE.

| Видео | Аудио | Профили |
|---|---|---|
| fMP4: H.264, HEVC Main/Main10, AV1 Main 8/10-bit, VP9 Profile 0/2 | WebM: Opus mono/stereo | `separate-fmp4-webm-{h264,hevc,av1,vp9}-opus` |
| WebM: VP9 Profile 0/2 или AV1 Main 8/10-bit | fMP4: AAC-LC или Opus mono/stereo | `separate-webm-fmp4-{vp9,av1}-{aac-lc,opus}` |

`containerHint` задаётся каждой дорожке отдельно: `fmp4` или `webm`.
Это не разрешение читать произвольный контейнер: плеер проверяет фактические
заголовки каждой дорожки, codecHint и её подтверждённый непрерывный префикс.
Пустой codecHint допускает обнаружение кодека по заголовку. Контейнер нельзя
менять обновлением текущего поколения. Расширение имени файла значения не имеет.

Например, для fMP4 H.264 + WebM Opus используются:

```json
[
  {"id":"v","kind":"video","required":true,"containerHint":"fmp4","codecHint":"h264","initializationReady":true,"state":"downloading","source":{"resourceId":"video"}},
  {"id":"a","kind":"audio","required":true,"containerHint":"webm","codecHint":"opus","initializationReady":true,"state":"downloading","source":{"resourceId":"audio"}}
]
```

Это только поле `tracks`; полный manifest также содержит два ресурса, writer,
timeline, идентификатор, generation и revision. Действующие
[контракт схемы](ProgressiveManifestV2.md) и [V2PreviewSession](ProgressiveSdk.md)
сохраняются. `status` дополнен полями `videoContainer` и `audioContainer`;
`mediaMode` содержит оба контейнера при смешанной паре, прежнее имя при одинаковых.

## Backend и ограничения

Инициализация, индекс, byte reads и EOF выбираются отдельно для каждой дорожки.
Classic создаёт MP4Splitter или MatroskaSplitter соответственно её контейнеру;
незавершённый WebM сохраняет специальное поведение Length. Qt использует lavf
с определением формата по проверенным заголовкам для смешанной пары: общий
принудительный `mov`/`matroska` ошибочно применяется и к внешнему аудио.
Для одинаковых контейнеров принудительный формат сохраняется.

Ограничения [fMP4](ProgressiveMediaV2.md), [HEVC](ProgressiveHevc.md),
[AV1](ProgressiveAv1.md), [VP9](ProgressiveVp9.md), [Opus](ProgressiveOpus.md),
[WebM VP9](ProgressiveWebM.md) и [WebM AV1](ProgressiveWebMAv1.md) остаются:
VOD с известной длительностью, две required-дорожки, отдельный ресурс для каждой,
100 нс timebase, неотрицательный offset, ограниченная инициализация.
WebM требует известные размеры кластеров, DefaultDuration видео, без lacing.
Для Opus fMP4 нужны согласованные pre-skip/edit, для WebM — CodecDelay/PreSkip
и SeekPreRoll. Смешивание не выполняет remux и не расширяет набор самих кодеков.

VP8/Vorbis, произвольный MKV, muxed/remux, unknown-size Cluster, несколько аудио,
субтитры/главы/metadata, live, реальные источники Harvester и физическая A/V
синхронизация остаются за рамками этапа. Установщики 0.2.0 SDK не обновляет.

[Результаты проверки трёх редакций](ProgressiveMixedValidation-2026-10-28.md).
