# SDK предпросмотра загрузки: Linux Qt, Windows Qt и Classic

SDK является частью FoxicMP и не меняет YouTubeHarvester. Python 3.10+,
только стандартная библиотека. Обе Qt-редакции устанавливают файлы в
`integration/`; независимый ZIP для всех редакций создаётся в репозитории командой:

```sh
python3 tools/package_progressive_sdk.py --output /absolute/new/FoxicMP-progressive-sdk.zip
```

В ZIP входят семь Python-модулей, эта инструкция, контракты v1 и draft v2,
пример метаданных v2, отчёты, лицензия и JSON с исходным commit, dirty-флагом и
SHA-256 файлов. Воспроизведение v1 сохраняется; SDK 1.6 добавляет
[ограниченный медиапрофиль v2](ProgressiveMediaV2.md): две отдельные fMP4 H.264/AAC-LC дорожки. [Расширенное ТЗ](ProgressiveIntegrationPlan.md)
ещё не выполнено. SDK не скачивает URL и не выполняет remux.

Проверки трёх редакций и ограничения: [отчёт](ProgressiveSdkValidation-2026-10-25.md).
Этот SDK требует новые сборки плеера с `releaseAcknowledgement=true`;
опубликованные установщики 0.2.0 не содержат это расширение.

SDK 1.30: [контрольные суммы подтверждённых блоков](ProgressiveIntegrity.md), helper `progressive_integrity.py`, согласование capability и прежняя изоляция optional audio. Контрольные суммы необязательны; без них гарантия проверки каждого блока не заявляется.

SDK 1.32: [staging и индекс одного сегмента](ProgressiveSegmentStaging.md), `ResourceProbe.start_segment`, capability `segmentProbeVersion=1`. Это диагностический API, без запуска segment playback.

SDK 1.33: [реальное воспроизведение цепочки fMP4 H.264/AAC-LC](ProgressiveSegmentPlayback.md), capability `schema2Playback.segmentedPlayback`. Требуются новые сборки; сегменты должны быть полностью опубликованы по отдельности, весь ролик может оставаться недокачанным.

SDK 1.34: [HEVC Main/Main10 в сегментном fMP4](ProgressiveSegmentHevc.md), capability `segmentedPlayback.profiles` и предварительное согласование HEVC в Python helper. Диагностика одного сегмента не расширяется этим профилем.

SDK 1.35: [AV1 Main 8/10-bit в сегментном fMP4](ProgressiveSegmentAv1.md), отдельная capability профиля и согласование AV1 до start; H.264/HEVC остаются совместимыми.

SDK 1.36: [сегментные VP9 8/10-bit и Opus mono/stereo](ProgressiveSegmentVp9Opus.md), согласование точной пары видео/аудио; прежние профили с AAC сохраняются.

SDK 1.38: [сегменты WebM VP8/VP9/AV1 с Opus](ProgressiveWebmSegments.md), отдельные init/Cluster и capability `segmentedPlayback.webm`; каталог согласуется как `paged-webm-v1`. Сохранены fMP4 и scalar WebM профили. [Результаты трёх редакций](ProgressiveWebmSegmentsValidation-2026-10-30.md). SDK не меняет Harvester и не содержит EXE.

SDK 1.37: [постраничный каталог fMP4](ProgressiveCatalogPlayback.md) для трёх редакций — отдельная capability `segmentedPlayback.catalog`, до 8192 metadata-ресурсов, восемь открытых медиафайлов и обязательный SHA-256. Проверены playback/seek/reopen 182–183 ресурсов; многочасовая приёмка остаётся отдельной. Для каталога используйте `probe` с `sessionDirectory`, не чистый inline `validate`. Храните все опубликованные файлы до release главного reader ID. Требуются новые сборки приложений; старые EXE не обновляются Python SDK.

## Проверка расширенной схемы (SDK 1.3)

Новые сборки отдельно объявляют `validationSchemaVersions:[2]` и
`playbackSchemaVersions:[1,2]` (с SDK 1.6 — только явно объявленный профиль v2). [Контракт v2](ProgressiveManifestV2.md) описывает
ресурсы, подтверждённые диапазоны, сегменты, несколько дорожек и поздние главы.
Эта часть SDK проверяет метаданные, не открывая медиа и не прерывая просмотр:

```python
from progressive_schema import unsigned64, signed64

snapshot_v2['revision'] = unsigned64(revision, positive=True)
snapshot_v2['timeline']['timestampOffset'] = signed64(offset_ticks)
reply = client.validate_session(snapshot_v2, previous=previous_snapshot_or_none)
assert reply['validated'] and reply['playbackSupported'] is False
```

Весь JSON-запрос, включая previous, ограничен 65536 байтами. Большие целые —
десятичные строки; float/bool и округление не допускаются. `progressive-v2-vod.json`
в ZIP — синтетический пример метаданных, без соответствующего media-файла.
`PreviewSession.publish()` и writer-стенд по-прежнему используют v1. `start`/`probe`
v2 с SDK 1.6 поддерживают [отдельный fMP4-профиль](ProgressiveMediaV2.md); остальные
профили отклоняются с unsupported_media. Полная codec/live/remux-матрица ещё не интегрирована.
[Проверки модели](ProgressiveModelValidation-2026-10-25.md) не являются сквозным
тестом Harvester; исходники Harvester не изменялись.

## Фактическое применение metadata (SDK 1.27)

[Главы и title](ProgressiveMetadata.md) теперь проверяются в видимом UI, включая
пустой список глав, отсутствие title и второй сеанс с metadata revision 1.
Status.appliedMetadata позволяет отличить принятую revision от применённой
интерфейсом. Этот этап исправляет основу metadata SDK 1.26.

## Частичная дополнительная аудиодорожка (SDK 1.28)

[Выбранная partial audio](ProgressivePartialAudio.md) доступна после проверки
initialization и первых пяти секунд индекса. Невыбранный короткий префикс не
тормозит основной просмотр; выбранный ограничивает доступный буфер. Возврат на
основную дорожку работает и во время ожидания. Условия ошибок этого профиля расширены в SDK 1.29. Требуется новая сборка каждой редакции.

## Ошибки активной audio (SDK 1.29)

[Обнаруженная ошибка optional audio](ProgressiveActiveAudioErrors.md) остаётся
ошибкой дорожки. При необходимости плеер возвращается на required primary audio,
сохраняя паузу, позицию, субтитры и reader. Повторный выбор запрещён до нового
сеанса. Ошибки required A/V и корневого writer остаются общими. Это расширение
не выполняет полное хеширование пакетов.

## Ошибки optional SRT (SDK 1.24)

[Изоляция SRT](ProgressiveOptionalSrt.md) применяется в Classic и обеих Qt-версиях.
Некорректный SRT не мешает start/update/complete и обязательному A/V, а ошибка
сообщается в subtitleTracks. Уже проверенная копия сохраняется при повреждении
оригинала. `ready` описывает её подготовку, `loaded` — подключение backend;
backendErrorCode/backendError сообщают отдельный отказ декодера. Неизменяемость
снимка и ошибки required audio/video остаются строгими. Проверять capabilities
subtitles.optionalErrorIsolation и retainsValidatedCopyOnSourceError у EXE.
[Проверки трёх редакций](ProgressiveOptionalSrtValidation-2026-10-29.md).

## Несколько аудио и поздние SRT (SDK 1.22)

[Медиапрофиль](ProgressiveMultiTrack.md) допускает один video и до восьми required
audio в отдельных ресурсах. До четырёх optional UTF-8 SRT подключаются после
полного завершения, включая появление файла во время просмотра. Старый v1 и
двухдорожечные v2 профили сохранены. Проверять capabilities текущего EXE;
SDK 1.23 добавляет [подтверждённую байтовую перемотку Qt](ProgressiveSeek.md):
предел 10 секунд снят, доступна подтверждённая часть всех required A/V.
`qtMultiAudioSeek.mode:confirmed-byte-seek` и `confirmedRangesOnly:true` отличают
новый EXE. После готовности A/V доступна вся длительность. Classic сохраняет
свой native индекс и не меняется в этом этапе.
Harvester не изменён. [Проверки multi-audio трёх редакций SDK 1.22](ProgressiveMultiTrackValidation-2026-10-28.md)
и [перемотки Qt SDK 1.23](ProgressiveSeekValidation-2026-10-28.md).

## VP8 и Vorbis (SDK 1.21)

[VP8 8-bit и Vorbis I mono/stereo 48 kHz в WebM](ProgressiveVp8Vorbis.md)
добавлены во все три редакции, включая пары с прежними VP9/AV1 и смешанные
fMP4/WebM. Проверять профили текущего EXE и `webm.videoCodecs/audioCodecs`.
Vorbis ограничен floor 1 и bounded setup; Ogg, произвольные режимы и muxed/remux
не добавлены. Python API сохраняется.

## Смешанные MP4/WebM-дорожки (SDK 1.20)

[Независимый контейнер каждой дорожки](ProgressiveMixedContainers.md):
fMP4-видео H.264/HEVC/AV1/VP9 + WebM Opus; WebM-видео VP9/AV1 + fMP4 AAC-LC/Opus.
Проверять `mixedContainers` и конкретный профиль в `schema2Playback.profiles`.
В `status` добавлены `videoContainer`/`audioContainer`; Python интерфейс прежний.
Сохранены ограничения кодеков, VOD и раздельных ресурсов; muxed/remux и live не добавлены.

## Progressive AV1 в WebM (SDK 1.19)

[AV1 Main 8/10-bit + Opus в отдельных WebM](ProgressiveWebMAv1.md):
профиль `separate-webm-av1-opus`. Требуется CodecPrivate с Sequence Header OBU
и первый ключевой пакет с совпадающим sequence header. Python API сохранён;
смешанные MP4/WebM-пары добавлены в SDK 1.20, остальные границы ТЗ сохраняются.

## Progressive WebM (SDK 1.18)

[Отдельные WebM VP9 8/10-bit и Opus mono/stereo](ProgressiveWebM.md):
`containerHint: "webm"`, профиль `separate-webm-vp9-opus`. Нужны известные
размеры кластеров, DefaultDuration видео и согласованные CodecDelay/PreSkip.
Python интерфейс сохранён. Поддержка остальных WebM/MKV-вариантов, muxed/remux,
live и реальная интеграция Harvester этим этапом не заявляются.

## Progressive Opus (SDK 1.17)

[Opus mono/stereo + H.264/HEVC/AV1/VP9 в отдельных fMP4](ProgressiveOpus.md)
добавлен в schema 2 трёх редакций. Нужны mapping family 0 и согласованный
pre-skip/edit; проверять schema2Playback.profiles/audioCodecs текущего EXE.
Python интерфейс прежний; WebM/MKV, multichannel, remux/live и metadata остаются
отдельными этапами. Установщики 0.2.0 этим SDK не обновляются.

## Progressive VP9 (SDK 1.16)

[VP9 Profile 0/2, 8/10-bit + AAC-LC fMP4](ProgressiveVp9.md) добавлен в schema 2
трёх редакций. Проверять schema2Playback.profiles/videoCodecs текущего EXE.
Python интерфейс прежний; WebM/MKV, Opus, remux/live и metadata остаются
отдельными этапами. Готовые установщики 0.2.0 этим SDK не обновляются.

## Progressive AV1 (SDK 1.15)

[AV1 Main 8/10-bit + AAC-LC fMP4](ProgressiveAv1.md) расширяет schema 2 трёх
редакций. Требуется Sequence Header OBU в av1C. Python интерфейс прежний;
проверять schema2Playback.profiles/videoCodecs текущего EXE. Opus, WebM/MKV,
remux/live и metadata остаются отдельными этапами.

## Progressive HEVC (SDK 1.14)

[Медиапрофиль HEVC Main/Main10 + AAC-LC](ProgressiveHevc.md) добавлен в schema 2
трёх редакций. Это две отдельные fMP4-дорожки; H.264 и публичный Python интерфейс
сохранены. Проверять schema2Playback.profiles/videoCodecs текущего EXE.
WebM/MKV, другие кодеки, muxed/remux/live и metadata остаются отдельными этапами.

## Progressive Classic close (SDK 1.13)

[Закрытие progressive-графа на STA](ProgressiveClassicClose.md) возвращает UI
управление без ожидания графового потока. Новые start и выход откладываются до
настоящего close event. Stop отменяет ожидающий start; reader release по-прежнему
требует физического закрытия файлов. Capability `progressiveStaCloseAsync` не
распространяется на UI-owned графы и обычные источники. Qt-код в этом этапе не менялся.

## Decoder shutdown (SDK 1.12)

[Qt native lifetime / Classic graph wait](ProgressiveEngineShutdown.md) различаются:
Qt объявляет `engineRetirement`, Classic — `graphShutdown` с nativeTeardownDetached=false.
Source reader release остаётся отдельным подтверждением фактического закрытия файлов.
SDK ZIP не обновляет EXE/установщики.

## Legacy maintenance (SDK 1.11)

[Файловые проверки и индексация legacy](ProgressiveLegacyMaintenance.md) выполняются
без metadata mutex UI. `legacyRefresh` описывает свойства maintenance-пути; это
не общая гарантия отсутствия ожиданий при закрытии графа/renderer.
Новая revision и доступная граница становятся видимыми вместе после подготовки.
Проверка усечения не требует увеличения revision.

## Legacy/provider opening (SDK 1.10)

[CLI-подготовка legacy-источника вынесена из UI](ProgressiveLegacyOpening.md) во всех
трёх редакциях. `legacyPreparation.offUi:true`, один слот, deadline 5000 мс;
Stop/Close/замена запрещают поздний запуск. `legacyPreparationPending` в runtime
capabilities показывает занятый слот, включая отменённое ожидание native I/O.
Это не release receipt. SDK ZIP по-прежнему не заменяет EXE плеера.

## Реестр и освобождение источников (SDK 1.9)

[Четыре reader-команды и уничтожение Source выполняются вне UI](ProgressiveRetirement.md).
Capabilities: `readerRegistryOffUi:true`, `sourceRetirement.maxSources:32`,
`sourceRetirement.workerLimit:2`. При `source_limit` дождаться подтверждённого
освобождения. Python-клиент повторяет `reader_registry_busy` только для reader-команд
в пределах общего deadline; положительный release по таймеру не подставляется.
[Проверки трёх сборок](ProgressiveRetirementValidation-2026-10-26.md).

## Публикация обновлений (SDK 1.8)

Первичные probe/start и последующие update/complete через IPC выполняются в worker.
[Порядок ревизий, разрыв соединения, Stop/Close и ограничения](ProgressivePublication.md).
API и Python-вызовы прежние. После потерянного ответа update проверять status;
разрыв соединения не откатывает уже начатую публикацию.

## Подготовка источника (SDK 1.7)

В новых сборках первичное открытие/индексация probe/start через IPC выполняются
вне UI. Формат ответа прежний; Stop/Close отменяют применение позднего результата.
[Capabilities, таймауты, recovery и границы](ProgressivePreparation.md).
Новые Python-команды не нужны. При TimeoutError сохранять файлы и искать readers.

## Медиапрофиль v2 (SDK 1.6)

Новый `progressive_v2.V2PreviewSession` принимает полные снимки v2 и напрямую
запускает две H.264/AAC-LC fMP4-дорожки через ResourceSource.
[Точный пример, capabilities, ограничения и release](ProgressiveMediaV2.md).
Старый PreviewSession и writer-стенд сохраняют v1.

## Подключение к событиям writer

SDK 1.5 добавляет отдельный [ResourceProbe](ProgressiveResourceIo.md): асинхронное
чтение подтверждённых диапазонов v2, обновления, ожидание и отмена без запуска
воспроизведения. Старый PreviewSession продолжает использовать media-схему v1.
Наличие resourceIoProbeVersion не означает поддержку decoder/remux v2.

Добавьте каталог SDK в путь импорта Python. Укажите EXE своей редакции:
`foxicmp-qt`, `FoxicMPq.exe` или `FoxicMP64.exe`. Клиент сам выберет транспорт.
Новые интеграции используют player-owned API и не запускают старый provider.

```python
from progressive_client import ProgressiveClient
from progressive_sdk import PreviewSession

client = ProgressiveClient(player_executable, profile_directory)  # profile optional
client.ensure_running()  # Discovery/readiness не открывает видео.
preview = PreviewSession(client, session_directory, title=title, activate=False)

# В worker загрузчика, после flush/записи доступных байтов:
reply = preview.publish(full_v1_snapshot)
# При отсутствии новых байтов живой writer вызывает это каждые <= 5 секунд:
preview.heartbeat()
# Когда обе дорожки полностью записаны, закончены, и ревизия увеличена:
preview.publish(final_snapshot)  # state=complete у сеанса и обеих дорожек
# Если writer завершился с ошибкой:
# preview.writer_error("Ошибка записи локальной дорожки")

# Stop/закрытие предпросмотра, а не отмена скачивания:
receipt = preview.close(timeout=2)
if receipt.get("released") is True:
    pass  # Этот читатель больше не удерживает исходники.
else:
    pass  # Сохранить исходники; позднее повторить close/status.
```

`publish` получает полный снимок со строго возрастающей ревизией. Только реальные
записанные префиксы; сетевой счётчик и размер sparse-файла не являются подтверждением.
Идентификатор и generation неизменны на срок объекта SDK. Другому поколению нужен
новый объект и явный запуск. Пока нет инициализации, SDK возвращает
`waiting-initialization`; затем использует настоящий `probe` плеера. Отказ probe
не маскируется успехом; после новых данных можно опубликовать следующую ревизию.

Вызовы сериализованы, но синхронные: выполнять их в worker, не в GUI-потоке Harvester.
Скрытых потоков и таймеров SDK нет. Ошибка предпросмотра не посылает загрузчику
команду отмены. После `close` дальнейшие события не перезапускают просмотр.
В диагностические сообщения не передавать cookies, подписанные URL и ключи.

## Подтверждение освобождения

SDK 1.4 в новых сборках дополнительно использует [reader leases](ProgressiveReaderLeases.md):
readerId привязан к реальным файлам и OS process identity, сохраняется квитанция
для recovery после crash. `PreviewSession.reader_id` доступен после tracked start;
close использует именно его, подтверждает/забывает квитанцию и кэширует результат.
Ручные методы: `reader_status`, `release_reader`, `forget_reader`, `readers`.
Потерянный ответ start остаётся неопределённым: сохранить файлы, затем обнаружить
читателей по исходным sessionId/generation. [Результаты](ProgressiveReaderValidation-2026-10-25.md).

Новые сборки объявляют `releaseAcknowledgement=true` и действие `release` в
capabilities. `release` прерывает только reader текущего sessionId. `status` и
ответы команд содержат `released`: true ставится после завершения деструктора
источника и закрытия всех медиа-дескрипторов, а не просто при `active=false`.
Повторные release/status после естественного конца безопасны.

`PreviewSession` проверяет эту возможность до старта; старую сборку без неё надо
обновить. Таймаут, отсутствие ответа, неизвестный сеанс и смерть одного из
процессов сами по себе не разрешают удаление. При нескольких окнах/профилях
загрузчик должен получить подтверждения от всех открытых им читателей.
В SDK 1.4 opt-in registry существует для player-owned v1. Для legacy/provider
и динамических ресурсов v2 его наличие не объявляется. Неизвестная/забытая квитанция
и пустой список не заменяют положительное подтверждение конкретных читателей.

## Локальный writer-стенд

В FoxicMP сгенерируйте синтетические fixtures штатным
`qt/tests/generate_media.py`. Укажите отдельную тестовую копию Classic с
отключённой регистрацией контекстного меню либо отдельный профиль Qt.
Каталог вывода должен ещё не существовать:

```sh
python3 progressive_writer.py --player /absolute/foxicmp-qt --profile /absolute/test-profile \
  --ensure --video /absolute/fixtures/progressive/video.mp4 \
  --audio /absolute/fixtures/progressive/audio.m4a --directory /absolute/new-session
```

Стенд читает каждый из двух локальных входов один раз, дописывает выходы целыми
fMP4-фрагментами с fsync и передаёт события через SDK. Он удерживает complete,
проверяет позицию >= 10 секунд до complete, ждёт естественный конец и
`released=true`. JSON-отчёт сохраняется в `report.json`; входы не изменяются,
выходы автоматически не удаляются. Это offline-проверка контракта, не сквозная
проверка реального Harvester и не измерение физической A/V точности.

## Старый manifest/provider и Windows Qt

Совместимый вход Qt сохраняется:

```text
FoxicMPq.exe --harvester-session C:\download\session.json
foxicmp-qt --harvester-session /absolute/session.json
FoxicMP64.exe /harvester-session C:\download\session.json
```

Для этого входа нужен действующий старый provider; один snapshot не заменяет его.
Windows Qt теперь подключается к `FoxicMP.Harvester.<sessionId>`, сверяет аккаунт
сервера, отправляет attached с реальными PID и readerId, проверяет сохранение lease
и только затем открывает дорожки. Состояние JSON и heartbeat обрабатываются
продолжающимся worker, включая complete/failed и новые префиксы. Закрытие файлов
предшествует released старому provider. Внешний Python-процесс не выдаёт себя за
PID плеера. Разрыв соединения не трактуется как успешный EOF и не снимает lease
до подтверждения provider либо смерти процесса.

При разрыве Windows Qt ставит незавершённый просмотр в ожидание и пытается
переподключиться с новой проверкой аккаунта и сохранённого lease. Смена
sessionId/generation либо ошибка writer не маскируются как восстановление.
Первичное получение lease синхронно ограничено двумя секундами; полное вынесение
probe/подготовки источника из UI остаётся работой следующего этапа.

Исходный Windows provider хранит lease по PID, поэтому Qt запрещает одновременно
открывать два legacy-reader одного sessionId в одном процессе. Новая интеграция
не должна использовать этот старый маршрут: единый клиент и события выше заменяют
manifest/provider для всех трёх редакций без Win32-кода в Harvester.

SDK 1.39: [сегментный MPEG-TS AVC/AAC](ProgressiveTsPlayback.md), общий виртуальный fMP4 без перекодирования, обязательный SHA-256 и отдельная capability каталога `paged-mpegts-v1`. Для playback требуются новые EXE всех трёх редакций; SDK содержит клиент и документацию, Harvester не меняется.

SDK 1.40: [совместные MPEG-TS H.264/AAC сегменты](ProgressiveMuxedTsPlayback.md), два logical view над одним resource/init, общий SHA/cache/leases, отдельный каталог `paged-muxed-mpegts-v1`. Это профиль полных сегментов; один растущий файл и muxed fMP4/WebM остаются последующими этапами.

SDK 1.41: [один растущий MPEG-TS AVC/AAC файл](ProgressiveGrowingTs.md),
`containerHint: mpegts-growing`, два required scalar view над одним resource.
Согласование через `schema2Playback.segmentedPlayback.mpegts.growing`;
`V2PreviewSession` отклоняет старый плеер до start. Неполные packet/PES/GOP
ждут publication, SHA-256 обязателен, decoder получает virtual fMP4 без
перекодирования и промежуточного файла. Пример начального manifest —
`progressive-v2-growing-ts-vod.json`: заменить IDs, duration и timestampOffset
на значения своего VOD, записать bytes и digest, затем включить readiness.
Отдельные установленные Windows-приложения нужно обновлять из соответствующей
сборки; один только ZIP SDK не меняет возможности ранее установленного EXE.
