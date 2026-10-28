# Progressive VP8/Vorbis — SDK 1.21

Расширение schema 2 для Linux Qt, Windows Qt и Windows Classic. Harvester не
изменяется. Прежние SDK/CLI/API и профили остаются совместимыми; проверять
конкретный профиль в `schema2Playback.profiles` текущего EXE.

| Видео | Аудио | Профили |
|---|---|---|
| WebM VP8 | WebM Opus или Vorbis | `separate-webm-vp8-{opus,vorbis}` |
| WebM VP9/AV1 8/10-bit | WebM Vorbis | `separate-webm-{vp9,av1}-vorbis` |
| fMP4 H.264/HEVC/AV1/VP9 | WebM Vorbis | `separate-fmp4-webm-{h264,hevc,av1,vp9}-vorbis` |
| WebM VP8 | fMP4 AAC-LC или Opus | `separate-webm-fmp4-vp8-{aac-lc,opus}` |

Каждая дорожка имеет собственный `containerHint` (`webm` или `fmp4`), codecHint
(`vp8`/`vorbis` либо прежний кодек) и отдельный ресурс. Пустой codecHint позволяет
обнаружение по заголовку; непустой проверяется по содержимому. Имя файла не
выбирает формат. `mediaMode`, `videoContainer`/`audioContainer`, чтение через
закреплённый ResourceSource и границы подтверждённого префикса сохраняются.

## Границы новых кодеков

VP8: 8-bit, до 16383×16383, известный DefaultDuration. Первый пакет обязан быть
отображаемым ключевым кадром, с корректным start code, version 0–3, ограниченной
первой partition, совпадающими размерами и без масштабирования кадра. CodecPrivate
для VP8 не принимается. Это проверка начального заголовка, не полная проверка
кодированного видеопотока; payload декодирует штатный backend.

Vorbis I: mono/stereo, 48 kHz, floor 1, block sizes 64–8192, до 65536 entries
и 16 dimensions на codebook; остальные варианты этим профилем не заявляются.
Проверяются Xiph lengths трёх заголовков, сигнатуры, version, channels/rate,
comment framing, bounded codebooks, references floor/residue/mapping, mode
window/transform, setup framing и padding. Заголовки ограничены общим 1 MiB.
Это структурная проверка конфигурации: не выполняет синтез аудио и не доказывает
целостность всех residue/codebook payload.

Индекс вычисляет длительность пакета Vorbis из текущего/предыдущего block size
и mode/window flags, проверяет тип аудиопакета и границы чтения. Метки начала
берутся из WebM; timestamp scale может квантовать их. Classic при перемотке
читает не менее 200 мс предыдущих аудиопакетов для восстановления overlap;
Qt использует seek штатного demuxer. Для Vorbis CodecDelay/SeekPreRoll должны
отсутствовать либо быть нулевыми; это отличается от Opus. CodecDelay/PreSkip
и 80 мс SeekPreRoll Opus сохраняются.

Остальные [ограничения WebM](ProgressiveWebM.md) и
[смешанных контейнеров](ProgressiveMixedContainers.md) остаются: VOD, известная
длительность, две отдельные required-дорожки, известные размеры Cluster,
непрерывный подтверждённый префикс, без lacing/encryption, muxed/remux и live.
Vorbis в Ogg/fMP4, другие sample rates/floor types, дополнительные аудио,
субтитры/главы/metadata и произвольный MKV не добавлены.

[Результаты и хеши сборок](ProgressiveVp8VorbisValidation-2026-10-28.md).
Опубликованные установщики 0.2.0 этим SDK не обновляются. Реальный Harvester,
физическая A/V-синхронизация и длительный дрейф требуют отдельных испытаний.

Основания форматов: [VP8 RFC 6386](https://www.rfc-editor.org/rfc/rfc6386),
[Vorbis I](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html),
[Matroska codec mapping](https://www.matroska.org/technical/codec_specs.html).
