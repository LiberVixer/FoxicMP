# Schema 2: первый сквозной медиапрофиль

SDK 1.6, Linux Qt, Windows Qt и Classic.
SDK 1.18 отдельно расширяет матрицу [WebM VP9/Opus](ProgressiveWebM.md).
С SDK 1.14 добавлен [HEVC Main/Main10 профиль](ProgressiveHevc.md). Ниже сохранено
описание исходного H.264-профиля и его ограничений. [Модель v2](ProgressiveManifestV2.md)
теперь связана с реальным чтением декодера через [ResourceSource](ProgressiveResourceIo.md).
Это ограниченная часть [ТЗ](ProgressiveIntegrationPlan.md), а не вся codec/live-матрица.
YouTubeHarvester не изменялся. Опубликованные установщики 0.2.0 этого API не содержат.

## Возможности и границы

Проверять `capabilities`, а не номер приложения:

```json
{
  "playbackSchemaVersions": [1, 2],
  "readerLeaseProtocol": 1,
  "schema2Playback": {
    "mode": "separate-fmp4-h264-aac-lc",
    "trackCount": 2,
    "separateResourcesRequired": true,
    "durationRequired": true,
    "timebase": {"numerator": 1, "denominator": 10000000},
    "negativeTimestampOffset": false,
    "muxed": false,
    "segments": false,
    "live": false,
    "metadataApplied": false,
    "initializationLimitBytes": 1048576
  }
}
```

Ровно два ресурса, один required video и один required audio. Каждый track имеет
`source.resourceId`, `containerHint:"fmp4"`; codecHint — `h264`/`aac-lc` либо пустой.
Плеер проверяет **реальные** avc1/avcC, mp4a/esds/AAC-LC и self-contained dref,
а не доверяет подсказкам. Зашифрованные sample entries и внешние ссылки отклоняются.
Существующие MP4-демультиплексоры проверяют фрагменты и получают данные через
общий I/O-слой. Это не новый универсальный demux/remux.

VOD с известной длительностью, timebase 1/10000000, offset >= 0. Обе инициализации
должны находиться в подтверждённом head moov до moof/mdat; предел 1 МиБ на init.
Для старта нужны 10 общих секунд; применяется прежняя буферизация 1/5 секунд.
Можно передать несколько ranges, но этот медиапрофиль использует только непрерывный
подтверждённый префикс от нуля. Он не перескакивает через дыры.

Не поддержаны этим профилем: WebM/Matroska, HEVC/VP9/AV1/Opus, muxed, сегментные
HLS/DASH, внешний init, live/compaction, дополнительные аудио/субтитры, отрицательный
offset и неизвестная duration. Не применять главы/language/default/forced из v2:
`metadataApplied:false` явно отражает эту границу. Обычное воспроизведение готового
файла другого формата не доказывает его поддержку в progressive API.

## Снимок и SDK

Пример снимка для **реальных отдельно записываемых** файлов (счётчики здесь условны):

```json
{
  "schemaVersion": 2,
  "sessionId": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "generation": "1",
  "revision": "1",
  "writer": {"instanceId": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "state": "downloading"},
  "timeline": {"timebase": {"numerator": 1, "denominator": 10000000}, "live": false,
               "duration": "200000000", "timestampOffset": "0"},
  "resources": [
    {"id": "v", "name": "video.part", "length": null,
     "ranges": [{"offset": "0", "length": "100000"}], "state": "downloading"},
    {"id": "a", "name": "audio.part", "length": null,
     "ranges": [{"offset": "0", "length": "20000"}], "state": "downloading"}
  ],
  "tracks": [
    {"id": "video", "kind": "video", "required": true, "containerHint": "fmp4",
     "codecHint": "h264", "initializationReady": true, "state": "downloading", "source": {"resourceId": "v"}},
    {"id": "audio", "kind": "audio", "required": true, "containerHint": "fmp4",
     "codecHint": "aac-lc", "initializationReady": true, "state": "downloading", "source": {"resourceId": "a"}}
  ]
}
```

```python
from progressive_client import ProgressiveClient
from progressive_v2 import V2PreviewSession

client = ProgressiveClient(player_executable, profile_directory)  # Classic: профиль рядом с EXE
client.ensure_running()
preview = V2PreviewSession(client, session_directory, title=title, activate=False)
# После записи/flush байтов, только подтверждённые ranges:
preview.publish(snapshot_v2)
# Пока writer жив, без новых байтов, каждые <= 5 секунд:
preview.heartbeat()
# После дописывания обеих дорожек: известная length, полные ranges,
# state complete у writer/ресурсов/дорожек, следующая revision:
preview.publish(final_snapshot_v2)
receipt = preview.close(timeout=2)  # останавливает предпросмотр, не загрузчик
```

`generation`/`revision` — канонические строки uint64, не float/JSON number.
Поколение, sessionId и writer.instanceId неизменны. `start` идемпотентен по этой
инкарнации; старый readerId не может менять новый просмотр того же sessionId.
Status, включая естественное завершение и release, сохраняет точные строки и
writerInstanceId. `decoderIoBytes.video/audio` — строки со счётчиками байтов,
прочитанных для медиадвижка; это не доказательство физического вывода звука/кадра.

SDK начинает просмотр напрямую через start; пока init не готов —
`waiting-initialization`. Прямой probe допустим, читает init/index и
не оставляет читателей. С SDK 1.7 [подготовка через IPC вынесена из UI](ProgressivePreparation.md);
клиент ждёт прежний конечный ответ. SDK 1.8 переносит и [update/complete](ProgressivePublication.md). `unsupported_media` означает неподдерживаемый профиль;
`waiting_initialization` — ожидание init; ошибки файлов/индекса возвращаются отдельно.
Старый `validate` по-прежнему только проверяет метаданные: его `playbackSupported:false`
и исторический `reasonCode:backend-not-integrated` **не** служат проверкой доступных
медиапрофилей. Для этого есть capabilities и probe.

Все байты индекса и payload проходят ResourceSource: закрепление OS file ID,
диапазоны, last-read-window guard, truncate/cancel/EOF. Дополнительно cached moov
сравнивается при publish; его изменение переводит источник в постоянную ошибку.
Это не полная криптографическая integrity-проверка всей записи и не защита от
произвольного изменения ещё не прочитанных payload-байтов.

Каждый ресурс имеет внутреннюю OS lease, просмотр — групповую квитанцию.
Внутренние leases подтверждаются после закрытия источника; групповая
`released:true` записывается после освобождения мостом **всех** файлов. При crash
неподтверждённые квитанции остаются для recovery. После потерянного start-ответа
SDK не объявляет файлы свободными: искать всех readers(sessionId, generation).
Даже подтверждённый один reader не разрешает удалить файл при других читателях.

[Результаты проверки трёх редакций](ProgressiveMediaValidation-2026-10-26.md).
Следующие этапы: registry I/O и финальные close-пути, другие codec/container-профили,
общий demux/remux, полная integrity-модель, timeline/metadata/live и реальный Harvester.
