# SDK 1.38: сегменты WebM с Opus

Общий bridge трёх редакций читает отдельные WebM video/audio дорожки как
виртуальный append-only поток: внешний init, затем целые медиафайлы из одного
или нескольких Cluster. Ни Harvester, ни настройки установленного Classic/Qt
этот этап не меняет.

## Negotiation и объявление

Проверяйте `schema2Playback.segmentedPlayback.webm`, а не capability старого
scalar WebM или диагностического `segmentProbe`:

- `mode: segmented-webm-vp9-opus`;
- `profiles`: `segmented-webm-vp8-opus`, `segmented-webm-vp9-opus`,
  `segmented-webm-av1-opus`;
- видео VP8, VP9 Profile 0/2 (8/10 bit), AV1 Main (8/10 bit);
- аудио Opus 48 kHz, mono/stereo, mapping family 0, согласованные CodecDelay
  и pre-skip, SeekPreRoll 80 ms.

В schema 2 обе required-дорожки имеют `containerHint: webm`, внешний
`initializationResourceId` и `source.segments`. Сеанс — VOD с известной
длительностью, timebase 1/10000000 и нулевым timestampOffset. Media mode в status
— `segmented-webm-<video>-opus`. Container и codecHint проверяются по реальным
байтам init/пакетов; пропущенный hint не отключает проверку.

## Файловый профиль

Init (до 1 MiB) содержит EBML, Segment с неизвестным размером, Info перед Tracks;
TimestampScale строго 1000000 ns. Init не содержит Cluster или Cues. Поле Segment
с неизвестным размером позволяет добавлять подтверждённые Cluster без изменения
уже опубликованного заголовка.

Медиафайл (до 16 MiB, до 65536 пакетов) содержит только целые Cluster известного
размера, без нового EBML/Segment/init. Первый пакет каждого видеосегмента —
проверенный ключевой кадр. Lacing, шифрование, смена codec/init, discontinuity,
rebasing и live не включены. Начальная реализация не включает segmented Vorbis;
прежний scalar WebM/Vorbis остаётся отдельным рабочим профилем.

Sequence последовательны. Абсолютный start/duration каждого сегмента совпадает
с границами индексированных пакетов с точностью 1 microsecond. При сравнении
соседних сегментов допускается неотрицательный промежуток до 1 ms — квантование
WebM clock, включая Opus CodecDelay, не означает discontinuity. Перекрытия
запрещены. Для fMP4 прежний допуск 1 microsecond не меняется.

Видим только полностью подтверждённый последовательный префикс. Дырка или
недокачанный хвост возвращают ожидание, а не EOF. Финальный EOF требует явного
complete. Размеры/типы Cluster и packet envelope проверяются до выдачи движку.

Это ограниченный basic-media-segment профиль, соответствующий структуре
[WebM DASH](https://wiki.webmproject.org/adaptive-streaming/webm-dash-specification)
и [WebM byte stream](https://www.w3.org/TR/mse-byte-stream-format-webm/),
а не универсальный загрузчик DASH/HLS.

## Каталог и SDK

Для постраничного варианта нужна отдельная capability
`segmentedPlayback.webm.catalog.mode: paged-webm-v1`. Wire format тот же, что у
[каталога fMP4](ProgressiveCatalogPlayback.md): SHA-256 страниц и всех media,
128 страниц по 64 KiB, metadata до 8192 ресурсов/4096 сегментов на дорожку,
не более восьми открытых media. Все опубликованные файлы остаются до release
главного reader ID; renderer/cache не даёт разрешения удалять отдельные страницы.

`V2PreviewSession` проверяет WebM/Opus capability и WebM catalog отдельно от fMP4,
до изменения локального состояния или IPC start. Для admission используйте
`probe` с sessionDirectory; `waiting-index` означает ожидание страницы и допускает
повтор публикации той же ревизии. Диагностический `resourceProbe` по-прежнему
индексирует прежний ограниченный fMP4-профиль и не объявляется WebM playback API.

Границы: muxed A/V, TS, segmented Vorbis, live/config changes, многочасовая
приёмка и реальные источники остаются отдельными этапами. Проверки и точные
коды завершения: [отчёт](ProgressiveWebmSegmentsValidation-2026-10-30.md).
