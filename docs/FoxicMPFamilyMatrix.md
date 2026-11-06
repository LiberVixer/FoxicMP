# FoxicMP: сравнение трёх редакций

Срез исходников и проверок: 25 октября 2026 года, Qt 0.2.0.
Эталон организации меню и настроек — **Linux Qt**, включая перенос в Classic.
Основа дальнейшей работы: [общий план](FoxicMPFamilyPlan.md).

## Что именно сравнивается

| Редакция | Движок | Проверенная сборка выпуска 0.2.0 | Отдельный профиль |
|---|---|---|---|
| FoxicMP Classic, Windows | MFC / DirectShow, код MPC-BE | 1.9.1.75 build 261025-1303 | `C:\FoxBroxer\Prg\FoxicMP` |
| FoxiqMP qt, Windows | Qt Widgets / libmpv | 0.2.0 (сборка проверена) | `C:\FoxBroxer\Prg\FoxicMPq` |
| FoxiqMP qt, Linux | Qt Widgets / libmpv | 0.2.0 (сборка проверена) | `_bin/FoxicMPQt-linux-x64` |

Версии выпуска проверены в сборочных/portable-копиях Windows через SSH;
Linux — через `--version`. Это версии пакетов, а не утверждение об обновлении
Windows-установок. Linux-копия обновлена до 0.2.0 с сохранением INI.
Профили и системные настройки Windows при выпуске не менялись.
Classic восстановлен после экспериментальной семейной синхронизации; текущий
профиль не менялся. Уточнение пользователя: в Classic переносить только порядок
меню/настроек, сохраняя собственные переводы, страницы, defaults и управление.
Дополнительно исправлены 79 команд контекстного запуска, ошибочно ведущих
к тестовой копии; реальный ролик с 14 главами и сохранение размера проверены.
См. [отчёт восстановления](FoxicMPClassicRestore-2026-10-24.md).

**Есть** означает подключённую функцию в исходниках приложения. Это не новая
проверка всех её сценариев на трёх ОС. **Частично** означает конкретно описанную
границу. **Нет** относится к названной функции/адаптеру, а не возможностям ОС.
Физический звук, GPU/HDR и оборудование не выводятся из наличия галочки.

## Источники и воспроизведение

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Локальные видео/аудио, папки, drag-and-drop | Есть | Есть | Есть | Один набор образцов и одинаковое поведение очереди |
| MP4/MKV/AVI/MOV/WebM, распространённые кодеки | Есть, свои декодеры/фильтры | Есть, libmpv/FFmpeg | Есть, libmpv/FFmpeg | Проверять фактические дорожки и вывод; не обещать все кодеки по расширению |
| HTTP(S), сетевые медиапотоки | Есть | Есть | Есть | Одинаковые ошибки, таймауты и правила повторного открытия |
| Страницы онлайн-видео и выбор качества | YouTube / YT_DLP, собственные настройки | Общие Qt / yt-dlp | Общие Qt / yt-dlp | Одни публичные сценарии YouTube/Rutube/VK, качество, звук и отмена |
| Онлайн-плейлисты | Есть | Есть | Есть | Лимиты, порядок, добавление и пропуск ошибок |
| Воспроизведение во время скачивания | Named-pipe v1 и подтверждение release | Named-pipe v1, native legacy/provider, подтверждение release | Provider, Unix v1, подтверждение release | Общий SDK/writer; две MP4/M4A H.264/AAC дорожки. Остальная матрица [нового ТЗ](ProgressiveIntegrationPlan.md) не выполнена; Harvester не меняется |
| Один контракт вызова загрузчиком | Есть v1 Windows named-pipe адаптер | Есть v1 Windows named-pipe API | Есть v1 Unix API | Общие JSON-команды и совместимые IPC-адаптеры |
| Проверка расширенного manifest v2 | Общий validate, без открытия | Общий validate, без открытия | Общий validate, без открытия | [Модель и границы](ProgressiveManifestV2.md), [результаты](ProgressiveModelValidation-2026-10-25.md). Есть ограниченный playback v2 H.264/AAC-LC fMP4; расширенные track/live scopes не подключены |
| Учёт reader по закреплённым файлам | Opt-in player-owned v1, IPC и OS receipts | Тот же runtime | Тот же runtime | [Reader leases](ProgressiveReaderLeases.md), [проверки](ProgressiveReaderValidation-2026-10-25.md); ресурсы v2 и legacy/provider не объявляются этим покрытыми |
| Подтверждённые диапазоны v2, I/O-слой | Общий ResourceSource и resourceProbe | Тот же код | Тот же код | [Контракт](ProgressiveResourceIo.md), [проверки](ProgressiveResourceValidation-2026-10-26.md). Leases ресурсов и байтовое чтение; отдельно подключён [fMP4 playback v2](ProgressiveMediaV2.md), универсальный remux отсутствует |
| Медиапрофиль v2: отдельные fMP4 H.264/AAC-LC | ResourceSource → DirectShow MP4 | ResourceSource → libmpv | Тот же Qt-код | [Профиль и границы](ProgressiveMediaV2.md), [сквозные проверки](ProgressiveMediaValidation-2026-10-26.md); не вся матрица ТЗ |
| Первичная подготовка progressive API вне UI | Worker → UI commit | Worker → UI commit | Тот же Qt-код | [SDK 1.7](ProgressivePreparation.md): probe/start, отмена позднего результата; [SDK 1.8](ProgressivePublication.md) добавляет update/complete и отделяет I/O от UI metadata mutex |
| Реестр progressive readers и уничтожение Source вне UI | IPC worker + FIFO cleanup | IPC worker + FIFO cleanup | Тот же Qt-код | [SDK 1.9](ProgressiveRetirement.md): 32 Source, ≤2 cleanup-workers; release только после закрытия файлов; renderer/graph shutdown отдельно |
| Открытие legacy/provider источника вне UI | Worker → UI graph | Worker → UI mpv | Тот же Qt-код | [SDK 1.10](ProgressiveLegacyOpening.md): один слот, 5 с до отмены применения; native I/O не прерывается принудительно, старые refresh/notify locks отдельно |
| Legacy refresh без файлового I/O под UI metadata mutex | Native checks/index → commit | Worker checks/publish → commit | Worker checks/index → commit | [SDK 1.11](ProgressiveLegacyMaintenance.md): проверка усечения и при неизменной revision; Stop во время initial buffering Classic доступен |
| Progressive HEVC Main/Main10 + AAC-LC | Schema-2 fMP4 через MP4Splitter | Schema-2 fMP4 через libmpv | Тот же Qt-код | [SDK 1.14](ProgressiveHevc.md): hvc1/hev1, отдельные ресурсы, подтверждённый префикс; remux/WebM/другие кодеки отдельно |
| Progressive AV1 Main 8/10-bit + AAC-LC | Schema-2 fMP4 через MP4Splitter | Schema-2 fMP4 через libmpv | Тот же Qt-код | [SDK 1.15](ProgressiveAv1.md): av01, Sequence Header в av1C; WebM/Opus/remux отдельно |
| Progressive VP9 Profile 0/2 + AAC-LC | Schema-2 fMP4 через MP4Splitter | Schema-2 fMP4 через libmpv | Тот же Qt-код | [SDK 1.16](ProgressiveVp9.md): vp09, 8/10-bit 4:2:0; WebM/Opus/remux отдельно |
| Progressive Opus mono/stereo + H.264/HEVC/AV1/VP9 | Schema-2 fMP4, 80 мс pre-roll | Schema-2 fMP4 через libmpv | Тот же Qt-код | [SDK 1.17](ProgressiveOpus.md): dOps/mapping 0, согласованный pre-skip/edit; WebM отдельно |
| Progressive WebM VP9 8/10-bit + Opus mono/stereo | Schema-2, MatroskaSplitter и подтверждённый индекс | Schema-2, ResourceSource/libmpv | Тот же Qt-код | [Профиль SDK 1.18](ProgressiveWebM.md): отдельные дорожки, известные размеры Cluster, без lacing/live/remux |
| Progressive WebM AV1 Main 8/10-bit + Opus | Schema-2, штатный AV1/Matroska DirectShow тракт | Schema-2, общий ResourceSource/libmpv | Тот же Qt-код | [SDK 1.19](ProgressiveWebMAv1.md): CodecPrivate/Sequence Header, начальный keyframe, отдельные дорожки |
| Progressive mixed fMP4/WebM | Schema-2, контейнер и splitter каждой дорожки отдельно | Schema-2, отдельные индексы, lavf header probe | Тот же Qt-код | [SDK 1.20](ProgressiveMixedContainers.md): MP4-видео + WebM Opus и WebM-видео + MP4 AAC-LC/Opus; [34 варианта × 3 редакции, exit 0](ProgressiveMixedValidation-2026-10-28.md) |
| Progressive VP8/Vorbis WebM | Schema-2, штатный VP8/Vorbis Matroska/DirectShow | Schema-2, общий WebM index/ResourceSource/libmpv | Тот же Qt-код | [SDK 1.21](ProgressiveVp8Vorbis.md): VP8 8-bit; Vorbis mono/stereo 48 kHz, floor 1 |
| Progressive несколько аудио + SRT | Отдельные splitters → AudioSwitcher; поздний SRT в native renderer | ResourceSource → внешние audio/sub libmpv | Тот же Qt-код | [SDK 1.22](ProgressiveMultiTrack.md): 1–8 required audio, до 4 optional полных UTF-8 SRT; optional audio — отдельный профиль ниже; без live/embedded subtitles |
| Progressive optional audio | Required video/audio запускаются без ожидания optional; проверенная late-дорожка подключается отдельно, выбранная частичная учитывается в буфере | ResourceSource + late audio-add, обнаруженные ошибки optional изолированы до и после подключения | Тот же Qt-код | [SDK 1.28](ProgressivePartialAudio.md): выбранный частичный audio ограничивает буфер; возврат во время ожидания. [SDK 1.29](ProgressiveActiveAudioErrors.md) изолирует обнаруженные ошибки активной дорожки; SDK 1.30 проверяет SHA-256 явно защищённых блоков |
| SHA-256 подтверждённых блоков schema 2 | Общий ResourceSource, проверка перед чтением | Тот же I/O-контракт | Тот же I/O-контракт | [SDK 1.30](ProgressiveIntegrity.md): требуется `resources[].integrity`; старые ресурсы без digest сохраняют прежнюю проверку. Inline JSON ограничен 64 KiB; [постраничный индекс](ProgressiveCatalogPlayback.md) подключён отдельно в SDK 1.37 |
| Воспроизведение сегментов schema 2 | Виртуальный fMP4 H.264/HEVC/AV1/VP9 + AAC-LC/Opus | Тот же общий поток + libmpv | Тот же общий поток + libmpv | [SDK 1.33](ProgressiveSegmentPlayback.md) + [HEVC Main/Main10 SDK 1.34](ProgressiveSegmentHevc.md) + [AV1 Main 8/10-bit SDK 1.35](ProgressiveSegmentAv1.md) + [VP9/Opus SDK 1.36](ProgressiveSegmentVp9Opus.md): VOD, полные отдельные сегменты, постоянный init/codec; без TS/live |
| Сегменты WebM VP8/VP9/AV1 + Opus | Общий whole-Cluster bridge + MatroskaSplitter | Тот же bridge + libmpv | Тот же Qt-код | [SDK 1.38](ProgressiveWebmSegments.md): отдельный init, VOD, неизменная конфигурация, inline или `paged-webm-v1`; segmented Vorbis/TS/live остаются вне профиля |
| Подготовка транспорта MPEG-TS | Общий C++17 PAT/PMT/PES parser, MSVC; используется TS-адаптером | Тот же parser, MinGW; используется TS-адаптером | Тот же parser, GCC и sanitizers; используется TS-адаптером | [Внутренний этап после SDK 1.38](ProgressiveTsTransport.md): parser, codec/index и ResourceSource подготовлены; playback подключён в SDK 1.39 |
| Подготовка TS AVC/AAC clock/index | Общий компонент, MSVC; используется TS → fMP4 адаптером | Тот же компонент, MinGW; используется TS → fMP4 адаптером | Тот же компонент, GCC/ASan; используется TS → fMP4 адаптером | [Framing и индекс](ProgressiveTsAccessIndex.md): fixed-rate AVC, ADTS AAC, IDR/audio seek и bounded readiness; TS playback capability — SDK 1.39 |
| TS ResourceSource/staging | Общий компонент проверен MSVC, подключён через виртуальный MP4 | Тот же компонент, MinGW; используется TS → fMP4 адаптером | Тот же компонент, GCC/ASan; используется TS → fMP4 адаптером | [Виртуальный поток](ProgressiveTsResourceStream.md): prefix/gap/EOF, SHA/leases, 8 открытых файлов, каталог 242 ресурсов; sample hashes и внешний FFmpeg совпадают; Playback capability — SDK 1.39 |
| Сегментный MPEG-TS AVC/AAC | Общий TS → fMP4 bridge + MP4Splitter | Тот же bridge + libmpv | Тот же общий Qt-код | [SDK 1.39](ProgressiveTsPlayback.md): required separate AVC/AAC, SHA, inline/`paged-mpegts-v1`; pause/seek/tail/complete/release; muxed/live/произвольный HLS вне профиля |
| Совместные TS-сегменты AVC/AAC | Общий demux двух PID → virtual MP4 + MP4Splitter | Тот же bridge + libmpv, shared file ID после admission | Тот же Qt-код | [SDK 1.40](ProgressiveMuxedTsPlayback.md): один resource с A/V, 2 logical views, SHA/cache/leases; inline/`paged-muxed-mpegts-v1`; один растущий файл и muxed fMP4/WebM остаются отдельно |
| Один растущий TS-файл AVC/AAC | Incremental TS demux → virtual MP4 + MP4Splitter | Тот же bridge + libmpv | Тот же общий Qt-код | [SDK 1.41](ProgressiveGrowingTs.md): один physical resource, два logical scalar view, SHA confirmed prefix, partial packet/PES и закрытые GOP; VOD без смены конфигурации/live; muxed fMP4/WebM остаются отдельно |
| Один растущий fMP4 AVC/AAC | Два projected moov/traf view общего mdat + MP4Splitter | Тот же bridge + libmpv | Тот же общий Qt-код | [SDK 1.42](ProgressiveMuxedFmp4.md): SHA, complete moof/mdat publication, один physical lease, self-contained data refs, B-frames/edit list; muxed WebM и другие кодеки в muxed fMP4 — отдельные профили |
| Один растущий WebM VP9/Opus | Два filtered TrackEntry/Cluster view + MatroskaSplitter | Общий scalar bridge + libmpv | Тот же Qt-код | [SDK 1.43](ProgressiveMuxedWebm.md): VP9 8/10-bit, Opus mono/stereo, fractional DefaultDuration, complete-Cluster publication, SHA/shared lease; AV1 — следующая строка; VP8 — строка SDK 1.45; Vorbis — строка SDK 1.46; другие кодеки fMP4 отдельно |
| Один растущий WebM AV1 Main 8/10-bit + Opus (SDK 1.44) | Да, отдельный mode | Да, отдельный mode | Да, отдельный mode | [Контракт](ProgressiveMuxedWebmAv1.md) · [Проверка](ProgressiveMuxedWebmAv1Validation-2026-10-31.md) |
| Один растущий WebM VP8/Opus (SDK 1.45) | Есть, отдельный mode | Есть, отдельный mode | Есть, отдельный mode | [Контракт](ProgressiveMuxedWebmVp8.md) · [Проверка](ProgressiveMuxedWebmVp8Validation-2026-10-31.md) |
| Один растущий WebM Vorbis + VP8/VP9/AV1 (SDK 1.46) | Есть, exact pair | Есть, exact pair | Есть, exact pair | [Контракт](ProgressiveMuxedWebmVorbis.md) · [Проверка](ProgressiveMuxedWebmVorbisValidation-2026-10-31.md) |
| Один растущий fMP4 HEVC Main/Main10 + AAC-LC (SDK 1.47) | Есть, отдельный mode | Есть, отдельный mode | Есть, отдельный mode | [Контракт](ProgressiveMuxedFmp4Hevc.md) · [Проверка](ProgressiveMuxedFmp4HevcValidation-2026-10-31.md) |
| Один растущий fMP4 AV1 Main 8/10-bit + AAC-LC (SDK 1.48) | Есть, отдельный mode | Есть, отдельный mode | Есть, отдельный mode | [Контракт](ProgressiveMuxedFmp4Av1.md) · [Проверка](ProgressiveMuxedFmp4Av1Validation-2026-10-31.md) |
| Один растущий fMP4 VP9 Profile 0/2 8/10-bit + AAC-LC (SDK 1.49) | Есть, отдельный mode | Есть, отдельный mode | Есть, отдельный mode | [Контракт](ProgressiveMuxedFmp4Vp9.md) · [Проверка](ProgressiveMuxedFmp4Vp9Validation-2026-10-31.md) |
| Один растущий fMP4 Opus + H.264/HEVC/AV1/VP9 (SDK 1.50) | Есть, exact pair | Есть, exact pair | Есть, exact pair | [Контракт](ProgressiveMuxedFmp4Opus.md) · [Проверка](ProgressiveMuxedFmp4OpusValidation-2026-10-31.md) |
| Backend demux диагностика (SDK 1.51) | AVIO probe | AVIO probe | AVIO probe | [Контракт](ProgressiveBackendDemux.md) · [Проверки](ProgressiveBackendDemuxValidation-2026-10-31.md); только анализ, не playback/remux |
| Copy-remux MP4 RAM-фрагмента (SDK 1.52) | Подготовка, H.264/AAC-LC | Подготовка, H.264/AAC-LC | Подготовка, H.264/AAC-LC | [Контракт](ProgressiveRemuxStage.md) · [Проверки](ProgressiveRemuxStageValidation-2026-10-31.md); отдельный ограниченный playback adapter добавлен в SDK 1.53 |
| Явный RAM MP4 preview (SDK 1.53) | Есть: штатный MP4 splitter/DirectShow | Есть: libmpv stream callback | Есть: libmpv stream callback | [Контракт](ProgressivePreparedPreview.md) · [Проверки](ProgressivePreparedPreviewValidation-2026-10-31.md); H.264/AAC-LC, один ограниченный фрагмент, continuousPlayback=false |
| Обычный растущий fast-start MP4 (SDK 1.54) | Есть: общая fMP4-проекция/DirectShow | Есть: общая проекция/libmpv | Есть: общая проекция/libmpv | [Контракт](ProgressiveIndexedMp4.md) · [Проверки](ProgressiveIndexedMp4Validation-2026-11-01.md); H.264/AAC-LC, head sample tables, весь файл, confirmed prefix/SHA |
| Диагностика одного сегмента schema 2 | `resourceProbe` + single-track fMP4 index | Тот же общий stager | Тот же общий stager | [SDK 1.32](ProgressiveSegmentStaging.md), без decoder playback и remux; HLS/DASH в целом ещё не готов |
| Сегментные ролики более 64 ресурсов | Каталог + bounded I/O + реальный граф | Тот же каталог + libmpv | Тот же каталог + libmpv | [SDK 1.37](ProgressiveCatalogPlayback.md): до 8192 metadata-ресурсов, восемь открытых медиа, SHA-256; pause/seek/reopen 182–183 ресурсов подтверждены. Многочасовая A/V-приёмка и оптимизация большого ready-каталога ещё нужны; прежний inline остаётся ограничен 64 |
| Progressive главы и title | Штатный IDSMChapterBag, меню/seekbar и заголовок; сброс revision при новом сеансе | Timeline и меню из опубликованных metadata, пустой список очищает главы | Тот же Qt-код | [SDK 1.27](ProgressiveMetadata.md): проверяется фактический appliedMetadata; [отчёт](ProgressiveMetadataUiValidation-2026-10-29.md) |
| Progressive ошибки optional SRT | Ошибка дорожки/загрузки отделена от A/V; проверенная копия сохраняется | То же + асинхронный ответ sub-add не закрывает видео | Тот же Qt-код | [SDK 1.24](ProgressiveOptionalSrt.md): metadata и required A/V остаются строгими; [проверки](ProgressiveOptionalSrtValidation-2026-10-29.md) |
| Progressive перемотка multi-audio | Native индекс, без изменений SDK 1.23 | Байтовая перемотка подтверждённого участка | Тот же Qt-код | [SDK 1.23](ProgressiveSeek.md): без лимита 10 секунд; [проверки обеих Qt-сборок](ProgressiveSeekValidation-2026-10-28.md) |
| Native decoder shutdown и renderer lifetime | Progressive STA close-state machine; обычные/UI-owned графы синхронны | Core и software renderer — bounded worker queue; GL free у владельца | Тот же Qt-код | [SDK 1.12](ProgressiveEngineShutdown.md): Qt 16 cores / ≤2 workers, stream callbacks и Source удерживаются; [SDK 1.13](ProgressiveClassicClose.md): Classic сохраняет COM owner и окно, откладывает start/exit; detached teardown не объявляется |
| Пауза, точный seek, главы, покадровые команды | Есть | Есть | Есть | Допуски seek и ограничения live/growing-файлов |
| Скорость, повторы, A–B | Есть | Есть | Есть | Края A–B, конец очереди, пауза и число проходов |
| Действия после окончания | Есть, Win32 | Есть, Win32 | Есть, logind/session D-Bus | Один смысл постоянного и однократного действия; платформенные исполнители |
| DVD/Blu-ray/CD: заголовки/содержимое | Есть | Общий Qt-путь; физические диски не подтверждены | Реализованы в описанных границах | Отложено; не смешивать открытие ISO и интерактивное меню |
| Интерактивное меню DVD/Blu-ray | Собственные дисковые пути, поддержка зависит от вида диска | Нет полного аналога | Нет полного аналога | Отложено пользователем |
| Захват, тюнеры, аналоговое ТВ | DirectShow/BDA-пути | Нет переноса Linux-устройств на Win32 | V4L2/ALSA/Pulse/DVB частично, аналоговая настройка не реализована | Отложено пользователем |

Основание: [MainFrm.cpp](../src/apps/mplayerc/MainFrm.cpp),
[PlayerYtDlp.cpp](../src/apps/mplayerc/PlayerYtDlp.cpp),
[HarvesterSession.cpp](../src/DSUtil/HarvesterSession.cpp),
[onlineoptions.cpp](../qt/src/onlineoptions.cpp),
[progressiveapi.cpp](../qt/src/progressiveapi.cpp),
[Windows-адаптер](../qt/src/progressivesource_win.cpp), [растущий источник](../qt/src/publishedsource.cpp),
[devicesource.cpp](../qt/src/devicesource.cpp),
[deviceplayback.cpp](../qt/src/deviceplayback.cpp),
[systemactions.cpp](../qt/src/systemactions.cpp).

## Окно, меню и управление

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Объединённая нижняя панель и узкая громкость | Есть | Синхронизирована с Classic: hover/pressed, двойные подсказки, ПКМ-действия | Синхронизирована с Classic: hover/pressed, двойные подсказки, ПКМ-действия | Геометрия, таймер, состояния play/pause/mute, светлая/тёмная темы |
| Линии глав RGB 94/99/104 | Есть | Есть | Есть | Один визуальный эталон и масштабирование |
| Заголовок файла/ролика, версии, состояния | Есть | Есть | Есть | Одинаковая семантика; системный заголовок остаётся нативным |
| Верхнее и контекстное меню | Сохранены 6 штатных разделов Classic; порядок исходного главного меню скорректирован | 9 организованных разделов | Те же 9 разделов | Classic сохраняет собственные команды и локализацию; 9-раздельная Qt-схема полностью не переносилась |
| Настройки слева деревом | Есть | Есть | Есть | Общие категории и названия |
| Настройки: дерево + вкладки справа + общий поиск | Нет Qt-схемы; 26 страниц Classic | 10 категорий / 40 страниц | Та же схема | В Classic перенести только порядок, сохраняя штатные страницы и переводы |
| Окно настроек появляется с загрузкой и отменой | Нет соответствующего Qt-механизма | Есть | Есть | Qt-механизм не перенесён; Classic сохраняет штатное окно |
| Тёмная/светлая тема, логотипы, размер кнопок | Есть; все 26 страниц настроек оформлены, прокрутка проверена при захвате | Есть | Есть | Общая палитра; лиса и подпись сохраняют редакцию |
| OSD/HUD, сообщения, часы, имя файла | Есть | Есть | Есть | OSD выключено для новых профилей; включение остаётся доступным |
| Превью на таймлайне | Есть | Есть | Есть | Локальные/HTTP-границы, отмена и ресурсы |
| Горячие клавиши и жесты мыши | Есть | Есть | Есть | Общий каталог команд и проверка конфликтов пользовательских назначений |
| Базовые клавиши Classic: Ctrl+Q, Ctrl+O, Alt+X | Есть | Есть | Есть после применения classicKeys | Не считать ранние Qt-fallback значения действующими назначениями |
| Перетаскивание окна за видео, клик, двойной клик | Есть | Есть | Есть | Общие порог drag, play/pause и fullscreen |
| Медиакоманды вне активного окна | Win32 APPCOMMAND / global hotkeys | Win32 APPCOMMAND для play/stop/next/previous/seek/volume | MPRIS / playerctl | SMTC и глобальные hotkeys остаются отдельным этапом |
| WinLIRC | Есть | Нет соответствующего адаптера | Нет соответствующего адаптера | Отдельная совместимость; пульт-клавиатура использует обычные клавиши |
| Геометрия, поверх окон, полный экран, монитор | Есть | Есть, Win32/Qt | Есть, X11/Qt | Одинаковые политики; реальное оборудование не проверялось заново |

Основание: [меню Classic](../src/apps/mplayerc/mplayerc.rc),
[страницы Classic](../src/apps/mplayerc/PPageSheet.cpp),
[организованное меню Qt](../qt/src/organizedmenus.cpp),
[страницы Qt](../qt/src/classicpreferences.cpp),
[клавиши Classic](../src/apps/mplayerc/AppSettings.cpp),
[эффективные клавиши Qt](../qt/src/classicmenus.cpp),
[системные медиаклавиши Classic](../src/apps/mplayerc/mplayerc.cpp),
[MPRIS](../qt/src/mediaremote.cpp), [управление Qt](../qt/src/inputcontrol.cpp).
Количество страниц измерено по подключённым AddPage/page, а не по всем .rc-файлам;
26 и 40 не являются числом отсутствующих возможностей.

## Библиотека и пользовательские данные

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Плейлисты, вкладки, поиск, сортировки | Есть | Есть | Есть | Набор операций, текущая строка при сортировке и переходы |
| M3U/M3U8, MPCPL, PLS, ASX, CUE | Есть | Есть с оговорёнными границами | Есть с оговорёнными границами | Общий набор импорт/экспорт-образцов; DirectShow-устройства не переносимы |
| Дорожки и позиция записи, отсутствующие файлы | Есть | Есть | Есть | Дубликаты, смена вкладки, пропуск ошибок, восстановление |
| История, поиск, выборочная очистка | Есть | Есть | Есть | Позиция, названия URL, лимиты и очистка без повреждения очереди |
| Избранное и закладки с позицией | Есть | Есть | Есть | Категории, порядок, относительные пути, перенос между ОС |
| Независимый portable-профиль | Есть, свой формат | Есть, FoxicMPQt.ini | Есть, FoxicMPQt.ini | Профили остаются раздельными; бинарные/платформенные настройки не смешивать |
| Экспорт/сброс настроек | Собственный экспорт и сброс | Общий Qt-архив/копии/восстановление/сброс | То же | Общий переносимый набор с явным отображением несовместимых полей |
| Ярлык выбранного профиля | Собственные параметры запуска; не аналог Qt profile-manager | Есть, .lnk и --profile | Есть, .desktop и --profile | Общая модель выбора профиля; разный транспорт запуска |
| Импорт библиотеки и общих предпочтений между всеми тремя | Нет единого мастера | Нет Classic↔Qt-мастера | Нет Classic↔Qt-мастера | Отложено пользователем; профили остаются независимыми |

Основание: [PlayerPlaylistBar.cpp](../src/apps/mplayerc/PlayerPlaylistBar.cpp),
[HistoryDlg.cpp](../src/apps/mplayerc/HistoryDlg.cpp),
[FavoriteOrganizeDlg.cpp](../src/apps/mplayerc/FavoriteOrganizeDlg.cpp),
[playlistpanel.cpp](../qt/src/playlistpanel.cpp),
[playlistfile.cpp](../qt/src/playlistfile.cpp), [library.cpp](../qt/src/library.cpp),
[profilearchive.cpp](../qt/src/profilearchive.cpp),
[profileoptions.cpp](../qt/src/profileoptions.cpp).

## Видео, звук и субтитры

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Пропорции, crop, pan/scan, повороты, отражение | Есть | Есть | Есть | Одинаковые команды и пресеты; не путать пропорции видео с ограничением окна |
| Стереораскладки и смена глаз | Есть | Есть | Есть | Сценарии преобразования; физический 3D-экран не подтверждён |
| Выбор видеорендерера | DirectShow/EVR/внешние рендереры | Qt Software / OpenGL | Qt Software / OpenGL | Сохранять платформенные механизмы, показывать фактический вывод |
| Аппаратное декодирование | Собственные DXVA/DirectShow-настройки | auto-copy; список ручных backend ещё содержит Linux-варианты | auto-copy / VA-API / NVDEC / VDPAU | Платформенный список и проверка доступности, Windows D3D-варианты не выданы как готовые |
| HDR и 10-bit конечного вывода | Зависит от выбранного рендерера; аппаратная проверка отсутствует | Конечная Qt OpenGL-поверхность RGBA8; нет заявленного HDR passthrough | То же | Общая честная диагностика; развитие вывода — отдельный этап, не обещание HDR |
| Цвет, скейлеры, ICC | Есть, зависит от рендерера | Есть; авто ICC через Windows ICM | Есть; авто ICC X11/colord | Общие названия и результат; разные адаптеры получения профиля |
| Шейдеры | HLSL / цепочки рендерера | GLSL и ограниченный импорт mpv hook | То же | Пресеты по назначению; автоматическая конверсия произвольного HLSL не реализована |
| Внешние DirectShow-фильтры, приоритеты | Есть | Не применимо к libmpv | Не применимо к libmpv | Оставить Classic-спецификой; не рисовать неработающие галочки |
| Выбор аудиоустройства, задержка, внешние дорожки | Есть | Есть | Есть | Горячая смена, возврат к auto, исчезнувшее устройство, отсутствие тишины |
| Нормализация, gain, микшер, фильтры | Есть, собственный тракт | Есть, общий Qt/libmpv тракт | То же | Сравнивать результат и диапазоны, а не номера фильтров |
| Два аудиовыхода одновременно | Есть, два рендерера | Есть: второй WASAPI/libmpv PCM-декодер с программной синхронизацией | Есть, PulseAudio/PipeWire PCM | Сверять задержку; физические устройства и ресиверы не проверены |
| PCM/частота/bitstream | Есть, зависит от аудиорендерера | Есть настройки общего libmpv-тракта | Есть; программные замеры PCM/IEC 61937 | Реальные HDMI/S/PDIF и ресивер отложены; галочка не подтверждает тракт |
| Встроенные/внешние SRT/ASS, задержка, оформление | Есть | Есть | Есть | Общий корпус, выбор языка, шрифты, масштаб и реплики |
| Редактор/синхронизация субтитров | Есть Subresync/сохранение | Есть Qt-редактор | То же | Совместимый набор операций и сохранение форматов с оговорёнными потерями |
| Сторонние subtitle-renderer/DirectShow очереди | Есть соответствующие настройки | Нет прямого аналога libmpv | Нет прямого аналога libmpv | Остаётся спецификой Classic |

Основание: [PPageVideo.cpp](../src/apps/mplayerc/PPageVideo.cpp),
[PPageAudio.cpp](../src/apps/mplayerc/PPageAudio.cpp),
[FGManager.cpp](../src/apps/mplayerc/FGManager.cpp),
[videooutput.cpp](../qt/src/videooutput.cpp),
[videooutputoptions.cpp](../qt/src/videooutputoptions.cpp),
[systemicc.cpp](../qt/src/systemicc.cpp), [videoshaders.cpp](../qt/src/videoshaders.cpp),
[audiosourceoptions.cpp](../qt/src/audiosourceoptions.cpp),
[audioencoding.cpp](../qt/src/audioencoding.cpp),
[subtitlerender.cpp](../qt/src/subtitlerender.cpp).

## Интеграция, инструменты и выпуск

| Возможность | Windows Classic | Windows Qt | Linux Qt | Что выравнивать |
|---|---|---|---|---|
| Меню видеофайлов и проверка пути | Есть, собственная регистрация | Есть, собственная регистрация | Есть, desktop/MIME | Одни действия пользователя, разные механизмы ОС |
| Снимки, буфер, миниатюры, свойства | Есть | Есть | Есть | Одинаковые команды; область захвата и доступные форматы явно показывать |
| Сохранение копии источника | Собственные пути Save As | Qt-копия локального файла | То же | Не приравнивать локальную копию к загрузке/ремультиплексированию сетевого потока |
| Веб-пульт | Classic HTML/команды и файловый browser | Qt-пульт, LAN и слой Classic Web | То же | Общий контракт разрешённых команд; совместимость части endpoints не полная замена Classic Web |
| CLI и команды работающему процессу | Собственные ключи, HWND/slave/WM_COPYDATA | Qt CLI и instance IPC | То же, Unix-адаптер | Общие сценарии с таблицей ключей и адаптерами, сохранить старые вызовы |
| Проверка обновлений и диагностика | Свои механизмы, minidump | Qt-проверка и отчёт | То же | Общая модель канала/сборки; общего автоматического обновлятора нет |
| Автоматическая сборка в репозитории | Есть Windows workflow | Отдельного Windows Qt workflow нет | Есть Linux Qt workflow | Три сборки по общему набору изменений и общий отчёт |
| Объём автоматизированных проверок | Сборка и named-pipe capabilities проверены; полный DirectShow Harvester playback зависит от интерактивного графа | Windows Qt: progressive API 11/11 PASS, toolbar contract PASS; audio-mirror 3 PASS/2 SKIP без детерминированного WASAPI endpoint | Linux Qt: audio-mirror, progressive-api и playback-controls 3/3 PASS | Физическую задержку звука и оборудование не выдавать за программный PASS |

Основание: [integration_win.cpp](../qt/src/integration_win.cpp),
[integration.cpp](../qt/src/integration.cpp),
[webcontrol.cpp](../qt/src/webcontrol.cpp),
[WebServer.cpp](../src/apps/mplayerc/WebServer.cpp),
[launchoptions.cpp](../qt/src/launchoptions.cpp),
[CMakeLists.txt](../qt/CMakeLists.txt),
[Windows Classic CI](../.github/workflows/main.yml),
[Linux Qt CI](../.github/workflows/linux-qt.yml),
[Windows Qt build/deploy](../qt/packaging/windows/README.ru.md).

## Главные расхождения и уже общие правила

1. Windows Qt получила v1 progressive API через named pipe и второй PCM-аудиовыход; физическая синхронизация устройств ещё не измерялась.
2. В Windows Qt нет самостоятельной системной интеграции медиакоманд уровня
   Win32 Classic или Linux MPRIS. Обычные клавиши внутри окна работают.
3. Classic не получила Qt-организацию меню и настроек с общим поиском,
   а её DirectShow/renderer-возможности нельзя перенести простым копированием UI.
4. Новая Windows Qt установка получает 617 сохранённых предпочтений из шаблона,
   Linux — значения профиля и fallback функций. Например, меню в шаблоне Windows
   скрыто, а Linux-fallback показывает его. Полного общего каталога defaults нет.
5. Горячие клавиши нельзя сравнивать только по buildActions: classicKeys позже
   заменяет базовые назначения. Эффективные Ctrl+Q/Ctrl+O/Alt+X уже согласованы
   для стандартного нового Qt-профиля; сохранённый пользовательский выбор выше.
6. Общие пользовательские правила уже есть: portable-данные, OSD выключено,
   запоминание положения окна включено, ограничение пропорций окна по кадру
   выключено. `KeepAspectRatio` видео и `LimitWindowProportions` окна — разные
   параметры Classic; не обнулять первый ради совпадения со вторым.
7. Имена/логотипы редакций уже разделены. Разрыв нумерации 1.9.1.75 и 0.2.0
   отражает историю движков и сам по себе не измеряет готовность функций.

## Границы подтверждения

Для Qt 0.1.72 имеются [проверки звука и OSD](AudioOsdValidation-2026-10-24.md):
20 Windows checks и шесть затронутых Linux-групп. Это не полный совместный
прогон трёх редакций. Ранее [медиаматрица](QtLinuxMediaMatrixValidation-2026-10-19.txt)
сравнивала Linux с Classic; Classic тогда использовала null renderers, поэтому
она не доказывает совпадение изображения, реального звука и A/V.

Диски/ТВ/физические устройства отложены пользователем. Wayland пользователь
исключил из проверок. Отсутствие оборудования не обозначается как дефект,
исправленный или подтверждённый только программным тестом.

SDK 1.33 добавляет [ограниченное воспроизведение цепочки fMP4 H.264/AAC-LC](ProgressiveSegmentPlayback.md) через общий I/O трёх редакций. TS, live, смена codec/init и произвольный HLS/DASH остаются за границей этого профиля; Harvester не менялся.


SDK 1.55: Linux Qt, Windows Qt и Classic имеют общий ограниченный [tail-moov MP4 H.264/AAC-LC](ProgressiveTailMp4.md), ожидание индекса и metadata-only advisory ranges. [Проверки](ProgressiveTailMp4Validation-2026-11-01.md) отдельно подтверждают каждую редакцию; произвольные sparse media и общий backend remux не заявляются.


SDK 1.56: [перемотка активной сессии после complete](ProgressiveCompletedSeek.md),
capability `completedSourceSeek`. Qt переоткрывает декодер на том же pinned source,
когда прежний потоковый decoder cache ограничивал дальний seek; Classic сохраняет
штатное позиционирование. Reader, ручная пауза, скорость, громкость/mute и основная
аудиодорожка сохранены; status дополнен `controls`. [Проверки трёх редакций](ProgressiveCompletedSeekValidation-2026-11-01.md).
Это ограниченный H.264/AAC-LC профиль; общий remux, прочие форматы и длительная
реальная приёмка остаются в чеклисте. Harvester не меняется.


SDK 1.57: [самостоятельные AAC-LC audio-only и H.264 video-only в fMP4](ProgressiveStandalone.md),
один реальный required track/resource, отдельная capability `schema2Playback.standalone`.
Тип дорожки отделён от native slot; буферизация не ждёт отсутствующий поток.
Проверены mono/stereo, VFR/B-frames, неизвестная длина, pause/seek/update/complete,
release и [совместимость трёх редакций](ProgressiveStandaloneValidation-2026-11-01.md).
Общая codec/container matrix и оставшееся ТЗ открыты. Harvester не меняется.

SDK 1.58: [самостоятельное Opus mono/stereo в растущем fMP4](ProgressiveStandaloneOpus.md),
профиль `standalone-fmp4-opus` внутри `schema2Playback.standalone`.
Один настоящий audio track/resource, 48 kHz, mapping family 0, проверка dOps/pre-skip/edit list;
старые AAC/H.264 и парные Opus профили сохранены. [Проверки каждой редакции](ProgressiveStandaloneOpusValidation-2026-11-01.md).
Следом — самостоятельные WebM/Opus/Vorbis и дальнейшая матрица из чеклиста. Harvester не изменён.

SDK 1.59: [самостоятельные Opus/Vorbis mono/stereo в растущем WebM](ProgressiveStandaloneWebm.md),
отдельная capability `schema2Playback.standalone.webm` и профили
`standalone-webm-opus` / `standalone-webm-vorbis`. Один настоящий audio track/resource;
тип slot 0 и аудиоперемотка Classic исправлены без изменения парных потоков.
[Результаты трёх редакций](ProgressiveStandaloneWebmValidation-2026-11-01.md).
Далее — standalone WebM video, остальные codec/container profiles и пункты чеклиста; Harvester не изменён.

SDK 1.60: [самостоятельное WebM-видео VP8/VP9/AV1](ProgressiveStandaloneWebmVideo.md),
отдельная capability `schema2Playback.standalone.webmVideo` и точные profiles.
Шесть runtime profiles каждой редакции: VP8, VP9/AV1 8/10-bit, 25 и 30000/1001 fps;
один настоящий video track/resource без audio. [Проверки](ProgressiveStandaloneWebmVideoValidation-2026-11-01.md).
Standalone WebM audio/fMP4 сохранены. Далее — прочие standalone codec/container profiles
и пункты общего чеклиста. Harvester не изменён.

SDK 1.61: [самостоятельное HEVC/AV1/VP9-видео в растущем fMP4](ProgressiveStandaloneFmp4Video.md),
точные standalone profiles, шесть 8/10-bit и fractional runtime rows каждой редакции.
Штатные hvcC/av1C/vpcC validators и один настоящий video track/resource без аудио;
[проверки](ProgressiveStandaloneFmp4VideoValidation-2026-11-01.md).
WebM и прежние fMP4 profiles сохранены. Далее — оставшаяся codec/container matrix
и пункты общего чеклиста. Harvester не изменён.

SDK 1.62: [самостоятельное FLAC-аудио в растущем fMP4](ProgressiveStandaloneFlac.md),
восемь mono/stereo × 16/24-bit × 44.1/48-kHz runtime profiles на редакцию,
строгая dfLa/STREAMINFO и PCM oracle после чтения через bridge;
[проверки](ProgressiveStandaloneFlacValidation-2026-11-01.md).
Прежние AAC/Opus/video сохранены. Raw `.flac`, другие audio containers,
многоканальные layout/rates и остальные пункты чеклиста остаются открытыми. Harvester не изменён.

SDK 1.63: [самостоятельный растущий native FLAC](ProgressiveRawFlac.md),
один настоящий audio track/resource; fixed-block mono/stereo, 16/24-bit,
44.1/48 kHz. Границы кадров, CRC-8/CRC-16 и SHA-256 проверяются до публикации
в виртуальный fMP4; исходные compressed frames сохраняются без перекодирования.
[Проверки трёх редакций](ProgressiveRawFlacValidation-2026-11-01.md).
Остальные audio containers, layouts/rates/depths, variable blocks, live и
реальная интеграция Harvester остаются в общем чеклисте. Harvester не изменён.

SDK 1.64: [самостоятельный растущий MPEG-4 ADTS AAC-LC](ProgressiveAdts.md),
mono/stereo, 44.1/48 kHz, без CRC, один raw data block/frame. AAC payload
передаётся через виртуальный MP4 без перекодирования, со строгими frame bounds,
неизменной configuration и ожиданием неполного header/payload.
[Проверки трёх редакций](ProgressiveAdtsValidation-2026-11-01.md).
FLAC использует общий scalar audio bridge и MP4 projection с сохранением
своего контракта и отдельными regression checks. Harvester не изменён;
остальные ADTS/audio formats и общая приёмка остаются в чеклисте.

SDK 1.65: [самостоятельный растущий MP3 MPEG-1 Layer III](ProgressiveMp3.md),
CBR/VBR mono/stereo, 44.1/48 kHz без CRC. Bounded ID3v2.3/v2.4/ID3v1,
проверка frame bounds и reservoir references; 250 ms seek preroll в Qt/Classic.
[Проверки трёх редакций](ProgressiveMp3Validation-2026-11-01.md).
Полные compressed frames сохраняются в виртуальном MP4 без перекодирования.
Xing/gapless, прочие MPEG audio profiles и общий чеклист остаются открытыми.
Harvester не изменён.

SDK 1.66: [MP3 с Xing/Info/VBRI](ProgressiveMp3Indexed.md), отдельный
`standalone-mp3-indexed-v1` с encoded-sample timeline. Счётчики проверяются
на growing/complete, non-audio index frame пропускается; raw encoder
delay/padding сохраняется, `gaplessTrimApplied=false` объявлен явно.
[Проверки трёх редакций](ProgressiveMp3IndexedValidation-2026-11-01.md).
Прежний MP3-профиль сохранён. Gapless decoder/output adapters, реальный
VBRI corpus и прочие пункты чеклиста открыты; Harvester не изменён.

Gapless PCM reference: [стенд](ProgressiveMp3GaplessReference.md), по 128
offline checks Linux/Windows. libmpv PCM output с `atrim`/seek проверен;
фактические progressive Qt adapters и Classic DirectShow output ещё открыты.
Production playback/API/SDK 1.66 этим тестовым этапом не меняются.

[Внутренний Qt PCM adapter](ProgressiveQtPcmWindow.md) реализован в общем
`Player` Windows/Linux: по 72 проверки exact PCM, user filters, stop/reload.
Progressive controller и Classic PCM/output adapter остаются открытыми;
полная gapless-функция не объявлена в capabilities SDK 1.66.

SDK 1.67: [LAME Info Tag CRC / PCM trim plan](ProgressiveMp3Tag.md),
единое ядро трёх редакций. CRC проверяет header/metadata, не звук целиком;
недостоверная metadata не даёт trim plan и не ломает прежний encoded playback.
[Native/runtime проверки](ProgressiveMp3TagValidation-2026-11-01.md),
включая настоящий libmp3lame encoder, corrupt tags и ASan/UBSan.
Публичного gapless playback ещё нет: controller и Classic PCM adapter открыты.
Harvester не изменён.

SDK 1.68: [gapless MP3 в Windows/Linux Qt](ProgressiveMp3Gapless.md),
отдельный `standalone-mp3-gapless-v1` с полезной звуковой шкалой,
проверенным tag CRC, точным PCM trim/seek и ожиданием writer complete.
[Результаты](ProgressiveMp3GaplessValidation-2026-11-02.md): по 31 новым native
checks, четырём growing profiles и 16 controller PCM windows на Qt ОС.
Classic явно отказывает новому профилю без reader, сохраняя прежнее воспроизведение;
его output adapter остаётся открытым. Общие capabilities прежние, Qt-дополнение
описано отдельно. Linux обновлён с backup и сохранением INI. Harvester не изменён.

Внутренний [PCM-адаптер Classic](ProgressiveClassicPcmWindow.md) проверен
через настоящий CFFAudioDecoder: 40 checks, 16 точных окон, flush/replacement
и отказ при усечённом PCM. Scalar: 1737 checks MSVC/Linux, ASan/UBSan Linux.
Публичный gapless Classic пока отключён: подключение graph/controller,
точное re-arm на seek и capture после DirectShow graph остаются открытыми.
Qt/SDK 1.68, пользовательские профили и установки не меняются.

[PCM-окно Classic подключено к внутреннему DirectShow-графу](ProgressiveClassicPcmGraph.md):
точный frame/sample preroll, передача плана после startup flush, применение
после discontinuity и повторное вооружение на seek. По 16 PCM capture windows,
четырём growing-профилям и проверке несовместимого downstream без вывода звука.
Публичная capability Classic остаётся выключенной до выбора decoder/status/UI
и проверки пользовательской обработки; Qt и SDK 1.68 не меняются.

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


4 ноября 2026, внутренний continuous output readerRelease: ordinary API всех
трёх редакций сопоставляет квитанцию с точным output resource/device/file identity
и зафиксированной попыткой просмотра. Проверены playing, pause, ожидание хвоста,
EOF, повторные/чужие/устаревшие команды; Qt Window/API — delayed registry I/O,
замена видео и дополнительный удерживаемый reader. Устранён Windows registry-busy
ответ при повторном чтении сразу после уже принятой остановки: возвращается
консервативная квитанция, затем клиент проверяет реальный close через readerStatus.
Уже закрытые квитанции инертны до снятия lock; внешний readerForget совместим
с cleanup задания только после независимых proofs закрытия и удаления кэша.
[Отчёт](ProgressiveContinuousRemuxReaderReleaseValidation-2026-11-04.json).
Linux обновлён атомарно с backup без изменения INI/267 других файлов.
Публичные capability/SDK и worker пользователю пока не поставлены; audio/B-frames
и полная waiting/replay/crash/stress matrix остаются открытыми. Harvester,
Windows-установки, shell registration и личные настройки сохранены.


4 ноября 2026, SDK 1.82: общий публичный ограниченный continuousRemux contract,
девять API действий и явный Python ContinuousRemux helper. Prepare/play разделены;
потеря ответов восстанавливается read-only status, writer events не запускают
просмотр. По пять реальных SDK IPC сценариев Linux Qt, Windows Qt и Classic,
17 SDK unit, прежние 17/17/18 playback и 6 preparation/24 bounded preparation/20
playback regression; 19 v1 и 63 v2 SDK unit checks. Windows Qt clean portable и
извлечённый .deb отдельно прошли SDK playback. Worker --info проверяет FFmpeg ABI;
упаковка включает worker/runtime, Linux executable mode и обязательные библиотеки.
[Контракт](ProgressiveContinuousSdk.md),
[результаты](ProgressiveContinuousSdkValidation-2026-11-04.json).
Linux обновляется с backup и сохранением профиля; Windows-установки/HKCU, SDK 1.81
ZIP и Harvester сохраняются. Только MP4/H.264 video-only без B-frames. Полная
waiting/replay/crash/stress matrix, audio/AAC trim/B-frames и остальное ТЗ открыты.


4 ноября 2026, публичный continuous SDK waiting/replay срез: по пять сценариев
Linux Qt, Windows Qt и Classic на опубликованных EXE/worker SDK 1.82. Реальные
паузы writer 10/30/60 s не дают EOF и не заменяют worker/output readers;
возобновление 0,20–0,41 s, API <0,23 s, manual pause/append, complete reverse
seek/EOF, Stop/Cancel у хвоста и terminal replay проверены. Начальный префикс
12 s одинаков для редакций и сохраняет штатный Classic startup buffer 10 s.
[Методика](ProgressiveContinuousWaiting.md),
[точные exit codes и исходные ошибки стенда](ProgressiveContinuousWaitingValidation-2026-11-04.json).
Production EXE, INI и 158 shell entries сохранены; три одноразовые Windows-задачи
и тестовые копии удалены. Новая сборка/переустановка для тестов и документации
не требуется. Harvester и опубликованные SDK/релизы не менялись.
Обнаружено: при реальной остановке decoder у хвоста Qt сообщает `playing`,
Classic — `buffering`. Qt state/reasonCodes/индикация остаются открытыми и идут
следующими, затем heartbeat expiry, shutdown/crash и длительная stress matrix.
Полная waiting/stress приёмка, audio/B-frames и остальное ТЗ не закрыты.


4 ноября 2026, Qt continuous buffering status/UI исправлен в общем Qt-коде.
API учитывает настоящий paused-for-cache; ручная пауза имеет приоритет в API,
строке состояния и шаблоне заголовка. Optional reasonCode различает waiting-range,
decoder-opening/decoder-buffering, paused, source-complete, playback-closed и ошибки.
[Методика](ProgressiveContinuousBuffering.md),
[новый отчёт](ProgressiveContinuousBufferingValidation-2026-11-04.json).
Baseline Linux EXE воспроизвёл ошибку с CTest exit 8; обе исправленные Qt ОС
прошли по 11 CTest групп, по 5 SDK waiting сценариев 10/30/60 s и по 17 raw playback
сценариев с отдельной проверкой writer-failed. Новый Window/API сценарий проверяет
настоящий label, API и ручную паузу при complete; resume 0,20–0,26 s.
Linux обновлён атомарно с backup, сохранены INI и 278 других существующих файлов.
Установленные Windows EXE/INI, Classic, 158 shell entries, опубликованные SDK ZIP
и Harvester не менялись. Новых периодических или одноразовых задач не создавалось.
Classic ранее уже подтверждал buffering; новый optional Qt reasonCode не объявлен
обязательным полем Classic/SDK. Следом идут heartbeat expiry, shutdown/crash и
длительная stress matrix. Полное ТЗ и общая межформатная reasonCodes приёмка открыты.


4 ноября 2026, [continuous writer heartbeat](ProgressiveContinuousHeartbeat.md):
по пять публичных SDK сценариев Linux Qt, Windows Qt и Classic с настоящим сроком
30000 ms. Status и чужие события не продлевают writer; timeout отличается от EOF,
поздние события отклоняются; complete допускает паузу без heartbeat. В общем Qt
исправлено удержание output reader декодером на ручной паузе после producer error;
новый Window/API тест проверяет освобождение без playback status/control.
[Отчёт и точные exit codes](ProgressiveContinuousHeartbeatValidation-2026-11-04.json).
Windows Qt: 12 групп exit 0. Первый исправленный Linux full run: exit 8, включая
ошибку waiting-стенда и превышение 2 s release на паузе; итоговые heartbeat/waiting
повторы прошли, но причина единичного превышения остаётся открытой. Следом —
локализация Linux release latency, shutdown/crash и stress. Linux обновлён атомарно
с backup без изменения профиля; Windows-установки, 158 shell entries и Harvester
сохранены. Одноразовая тестовая задача удалена; периодические не создавались.
Полная reconnect/resume и общая приёмка ТЗ не объявляются завершёнными.


Дополнительная локализация Qt release latency: по шесть настоящих 30 s paused
heartbeat timeout Linux/Windows Qt в тех же приватных процессах, exit 0; добавлен
status-only trace до любых дополнительных диагностических запросов на отказе.
Linux: 0,0034–0,3033 s от первого ответа об ошибке до подтверждения release;
Windows: release уже подтверждён первым контрольным опросом. Эти повторы не
закрывают причину прежнего >2 s Linux отказа. [Отдельные измерения](ProgressiveContinuousReleaseLatencyValidation-2026-11-04.json).
Production EXE/установки/профили не менялись, Classic повторно не запускался,
Harvester сохранён. Следом — продолжение диагностики при повторении и отдельная
continuous shutdown/crash matrix; строгая latency приёмка остаётся открытой.


4 ноября 2026, [continuous player close/crash и same-profile recovery](ProgressiveContinuousShutdown.md):
по восемь сценариев Linux Qt, Windows Qt и Classic, все exit 0; preparation,
playing, paused, buffering, обычный выход и принудительное завершение. Worker exit
подтверждён ОС, независимый просмотр сохраняет IDs и продолжает кадры; writer
физически дописывает оригинал после выхода player, новый prepare восстанавливает
кэш до подтверждения старых receipts, Play создаёт новые IDs. [Результаты](ProgressiveContinuousShutdownValidation-2026-11-04.json).
При части нормальных выходов закрытый journaled cache остаётся до повторного
запуска: это открытый пункт очистки, не доказательство полного teardown ≤2 s.
Прежний Linux heartbeat release outlier также открыт. Следом — normal-exit cache
cleanup, worker crash/mid-I/O/races и stress. EXE/установленные профили, 158 shell
entries, SDK ZIP и Harvester сохранены; одноразовая задача удалена.


4 ноября 2026, [нормальная очистка continuous cache](ProgressiveContinuousExitCleanup.md)
исправлена общим bounded final-exit wait ≤1500 ms после уничтожения владельцев
GUI/API/decoder. Обычные деструкторы/Stop не ждут; живые readers сохраняют fence
и журнал. По восемь усиленных close/crash сценариев каждой редакции: после всех
двенадцати нормальных выходов кэш отсутствует, 0,062–0,235 s. Native controller
проверяет timeout с удерживаемым reader и успешную очистку после реального close.
[Отчёт](ProgressiveContinuousExitCleanupValidation-2026-11-04.json).
Общая latency/crash/stress приёмка и прежний Linux heartbeat outlier открыты.
Windows-установки, профили, shell registration, SDK ZIP и Harvester сохраняются;
проверенная Linux-сборка устанавливается атомарно с backup и сохранением INI.


4 ноября 2026, [аварийное завершение continuous worker](ProgressiveContinuousWorkerCrash.md):
по четыре сценария отдельно Linux Qt, Windows Qt и Classic, все exit 0. Проверенный
OS identity дочернего worker, preparation/playing/paused/buffering, status-only
release без Stop, filesClosed receipts и удаление кэша, независимый просмотр,
реальное дописывание partial и явный retry в том же player с новыми IDs.
Сумма kill/exit и release ожидания 0,047–0,297 s; [результаты и ошибка первого
стендового ожидания](ProgressiveContinuousWorkerCrashValidation-2026-11-04.json).
Production code/EXE, установки, INI, 158 shell entries, SDK ZIP и Harvester
сохранены; временная одноразовая задача удалена. Прежний Linux heartbeat outlier,
mid-I/O/races, расширенный stress и полная приёмка ТЗ остаются открыты.


4 ноября 2026, [гонки continuous-команд и замены просмотра](ProgressiveContinuousCommandRaces.md):
по 16 сценариев Linux Qt, Windows Qt и Classic, все exit 0; publication/Stop,
publication/replacement, readerRelease/replacement и cancel/replacement. Поздние
старые команды не прерывают новый и независимый просмотр, receipts/cache и SHA
проверены. Исправлена общая ложная ошибка MP4 header при registry contention:
сохраняется reader_registry_busy; детерминированный baseline exit 1 сохранён.
На Qt ОС по 13 основных и две multi-reader CTest-группы, по 469 native media
checks; [отчёт](ProgressiveContinuousCommandRacesValidation-2026-11-04.json).
Все main EXE пересобраны, Linux обновлён атомарно с backup и сохранением профиля;
Windows-установки/HKCU/SDK ZIP и Harvester сохранены, одноразовая задача удалена.
Прежний Linux heartbeat outlier, точные mid-I/O schedules, длительный stress и
полная приёмка ТЗ остаются открыты.


5 ноября 2026, [faults незавершённого pipe I/O](ProgressiveContinuousPipeFaults.md):
по 12 native сценариев GCC/Linux, MinGW/Windows, MSVC/Windows и ASan/UBSan,
все exit 0. Тестовый child доказывает partial receive либо backpressure send
до kill/cancel/timeout. Source readers фактически закрыты, partial output не
принят, оригиналы сохранены. Повторены прежние parent/transport проверки Qt ОС
и реальные SDK worker-crash сценарии всех трёх редакций;
[результаты и границы](ProgressiveContinuousPipeFaultsValidation-2026-11-05.json).
Production code/main EXE, установки, профили, HKCU, SDK ZIP и Harvester сохранены;
одноразовая задача удалена. Это компонентная проверка с synthetic child:
настоящий FFmpeg mid file-I/O/cache/decoder, Linux heartbeat outlier и общая
stress/ТЗ-приёмка остаются открытыми.


5 ноября 2026, [авария настоящего FFmpeg в AVIO](ProgressiveContinuousAvioFaults.md):
по восемь public SDK сценариев каждой редакции, всего 24; read/write callbacks,
playing/manual pause и surviving external cache reader. Фактические decoder/
source filesClosed, удержание кэша до последнего close, partial output rejection,
независимый просмотр, SHA и явный retry проверены;
[отчёт с исходными отказами стенда](ProgressiveContinuousAvioFaultsValidation-2026-11-05.json).
Отдельный instrumented test worker собран GCC/MinGW/MSVC, девять engine guards
пройдены под ASan/UBSan. Production worker macro выключен; пересобранный Linux
worker побайтно прежний. Установки/профили, SDK ZIP, HKCU и Harvester сохранены,
одноразовая задача удалена. OS disk/parent file-I/O, прежний Linux heartbeat
outlier и полная stress/ТЗ-приёмка остаются открытыми.


5 ноября 2026, [parent syscall-result faults](ProgressiveContinuousFileIoFaults.md):
50 native scenarios GCC/MinGW/MSVC/ASan-UBSan — source error/short read, cache
write error/disk-full/physical partial write, external readers и POSIX retry.
Исправлена общая диагностика short read: source-truncated вместо ложной
source-rewritten; baseline exit 1 сохранён. По 15 Qt CTest-групп ОС и восемь
настоящих AVIO SDK scenarios Classic;
[результаты, SHA и установка](ProgressiveContinuousFileIoFaultsValidation-2026-11-05.json).
Все main EXE пересобраны, Linux обновлён атомарно с backup и сохранением профиля;
Windows-установки/HKCU, SDK ZIP и Harvester сохранены, одноразовая задача удалена.
Это конечные syscall return faults, не зависший диск: полный cancellation/stress
и прежний Linux heartbeat outlier остаются открытыми.


5 ноября 2026, [continuous lifecycle stress и повторные expiry](ProgressiveContinuousLifecycleStress.md):
итоговый повтор — по 100 измеряемых +4 warmup cycles и 12 настоящих paused
heartbeat timeout всех трёх редакций, те же два owned PID на серию.
Stop/cancel/readerRelease/complete-seek, независимый playback, filesClosed/cache/SHA
и заранее заданные budgets прошли в повторе;
[метрики, первичный отказ и критические RPC timing windows](ProgressiveContinuousLifecycleStressValidation-2026-11-05.json).
Первый Classic resource-budget FAIL после heartbeat 12 остаётся OPEN: точный
неуспешный замер отсутствует; новый стенд сохраняет baseline и ошибки до assert.
Исправлена гонка наблюдения IPC/OS exit в тестовом heartbeat helper;
пороги не увеличены, production fix не заявляется. EXE/установки/профили, SDK ZIP
и Harvester сохранены, HKCU сверены, одноразовая задача удалена. Исходный Linux
release >2 s тоже OPEN. Длительная growing continuous загрузка и полная stress/
ТЗ-приёмка остаются открытыми.


5 ноября 2026, [пассивное наблюдение release/Classic resources](ProgressiveContinuousObservation.md):
две 100-cycle серии со штатным внешним MPCVR завершились resource-budget exit 1,
handles 568 → 625 при пределе 576; после отказа сохранены Event types/modules
и локальные minidump. Контроли по 100 циклов с внутренним EVR и Null прошли
без роста. Это локализация пути, не доказательство ошибки конкретного allocator/
renderer/драйвера и не production fix. Инструментированные успехи отказы не отменяют.
На исходном Linux EXE a778… и текущем 9f67… дважды выполнена полная heartbeat
последовательность: 20 сценариев, CTest exit 0; исторический release >2 s OPEN.
[Результаты и границы](ProgressiveContinuousObservationValidation-2026-11-05.json).
EXE/профили/SDK/worker сохранены, 158 shell entries совпали, одноразовая задача
удалена; Harvester и сторонние установленные компоненты не менялись.


5 ноября 2026, [исправление времени жизни DWM Event в Classic](ProgressiveDwmEventLifetime.md):
лёгкий process-local аудит воспроизвёл три FAIL, каждый со 104 Event из
CreateDxBltEvent; PDB/стеки показали первичную регистрацию DWM cleanup hook в
недолговечном splitter thread. Начальный MPCVR Paint теперь выполняется в UI
до Run/Pause. Обычный EXE прошёл 100 cycles +12 heartbeat expiry, отдельный повтор
ещё 100 cycles, диагностический EXE ещё 100 cycles и подтвердил ноль оставшихся
DWM Event. Максимальный наблюдаемый auto-release 0,296 s при прежнем лимите 2 s;
обычный Stop/Play и 16 command-race сценариев прошли. [Exit codes и исходные
отказы](ProgressiveDwmEventLifetimeValidation-2026-11-05.json). Это конкретный
Classic source fix без Qt/Harvester изменений; SDK ZIP 1.82 и установленные
приложения сохранены. 17 защищённых файлов и 158 shell entries совпали, обе
одноразовые задачи удалены. Исторические Classic heartbeat-12 и Linux >2 s не
объявляются объяснёнными; полная ТЗ/stress/A-V приёмка остаётся OPEN.


5 ноября 2026, [очистка decoder readers без ожидания libmpv callback](ProgressiveDecoderRetirement.md):
целевая native-фиксация удержанного cursor воспроизвела cache-in-use дольше 2 s.
На фоне controller после cancel закрывает только зарегистрированные decoder
readers; independent readers сохраняют lease, complete без отмены не закрывается.
Ошибка публикуется до close I/O, cancel остаётся metadata-only. Linux/Windows Qt:
по четыре целевых native случая, по 12 CTest групп, включая пять настоящих
heartbeat-сценариев и 16 command-race случаев, exit 0. Classic: приватная сборка,
100+4 cycles и пять heartbeat-сценариев, exit 0; raw fragment reader путь сохранён.
Linux EXE обновлён атомарно с backup, INI и 309 других файлов сохранены.
Windows-установки/SDK ZIP/Harvester не менялись, 158 shell entries совпали,
одноразовая задача удалена. [SHA, исходные отказы и результаты](ProgressiveDecoderRetirementValidation-2026-11-05.json).
Точное происхождение исторического Linux >2 s всё ещё OPEN: новый fixture
доказывает конкретную зависимость, не тождественность старому отказу. Stuck OS I/O,
общие A/V и полный stress остаются отдельными шагами.


5 ноября 2026, [два просмотра одного физически растущего файла](ProgressiveGrowingStress.md):
128 s H.264 video-only, 27 append-only публикаций и настоящая 60 s пауза writer.
Во всех трёх редакциях один прогон длился 129–131 s; manual pause сохранена,
второй viewer пережил close/replay первого с прежними identities/worker до complete.
По два output caches каждой редакции совпали с собственным reference по всем
3200 payload/PTS/DTS/duration. После complete проверены seek/EOF/filesClosed/forget;
прежние +8 handles/+2 threads/+256 MiB/≤1 core и <2 s request/resume/release
budgets выдержаны. [Полные данные](ProgressiveGrowingStressValidation-2026-11-05.json).
Production code не менялся, нового EXE нет; установленный Linux ebb0a806… и
все профили/SDK/Windows-установки сохранены. 17 защищённых файлов и 158 shell
entries совпали, одноразовая задача удалена, Harvester не изменялся. Новый CTest
имеет label stress. Это ограниченный двухминутный срез, не многочасовая/A-V
приёмка; исторические Linux >2 s и Classic heartbeat-12 остаются OPEN.


5 ноября 2026, [внутренний continuous worker с H.264 B-frames](ProgressiveContinuousReorder.md):
новый частный mode использует delay_moov/edit list и исходную timebase, сохраняя
отрицательные DTS и presentation timeline. Четыре профиля: 25, fractional,
encoder -bf 16 и VFR; на сборку 3239 точных packet/frame и 16 промежуточных
prefix comparisons. По 11 новых и 9 прежних сценариев GCC/MinGW/MSVC/ASan-UBSan,
exit 0; по пять прежних SDK playback-сценариев трёх редакций с новым worker,
exit 0. [SHA, ошибки стенда и counterexample старых mux flags](ProgressiveContinuousReorderValidation-2026-11-05.json).
Новый режим пока не подключён к managed parent/cache/index/libmpv/DirectShow
или публичному SDK; прежний public mode по-прежнему отклоняет audio/B-frames.
Частные сборки не заменяют установленный worker/main EXE: 17 защищённых файлов,
INI/SDK ZIP и HKCU shell сохранены, одноразовая задача удалена. Harvester не
менялся. Audio/AAC trim, полная reorder playback/stress и исторические outliers
остаются OPEN.


5 ноября 2026, [managed B-frame профиль, кэш и настоящие декодеры](ProgressiveManagedReorder.md):
parent/cache получают явный внутренний opt-in; прежний controller/SDK его не
выбирает. Production FragmentIndex/ContinuousStreams/Classic adapter изменять
не потребовалось. По четыре physical append-only профиля GCC/MinGW/MSVC/
ASan-UBSan: 3239 точных packet/PNG и 1889 seek-кадров на сборку, sample index
≤1 ×100 ns, pause/reader isolation/confirmed EOF/filesClosed/retirement.
Настоящий Classic DirectShow software graph прошёл пять B-frame и пять no-B
сценариев с независимым NV12-эталоном. Прежние parent/cache/decoder регрессии
прошли; [exit codes, baseline и исправления oracle](ProgressiveManagedReorderValidation-2026-11-05.json).
Public mode/capability и main EXE не расширены, установленный Linux/Windows,
worker/INI/SDK ZIP и 158 HKCU shell entries сохранены; Harvester не менялся.
Планировщик не использовался. Новый профиль ещё требует controller/Player/
Window/API/lifecycle приёмки и публичной поставки; audio, общая stress/A-V
матрица и исторические outliers остаются OPEN.


5 ноября 2026, [внутренний B-frame controller/Player/API](ProgressiveReorderPlayerApi.md):
выбор mode при start связан с immutable job/source/playback ticket, а не с
текущим глобальным заданием. Mode в replay fingerprint, смена в update запрещена;
следующее задание без mode сохраняет legacy и отвергает B-frames. По 18 новых,
17 прежних playback и пять SDK-сценариев каждой редакции; семь Qt CTest-групп
каждой ОС. Classic использует приватный EXE и guarded INI. [SHA, отказы и
установка](ProgressiveReorderPlayerApiValidation-2026-11-05.json).
Linux EXE/worker обновлены атомарно с backup, INI и 309 прежних файлов сохранены;
проверен установленный EXE с частным профилем. Windows-установки/SDK ZIP/Harvester
и 158 HKCU shell entries сохранены, одноразовая задача удалена.
Один Windows Qt reader-release receipt FAIL с QTRY timeout 5000 ms не объяснён;
добавлена диагностика, три целевых и полный повторы прошли без смены порога.
Этот outlier, прежние Linux >2 s/Classic heartbeat-12, общие stress/A-V остаются
OPEN. Capability/Python SDK пока объявляют только legacy; публичный reorder
contract/SDK/пакеты и fractional/VFR UI — следующий этап.


5 ноября 2026, [публичный opt-in SDK 1.83](ProgressiveReorderedSdk.md):
capability двух профилей и keyword-only mode для continuous H.264 B-frames.
Legacy default/payload и его запрет B-frames сохранены; новый режим проверяет
capability до start и связывает ответы с неизменным mode/job/playback identity.
23 unit checks и по 34 реальных SDK/API сценария каждой редакции, включая
неизменённый SDK 1.82; отдельные staged SDK проверки импортируют модули из
integration с фиксацией SHA. В тестовом запускателе исправлен перехват imports
исходным деревом; прежние неприкреплённые old/staged результаты заменены.
UI proof проверяется одним успешным snapshot, registry busy воспроизведён native
lock; production reader code/порог 5000 ms не менялись. Linux UI и четыре финальных
Windows UI прогона прошли. [Полные данные](ProgressiveReorderedSdkValidation-2026-11-05.json).
Исходный Windows receipt outlier, Linux >2 s/Classic heartbeat-12, audio и общая
stress/A-V приёмка остаются OPEN. Финальные архивы/публикация/обновление Linux
учитываются отдельно в чеклисте; Harvester и Windows-установки не менялись.


5 ноября 2026, [поставка SDK 1.83](ProgressiveReorderedSdkPackagesValidation-2026-11-05.json):
[prerelease](https://github.com/LiberVixer/FoxicMP/releases/tag/progressive-api-v1.83)
с clean SDK ZIP, Windows portable Classic/Qt, Linux tar и `.deb` 0.2.0-183.
Кодовый коммит 51c7fc9d9; приложение остаётся 0.2.0. По 318 SDK SHA; 39 реальных
сценариев окончательно распакованных пакетов и 11 установленного Linux, exit 0.
Удалён созданный тестами Python cache из draft packages, проверки повторены без
записи bytecode. Все семь server digests/size GitHub совпали; SDK и оба манифеста
скачаны обратно (полные бинарные архивы повторно не скачивались). Linux обновлён
атомарно с rollback, пользовательский INI и неменявшиеся файлы сохранены.
Windows-установки/старые SDK/Classic build control files и 158 HKCU shell entries
сохранены. Задача повторной проверки создавалась после удаления предыдущей;
обе одноразовые фазы завершены и удалены. Harvester не менялся. Audio, public
fractional/VFR UI, длинный stress/crash/latency и исторические outliers OPEN.


5 ноября 2026, [fractional/VFR через публичный SDK 1.83 и обычные окна](ProgressiveReorderedSdkTiming.md):
по четыре 24-секундных fixture (25, 30000/1001, 60000/1001, VFR) в Linux Qt,
Windows Qt и Classic; физический append-only, unpublished-tail bounds, пауза
при росте, complete и 36 seek с допуском интервала кадра +10 ms. По 3239 точных
packet records и decoded frames managed cache на редакцию, 12 EOF/release/forget.
Classic отдельно сохраняет и подтверждает начальный буфер 10 s (4/8 s ожидает,
после 12 s играет), повторный штатный порог остаётся 5 s. Первый неверный timeout
этого теста и отсутствие ffprobe в test PATH сохранены. [SHA, exit codes, измерения](ProgressiveReorderedSdkTimingValidation-2026-11-05.json).
Менялись только новый тест/CTest и документы. Проверен установленный Linux без
замены EXE: все 572 защищённых файла, immutable SDK/release assets, Windows
установки и 158 shell entries совпали; одноразовая задача удалена. Харвестер
не менялся. Это функциональная приёмка перечисленных public SDK/Player профилей,
не физическая экранная/A-V синхронизация или полная waiting/crash/stress матрица;
исторические outliers и audio/remux остаются OPEN.


5 ноября 2026, [первый внутренний continuous H.264/AAC worker](ProgressiveContinuousAacAligned.md):
частный `continuous-h264-aac-aligned-v1`, ровно одна H.264 и одна AAC-LC дорожка,
44,1/48 kHz mono/stereo, начальный skip и окончание на границе 1024 samples.
Copy-remux сохраняет рациональные clocks без независимого обнуления; movie clock
равен точному НОК timebase. По 17 новых и 20 прежних worker сценариев GCC,
MinGW, MSVC и ASan/UBSan; по 11 video-only SDK playback случаев трёх редакций
с частными новыми worker. [SHA, exit codes и границы](ProgressiveContinuousAacAlignedValidation-2026-11-05.json).
На Windows воспроизведены лишние 990/848 PCM frames при non-aligned AAC, простой
elst patch не помогает. Такие файлы отклоняются до output header. Для успешных
fixtures PCM сравнивается напрямую, без trim-фильтра, с проверкой ожидаемого
sample count. Linux ffprobe duration oracle исправлен на чтение trex/tfhd/trun,
исходный KeyError сохранён. Source parent/cache/AV decoder и публичная AAC
capability не подключены; это внутренний компонент, не готовый audio playback.
Все 572 защищённых файла и 158 shell entries совпали; задача Windows удалена.
Main EXE, установленные Linux/Windows worker/INI и immutable SDK ZIP сохранены;
новой пользовательской версии нет. Harvester не менялся. Sample-window trim,
AV adapters, full stress/A-V и исторические outliers остаются OPEN.


6 ноября 2026, [точное AAC PCM-окно](ProgressiveContinuousAacWindow.md):
частный `continuous-h264-aac-window-v1` снимает ограничение окончания на границе
1024 samples. Общая геометрия различает raw decoder и presentation origins;
worker требует SHA-подтверждения плана до медиабайтов. Реальный Qt Player
на Windows/Linux и внутренний AAC decoder Classic дают точные PCM-границы.
По 53 worker cases и 9830 geometry checks в GCC/MinGW/MSVC/ASan-UBSan;
32 Qt captures, 16 Classic trim captures, шесть raw captures, 12 lifecycle
checks, шесть truncated rejections и 40 MP3 регрессий. Прежние 33 SDK cases
трёх редакций прошли. Исправлена очередь interleave при одном полезном AAC sample
только в новом режиме; исправление startup warmup теста не повышает CPU budget.
[Отчёт с исходными отказами и SHA](ProgressiveContinuousAacWindowValidation-2026-11-06.json).
Это частные worker/PCM adapters, ещё без managed AV parent/cache/index,
настоящих continuous AV entry points/DirectShow control и публичной audio capability.
Следующий этап — их подключение и growing A/V/seek/pause/EOF/reader приёмка,
затем SDK/пакеты. Физическая A/V, полный stress и прежние outliers остаются OPEN.
Все 576 защищённых файлов и 158 shell entries совпали, одноразовая задача удалена.
Установленные main EXE/worker/INI, исходная MpaDecFilter.lib и SDK ZIP сохранены;
новой пользовательской версии нет. Harvester не менялся.


6 ноября 2026, [внутренний managed AAC parent/cache/index](ProgressiveManagedAac.md):
явный `H264AacWindow` требует SHA-подтверждения неизменяемого PCM-плана до
медиабайтов. Кэш проверяет две дорожки, clocks, AAC geometry/packet count и полное
покрытие payload; только частный `Layout::ContinuousAv()` допускает один traf
после окончания короткой дорожки. Старые shared fMP4 ограничения сохраняются.
По 22 новых и 31 прежнему cache case GCC/MinGW/MSVC/ASan-UBSan, 212 PASS/exit 0;
физический append-only source, unpublished tail, independent leases, cancel
0,016–0,047 s и zero-budget cleanup. [SHA, первоначальные отказы и границы](ProgressiveManagedAacValidation-2026-11-06.json).
Это native parent/cache с FFmpeg PCM reference, без настоящего continuous AV
Player/DirectShow graph. Существующий video-only decoder source отвергает новый
mode до подключения PCM adapters. Следующий этап — настоящие decoder sources,
AAC control/re-arm после flush/seek, growing A/V/pause/seek/EOF/reader приёмка,
затем public controller/API/SDK и пакеты. Физический A/V, полный stress и прежние
outliers остаются OPEN. Все 576 защищённых файлов и 158 HKCU entries совпали.
Main EXE/worker/INI/SDK ZIP не обновлялись: новой пользовательской версии нет.
Harvester/рабочий стол/микшер не менялись; scheduled tasks не создавались.


### 6 ноября 2026: внутренние continuous AAC decoder adapters

[Qt Player и Classic DirectShow graph](ProgressiveContinuousAacDecoder.md) подключены
к managed AV cache через явный PCM-window opt-in. Default entry и публичный SDK
1.83 сохраняют прежние video-only profiles. Отдельный AAC COM control, два
защищённых fMP4 view, re-arm после flush/seek и отсутствие повтора последнего
sample при seek за конец короткого звука. Qt использует две protected проекции
с независимым audio EOF, устраняя startup stall короткой дорожки. Linux/Windows Qt — по 31 сценарию,
Classic — 37; native/cache, MP3/AAC adapters и старые no-B/B-frame graphs прошли.
[SHA, точные exit codes, ограничения seek oracles и исходные отказы](ProgressiveContinuousAacDecoderValidation-2026-11-06.json).
Физический A/V не измерялся; Qt frame export в этом стенде отсутствует.
Далее — public controller/status/UI/SDK, обычная API приёмка, расширенные
waiting/seek/replacement/failure/stress и поставка. 576 защищённых файлов и 158
HKCU shell entries сохранены. Пользовательской версии и переустановки на этом
внутреннем этапе нет; Harvester не менялся.

### 6 ноября 2026: continuous AAC через API и SDK-кандидат 1.84

[Явный AAC opt-in](ProgressiveContinuousAacApi.md) подключён к общему controller,
production Qt Window/Player и обычному Classic EXE. Новый профиль требует две
обязательные дорожки общего SHA-protected MP4 и проверенное PCM-окно до commit.
Прежние video-only defaults сохранены. В Classic подключены обе проекции и
независимый endpoint короткого AAC; пороги буфера 10/5 s не менялись.

По девять IPC сценариев каждой редакции прошли (27): growing prepare/play,
pause/publication, completed seek, EOF/Stop/audio failure и close/filesClosed/forget.
Qt использует частный host с null audio; Classic — полный EXE в интерактивной Windows.
Проверены canonical LC ASC и отказы конфигураций, а также прежний controller/SDK.
[Точные exits, SHA, исходные отказы и границы](ProgressiveContinuousAacApiValidation-2026-11-06.json).

Это source/API этап: обычные упакованные Qt main, SDK/runtime 1.84 prerelease и
установка Linux ещё открыты. Published SDK 1.83 и установленные профили сохранены.
Полная AAC waiting/crash/race/stress матрица и прежние latency outliers не закрыты.

### 6 ноября 2026: обычные приложения, пакеты AAC 1.84 и Classic seek race

[Проверка обычных main и упаковки](ProgressiveContinuousAacPackages.md):
60 сценариев AAC/default/reordered трёх редакций, 20 чистого .deb,
18 распакованных Windows portable, 9 установленного Linux и 90 повторов
исправленной Classic прошли. Финальные 107 сценариев stage/.deb/архивов/установки закрепляют все десять
SDK-модулей за пакетом, включая Windows transport. Ранние FAIL сохранены.
Причина Classic completed-seek отказа — запоздавший локальный fragment index
при уже завершённом verified cache. CanSeek использует подтверждённое
completed-состояние кэша; частный fault test с задержкой 800 ms воспроизводит
старый отказ и проходит с исправлением на том же MP4, ещё четыре сценария.
В поставляемом EXE нет диагностических hooks. Debian packager отклоняет
Python bytecode; чистый пакет пересобран и проверен. Linux обновлён атомарно
с backup и неизменным INI; Windows установки и 158 HKCU shell entries сохранены.
[Точные exits, SHA, исходные отказы и границы](ProgressiveContinuousAacPackagesValidation-2026-11-06.json).
Полная AAC/crash/stress, физический A/V и стабильная ТЗ-приёмка остаются открытыми.
