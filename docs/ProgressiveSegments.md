# Сегментированный timeline — SDK 1.31 (этап контракта)

SDK 1.31 уточняет и проверяет сегментную часть schema 2 для HLS/DASH VOD и
подготовки live. В этом этапе SDK 1.31 был реализован контракт публикации и
проверки снимков; decoder/demux подключён позднее в ограниченном профиле SDK 1.33. YouTubeHarvester
не изменяется.

Каждая сегментная дорожка передаёт `resourceId`, положительный `sequence`,
`start`, положительную `duration`, `timestampOffset`, `discontinuity`,
`configurationId` и при смене конфигурации `initializationResourceId`.
Плеер проверяет исходную шкалу `start` и presentation-шкалу
`start + timestampOffset`; обе не должны пересекаться и не должны переполнять
int64. Опубликованный сегмент нельзя менять, удалять или переставлять; новые
сегменты добавляются только в конец незавершённой дорожки.

Для `live=true` duration остаётся `null`, а `timeline.window.start/end` задаёт
видимое окно. Следующая revision может сдвигать конец вперёд и добавлять сегменты;
движение окна назад запрещено. Удаление вышедших сегментов и компактизация
ресурсов пока запрещены reader registry, поэтому этот этап не заявляет live playback.

Проверки: `progressive-segments` (configuration, offset, sequence, live-window),
`progressive-model` (полный schema 2) и прежний `progressive-media`. SDK 1.32 добавил staging/demux одного сегмента через `ResourceSource`.
В SDK 1.31/1.32 capability `schema2Playback.segments=false` сохранялся;
контролируемая смена декодера остаётся отдельным будущим этапом.

В SDK 1.33 добавлен отдельный [ограниченный playback-профиль](ProgressiveSegmentPlayback.md) с постоянной конфигурацией. Описанная выше модель допускает больше вариантов, чем этот первый backend.
