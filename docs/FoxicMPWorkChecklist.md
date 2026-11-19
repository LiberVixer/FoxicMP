# FoxicMP: единый учёт работ

База пересчёта — 17 ноября 2026, SDK1.106. Реестр охватывает известные работы
progressive-ТЗ и семейного плана. Он считает результаты работ, а не число тестов.
Статусы восстановлены по имеющимся отчётам; новый полный runtime-аудит не проводился.

## Текущий счётчик

| Уровень | Всего | Готово | Не завершено | Активный план | Внешнее условие | Отложено |
|---|---:|---:|---:|---:|---:|---:|
| Задачи | 26 | 6 | 20 | 17 | 2 | 1 |
| Подзадачи | 267 | 178 | 89 | 73 | 7 | 9 |
| Подподзадачи | 353 | 146 | 207 | 189 | 13 | 5 |
| **Все узлы трёх уровней** | **646** | **330** | **316** | 279 | 22 | 15 |
| **Только конечные рабочие пункты** | **537** | **312** | **225** | 200 | 13 | 12 |

Родитель и его дети — разные уровни, поэтому их сумма не является оценкой
трудозатрат или процентом готовности продукта. Для движения работы полезнее
конечные пункты. Родитель закрывается только после всех своих детей.

## Изменения относительно базы

Добавлено задач / подзадач / подподзадач: **0 / 0 / 0**.
Закрыто с базы по уровням: **0 / 0 / 1**.
Удалено: 0; повторно открыто: 0; сменило уровень: 0; изменено описание: 0.

Сам исходный пересчёт — миграция известных работ, а не появление новых требований.
Отдельно: [только оставшиеся конечные пункты](FoxicMPWorkRemaining.md),
[проверка пересчёта](FoxicMPWorkRecount-2026-11-17.json).
При последующем добавлении указываются ID и причина: новое требование,
детализация существующего пункта либо исправление пропуска учёта.

## Почему прежние77 не показывали движение

Все 324 checkbox-записи 8 старых списков связаны с этим реестром;
8 записей — повтор/общий критерий или ограничение, а не новая единица работы.
В старом общем журнале 149 готовых записей и 77 открытых оставались неизменными.
98 поздних выполненных шагов VFR/SimpleBlock/video count/nominal/audio count/Opus
жили в отдельных чеклистах. Здесь они учтены внутри своих законченных этапов.
Сами 77 — историческое состояние, больше не основной рабочий счётчик.

## Все задачи

| ID | Задача | Статус | Конечных пунктов готово / всего | Активно осталось | Внешнее условие | Отложено |
|---|---|---|---:|---:|---:|---:|
| P00 | Основа протокола и совместимость | готово | 13 / 13 | 0 | 0 | 0 |
| P01 | Общий растущий fMP4 | готово | 5 / 5 | 0 | 0 | 0 |
| P02 | Demux и непрерывный remux контейнеров | частично | 59 / 80 | 21 | 0 | 0 |
| P03 | Обычный MP4 и индексы | готово | 4 / 4 | 0 | 0 | 0 |
| P04 | Аудиокодеки и самостоятельные потоки | частично | 21 / 36 | 15 | 0 | 0 |
| P05 | Непоследовательная запись и большие ресурсы | частично | 6 / 16 | 10 | 0 | 0 |
| P06 | Временная шкала и буферизация | частично | 104 / 116 | 12 | 0 | 0 |
| P07 | Конечные HLS/DASH | частично | 8 / 20 | 12 | 0 | 0 |
| P08 | Смена качества и конфигурации | осталось | 0 / 6 | 6 | 0 | 0 |
| P09 | Динамические аудиодорожки | частично | 4 / 13 | 9 | 0 | 0 |
| P10 | Субтитры и вложения | частично | 1 / 19 | 18 | 0 | 0 |
| P11 | Главы и метаданные | частично | 2 / 10 | 8 | 0 | 0 |
| P12 | Live/event HLS и dynamic DASH | осталось | 0 / 8 | 8 | 0 | 0 |
| P13 | Жизненный цикл, I/O и безопасность | частично | 24 / 47 | 20 | 1 | 2 |
| P14 | API, интерфейс и диагностика | частично | 8 / 19 | 11 | 0 | 0 |
| P15 | Приёмка, ресурсы и задержки | частично | 12 / 41 | 26 | 0 | 3 |
| P16 | Внешняя интеграция Harvester | внешнее условие | 0 / 4 | 0 | 4 | 0 |
| P17 | Итоговые пакеты и GitHub release | частично | 5 / 18 | 13 | 0 | 0 |
| H | Атрибуция четырёх исторических сбоев | внешнее условие | 0 / 8 | 0 | 8 | 0 |
| F0 | Общая архитектура и три независимые редакции | готово | 3 / 3 | 0 | 0 | 0 |
| F1 | Общие команды, defaults и профили | готово | 5 / 5 | 0 | 0 | 0 |
| F2 | Обе Qt-платформы и автоматическая проверка | частично | 2 / 6 | 4 | 0 | 0 |
| F3 | Интерфейс и управление семейства | готово | 6 / 6 | 0 | 0 | 0 |
| F5 | Мастер переноса библиотеки и предпочтений | отложено | 0 / 3 | 0 | 0 | 3 |
| F6 | Мультимедиа и платформенные ограничения | частично | 18 / 27 | 5 | 0 | 4 |
| F7 | Общий цикл сборки и версии семейства | частично | 2 / 4 | 2 | 0 | 0 |

**Следующий конкретный пункт очереди:** P02.C149.04 — WebM VP8/AV1: continuous пути за пределами native профилей.

## Полный список с подзадачами

Готовые результаты сохраняют точные границы исходных отчётов. Они не обещают
непроверенные сочетания кодеков, аппаратный вывод или настоящий Harvester.

- **P00** — **готово**: Основа протокола и совместимость. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P00.C028** — **готово**: Совместимые v1 start/update/complete и исправление sessionDirectory Classic — проверки. Источник: [ProgressiveCompletionChecklist.md:28](ProgressiveCompletionChecklist.md).
  - **P00.C029** — **готово**: Общий SDK/client, OS discovery/IPC и Windows Qt legacy/provider adapter в ограниченном профиле — проверки. Источник: [ProgressiveCompletionChecklist.md:29](ProgressiveCompletionChecklist.md).
  - **P00.C030** — **готово**: Модель v2, подтверждённые ranges, resource I/O и reader receipts — контракт. Источник: [ProgressiveCompletionChecklist.md:30](ProgressiveCompletionChecklist.md).
  - **P00.C031** — **готово**: Объявленные SHA-256 blocks и проверка перед чтением — контракт и границы. Источник: [ProgressiveCompletionChecklist.md:31](ProgressiveCompletionChecklist.md).
  - **P00.NATIVE** — **готово**: Отдельные native v2 потоки и смешанные пары в опубликованных границах. Источник: [ProgressiveMediaV2.md](ProgressiveMediaV2.md), [ProgressiveHevc.md](ProgressiveHevc.md), [ProgressiveAv1.md](ProgressiveAv1.md), [ProgressiveVp9.md](ProgressiveVp9.md), [ProgressiveOpus.md](ProgressiveOpus.md), [ProgressiveWebM.md](ProgressiveWebM.md), [ProgressiveWebMAv1.md](ProgressiveWebMAv1.md), [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md), [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md).
    - **P00.NATIVE.01** — **готово**: Первый separate fMP4 H.264/AAC-LC playback schema2. Источник: [ProgressiveMediaV2.md](ProgressiveMediaV2.md).
    - **P00.NATIVE.02** — **готово**: Separate HEVC Main/Main10 fMP4 + AAC-LC. Источник: [ProgressiveHevc.md](ProgressiveHevc.md).
    - **P00.NATIVE.03** — **готово**: Separate AV1 Main8/10 fMP4 + AAC-LC. Источник: [ProgressiveAv1.md](ProgressiveAv1.md).
    - **P00.NATIVE.04** — **готово**: Separate VP9p0/p2 fMP4 + AAC-LC. Источник: [ProgressiveVp9.md](ProgressiveVp9.md).
    - **P00.NATIVE.05** — **готово**: Opus fMP4 с четырьмя разрешёнными video codecs. Источник: [ProgressiveOpus.md](ProgressiveOpus.md).
    - **P00.NATIVE.06** — **готово**: Separate WebM VP9/Opus. Источник: [ProgressiveWebM.md](ProgressiveWebM.md).
    - **P00.NATIVE.07** — **готово**: Separate WebM AV1/Opus. Источник: [ProgressiveWebMAv1.md](ProgressiveWebMAv1.md).
    - **P00.NATIVE.08** — **готово**: Смешанные separate fMP4/WebM в обоих направлениях. Источник: [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md).
    - **P00.NATIVE.09** — **готово**: Separate VP8 и Vorbis в WebM. Источник: [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md).

- **P01** — **готово**: Общий растущий fMP4. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P01.C039** — **готово**: HEVC Main/Main10 + AAC: 8/10-bit, hvc1/hev1, B-frames, fractional fps, edit lists, partial fragments — SDK 1.47 и проверки трёх редакций. Источник: [ProgressiveCompletionChecklist.md:39](ProgressiveCompletionChecklist.md).
  - **P01.C040** — **готово**: AV1 Main 8/10-bit + AAC: fractional fps, edit lists, partial fragments, SHA/leases — SDK 1.48 и проверки трёх редакций. Источник: [ProgressiveCompletionChecklist.md:40](ProgressiveCompletionChecklist.md).
  - **P01.C041** — **готово**: VP9 Profile 0/2 8/10-bit + AAC: fractional fps, edit lists, partial fragments, SHA/leases — SDK 1.49 и проверки трёх редакций. Источник: [ProgressiveCompletionChecklist.md:41](ProgressiveCompletionChecklist.md).
  - **P01.C042** — **готово**: H.264/HEVC/VP9/AV1 + Opus mono/stereo 48 kHz, 8/10-bit и fractional profiles — SDK 1.50 и проверки трёх редакций. Источник: [ProgressiveCompletionChecklist.md:42](ProgressiveCompletionChecklist.md).
  - **P01.AVC** — **готово**: Shared growing H.264/AAC fMP4. Источник: [ProgressiveMuxedFmp4.md](ProgressiveMuxedFmp4.md).

- **P02** — **частично**: Demux и непрерывный remux контейнеров. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P02.C051** — **готово**: SDK 1.88: ordinary MP4 → continuous HEVC Main/Main10 video-only, явный mode, hvc1/hev1, 8/10-bit 4:2:0, B-frames; worker/cache/decoder/Classic bridge и SDK. 54 worker checks, 30 public API rows, 78 регрессий, ≥. Источник: [ProgressiveCompletionChecklist.md:51](ProgressiveCompletionChecklist.md).
  - **P02.C052** — **готово**: SDK 1.89: HEVC + AAC-LC в ordinary MP4, отдельный mode, exact AAC PCM-window 44.1/48 kHz mono/stereo, Main/Main10, audio-first и one-sample/short audio. 36 worker, 36 PCM consumer, 39 API rows и 112 регрессий п. Источник: [ProgressiveCompletionChecklist.md:52](ProgressiveCompletionChecklist.md).
  - **P02.C054** — **готово**: SDK 1.90: AV1 Main8/Main10 video-only из ordinary MP4, отдельный mode, av01, single-layer, PTS=DTS и один displayed Frame OBU/sample. 51 worker, 30 public API rows, exact packets/clocks/frame hashes, ≥10 s play. Источник: [ProgressiveCompletionChecklist.md:54](ProgressiveCompletionChecklist.md).
  - **P02.C056** — **готово**: SDK 1.91: AV1 + AAC-LC из ordinary MP4, explicit paired mode, Main8/Main10 av01, mono/stereo 44.1/48 kHz, exact PCM-window, audio-first/one-sample/short audio. 48 worker, 48 PCM consumer, 39 API rows и 192 регр. Источник: [ProgressiveCompletionChecklist.md:56](ProgressiveCompletionChecklist.md).
  - **P02.C058** — **готово**: SDK 1.92: VP9 video-only из ordinary MP4, explicit mode, vp09 Profile0/8-bit или 2/10-bit, head moov ≤1 MiB, один displayed frame/sample, PTS=DTS и fixed coded/render size. 93 worker, 30 API rows, 231 регрессия. Источник: [ProgressiveCompletionChecklist.md:58](ProgressiveCompletionChecklist.md).
  - **P02.C060** — **готово**: SDK 1.93: VP9 + AAC-LC из ordinary MP4, explicit paired mode, Profile0/8-bit или Profile2/10-bit vp09, mono/stereo 44.1/48 kHz, immutable exact PCM-window и audio-first/one-sample/short audio. 48 worker, 48 PCM. Источник: [ProgressiveCompletionChecklist.md:60](ProgressiveCompletionChecklist.md).
  - **P02.C062** — **готово**: SDK 1.94: VP9 indexed superframes/alt-ref video-only, отдельный mode, Profile0/8-bit или Profile2/10-bit, 1–8 кадров/sample, hidden inter prefixes и один displayed last, PTS=DTS/fixed size. 108 worker, 30 новых. Источник: [ProgressiveCompletionChecklist.md:62](ProgressiveCompletionChecklist.md).
  - **P02.C064** — **готово**: SDK 1.95: VP9 indexed superframes/alt-ref + AAC-LC, explicit paired mode, Profile0/8-bit или Profile2/10-bit, mono/stereo 44.1/48 kHz, immutable exact PCM-window, audio-first/one-sample/short audio. 66 worker,. Источник: [ProgressiveCompletionChecklist.md:64](ProgressiveCompletionChecklist.md).
  - **P02.C066** — **готово**: Подготовка continuous WebM/VP9, без bump версии/API SDK 1.95 или изменения продукта: 36 fixture/index/copy-reference строк трёх worker builds. 25 fps finite/unknown Clusters сохраняют payload/clocks/video на fu. Источник: [ProgressiveCompletionChecklist.md:66](ProgressiveCompletionChecklist.md).
  - **P02.C068** — **готово**: SDK 1.96: continuous WebM/VP9 video-only 25 fps, Profile0/8-bit или Profile2/10-bit, finite/unknown Clusters, структурные границы и expected sample count из timeline. 87 worker, 39 новых API rows, 66 прежних MP. Источник: [ProgressiveCompletionChecklist.md:68](ProgressiveCompletionChecklist.md).
  - **P02.C070** — **готово**: SDK 1.97: WebM VP9 indexed superframes/alt-ref video-only, отдельный mode, 25 fps, Profile0/8-bit или Profile2/10-bit, finite/unknown Clusters, до семи hidden inter frames перед одним показанным. 99 worker, 39. Источник: [ProgressiveCompletionChecklist.md:70](ProgressiveCompletionChecklist.md).
  - **P02.C072** — **готово**: Подготовка continuous WebM VP9/Opus, без нового mode или bump SDK1.97: 36 characterization rows трёх compiler/native стендов.30 точных PCM-window rows,60 точных native full/prefix projections,72 MP4 copy refere. Источник: [ProgressiveCompletionChecklist.md:72](ProgressiveCompletionChecklist.md).
  - **P02.C074** — **готово**: SDK1.98: continuous WebM VP9/Opus: bounded structural sealer, finite/unknown Clusters, защищённый WebM cache и обе обязательные decoder views в трёх редакциях; VP9p0/p2 indexed25fps, Opus48k mono/stereo20ms/pre. Источник: [ProgressiveCompletionChecklist.md:74](ProgressiveCompletionChecklist.md).
  - **P02.C076** — **готово**: SDK1.99: continuous WebM VP9/Opus30000/1001fps: rational frame clock, exact frame count по timeline, nearest-ms packets и bounded coverage без ложных дыр. 87 новых/78 прежних worker cases,9 новых/9 прежних ordi. Источник: [ProgressiveCompletionChecklist.md:76](ProgressiveCompletionChecklist.md).
  - **P02.C082** — **готово**: Первый этап: bounded FFmpeg AVIO **диагностика** подтверждённых ресурсов,. Источник: [ProgressiveCompletionChecklist.md:82](ProgressiveCompletionChecklist.md).
  - **P02.C087** — **готово**: SDK 1.52: copy-remux ограниченного MP4-фрагмента в памяти,. Источник: [ProgressiveCompletionChecklist.md:87](ProgressiveCompletionChecklist.md).
  - **P02.C092** — **готово**: SDK 1.53: явный playback подготовленного RAM MP4-фрагмента. Источник: [ProgressiveCompletionChecklist.md:92](ProgressiveCompletionChecklist.md).
  - **P02.C097** — **готово**: Первый внутренний компонент непрерывного remux: один persistent FFmpeg worker H.264 video-only → fMP4, две 24 s фикстуры по 600/720 packets, incremental output до source complete, ожидания с тем же demux contex. Источник: [ProgressiveCompletionChecklist.md:97](ProgressiveCompletionChecklist.md).
  - **P02.C099** — **готово**: Внутренний управляемый continuous-remux parent: отдельный поток, ResourceSource/SHA/ranges, writer/session identity, heartbeat/errors, Windows Job, раздельные lifetime/pipe deadlines и source leases. По 17 nati. Источник: [ProgressiveCompletionChecklist.md:99](ProgressiveCompletionChecklist.md).
  - **P02.C101** — **готово**: Ограниченный внутренний output store непрерывного remux: private disk cache/shared budget, atomic moov/moof+mdat visibility, FragmentIndex, SHA on read, original/output reader leases и cleanup после подтверждён. Источник: [ProgressiveCompletionChecklist.md:101](ProgressiveCompletionChecklist.md).
  - **P02.C103** — **готово**: Ограниченное восстановление private cache после смерти owner: immutable atomic journal, PID/birth + locks, physical/creation identities, source/output reader proofs, quarantine и startup admission до нового bud. Источник: [ProgressiveCompletionChecklist.md:103](ProgressiveCompletionChecklist.md).
  - **P02.C105** — **готово**: Первые внутренние decoder views: блокирующий cache cursor, настоящий libmpv callback adapter и Classic FragmentIndex/sample view. По 11 сценариев GCC/MinGW/MSVC/ASan-UBSan; по 600 кадров совпали с completed-sou. Источник: [ProgressiveCompletionChecklist.md:105](ProgressiveCompletionChecklist.md).
  - **P02.C107** — **готово**: Внутренние playback entry points continuous-remux в настоящем Qt Player/MpvLifetime и Classic CHarvesterSession/DirectShow. По пять сценариев Qt каждой ОС и пять Classic graph; 600 NV12 кадров совпали с независ. Источник: [ProgressiveCompletionChecklist.md:107](ProgressiveCompletionChecklist.md).
  - **P02.C109** — **готово**: Общий внутренний async controller continuous-remux: replay/fences, отдельные accepted/applied revision/heartbeat, очередь ≤8 pending updates, быстрые status/cancel/destruction и удержание cache/root до настояще. Источник: [ProgressiveCompletionChecklist.md:109](ProgressiveCompletionChecklist.md).
  - **P02.C113** — **готово**: Ограниченный continuous output `readerRelease` adapter всех трёх редакций: точная привязка квитанции к output resource/file identity и попытке просмотра, остановка при playing/pause/ожидании хвоста, EOF/replay. Источник: [ProgressiveCompletionChecklist.md:113](ProgressiveCompletionChecklist.md).
  - **P02.C115** — **готово**: Ограниченный публичный continuous SDK waiting/replay срез: реальные паузы writer 10/30/60 s, тот же worker и decoder readers, ручная пауза при append, complete forward/reverse seek и EOF, Stop/Cancel у хвоста и. Источник: [ProgressiveCompletionChecklist.md:115](ProgressiveCompletionChecklist.md).
  - **P02.C120** — **готово**: Неблокирующее закрытие output readers при занятом registry: bounded pending receipts, независимый cache mutex, retry существующим worker без ложного release; отчёт. Linux обновлён с backup/неизменным профилем;. Источник: [ProgressiveCompletionChecklist.md:120](ProgressiveCompletionChecklist.md).
  - **P02.C121** — **готово**: Закрытие original source вне mutex continuous-remux: все native media pins закрываются до receipt persistence, status/cancel остаются доступны, late heartbeat при EOF безопасен. Приёмка: GCC/ASan/MinGW/MSVC, тр. Источник: [ProgressiveCompletionChecklist.md:121](ProgressiveCompletionChecklist.md).
  - **P02.C122** — **готово**: Ограниченное закрытие/crash continuous player и явный повторный запуск: по восемь сценариев каждой редакции при preparation/playing/paused/buffering, реальный выход worker, сохранение независимого просмотра, фи. Источник: [ProgressiveCompletionChecklist.md:122](ProgressiveCompletionChecklist.md).
  - **P02.C123** — **готово**: Ограниченная очистка continuous cache при нормальном выходе: общий final-exit wait ≤1500 ms после уничтожения GUI/API/decoder; по восемь усиленных сценариев каждой редакции, все двенадцать нормальных выходов бе. Источник: [ProgressiveCompletionChecklist.md:123](ProgressiveCompletionChecklist.md).
  - **P02.C124** — **готово**: Ограниченная авария continuous worker через публичный SDK: по четыре сценария каждой редакции при preparation/playing/paused/buffering. Только identity-checked дочерний worker, OS exit, status-only automatic re. Источник: [ProgressiveCompletionChecklist.md:124](ProgressiveCompletionChecklist.md).
  - **P02.C125** — **готово**: Ограниченные гонки public continuous-команд и замены просмотра: по 16 сценариев каждой редакции, публикация/Stop, публикация/замена, readerRelease/замена, cancel/замена; поздние старые команды не прерывают новы. Источник: [ProgressiveCompletionChecklist.md:125](ProgressiveCompletionChecklist.md).
  - **P02.C126** — **готово**: Ограниченные faults незавершённого pipe I/O: по 12 native сценариев GCC/MinGW/MSVC/ASan-UBSan, всего 48, все exit 0. Перед смертью child, cancel и timeout доказан partial receive или backpressure send; filesClo. Источник: [ProgressiveCompletionChecklist.md:126](ProgressiveCompletionChecklist.md).
  - **P02.C127** — **готово**: Ограниченные аварии настоящего FFmpeg внутри AVIO: по восемь SDK сценариев трёх редакций, всего 24. Instrumented test worker останавливается внутри real read/write callback после начала playing/manual pause; в. Источник: [ProgressiveCompletionChecklist.md:127](ProgressiveCompletionChecklist.md).
  - **P02.C128** — **готово**: Ограниченные syscall-result faults parent I/O: 14 GCC/Linux, 11 MinGW/Windows, 11 MSVC/Windows, 14 ASan/UBSan; source read error/short read, cache error/disk-full/physical partial write, внешние readers и POSIX. Источник: [ProgressiveCompletionChecklist.md:128](ProgressiveCompletionChecklist.md).
  - **P02.C130** — **готово**: Диагностический срез пассивного наблюдения: 680 Classic cycles в восьми сериях, два реальных resource-budget FAIL с ростом Event на пути внешнего MPCVR; EVR/Null контроли без роста. Захват исходной ошибки, типо. Источник: [ProgressiveCompletionChecklist.md:130](ProgressiveCompletionChecklist.md).
  - **P02.C131** — **готово**: Исправлен конкретный воспроизводимый рост DWM Event в Classic: первый MPCVR redraw перенесён до Run/Pause в постоянный UI-поток. Три исходных instrumented FAIL с 104 сохраняющимися Event, затем три успешных 100. Источник: [ProgressiveCompletionChecklist.md:131](ProgressiveCompletionChecklist.md).
  - **P02.C132** — **готово**: Убрана зависимость освобождения файлов декодера от libmpv close callback. Native baseline удерживает cursor после ошибки и падает по 2 s; фоновый контроллер теперь закрывает только decoder readers, сохраняя ind. Источник: [ProgressiveCompletionChecklist.md:132](ProgressiveCompletionChecklist.md).
  - **P02.C133** — **готово**: Ограниченный sustained growing stress одного файла и двух viewers: по 129–131 s каждой редакции, физический append-only writer, настоящий 60 s stall с heartbeat, ручная пауза, закрытие/replay первого endpoint б. Источник: [ProgressiveCompletionChecklist.md:133](ProgressiveCompletionChecklist.md).
  - **P02.C134** — **готово**: Первый внутренний continuous worker с H.264 B-frames: дополнительный частный mode, delay_moov/edit list с точными рациональными PTS/DTS, четыре 24 s профиля включая fractional/VFR, по 3239 packet/frame comparis. Источник: [ProgressiveCompletionChecklist.md:134](ProgressiveCompletionChecklist.md).
  - **P02.C135** — **готово**: Внутренний managed continuous B-frame профиль и настоящие декодеры: явный opt-in в parent/cache, четыре physical growing профиля GCC/MinGW/MSVC/ASan-UBSan, по 3239 полных и 1889 seek-кадров, точные packet/PNG c. Источник: [ProgressiveCompletionChecklist.md:135](ProgressiveCompletionChecklist.md).
  - **P02.C136** — **готово**: Внутренний B-frame controller/Player/API срез: явный start mode, неизменяемые job/source/ticket mode, replay conflict и возвращение к прежнему default. По 18 новых и 17 прежних playback-сценариев и пять SDK-сце. Источник: [ProgressiveCompletionChecklist.md:136](ProgressiveCompletionChecklist.md).
  - **P02.C141** — **готово**: Первый внутренний H.264/AAC aligned worker: две дорожки в любом порядке, AAC-LC mono/stereo 44,1/48 kHz с окончанием на границе 1024 samples, точные общие clocks и начальный skip, copy-remux без фильтров/переко. Источник: [ProgressiveCompletionChecklist.md:141](ProgressiveCompletionChecklist.md).
  - **P02.C142** — **готово**: Внутреннее точное PCM-окно обычного AAC: частный worker требует подтверждения raw/presentation границ до медиабайтов. Общая целочисленная геометрия, реальный Qt Player на обеих ОС и внутренний AAC decoder Class. Источник: [ProgressiveCompletionChecklist.md:142](ProgressiveCompletionChecklist.md).
  - **P02.C143** — **готово**: Внутренний managed AAC parent/cache/index: SHA-подтверждение PCM-плана до bytes, immutable cache binding, два проверяемых индекса и точное количество AAC packets до complete. Короткая аудиодорожка может закончи. Источник: [ProgressiveCompletionChecklist.md:143](ProgressiveCompletionChecklist.md).
  - **P02.C144** — **готово**: Внутренние continuous AAC decoder adapters: immutable PCM-план через явный opt-in, Qt Player обеих ОС и две Classic fMP4-проекции с отдельным AAC COM control/re-arm после flush/seek. По 31 Qt + 37 Classic сцена. Источник: [ProgressiveCompletionChecklist.md:144](ProgressiveCompletionChecklist.md).
  - **P02.C148** — **осталось**: Проверенные backend demuxers через управляемый local I/O; платформенные адаптеры Qt/Classic. Источник: [ProgressiveCompletionChecklist.md:148](ProgressiveCompletionChecklist.md).
    - **P02.C148.01** — **осталось**: Подключить production demux через confirmed local I/O для ещё не поддержанных входов. Источник: [ProgressiveCompletionChecklist.md:148](ProgressiveCompletionChecklist.md).
    - **P02.C148.02** — **осталось**: Подключить оставшиеся Qt/Classic decoder adapters без открытия частичного файла как обычного. Источник: [ProgressiveCompletionChecklist.md:148](ProgressiveCompletionChecklist.md).
    - **P02.C148.03** — **осталось**: Проверить limits/cancel/malformed/EOF для каждого вновь подключённого demux path. Источник: [ProgressiveCompletionChecklist.md:148](ProgressiveCompletionChecklist.md).
  - **P02.C149** — **частично**: Обычные MP4/M4A/MOV, WebM/Matroska, MPEG-TS и их допустимые варианты вне нынешних узких профилей. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.01** — **готово**: Зафиксировать конечный перечень обязательных положительных container/codec/layout строк и точные исключения. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md), [ProgressiveRequiredMediaMatrix.md](ProgressiveRequiredMediaMatrix.md), [ProgressiveRequiredMediaMatrixValidation-2026-11-17.json](ProgressiveRequiredMediaMatrixValidation-2026-11-17.json).
    - **P02.C149.02** — **готово**: Обычный M4A: audio-only admission и progressive playback. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md), [ProgressiveM4A.md](ProgressiveM4A.md), [ProgressiveM4AValidation-2026-11-18.json](ProgressiveM4AValidation-2026-11-18.json).
    - **P02.C149.03** — **готово**: MOV: sample entries, edit lists и отличия структуры от MP4. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md), [ProgressiveIndexedMOV.md](ProgressiveIndexedMOV.md), [ProgressiveIndexedMOVValidation-2026-11-19.json](ProgressiveIndexedMOVValidation-2026-11-19.json).
    - **P02.C149.04** — **осталось**: WebM VP8/AV1: continuous пути за пределами native профилей. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.05** — **осталось**: WebM/Vorbis: continuous путь за пределами native профиля. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.06** — **осталось**: Matroska H.264/HEVC и допустимые пары аудио. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.07** — **осталось**: Matroska: lacing/CodecPrivate/Cues и track identity за пределами WebM. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.08** — **осталось**: MPEG-TS: continuous remux HEVC и предусмотренных MPEG-вариантов. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.09** — **осталось**: Separate/multi-input remux и обязательные ещё не покрытые muxed сочетания. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
    - **P02.C149.10** — **осталось**: Иные допустимые MP4/WebM layouts за пределами опубликованных строгих профилей; перечень заморозить до реализации. Источник: [ProgressiveCompletionChecklist.md:149](ProgressiveCompletionChecklist.md).
  - **P02.C150** — **осталось**: Явные native-progressive, progressive-remux, waiting, post-download и unsupported результаты. Источник: [ProgressiveCompletionChecklist.md:150](ProgressiveCompletionChecklist.md).
    - **P02.C150.01** — **осталось**: Различать native-progressive и progressive-remux по фактически выполненному path. Источник: [ProgressiveCompletionChecklist.md:150](ProgressiveCompletionChecklist.md).
    - **P02.C150.02** — **осталось**: Различать временное waiting и окончательные unsupported/corrupt/encrypted/missing-decoder. Источник: [ProgressiveCompletionChecklist.md:150](ProgressiveCompletionChecklist.md).
    - **P02.C150.03** — **осталось**: Выдавать post-download как ограничение, а не успешную progressive строку. Источник: [ProgressiveCompletionChecklist.md:150](ProgressiveCompletionChecklist.md).
  - **P02.C151** — **осталось**: Remux только подтверждённых локальных данных, без перекодирования/ухудшения качества и без изменения оригиналов. Источник: [ProgressiveCompletionChecklist.md:151](ProgressiveCompletionChecklist.md).
    - **P02.C151.01** — **осталось**: Для каждой новой positive строки подтвердить чтение только опубликованных диапазонов. Источник: [ProgressiveCompletionChecklist.md:151](ProgressiveCompletionChecklist.md).
    - **P02.C151.02** — **осталось**: Сравнить compressed payload и исходные PTS/DTS до и после remux. Источник: [ProgressiveCompletionChecklist.md:151](ProgressiveCompletionChecklist.md).
    - **P02.C151.03** — **осталось**: Подтвердить отсутствие транскодирования и изменения оригиналов во всей новой матрице. Источник: [ProgressiveCompletionChecklist.md:151](ProgressiveCompletionChecklist.md).
  - **P02.C159** — **готово**: Classic: происхождение transient threads и дополнительный idle-критерий: строгий cold-baseline 7→10 снова воспроизведён (exit 1), три добавленных TppWorkerThread определены по точным Microsoft PDB symbols, EXE-. Источник: [ProgressiveCompletionChecklist.md:159](ProgressiveCompletionChecklist.md).
  - **P02.C160** — **готово**: Шесть AAC-профилей и независимость Classic history: по 30 функциональных cases трёх редакций +2 обычной истории, native exits 0. Classic больше не возобновляет новый continuous job с позиции другого под общим a. Источник: [ProgressiveCompletionChecklist.md:160](ProgressiveCompletionChecklist.md).
  - **P02.C163** — **готово**: Qt: восстановление после краткой registry busy и обязательный звук: точный повтор поймал `reader_registry_busy`; только decoder admission использует окно повторов 250 ms, raw/API busy сохранён. Постоянный отказ. Источник: [ProgressiveCompletionChecklist.md:163](ProgressiveCompletionChecklist.md).
  - **P02.C169** — **готово**: Очередь: ограниченный commit/reentrancy-срез: общая транзакция сохраняет cancel/новую revision после старой команды; guard перед decoder handoff, защита от вложенного poll и резерв последнего слота. Общий тест. Источник: [ProgressiveCompletionChecklist.md:169](ProgressiveCompletionChecklist.md).
  - **P02.C178** — **осталось**: Growing seek: оставшиеся I/O границы (неподтверждённая драйвером отмена disk I/O, AAC projections, original-source/worker path), детерминированные границы другого v2 bridge и расширенная CMainFrame/COM/renderer/AAC/physical-I/O re. Источник: [ProgressiveCompletionChecklist.md:178](ProgressiveCompletionChecklist.md).
    - **P02.C178.01** — **осталось**: Проверить отмену original-source/worker disk I/O на реальной OS границе. Источник: [ProgressiveCompletionChecklist.md:178](ProgressiveCompletionChecklist.md).
    - **P02.C178.02** — **осталось**: Расширить AAC projection и второй v2 bridge на детерминированных seek границах. Источник: [ProgressiveCompletionChecklist.md:178](ProgressiveCompletionChecklist.md).
    - **P02.C178.03** — **осталось**: Проверить COM/renderer/CMainFrame reentry при growing seek. Источник: [ProgressiveCompletionChecklist.md:178](ProgressiveCompletionChecklist.md).
    - **P02.C178.04** — **осталось**: Проверить supersede/replacement/crash для оставшихся codec paths. Источник: [ProgressiveCompletionChecklist.md:178](ProgressiveCompletionChecklist.md).
    - **P02.C178.05** — **осталось**: Определить и проверить расширенные receipt retention/квоты за пределами bounded v1. Источник: [ProgressiveCompletionChecklist.md:178](ProgressiveCompletionChecklist.md).
  - **P02.C186** — **готово**: SDK 1.100: явный WebM VP9/Opus CFR, восемь канонических частот 23.976–60 fps; immutable start.videoRate, точный frame timeline, исходные packet clocks, допуск DefaultDuration вниз 1 ns для FFmpeg. Проверки трёх. Источник: [ProgressiveCompletionChecklist.md:186](ProgressiveCompletionChecklist.md).
  - **P02.NATIVE_TS** — **готово**: Shared growing H.264/AAC MPEG-TS. Источник: [ProgressiveGrowingTs.md](ProgressiveGrowingTs.md).
  - **P02.MUXWEBM** — **готово**: Native shared growing WebM A/V в опубликованных профилях. Источник: [ProgressiveMuxedWebm.md](ProgressiveMuxedWebm.md), [ProgressiveMuxedWebmVp8.md](ProgressiveMuxedWebmVp8.md), [ProgressiveMuxedWebmAv1.md](ProgressiveMuxedWebmAv1.md), [ProgressiveMuxedWebmVorbis.md](ProgressiveMuxedWebmVorbis.md).
    - **P02.MUXWEBM.01** — **готово**: Muxed WebM VP9/Opus. Источник: [ProgressiveMuxedWebm.md](ProgressiveMuxedWebm.md).
    - **P02.MUXWEBM.02** — **готово**: Muxed WebM VP8/Opus. Источник: [ProgressiveMuxedWebmVp8.md](ProgressiveMuxedWebmVp8.md).
    - **P02.MUXWEBM.03** — **готово**: Muxed WebM AV1/Opus. Источник: [ProgressiveMuxedWebmAv1.md](ProgressiveMuxedWebmAv1.md).
    - **P02.MUXWEBM.04** — **готово**: Muxed WebM VP8/VP9/AV1 с Vorbis. Источник: [ProgressiveMuxedWebmVorbis.md](ProgressiveMuxedWebmVorbis.md).

- **P03** — **готово**: Обычный MP4 и индексы. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P03.C203** — **готово**: SDK 1.54: ограниченный fast-start MP4 с обычными sample tables, H.264/AAC-LC, один ресурс/две дорожки; проверки. Источник: [ProgressiveCompletionChecklist.md:203](ProgressiveCompletionChecklist.md).
  - **P03.C204** — **готово**: SDK 1.55: ограниченный tail-moov MP4 H.264/AAC-LC, ожидание индекса и advisory ranges через metadata-only API; проверки трёх редакций. Произвольные sparse media остаются в пункте 5. Источник: [ProgressiveCompletionChecklist.md:204](ProgressiveCompletionChecklist.md).
  - **P03.C205** — **готово**: В указанном профиле SDK 1.54: старт до complete, ожидание неполного sample, resume с тем же readerId; отсутствие готового head index не становится успешным fallback. Прочие MP4 layouts остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:205](ProgressiveCompletionChecklist.md).
  - **P03.C206** — **готово**: SDK 1.56: перемотка после complete в активной сессии H.264/AAC-LC: отдельные fMP4 и обычный head/tail MP4; тот же reader, pause/speed/volume/mute/основная audio сохранены. Проверки трёх редакций. Остальные layo. Источник: [ProgressiveCompletionChecklist.md:206](ProgressiveCompletionChecklist.md).

- **P04** — **частично**: Аудиокодеки и самостоятельные потоки. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P04.C210** — **осталось**: Полная согласованная audio codec matrix: AAC-LC/HE-AAC, Opus, Vorbis, MP3, FLAC, PCM, AC-3/E-AC-3. Источник: [ProgressiveCompletionChecklist.md:210](ProgressiveCompletionChecklist.md).
    - **P04.C210.01** — **осталось**: HE-AAC: применимые контейнеры, SBR/PS и правильные PCM clocks. Источник: [ProgressiveCompletionChecklist.md:210](ProgressiveCompletionChecklist.md).
    - **P04.C210.02** — **осталось**: Opus: оставшиеся one-packet/priming/mapping/chaining layouts за пределами SDK1.106. Источник: [ProgressiveCompletionChecklist.md:210](ProgressiveCompletionChecklist.md).
    - **P04.C210.03** — **осталось**: Vorbis: дополнительные setup/rates/layouts/chaining. Источник: [ProgressiveCompletionChecklist.md:210](ProgressiveCompletionChecklist.md).
    - **P04.C210.04** — **осталось**: AAC-LC/MP3/FLAC/PCM/AC-3/E-AC-3: завершить согласованную codec/container матрицу. Источник: [ProgressiveCompletionChecklist.md:210](ProgressiveCompletionChecklist.md), [ProgressiveRequiredMediaMatrix.json](ProgressiveRequiredMediaMatrix.json).
  - **P04.C211** — **осталось**: ADTS AAC, MP3, Ogg, FLAC, WAV/PCM; дополнительные rates/layouts и многоканальный звук. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
    - **P04.C211.01** — **осталось**: ADTS AAC: оставшиеся rates/layouts. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
    - **P04.C211.02** — **осталось**: MP3: оставшиеся MPEG versions/layers и clocks. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
    - **P04.C211.03** — **осталось**: Ogg Opus/Vorbis: оставшиеся chaining/origin layouts. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
    - **P04.C211.04** — **осталось**: Raw FLAC: дополнительные rates/layouts/block strategies. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
    - **P04.C211.05** — **осталось**: WAV/RF64: float/иные depths/rates/channel masks. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
    - **P04.C211.06** — **осталось**: Многоканальный AAC/Opus/Vorbis/FLAC/PCM/AC-3/E-AC-3 в допустимых контейнерах. Источник: [ProgressiveCompletionChecklist.md:211](ProgressiveCompletionChecklist.md).
  - **P04.C212** — **готово**: SDK 1.57: самостоятельные AAC-LC и H.264 в scalar fMP4, один настоящий required track/resource без ожидания второго; проверки трёх редакций. Прочие standalone codec/container profiles остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:212](ProgressiveCompletionChecklist.md).
  - **P04.C213** — **готово**: SDK 1.58: standalone Opus mono/stereo fMP4; dOps/edit list, buffering/pause/seek/complete/release проверены отдельно на трёх редакциях. WebM/Vorbis и полная codec matrix остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:213](ProgressiveCompletionChecklist.md).
  - **P04.C214** — **готово**: SDK 1.59: standalone WebM Opus/Vorbis mono/stereo, отдельная вложенная capability, packet/header validation и испытания трёх редакций. Standalone WebM video и полная codec matrix остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:214](ProgressiveCompletionChecklist.md).
  - **P04.C215** — **готово**: SDK 1.60: standalone WebM VP8/VP9/AV1 video, шесть 8/10-bit и fractional profiles на трёх редакциях. Остальные codec/container combinations остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:215](ProgressiveCompletionChecklist.md).
  - **P04.C216** — **готово**: SDK 1.61: standalone HEVC/AV1/VP9 fMP4 video, шесть 8/10-bit/fractional profiles на трёх редакциях; остальные codec/container combinations остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:216](ProgressiveCompletionChecklist.md).
  - **P04.C217** — **готово**: SDK 1.62: standalone FLAC fMP4, восемь mono/stereo, 16/24-bit, 44.1/48-kHz profiles; PCM bit-exact после bridge. Raw FLAC и остальные audio containers/matrix остаются открытыми. Источник: [ProgressiveCompletionChecklist.md:217](ProgressiveCompletionChecklist.md).
  - **P04.C218** — **готово**: SDK 1.63: native standalone FLAC, fixed blocks, mono/stereo, 16/24-bit, 44.1/48-kHz; CRC/SHA, точные packet clocks и PCM, восемь runtime profiles каждой редакции. Остальные audio containers и layouts остаются о. Источник: [ProgressiveCompletionChecklist.md:218](ProgressiveCompletionChecklist.md).
  - **P04.C219** — **готово**: SDK 1.64: standalone ADTS AAC-LC, MPEG-4 без CRC, один raw block/frame, mono/stereo, 44.1/48 kHz; pause/seek/tail/update/EOF/release на трёх редакциях. Payload/clocks и декодированный PCM сохранены; остальные A. Источник: [ProgressiveCompletionChecklist.md:219](ProgressiveCompletionChecklist.md).
  - **P04.C220** — **готово**: SDK 1.65: standalone MPEG-1 Layer III MP3, CBR/VBR mono/stereo 44.1/48 kHz без CRC, bounded ID3, reservoir/preroll; восемь runtime profiles трёх редакций. Xing/gapless и другие MPEG audio variants открыты. Источник: [ProgressiveCompletionChecklist.md:220](ProgressiveCompletionChecklist.md).
  - **P04.C221** — **готово**: SDK 1.66: Xing/Info/VBRI standalone MP3, отдельный encoded-sample профиль с counters и сохранением raw delay/padding; 12 runtime profiles на редакцию. VBRI fixtures независимо собраны; реальный VBRI корпус и ga. Источник: [ProgressiveCompletionChecklist.md:221](ProgressiveCompletionChecklist.md).
  - **P04.C224** — **готово**: SDK 1.67: LAME Info Tag CRC и анализ границ PCM, общее ядро Qt/Classic, 72 новые native checks и диагностика при growing playback. Это проверка метаданных без применения trim; gapless controller/Classic output. Источник: [ProgressiveCompletionChecklist.md:224](ProgressiveCompletionChecklist.md).
  - **P04.C225** — **готово**: SDK 1.68: gapless MP3 в Qt, audible timeline, проверенный CRC, PCM trim/seek и ожидание writer complete. По 31 new native checks, четыре growing profiles и 16 controller PCM windows на Qt ОС; Classic явно отказ. Источник: [ProgressiveCompletionChecklist.md:225](ProgressiveCompletionChecklist.md).
  - **P04.C228** — **готово**: SDK 1.69: обычный API Classic для gapless MP3, принудительный проверенный decoder, фактический PCM status, четыре growing profiles и Stop/cancel/reopen. Повторная Qt-проверка на обеих ОС; остальные MPEG/audio p. Источник: [ProgressiveCompletionChecklist.md:228](ProgressiveCompletionChecklist.md).
  - **P04.C229** — **готово**: SDK 1.70: растущий WAV PCM16, RIFF, mono/stereo 44.1/48 kHz, известные RIFF/data sizes, побайтное сохранение PCM, partial packets и четыре runtime profiles трёх редакций. RF64/float/24-bit и остальная audio mat. Источник: [ProgressiveCompletionChecklist.md:229](ProgressiveCompletionChecklist.md).
  - **P04.C230** — **готово**: SDK 1.71: растущий Ogg Opus, один logical stream mono/stereo 48 kHz, page CRC/continuations, pre-skip/gain и end trim, четыре growing profiles трёх редакций. Ogg Vorbis, chaining/multiplexing, live и остальная. Источник: [ProgressiveCompletionChecklist.md:230](ProgressiveCompletionChecklist.md).
  - **P04.C231** — **готово**: SDK 1.72: растущий Ogg Vorbis, Vorbis I mono/stereo 48 kHz, granule origin 0 без initial trim, page CRC/continuations и EOS trim, четыре growing profiles трёх редакций. Initial trim/nonzero origin, chaining/mul. Источник: [ProgressiveCompletionChecklist.md:231](ProgressiveCompletionChecklist.md).
  - **P04.C232** — **готово**: SDK 1.73: растущий AC-3, bsid 8, mono/stereo 48 kHz, CRC1/CRC2, неизменная конфигурация, 96 native checks и четыре growing profiles трёх редакций. Остальные layouts/rates/bsid и общая audio matrix открыты. Источник: [ProgressiveCompletionChecklist.md:232](ProgressiveCompletionChecklist.md).
  - **P04.C233** — **готово**: SDK 1.74: растущий E-AC-3, независимый substream 0, bsid 16, mono/stereo 48 kHz, шесть блоков и CRC. Проверяются шесть битрейтов, 80 native checks и четыре growing profiles каждой редакции; dependent substreams. Источник: [ProgressiveCompletionChecklist.md:233](ProgressiveCompletionChecklist.md).
  - **P04.C234** — **готово**: SDK 1.75: растущий WAV PCM24, legacy RIFF tag 1, mono/stereo 44.1/48 kHz, отдельная capability и in24/enda adapter. По 65 native checks и четыре growing profiles каждой редакции; все три байта PCM, clocks и RIF. Источник: [ProgressiveCompletionChecklist.md:234](ProgressiveCompletionChecklist.md).
  - **P04.C235** — **готово**: SDK 1.76: растущий WAVEFORMATEXTENSIBLE PCM24, mono/stereo 44.1/48 kHz, PCM GUID, 24 container/valid bits и явные маски 4/3. По 83 native checks с FFmpeg и независимым struct writer, четыре growing profiles каж. Источник: [ProgressiveCompletionChecklist.md:235](ProgressiveCompletionChecklist.md).
  - **P04.C236** — **готово**: SDK 1.77: растущий RF64 PCM16/PCM24, mono/stereo 44.1/48 kHz, обязательный ds64 и согласованные 64-bit sizes/sampleCount. По 161 native checks, восемь growing profiles каждой редакции и восемь синтетических сце. Источник: [ProgressiveCompletionChecklist.md:236](ProgressiveCompletionChecklist.md).
  - **P04.C237** — **осталось**: Отсутствие аудиоустройства не делает валидный video-only источник ошибочным. Источник: [ProgressiveCompletionChecklist.md:237](ProgressiveCompletionChecklist.md).
    - **P04.C237.01** — **осталось**: Проверить валидное video-only воспроизведение без аудиоустройства в Linux Qt. Источник: [ProgressiveCompletionChecklist.md:237](ProgressiveCompletionChecklist.md).
    - **P04.C237.02** — **осталось**: Проверить тот же сценарий Windows Qt и Classic, включая ошибку backend AO. Источник: [ProgressiveCompletionChecklist.md:237](ProgressiveCompletionChecklist.md).
  - **P04.C238** — **осталось**: MPEG-2/MPEG-4 Part 2 для предусмотренных ТЗ загрузок/локальных входов. Источник: [ProgressiveCompletionChecklist.md:238](ProgressiveCompletionChecklist.md).
    - **P04.C238.01** — **осталось**: MPEG-2 video: progressive reader/remux и decoder admission. Источник: [ProgressiveCompletionChecklist.md:238](ProgressiveCompletionChecklist.md).
    - **P04.C238.02** — **осталось**: MPEG-4 Part2: configuration/B-frames и progressive playback. Источник: [ProgressiveCompletionChecklist.md:238](ProgressiveCompletionChecklist.md).
    - **P04.C238.03** — **осталось**: Подтвердить соответствующие реальные/локальные входы из ТЗ и отрицательные случаи. Источник: [ProgressiveCompletionChecklist.md:238](ProgressiveCompletionChecklist.md).

- **P05** — **частично**: Непоследовательная запись и большие ресурсы. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P05.C242** — **готово**: Дополнительная проверка SDK 1.81: предельные RF64: восемь native profiles на ОС на эффективной границе 250000 packets  / 16 GiB, отказ при превышении, по два фактических предельных playback profiles каждой реда. Источник: [ProgressiveCompletionChecklist.md:242](ProgressiveCompletionChecklist.md).
  - **P05.C243** — **готово**: Дополнительная проверка SDK 1.81: физические sparse RF64 >4 GiB, PCM16/PCM24 stereo 48 kHz во всех трёх редакциях. Настоящие незаписанные дыры, дальний индексный seek без сканирования 4 GiB, обратный переход, о. Источник: [ProgressiveCompletionChecklist.md:243](ProgressiveCompletionChecklist.md).
  - **P05.C244** — **готово**: SDK 1.81: отдельный sparse RF64 audio playback, стабильные offsets, фактический seek в островок и обратно, ожидание/заполнение дыр, pause/EOF/release и cancel-at-hole. По 272 native checks на ОС и четыре runtim. Источник: [ProgressiveCompletionChecklist.md:244](ProgressiveCompletionChecklist.md).
  - **P05.C245** — **готово**: SDK 1.80: геометрическая RF64 packet map, точные интервалы готовых пакетов в том числе за дырой; по 242 native checks, 1608 synthetic geometry cases и actual status трёх редакций — проверки. Это диагностика без. Источник: [ProgressiveCompletionChecklist.md:245](ProgressiveCompletionChecklist.md).
  - **P05.C246** — **готово**: SDK 1.79: RF64 publication вне порядка, непрерывный head prefix, ожидание дыр и same-reader resume с сохранением ручной паузы. По 229 native checks на ОС и четыре runtime profiles каждой редакции; отчёт. Seek в. Источник: [ProgressiveCompletionChecklist.md:246](ProgressiveCompletionChecklist.md).
  - **P05.C247** — **готово**: SDK 1.78: внешний digest index для RF64 и физические PCM16/PCM24 stereo 48 kHz больше 4 GiB во всех трёх редакциях — контракт, сценарий, проверки. Это ограниченный результат; следующие общие пункты остаются отк. Источник: [ProgressiveCompletionChecklist.md:247](ProgressiveCompletionChecklist.md).
  - **P05.C248** — **осталось**: Карта ranges в оставшихся playback-режимах: sparse holes, out-of-order запись и доступные островки данных. Источник: [ProgressiveCompletionChecklist.md:248](ProgressiveCompletionChecklist.md).
    - **P05.C248.01** — **осталось**: Sparse holes в ещё не покрытых playback modes. Источник: [ProgressiveCompletionChecklist.md:248](ProgressiveCompletionChecklist.md).
    - **P05.C248.02** — **осталось**: Out-of-order публикация диапазонов и отдельные доступные островки. Источник: [ProgressiveCompletionChecklist.md:248](ProgressiveCompletionChecklist.md).
    - **P05.C248.03** — **осталось**: Seek и готовность по фактической карте диапазонов, а не по сплошному размеру файла. Источник: [ProgressiveCompletionChecklist.md:248](ProgressiveCompletionChecklist.md).
  - **P05.C249** — **осталось**: Незаписанные дыры не читаются как нули; file size не заменяет подтверждение writer. Источник: [ProgressiveCompletionChecklist.md:249](ProgressiveCompletionChecklist.md).
    - **P05.C249.01** — **осталось**: Незаписанные holes не выдаются декодеру как нули во всех допущенных путях. Источник: [ProgressiveCompletionChecklist.md:249](ProgressiveCompletionChecklist.md).
    - **P05.C249.02** — **осталось**: Физический размер файла не заменяет опубликованные ranges/integrity. Источник: [ProgressiveCompletionChecklist.md:249](ProgressiveCompletionChecklist.md).
  - **P05.C250** — **осталось**: Масштабируемые media indices и внешний индекс digest без огромного командного JSON. Источник: [ProgressiveCompletionChecklist.md:250](ProgressiveCompletionChecklist.md).
    - **P05.C250.01** — **осталось**: Масштабировать media indices до согласованного большого корпуса. Источник: [ProgressiveCompletionChecklist.md:250](ProgressiveCompletionChecklist.md).
    - **P05.C250.02** — **осталось**: Расширить внешний digest index за пределами уже проверенного RF64 среза. Источник: [ProgressiveCompletionChecklist.md:250](ProgressiveCompletionChecklist.md).
    - **P05.C250.03** — **осталось**: Передавать большие наборы без огромного JSON и проверить память/дисковый кэш. Источник: [ProgressiveCompletionChecklist.md:250](ProgressiveCompletionChecklist.md).
  - **P05.C251** — **осталось**: Быстрый поиск/старт большого каталога без полного предварительного сканирования. Источник: [ProgressiveCompletionChecklist.md:251](ProgressiveCompletionChecklist.md).
    - **P05.C251.01** — **осталось**: Старт большого каталога сегментов без полного предварительного сканирования. Источник: [ProgressiveCompletionChecklist.md:251](ProgressiveCompletionChecklist.md).
    - **P05.C251.02** — **осталось**: Быстрый seek и reopen большого каталога с ограниченным числом handles. Источник: [ProgressiveCompletionChecklist.md:251](ProgressiveCompletionChecklist.md).

- **P06** — **частично**: Временная шкала и буферизация. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P06.C140** — **готово**: SDK 1.83, публичные fractional/VFR SDK и обычные плееры: по четыре физически растущих H.264/B-frame профиля каждой редакции (25, 30000/1001, 60000/1001 и VFR), 9717 точных packet records/декодированных кадров m. Источник: [ProgressiveCompletionChecklist.md:140](ProgressiveCompletionChecklist.md).
  - **P06.C161** — **готово**: Внутреннее временное покрытие continuous cache: объединение подтверждённых PTS-интервалов без перескакивания B-frame holes, точный конечный AAC не ограничивает длинное видео, ошибка/cleanup обнуляют покрытие. П. Источник: [ProgressiveCompletionChecklist.md:161](ProgressiveCompletionChecklist.md).
  - **P06.C162** — **готово**: Старт с позиции 1 s в растущем AAC-кэше: шесть профилей трёх редакций, 18 новых +99 прежних Player/graph сценариев, Qt PCM/file oracle и Classic fresh-PNS/raw oracle/NV12 hashes. Только начальная позиция до пер. Источник: [ProgressiveCompletionChecklist.md:162](ProgressiveCompletionChecklist.md).
  - **P06.C165** — **готово**: SDK-кандидат 1.86: bounded повторные переходы: `control(seek, allow_growing=True)`, 16 попыток до complete, подтверждённый prefix, непринятый target сохраняет playback; Qt reload того же protected cache/URI, Cl. Источник: [ProgressiveCompletionChecklist.md:165](ProgressiveCompletionChecklist.md).
  - **P06.C166** — **готово**: SDK-кандидат 1.87: одна ожидающая цель: opt-in `wait_for_range=True`, latest-target superseding, `cancelSeek`, применение таймером приложения без playback polling; прежнее воспроизведение до commit. Невалидный. Источник: [ProgressiveCompletionChecklist.md:166](ProgressiveCompletionChecklist.md).
  - **P06.C167** — **готово**: Ожидающий переход на таймлайне: Qt/Classic, отдельная метка, ПКМ для отмены, superseding, очистка после Stop/replacement/error; фактическая позиция отделена от цели. Проверки: 222 main cases, 40 Qt Window cases. Источник: [ProgressiveCompletionChecklist.md:167](ProgressiveCompletionChecklist.md).
  - **P06.C255** — **осталось**: VFR, разные timebase, ненулевые/отрицательные offsets, preroll, edit lists и gapless metadata во всей матрице. Источник: [ProgressiveCompletionChecklist.md:255](ProgressiveCompletionChecklist.md).
    - **P06.C255.01** — **осталось**: Ненулевые и отрицательные origins/offsets в оставшейся матрице. Источник: [ProgressiveCompletionChecklist.md:255](ProgressiveCompletionChecklist.md).
    - **P06.C255.02** — **осталось**: Разные timebase и fractional clocks в ещё не покрытых сочетаниях. Источник: [ProgressiveCompletionChecklist.md:255](ProgressiveCompletionChecklist.md).
    - **P06.C255.03** — **осталось**: Preroll/edit lists/gapless за пределами известных AAC/Opus профилей. Источник: [ProgressiveCompletionChecklist.md:255](ProgressiveCompletionChecklist.md).
    - **P06.C255.04** — **осталось**: VFR/endpoint semantics и неизвестные длина/конечная точка за пределами опубликованных MP4/WebM режимов. Источник: [ProgressiveCompletionChecklist.md:255](ProgressiveCompletionChecklist.md).
  - **P06.C256** — **осталось**: Нужные варианты TS access units, discontinuity и rollover транспортных часов. Источник: [ProgressiveCompletionChecklist.md:256](ProgressiveCompletionChecklist.md).
    - **P06.C256.01** — **осталось**: TS access units для предусмотренных ещё не покрытых кодеков. Источник: [ProgressiveCompletionChecklist.md:256](ProgressiveCompletionChecklist.md).
    - **P06.C256.02** — **осталось**: TS discontinuity без ложного EOF/скачка общей шкалы. Источник: [ProgressiveCompletionChecklist.md:256](ProgressiveCompletionChecklist.md).
    - **P06.C256.03** — **осталось**: Rollover транспортных часов и восстановление после seek. Источник: [ProgressiveCompletionChecklist.md:256](ProgressiveCompletionChecklist.md).
  - **P06.C257** — **осталось**: Доступная граница по обязательным выбранным потокам; исходные PTS/DTS не обнуляются независимо. Источник: [ProgressiveCompletionChecklist.md:257](ProgressiveCompletionChecklist.md).
    - **P06.C257.01** — **осталось**: Общая доступная граница обязательных выбранных потоков во всей матрице. Источник: [ProgressiveCompletionChecklist.md:257](ProgressiveCompletionChecklist.md).
    - **P06.C257.02** — **осталось**: Сохранение относительных PTS/DTS при разных началах дорожек. Источник: [ProgressiveCompletionChecklist.md:257](ProgressiveCompletionChecklist.md).
  - **P06.C258** — **осталось**: Start/resume/buffer policies, ручная пауза и восстановление синхронизации после ожидания/seek. Источник: [ProgressiveCompletionChecklist.md:258](ProgressiveCompletionChecklist.md).
    - **P06.C258.01** — **осталось**: Проверить start10s/restart5s/low-water1s и короткие законченные источники во всей матрице. Источник: [ProgressiveCompletionChecklist.md:258](ProgressiveCompletionChecklist.md).
    - **P06.C258.02** — **осталось**: Сохранить ручную паузу при append/complete во всём корпусе. Источник: [ProgressiveCompletionChecklist.md:258](ProgressiveCompletionChecklist.md).
    - **P06.C258.03** — **осталось**: Восстановить A/V sync после ожидания/seek без постоянного offset workaround. Источник: [ProgressiveCompletionChecklist.md:258](ProgressiveCompletionChecklist.md).
  - **P06.VFR** — **готово**: BlockDuration VFR (SDK1.101). Источник: [ProgressiveVfrExecutionChecklist.md](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-01** — **готово**: Реализовать отдельный режим и неизменяемые videoSamples/audioSamples. Источник: [ProgressiveVfrExecutionChecklist.md:24](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-02** — **готово**: Реализовать структурную обработку finite/unknown Clusters, проверку BlockDuration, PTS, endpoints и references. Источник: [ProgressiveVfrExecutionChecklist.md:25](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-03** — **готово**: Реализовать разрешение отсутствующего DefaultDuration только для явно выбранного VFR-пути. Источник: [ProgressiveVfrExecutionChecklist.md:26](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-04** — **готово**: Подключить проверку фактических интервалов к кэшу и состоянию готовности. Источник: [ProgressiveVfrExecutionChecklist.md:27](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-05** — **готово**: Подключить SDK/capabilities и проверить типы, идентичность и совместимость модулей: 53 + 19 + 63 unit tests. Источник: [ProgressiveVfrExecutionChecklist.md:28](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-06** — **готово**: Реализовать передачу VFR-флага в повторный FragmentIndex Classic; финальный функциональный прогон учитывается отдельно. Источник: [ProgressiveVfrExecutionChecklist.md:29](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-07** — **готово**: Выполнить первичный Linux Qt прогон: 6 ordinary-main сценариев и 6 PCM captures, с отдельными профилями. Источник: [ProgressiveVfrExecutionChecklist.md:30](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-08** — **готово**: Выполнить первичный Windows Qt ordinary-main прогон: 6 сценариев. Источник: [ProgressiveVfrExecutionChecklist.md:31](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-09** — **готово**: Проверить окончательный ordinary-main VFR-профиль всех трёх редакций, включая исправленный Classic. Источник: [ProgressiveVfrExecutionChecklist.md:32](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-10** — **готово**: Проверить PCM окончательных Qt/Classic сборок во всех трёх редакциях. Источник: [ProgressiveVfrExecutionChecklist.md:33](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-11** — **готово**: Завершить финальные worker/fault/negative проверки, sanitizer gates и прежние CFR/NTSC/25fps/AAC регрессии. Источник: [ProgressiveVfrExecutionChecklist.md:34](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-12** — **готово**: Свести результаты трёх редакций, точные exits, SHA и контроль сохранности установок/регистрации. Источник: [ProgressiveVfrExecutionChecklist.md:35](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-13** — **готово**: Атомарно обновить установленный Linux с backup и неизменным профилем; проверить его собственный SDK. Источник: [ProgressiveVfrExecutionChecklist.md:36](ProgressiveVfrExecutionChecklist.md).
    - **P06.VFR.VFR-14** — **готово**: Обновить инструкции/матрицу, проверить чистый SDK, сохранить commit и push. Источник: [ProgressiveVfrExecutionChecklist.md:37](ProgressiveVfrExecutionChecklist.md).
  - **P06.SIMPLE** — **готово**: SimpleBlock VFR, подготовка и runtime (SDK1.102). Источник: [ProgressiveSimpleVfrExecutionChecklist.md](ProgressiveSimpleVfrExecutionChecklist.md), [ProgressiveSimpleVfrProductionChecklist.md](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVP-01** — **готово**: Подготовить SimpleBlock VFR fixtures без DefaultDuration: VP9 p0/p2, fractional cadence, Opus mono/stereo, finite/unknown Clusters. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:8](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-02** — **готово**: Реализовать тестовый incremental parser, читающий только подтверждённые bytes и соблюдающий bounds. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:9](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-03** — **готово**: Проверить next-PTS lookahead, удержание последнего кадра и подтверждённый EOF без ложного complete. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:10](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-04** — **готово**: Проверить перепаковку BlockDuration и удаление устаревших SeekHead/Cues/Position/PrevSize/CRC без изменения packets/clocks/trim. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:11](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-05** — **готово**: Проверить partial headers/payloads, clock/count/EOF errors и лимиты pending buffers. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:12](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-06** — **готово**: Проверить canonical output через существующий VFR worker и настоящие PCM-декодеры трёх редакций. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:13](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-07** — **готово**: Свести exits, SHA, packet/PCM/video сравнения, сохранность исходников, установок и HKCU. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:14](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVP-08** — **готово**: Обновить контракт следующего production-режима, воспроизводимые тесты и опубликовать результаты. Источник: [ProgressiveSimpleVfrExecutionChecklist.md:15](ProgressiveSimpleVfrExecutionChecklist.md).
    - **P06.SIMPLE.SVC-01** — **готово**: Зафиксировать отдельный source mode и границы без изменения старого VFR admission. Источник: [ProgressiveSimpleVfrProductionChecklist.md:6](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-02** — **готово**: Подключить bounded C++ incremental scanner и next-PTS lookahead. Источник: [ProgressiveSimpleVfrProductionChecklist.md:7](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-03** — **готово**: Подключить динамический canonical output к worker/cache без final-size oracle. Источник: [ProgressiveSimpleVfrProductionChecklist.md:8](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-04** — **готово**: Согласовать mode identity, frame/audio plan и decoder adapters Qt/Classic. Источник: [ProgressiveSimpleVfrProductionChecklist.md:9](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-05** — **готово**: Добавить SDK/capabilities и отрицательные проверки контракта. Источник: [ProgressiveSimpleVfrProductionChecklist.md:10](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-06** — **готово**: Проверить worker, partial structures/EOF, metadata, packets/clocks/video/PCM на Linux. Источник: [ProgressiveSimpleVfrProductionChecklist.md:11](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-07** — **готово**: Проверить Windows Qt worker и PCM. Источник: [ProgressiveSimpleVfrProductionChecklist.md:12](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-08** — **готово**: Проверить Classic worker и PCM с отдельным INI и неизменным HKCU. Источник: [ProgressiveSimpleVfrProductionChecklist.md:13](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-09** — **готово**: Проверить ordinary Linux Qt: ≥10 s до complete, pause/seek/queued target/EOF/release. Источник: [ProgressiveSimpleVfrProductionChecklist.md:14](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-10** — **готово**: Проверить те же ordinary-main сценарии Windows Qt. Источник: [ProgressiveSimpleVfrProductionChecklist.md:15](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-11** — **готово**: Проверить те же ordinary-main сценарии Classic. Источник: [ProgressiveSimpleVfrProductionChecklist.md:16](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-12** — **готово**: Проверить регрессии старых VFR/CFR/NTSC/25fps/AAC и sanitizer bounds. Источник: [ProgressiveSimpleVfrProductionChecklist.md:17](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-13** — **готово**: Свести отчёт, обновить матрицу/план, проверить профили/установки. Источник: [ProgressiveSimpleVfrProductionChecklist.md:18](ProgressiveSimpleVfrProductionChecklist.md).
    - **P06.SIMPLE.SVC-14** — **готово**: Опубликовать исходники/clean SDK и атомарно обновить Linux с backup и неизменным INI. Источник: [ProgressiveSimpleVfrProductionChecklist.md:19](ProgressiveSimpleVfrProductionChecklist.md).
  - **P06.AUTO** — **готово**: Производный video count (SDK1.103). Источник: [ProgressiveAutoVfrExecutionChecklist.md](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-01** — **готово**: Зафиксировать отдельный source mode и границы без изменения старого VFR admission. Источник: [ProgressiveAutoVfrExecutionChecklist.md:6](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-02** — **готово**: Подключить bounded C++ scanner с ограниченным производным счётчиком и next-PTS lookahead. Источник: [ProgressiveAutoVfrExecutionChecklist.md:7](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-03** — **готово**: Подключить динамический canonical output к worker/cache без final-size oracle. Источник: [ProgressiveAutoVfrExecutionChecklist.md:8](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-04** — **готово**: Согласовать mode identity, производный video count и заданный audio plan и decoder adapters Qt/Classic. Источник: [ProgressiveAutoVfrExecutionChecklist.md:9](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-05** — **готово**: Добавить SDK/capabilities и отрицательные проверки контракта. Источник: [ProgressiveAutoVfrExecutionChecklist.md:10](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-06** — **готово**: Проверить worker, partial structures/EOF, metadata, packets/clocks/video/PCM на Linux. Источник: [ProgressiveAutoVfrExecutionChecklist.md:11](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-07** — **готово**: Проверить Windows Qt worker и PCM. Источник: [ProgressiveAutoVfrExecutionChecklist.md:12](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-08** — **готово**: Проверить Classic worker и PCM с отдельным INI и неизменным HKCU. Источник: [ProgressiveAutoVfrExecutionChecklist.md:13](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-09** — **готово**: Проверить ordinary Linux Qt: ≥10 s до complete, pause/seek/queued target/EOF/release. Источник: [ProgressiveAutoVfrExecutionChecklist.md:14](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-10** — **готово**: Проверить те же ordinary-main сценарии Windows Qt. Источник: [ProgressiveAutoVfrExecutionChecklist.md:15](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-11** — **готово**: Проверить те же ordinary-main сценарии Classic. Источник: [ProgressiveAutoVfrExecutionChecklist.md:16](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-12** — **готово**: Проверить регрессии старых VFR/CFR/NTSC/25fps/AAC и sanitizer bounds. Источник: [ProgressiveAutoVfrExecutionChecklist.md:17](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-13** — **готово**: Свести отчёт, обновить матрицу/план, проверить профили/установки. Источник: [ProgressiveAutoVfrExecutionChecklist.md:18](ProgressiveAutoVfrExecutionChecklist.md).
    - **P06.AUTO.VAC-14** — **готово**: Опубликовать исходники/clean SDK и атомарно обновить Linux с backup и неизменным INI. Источник: [ProgressiveAutoVfrExecutionChecklist.md:19](ProgressiveAutoVfrExecutionChecklist.md).
  - **P06.NOMINAL** — **готово**: Номинальный DefaultDuration (SDK1.104). Источник: [ProgressiveNominalVfrExecutionChecklist.md](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-01** — **готово**: Зафиксировать отдельный source mode и границы без изменения старого VFR admission. Источник: [ProgressiveNominalVfrExecutionChecklist.md:6](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-02** — **готово**: Подключить bounded C++ admission номинального DefaultDuration, без изменения next-PTS интервалов. Источник: [ProgressiveNominalVfrExecutionChecklist.md:7](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-03** — **готово**: Подключить динамический canonical output к worker/cache без final-size oracle. Источник: [ProgressiveNominalVfrExecutionChecklist.md:8](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-04** — **готово**: Согласовать mode identity, производный video count и заданный audio plan и decoder adapters Qt/Classic. Источник: [ProgressiveNominalVfrExecutionChecklist.md:9](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-05** — **готово**: Добавить SDK/capabilities и отрицательные проверки контракта. Источник: [ProgressiveNominalVfrExecutionChecklist.md:10](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-06** — **готово**: Проверить worker, partial structures/EOF, metadata, packets/clocks/video/PCM на Linux. Источник: [ProgressiveNominalVfrExecutionChecklist.md:11](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-07** — **готово**: Проверить Windows Qt worker и PCM. Источник: [ProgressiveNominalVfrExecutionChecklist.md:12](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-08** — **готово**: Проверить Classic worker и PCM с отдельным INI и неизменным HKCU. Источник: [ProgressiveNominalVfrExecutionChecklist.md:13](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-09** — **готово**: Проверить ordinary Linux Qt: ≥10 s до complete, pause/seek/queued target/EOF/release. Источник: [ProgressiveNominalVfrExecutionChecklist.md:14](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-10** — **готово**: Проверить те же ordinary-main сценарии Windows Qt. Источник: [ProgressiveNominalVfrExecutionChecklist.md:15](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-11** — **готово**: Проверить те же ordinary-main сценарии Classic. Источник: [ProgressiveNominalVfrExecutionChecklist.md:16](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-12** — **готово**: Проверить регрессии старых VFR/CFR/NTSC/25fps/AAC и sanitizer bounds. Источник: [ProgressiveNominalVfrExecutionChecklist.md:17](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-13** — **готово**: Свести отчёт, обновить матрицу/план, проверить профили/установки. Источник: [ProgressiveNominalVfrExecutionChecklist.md:18](ProgressiveNominalVfrExecutionChecklist.md).
    - **P06.NOMINAL.NVF-14** — **готово**: Опубликовать исходники/clean SDK и атомарно обновить Linux с backup и неизменным INI. Источник: [ProgressiveNominalVfrExecutionChecklist.md:19](ProgressiveNominalVfrExecutionChecklist.md).
  - **P06.AUDIO** — **готово**: Производный audio count (SDK1.105). Источник: [ProgressiveAudioCountExecutionChecklist.md](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-01** — **готово**: Зафиксировать семантику EOF, pre-skip/end-trim и границу потери целого пакета. Источник: [ProgressiveAudioCountExecutionChecklist.md:7](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-02** — **готово**: Проверить bounded incremental prototype без внешнего audioSamples. Источник: [ProgressiveAudioCountExecutionChecklist.md:8](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-03** — **готово**: Проверить partial packet, premature EOF, clocks/trim/framing и budgets. Источник: [ProgressiveAudioCountExecutionChecklist.md:9](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-04** — **готово**: Сверить derived count с независимым decoded PCM для full/short/zero-trim/minimal/VFR fixtures. Источник: [ProgressiveAudioCountExecutionChecklist.md:10](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-05** — **готово**: Проверить canonical output через существующие worker/PCM adapters трёх редакций, явно передавая известный count только после prototype EOF. Источник: [ProgressiveAudioCountExecutionChecklist.md:11](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-06** — **готово**: Подтвердить неизменность SDK/capabilities, установок/профилей и HKCU; не объявлять preflight новым playback mode. Источник: [ProgressiveAudioCountExecutionChecklist.md:12](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-07** — **готово**: Опубликовать воспроизводимые тесты, отчёт и production план. Источник: [ProgressiveAudioCountExecutionChecklist.md:13](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-08** — **готово**: Перенести bounded derived-audio state machine в рабочий C++ scanner. Источник: [ProgressiveAudioCountExecutionChecklist.md:14](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-09** — **готово**: Подключить worker/start/result без внешнего audioSamples и с точной mode identity. Источник: [ProgressiveAudioCountExecutionChecklist.md:15](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-10** — **готово**: Подключить независимую проверку cache count/trim/coverage; отделить provisional track end от writer EOF. Источник: [ProgressiveAudioCountExecutionChecklist.md:16](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-11** — **готово**: Подключить decoder adapters Qt/Classic, включая short audio, pause/seek и repeat indexing. Источник: [ProgressiveAudioCountExecutionChecklist.md:17](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-12** — **готово**: Добавить совместимый SDK/capabilities и отрицательные проверки, сохранив старые known-plan modes. Источник: [ProgressiveAudioCountExecutionChecklist.md:18](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-13** — **готово**: Проверить ordinary Linux Qt ≥10 s до complete, pause/growing/queued seek/EOF/release. Источник: [ProgressiveAudioCountExecutionChecklist.md:19](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-14** — **готово**: Проверить те же ordinary-main сценарии Windows Qt. Источник: [ProgressiveAudioCountExecutionChecklist.md:20](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-15** — **готово**: Проверить те же ordinary-main сценарии Classic, прежние modes и неизменность HKCU. Источник: [ProgressiveAudioCountExecutionChecklist.md:21](ProgressiveAudioCountExecutionChecklist.md).
    - **P06.AUDIO.ADC-16** — **готово**: Проверить sanitizer/resource bounds; clean SDK, три сборки и атомарное обновление Linux с backup/неизменным профилем. Источник: [ProgressiveAudioCountExecutionChecklist.md:22](ProgressiveAudioCountExecutionChecklist.md).
  - **P06.OPUS** — **готово**: Переменные Opus packets/pre-skip/DiscardPadding (SDK1.106). Источник: [ProgressiveOpusGeometryExecutionChecklist.md](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-01** — **готово**: Зафиксировать TOC/framing, packet duration, pre-skip и end-trim без внешнего audioSamples. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:11](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-02** — **готово**: Проверить bounded incremental test-only clock: confirmed bytes, partial packets, count final только после writer EOF. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:12](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-03** — **готово**: Подготовить реальные Opus-пакеты2.5/5/10/20/40/60/80/100/120ms, mono/stereo и finite/unknown Clusters. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:13](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-04** — **готово**: Подготовить чередующиеся20–120ms packets одного непрерывного codec state; сверить compressed packets/PTS/DTS, decoded video/PCM до/после canonical conversion. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:14](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-05** — **готово**: Проверить framing/clock/trim, premature EOF и sample/packet budgets; сохранить отказы старого режима. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:15](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-06** — **готово**: Проверить production TOC parser GNU/MinGW/MSVC и bounded sanitizer mutations. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:16](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-07** — **готово**: Измерить completed-file PCM реальных Qt/Classic decoder paths; явно сохранить расхождения и не выдавать file controls за progressive playback. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:17](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-08** — **готово**: Опубликовать отчёт, матрицу и production план; подтвердить неизменность SDK, установок/профилей и HKCU. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:18](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-09** — **готово**: Реализовать отдельную production clock/profile identity с длительностью из packet bytes и явным pre-skip. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:19](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-10** — **готово**: Подключить scanner/sealer/worker, сохранив bounded audio/video lookahead и прежний960-sample mode. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:20](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-11** — **готово**: Подключить независимый cache recount, variable PCM coverage и final-counter verification без угадывания EOF. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:21](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-12** — **готово**: Подключить Qt/Classic adapters с точными pre-skip/end-trim и seek по сумме длительностей; сохранить старый ABI/admission. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:22](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-13** — **готово**: Добавить совместимый API/SDK/capabilities и точные отрицательные reason codes после runtime подключения. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:23](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-14** — **готово**: Проверить ordinary Linux Qt ≥10s до complete, pause/growing/queued seek/EOF/release и growing PCM. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:24](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-15** — **готово**: Проверить те же сценарии Windows Qt. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:25](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-16** — **готово**: Проверить те же сценарии Classic с private INI и HKCU before/after. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:26](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-17** — **готово**: Проверить прежние modes, sanitizer/resource bounds и неодинаковые длительности при short/zero-trim audio. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:27](ProgressiveOpusGeometryExecutionChecklist.md).
    - **P06.OPUS.OPG-18** — **готово**: Подготовить три проверенные сборки/clean SDK и атомарно обновить Linux с backup/неизменным профилем. Источник: [ProgressiveOpusGeometryExecutionChecklist.md:28](ProgressiveOpusGeometryExecutionChecklist.md).

- **P07** — **частично**: Конечные HLS/DASH. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P07.C262** — **осталось**: Локальные init/media segments, параллельное получение, gaps, discontinuity и rebasing. Источник: [ProgressiveCompletionChecklist.md:262](ProgressiveCompletionChecklist.md).
    - **P07.C262.01** — **осталось**: Локальные init/media HLS/DASH segments вне текущего узкого admission. Источник: [ProgressiveCompletionChecklist.md:262](ProgressiveCompletionChecklist.md).
    - **P07.C262.02** — **осталось**: Параллельное получение сегментов и gaps. Источник: [ProgressiveCompletionChecklist.md:262](ProgressiveCompletionChecklist.md).
    - **P07.C262.03** — **осталось**: Discontinuity/rebasing общей сегментной шкалы. Источник: [ProgressiveCompletionChecklist.md:262](ProgressiveCompletionChecklist.md).
  - **P07.C263** — **осталось**: Расширенная сегментная codec/container matrix, включая Vorbis где применимо. Источник: [ProgressiveCompletionChecklist.md:263](ProgressiveCompletionChecklist.md).
    - **P07.C263.01** — **осталось**: Зафиксировать конечную расширенную segmented codec/container матрицу. Источник: [ProgressiveCompletionChecklist.md:263](ProgressiveCompletionChecklist.md).
    - **P07.C263.02** — **осталось**: Подключить и проверить недостающие video/audio пары. Источник: [ProgressiveCompletionChecklist.md:263](ProgressiveCompletionChecklist.md).
    - **P07.C263.03** — **осталось**: Vorbis в допустимых сегментных входах. Источник: [ProgressiveCompletionChecklist.md:263](ProgressiveCompletionChecklist.md).
  - **P07.C264** — **осталось**: Состояние аудио между сегментами, смена init/config и безопасный доступ к ключевым кадрам. Источник: [ProgressiveCompletionChecklist.md:264](ProgressiveCompletionChecklist.md).
    - **P07.C264.01** — **осталось**: Сохранить audio decoder state между сегментами. Источник: [ProgressiveCompletionChecklist.md:264](ProgressiveCompletionChecklist.md).
    - **P07.C264.02** — **осталось**: Обработать смену init/config на безопасной decode point. Источник: [ProgressiveCompletionChecklist.md:264](ProgressiveCompletionChecklist.md).
    - **P07.C264.03** — **осталось**: Не открывать ещё недоступный keyframe/инициализацию. Источник: [ProgressiveCompletionChecklist.md:264](ProgressiveCompletionChecklist.md).
  - **P07.C265** — **осталось**: Длинные каталоги, ограниченные handles/cache и повторное открытие ресурсов. Источник: [ProgressiveCompletionChecklist.md:265](ProgressiveCompletionChecklist.md).
    - **P07.C265.01** — **осталось**: Длинные сегментные каталоги и Unicode. Источник: [ProgressiveCompletionChecklist.md:265](ProgressiveCompletionChecklist.md).
    - **P07.C265.02** — **осталось**: Bounded handles/cache при большом числе ресурсов. Источник: [ProgressiveCompletionChecklist.md:265](ProgressiveCompletionChecklist.md).
    - **P07.C265.03** — **осталось**: Retire/reopen ресурсов без потери identity. Источник: [ProgressiveCompletionChecklist.md:265](ProgressiveCompletionChecklist.md).
  - **P07.BASE** — **готово**: Ограниченный локальный segmented профиль; не полный HLS/DASH допуск. Источник: [ProgressiveSegments.md](ProgressiveSegments.md), [ProgressiveSegmentStaging.md](ProgressiveSegmentStaging.md), [ProgressiveSegmentPlayback.md](ProgressiveSegmentPlayback.md), [ProgressiveSegmentHevc.md](ProgressiveSegmentHevc.md), [ProgressiveSegmentAv1.md](ProgressiveSegmentAv1.md), [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md), [ProgressiveWebmSegments.md](ProgressiveWebmSegments.md), [ProgressiveSegmentCatalog.md](ProgressiveSegmentCatalog.md).
    - **P07.BASE.01** — **готово**: Сегментный timeline/schema и диагностический probe. Источник: [ProgressiveSegments.md](ProgressiveSegments.md).
    - **P07.BASE.02** — **готово**: Безопасный bounded segment staging. Источник: [ProgressiveSegmentStaging.md](ProgressiveSegmentStaging.md).
    - **P07.BASE.03** — **готово**: Segmented H.264/AAC-LC fMP4 playback. Источник: [ProgressiveSegmentPlayback.md](ProgressiveSegmentPlayback.md).
    - **P07.BASE.04** — **готово**: Segmented HEVC fMP4 playback. Источник: [ProgressiveSegmentHevc.md](ProgressiveSegmentHevc.md).
    - **P07.BASE.05** — **готово**: Segmented AV1 fMP4 playback. Источник: [ProgressiveSegmentAv1.md](ProgressiveSegmentAv1.md).
    - **P07.BASE.06** — **готово**: Segmented VP9/Opus fMP4 playback. Источник: [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md).
    - **P07.BASE.07** — **готово**: Ограниченные WebM segments. Источник: [ProgressiveWebmSegments.md](ProgressiveWebmSegments.md).
    - **P07.BASE.08** — **готово**: Каталог сегментов и ограниченный cache/handles профиль. Источник: [ProgressiveSegmentCatalog.md](ProgressiveSegmentCatalog.md).

- **P08** — **осталось**: Смена качества и конфигурации. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P08.C269** — **осталось**: Контролируемая переинициализация decoder на подходящей точке декодирования. Источник: [ProgressiveCompletionChecklist.md:269](ProgressiveCompletionChecklist.md).
    - **P08.C269.01** — **осталось**: Найти допустимую decode point новой конфигурации. Источник: [ProgressiveCompletionChecklist.md:269](ProgressiveCompletionChecklist.md).
    - **P08.C269.02** — **осталось**: Переинициализировать decoder и проверить old/new isolation. Источник: [ProgressiveCompletionChecklist.md:269](ProgressiveCompletionChecklist.md).
  - **P08.C270** — **осталось**: Сохранение позиции, pause, speed, volume и выбранных дорожек. Источник: [ProgressiveCompletionChecklist.md:270](ProgressiveCompletionChecklist.md).
    - **P08.C270.01** — **осталось**: Сохранить позицию и ручную паузу при смене качества. Источник: [ProgressiveCompletionChecklist.md:270](ProgressiveCompletionChecklist.md).
    - **P08.C270.02** — **осталось**: Сохранить speed/volume и выбранные audio/subtitle tracks. Источник: [ProgressiveCompletionChecklist.md:270](ProgressiveCompletionChecklist.md).
  - **P08.C271** — **осталось**: Недостаточные данные новой конфигурации дают ожидание с точной причиной. Источник: [ProgressiveCompletionChecklist.md:271](ProgressiveCompletionChecklist.md).
    - **P08.C271.01** — **осталось**: Ожидать недостающие initialization/configuration bytes. Источник: [ProgressiveCompletionChecklist.md:271](ProgressiveCompletionChecklist.md).
    - **P08.C271.02** — **осталось**: Выдать точную причину и продолжить после публикации без второго скачивания. Источник: [ProgressiveCompletionChecklist.md:271](ProgressiveCompletionChecklist.md).

- **P09** — **частично**: Динамические аудиодорожки. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P09.C275** — **осталось**: Несколько audio в muxed и segmented источниках; динамическое добавление/отключение. Источник: [ProgressiveCompletionChecklist.md:275](ProgressiveCompletionChecklist.md).
    - **P09.C275.01** — **осталось**: Несколько audio в muxed источнике за пределами текущего admission. Источник: [ProgressiveCompletionChecklist.md:275](ProgressiveCompletionChecklist.md).
    - **P09.C275.02** — **осталось**: Несколько audio в segmented источнике. Источник: [ProgressiveCompletionChecklist.md:275](ProgressiveCompletionChecklist.md).
    - **P09.C275.03** — **осталось**: Позднее добавление и удаление audio без перезапуска загрузки. Источник: [ProgressiveCompletionChecklist.md:275](ProgressiveCompletionChecklist.md).
  - **P09.C276** — **осталось**: Стабильные ID, language/label/default/forced и сохранение выбора. Источник: [ProgressiveCompletionChecklist.md:276](ProgressiveCompletionChecklist.md).
    - **P09.C276.01** — **осталось**: Стабильные track IDs при revisions/reconnect. Источник: [ProgressiveCompletionChecklist.md:276](ProgressiveCompletionChecklist.md).
    - **P09.C276.02** — **осталось**: Сохранять language/label/default/forced и пользовательский выбор. Источник: [ProgressiveCompletionChecklist.md:276](ProgressiveCompletionChecklist.md).
  - **P09.C277** — **осталось**: Невыбранные optional tracks не тормозят просмотр; выбранная недокачанная дорожка ждёт свои данные. Источник: [ProgressiveCompletionChecklist.md:277](ProgressiveCompletionChecklist.md).
    - **P09.C277.01** — **осталось**: Невыбранные optional audio не ограничивают primary. Источник: [ProgressiveCompletionChecklist.md:277](ProgressiveCompletionChecklist.md).
    - **P09.C277.02** — **осталось**: Выбранная partial audio ждёт только необходимые диапазоны без молчаливой замены языка. Источник: [ProgressiveCompletionChecklist.md:277](ProgressiveCompletionChecklist.md).
  - **P09.C278** — **осталось**: Ошибка optional audio изолируется и объясняется; язык не подменяется молча. Источник: [ProgressiveCompletionChecklist.md:278](ProgressiveCompletionChecklist.md).
    - **P09.C278.01** — **осталось**: Изоляция optional audio errors на новых paths. Источник: [ProgressiveCompletionChecklist.md:278](ProgressiveCompletionChecklist.md).
    - **P09.C278.02** — **осталось**: Показывать причину и дать явный выбор восстановления. Источник: [ProgressiveCompletionChecklist.md:278](ProgressiveCompletionChecklist.md).
  - **P09.BASE** — **готово**: Ограниченный multi-audio/optional профиль уже работает. Источник: [ProgressiveMultiTrack.md](ProgressiveMultiTrack.md), [ProgressiveOptionalAudio.md](ProgressiveOptionalAudio.md), [ProgressivePartialAudio.md](ProgressivePartialAudio.md), [ProgressiveActiveAudioErrors.md](ProgressiveActiveAudioErrors.md).
    - **P09.BASE.01** — **готово**: Несколько required audio с выбором языка. Источник: [ProgressiveMultiTrack.md](ProgressiveMultiTrack.md).
    - **P09.BASE.02** — **готово**: Поздняя полная optional audio. Источник: [ProgressiveOptionalAudio.md](ProgressiveOptionalAudio.md).
    - **P09.BASE.03** — **готово**: Выбор частично скачанной optional audio и ожидание данных. Источник: [ProgressivePartialAudio.md](ProgressivePartialAudio.md).
    - **P09.BASE.04** — **готово**: Изоляция ошибок активной optional audio. Источник: [ProgressiveActiveAudioErrors.md](ProgressiveActiveAudioErrors.md).

- **P10** — **частично**: Субтитры и вложения. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P10.C282** — **осталось**: Растущие SRT/WebVTT/ASS/SSA/TTML, адаптеры JSON3/SRV. Источник: [ProgressiveCompletionChecklist.md:282](ProgressiveCompletionChecklist.md).
    - **P10.C282.01** — **осталось**: Растущий SRT за пределами complete-only текущего профиля. Источник: [ProgressiveCompletionChecklist.md:282](ProgressiveCompletionChecklist.md).
    - **P10.C282.02** — **осталось**: Растущий WebVTT. Источник: [ProgressiveCompletionChecklist.md:282](ProgressiveCompletionChecklist.md).
    - **P10.C282.03** — **осталось**: Растущий ASS/SSA. Источник: [ProgressiveCompletionChecklist.md:282](ProgressiveCompletionChecklist.md).
    - **P10.C282.04** — **осталось**: Растущий TTML. Источник: [ProgressiveCompletionChecklist.md:282](ProgressiveCompletionChecklist.md).
    - **P10.C282.05** — **осталось**: Документированные JSON3/SRV адаптеры. Источник: [ProgressiveCompletionChecklist.md:282](ProgressiveCompletionChecklist.md).
  - **P10.C283** — **осталось**: UTF-8/UTF-16/BOM и неполные cues; только завершённые события без дубликатов. Источник: [ProgressiveCompletionChecklist.md:283](ProgressiveCompletionChecklist.md).
    - **P10.C283.01** — **осталось**: UTF-8/UTF-16/BOM на границах обновления bytes. Источник: [ProgressiveCompletionChecklist.md:283](ProgressiveCompletionChecklist.md).
    - **P10.C283.02** — **осталось**: Отбрасывать неполный cue до подтверждения. Источник: [ProgressiveCompletionChecklist.md:283](ProgressiveCompletionChecklist.md).
    - **P10.C283.03** — **осталось**: Повторные revisions не дублируют subtitle events. Источник: [ProgressiveCompletionChecklist.md:283](ProgressiveCompletionChecklist.md).
  - **P10.C284** — **осталось**: Embedded mov_text/WebVTT/ASS/SubRip; PGS/DVD subtitles с нужными headers/palettes в локальных контейнерах. Источник: [ProgressiveCompletionChecklist.md:284](ProgressiveCompletionChecklist.md).
    - **P10.C284.01** — **осталось**: Embedded mov_text и WebVTT. Источник: [ProgressiveCompletionChecklist.md:284](ProgressiveCompletionChecklist.md).
    - **P10.C284.02** — **осталось**: Embedded ASS и SubRip. Источник: [ProgressiveCompletionChecklist.md:284](ProgressiveCompletionChecklist.md).
    - **P10.C284.03** — **осталось**: Локальные PGS с обязательными headers/palettes. Источник: [ProgressiveCompletionChecklist.md:284](ProgressiveCompletionChecklist.md).
    - **P10.C284.04** — **осталось**: Локальные DVD subtitle с обязательными headers/palettes. Источник: [ProgressiveCompletionChecklist.md:284](ProgressiveCompletionChecklist.md).
  - **P10.C285** — **осталось**: Styles/settings/language/default/forced; ограничения конвертации описаны без ложного обещания полного оформления. Источник: [ProgressiveCompletionChecklist.md:285](ProgressiveCompletionChecklist.md).
    - **P10.C285.01** — **осталось**: ASS styles и WebVTT settings. Источник: [ProgressiveCompletionChecklist.md:285](ProgressiveCompletionChecklist.md).
    - **P10.C285.02** — **осталось**: Поддержанная часть TTML styling с явными ограничениями. Источник: [ProgressiveCompletionChecklist.md:285](ProgressiveCompletionChecklist.md).
    - **P10.C285.03** — **осталось**: Language/default/forced и выбор subtitle track без потери при update. Источник: [ProgressiveCompletionChecklist.md:285](ProgressiveCompletionChecklist.md).
  - **P10.C286** — **осталось**: Безопасные bounded fonts/attachments без установки в систему и внешних XML/HTML ресурсов. Источник: [ProgressiveCompletionChecklist.md:286](ProgressiveCompletionChecklist.md).
    - **P10.C286.01** — **осталось**: Bounded font/attachment parser без установки в систему. Источник: [ProgressiveCompletionChecklist.md:286](ProgressiveCompletionChecklist.md).
    - **P10.C286.02** — **осталось**: Запрет внешних XML entities/HTML resources. Источник: [ProgressiveCompletionChecklist.md:286](ProgressiveCompletionChecklist.md).
    - **P10.C286.03** — **осталось**: Негативные size/path/malformed проверки attachments. Источник: [ProgressiveCompletionChecklist.md:286](ProgressiveCompletionChecklist.md).
  - **P10.BASE** — **готово**: Поздние полные UTF-8 SRT и изоляция optional errors. Источник: [ProgressiveOptionalSrt.md](ProgressiveOptionalSrt.md).

- **P11** — **частично**: Главы и метаданные. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P11.C290** — **осталось**: Совместная обработка container/writer chapters, стабильные IDs, late updates без сброса позиции. Источник: [ProgressiveCompletionChecklist.md:290](ProgressiveCompletionChecklist.md).
    - **P11.C290.01** — **осталось**: Согласовать container и writer главы по стабильным IDs. Источник: [ProgressiveCompletionChecklist.md:290](ProgressiveCompletionChecklist.md).
    - **P11.C290.02** — **осталось**: Late chapter updates без сброса позиции и дубликатов. Источник: [ProgressiveCompletionChecklist.md:290](ProgressiveCompletionChecklist.md).
  - **P11.C291** — **осталось**: Неизвестная финальная duration и главы за доступной границей. Источник: [ProgressiveCompletionChecklist.md:291](ProgressiveCompletionChecklist.md).
    - **P11.C291.01** — **осталось**: Работа глав при неизвестной конечной duration. Источник: [ProgressiveCompletionChecklist.md:291](ProgressiveCompletionChecklist.md).
    - **P11.C291.02** — **осталось**: Переход к ещё недоступной главе через общие availability rules. Источник: [ProgressiveCompletionChecklist.md:291](ProgressiveCompletionChecklist.md).
  - **P11.C292** — **осталось**: Covers/preview, названия дорожек, rotation/aspect ratio и HDR/color metadata. Источник: [ProgressiveCompletionChecklist.md:292](ProgressiveCompletionChecklist.md).
    - **P11.C292.01** — **осталось**: Covers/preview не блокируют required A/V. Источник: [ProgressiveCompletionChecklist.md:292](ProgressiveCompletionChecklist.md).
    - **P11.C292.02** — **осталось**: Названия дорожек и сохранение metadata при remux/finalize. Источник: [ProgressiveCompletionChecklist.md:292](ProgressiveCompletionChecklist.md).
    - **P11.C292.03** — **осталось**: Rotation/aspect ratio из контейнера/writer. Источник: [ProgressiveCompletionChecklist.md:292](ProgressiveCompletionChecklist.md).
    - **P11.C292.04** — **осталось**: Сохранить и диагностировать HDR/color metadata; physical output отдельно. Источник: [ProgressiveCompletionChecklist.md:292](ProgressiveCompletionChecklist.md).
  - **P11.BASE** — **готово**: Ограниченные writer metadata/chapters уже работают. Источник: [ProgressiveMetadata.md](ProgressiveMetadata.md), [ProgressiveMetadataUiValidation-2026-10-29.md](ProgressiveMetadataUiValidation-2026-10-29.md).
    - **P11.BASE.01** — **готово**: Поздние metadata с revision/stable chapter IDs. Источник: [ProgressiveMetadata.md](ProgressiveMetadata.md).
    - **P11.BASE.02** — **готово**: Реальные главы/title в UI, очистка и смена сеанса. Источник: [ProgressiveMetadataUiValidation-2026-10-29.md](ProgressiveMetadataUiValidation-2026-10-29.md).

- **P12** — **осталось**: Live/event HLS и dynamic DASH. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P12.C296** — **осталось**: Live/event HLS и dynamic DASH через локальную публикацию writer. Источник: [ProgressiveCompletionChecklist.md:296](ProgressiveCompletionChecklist.md).
    - **P12.C296.01** — **осталось**: Live/event HLS через локальный writer. Источник: [ProgressiveCompletionChecklist.md:296](ProgressiveCompletionChecklist.md).
    - **P12.C296.02** — **осталось**: Dynamic DASH через локальный writer. Источник: [ProgressiveCompletionChecklist.md:296](ProgressiveCompletionChecklist.md).
  - **P12.C297** — **осталось**: Неизвестная duration, движущееся окно, отдельная целевая задержка и reconnect. Источник: [ProgressiveCompletionChecklist.md:297](ProgressiveCompletionChecklist.md).
    - **P12.C297.01** — **осталось**: Неизвестная duration и движущееся live окно. Источник: [ProgressiveCompletionChecklist.md:297](ProgressiveCompletionChecklist.md).
    - **P12.C297.02** — **осталось**: Целевая задержка, reconnect и восстановление reader state. Источник: [ProgressiveCompletionChecklist.md:297](ProgressiveCompletionChecklist.md).
  - **P12.C298** — **осталось**: Отсутствие сегмента/heartbeat не означает EOF; конец подтверждает writer. Источник: [ProgressiveCompletionChecklist.md:298](ProgressiveCompletionChecklist.md).
    - **P12.C298.01** — **осталось**: Пропавший сегмент/heartbeat переводит в waiting, не EOF. Источник: [ProgressiveCompletionChecklist.md:298](ProgressiveCompletionChecklist.md).
    - **P12.C298.02** — **осталось**: Только явный writer end завершает live. Источник: [ProgressiveCompletionChecklist.md:298](ProgressiveCompletionChecklist.md).
  - **P12.C299** — **осталось**: Retirement вышедших ресурсов только после освобождения всеми readers; seek за окном имеет отдельную причину. Источник: [ProgressiveCompletionChecklist.md:299](ProgressiveCompletionChecklist.md).
    - **P12.C299.01** — **осталось**: Retirement вышедших ресурсов после release всех readers. Источник: [ProgressiveCompletionChecklist.md:299](ProgressiveCompletionChecklist.md).
    - **P12.C299.02** — **осталось**: Явный reason для seek вне live окна. Источник: [ProgressiveCompletionChecklist.md:299](ProgressiveCompletionChecklist.md).

- **P13** — **частично**: Жизненный цикл, I/O и безопасность. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P13.C170** — **готово**: Детерминированные cache I/O-границы: четыре private compile-only barrier, interrupt/close/source cancel/seek, порча/усечение, публикация, EOF/probe и filesClosed/forget/budget. По 25 native cases/505 checks в G. Источник: [ProgressiveCompletionChecklist.md:170](ProgressiveCompletionChecklist.md).
  - **P13.C171** — **готово**: Classic SyncRead/ReadV2 и ограниченный graph reentry: исправлены пропущенная отмена последнего блока и потерянный полный flush cycle; 31 native case после 19/31 pre-fix FAIL. Настоящий DirectShow graph прошёл 1. Источник: [ProgressiveCompletionChecklist.md:171](ProgressiveCompletionChecklist.md).
  - **P13.C172** — **готово**: Classic native I/O: ограниченный pending-read срез: исправлена потеря полного flush cycle при ожидающем ReadFile; по 38 native cases до/после, два red FAIL требуют rescue, fixed PASS без rescue. Настоящий Windo. Источник: [ProgressiveCompletionChecklist.md:172](ProgressiveCompletionChecklist.md).
  - **P13.C173** — **готово**: Classic missing-range/sample/complete waits: поколение flush передаётся внутрь WaitRange; WaitForSample записывает caller output после final guard; completionWaiters снимается при выходе. 38 native cases: 17 pr. Источник: [ProgressiveCompletionChecklist.md:173](ProgressiveCompletionChecklist.md).
  - **P13.C174** — **готово**: Classic v2/AAC payload boundaries: SyncRead сохраняет E_FAIL после ReadV2Guarded::Fail, не превращая собственную остановку сессии в E_ABORT. 240 cases двух профилей (48 kHz mono/BF2 и 44.1 kHz stereo/BF3): pair. Источник: [ProgressiveCompletionChecklist.md:174](ProgressiveCompletionChecklist.md).
  - **P13.C175** — **готово**: Classic CMainFrame/MAINSTA + public IPC reentry, ограниченный срез: 16 сочетаний frame/graph before/after × cancel/new target/same target/nested poll. Настоящие CMainFrame, splitter SetPositions и named-pipe re. Источник: [ProgressiveCompletionChecklist.md:175](ProgressiveCompletionChecklist.md).
  - **P13.C176** — **готово**: Classic Stop/replace/window close внутри seek, ограниченный срез: 12 сочетаний четырёх frame/graph boundaries и трёх реальных действий; повтор на двух private EXE, все native main exits 0. Новый regular source. Источник: [ProgressiveCompletionChecklist.md:176](ProgressiveCompletionChecklist.md).
  - **P13.C177** — **готово**: Classic AAC teardown внутри seek, ограниченный срез: 24 cases на 48000 Hz mono/BF2 и 44100 Hz stereo/BF3 — Stop, A/V replacement и WM_CLOSE в четырёх frame/graph boundaries; ещё 12 video regression и smoke. До. Источник: [ProgressiveCompletionChecklist.md:177](ProgressiveCompletionChecklist.md).
  - **P13.C179** — **готово**: Авария worker внутри Classic seek и общий post-handoff guard: после принятия graph seek backend error больше не превращает pending intent в applied; квота принятой операции сохранена, новый intent не стирается. Источник: [ProgressiveCompletionChecklist.md:179](ProgressiveCompletionChecklist.md).
  - **P13.C183** — **готово**: Linux Qt: причина роста private memory и исправление: glibc удерживала освобождённые крупные блоки в native arenas; Linux-only статический mmap threshold и reuse индексных buffers. Приёмка 12 ноября: два final. Источник: [ProgressiveCompletionChecklist.md:183](ProgressiveCompletionChecklist.md).
  - **P13.C303** — **осталось**: Оставшиеся UI-blocking пути probe/index/remux/read/teardown, Qt GL cleanup и обычные Classic graphs. Источник: [ProgressiveCompletionChecklist.md:303](ProgressiveCompletionChecklist.md).
    - **P13.C303.01** — **осталось**: Оставшиеся blocking probe/index/remux/read paths вне UI. Источник: [ProgressiveCompletionChecklist.md:303](ProgressiveCompletionChecklist.md).
    - **P13.C303.02** — **осталось**: Qt OpenGL cleanup без зависания UI. Источник: [ProgressiveCompletionChecklist.md:303](ProgressiveCompletionChecklist.md).
    - **P13.C303.03** — **осталось**: Обычные UI-owned Classic graph/COM teardown. Источник: [ProgressiveCompletionChecklist.md:303](ProgressiveCompletionChecklist.md).
  - **P13.C304** — **осталось**: Управляемая отмена ожиданий, bounded остановка внешнего процесса, фактические release/filesClosed receipts. Источник: [ProgressiveCompletionChecklist.md:304](ProgressiveCompletionChecklist.md).
    - **P13.C304.01** — **осталось**: Отмена управляемых ожиданий при Stop/Close/Cancel. Источник: [ProgressiveCompletionChecklist.md:304](ProgressiveCompletionChecklist.md).
    - **P13.C304.02** — **осталось**: Bounded остановка зависшего внешнего worker. Источник: [ProgressiveCompletionChecklist.md:304](ProgressiveCompletionChecklist.md).
    - **P13.C304.03** — **осталось**: FilesClosed/release receipts отражают реальное закрытие, включая registry contention. Источник: [ProgressiveCompletionChecklist.md:304](ProgressiveCompletionChecklist.md).
    - **P13.C304.04** — **осталось**: Проверить deadline ≤2s на всём поддержанном корпусе; device-stalled I/O отделён явно. Источник: [ProgressiveCompletionChecklist.md:304](ProgressiveCompletionChecklist.md).
  - **P13.C305** — **готово**: Ограниченный player-crash срез: четыре RF64-профиля каждой редакции, два самостоятельных процесса, принудительное завершение при playing/buffering, подтверждение выхода ОС для старых readers, сохранение второго. Источник: [ProgressiveCompletionChecklist.md:305](ProgressiveCompletionChecklist.md).
  - **P13.C306** — **готово**: Ограниченный writer-failure срез: RF64 PCM16/24, crash после неполного кадра и инъекция ENOSPC, восемь сценариев каждой редакции. ≥16 s без heartbeat не дают EOF/release живого reader; новый writer проверяет ба. Источник: [ProgressiveCompletionChecklist.md:306](ProgressiveCompletionChecklist.md).
  - **P13.C307** — **готово**: Ограниченный final-file срез: rename RF64 при активных readers и complete после rename, четыре SHA-защищённых профиля каждой редакции, неизменное логическое имя, отказ подменённому пути, по восемь natural EOF и. Источник: [ProgressiveCompletionChecklist.md:307](ProgressiveCompletionChecklist.md).
  - **P13.C308** — **осталось**: Writer/player crash, resume по идентичности bytes, несколько readers, rename и переход на итоговый файл. Источник: [ProgressiveCompletionChecklist.md:308](ProgressiveCompletionChecklist.md).
    - **P13.C308.01** — **осталось**: Resume после writer/player/worker crash по identity bytes. Источник: [ProgressiveCompletionChecklist.md:308](ProgressiveCompletionChecklist.md).
    - **P13.C308.02** — **осталось**: Изоляция нескольких readers при crash/Stop/reopen. Источник: [ProgressiveCompletionChecklist.md:308](ProgressiveCompletionChecklist.md).
    - **P13.C308.03** — **осталось**: Rename/finalize для всех допущенных контейнеров. Источник: [ProgressiveCompletionChecklist.md:308](ProgressiveCompletionChecklist.md).
    - **P13.C308.04** — **осталось**: Переход к итоговому файлу с сохранением позиции/паузы. Источник: [ProgressiveCompletionChecklist.md:308](ProgressiveCompletionChecklist.md).
  - **P13.C309** — **осталось**: Ошибка preview не отменяет download; merge/writer errors не становятся успешным EOF. Источник: [ProgressiveCompletionChecklist.md:309](ProgressiveCompletionChecklist.md).
    - **P13.C309.01** — **осталось**: Preview error не отменяет загрузку и не меняет архив. Источник: [ProgressiveCompletionChecklist.md:309](ProgressiveCompletionChecklist.md).
    - **P13.C309.02** — **осталось**: Writer/merge failure не становится successful EOF. Источник: [ProgressiveCompletionChecklist.md:309](ProgressiveCompletionChecklist.md).
  - **P13.C310** — **готово**: Schema 1: длинные локальные пути трёх редакций: 40 финальных случаев (14 Classic /14 Windows Qt /12 Linux Qt), точные exit 0. В Windows общий Unicode/extended-path helper; Classic снял 240-char/MAX_PATH и отдел. Источник: [ProgressiveCompletionChecklist.md:310](ProgressiveCompletionChecklist.md).
  - **P13.C311** — **готово**: Schema 2 и continuous-remux: длинные source roots: 18 новых playback cases трёх редакций с каталогами 220/320/1100 chars, append/complete, decoder I/O, положительными filesClosed и worker OS exit; точные Python. Источник: [ProgressiveCompletionChecklist.md:311](ProgressiveCompletionChecklist.md).
  - **P13.C312** — **готово**: Ограниченный private registry/cache и legacy-манифест срез: 3 native длины каждой ОС (до registry/cache 1120/1117 Linux и 1148/1145 Windows), реальные reads/worker/complete/filesClosed/очистка/forget; reader co. Источник: [ProgressiveCompletionChecklist.md:312](ProgressiveCompletionChecklist.md).
  - **P13.C313** — **готово**: Ограниченный writable Qt profile и runtime-path срез: full main/CLI обычный/320/1100 в Windows и Linux; atomic INI sync и concurrent merge без orphan lock. Частная исправленная QtCore Windows сохраняет atomic s. Источник: [ProgressiveCompletionChecklist.md:313](ProgressiveCompletionChecklist.md).
  - **P13.C314** — **готово**: Ограниченный Classic TEMP120/240 срез: 4 indexed/fragmented growing playback случая, реальные clock/append/complete, opt-in reader receipts и explicit filesClosed/forget, два normal main exit 0. Native Windows. Источник: [ProgressiveCompletionChecklist.md:314](ProgressiveCompletionChecklist.md).
  - **P13.C315** — **готово**: Устранён блокирующий GetTempPathW при выборе Classic reader registry: matched longPathAware native control локализовал timeout, новый bounded environment resolver сохраняет TMP/TEMP/USERPROFILE/Windows preceden. Источник: [ProgressiveCompletionChecklist.md:315](ProgressiveCompletionChecklist.md).
  - **P13.C316** — **готово**: Ограниченный Classic full-main long-TEMP playback: snapshot локализовал ожидание GetTempPathW внутри D3D9/driver→DbgHelp; process-local adapter сохраняет short native path/buffer semantics и корректно возвращае. Источник: [ProgressiveCompletionChecklist.md:316](ProgressiveCompletionChecklist.md).
  - **P13.C317** — **готово**: Ограниченный Windows Qt exact-TEMP runtime/Player срез: частная QtCore сохраняет длинный user TMP/TEMP/USERPROFILE, короткий TMP приоритетен, пустые переменные пропускаются; SYSTEM делегирован штатному API и ре. Источник: [ProgressiveCompletionChecklist.md:317](ProgressiveCompletionChecklist.md).
  - **P13.C318** — **готово**: Classic: пустой TMP→длинный TEMP: capacity1 пустой environment variable больше не считается нестабильным непустым значением. Старый native helper дал retained exit1; новый resolver19, real Detours6 и full main1. Источник: [ProgressiveCompletionChecklist.md:318](ProgressiveCompletionChecklist.md).
  - **P13.C319** — **внешнее условие**: Внешний Python launcher с длинным собственным TEMP остаётся OPEN: пять owned controls/diagnostics и три stack snapshots локализуют ожидание GetTempPathW внутри Windows CreateProcess до возврата Popen. Источник: [ProgressiveCompletionChecklist.md:319](ProgressiveCompletionChecklist.md).
    - **P13.C319.01** — **готово**: Сохранить normal-parent-TEMP маршрут и child-only long TEMP в инструкции. Источник: [ProgressiveCompletionChecklist.md:319](ProgressiveCompletionChecklist.md).
    - **P13.C319.02** — **внешнее условие**: Проверить внешний Python CreateProcess с собственным long parent TEMP без изменения Harvester/Python/Windows. Источник: [ProgressiveCompletionChecklist.md:319](ProgressiveCompletionChecklist.md).
      Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.
  - **P13.C320** — **готово**: Qt literal `--harvester-session`, полный main/CLI: 10 случаев (по пять Linux/Windows): source roots 0/320/1100 со Stop, отдельно short-root natural EOF и writer failed; реальные IPC buffering/playing/paused/err. Источник: [ProgressiveCompletionChecklist.md:320](ProgressiveCompletionChecklist.md).
  - **P13.C321** — **осталось**: Предельные длины компонентов/пути, Unicode private CLI argv и полный long-path rename matrix. Natural EOF/error на длинных roots — отдельная расширенная матрица, не результат short-root CLI-среза. Источник: [ProgressiveCompletionChecklist.md:321](ProgressiveCompletionChecklist.md).
    - **P13.C321.01** — **осталось**: Предельная длина компонентов и полного пути. Источник: [ProgressiveCompletionChecklist.md:321](ProgressiveCompletionChecklist.md).
    - **P13.C321.02** — **осталось**: Unicode private CLI argv на Windows и Linux. Источник: [ProgressiveCompletionChecklist.md:321](ProgressiveCompletionChecklist.md).
    - **P13.C321.03** — **осталось**: Полная long-path rename/substitution матрица. Источник: [ProgressiveCompletionChecklist.md:321](ProgressiveCompletionChecklist.md).
    - **P13.C321.04** — **осталось**: Natural EOF/error на длинных source roots за пределами короткого CLI контроля. Источник: [ProgressiveCompletionChecklist.md:321](ProgressiveCompletionChecklist.md).
  - **P13.C322** — **осталось**: Owner/permissions, reparse/symlink/path traversal/substitution, IPC current-user, секреты не попадают в логи. Источник: [ProgressiveCompletionChecklist.md:322](ProgressiveCompletionChecklist.md).
    - **P13.C322.01** — **осталось**: Owner/permissions/current-user IPC для всех новых путей. Источник: [ProgressiveCompletionChecklist.md:322](ProgressiveCompletionChecklist.md).
    - **P13.C322.02** — **осталось**: Reparse/symlink/traversal/substitution для всех новых путей. Источник: [ProgressiveCompletionChecklist.md:322](ProgressiveCompletionChecklist.md).
    - **P13.C322.03** — **осталось**: Проверить отсутствие cookies/tokens/signed URLs в обычных и diagnostic logs. Источник: [ProgressiveCompletionChecklist.md:322](ProgressiveCompletionChecklist.md).
  - **P13.C323** — **отложено**: Network/removable filesystem capabilities только по отдельным доказательствам; обязательная база — local fixed drive. Источник: [ProgressiveCompletionChecklist.md:323](ProgressiveCompletionChecklist.md).
    - **P13.C323.01** — **отложено**: Network filesystem: отдельные identity/read/write/disconnect доказательства. Источник: [ProgressiveCompletionChecklist.md:323](ProgressiveCompletionChecklist.md).
      Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
    - **P13.C323.02** — **отложено**: Removable filesystem: отдельные identity/read/write/disconnect доказательства. Источник: [ProgressiveCompletionChecklist.md:323](ProgressiveCompletionChecklist.md).
      Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.

- **P14** — **частично**: API, интерфейс и диагностика. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P14.C111** — **готово**: Шесть внутренних команд подготовки continuous-remux через обычный IPC всех трёх редакций, без playback commit и без изменения advertised capabilities/SDK. По шесть IPC сценариев на редакцию, прежние 24 preparat. Источник: [ProgressiveCompletionChecklist.md:111](ProgressiveCompletionChecklist.md).
  - **P14.C112** — **готово**: Внутренний continuous-remux playback commit/control/status через обычный IPC, intent fencing при Stop/замене/закрытии: по 14 сценариев Qt ОС и 15 Classic, две Qt Window/API lifecycle проверки на ОС, 11 native d. Источник: [ProgressiveCompletionChecklist.md:112](ProgressiveCompletionChecklist.md).
  - **P14.C114** — **готово**: SDK 1.82: публичный ограниченный `continuousRemux` contract, явный `ContinuousRemux` helper, read-only восстановление job/playback после потери ответа и поставка worker/runtime. По пять SDK IPC сценариев каждой. Источник: [ProgressiveCompletionChecklist.md:114](ProgressiveCompletionChecklist.md).
  - **P14.C116** — **готово**: Qt continuous buffering status/UI: реальный `paused-for-cache` учитывается в API; ручная пауза приоритетна в API/строке состояния/шаблоне заголовка; дополнительный optional `reasonCode` различает ожидание/загру. Источник: [ProgressiveCompletionChecklist.md:116](ProgressiveCompletionChecklist.md).
  - **P14.C117** — **готово**: Ограниченная heartbeat-семантика публичного continuous SDK: по пять сценариев трёх редакций, настоящий срок 30000 ms, status не продлевает writer, timeout не EOF, late events отклоняются, complete не требует he. Источник: [ProgressiveCompletionChecklist.md:117](ProgressiveCompletionChecklist.md).
  - **P14.C138** — **готово**: SDK 1.83: публичный opt-in H.264 B-frame профиль, общие capability profiles, keyword-only `mode=REORDERED_MODE` и строгая проверка объявленного профиля/ответов. 23 unit checks; по шесть новых, пять legacy, пять. Источник: [ProgressiveCompletionChecklist.md:138](ProgressiveCompletionChecklist.md).
  - **P14.C153** — **готово**: Continuous AAC API и SDK-кандидат 1.84: явный вложенный profile, отложенный до PCM-плана commit, 27 IPC сценариев трёх редакций, подключение обязательного audio в Classic и независимый короткий AAC EOF. Qt пров. Источник: [ProgressiveCompletionChecklist.md:153](ProgressiveCompletionChecklist.md).
  - **P14.C164** — **готово**: SDK-кандидат 1.85: публичная начальная позиция: `continuousRemuxPlay.startSeconds`, helper `play(start_seconds=...)`, явная capability, status indexedEnd/duration/start. Неподтверждённый target даёт waiting-ran. Источник: [ProgressiveCompletionChecklist.md:164](ProgressiveCompletionChecklist.md).
  - **P14.C327** — **осталось**: Общие точные reasonCodes и states для всех новых режимов; ok принятия команды не означает playing. Источник: [ProgressiveCompletionChecklist.md:327](ProgressiveCompletionChecklist.md).
    - **P14.C327.01** — **осталось**: Общие точные states/reasonCodes во всех новых режимах. Источник: [ProgressiveCompletionChecklist.md:327](ProgressiveCompletionChecklist.md).
    - **P14.C327.02** — **осталось**: Разделять command accepted, preparation, waiting, playing и terminal outcome. Источник: [ProgressiveCompletionChecklist.md:327](ProgressiveCompletionChecklist.md).
  - **P14.C328** — **осталось**: Название/источник/выбранные дорожки/доступная граница/буфер/причина ожидания. Источник: [ProgressiveCompletionChecklist.md:328](ProgressiveCompletionChecklist.md).
    - **P14.C328.01** — **осталось**: Название/источник/выбранные дорожки во всех новых modes. Источник: [ProgressiveCompletionChecklist.md:328](ProgressiveCompletionChecklist.md).
    - **P14.C328.02** — **осталось**: Показывать доступную границу/буфер/конкретную причину waiting. Источник: [ProgressiveCompletionChecklist.md:328](ProgressiveCompletionChecklist.md).
  - **P14.C329** — **осталось**: Pause/Stop/Seek, главы и track menus остаются отзывчивыми; activate=false не перехватывает фокус. Источник: [ProgressiveCompletionChecklist.md:329](ProgressiveCompletionChecklist.md).
    - **P14.C329.01** — **осталось**: Отзывчивые Pause/Stop/Seek/track menus/главы во всей матрице. Источник: [ProgressiveCompletionChecklist.md:329](ProgressiveCompletionChecklist.md).
    - **P14.C329.02** — **осталось**: Проверить activate=false и отсутствие захвата фокуса. Источник: [ProgressiveCompletionChecklist.md:329](ProgressiveCompletionChecklist.md).
  - **P14.C330** — **осталось**: Capabilities отражают фактические EXE/decoder builds; v1/client/legacy adapters не регрессировали. Источник: [ProgressiveCompletionChecklist.md:330](ProgressiveCompletionChecklist.md).
    - **P14.C330.01** — **осталось**: Сверить фактические shipped EXE/worker/decoder capabilities. Источник: [ProgressiveCompletionChecklist.md:330](ProgressiveCompletionChecklist.md).
    - **P14.C330.02** — **осталось**: Проверить v1/client/legacy migration corpus на итоговых сборках. Источник: [ProgressiveCompletionChecklist.md:330](ProgressiveCompletionChecklist.md).
  - **P14.RANGE** — **осталось**: Запрос нужного диапазона/приоритета writer и порционная передача больших metadata. Источник: [ProgressiveTechnicalSpec-2026-10-25.ru.md:121](ProgressiveTechnicalSpec-2026-10-25.ru.md).
    - **P14.RANGE.01** — **осталось**: Согласовать capability запроса диапазона/приоритета и точный player/client ответ. Источник: [ProgressiveTechnicalSpec-2026-10-25.ru.md:121](ProgressiveTechnicalSpec-2026-10-25.ru.md).
    - **P14.RANGE.02** — **осталось**: Передать запрос через SDK-клиент; writer без этой capability оставляет просмотр в честном waiting. Источник: [ProgressiveTechnicalSpec-2026-10-25.ru.md:121](ProgressiveTechnicalSpec-2026-10-25.ru.md).
    - **P14.RANGE.03** — **осталось**: Передавать большие главы/субтитры ограниченными порциями или локальными ресурсами без увеличения JSON лимита. Источник: [ProgressiveTechnicalSpec-2026-10-25.ru.md:121](ProgressiveTechnicalSpec-2026-10-25.ru.md).

- **P15** — **частично**: Приёмка, ресурсы и задержки. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P15.C146** — **осталось**: Полные waiting/replay/shutdown/crash/stress проверки публичного continuous-remux SDK трёх редакций. Далее — Linux release latency, зависший OS I/O/cancellation и расширенная race/stress matrix. Источник: [ProgressiveCompletionChecklist.md:146](ProgressiveCompletionChecklist.md).
    - **P15.C146.01** — **осталось**: Свести waiting/replay/replacement/re-arm/failure сценарии всего поддержанного continuous корпуса. Источник: [ProgressiveCompletionChecklist.md:146](ProgressiveCompletionChecklist.md).
    - **P15.C146.02** — **осталось**: Проверить worker/player crash и shutdown всего поддержанного корпуса. Источник: [ProgressiveCompletionChecklist.md:146](ProgressiveCompletionChecklist.md).
    - **P15.C146.03** — **осталось**: Проверить длительный и циклический stress всего корпуса без замены немедленных budget поздним idle. Источник: [ProgressiveCompletionChecklist.md:146](ProgressiveCompletionChecklist.md).
    - **P15.C146.04** — **осталось**: Повторить release latency и реальные OS-I/O cancellation границы отдельно от успешных file/null тестов. Источник: [ProgressiveCompletionChecklist.md:146](ProgressiveCompletionChecklist.md).
  - **P15.C156** — **готово**: Ограниченный AAC resilience-срез 1.84: 99 сценариев трёх редакций, реальные 10/30/60 s, Stop/Cancel/replay, concurrent replacement, worker death, normal/player crash и cache recovery/явный retry; stereo 48 kHz. Источник: [ProgressiveCompletionChecklist.md:156](ProgressiveCompletionChecklist.md).
  - **P15.C157** — **готово**: Ограниченный AAC cyclic/growing stress 1.84: по 100 измеряемых +4 warmup cycles и один 128 s append-only / 60 s writer-pause сценарий каждой редакции; 300+12 циклов, три growing cases, прежние бюджеты CPU/RSS/p. Источник: [ProgressiveCompletionChecklist.md:157](ProgressiveCompletionChecklist.md).
  - **P15.C168** — **готово**: Конкурентные команды очереди и 100 циклов: 36 парных сценариев и 100 измеряемых +4 warmup цикла каждой редакции; 408 основных cases/12 warmup, 5100 конкурентных пар с warmup, конечные drivers exit 0. Последняя. Источник: [ProgressiveCompletionChecklist.md:168](ProgressiveCompletionChecklist.md).
  - **P15.C180** — **готово**: Дополнительные AAC-профили queue/race/stress: 48 kHz mono/BF2/25 fps и 44.1 kHz stereo/BF3/30000/1001, по 36 races и 100 измеряемых циклов каждого профиля на каждой редакции. Всего 216 races +600 циклов +24 war. Источник: [ProgressiveCompletionChecklist.md:180](ProgressiveCompletionChecklist.md).
  - **P15.C181** — **готово**: AAC shutdown/recovery с queued seek: normal/crash при ожидающей цели 19 s и после её применения, physical writer append, новый job с начальной позицией и отказ поздним старым Stop/cancelSeek/seek. 12 новых +24. Источник: [ProgressiveCompletionChecklist.md:181](ProgressiveCompletionChecklist.md).
  - **P15.C182** — **готово**: Длительный continuous A/V-прогон, ограниченный срез: три полных 600 s jobs в тех же двух main processes каждой редакции, ≥31 min elapsed и 18 natural-EOF viewer jobs. 10/30/60 s writer pauses, manual pause, enc. Источник: [ProgressiveCompletionChecklist.md:182](ProgressiveCompletionChecklist.md).
  - **P15.C334** — **осталось**: Общая positive/negative matrix трёх редакций, минимум 10 секунд просмотра до complete. Источник: [ProgressiveCompletionChecklist.md:334](ProgressiveCompletionChecklist.md).
    - **P15.C334.01** — **осталось**: Зафиксировать каждую обязательную positive/negative строку и её expected result. Источник: [ProgressiveCompletionChecklist.md:334](ProgressiveCompletionChecklist.md).
    - **P15.C334.02** — **осталось**: Positive playback ≥10s до complete во всех трёх редакциях. Источник: [ProgressiveCompletionChecklist.md:334](ProgressiveCompletionChecklist.md).
    - **P15.C334.03** — **осталось**: Проверить неполученные bytes/ложный EOF/исходники/metadata на всём корпусе. Источник: [ProgressiveCompletionChecklist.md:334](ProgressiveCompletionChecklist.md).
    - **P15.C334.04** — **осталось**: У каждой negative/SKIP строки точная причина и подтверждённая ОС; запрет collector-only PASS. Источник: [ProgressiveCompletionChecklist.md:334](ProgressiveCompletionChecklist.md).
  - **P15.C335** — **осталось**: Неравномерная запись, writer pauses 10/30/60 s, partial structures, holes, ошибки, disk full, cancel/crash. Источник: [ProgressiveCompletionChecklist.md:335](ProgressiveCompletionChecklist.md).
    - **P15.C335.01** — **осталось**: Неравномерные скорости обязательных потоков. Источник: [ProgressiveCompletionChecklist.md:335](ProgressiveCompletionChecklist.md).
    - **P15.C335.02** — **осталось**: Writer pauses 10/30/60s с корректным heartbeat. Источник: [ProgressiveCompletionChecklist.md:335](ProgressiveCompletionChecklist.md).
    - **P15.C335.03** — **осталось**: Partial headers/packets, holes и out-of-order запись. Источник: [ProgressiveCompletionChecklist.md:335](ProgressiveCompletionChecklist.md).
    - **P15.C335.04** — **осталось**: Disk full, cancel и crash на границах ввода/вывода. Источник: [ProgressiveCompletionChecklist.md:335](ProgressiveCompletionChecklist.md).
  - **P15.C336** — **осталось**: Реальные разрешённые YT/VK/Rutube direct/HLS/DASH сценарии с protocol/format IDs и offline fixtures без секретных URL. Источник: [ProgressiveCompletionChecklist.md:336](ProgressiveCompletionChecklist.md).
    - **P15.C336.01** — **осталось**: YouTube direct/HLS/DASH там, где фактически доступно. Источник: [ProgressiveCompletionChecklist.md:336](ProgressiveCompletionChecklist.md).
    - **P15.C336.02** — **осталось**: VK direct/HLS/DASH там, где фактически доступно. Источник: [ProgressiveCompletionChecklist.md:336](ProgressiveCompletionChecklist.md).
    - **P15.C336.03** — **осталось**: Rutube direct/HLS/DASH там, где фактически доступно. Источник: [ProgressiveCompletionChecklist.md:336](ProgressiveCompletionChecklist.md).
    - **P15.C336.04** — **осталось**: Сохранить protocol/format IDs и воспроизводимые offline fixtures без секретных URL. Источник: [ProgressiveCompletionChecklist.md:336](ProgressiveCompletionChecklist.md).
  - **P15.C337** — **готово**: Ограниченный срез: 100 native core open/cancel/release cycles на каждой ОС с предельными RF64, реальными sparse resources и RSS/private/handle measurements. Это не 100 полных GUI/decoder cycles; следующий общий. Источник: [ProgressiveCompletionChecklist.md:337](ProgressiveCompletionChecklist.md).
  - **P15.C338** — **готово**: Ограниченный GUI/decoder срез: 100 start/toolbar-Stop/release циклов каждой редакции, 300 измеряемых и 12 прогревочных, четыре RF64 режима; pause/seek, реальный decoder I/O, filesClosed, SHA/удаление и budgets. Источник: [ProgressiveCompletionChecklist.md:338](ProgressiveCompletionChecklist.md).
  - **P15.C339** — **готово**: Ограниченный concurrent RF64 срез: два playback endpoints и дополнительное чтение общего файла, максимум пять квитанций, четыре профиля каждой редакции, изолированные cancel/Stop/reopen и общий release перед уд. Источник: [ProgressiveCompletionChecklist.md:339](ProgressiveCompletionChecklist.md).
  - **P15.C340** — **готово**: Ограниченный длительный срез: 600 s растущего RF64 каждой редакции, два самостоятельных процесса PCM16 ordinary / PCM24 sparse hint, последовательный префикс. Паузы writer 10/30/60 s с heartbeat, buffering/resu. Источник: [ProgressiveCompletionChecklist.md:340](ProgressiveCompletionChecklist.md).
  - **P15.C341** — **готово**: Ограниченный длительный видеосрез: 600 s H.264/AAC fMP4 и MPEG-TS каждой редакции, два процесса, inline SHA, прежние паузы/бюджеты и положительный filesClosed. Найден и исправлен общий отказ при 129 inline SHA-. Источник: [ProgressiveCompletionChecklist.md:341](ProgressiveCompletionChecklist.md).
  - **P15.C343** — **готово**: Текущая Classic: строгие 100 циклов +12 paused expiry, native exit 0. Проверена отдельная сборка с DWM/history fixes и bundled SDK 1.84, те же два PID, прежние немедленные resource/release budgets; baseline и в. Источник: [ProgressiveCompletionChecklist.md:343](ProgressiveCompletionChecklist.md).
  - **P15.C344** — **осталось**: Длительные загрузки, минимум 100 start/cancel циклов, несколько readers, RAM/cache/temp/CPU/handles/threads без нарастающих утечек. Источник: [ProgressiveCompletionChecklist.md:344](ProgressiveCompletionChecklist.md).
    - **P15.C344.01** — **осталось**: Длительные runs на расширенном codec/container корпусе. Источник: [ProgressiveCompletionChecklist.md:344](ProgressiveCompletionChecklist.md).
    - **P15.C344.02** — **осталось**: Минимум100 start/cancel циклов остальных modes. Источник: [ProgressiveCompletionChecklist.md:344](ProgressiveCompletionChecklist.md).
    - **P15.C344.03** — **осталось**: Несколько readers и смешанные редакции. Источник: [ProgressiveCompletionChecklist.md:344](ProgressiveCompletionChecklist.md).
    - **P15.C344.04** — **осталось**: Проверить RAM/cache/temp/CPU/handles/threads budgets без нарастающих утечек. Источник: [ProgressiveCompletionChecklist.md:344](ProgressiveCompletionChecklist.md).
  - **P15.C345** — **осталось**: Обычные завершённые файлы, пользовательские профили и shell registration не регрессировали. Источник: [ProgressiveCompletionChecklist.md:345](ProgressiveCompletionChecklist.md).
    - **P15.C345.01** — **осталось**: Регрессия обычного completed-file corpus. Источник: [ProgressiveCompletionChecklist.md:345](ProgressiveCompletionChecklist.md).
    - **P15.C345.02** — **осталось**: Сохранность пользовательских профилей и соседних установок. Источник: [ProgressiveCompletionChecklist.md:345](ProgressiveCompletionChecklist.md).
    - **P15.C345.03** — **осталось**: Shell registration/HKCU до/после и независимость IPC/EXE/INI. Источник: [ProgressiveCompletionChecklist.md:345](ProgressiveCompletionChecklist.md).
  - **P15.C368** — **осталось**: Старт после готового буфера ≤3 s, resume ≤2 s на согласованном эталонном ПК; remux preparation измеряется отдельно. Источник: [ProgressiveCompletionChecklist.md:368](ProgressiveCompletionChecklist.md).
    - **P15.C368.01** — **осталось**: Согласовать эталонный ПК и измерительную методику start/resume. Источник: [ProgressiveCompletionChecklist.md:368](ProgressiveCompletionChecklist.md).
    - **P15.C368.02** — **осталось**: Измерить ready-buffer start ≤3s и resume ≤2s во всей матрице. Источник: [ProgressiveCompletionChecklist.md:368](ProgressiveCompletionChecklist.md).
    - **P15.C368.03** — **осталось**: Измерить remux preparation отдельно от ready-start. Источник: [ProgressiveCompletionChecklist.md:368](ProgressiveCompletionChecklist.md).
  - **P15.C369** — **отложено**: Абсолютная A/V ошибка ≤80 ms; накопленный drift за 30 min ≤40 ms, с описанной методикой физического выхода. Источник: [ProgressiveCompletionChecklist.md:369](ProgressiveCompletionChecklist.md).
    - **P15.C369.01** — **отложено**: Согласовать физический flash/beep стенд и latency устройства. Источник: [ProgressiveCompletionChecklist.md:369](ProgressiveCompletionChecklist.md).
      Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
    - **P15.C369.02** — **отложено**: Измерить абсолютную A/V ошибку ≤80ms. Источник: [ProgressiveCompletionChecklist.md:369](ProgressiveCompletionChecklist.md).
      Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
    - **P15.C369.03** — **отложено**: Измерить накопленный drift за30min ≤40ms. Источник: [ProgressiveCompletionChecklist.md:369](ProgressiveCompletionChecklist.md).
      Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.

- **P16** — **внешнее условие**: Внешняя интеграция Harvester. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P16.C349** — **внешнее условие**: Отдельное разрешение/задача на подключение реальных download events и кнопки preview в Harvester. Источник: [ProgressiveCompletionChecklist.md:349](ProgressiveCompletionChecklist.md).
    - **P16.C349.01** — **внешнее условие**: Получить отдельную задачу/разрешение на реальные Harvester events/button. Источник: [ProgressiveCompletionChecklist.md:349](ProgressiveCompletionChecklist.md).
      Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.
  - **P16.C350** — **внешнее условие**: Сквозные start/update/complete/error/cancel/release из реального Harvester во всех трёх редакциях. Источник: [ProgressiveCompletionChecklist.md:350](ProgressiveCompletionChecklist.md).
    - **P16.C350.01** — **внешнее условие**: Реальный Harvester → Linux Qt: start/update/complete/error/cancel/release. Источник: [ProgressiveCompletionChecklist.md:350](ProgressiveCompletionChecklist.md).
      Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.
    - **P16.C350.02** — **внешнее условие**: Реальный Harvester → Windows Qt: тот же conformance. Источник: [ProgressiveCompletionChecklist.md:350](ProgressiveCompletionChecklist.md).
      Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.
    - **P16.C350.03** — **внешнее условие**: Реальный Harvester → Classic: тот же conformance. Источник: [ProgressiveCompletionChecklist.md:350](ProgressiveCompletionChecklist.md).
      Внешний проект/условие вне разрешённой работы FoxicMP; не менять Harvester, Python или Windows.

- **P17** — **частично**: Итоговые пакеты и GitHub release. Источник: [ProgressiveCompletionChecklist.md](ProgressiveCompletionChecklist.md).
  - **P17.C139** — **готово**: SDK 1.83: clean SDK ZIP (318 SHA), portable трёх редакций и Linux `.deb`; 39 сценариев распакованных пакетов и 11 установленного Linux. Опубликованный prerelease: SHA-256/size всех семи assets сверены с GitHub. Источник: [ProgressiveCompletionChecklist.md:139](ProgressiveCompletionChecklist.md).
  - **P17.C154** — **готово**: Ordinary main/пакетная приёмка AAC 1.84: 107 финальных сценариев с десятью SDK-модулями из пакета, ещё 90 повторов исправленной Classic и 4 controlled-index-lag сценария; Linux обновлён с backup/сохранением INI. Источник: [ProgressiveCompletionChecklist.md:154](ProgressiveCompletionChecklist.md).
  - **P17.C155** — **готово**: Экспериментальная поставка SDK/runtime 1.84: source-clean SDK ZIP, отдельные portable трёх редакций и .deb, tag/commit d920adcfe, серверные SHA-256 и полная обратная загрузка семи assets, Linux backup/неизменны. Источник: [ProgressiveCompletionChecklist.md:155](ProgressiveCompletionChecklist.md).
  - **P17.C358** — **готово**: Ограниченный кандидат AAC 1.84: ordinary main, чистый .deb и portable, атомарное обновление Linux и сохранность профиля. Источник: [ProgressiveCompletionChecklist.md:358](ProgressiveCompletionChecklist.md).
  - **P17.C359** — **готово**: Локализован и исправлен Classic completed-seek race: parent cache complete опережал локальный fragment index. Старый код отказал с задержкой 800 ms, исправленный прошёл на том же MP4; подтверждены обычный EXE и. Источник: [ProgressiveCompletionChecklist.md:359](ProgressiveCompletionChecklist.md).
  - **P17.C361** — **осталось**: Три проверенные сборки, Windows EXE/MSI и Linux .deb, SDK и инструкции интеграции. Источник: [ProgressiveCompletionChecklist.md:361](ProgressiveCompletionChecklist.md).
    - **P17.C361.01** — **осталось**: Три окончательные проверенные сборки из одного commit. Источник: [ProgressiveCompletionChecklist.md:361](ProgressiveCompletionChecklist.md).
    - **P17.C361.02** — **осталось**: Windows Classic EXE и MSI. Источник: [ProgressiveCompletionChecklist.md:361](ProgressiveCompletionChecklist.md).
    - **P17.C361.03** — **осталось**: Windows Qt EXE и MSI. Источник: [ProgressiveCompletionChecklist.md:361](ProgressiveCompletionChecklist.md).
    - **P17.C361.04** — **осталось**: Linux DEB и portable. Источник: [ProgressiveCompletionChecklist.md:361](ProgressiveCompletionChecklist.md).
    - **P17.C361.05** — **осталось**: Совместимый SDK, writer/client и инструкции интеграции. Источник: [ProgressiveCompletionChecklist.md:361](ProgressiveCompletionChecklist.md).
  - **P17.C362** — **осталось**: Исходники, воспроизводимые fixtures, версии/build options/licenses компонентов и итоговая matrix с exit codes. Источник: [ProgressiveCompletionChecklist.md:362](ProgressiveCompletionChecklist.md).
    - **P17.C362.01** — **осталось**: Исходники и воспроизводимые test fixtures с происхождением. Источник: [ProgressiveCompletionChecklist.md:362](ProgressiveCompletionChecklist.md).
    - **P17.C362.02** — **осталось**: Backend versions/build options/licenses. Источник: [ProgressiveCompletionChecklist.md:362](ProgressiveCompletionChecklist.md).
    - **P17.C362.03** — **осталось**: Итоговая matrix с точными native exit codes. Источник: [ProgressiveCompletionChecklist.md:362](ProgressiveCompletionChecklist.md).
  - **P17.C363** — **осталось**: Независимые установки/профили/регистрация; Linux обновлён с backup и проверкой сохранности INI. Источник: [ProgressiveCompletionChecklist.md:363](ProgressiveCompletionChecklist.md).
    - **P17.C363.01** — **осталось**: Clean install/upgrade Windows Classic и Qt без смешения профилей. Источник: [ProgressiveCompletionChecklist.md:363](ProgressiveCompletionChecklist.md).
    - **P17.C363.02** — **осталось**: Clean install/upgrade Linux с backup и сохранением INI. Источник: [ProgressiveCompletionChecklist.md:363](ProgressiveCompletionChecklist.md).
    - **P17.C363.03** — **осталось**: Проверка независимости регистраций/IPC и соседних MPC/MPMY установок. Источник: [ProgressiveCompletionChecklist.md:363](ProgressiveCompletionChecklist.md).
  - **P17.C364** — **осталось**: Итоговый GitHub release и проверка скачанных assets/checksums/source commit. Источник: [ProgressiveCompletionChecklist.md:364](ProgressiveCompletionChecklist.md).
    - **P17.C364.01** — **осталось**: Опубликовать окончательный GitHub release после итоговых gates. Источник: [ProgressiveCompletionChecklist.md:364](ProgressiveCompletionChecklist.md).
    - **P17.C364.02** — **осталось**: Скачать опубликованные assets и проверить checksums/source commit. Источник: [ProgressiveCompletionChecklist.md:364](ProgressiveCompletionChecklist.md).

- **H** — **внешнее условие**: Атрибуция четырёх исторических сбоев. Источник: [ProgressiveRemainingWork.md](ProgressiveRemainingWork.md).
  - **H.C119** — **внешнее условие**: Локализовать единичное превышение 2 s автоматического release source/cache на паузе в исправленной Linux-сборке. Источник: [ProgressiveCompletionChecklist.md:119](ProgressiveCompletionChecklist.md).
    - **H.C119.01** — **внешнее условие**: Сохранить close-stage/PID/TID/lock/native-I/O данные при новом естественном Linux release >2s. Источник: [ProgressiveCompletionChecklist.md:119](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
    - **H.C119.02** — **внешнее условие**: Установить причину по захваченному естественному отказу; не приписывать ему искусственный registry lock. Источник: [ProgressiveCompletionChecklist.md:119](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
  - **H.C129** — **внешнее условие**: Ограниченный continuous lifecycle stress и paused expiry: итоговый повтор по 100 измеряемых +4 warmup cycles и 12 настоящих 30000 ms timeout каждой редакции прошёл прежние budgets. Источник: [ProgressiveCompletionChecklist.md:129](ProgressiveCompletionChecklist.md).
    - **H.C129.01** — **внешнее условие**: Сохранить baseline и failed resource snapshot до assert при новом Classic heartbeat-12 отказе. Источник: [ProgressiveCompletionChecklist.md:129](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
    - **H.C129.02** — **внешнее условие**: При воспроизведении установить инициатора; не считать DWM-fix доказательством причины старого события. Источник: [ProgressiveCompletionChecklist.md:129](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
  - **H.C137** — **внешнее условие**: Исторический Windows Qt `windowClose:reader-release` assertion FAIL остаётся неатрибутированным: нет исходного JSON и индивидуального времени ожидания. Источник: [ProgressiveCompletionChecklist.md:137](ProgressiveCompletionChecklist.md).
    - **H.C137.01** — **внешнее условие**: Сохранять первый JSON/receipt и точное время неуспешного Windows Qt наблюдения. Источник: [ProgressiveCompletionChecklist.md:137](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
    - **H.C137.02** — **внешнее условие**: При воспроизведении атрибутировать старый reader-release assertion отдельно от времени закрытия окна. Источник: [ProgressiveCompletionChecklist.md:137](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
  - **H.C158** — **внешнее условие**: AAC heartbeat-срез 1.84: Linux/Windows Qt — по 17 PASS и точный exit 0. Источник: [ProgressiveCompletionChecklist.md:158](ProgressiveCompletionChecklist.md).
    - **H.C158.01** — **внешнее условие**: Захватить origin/callback временных TppWorkerThread при превышении строгого AAC budget. Источник: [ProgressiveCompletionChecklist.md:158](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.
    - **H.C158.02** — **внешнее условие**: Установить инициирующий callback и проверить исходный немедленный resource gate. Источник: [ProgressiveCompletionChecklist.md:158](ProgressiveCompletionChecklist.md).
      Нужен новый естественный отказ с сохранёнными исходными данными; бессрочные повторы успешного случая не назначаются.

- **F0** — **готово**: Общая архитектура и три независимые редакции. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F0.01** — **готово**: Сравнение возможностей и правила Classic↔Qt переноса. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F0.02** — **готово**: Раздельные branding/логотипы/#LiberVixer и названия трёх редакций. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F0.03** — **готово**: Независимые EXE/INI/IPC endpoints/ProgID и portable данные. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).

- **F1** — **готово**: Общие команды, defaults и профили. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F1.01** — **готово**: Общий каталог43 команд и адаптеры с постоянными IDs. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F1.02** — **готово**: Общие Qt defaults; сохранённый профиль выше defaults. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F1.03** — **готово**: Сохранить native defaults/клавиши Classic после отката. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F1.04** — **готово**: OSD off, запоминание окна и отключённое ограничение пропорций окна по кадру. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F1.05** — **готово**: Проверки новых/изменённых профилей, клавиш и сохранности установок. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).

- **F2** — **частично**: Обе Qt-платформы и автоматическая проверка. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F2.01** — **готово**: Общие CMake группы Qt и отдельные native OS tests. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F2.02** — **готово**: Win32 APPCOMMAND и Linux MPRIS адаптеры. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F2.03** — **осталось**: Классифицировать весь Qt-корпус: общий/OS-adapter/Linux-only/Windows-only. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F2.04** — **осталось**: Подключить самостоятельный Windows Qt CI workflow. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F2.05** — **осталось**: Проверять portable Windows Qt на системном PATH без developer SDK. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F2.06** — **осталось**: Убрать Linux-only backend варианты из Windows Qt и показывать реальный fallback. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).

- **F3** — **готово**: Интерфейс и управление семейства. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F3.01** — **готово**: Девять общих Qt меню, контекстные действия, дерево/вкладки/поиск/загрузка настроек. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F3.02** — **готово**: Classic: только порядок родных меню/страниц с сохранением переводов и темы. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F3.03** — **готово**: Classic dark settings: рамки/scrollbars/checkboxes/встроенные фильтры. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F3.04** — **готово**: Панель Classic↔Qt: hover/pressed, Previous/Next, Open/Frame ПКМ. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F3.05** — **готово**: Заголовок/нижняя строка/громкость/цвет линий глав и светлая тема. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F3.06** — **готово**: Плейлисты, история, избранное и собственные форматы каждой редакции. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).

- **F5** — **отложено**: Мастер переноса библиотеки и предпочтений. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F5.01** — **отложено**: Нейтральный формат истории/избранного и общих предпочтений Classic↔Qt. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
  - **F5.02** — **отложено**: Предпросмотр импорта с несовместимыми полями и Windows/Linux paths. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
  - **F5.03** — **отложено**: Проверить перенос без изменения исходной установки/файлов. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.

- **F6** — **частично**: Мультимедиа и платформенные ограничения. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.01** — **готово**: Программные результаты17 принятых Linux этапов в документированных границах. Источник: [LinuxParityPlan.md:18](LinuxParityPlan.md).
    - **F6.01.L05** — **готово**: Действия после окончания — только описанная программная часть. Источник: [LinuxParityPlan.md:22](LinuxParityPlan.md).
    - **F6.01.L01** — **готово**: Открытие файлов и источников — только описанная программная часть. Источник: [LinuxParityPlan.md:23](LinuxParityPlan.md).
    - **F6.01.L02** — **готово**: Онлайн-видео и сеть — только описанная программная часть. Источник: [LinuxParityPlan.md:24](LinuxParityPlan.md).
    - **F6.01.L03** — **готово**: Диски и устройства — только описанная программная часть. Источник: [LinuxParityPlan.md:25](LinuxParityPlan.md).
    - **F6.01.L04** — **готово**: Управление воспроизведением — только описанная программная часть. Источник: [LinuxParityPlan.md:26](LinuxParityPlan.md).
    - **F6.01.L06** — **готово**: Плейлист — расширенные возможности — только описанная программная часть. Источник: [LinuxParityPlan.md:27](LinuxParityPlan.md).
    - **F6.01.L07** — **готово**: Форматы и поведение плейлиста — только описанная программная часть. Источник: [LinuxParityPlan.md:28](LinuxParityPlan.md).
    - **F6.01.L08** — **готово**: История и избранное — только описанная программная часть. Источник: [LinuxParityPlan.md:29](LinuxParityPlan.md).
    - **F6.01.L09** — **готово**: Окно и полный экран — только описанная программная часть. Источник: [LinuxParityPlan.md:30](LinuxParityPlan.md).
    - **F6.01.L10** — **готово**: Оформление, OSD и превью — только описанная программная часть. Источник: [LinuxParityPlan.md:31](LinuxParityPlan.md).
    - **F6.01.L11** — **готово**: Клавиатура, мышь, внешнее управление — только описанная программная часть. Источник: [LinuxParityPlan.md:32](LinuxParityPlan.md).
    - **F6.01.L12** — **готово**: Pan&Scan и преобразования — только описанная программная часть. Источник: [LinuxParityPlan.md:33](LinuxParityPlan.md).
    - **F6.01.L13** — **готово**: Расширенные субтитры — только описанная программная часть. Источник: [LinuxParityPlan.md:34](LinuxParityPlan.md).
    - **F6.01.L14** — **готово**: Расширенный звук — только описанная программная часть. Источник: [LinuxParityPlan.md:35](LinuxParityPlan.md).
    - **F6.01.L15** — **готово**: Видеовывод и обработка — только описанная программная часть. Источник: [LinuxParityPlan.md:36](LinuxParityPlan.md).
    - **F6.01.L16** — **готово**: Свойства, диагностика, сохранение — только описанная программная часть. Источник: [LinuxParityPlan.md:37](LinuxParityPlan.md).
    - **F6.01.L17** — **готово**: Интеграция, профиль, обслуживание — только описанная программная часть. Источник: [LinuxParityPlan.md:38](LinuxParityPlan.md).
  - **F6.02** — **готово**: Qt Windows/Linux: два PCM выхода и собственные audio adapters. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.03** — **осталось**: Общие сценарии SRT/ASS, оформления и сохранения subtitle editor. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.04** — **осталось**: Сравнить семантику audio delay/gain/mixer/filter диапазонов трёх редакций. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.05** — **осталось**: Общие пресеты crop/pan/цвет/скейлеры и диагностика фактического decoder/renderer/AO. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.06** — **осталось**: WinLIRC/SMTC/global hotkeys: определить применимую платформенную совместимость без требования одинакового backend. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.07** — **осталось**: Расширенный безопасный GLSL hook importer за пределами текущего ограниченного профиля. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F6.08** — **отложено**: Физические мониторы/GPU/HDR/стерео/HDMI/S-PDIF/ресиверы/пульты. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
  - **F6.09** — **отложено**: Меню дисков. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
  - **F6.10** — **отложено**: Аналоговое ТВ. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.
  - **F6.11** — **отложено**: Физические устройства захвата/ТВ. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
    Пользователь отложил перенос/аппаратную проверку; network/removable допуски отдельно условны и не объявлены поддержанными.

- **F7** — **частично**: Общий цикл сборки и версии семейства. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F7.01** — **готово**: 0.2.0: три portable, Windows MSI/EXE и Linux DEB опубликованы. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F7.02** — **готово**: Общий family release manifest с commit/versions/hashes. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F7.03** — **осталось**: Общий CI цикл трёх редакций с aggregate report. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).
  - **F7.04** — **осталось**: Один процесс сборки пакетов из одного commit и общий release gate. Источник: [FoxicMPFamilyPlan.md](FoxicMPFamilyPlan.md), [FoxicMPFamilyMatrix.md](FoxicMPFamilyMatrix.md).

## Границы и порядок обновления

- Это исчерпывающий учёт известных записанных работ на дату базы, а не обещание посчитать ещё не определённые варианты всех медиафайлов.
- P02.C149.01 требует заморозить конечную обязательную медиа-матрицу. Её последующая детализация отдельно видна в added/decomposition, а не маскируется изменением знаменателя.
- Готовность импортирована из имеющихся доказательств; полный новый runtime-аудит при этом пересчёте не выполнялся.
- Результаты отдельного узкого профиля закрывают его собственные листья, а не весь контейнер/кодек/общий ТЗ.
- Базовые отчёты и новые исполнимые этапы связаны без суммирования одинаковых пересказов FamilyPlan/LinuxParityPlan.
- Автоматический updater, полноценная конверсия HLSL→GLSL и 1:1 backend parity не объявляются обязательными новыми работами из одного факта отсутствия.
- Wayland исключён пользователем; не задача текущего плана
- Не переносить Qt страницы/поиск/загрузку в Classic; сохранять родные UI/defaults
- DirectShow/HLSL и GLSL/libmpv — документированные отличия движков, не автоматические задачи переноса
- Реальная Harvester интеграция блокирована отдельным разрешением; не менять загрузчик
- Старые snapshots и повторные test cases не становятся новыми задачами
- Не обещать все комбинации расширений; конечный remaining-layout перечень должен быть явно зафиксирован в P02.C149.01

Числа вычисляются из [JSON-реестра](FoxicMPWorkRegistry.json), не правятся вручную.
Проверка/сводка: `python3 tools/foxicmp_work_status.py`.
Обновление этого документа: `python3 tools/foxicmp_work_status.py --write`.
Сравнение с последним сохранённым отчётом: `--compare ПУТЬ_К_СНИМКУ`.
Новый снимок: `--snapshot ПУТЬ_К_НОВОМУ_СНИМКУ` (существующий не перезаписывается).
Перед сохранением снимка показать пользователю added/closed/reopened и причины новых пунктов.
Сохранить итог задания и обновить точку сравнения: `--write --record-report`.
По умолчанию сравнение идёт с последним отчётом; исходная база остаётся неизменной.
Новый ID требует `additionType` = `new-requirement`, `decomposition` или `omission`
и непустую `changeReason`. Удаление и повторное открытие также требуют причины.
Статусы листьев меняются только со ссылкой на проверку результата. Повтор теста
добавляет доказательство к прежнему ID, а не новую выполненную задачу.
После задания выводить три уровня всего/готово/осталось и добавленные пункты;
промежуточные сообщения счётчиком не перегружать.
