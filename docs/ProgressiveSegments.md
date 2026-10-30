# Сегментированный timeline — SDK 1.31 (этап контракта)

SDK 1.31 уточняет и проверяет сегментную часть schema 2 для HLS/DASH VOD и
подготовки live. Это пока контракт публикации и проверки снимков: decoder,
demux/remux и подключение к воспроизведению ещё не включены. YouTubeHarvester
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
`progressive-model` (полный schema 2) и прежний `progressive-media`. Следующий
этап — staging/demux одного сегмента через `ResourceSource`, затем объединение
с контролируемой сменой декодера. Capability `schema2Playback.segments=false`
сохраняется до готовности этого playback-слоя.
