# FoxicMP: API воспроизведения во время скачивания, версия 1

Реализация и пример клиента находятся только в проекте FoxicMP. Репозиторий и
интерфейс YouTubeHarvester не изменяются. Контракт общий для трёх редакций: Linux Qt
использует Unix-сокет, Windows Qt и Windows Classic — локальный именованный канал
с теми же JSON-командами и ограничениями. Файл сохранён под прежним именем для
совместимости интеграций. Загрузчик сможет подключить этот контракт
отдельно. API не скачивает URL и не выбирает качество за загрузчик.

## Обнаружение и запуск

```sh
foxicmp-qt --progressive-api-info
foxicmp-qt --profile /absolute/profile --progressive-api-info
python3 integration/progressive_client.py --player /absolute/foxicmp-qt --ensure capabilities
```

Первая команда возвращает JSON с `endpoint`, `profile`, версией, лимитами и
возможностями, не создавая профиль. Клиент `--ensure` запускает плеер или использует
первое окно выбранного профиля. Дополнительные независимые окна своего API не
публикуют. IPC доступен только тому же пользователю Linux и не использует TCP.
Настройка MPRIS на этот интерфейс не влияет.

Транспорт: Unix stream socket из `endpoint`. Одна UTF-8 JSON-строка с `\n` на
соединение, один JSON-ответ с `\n`, затем закрытие. Максимум запроса вместе с
переводом строки — 65536 байтов, ожидание запроса — 5 секунд. Дубли ключей,
неверная версия и неизвестные поля оболочки отклоняются. `ok: true` означает
принятие команды; фактическое воспроизведение подтверждает `state: playing`.

## Снимок сеанса

Для YouTube, Rutube и VK контракт одинаков. Загрузчик предоставляет две
append-only дорожки: H.264 MP4 video и AAC-LC M4A audio. Это могут быть ordinary
fast-start MP4 с готовым `moov` перед `mdat` или fragmented MP4 с готовой
инициализацией. WebM, MPEG-TS, DRM и MP4 с индексом в конце не поддерживаются этим
API. Мультиплексированный HLS нужно сначала представить отдельными локальными
fMP4-дорожками; существующий тестовый HLS-адаптер FoxicMP умеет делать это из уже
скачанных сегментов. Сам приёмник API не перепаковывает файлы.

Расширение на другие контейнеры, кодеки, несколько дорожек, диапазоны и live
описано в [новом ТЗ](ProgressiveTechnicalSpec-2026-10-25.ru.md) и
[плане реализации](ProgressiveIntegrationPlan.md); оно ещё не реализовано в v1.

```json
{
  "schemaVersion": 1,
  "sessionId": "0123456789abcdef0123456789abcdef",
  "generation": 1,
  "revision": 1,
  "state": "downloading",
  "duration100ns": 1200000000,
  "timestampOffset100ns": 0,
  "tracks": {
    "video": {
      "path": "/absolute/download/video.part",
      "container": "mp4",
      "fragmented": true,
      "initializationReady": true,
      "availablePrefixBytes": 1048576,
      "expectedTotalBytes": null,
      "state": "downloading"
    },
    "audio": {
      "path": "/absolute/download/audio.part",
      "container": "m4a",
      "fragmented": true,
      "initializationReady": true,
      "availablePrefixBytes": 262144,
      "expectedTotalBytes": null,
      "state": "downloading"
    }
  }
}
```

Числа байтов в примере условные: загрузчик должен подтверждать только непрерывный
префикс, уже записанный в файл, а не сетевой счётчик и не размер sparse-файла.
Начальные индексы и первый пакет каждой дорожки должны быть доступны до `start`.
Ранний вызов вернёт `source_not_ready`; его можно повторить после записи индексов.
`sessionId` генерируется UUID4 (`uuid.uuid4().hex`). При обновлениях `revision`
строго возрастает; идентификатор, generation, пути, формат и временная шкала
фиксированы. Время указано в единицах 100 нс: 1 секунда = 10000000.
Для ordinary MP4 полный размер обязателен сразу; для fMP4 до завершения допустим
`null`, длительность должна быть положительной.

Поля `tracks.*.path` могут быть абсолютными локальными путями, как в примере,
либо простыми именами файлов (`video.part`, `audio.part`). Во втором варианте
команда передаёт абсолютный `sessionDirectory` рядом с `session`. Плеер разрешает
только обычные имена внутри этого каталога и отклоняет разделители, `..`, ссылки,
сетевые и другие не локальные пути. При `start` каталог запоминается для следующих
`update`/`complete`, поэтому его можно передать только один раз. Эквивалентная
нормализованная запись каталога допустима; другой каталог отклоняется даже при
абсолютных путях дорожек. Замена, завершение и отмена не передают его следующему
сеансу. Неудачная попытка start/replace не меняет каталог работающего сеанса.

Плеер открывает обычные файлы текущего пользователя на чтение, отвергает
символические ссылки и FIFO, удерживает дескрипторы и не изменяет медиа.
Переименование/удаление имени файла при финализации не прерывает чтение уже
открытого inode. Сжатие, перезапись или подмена содержимого запрещены; усечение
подтверждённых данных приводит к ошибке. При перезапуске скачивания нужен новый
сеанс и явная замена старого. Плеер не удаляет временные файлы загрузчика.

## Команды

Каждая команда содержит `version: 1` и `action`:

| action | Дополнительные поля | Назначение |
|---|---|---|
| `capabilities` | нет | Поддерживаемые форматы, пороги и лимиты |
| `validate` | `session` (schema 2), необязательный `previous` | Только проверка расширенных метаданных/перехода, без открытия файлов |
| `probe` | `session`, необязательный `sessionDirectory` | Проверить снимок без запуска воспроизведения |
| `start` | `session`, необязательные `title`, `activate` (true), `replace` (false) | Открыть растущие дорожки |
| `update` | `sessionId`, `session`, необязательный `sessionDirectory` | Новый полный снимок подтверждённых данных |
| `complete` | `sessionId`, `session`, необязательный `sessionDirectory` | Снимок с `state: complete` у сеанса и обеих дорожек |
| `heartbeat` | `sessionId` | Загрузчик работает, новых данных пока нет |
| `status` | `sessionId` | Состояние, позиция, общая доступная граница |
| `cancel` | `sessionId` | Закрыть только этот сеанс |
| `release` | `sessionId` | Закрыть reader; `released` подтверждает закрытие его файлов |
| `readerStatus` / `readerRelease` / `readerForget` | `readerId` | Opt-in учёт по конкретному reader ID, независимо от текущего видео |
| `readers` | `sessionId`, строковая `generation`, необязательный `afterReaderId` | Ограниченная постраничная выдача квитанций профиля |
| `resourceProbe*` | snapshot v2 / probeId, согласно отдельному контракту | Асинхронная проверка подтверждённых диапазонов без запуска playback |
| `error` | `sessionId`, `message` | Сообщить об ошибке загрузки и прекратить чтение |

В финальном снимке `expectedTotalBytes == availablePrefixBytes` у обеих дорожек.
Физический конец растущего файла никогда сам по себе не считается завершением.
Загрузчик отправляет update или heartbeat не реже одного раза в 5 секунд; после
15 секунд без них плеер ждёт загрузчик, а новый heartbeat/update разрешает
продолжение. После подтверждённого complete heartbeat больше не нужен.

Повторный start с тем же sessionId идемпотентен и не перематывает воспроизведение.
Другой активный progressive-сеанс заменяется только с `replace: true`. Ошибочный
новый источник не закрывает уже работающий. Отмена старого сеанса не останавливает
другое видео, открытое пользователем.

Ответы содержат `version`, `ok`, `sessionId`, `active`, `state`, `connected`,
`complete`, `positionSeconds`, `availableSeconds`, `durationSeconds`, `error`
(для активного сеанса). Состояния: buffering, waiting-downloader, playing,
paused, finished, error, cancelled, stopped. `finished` означает естественный конец
воспроизведения. После него плеер освобождает сеанс и открытые файлы, а status
сохраняет результат с `active: false`; повторный cancel безопасен. Ошибки команды имеют `ok: false`, машинный
`code` и пояснение `error`; например source_not_ready, invalid_update,
session_busy, session_closed, unknown_session.

Новые сборки дополнительно объявляют `releaseAcknowledgement: true` и возвращают
`released`. `active: false` само по себе не доказывает закрытие дескрипторов.
До `released: true` writer сохраняет исходники. `release`/`cancel` не отменяют
скачивание. Подробнее: [SDK и writer-стенд](ProgressiveSdk.md).

SDK 1.3 добавляет `validationSchemaVersions:[2]`. Успех validate содержит
`playbackSupported:false` (проверка только метаданных). SDK 1.6 объявляет
`playbackSchemaVersions:[1,2]` и ограниченный schema2Playback-профиль: start/probe
поддерживают две отдельные fMP4 H.264/AAC-LC дорожки.
[Черновой контракт v2 и его ограничения](ProgressiveManifestV2.md).

SDK 1.4 добавляет `trackReader:true` во внешний start и
`readerLeaseProtocol:1`. [Контракт reader leases и recovery](ProgressiveReaderLeases.md).
Старые JSON-вызовы и release по sessionId сохраняются.

SDK 1.5 объявляет resourceIoProbeVersion=1 и resourceIoPlaybackSupported=false.
[ResourceProbe, ожидание, обновления и отмена](ProgressiveResourceIo.md) выполняют
I/O в worker. Это отдельная диагностика; [медиапрофиль SDK 1.6](ProgressiveMediaV2.md)
связывает ResourceSource с декодером и имеет собственные ограничения.

Воспроизведение начинается с 10 общих секунд данных; завершённые короткие ролики
стартуют сразу. Менее 1 секунды запаса включает буферизацию, 5 секунд возобновляют
просмотр. Ручная пауза сохраняется. Перемотка ограничена общей подтверждённой
частью видео/аудио и кэшем декодера. Звук и видео декодирует один libmpv.

SDK 1.7: [первичная подготовка probe/start через IPC вне UI](ProgressivePreparation.md),
ограничение одного worker и защита от запоздавшего запуска после Stop/Close.
SDK 1.8 расширяет это на [update/complete](ProgressivePublication.md) с коротким
применением готового metadata/index. Legacy/provider opening, registry I/O и часть
финального закрытия ещё требуют работы.

`probe` выполняет ту же проверку контейнера, индекса, инициализации и
подтверждённого префикса, что и `start`, но не создаёт активный сеанс и не
перехватывает окно. Ответ содержит `supported`, `mode`, `reasonCode`, `reason`,
`readySeconds` и `complete`. Для текущего нативного пути `mode` равен
`native-progressive`; неподдержанный формат получает `post-download`,
`native-progressive-rejected` и `requiresCompleteFile: true`. Поле
`fallback.available` в `capabilities` сейчас ложно: плеер не запускает скрытую
сетевую загрузку и не делает неявный remux. Это позволяет загрузчику заранее
выбрать обычное воспроизведение после полной загрузки.

## Подключение из Python

Файл `integration/progressive_client.py` поставляется с плеером и использует
только стандартную библиотеку Python. В исходниках это `qt/tools/progressive_client.py`.
Рядом поставляются событийный `progressive_sdk.py` и offline-стенд
`progressive_writer.py`. Их интерфейс одинаков для Linux Qt, Windows Qt и Classic;
SDK заранее проверяет поддержку подтверждения освобождения в capabilities.

```python
from progressive_client import ProgressiveClient

client = ProgressiveClient("/absolute/FoxicMP/foxicmp-qt")
client.ensure_running()
reply = client.start(snapshot, title="Название ролика")
# После записи очередной порции: обновить префиксы и увеличить revision.
client.update(snapshot)
client.command("heartbeat", snapshot["sessionId"])
status = client.command("status", snapshot["sessionId"])
# После закрытия файлов загрузчиком и подготовки финального снимка:
client.update(final_snapshot, complete=True)
```

CLI принимает `--snapshot /absolute/snapshot.json` для start/update/complete и
`--session-id HEX` для остальных команд. Файлы snapshot нужно обновлять атомарно,
если тестовый клиент читает их одновременно с записью. Для применения API в
YouTubeHarvester достаточно позже подключить эти вызовы к его событиям загрузки;
изменений Harvester в рамках этой работы нет.

## Реестр и cleanup (SDK 1.9)

IPC readerStatus/readerRelease/readers/readerForget выполняются вне UI в отдельном
слоте. Playback Source уничтожаются в ограниченной очереди: 32 активных/закрывающихся
объекта, не более двух workers. `source_limit` означает отказ нового Source до
открытия файлов. Release остаётся подтверждением фактического закрытия, не таймером.
[Capabilities, retry и ограничения](ProgressiveRetirement.md).
