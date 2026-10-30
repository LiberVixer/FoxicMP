# Модель progressive-manifest-v2: черновой контракт

Этап 3 [плана интеграции](ProgressiveIntegrationPlan.md). Реализованы общий
парсер, проверки обновлений/диапазонов и модель жизненного цикла читателей.
Это **проверка метаданных**, а не воспроизведение WebM, AV1, live или нескольких
аудиодорожек. SDK 1.5 отдельно добавляет [I/O с leases ресурсов](ProgressiveResourceIo.md);
SDK 1.6 добавляет [сквозной fMP4-профиль H.264/AAC-LC](ProgressiveMediaV2.md).
Универсальный demux/remux и compaction live-ресурсов ещё впереди.
YouTubeHarvester не изменяется. Схема v1 сохраняет прежние поля и смысл.

SDK 1.30 добавляет необязательные [SHA-256 блоки ресурсов](ProgressiveIntegrity.md). Старые снимки совместимы; digest покрывают все подтверждённые диапазоны защищённого ресурса. SDK 1.31 уточняет [сегментный timeline](ProgressiveSegments.md): проверяются исходная и presentation шкалы, конфигурации и движение live-окна. Playback capability пока остаётся `segments=false`.

## Использование сейчас

Все три редакции поддерживают `validate` в прежней JSON-оболочке API версии 1:

```python
from progressive_client import ProgressiveClient
from progressive_schema import unsigned64, signed64

client = ProgressiveClient(player_path, profile_path)
reply = client.validate_session(snapshot_v2, previous=previous_snapshot_or_none)
# reply: ok=true, validated=true, mode=validation-only,
# playbackSupported=false, reasonCode=backend-not-integrated
```

```text
python3 progressive_client.py --player PLAYER --profile PROFILE validate --snapshot session-v2.json
python3 progressive_client.py --player PLAYER --profile PROFILE validate --snapshot next.json --previous-snapshot old.json
```

`validate` не читает ресурсы, не меняет окно/профиль и не создаёт reader.
Снимки не сохраняются в API: при проверке перехода клиент передаёт `previous`.
Размер **всего запроса**, включая оба снимка, остаётся не более 65536 байтов.
Возможности разделены: `playbackSchemaVersions:[1,2]`, `validationSchemaVersions:[2]`.
`start`/`probe` schema 2 с SDK 1.6 поддерживают только явно объявленный
[schema2Playback-профиль](ProgressiveMediaV2.md); другие профили возвращают
unsupported_media. `validate` сохраняет прежний ответ metadata-only, включая
исторический reasonCode backend-not-integrated; он не выбирает медиапрофиль.
Полная схема 2 остаётся черновой, расширенная матрица ТЗ ещё не выполнена.

## Корневой объект и точность

Обязательные поля:

| Поле | Представление / смысл |
|---|---|
| `schemaVersion` | JSON integer `2` |
| `sessionId` | 32 lowercase hex, случайный ID |
| `generation`, `revision` | Положительные десятичные строки uint64 |
| `writer` | `instanceId` (32 hex) и `state` |
| `timeline` | `timebase`, `live`, `duration`, `timestampOffset`; необязательное `window` |
| `resources` | 1–64 локальных ресурса |
| `tracks` | 1–32 дорожки, минимум одна audio/video |
| `metadata` | Необязательные главы/название/обложка со своей `revision` |

Целые смещения, размеры, counters, номера сегментов и время передаются **строками**.
Float, JSON number, экспонента, `+1`, `01`, `-0`, пробелы и переполнение отклоняются.
Counters: 1…18446744073709551615. Файловые размеры/смещения: 0…9223372036854775807
(предел знакового файлового seek); время: полный int64, включая отрицательный preroll.
`unsigned64()` / `signed64()` SDK отказывают float/bool вместо округления.

`timeline.timebase` — `{numerator:1, denominator:10000000}`: оба небольших JSON
integer 1…1000000000. Значения времени выражены в этих тиках; парсер не переводит
их через double. `timestampOffset` общий. `duration` — положительная строка или
null; неизвестная длительность не выдумывается. При `live=true` duration=null,
`window:{start,end}` допускает отрицательное начало и требует start < end.

Состояния writer: downloading, paused, merging, complete, failed, cancelled.
Ресурсы/дорожки используют те же значения, кроме merging. Это состояния записи;
playback-состояние и выбор текущих дорожек не записываются загрузчиком в снимок.

## Ресурсы и диапазоны

```json
{
  "id": "media",
  "name": "video.part",
  "length": "9007199254741010",
  "ranges": [{"offset": "9007199254740993", "length": "8"}],
  "state": "downloading",
  "retired": false
}
```

`length` может быть null до получения размера. Имена — plain local filenames,
без URL, разделителей, ADS, `..`, ASCII control/null, завершающего пробела/точки
и Windows device names. ID уникальны; имена с одинаковым ASCII case-fold
отклоняются. Проверки Unicode-эквивалентности файлов, hardlinks, владельца,
reparse points и закрепление дескрипторов обязательны в будущем I/O-адаптере:
синтаксический валидатор сам не доказывает безопасность существующего файла.
Абсолютный sessionDirectory принадлежит внешнему вызову открытия/сеансу, а не
ресурсу; его нормализация и привязка v1 сохраняются без изменений.

Диапазоны (не более 128 на ресурс) отсортированы по offset, имеют положительную
length, не пересекаются и не выходят за известную длину/предел seek. Соседние
диапазоны разрешены, `covers()` умеет покрывать их объединение; дыры остаются
недоступными. Арифметика проверяется до сложения. Запрос нулевой длины не
даёт права прочитать байт. `complete` требует известный размер и полное покрытие
0…length; пустой завершённый ресурс длины 0 допустим, его декодируемость не обещается.

`retired=true` помечает исторический ресурс окна, но **не разрешает удаление**.
Текущий `Advance` запрещает удалять ресурс из снимка: для удаления/компактизации
нужен подключённый runtime registry. Поэтому произвольно длинное движущееся live-окно
этим этапом ещё не реализовано; ограниченный draft-снимок описывает его метаданные.

## Дорожки, сегменты и метаданные

Дорожка содержит `id`, `kind` (video/audio/subtitle/attachment), `required`,
`codecHint`, `containerHint`, `state`, `initializationReady`, `source`.
Необязательны `label`, `language`, `default`, `forced`, `initializationResourceId`.
Hints могут быть пустыми и не считаются проверкой реального контейнера/декодера.
Один ресурс может обслуживать video и audio: muxed не требует выдуманного второго файла.
Optional субтитры и обложки не должны блокировать будущий старт A/V; обязательные
потоки и текущий пользовательский выбор будут учитываться медиадвижком.

`source` имеет **либо** `resourceId`, **либо** `segments` (до 128 элементов).
Каждый сегмент задаёт `resourceId`, `sequence`, `start`, положительную `duration`,
`timestampOffset`, `discontinuity`, `configurationId` и необязательный
`initializationResourceId`. Все ссылки существуют в resources. Номера строго
возрастают, шкала после применения описанных start не пересекается, сложение
start+duration не переполняется. Новая configuration требует discontinuity и
init resource. Это описание для backend, а не уже реализованный HLS/DASH demux.

`metadata` содержит собственную строковую revision и необязательные `title`,
`coverResourceId`, `chapters`. Глава: стабильный `id`, строковое `time`, `title`.
Время неотрицательное, упорядочено, не выходит за известную duration; ID уникальны.
До 128 глав в одном ограниченном снимке. Большие списки/потоковые subtitle cues
потребуют отдельного ограниченного канала, не снятия лимита JSON.

## Проверка обновления

`Advance(previous,next)` проверяет:

- Неизменные sessionId, generation и writer instance; смене нужен новый запуск.
- Строго возрастающую revision и неизменную общую шкалу; неизвестная duration
  может один раз стать известной. Live window не движется назад.
- Идентичность ресурсов, сохранение известных размеров и всех опубликованных
  диапазонов; terminal-ресурс не меняет байты и состояние, retired не откатывается.
- Стабильные track ID/kind/source/hints/required; initializationReady не откатывается.
  Опубликованные сегменты неизменны; новые только дописываются к незавершённой дорожке.
- Терминальный writer не открывается заново и не добавляет дорожки/ресурсы/байты.
  Complete/merging не допускаются до окончания обязательных дорожек; complete
  не оставляет активных ресурсов. Failed/cancelled не трактуются как успешный EOF.
- Изменение metadata требует своей новой revision; порядок JSON-ключей не считается
  изменением. Исправление глав/названия не требует переоткрытия A/V.

Ошибки имеют `code`, `path`, `error`: invalid_snapshot, unsupported_schema,
invalid_request, incomplete_resource, stale_revision, generation_changed,
readers_required. Успех validate всегда содержит playbackSupported=false.

## Модель читателей

`shared/progressivereaders.h` реализует `ReaderBook`: readerId, sessionId/generation,
набор ресурсов и подтверждённую OS-идентичность процесса (PID + birth token).
Идентичность передаёт доверенный платформенный адаптер, не поле от клиента.
Вызовы сериализует backend; сам класс не выполняет I/O и не владеет таймером.

Переходы: Attached → Closing → Released. Начало закрытия не снимает lease.
`filesClosed` допустим только после начала закрытия; один reader не освобождает
другой. Неизвестное состояние процесса не снимает удержание. Подтверждённый выход
либо другой birth token у того же PID доказывает смерть прежнего процесса.
Истечение времени/обрыв IPC не являются таким доказательством. Лимит — 128 reader
записей, живые записи нельзя забыть ради освобождения места.

ReaderBook проверен общим conformance-тестом. SDK 1.4 подключает его к
[рабочим v1 sources](ProgressiveReaderLeases.md):
настоящим файлам, приватным квитанциям, OS locks, IPC и восстановлению после crash.
Рабочий v1 также сохраняет прежнее `releaseAcknowledgement`. SDK 1.5 привязывает
поздние ресурсы v2 к независимым leases в общем [ResourceSource](ProgressiveResourceIo.md).
Подключение decoder/remux и безопасная компактизация live-окна ещё не выполнены.

## Проверки и исходники

- `shared/progressivesession.h` — один парсер для Qt и Classic, без Qt/MFC/OS API.
- `shared/progressivereaders.h` — состояние владения reader.
- `tests/progressive_v2_conformance.py` — одинаковые vectors для native runner и API.
- `tests/progressive_model_cli.cpp` — portable C++ runner (GCC/MinGW/MSVC).
- `tests/fixtures/progressive-v2/vod.json` — пример muxed VP9/Opus **метаданных**:
  соответствующий media-файл не поставляется и не открывается при validate.
