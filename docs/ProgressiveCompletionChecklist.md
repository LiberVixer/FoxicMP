# FoxicMP: чеклист завершения progressive-ТЗ

Основание: [ТЗ от 25 октября 2026](ProgressiveTechnicalSpec-2026-10-25.ru.md),
[история реализации](ProgressiveIntegrationPlan.md), [семейный план](FoxicMPFamilyPlan.md).
Текущий проверенный срез — SDK 1.82, приложение 0.2.0. Этот документ — перечень
работ и критериев; пустой checkbox не означает отсутствие функции у обычного
воспроизведения завершённых файлов. Проверяется именно progressive-сценарий.

Статусы: `[x]` — выполнен указанный узкий результат с доказательством;
`[ ]` — остаётся открытым. Начатый пункт отмечается текстом, без преждевременного `[x]`.
Все применимые пункты проверяются отдельно в Linux Qt, Windows Qt и Classic.

## Проверенная основа

- [x] Совместимые v1 start/update/complete и исправление sessionDirectory Classic — [проверки](ProgressiveV1Validation-2026-10-25.md).
- [x] Общий SDK/client, OS discovery/IPC и Windows Qt legacy/provider adapter в ограниченном профиле — [проверки](ProgressiveSdkValidation-2026-10-25.md).
- [x] Модель v2, подтверждённые ranges, resource I/O и reader receipts — [контракт](ProgressiveResourceIo.md).
- [x] Объявленные SHA-256 blocks и проверка перед чтением — [контракт и границы](ProgressiveIntegrity.md).
- [x] Проверенные ограниченные separate/segmented fMP4/WebM/TS профили — [матрица](FoxicMPFamilyMatrix.md).
- [x] Shared growing AVC/AAC fMP4 и AVC/AAC TS — [fMP4](ProgressiveMuxedFmp4.md), [TS](ProgressiveGrowingTs.md).
- [x] Shared growing WebM VP8/VP9/AV1 с Opus/Vorbis — [SDK 1.46 и проверки](ProgressiveMuxedWebmVorbisValidation-2026-10-31.md).
- [x] Ограниченные multi-audio, поздние полные UTF-8 SRT, metadata/chapters и optional track isolation — [история](ProgressiveIntegrationPlan.md).

## 1. Матрица общего растущего fMP4 — завершён проверенный ограниченный профиль

- [x] HEVC Main/Main10 + AAC: 8/10-bit, hvc1/hev1, B-frames, fractional fps, edit lists, partial fragments — [SDK 1.47 и проверки трёх редакций](ProgressiveMuxedFmp4HevcValidation-2026-10-31.md).
- [x] AV1 Main 8/10-bit + AAC: fractional fps, edit lists, partial fragments, SHA/leases — [SDK 1.48 и проверки трёх редакций](ProgressiveMuxedFmp4Av1Validation-2026-10-31.md).
- [x] VP9 Profile 0/2 8/10-bit + AAC: fractional fps, edit lists, partial fragments, SHA/leases — [SDK 1.49 и проверки трёх редакций](ProgressiveMuxedFmp4Vp9Validation-2026-10-31.md).
- [x] H.264/HEVC/VP9/AV1 + Opus mono/stereo 48 kHz, 8/10-bit и fractional profiles — [SDK 1.50 и проверки трёх редакций](ProgressiveMuxedFmp4OpusValidation-2026-10-31.md).
- [x] Для каждой пары: explicit capability, native bytes/clock/decoded hashes и playback/buffer/pause/seek/complete/release трёх редакций — отчёты SDK 1.42, 1.47–1.50 выше.

## 2. Общий demux и локальная подготовка — начат

- [x] Первый этап: bounded FFmpeg AVIO **диагностика** подтверждённых ресурсов,
  11 format hints, отдельный процесс, ranges/SHA/wait/update/cancel/release,
  одинаковый API трёх редакций — [SDK 1.51](ProgressiveBackendDemux.md),
  [проверки](ProgressiveBackendDemuxValidation-2026-10-31.md).
  SDK 1.51 — только диагностика; нижеследующие общие пункты остаются открытыми.
- [x] SDK 1.52: [copy-remux ограниченного MP4-фрагмента в памяти](ProgressiveRemuxStage.md),
  H.264/AAC-LC, ranges/SHA, ожидание/update/cancel/forget и точный audio trim.
  [Проверки](ProgressiveRemuxStageValidation-2026-10-31.md). Это локальная подготовка,
  без подключения нового playback и без непрерывной подготовки всего ролика.

- [x] SDK 1.53: [явный playback подготовленного RAM MP4-фрагмента](ProgressivePreparedPreview.md)
  в трёх редакциях: H.264/AAC-LC, pause/seek/cancel, read-only recovery по requestId,
  фактическое закрытие readers. [Проверки](ProgressivePreparedPreviewValidation-2026-10-31.md).
  `finished` фрагмента отличается от EOF исходника; непрерывного remux ещё нет.

- [x] Первый внутренний компонент непрерывного remux: [один persistent FFmpeg worker H.264 video-only → fMP4](ProgressiveContinuousRemux.md), две 24 s фикстуры по 600/720 packets, incremental output до source complete, ожидания с тем же demux context, exact packet/frame hashes, clocks и cancel/error guards. По 9 native сценариев Windows/Linux и 9 ASan/UBSan; [результаты](ProgressiveContinuousRemuxValidation-2026-11-03.json). Это engine-only: ResourceSource/leases/cache и обычный API Qt/Classic ещё не подключены, новая capability не объявлена; audio/B-frames и общий remux остаются открытыми.

- [x] Внутренний управляемый continuous-remux parent: отдельный поток, ResourceSource/SHA/ranges, writer/session identity, heartbeat/errors, Windows Job, раздельные lifetime/pipe deadlines и source leases. По 17 native сценариев GCC/MinGW/MSVC/ASan-UBSan; исправлен worker-death wait, повторена совместимость обычного remux API всех трёх редакций, Linux обновлён с backup. [Отчёт](ProgressiveContinuousRemuxParentValidation-2026-11-04.json). Output cache, decoder reader group, playback bridges и публичный API этим не закрыты.

- [x] Ограниченный внутренний output store непрерывного remux: private disk cache/shared budget, atomic moov/moof+mdat visibility, FragmentIndex, SHA on read, original/output reader leases и cleanup после подтверждённого close. По 31 сценарию GCC/MinGW/MSVC/ASan-UBSan, включая cross-process readers, очередь retirement с пределом 16 store, quotas, corruption, worker crash, cancel и 600 packet/frame hashes — [отчёт](ProgressiveContinuousRemuxCacheValidation-2026-11-04.json). Это native component: настоящие decoder/API adapters, orphan recovery после перезапуска и общая cache/crash matrix ещё открыты; main EXE/SDK не менялись.

- [x] Ограниченное восстановление private cache после смерти owner: immutable atomic journal, PID/birth + locks, physical/creation identities, source/output reader proofs, quarantine и startup admission до нового budget. По 33 recovery rows Linux GCC/ASan-UBSan, по 31 Windows MinGW/MSVC, повторены cache/parent/engine регрессии; исправлена live-очистка по подменённому registry, baseline failure сохранён — [отчёт](ProgressiveContinuousRemuxRecoveryValidation-2026-11-04.json). Это не восстановление playback/FFmpeg context, не decoder/API интеграция и не полная cache/crash/power-loss приёмка.

- [x] Первые внутренние decoder views: блокирующий cache cursor, настоящий libmpv callback adapter и Classic FragmentIndex/sample view. По 11 сценариев GCC/MinGW/MSVC/ASan-UBSan; по 600 кадров совпали с completed-source libmpv reference, проверены growing ResourceSource→worker→cache, manual pause, cancel/EOF/errors, совместные readers и close; повторены 31 cache regression Windows/Linux — [отчёт](ProgressiveContinuousRemuxDecoderValidation-2026-11-04.json). Это native fixture: установленный Player/API и DirectShow graph не подключены, audio/B-frames и общая приёмка открыты.

- [x] Внутренние playback entry points continuous-remux в настоящем Qt Player/MpvLifetime и Classic CHarvesterSession/DirectShow. По пять сценариев Qt каждой ОС и пять Classic graph; 600 NV12 кадров совпали с независимым FFmpeg original decode, completed seek дал 350 кадров, pause/stop/error/close/receipts проверены. Повторены Qt retirement и прежний API 24 preparation/20 playback на ОС, Linux атомарно обновлён с сохранением INI/файлов — [отчёт](ProgressiveContinuousRemuxPlaybackValidation-2026-11-04.json). Публичного continuous API/controller/capability ещё нет; installed Classic UI/API, audio/B-frames и общая приёмка этим не закрыты.

- [x] Общий внутренний async controller continuous-remux: replay/fences, отдельные accepted/applied revision/heartbeat, очередь ≤8 pending updates, быстрые status/cancel/destruction и удержание cache/root до настоящего reader close. По 17 native сценариев GCC/MinGW/MSVC/ASan-UBSan; cache/recovery regression Windows/Linux — [отчёт](ProgressiveContinuousRemuxControllerValidation-2026-11-04.json). Обычные endpoints/UI commit/capability пока не подключены; main EXE/установки/SDK не менялись.

- [x] Шесть внутренних команд подготовки continuous-remux через обычный IPC всех трёх редакций, без playback commit и без изменения advertised capabilities/SDK. По шесть IPC сценариев на редакцию, прежние 24 preparation/20 playback; исправлен heartbeat после producer EOF, по 18 native сценариев четырёх сборок с 12 completion cycles — [отчёт](ProgressiveContinuousRemuxApiValidation-2026-11-04.json). Все main EXE пересобраны, Linux обновлён с backup без изменения INI/личных файлов; Windows-установки/HKCU и Harvester сохранены. Worker пока только в тестовых копиях.
- [x] Внутренний continuous-remux playback commit/control/status через обычный IPC, intent fencing при Stop/замене/закрытии: по 14 сценариев Qt ОС и 15 Classic, две Qt Window/API lifecycle проверки на ОС, 11 native decoder ASan/UBSan. Исправлены prefix-duration seek Qt и преждевременная отмена текущего просмотра при подготовке замены; проверены EOF/receipts, Classic normal exit 0 и повторный запуск. Все main EXE пересобраны, Linux обновлён с backup без изменения профиля — [отчёт](ProgressiveContinuousRemuxPlaybackApiValidation-2026-11-04.json). Пока только внутренний узкий H.264 video-only профиль; capability/SDK/worker пользователю не поставлены.
- [x] Ограниченный continuous output `readerRelease` adapter всех трёх редакций: точная привязка квитанции к output resource/file identity и попытке просмотра, остановка при playing/pause/ожидании хвоста, EOF/replay и изоляция старых/чужих запросов. Qt отдельно проверяет задержанный registry I/O, замену видео и удерживаемого дополнительного reader; положительный release требует фактического close — [отчёт](ProgressiveContinuousRemuxReaderReleaseValidation-2026-11-04.json). Это внутренний H.264 video-only профиль, не полная lifecycle/stress приёмка.
- [x] SDK 1.82: публичный ограниченный `continuousRemux` contract, явный `ContinuousRemux` helper, read-only восстановление job/playback после потери ответа и поставка worker/runtime. По пять SDK IPC сценариев каждой редакции, 17 SDK unit checks, Qt portable Windows и извлечённый Linux .deb проверены реальным SDK; worker ABI/права/dependencies и сохранение профиля проверяются упаковкой — [контракт](ProgressiveContinuousSdk.md), [отчёт](ProgressiveContinuousSdkValidation-2026-11-04.json). Только MP4/H.264 video-only без B-frames; весь ТЗ этим не закрыт.
- [x] Ограниченный публичный continuous SDK waiting/replay срез: реальные паузы writer 10/30/60 s, тот же worker и decoder readers, ручная пауза при append, complete forward/reverse seek и EOF, Stop/Cancel у хвоста и три повтора старой команды. По пять сценариев каждой редакции, resume 0,20–0,41 s и API <0,23 s; [методика](ProgressiveContinuousWaiting.md), [exit codes и результаты](ProgressiveContinuousWaitingValidation-2026-11-04.json). В первом срезе найден Qt status `playing` при ожидании; последующее исправление проверено ниже. В исходном срезе production EXE, INI, shell registration, SDK ZIP и Harvester сохранены.
- [x] Qt continuous buffering status/UI: реальный `paused-for-cache` учитывается в API; ручная пауза приоритетна в API/строке состояния/шаблоне заголовка; дополнительный optional `reasonCode` различает ожидание/загрузку/паузу/завершение/ошибку. Новый настоящий Window/API тест и по пять усиленных SDK waiting сценариев каждой Qt ОС, по 11 CTest групп, raw playback 17 с `writer-failed`; [методика](ProgressiveContinuousBuffering.md), [отчёт](ProgressiveContinuousBufferingValidation-2026-11-04.json). Baseline exit 8, финальные exit 0, resume 0,20–0,26 s. Linux обновлён с backup без изменения INI/278 других файлов. Classic/Windows-установки, 158 shell entries, SDK ZIP и Harvester сохранены. Только проверенный MP4/H.264 video-only continuous профиль; общие reasonCodes всех режимов не закрыты.
- [x] Ограниченная [heartbeat-семантика публичного continuous SDK](ProgressiveContinuousHeartbeat.md): по пять сценариев трёх редакций, настоящий срок 30000 ms, status не продлевает writer, timeout не EOF, late events отклоняются, complete не требует heartbeat. Исправлено удержание paused Qt decoder; [результаты, исходные отказы и SHA](ProgressiveContinuousHeartbeatValidation-2026-11-04.json).
- [ ] Локализовать единичное превышение 2 s автоматического release source/cache на паузе в исправленной Linux-сборке. Последующие пять heartbeat-сценариев прошли, но причина первого отказа неизвестна; строгий бюджет не принят. Добавлены [шесть повторов каждой Qt ОС и status-only trace](ProgressiveContinuousReleaseLatencyValidation-2026-11-04.json): все прошли, первоначальная причина остаётся открытой. Дополнительно выполнены [полные последовательности на исходном и текущем Linux EXE](ProgressiveContinuousObservation.md): 20 сценариев прошли, но старый отказ не объяснён.
- [x] Ограниченное [закрытие/crash continuous player и явный повторный запуск](ProgressiveContinuousShutdown.md): по восемь сценариев каждой редакции при preparation/playing/paused/buffering, реальный выход worker, сохранение независимого просмотра, физическое продолжение записи writer, recovery прежнего кэша и новые reader IDs; [отчёт](ProgressiveContinuousShutdownValidation-2026-11-04.json). Нормальные receipts — files-closed; аварийные — доказанный process-exited. Позиция/пауза автоматически не восстанавливаются.
- [x] Ограниченная [очистка continuous cache при нормальном выходе](ProgressiveContinuousExitCleanup.md): общий final-exit wait ≤1500 ms после уничтожения GUI/API/decoder; по восемь усиленных сценариев каждой редакции, все двенадцать нормальных выходов без оставшегося кэша. Native тест удерживает reader: timeout не удаляет живой кэш, после close очистка завершается. [SHA и результаты](ProgressiveContinuousExitCleanupValidation-2026-11-04.json). Произвольно зависший I/O и полный teardown бюджет этим не закрыты.
- [x] Ограниченная [авария continuous worker через публичный SDK](ProgressiveContinuousWorkerCrash.md): по четыре сценария каждой редакции при preparation/playing/paused/buffering. Только identity-checked дочерний worker, OS exit, status-only automatic release без Stop, реальные filesClosed receipts и удаление кэша; независимый просмотр и физическое дописывание partial сохранены, явный retry в том же player с новыми IDs. [Все 12 результатов, SHA и исходная ошибка стенда](ProgressiveContinuousWorkerCrashValidation-2026-11-04.json). Production code и EXE не менялись; mid-I/O/races и общий crash/stress этим не закрыты.
- [x] Ограниченные [гонки public continuous-команд и замены просмотра](ProgressiveContinuousCommandRaces.md): по 16 сценариев каждой редакции, публикация/Stop, публикация/замена, readerRelease/замена, cancel/замена; поздние старые команды не прерывают новый и независимый просмотр. Исправлена ложная ошибка MP4-заголовка при contention реестра readers; детерминированный baseline exit 1, финальные native и IPC прогоны exit 0. На Qt ОС по 13 основных и две multi-reader CTest-группы, по 469 native media checks; [измерения и SHA](ProgressiveContinuousCommandRacesValidation-2026-11-04.json). Linux обновлён с backup и сохранением профиля; все schedule/mid-I/O/stress этим не закрыты.
- [x] Ограниченные [faults незавершённого pipe I/O](ProgressiveContinuousPipeFaults.md): по 12 native сценариев GCC/MinGW/MSVC/ASan-UBSan, всего 48, все exit 0. Перед смертью child, cancel и timeout доказан partial receive или backpressure send; filesClosed receipts, пустой незавершённый output, bounded reads и SHA проверены. Это синтетический child с настоящим managed transport/ResourceSource; реальный FFmpeg file-I/O/cache/decoder в этой точке остаётся открытым. Повторены worker-crash SDK сценарии всех трёх редакций; [отчёт](ProgressiveContinuousPipeFaultsValidation-2026-11-05.json).
- [x] Ограниченные [аварии настоящего FFmpeg внутри AVIO](ProgressiveContinuousAvioFaults.md): по восемь SDK сценариев трёх редакций, всего 24. Instrumented test worker останавливается внутри real read/write callback после начала playing/manual pause; в 12 случаях surviving external reader удерживает настоящий кэш до filesClosed. Decoder/source закрываются, незавершённый output не принимается, кэш удаляется после последнего close; independent playback, SHA, late events и explicit retry проверены. Девять ASan/UBSan engine регрессий; [SHA, exit codes и границы](ProgressiveContinuousAvioFaultsValidation-2026-11-05.json). Production main/worker/профили сохранены; OS disk/parent file-I/O и полный stress открыты.
- [x] Ограниченные [syscall-result faults parent I/O](ProgressiveContinuousFileIoFaults.md): 14 GCC/Linux, 11 MinGW/Windows, 11 MSVC/Windows, 14 ASan/UBSan; source read error/short read, cache error/disk-full/physical partial write, внешние readers и POSIX EINTR/short-write controls. Исправлена ложная `source-rewritten` при short read: теперь `source-truncated`, реальные изменённые bytes сохраняют прежний code. Строгий baseline exit 1 сохранён; [native/GUI регрессии, SHA и установка](ProgressiveContinuousFileIoFaultsValidation-2026-11-05.json). Настоящий зависший OS I/O/полный stress этим не закрыты.
- [ ] Ограниченный [continuous lifecycle stress и paused expiry](ProgressiveContinuousLifecycleStress.md): итоговый повтор по 100 измеряемых +4 warmup cycles и 12 настоящих 30000 ms timeout каждой редакции прошёл прежние budgets. Первый Classic resource-budget FAIL после heartbeat 12 остаётся необъяснённым; baseline/неуспешное значение тогда не записаны. В новой диагностике дважды воспроизведён устойчивый рост Event на пути внешнего MPCVR; его связь со старым heartbeat-12 отказом ещё не доказана. Стенд теперь сохраняет их до assert и диагностирует последующую очистку, не меняя пороги. [Первичный отказ и результаты повторов](ProgressiveContinuousLifecycleStressValidation-2026-11-05.json). Исходный Linux >2 s, длительная растущая continuous загрузка и общий stress остаются OPEN.
- [x] Диагностический срез [пассивного наблюдения](ProgressiveContinuousObservation.md): 680 Classic cycles в восьми сериях, два реальных resource-budget FAIL с ростом Event на пути внешнего MPCVR; EVR/Null контроли без роста. Захват исходной ошибки, типов/modules и локального minidump проверен отдельно. Две полные heartbeat последовательности на старом и текущем Linux EXE дали 20 PASS сценариев. [Доказательства](ProgressiveContinuousObservationValidation-2026-11-05.json). Это завершённая диагностика ограниченного среза, не исправление утечки; два открытых пункта выше не закрываются.
- [x] Исправлен конкретный воспроизводимый [рост DWM Event в Classic](ProgressiveDwmEventLifetime.md): первый MPCVR redraw перенесён до Run/Pause в постоянный UI-поток. Три исходных instrumented FAIL с 104 сохраняющимися Event, затем три успешных 100-cycle серии исправления, 12 настоящих heartbeat expiry (release ≤0,296 s), обычный Stop/Play и 16 command-race сценариев. [Результаты и SHA](ProgressiveDwmEventLifetimeValidation-2026-11-05.json). Без переключения renderer/профилей и без audit code в обычном EXE. Исторические Classic heartbeat-12 и Linux >2 s остаются OPEN; SDK ZIP 1.82 и установки сохранены, исправление пока в исходниках и приватном проверенном Classic build.
- [x] Убрана зависимость [освобождения файлов декодера от libmpv close callback](ProgressiveDecoderRetirement.md). Native baseline удерживает cursor после ошибки и падает по 2 s; фоновый контроллер теперь закрывает только decoder readers, сохраняя independent leases и complete. По четыре native случая и 12 групп каждой Qt ОС; Classic 100+4 cycles и пять heartbeat-сценариев, все exit 0. [Измерения, исходные отказы и установка](ProgressiveDecoderRetirementValidation-2026-11-05.json). Linux обновлён с backup, INI/309 других файлов сохранены. Идентичность этого механизма историческому Linux >2 s не доказана; прежний открытый пункт не закрывается.
- [x] Ограниченный [sustained growing stress одного файла и двух viewers](ProgressiveGrowingStress.md): по 129–131 s каждой редакции, физический append-only writer, настоящий 60 s stall с heartbeat, ручная пауза, закрытие/replay первого endpoint без остановки второго и прежний worker второго до complete. Шесть packet comparisons по 3200 payload/PTS/DTS/duration, complete/seek/EOF/filesClosed/forget и прежние resource/request/release budgets прошли, exit 0. [Результаты и SHA](ProgressiveGrowingStressValidation-2026-11-05.json). Test-only: Linux EXE/INI, Windows-установки, SDK ZIP, Harvester и 158 shell entries сохранены. Это двухминутный video-only срез; многочасовой stress, A/V/B-frames и прежние необъяснённые отказы не закрываются.
- [ ] Полные waiting/replay/shutdown/crash/stress проверки публичного continuous-remux SDK трёх редакций. Далее — Linux release latency, зависший OS I/O/cancellation и расширенная race/stress matrix.

- [ ] Проверенные backend demuxers через управляемый local I/O; платформенные адаптеры Qt/Classic.
- [ ] Обычные MP4/M4A/MOV, WebM/Matroska, MPEG-TS и их допустимые варианты вне нынешних узких профилей.
- [ ] Явные native-progressive, progressive-remux, waiting, post-download и unsupported результаты.
- [ ] Remux только подтверждённых локальных данных, без перекодирования/ухудшения качества и без изменения оригиналов.

## 3. Обычный MP4 и индексы

- [x] SDK 1.54: ограниченный [fast-start MP4 с обычными sample tables](ProgressiveIndexedMp4.md), H.264/AAC-LC, один ресурс/две дорожки; [проверки](ProgressiveIndexedMp4Validation-2026-11-01.md).
- [x] SDK 1.55: [ограниченный tail-moov MP4 H.264/AAC-LC](ProgressiveTailMp4.md), ожидание индекса и advisory ranges через metadata-only API; [проверки трёх редакций](ProgressiveTailMp4Validation-2026-11-01.md). Произвольные sparse media остаются в пункте 5.
- [x] В указанном профиле SDK 1.54: старт до complete, ожидание неполного sample, resume с тем же readerId; отсутствие готового head index не становится успешным fallback. Прочие MP4 layouts остаются открытыми.
- [x] SDK 1.56: [перемотка после complete](ProgressiveCompletedSeek.md) в активной сессии H.264/AAC-LC: отдельные fMP4 и обычный head/tail MP4; тот же reader, pause/speed/volume/mute/основная audio сохранены. [Проверки трёх редакций](ProgressiveCompletedSeekValidation-2026-11-01.md). Остальные layouts/codecs и повторный просмотр после освобождённого EOF этим пунктом не закрываются.

## 4. Аудио и самостоятельные потоки

- [ ] Полная согласованная audio codec matrix: AAC-LC/HE-AAC, Opus, Vorbis, MP3, FLAC, PCM, AC-3/E-AC-3.
- [ ] ADTS AAC, MP3, Ogg, FLAC, WAV/PCM; дополнительные rates/layouts и многоканальный звук.
- [x] SDK 1.57: [самостоятельные AAC-LC и H.264 в scalar fMP4](ProgressiveStandalone.md), один настоящий required track/resource без ожидания второго; [проверки трёх редакций](ProgressiveStandaloneValidation-2026-11-01.md). Прочие standalone codec/container profiles остаются открытыми.
- [x] SDK 1.58: [standalone Opus mono/stereo fMP4](ProgressiveStandaloneOpus.md); dOps/edit list, buffering/pause/seek/complete/release проверены отдельно на трёх редакциях. WebM/Vorbis и полная codec matrix остаются открытыми.
- [x] SDK 1.59: [standalone WebM Opus/Vorbis mono/stereo](ProgressiveStandaloneWebm.md), отдельная вложенная capability, packet/header validation и испытания трёх редакций. Standalone WebM video и полная codec matrix остаются открытыми.
- [x] SDK 1.60: [standalone WebM VP8/VP9/AV1 video](ProgressiveStandaloneWebmVideo.md), шесть 8/10-bit и fractional profiles на трёх редакциях. Остальные codec/container combinations остаются открытыми.
- [x] SDK 1.61: [standalone HEVC/AV1/VP9 fMP4 video](ProgressiveStandaloneFmp4Video.md), шесть 8/10-bit/fractional profiles на трёх редакциях; остальные codec/container combinations остаются открытыми.
- [x] SDK 1.62: [standalone FLAC fMP4](ProgressiveStandaloneFlac.md), восемь mono/stereo, 16/24-bit, 44.1/48-kHz profiles; PCM bit-exact после bridge. Raw FLAC и остальные audio containers/matrix остаются открытыми.
- [x] SDK 1.63: [native standalone FLAC](ProgressiveRawFlac.md), fixed blocks, mono/stereo, 16/24-bit, 44.1/48-kHz; CRC/SHA, точные packet clocks и PCM, восемь runtime profiles каждой редакции. Остальные audio containers и layouts остаются открытыми.
- [x] SDK 1.64: [standalone ADTS AAC-LC](ProgressiveAdts.md), MPEG-4 без CRC, один raw block/frame, mono/stereo, 44.1/48 kHz; pause/seek/tail/update/EOF/release на трёх редакциях. Payload/clocks и декодированный PCM сохранены; остальные ADTS variants и полная audio matrix открыты.
- [x] SDK 1.65: [standalone MPEG-1 Layer III MP3](ProgressiveMp3.md), CBR/VBR mono/stereo 44.1/48 kHz без CRC, bounded ID3, reservoir/preroll; восемь runtime profiles трёх редакций. Xing/gapless и другие MPEG audio variants открыты.
- [x] SDK 1.66: [Xing/Info/VBRI standalone MP3](ProgressiveMp3Indexed.md), отдельный encoded-sample профиль с counters и сохранением raw delay/padding; 12 runtime profiles на редакцию. VBRI fixtures независимо собраны; реальный VBRI корпус и gapless trim остаются открытыми.
- Начат gapless output adapter: [PCM-эталон](ProgressiveMp3GaplessReference.md), по 128 offline checks Linux/Windows, включая libmpv PCM output и sample-accurate `atrim`/seek. Подключение к progressive Qt/Classic, LAME tag CRC и growing приёмка открыты; API/SDK 1.66 не меняются.
- Реализован [внутренний Qt PCM adapter](ProgressiveQtPcmWindow.md): по 72 проверки настоящего `Player` на ОС, точный trim/seek, пользовательская цепочка, stop и replacement. Progressive controller и Classic output ещё не подключены; публичный gapless capability не объявлен.
- [x] SDK 1.67: [LAME Info Tag CRC и анализ границ PCM](ProgressiveMp3Tag.md), общее ядро Qt/Classic, 72 новые native checks и диагностика при growing playback. Это проверка метаданных без применения trim; gapless controller/Classic output остаются открытыми.
- [x] SDK 1.68: [gapless MP3 в Qt](ProgressiveMp3Gapless.md), audible timeline, проверенный CRC, PCM trim/seek и ожидание writer complete. По 31 new native checks, четыре growing profiles и 16 controller PCM windows на Qt ОС; Classic явно отказывает и сохраняет прежний playback. [Проверки](ProgressiveMp3GaplessValidation-2026-11-02.md). Classic output adapter и остальная MPEG matrix открыты.
- Реализован [внутренний PCM-адаптер Classic](ProgressiveClassicPcmWindow.md): 40 checks настоящего decoder и 1737 scalar checks на Windows/Linux. Это компонент без подключения progressive graph/controller; capability Classic остаётся выключенной.
- Проверен [внутренний DirectShow graph с PCM-окном](ProgressiveClassicPcmGraph.md): 16 точных окон, growing/full-prefix wait, same-graph seek, Stop/release и отказ несовместимому downstream. Публичный Classic профиль, выбор decoder/status/UI и AudioSwitcher приёмка открыты.
- [x] SDK 1.69: [обычный API Classic для gapless MP3](ProgressiveClassicMp3Gapless.md), принудительный проверенный decoder, фактический PCM status, четыре growing profiles и Stop/cancel/reopen. Повторная Qt-проверка на обеих ОС; остальные MPEG/audio profiles и общая приёмка остаются открытыми.
- [x] SDK 1.70: [растущий WAV PCM16](ProgressiveWav.md), RIFF, mono/stereo 44.1/48 kHz, известные RIFF/data sizes, побайтное сохранение PCM, partial packets и четыре runtime profiles трёх редакций. RF64/float/24-bit и остальная audio matrix открыты.
- [x] SDK 1.71: [растущий Ogg Opus](ProgressiveOggOpus.md), один logical stream mono/stereo 48 kHz, page CRC/continuations, pre-skip/gain и end trim, четыре growing profiles трёх редакций. Ogg Vorbis, chaining/multiplexing, live и остальная audio matrix открыты.
- [x] SDK 1.72: [растущий Ogg Vorbis](ProgressiveOggVorbis.md), Vorbis I mono/stereo 48 kHz, granule origin 0 без initial trim, page CRC/continuations и EOS trim, четыре growing profiles трёх редакций. Initial trim/nonzero origin, chaining/multiplexing/live и остальная audio matrix открыты.
- [x] SDK 1.73: [растущий AC-3](ProgressiveAc3.md), bsid 8, mono/stereo 48 kHz, CRC1/CRC2, неизменная конфигурация, 96 native checks и четыре growing profiles трёх редакций. Остальные layouts/rates/bsid и общая audio matrix открыты.
- [x] SDK 1.74: [растущий E-AC-3](ProgressiveEac3.md), независимый substream 0, bsid 16, mono/stereo 48 kHz, шесть блоков и CRC. Проверяются шесть битрейтов, 80 native checks и четыре growing profiles каждой редакции; dependent substreams/JOC, другие rates/layouts и общая audio matrix остаются открытыми.
- [x] SDK 1.75: [растущий WAV PCM24](ProgressiveWav24.md), legacy RIFF tag 1, mono/stereo 44.1/48 kHz, отдельная capability и in24/enda adapter. По 65 native checks и четыре growing profiles каждой редакции; все три байта PCM, clocks и RIFF padding сохранены. PCM16 не изменён; RF64/extensible/float, другие layouts/rates и полная audio matrix остаются открытыми.
- [x] SDK 1.76: [растущий WAVEFORMATEXTENSIBLE PCM24](ProgressiveWavExtensible24.md), mono/stereo 44.1/48 kHz, PCM GUID, 24 container/valid bits и явные маски 4/3. По 83 native checks с FFmpeg и независимым struct writer, четыре growing profiles каждой редакции; legacy PCM16/24 повторно проверены. Другие masks/valid bits/layouts, RF64/float и полная audio matrix остаются открытыми.
- [x] SDK 1.77: [растущий RF64 PCM16/PCM24](ProgressiveRf64.md), mono/stereo 44.1/48 kHz, обязательный ds64 и согласованные 64-bit sizes/sampleCount. По 161 native checks, восемь growing profiles каждой редакции и восемь синтетических сценариев layout/index/seek за 4 GiB. Старые RIFF-профили сохранены. Qt CLI discovery теперь compact JSON в прежнем лимите 64 KiB. Физические RF64 больше 4 GiB, float/другие layouts и полная audio matrix остаются открытыми.
- [ ] Отсутствие аудиоустройства не делает валидный video-only источник ошибочным.
- [ ] MPEG-2/MPEG-4 Part 2 для предусмотренных ТЗ загрузок/локальных входов.

## 5. Непоследовательная запись и большие ресурсы

- [x] Дополнительная проверка SDK 1.81: [предельные RF64](ProgressiveRf64Limits.md): восемь native profiles на ОС на эффективной границе 250000 packets  / 16 GiB, отказ при превышении, по два фактических предельных playback profiles каждой редакции, обратный/repeat seek и cancel. [Отчёт](ProgressiveRf64LimitsValidation-2026-11-03.json). Full-file complete/EOF на 16 GiB, остальные предельные playback profiles и длительный stress остаются открытыми.
- [x] Дополнительная проверка SDK 1.81: [физические sparse RF64 >4 GiB](ProgressiveLargeSparseRf64.md), PCM16/PCM24 stereo 48 kHz во всех трёх редакциях. Настоящие незаписанные дыры, дальний индексный seek без сканирования 4 GiB, обратный переход, отказ ложного complete, заполнение дыр, полный SHA и EOF/release — [отчёт](ProgressiveLargeSparseRf64Validation-2026-11-03.json). Те же production EXE; maximum-size/stress и остальные большие rates/layouts остаются открытыми.
- [x] SDK 1.81: [отдельный sparse RF64 audio playback](ProgressiveRf64Sparse.md), стабильные offsets, фактический seek в островок и обратно, ожидание/заполнение дыр, pause/EOF/release и cancel-at-hole. По 272 native checks на ОС и четыре runtime profiles каждой редакции — [отчёт](ProgressiveRf64SparseValidation-2026-11-03.json). Только явные новые hints, known-length/head-initialized PCM16/24; прочие контейнеры/video/live остаются открытыми.
- [x] SDK 1.80: [геометрическая RF64 packet map](ProgressiveRf64RangeMap.md), точные интервалы готовых пакетов в том числе за дырой; по 242 native checks, 1608 synthetic geometry cases и actual status трёх редакций — [проверки](ProgressiveRf64RangeMapValidation-2026-11-03.json). Это диагностика без payload I/O; island seek остаётся запрещён.
- [x] SDK 1.79: [RF64 publication вне порядка](ProgressiveRf64Ranges.md), непрерывный head prefix, ожидание дыр и same-reader resume с сохранением ручной паузы. По 229 native checks на ОС и четыре runtime profiles каждой редакции; [отчёт](ProgressiveRf64RangesValidation-2026-11-03.json). Seek в островки и общая sparse-матрица остаются открытыми.
- [x] SDK 1.78: внешний digest index для RF64 и физические PCM16/PCM24 stereo 48 kHz больше 4 GiB во всех трёх редакциях — [контракт](ProgressiveExternalIntegrity.md), [сценарий](ProgressiveLargeRf64.md), [проверки](ProgressiveLargeRf64Validation-2026-11-03.json). Это ограниченный результат; следующие общие пункты остаются открытыми.
- [ ] Карта ranges в оставшихся playback-режимах: sparse holes, out-of-order запись и доступные островки данных.
- [ ] Незаписанные дыры не читаются как нули; file size не заменяет подтверждение writer.
- [ ] Масштабируемые media indices и внешний индекс digest без огромного командного JSON.
- [ ] Быстрый поиск/старт большого каталога без полного предварительного сканирования.

## 6. Время и буферизация

- [ ] VFR, разные timebase, ненулевые/отрицательные offsets, preroll, edit lists и gapless metadata во всей матрице.
- [ ] Нужные варианты TS access units, discontinuity и rollover транспортных часов.
- [ ] Доступная граница по обязательным выбранным потокам; исходные PTS/DTS не обнуляются независимо.
- [ ] Start/resume/buffer policies, ручная пауза и восстановление синхронизации после ожидания/seek.

## 7. Конечные HLS/DASH

- [ ] Локальные init/media segments, параллельное получение, gaps, discontinuity и rebasing.
- [ ] Расширенная сегментная codec/container matrix, включая Vorbis где применимо.
- [ ] Состояние аудио между сегментами, смена init/config и безопасный доступ к ключевым кадрам.
- [ ] Длинные каталоги, ограниченные handles/cache и повторное открытие ресурсов.

## 8. Смена качества/configuration

- [ ] Контролируемая переинициализация decoder на подходящей точке декодирования.
- [ ] Сохранение позиции, pause, speed, volume и выбранных дорожек.
- [ ] Недостаточные данные новой конфигурации дают ожидание с точной причиной.

## 9. Динамические аудиодорожки

- [ ] Несколько audio в muxed и segmented источниках; динамическое добавление/отключение.
- [ ] Стабильные ID, language/label/default/forced и сохранение выбора.
- [ ] Невыбранные optional tracks не тормозят просмотр; выбранная недокачанная дорожка ждёт свои данные.
- [ ] Ошибка optional audio изолируется и объясняется; язык не подменяется молча.

## 10. Субтитры

- [ ] Растущие SRT/WebVTT/ASS/SSA/TTML, адаптеры JSON3/SRV.
- [ ] UTF-8/UTF-16/BOM и неполные cues; только завершённые события без дубликатов.
- [ ] Embedded mov_text/WebVTT/ASS/SubRip; PGS/DVD subtitles с нужными headers/palettes в локальных контейнерах.
- [ ] Styles/settings/language/default/forced; ограничения конвертации описаны без ложного обещания полного оформления.
- [ ] Безопасные bounded fonts/attachments без установки в систему и внешних XML/HTML ресурсов.

## 11. Главы и metadata

- [ ] Совместная обработка container/writer chapters, стабильные IDs, late updates без сброса позиции.
- [ ] Неизвестная финальная duration и главы за доступной границей.
- [ ] Covers/preview, названия дорожек, rotation/aspect ratio и HDR/color metadata.

## 12. Live

- [ ] Live/event HLS и dynamic DASH через локальную публикацию writer.
- [ ] Неизвестная duration, движущееся окно, отдельная целевая задержка и reconnect.
- [ ] Отсутствие сегмента/heartbeat не означает EOF; конец подтверждает writer.
- [ ] Retirement вышедших ресурсов только после освобождения всеми readers; seek за окном имеет отдельную причину.

## 13. Жизненный цикл, I/O и безопасность

- [ ] Оставшиеся UI-blocking пути probe/index/remux/read/teardown, Qt GL cleanup и обычные Classic graphs.
- [ ] Управляемая отмена ожиданий, bounded остановка внешнего процесса, фактические release/filesClosed receipts.
- [x] Ограниченный player-crash срез: [четыре RF64-профиля каждой редакции](ProgressivePlayerCrashRecovery.md), два самостоятельных процесса, принудительное завершение при playing/buffering, подтверждение выхода ОС для старых readers, сохранение второго воспроизведения, append writer и перезапуск того же профиля с новым readerId. [Отчёт](ProgressivePlayerCrashRecoveryValidation-2026-11-03.json). Writer crash, восстановление прежней позиции/паузы, прочие форматы и общая аварийная приёмка этим не закрыты.
- [x] Ограниченный writer-failure срез: [RF64 PCM16/24, crash после неполного кадра и инъекция ENOSPC](ProgressiveWriterFailures.md), восемь сценариев каждой редакции. ≥16 s без heartbeat не дают EOF/release живого reader; новый writer проверяет байты и явно открывает generation 2; ENOSPC остаётся ошибкой, контрольная сессия независима. [Отчёт](ProgressiveWriterFailuresValidation-2026-11-03.json). Настоящий disk-full, syscall/power-loss, durable checkpoint, прочие форматы и общая аварийная приёмка открыты.
- [x] Ограниченный final-file срез: [rename RF64 при активных readers и complete после rename](ProgressiveFinalFileTransition.md), четыре SHA-защищённых профиля каждой редакции, неизменное логическое имя, отказ подменённому пути, по восемь natural EOF и открытий итогового имени в generation 2 на редакцию. [Отчёт](ProgressiveFinalFileTransitionValidation-2026-11-03.json). Прочие контейнеры, большие/внешние индексы, межтомные moves, коллизии и durable rename остаются открытыми.
- [ ] Writer/player crash, resume по идентичности bytes, несколько readers, rename и переход на итоговый файл.
- [ ] Ошибка preview не отменяет download; merge/writer errors не становятся успешным EOF.
- [ ] Long paths, включая снятие нынешнего 240-символьного прикладного ограничения Classic.
- [ ] Owner/permissions, reparse/symlink/path traversal/substitution, IPC current-user, секреты не попадают в логи.
- [ ] Network/removable filesystem capabilities только по отдельным доказательствам; обязательная база — local fixed drive.

## 14. API, UI и диагностика

- [ ] Общие точные reasonCodes и states для всех новых режимов; ok принятия команды не означает playing.
- [ ] Название/источник/выбранные дорожки/доступная граница/буфер/причина ожидания.
- [ ] Pause/Stop/Seek, главы и track menus остаются отзывчивыми; activate=false не перехватывает фокус.
- [ ] Capabilities отражают фактические EXE/decoder builds; v1/client/legacy adapters не регрессировали.

## 15. Приёмка плеера

- [ ] Общая positive/negative matrix трёх редакций, минимум 10 секунд просмотра до complete.
- [ ] Неравномерная запись, writer pauses 10/30/60 s, partial structures, holes, ошибки, disk full, cancel/crash.
- [ ] Реальные разрешённые YT/VK/Rutube direct/HLS/DASH сценарии с protocol/format IDs и offline fixtures без секретных URL.
- [x] Ограниченный срез: [100 native core open/cancel/release cycles на каждой ОС](ProgressiveRf64Limits.md) с предельными RF64, реальными sparse resources и RSS/private/handle measurements. Это не 100 полных GUI/decoder cycles; следующий общий пункт остаётся открытым.
- [x] Ограниченный GUI/decoder срез: [100 start/toolbar-Stop/release циклов каждой редакции](ProgressivePlayerLifecycle.md), 300 измеряемых и 12 прогревочных, четыре RF64 режима; pause/seek, реальный decoder I/O, filesClosed, SHA/удаление и budgets RSS/private/handles/threads. Исправлены две Windows handle leaks, сохранены failed-before-fix доказательства; [отчёт](ProgressivePlayerLifecycleValidation-2026-11-03.json). Длительный stress, остальные форматы, несколько readers и общая приёмка этим не закрыты.
- [x] Ограниченный concurrent RF64 срез: [два playback endpoints и дополнительное чтение общего файла](ProgressiveMultiReaderPlayback.md), максимум пять квитанций, четыре профиля каждой редакции, изолированные cancel/Stop/reopen и общий release перед удалением. Qt — два Player одного процесса; Classic — два процесса. Исправлена конкуренция resource-probe с reader queries, проверены forced contention/zero-wait/deadline/cancel и регрессии; [отчёт](ProgressiveMultiReaderPlaybackValidation-2026-11-03.json). Общая multi-reader/stress приёмка остаётся открытой.
- [x] Ограниченный длительный срез: [600 s растущего RF64 каждой редакции](ProgressiveSoakPlayback.md), два самостоятельных процесса PCM16 ordinary / PCM24 sparse hint, последовательный префикс. Паузы writer 10/30/60 s с heartbeat, buffering/resume после длинных пауз, прежние readerId, скорость 1 и заранее заданные бюджеты CPU/RSS/private/handles/threads/API/session bytes/release; [все измерения и exit codes](ProgressiveSoakValidation-2026-11-03.json). Короткий smoke не засчитан. Прочие контейнеры, video/remux/live, 30-min A/V, смешанные редакции, все cache/temp варианты и общая stress-приёмка открыты.
- [x] Ограниченный длительный видеосрез: [600 s H.264/AAC fMP4 и MPEG-TS каждой редакции](ProgressiveVideoSoakPlayback.md), два процесса, inline SHA, прежние паузы/бюджеты и положительный filesClosed. Найден и исправлен общий отказ при 129 inline SHA-блоках; повторный прогон перешёл эту границу без укрупнения порций. Bounded RAM remux и native SHA/packet/clock регрессии повторены на исправленных EXE; [отчёт с failed-before-fix](ProgressiveVideoSoakValidation-2026-11-03.json). HD/4K, прочие codecs, непрерывный remux, cache matrix, 30-min физический A/V и полная stress-приёмка открыты.
- [ ] Завершить bounded continuous cyclic slice: [итоговый повтор 100 циклов и 12 paused expiry каждой редакции прошёл](ProgressiveContinuousLifecycleStress.md), но первичный Classic resource-budget FAIL ещё не объяснён. [Новая диагностика](ProgressiveContinuousObservation.md) дважды воспроизвела рост Event на пути MPCVR; точный allocator ещё не найден. Общая приёмка остаётся открытой.
- [ ] Длительные загрузки, минимум 100 start/cancel циклов, несколько readers, RAM/cache/temp/CPU/handles/threads без нарастающих утечек.
- [ ] Обычные завершённые файлы, пользовательские профили и shell registration не регрессировали.

## 16. Зависимость реального Harvester

- [ ] Отдельное разрешение/задача на подключение реальных download events и кнопки preview в Harvester.
- [ ] Сквозные start/update/complete/error/cancel/release из реального Harvester во всех трёх редакциях.
- [ ] До этого допустим статус «плеер готов к интеграции», но не «всё ТЗ выполнено».

Действующий запрет изменять Harvester сохраняется. Этот чеклист не является
разрешением менять другой проект.

## 17. Итоговая поставка

- [ ] Три проверенные сборки, Windows EXE/MSI и Linux .deb, SDK и инструкции интеграции.
- [ ] Исходники, воспроизводимые fixtures, версии/build options/licenses компонентов и итоговая matrix с exit codes.
- [ ] Независимые установки/профили/регистрация; Linux обновлён с backup и проверкой сохранности INI.
- [ ] Итоговый GitHub release и проверка скачанных assets/checksums/source commit.

## Численные критерии ТЗ

- [ ] Старт после готового буфера ≤3 s, resume ≤2 s на согласованном эталонном ПК; remux preparation измеряется отдельно.
- [ ] Абсолютная A/V ошибка ≤80 ms; накопленный drift за 30 min ≤40 ms, с описанной методикой физического выхода.
- [ ] Stop/Close/Cancel освобождает управляемые ожидания ≤2 s; UI не зависает.
- [ ] Все обязательные positive rows реально играют до complete; нет чтения неопубликованного, ложного EOF, порчи исходников и потери выбора/metadata.
- [ ] Необъяснимые SKIP и неподтверждённые OS/backend результаты не считаются PASS.

Если физический A/V стенд недоступен, приёмка этого пункта остаётся открытой.
Отложенные пользователем дисковые меню, аналоговое ТВ, физические устройства и
Wayland этим чеклистом не возобновляются. Промежуточные feature-релизы не закрывают
всю матрицу. Каждый новый формат проверяется в допустимых для контейнера сочетаниях.
