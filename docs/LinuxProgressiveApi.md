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

В Classic SDK 1.13 запрос выхода во время асинхронного закрытия progressive STA
отклоняет новые IPC-команды с player_closing. Принятый перед выходом start может
ожидать закрытия старого графа; Stop отменяет его. После Cancel обычный start
без replace совместимо принимается, если другого активного pending-сеанса нет.
[Границы этого пути Classic](ProgressiveClassicClose.md).

Новые сборки дополнительно объявляют `releaseAcknowledgement: true` и возвращают
`released`. `active: false` само по себе не доказывает закрытие дескрипторов.
До `released: true` writer сохраняет исходники. `release`/`cancel` не отменяют
скачивание. Подробнее: [SDK и writer-стенд](ProgressiveSdk.md).

SDK 1.3 добавляет `validationSchemaVersions:[2]`. Успех validate содержит
`playbackSupported:false` (проверка только метаданных). SDK 1.6 объявляет
`playbackSchemaVersions:[1,2]` и ограниченный schema2Playback-профиль: start/probe
поддерживают две отдельные fMP4 H.264/AAC-LC дорожки.
SDK 1.14 расширяет schema 2 профилем [HEVC Main/Main10 + AAC-LC](ProgressiveHevc.md):
проверять schema2Playback.profiles и schema2Playback.videoCodecs. SDK 1.15 добавляет [AV1 Main 8/10-bit + AAC-LC](ProgressiveAv1.md) с Sequence Header
в av1C. Старый v1-путь и его верхнеуровневый список кодеков этим не расширяются.
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

## Legacy/provider opening (SDK 1.10)

CLI `--harvester-session` сохраняется; manifest/provider attach/начальная индексация
готовятся в worker. [Слот, deadline и отмена](ProgressiveLegacyOpening.md).
Runtime capabilities содержат `legacyPreparationPending`; это занятость подготовки,
а не разрешение на удаление ресурсов. Синхронный Source::open остаётся внутренним
низкоуровневым вызовом.

## Legacy maintenance (SDK 1.11)

`legacyRefresh` объявляет I/O вне metadata mutex UI и согласованное применение
snapshot/index-ready состояния. [Границы и проверки](ProgressiveLegacyMaintenance.md).
Усечение проверяется и при неизменном manifest; release по-прежнему требует
фактического закрытия ресурсов. Graph/renderer shutdown отдельно.

## Decoder lifetime (SDK 1.12)

Qt `engineRetirement`: 16 active/closing cores, до двух workers, native destroy
и software renderer free вне UI; GL context free сохраняет owner thread.
[Callback lifetime и подтверждение закрытия](ProgressiveEngineShutdown.md).
Classic отдельно объявляет `graphShutdown`, не detached teardown.

SDK 1.16 добавляет [VP9 Profile 0/2 8/10-bit + AAC-LC fMP4](ProgressiveVp9.md).
Проверять schema2Playback.profiles/videoCodecs; WebM и Opus пока не поддержаны
этим progressive-мостом.

SDK 1.17 добавляет [Opus mono/stereo + H.264/HEVC/AV1/VP9 fMP4](ProgressiveOpus.md).
Проверять schema2Playback.profiles/audioCodecs. Нужен согласованный pre-skip/edit;
WebM и multichannel не добавлены этим progressive-профилем.

SDK 1.18/1.19 добавляют ограниченные отдельные WebM VP9/AV1 + Opus
[профили](ProgressiveWebMAv1.md). SDK 1.20 поддерживает
[смешанные пары fMP4/WebM](ProgressiveMixedContainers.md) в обоих направлениях:
контейнер задаётся каждой дорожке отдельно. Проверять `schema2Playback.profiles`
и `mixedContainers` у текущего EXE; `status` сообщает `videoContainer` и
`audioContainer`. Это расширение schema 2, а не изменение ограничений v1 выше.

SDK 1.21 расширяет WebM-профили [VP8/Vorbis](ProgressiveVp8Vorbis.md):
VP8-видео, Vorbis mono/stereo 48 kHz, floor 1, bounded setup. Поддержаны
прежние видеокодеки с WebM Vorbis и VP8 с fMP4 AAC-LC/Opus; проверять
конкретный профиль текущего EXE. v1 и Python API не меняются.

SDK 1.22 добавляет [несколько аудио и полные UTF-8 SRT](ProgressiveMultiTrack.md):
один video, 1–8 required audio и до четырёх optional SRT. Поздний complete SRT
подключается во время просмотра. Поддержку проверять по multiAudio/audioTrackLimit
и subtitles; прежнее trackCount:2 сохранено для старых клиентов.

SDK 1.23 снимает предел 0–10 секунд при нескольких аудио в Qt:
[обычный seek в подтверждённой части](ProgressiveSeek.md) сохраняет выбранные
дорожки и паузу. `qtMultiAudioSeek.mode:confirmed-byte-seek`,
`confirmedRangesOnly:true`; прежний числовой maxTargetSecondsWhileIncomplete
сохранён как верхняя граница формата, фактический предел — availableSeconds.
Готовый источник допускает seek по всей длительности. Legacy и two-track
сохраняют прежний способ открытия; Classic использует свой native индекс.

Qt status во время перехода включает `seeking:true` и `seekTargetSeconds`.
`positionSeconds` остаётся фактической позицией декодера. У хвоста буферизация
считается относительно цели; новый seek может отменить ожидание через переоткрытие
декодеров на том же Source.

SDK 1.24 добавляет общую для трёх редакций [изоляцию optional SRT](ProgressiveOptionalSrt.md).
subtitleTracks.state: waiting/ready/error/cached; errorCode/error относятся к
отдельной SRT. Backend подключение сообщает loaded и backendErrorCode/backendError.
Корневой error сохраняется для A/V/сеанса; неудачная SRT не прерывает видео.
Проверять subtitles.optionalErrorIsolation и retainsValidatedCopyOnSourceError.
Метаданные, required-дорожки и reader release по-прежнему проверяются строго.

SDK 1.33 добавляет [ограниченное воспроизведение цепочки fMP4 H.264/AAC-LC](ProgressiveSegmentPlayback.md) через общий I/O трёх редакций. TS, live, смена codec/init и произвольный HLS/DASH остаются за границей этого профиля; Harvester не менялся.

SDK 1.34 добавляет [HEVC Main/Main10 в сегментном fMP4](ProgressiveSegmentHevc.md) с AAC-LC для всех трёх редакций. Требуется явная capability профиля; TS/live и смена конфигурации остаются за границей этого этапа.

SDK 1.35 добавляет [AV1 Main 8/10-bit в сегментном fMP4 с AAC-LC](ProgressiveSegmentAv1.md) для всех трёх редакций. VP9/Opus в сегментном профиле, TS/live и смена конфигурации остаются отдельными этапами.

SDK 1.36 добавляет [VP9 8/10-bit и Opus mono/stereo в сегментном fMP4](ProgressiveSegmentVp9Opus.md) для всех трёх редакций. Следующий этап — длинные ролики без упора в 64 ресурса; WebM/TS, muxed A/V, смена конфигурации и live остаются последующими этапами.

SDK 1.37 добавляет [постраничный каталог fMP4](ProgressiveCatalogPlayback.md), capability `schema2Playback.segmentedPlayback.catalog.mode=paged-fmp4-v1`. В новом режиме до 8192 раскрытых metadata-ресурсов, восемь открытых файлов, обязательны SHA-256 и сохранение всех опубликованных файлов до release главного reader ID. Проверены три редакции с 182–183 ресурсами; многочасовая A/V-приёмка ещё нужна. Inline `validate` не читает страницы; проверяйте каталог через `probe` с `sessionDirectory`.

SDK 1.41: [один растущий TS AVC/AAC VOD](ProgressiveGrowingTs.md),
`containerHint: mpegts-growing`, одна physical resource и две required
logical scalar views. Capability согласуется отдельно; incomplete packet/PES
и закрытие GOP не превращаются в ложный EOF. [Проверки](ProgressiveGrowingTsValidation-2026-10-31.md).

SDK 1.42: [общий растущий fMP4 H.264/AAC](ProgressiveMuxedFmp4.md),
`containerHint: fmp4-muxed`, две scalar views одного physical resource.
Capability согласуется как `schema2Playback.muxedPlayback.fmp4`; новый
индекс публикуется после полного moof/mdat обеих дорожек.

SDK 1.43: [shared WebM VP9/Opus](ProgressiveMuxedWebm.md),
`containerHint: webm-muxed`, capability `schema2Playback.muxedPlayback.webm`;
VP9 8/10-bit, Opus mono/stereo, fractional DefaultDuration и complete Clusters.

SDK 1.44: [shared WebM AV1/Opus](ProgressiveMuxedWebmAv1.md),
точное согласование `schema2Playback.muxedPlayback.webm.av1.mode`,
`containerHint: webm-muxed` и обязательный video `codecHint: av1`.

SDK 1.45: [shared WebM VP8/Opus](ProgressiveMuxedWebmVp8.md),
`containerHint: webm-muxed`, обязательный video `codecHint: vp8`,
точное согласование `schema2Playback.muxedPlayback.webm.vp8.mode`.

SDK 1.46: [shared WebM Vorbis](ProgressiveMuxedWebmVorbis.md), явные video/audio
codecHint и capability `schema2Playback.muxedPlayback.webm.vorbis` с точной парой.

SDK 1.68: [gapless MP3 в Windows/Linux Qt](ProgressiveMp3Gapless.md),
отдельный `standalone-mp3-gapless-v1` с полезной звуковой шкалой,
проверенным tag CRC, точным PCM trim/seek и ожиданием writer complete.
[Результаты](ProgressiveMp3GaplessValidation-2026-11-02.md): по 31 новым native
checks, четырём growing profiles и 16 controller PCM windows на Qt ОС.
Classic явно отказывает новому профилю без reader, сохраняя прежнее воспроизведение;
его output adapter остаётся открытым. Общие capabilities прежние, Qt-дополнение
описано отдельно. Linux обновлён с backup и сохранением INI. Harvester не изменён.

SDK 1.69: [gapless MP3 через обычный API Classic](ProgressiveClassicMp3Gapless.md).
Проверенный внутренний decoder подключается независимо от codec merits;
статус отражает применение PCM-окна, в том числе после paused seek/Stop/cancel.
Четыре growing profiles Classic и 12 запусков Stop/cancel/reopen с отключённым
обычным приоритетом internal MP3 прошли проверку. Повторены четыре Qt profiles
и 16 controller PCM windows на каждой ОС; SDK — по 68 tests Linux/Windows.
Общий manifest сохранён, Classic/Qt additions описаны отдельно. Полная audio
matrix, длительная A/V-приёмка и реальная Harvester-интеграция остаются открытыми.

SDK 1.70: [растущий RIFF/WAVE PCM16](ProgressiveWav.md), mono/stereo
44.1/48 kHz, отдельный профиль с известными RIFF/data sizes. Проекция без
перекодирования сохраняет PCM bytes; неполные packets ждут update.
По 55 native checks Windows/Linux и 55 ASan/UBSan checks, четыре growing
profiles каждой редакции; повторены MP3 gapless/ADTS native и Classic playback.
SDK — по 69 tests на ОС. Linux обновлён атомарно с backup и сохранением INI;
Harvester и установленные Windows-плееры не менялись. RF64/float/24-bit,
прочие audio profiles и полная приёмка остаются открытыми.

SDK 1.71: [растущий Ogg Opus](ProgressiveOggOpus.md), mono/stereo 48 kHz,
CRC страниц, continued packets, pre-skip/gain и EOS end trim. Виртуальный
WebM сохраняет исходные Opus payload; EOS без writer complete не даёт EOF.
По 87 native checks Windows/Linux, включая packet durations 2.5/5/10/20/40/60 ms,
continued tags/audio и односэмпловую конечную обрезку; те же 87 ASan/UBSan checks.
По четыре growing profiles трёх редакций, SDK — по 70 tests на ОС.
Ogg Vorbis, chaining/multiplexing/live, прочие audio profiles и общая приёмка
остаются открытыми. Harvester и установленные Windows-плееры не менялись.

SDK 1.72: [растущий Ogg Vorbis](ProgressiveOggVorbis.md), Vorbis I
mono/stereo 48 kHz с существующим bounded setup/floor-1 validator.
Первый packet подготавливает окно, дальнейшие granules считаются по
перекрытию блоков; EOS trim передаётся без повторной обрезки.
По 69 native checks Windows/Linux и ASan/UBSan, включая continued packets,
большие comments, шумовой сигнал и односэмпловый end trim. По четыре growing
profiles каждой редакции; SDK — по 71 tests на ОС. Ogg Opus и WAV повторно
проверены. Начальная обрезка/nonzero origin, chaining/multiplexing/live и
остальная audio matrix остаются открытыми. Harvester не менялся.

SDK 1.73: [растущий AC-3](ProgressiveAc3.md), bsid 8, mono/stereo
48 kHz, шесть блоков/1536 samples, обе CRC и неизменная конфигурация.
Виртуальный fMP4/ac-3 сохраняет исходные syncframes и цифровой PCM.
По 96 native checks Windows/Linux и ASan/UBSan, 20 PCM corpora со всеми
объявленными битрейтами; по четыре growing profiles трёх редакций.
Повторены Ogg Vorbis/WAV native и Classic playback, SDK — по 72 tests на ОС.
E-AC-3, многоканальные layouts, другие rates/bsid, bitstream hardware и
полная audio matrix остаются открытыми. Harvester не менялся.

SDK 1.74: [растущий E-AC-3](ProgressiveEac3.md), независимый substream 0, bsid 16, mono/stereo 48 kHz, шесть блоков и CRC. Проверяются шесть битрейтов, 80 native checks и четыре growing profiles каждой редакции; dependent substreams/JOC, другие rates/layouts и общая audio matrix остаются открытыми.

SDK 1.75: [растущий WAV PCM24](ProgressiveWav24.md), legacy RIFF tag 1, mono/stereo 44.1/48 kHz, отдельная capability и in24/enda adapter. По 65 native checks и четыре growing profiles каждой редакции; все три байта PCM, clocks и RIFF padding сохранены. PCM16 не изменён; RF64/extensible/float, другие layouts/rates и полная audio matrix остаются открытыми.

SDK 1.76: [растущий WAVEFORMATEXTENSIBLE PCM24](ProgressiveWavExtensible24.md), mono/stereo 44.1/48 kHz, PCM GUID, 24 container/valid bits и явные маски 4/3. По 83 native checks с FFmpeg и независимым struct writer, четыре growing profiles каждой редакции; legacy PCM16/24 повторно проверены. Другие masks/valid bits/layouts, RF64/float и полная audio matrix остаются открытыми.

SDK 1.77: [растущий RF64 PCM16/PCM24](ProgressiveRf64.md), mono/stereo 44.1/48 kHz, обязательный ds64 и согласованные 64-bit sizes/sampleCount. По 161 native checks, восемь growing profiles каждой редакции и восемь синтетических сценариев layout/index/seek за 4 GiB. Старые RIFF-профили сохранены. Qt CLI discovery теперь compact JSON в прежнем лимите 64 KiB. Физические RF64 больше 4 GiB, float/другие layouts и полная audio matrix остаются открытыми.

SDK 1.78: [внешние SHA-256 индексы](ProgressiveExternalIntegrity.md) и [физические RF64 больше 4 GiB](ProgressiveLargeRf64.md). PCM16/PCM24 stereo 48 kHz проверены в каждой редакции: обычные файлы 4,3 GB, пауза при публикации, дальний переход, фактическое чтение за 2^32, complete/EOF/release, независимый PCM и SHA всего файла. Исправлен индексный seek Qt, включая замену ещё не завершённого перехода. По 52 новых native checks и 80 SDK tests на ОС; ASAN/UBSAN и регрессии прошли. [Итоговый отчёт](ProgressiveLargeRf64Validation-2026-11-03.json). Лимит JSON 64 KiB и прежние inline blocks сохранены; внешние индексы пока согласуются только для RF64. Linux обновлён с backup без изменения INI; Harvester, установленные Windows EXE/INI и 158 shell keys сохранены. Остальные большие RF64 rates/layouts, sparse/live и общая audio matrix остаются открытыми.

SDK 1.79: [RF64 ranges вне порядка](ProgressiveRf64Ranges.md), явная capability `outOfOrder` и prefix-gated playback. Принимаются подтверждённые островки, но декодер и seek используют только непрерывный head prefix; дыры дают ожидание, их заполнение продолжает тот же reader и сохраняет ручную паузу. Лимит 16 MiB применяется к новым подтверждённым байтам, включая сценарий одного байта, открывающего >16 MiB прежних данных. По 229 новых native checks и 81 SDK tests на ОС, 16 native profile/index combinations, по четыре runtime profile combinations каждой редакции; ASAN/UBSAN, прежние RIFF/RF64 и Qt-физические >4 GiB прошли. Начальные ошибки fixtures и CTest exit8 сохранены. Также устранена гонка чтения/роста индекса Qt, проверен порядок блокировок; после исправления полный набор из 15 групп на каждой Qt ОС прошёл с exit0. [Отчёт](ProgressiveRf64RangesValidation-2026-11-03.json). Linux обновлён с backup без изменения INI; Windows-установки, 158 shell keys и Harvester сохранены. Seek в отдельные островки, другие контейнеры и общая sparse-матрица остаются открытыми.

SDK 1.80: [диагностическая карта полных RF64 PCM-пакетов](ProgressiveRf64RangeMap.md), включая подтверждённые интервалы за дырой, точные frame/byte/tick boundaries, объединение соседних ranges и короткий последний packet. Карта не читает payload, `geometryOnly=true`, `payloadVerified=false`, `islandSeek=false`; actual playback по-прежнему prefix-gated. До 32 интервалов в status, с явными total count/truncation. По 242 новых native checks на ОС, 1608 синтетических 64-bit geometry cases на ОС (в Linux под ASAN/UBSAN), по четыре actual status/playback profiles в каждой редакции; 16/16 Qt-групп на ОС и 81 SDK tests прошли. [Отчёт](ProgressiveRf64RangeMapValidation-2026-11-03.json). Linux обновлён с backup без изменения INI; Windows EXE/INI, 158 shell keys и Harvester сохранены. Реальный island seek, его decoder adapters и прочая sparse/container matrix остаются открытыми.

SDK 1.81: [явные RF64 sparse playback profiles](ProgressiveRf64Sparse.md) для PCM16/24 mono/stereo 44.1/48 kHz. Стабильный виртуальный индекс создаётся из проверенного header, payload reads ждут writer ranges и проверяют SHA; seek разрешён в подтверждённые целые PCM packets, включая островок за дырой и обратный переход. Ready end/буферизация следуют текущему интервалу, ручная пауза и reader сохраняются; конец островка не становится EOF. По 272 native checks и 82 тестов SDK на ОС, ASAN/UBSAN; по четыре runtime profiles каждой редакции, complete last50ms seek и отдельный cancel-at-hole. Предварительный широкий Qt-набор 18/18 на ОС и окончательные пересобранные бинарные файлы с 8/8 затронутыми группами проверены раздельно — [отчёт](ProgressiveRf64SparseValidation-2026-11-03.json). Linux EXE/V2 client обновлены с backup без изменения INI; Windows-установки, 158 shell keys и Harvester сохранены. Обычные RF64 hints остаются prefix-gated. На этом исходном срезе физические sparse islands >4 GiB ещё не проверялись; дополнительная проверка тех же бинарных файлов приведена ниже. Maximum-size/stress, video/другие контейнеры/live и общая sparse-матрица остаются открытыми.

Дополнительная проверка SDK 1.81: [физические sparse RF64 больше 4 GiB](ProgressiveLargeSparseRf64.md), PCM16/PCM24 stereo 48 kHz, по два профиля каждой редакции. Проверены настоящие незаписанные области, индексный переход за 2^32 при малом decoder I/O, ожидание/расширение островка, обратный seek, отказ ложного complete, заполнение всех дыр, SHA всего файла и EOF/release — [результаты](ProgressiveLargeSparseRf64Validation-2026-11-03.json). Production code и EXE не изменялись; Linux уже установлен в проверенной версии. Windows EXE/INI и 158 shell keys сохранены, Harvester не менялся. Maximum-size/stress, остальные большие rates/layouts, video/другие контейнеры/live остаются открытыми.

Дополнительная проверка SDK 1.81: [эффективные пределы RF64 и повторное открытие ядра](ProgressiveRf64Limits.md). Восьми native profiles на ОС соответствуют точные границы 250000 packets / 16 GiB и восемь отказов при превышении на один PCM frame. На каждой ОС отдельно выполнены 100 native core open/cancel/release cycles с замерами RSS/private/handles; это не 100 GUI/decoder lifecycles. По два предельных stereo 48 kHz файла фактически воспроизводятся в каждой редакции, включая repeat/backward seek и cancel у незагруженного EOF — [данные](ProgressiveRf64LimitsValidation-2026-11-03.json). Production code/EXE, личные профили, Windows shell и Harvester сохранены. Долгий stress, full-file complete/EOF на 16 GiB, остальные предельные playback rates/layouts и общая матрица остаются открытыми.

Дополнительная проверка SDK 1.81: [100 фактических source/decoder lifecycles в каждой редакции](ProgressivePlayerLifecycle.md), четыре RF64 PCM16/24 stereo 48 kHz режима, 300 измеряемых циклов и 12 прогревочных. Каждый цикл подтверждает реальное воспроизведение, Stop через панель, read-only filesClosed до SDK release, удаление исходника и RSS/private/handles/threads. Устранены две Windows-утечки: потерянные Thread HANDLE Classic splitter и Event HANDLE detached TSD cleanup libwinpthread Windows Qt. Проверены обычный Classic Stop/Play, growing video/audio регрессии и portable Qt с частной исправленной DLL; [точные результаты и exit codes](ProgressivePlayerLifecycleValidation-2026-11-03.json). Пороги утечек не ослаблялись. Установки/INI, 158 shell entries и Harvester сохранены; Linux production EXE не изменился и повторной замены не требует. SDK-контракт и опубликованный ZIP 1.81 неизменны; исправления находятся в текущих исходниках. Длительная нагрузка, полная matrix, несколько readers и итоговая поставка остаются открытыми.

Дополнительная проверка SDK 1.81: [несколько readers одного растущего RF64](ProgressiveMultiReaderPlayback.md). Четыре PCM16/24 stereo 48 kHz режима на редакцию, два playback endpoints и дополнительная I/O-проверка, максимум пять квитанций. Отмена и toolbar Stop изолированы, проверены повторное открытие и удаление только после всех release. В Qt — два Window/Player одного процесса, в Classic — два процесса. Исправлен transient reader_registry_busy в общем raw resource-probe loop: повтор в прежнем wait budget с отменой, без изменения zero-wait/deadline ошибок. Принудительная блокировка, прежние 11 resource-I/O checks и Qt growing AAC/H.264 регрессии — [отчёт](ProgressiveMultiReaderPlaybackValidation-2026-11-03.json). Windows-установки/INI, 158 shell entries и Harvester сохранены; Linux обновлён атомарно с backup. Полная concurrent/stress matrix, смешанные пары, crash и итоговая поставка открыты; опубликованный SDK ZIP 1.81 неизменён.

Дополнительная проверка SDK 1.81: [принудительное завершение настоящего плеера и recovery readers](ProgressivePlayerCrashRecovery.md), четыре RF64 PCM16/24 профиля каждой редакции, два самостоятельных процесса на всех платформах. Проверены аварии при playing/buffering, 36 сохранённых квитанций с OS-confirmed process exit, продолжение второго плеера и writer, штатный перезапуск того же профиля, новое воспроизведение и изоляция старых команд — [точные exit codes и данные](ProgressivePlayerCrashRecoveryValidation-2026-11-03.json). Ненулевые exit codes принудительно завершённых дочерних процессов отделены от успешных драйверов. Новых production-исправлений не потребовалось; EXE/INI, 158 shell entries, SDK ZIP и Harvester сохранены. Установленный Linux уже совпадает с проверенным. Writer crash/disk-full, возврат к прежней позиции, rename, mixed-edition и длительный stress остаются открытыми.

Дополнительная проверка SDK 1.81: [writer crash, неопубликованный неполный PCM-кадр и инъекция ENOSPC](ProgressiveWriterFailures.md), по восемь сценариев четырёх RF64-профилей каждой редакции. Реальные writer-процессы, не менее 16 s без heartbeat, waiting-downloader без ложного EOF/release, независимая контрольная сессия, проверка опубликованных bytes и явная generation 2 для нового writer — [отчёт и exit codes](ProgressiveWriterFailuresValidation-2026-11-03.json). ENOSPC внедряется после реальной короткой записи; заполненная файловая система не испытывалась. Первый дефект тестового буфера исправлен без изменения порогов плеера, failed runs сохранены. Production EXE/INI, 158 shell entries, SDK ZIP и Harvester не изменились; установленный Linux уже совпадает с проверенным. Реальный disk-full, durable checkpoint/power-loss, прочие контейнеры и длительный stress остаются открытыми.

Дополнительная проверка SDK 1.81: [финализация RF64 при активном просмотре](ProgressiveFinalFileTransition.md), четыре PCM16/24 stereo 48 kHz профиля каждой редакции и два самостоятельных процесса. Полные bytes/SHA подготовлены до rename, complete публикуется после него с прежним логическим именем; descriptor rename отклоняется, подмена старого пути не меняет закреплённые readers и отвергается новой SHA-проверкой. По восемь естественных EOF и открытий итогового имени в generation 2 на редакцию; независимый FFmpeg подтверждает точный PCM — [отчёт](ProgressiveFinalFileTransitionValidation-2026-11-03.json). Native ResourceSource: 30/28 Linux/Windows checks. Production EXE/INI, 158 shell entries, SDK ZIP и Harvester сохранены; установленный Linux уже соответствует проверенному. Другие контейнеры/большие sparse и sidecars, межтомные/коллизионные moves, durable rename, mixed-edition и длительная нагрузка остаются открытыми.

Дополнительная проверка SDK 1.81: [600 s реального растущего RF64](ProgressiveSoakPlayback.md) в каждой из трёх редакций, два самостоятельных процесса: PCM16 ordinary hint и PCM24 sparse hint, stereo 48 kHz, последовательный префикс. Паузы writer 10/30/60 s с heartbeat, реальная buffering/resume после длинных пауз, прежние readerId и скорость 1. Заранее заданные бюджеты CPU/RSS/private/handles/threads/session bytes/API/release выдержаны всеми шестью процессами; [полные измерения и exit codes](ProgressiveSoakValidation-2026-11-03.json). Внешние SHA sidecars учитываются отдельно и удаляются после filesClosed. Короткий smoke исключён из приёмки. EXE/INI, 158 shell entries, SDK ZIP и Harvester сохранены; Linux уже соответствует проверенному EXE. Это не 30-минутный физический A/V drift, полный codec/video/remux/live stress или итоговая приёмка ТЗ.

Дополнительная проверка SDK 1.81: [600 s растущего видео H.264/AAC fMP4 и MPEG-TS](ProgressiveVideoSoakPlayback.md) на каждой редакции, два самостоятельных процесса и паузы writer 10/30/60 s. Первый полный прогон выявил реальный отказ на 129-м inline SHA-блоке: общий legacy array limit 128 противоречил объявленным 4096 блокам. Исправлен только scoped validator resources[].integrity.blocks schema v2 в общем Parse, Qt/Classic API, Classic v2 publish и ResourceProbe; прочие массивы/legacy, nesting, duplicate keys, SHA/coverage и 64 KiB сохранены. По 13 новых native checks Windows/Linux и 13 ASan/UBSan, resource I/O/integrity/cache и model/segments регрессии. Повторный полный прогон с прежними 2 s порциями и бюджетами прошёл; каждый reader перешёл границу 128 блоков. На новых EXE повторены bounded remux: по 24 preparation checks и 20 preview checks/4 profiles на редакцию, независимые native packet/PCM/PTS проверки. [Все измерения, before-fix failures и exit codes](ProgressiveVideoSoakValidation-2026-11-03.json). Linux обновлён атомарно с backup, Windows-установки/INI, личный профиль Linux, 158 shell entries, immutable SDK ZIP 1.81 и Harvester сохранены. Непрерывный remux, HD/4K/прочие codecs, полная stress matrix и физический 30-min A/V остаются открытыми.

Первый компонент непрерывного remux: [внутренний потоковый FFmpeg worker](ProgressiveContinuousRemux.md), H.264 video-only без reorder, обычный MP4 → incremental fMP4. Один context переживает несколько публикаций; полный media fragment доступен до complete, packet/frame hashes и PTS/DTS/duration сохранены для 600/720 packets. По 9 native сценариев Windows/Linux и 9 ASan/UBSan, включая ожидание дольше 4 s, cancel/write failure/unconfirmed EOF и отказ audio/B-frames; старые demux/remux протоколы повторно проверены — [отчёт](ProgressiveContinuousRemuxValidation-2026-11-03.json). Windows startup GLib/GIO измерен отдельно от steady wait, без изменения его CPU-порога. Публичный controller, ResourceSource/reader leases/cache и подключение Qt/Classic остаются следующими обязательными шагами; capability и SDK 1.81 не расширялись. Новый worker не устанавливается, EXE/INI действующих приложений, shell, private pthread DLL и Harvester сохранены; установленный Linux уже соответствует прежнему проверенному main EXE.


4 ноября 2026: к внутреннему continuous worker добавлен общий
[асинхронный управляющий слой](ProgressiveContinuousRemux.md): ResourceSource,
SHA/ranges, immutable publications, heartbeat/writer failures, отдельные
lifetime/pipe deadlines, Windows Job и закрытие source до receipt. По 17 native
сценариев GCC/MinGW/MSVC/ASan-UBSan; найден и исправлен пропуск worker death во
время source wait. [Результаты](ProgressiveContinuousRemuxParentValidation-2026-11-04.json).
Все три плеера пересобраны и проверены через прежний remux API, Linux обновлён
с backup и сохранением профиля. Это внутренняя подготовка: cache/decoder readers,
непрерывный playback bridge и публичная capability/SDK ещё не подключены;
Harvester и Windows установки не изменены.


4 ноября 2026, следующий внутренний этап: [ограниченный выходной cache
continuous-remux](ProgressiveContinuousRemux.md) связан с managed parent.
Полные fMP4 fragments проверяются FragmentIndex и публикуются отдельно от
незавершённых bytes; private disk budget ≤64 MiB, SHA на чтении, независимые
reader leases, original + cross-process output receipt gates перед удалением.
Владелец очереди удерживает до 16 активных/retired store и повторяет cleanup
после закрытия внешних readers без периодических таймеров.
По 31 native сценарию GCC/MinGW/MSVC/ASan-UBSan — [отчёт](ProgressiveContinuousRemuxCacheValidation-2026-11-04.json).
GUI/decoder adapters и публичный API ещё не подключены; orphan recovery и полная
cache/crash matrix открыты. Main EXE всех трёх редакций/установленный Linux EXE
не изменились, новая поставка не создаётся. Harvester не меняется.


4 ноября 2026, journal recovery: [неизменяемый private ownership journal
и startup admission](ProgressiveContinuousRemux.md) проверяют OS owner birth,
locks, file/directory creation identity, тот же registry и живые reader receipts
до удаления cache. Непроверенные остатки сохраняются и блокируют новый budget.
Исправлен live cleanup через подменённый пустой registry; baseline failure сохранён.
Linux GCC/ASan-UBSan: по 33 recovery сценария, Windows MinGW/MSVC: по 31 —
[отчёт](ProgressiveContinuousRemuxRecoveryValidation-2026-11-04.json).
Это внутренний компонент; реальные decoder/API adapters и общая приёмка открыты.
Main EXE, установки, Harvester и SDK/release assets не изменялись.


4 ноября 2026, первые decoder views continuous-remux: общий блокирующий cursor,
внутренний libmpv stream adapter и sample-index view для Classic.
Реальный libmpv декодирует 600 кадров до/после source complete; каждый PNG
совпадает с completed-source reference той же ОС. Есть pause/resume, interrupt,
EOF/producer errors, совместные readers и actual close. По 11 native сценариев
GCC/MinGW/MSVC/ASan-UBSan, повторены 31 cache regression Windows/Linux —
[отчёт](ProgressiveContinuousRemuxDecoderValidation-2026-11-04.json).
Это не установка нового playback: обычные Player/API и реальный Classic
DirectShow graph ещё не подключены, их приёмка остаётся открытой.
Main EXE/профили/Harvester/SDK 1.81 и опубликованные assets не менялись.


4 ноября 2026, внутренние playback entry points continuous-remux подключены к
настоящему Qt Player/MpvLifetime и CHarvesterSession/DirectShow. По пять Qt
сценариев Linux/Windows, пять Classic graph сценариев; 600 NV12 кадров Classic
совпали с независимым FFmpeg original decode, complete seek дал 350 кадров.
Qt сохраняет pause/speed/volume/mute, Stop/replacement/close освобождают readers;
повторены retirement и прежние 24 preparation/20 playback проверки API на ОС —
[отчёт](ProgressiveContinuousRemuxPlaybackValidation-2026-11-04.json).
Все main EXE пересобраны; Linux обновлён атомарно с backup, INI/263 файла сохранены.
HKCU Classic commands/icons неизменны. Публичный continuous controller/capability
и запуск из Harvester ещё не подключены; Windows установки/SDK 1.81 не менялись.


4 ноября 2026, общий внутренний `ContinuousRemuxController`: async preparation,
accepted/applied updates и heartbeat, strict job/session/writer fences,
requestId replay, bounded queue и off-caller shutdown. Source/cache owner
удерживается до close output readers; один service fence используется successive
jobs. По 17 native сценариев GCC/MinGW/MSVC/ASan-UBSan, cache/recovery regressions —
[отчёт](ProgressiveContinuousRemuxControllerValidation-2026-11-04.json).
Это общий компонент для API adapters; ordinary endpoints и public capabilities
ещё не подключены. Main EXE, установки, INI, Harvester и SDK 1.81 не менялись.


4 ноября 2026, внутренние команды continuous-remux подключены к обычному IPC
Qt обеих ОС и Classic: шесть команд подготовки, без playback commit и без
изменения advertised capabilities/SDK. По шесть сценариев через настоящие
endpoints каждой редакции и прежние 24 preparation/20 playback проверки.
Исправлена гонка heartbeat с producer EOF; по 18 native сценариев
GCC/MinGW/MSVC/ASan-UBSan, включая 12 completion cycles —
[отчёт](ProgressiveContinuousRemuxApiValidation-2026-11-04.json).
Все три main EXE пересобраны; Linux обновлён атомарно с backup, INI/264 других
файла сохранены. Windows-установки, 158 shell entries, SDK ZIP и Harvester не
менялись; одноразовые задачи/копии Classic удалены. Worker остаётся тестовым.
Следующий шаг — playback commit/control/status, intent fencing и проверки
обычного API всех трёх редакций, затем capability/SDK и поставка worker.


4 ноября 2026, внутренний continuous-remux playback API: Play/Status/Control в
Qt обеих ОС и Classic, уникальная попытка просмотра, replay/fences, pause/append,
complete-only seek, EOF и освобождение при Stop/замене/закрытии. По 14 IPC
сценариев каждой Qt ОС и 15 Classic; две Qt Window/API lifecycle проверки на ОС,
11 native decoder ASan/UBSan. Исправлены Qt seek по короткому prefix duration и
отмена текущего просмотра до готовности замены; сохранены before-fix failures —
[отчёт](ProgressiveContinuousRemuxPlaybackApiValidation-2026-11-04.json).
Classic normal close дал exit 0 и files-closed receipts, кэш был очищен до
повторного запуска профиля. Прежние 6 preparation/24 bounded preparation/20
playback и 18 controller/retirement проверки прошли. Все main EXE пересобраны,
Linux обновлён атомарно с backup без изменения INI/265 других файлов;
Windows-установки, 158 shell entries, SDK ZIP и Harvester сохранены. Четыре
одноразовые задачи/копии Classic удалены. Публичные capability/SDK, поставка
worker, continuous readerRelease adapter и полная waiting/replay/crash/stress
matrix остаются открытыми; audio/B-frames этим не добавлены.
