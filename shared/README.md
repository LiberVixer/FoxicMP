# Общая основа FoxicMP

`familydefaults.h` — базовые значения новых профилей Classic и обеих Qt-сборок.
Сохранённый профиль всегда имеет приоритет. Значения, зависящие от ОС/движка,
остаются в собственных слоях приложений.

`familycommands.inc` — 43 общие команды: стабильный ID Qt, ID Classic,
PortableText клавиши Qt и Win32 key/modifiers. Символы ID Classic нужны только
его адаптеру; Qt получает их строковые имена для существующего сопоставления
меню. Переводимые подписи берутся из прежних ресурсных каталогов.

Classic выбирает стандартную клавишу до создания wmcmd: Restore/сброс сохраняет
правильную исходную комбинацию. APPCOMMAND, жесты мыши и ресурс подписи не меняются.
Qt применяет каталог к меню/стандартным клавишам. Для нового профиля из
платформенного снимка убираются явные значения общих keys: стандартные клавиши
остаются fallback, чтобы пользовательское назначение могло подавить конфликт. Существующий, в том числе без номера
версии, сохраняет назначенные и явно отключённые клавиши.

Для редакционных или платформенных команд продолжают действовать собственные
каталоги. Полный перенос всех параметров/команд не объявляется завершённым.
Нельзя переименовывать существующий ID, не сохранив совместимость CLI/Web/IPC.

Эталон организации интерфейса — Linux Qt, см.
[общий план](../docs/FoxicMPFamilyPlan.md).

`progressivesession.h` — переносимая модель чернового manifest v2: точные
64-битные строки, ресурсы/диапазоны/сегменты, дорожки, метаданные и проверка
обновлений. Один парсер используется Qt и Classic. Он не открывает файлы и не
доказывает поддержку контейнера или кодека.

`progressivereaders.h` — in-memory модель reader lifecycle с PID/birth identity,
несколькими читателями и освобождением после закрытия файлов.
`progressivereaderlease.h` подключает её к настоящим media-дескрипторам player-owned
v1 sources в Qt и Classic: приватные квитанции, OS locks, recovery по OS process
identity и одинаковый IPC handler. Для динамических ресурсов v2 привязка ещё впереди.
[Контракт и ограничения](../docs/ProgressiveManifestV2.md).
[Runtime reader leases](../docs/ProgressiveReaderLeases.md).

`progressiveresourceio.h` — закреплённые файлы v2, чтение только подтверждённых
диапазонов, ожидание/отмена, независимые leases ресурсов, EOF по complete.
`progressiveresourceprobe.h` — общий асинхронный I/O-стенд в API трёх редакций.
Он не активирует playback. Контроль перезаписи ограничен последним прочитанным
окном каждого файла; полной проверки integrity и demux/decode пока нет.
[Контракт I/O](../docs/ProgressiveResourceIo.md).

`../src/DSUtil/HarvesterTS.h` — общий внутренний C++17 transport parser для
будущего сегментного MPEG-TS: PAT/PMT/CRC, continuity, PCR/PTS/DTS и PES payload
ranges. Используется внутренними codec/index/ResourceSource слоями; к движкам
плееров/IPC пока не подключён; [границы парсера](../docs/ProgressiveTsTransport.md).

`../src/DSUtil/HarvesterTSCodec.h` — внутренний общий framing/clock/index слой
поверх TS PES: ограниченный fixed-rate AVC, ADTS AAC, declared envelope,
межсегментные invariants, IDR/audio seek и readiness.
[Границы и результаты](../docs/ProgressiveTsAccessIndex.md); в SDK 1.39 используется через общий TS → fMP4 адаптер.

`progressivetsstream.h` — внутренний TS ResourceSource/staging: подтверждённый
непрерывный префикс, bounded cache/leases/SHA, inline или paged catalog,
виртуальные byte reads и сборка AVC/AAC access units. Есть проверки GCC/MinGW/
MSVC и внешнего FFmpeg; [профиль и ошибки](../docs/ProgressiveTsResourceStream.md).
SDK 1.39 подключает его через `progressivetsmp4.h` к общему playback bridge:
виртуальные moov/moof/mdat, AVC length prefixes и AAC без ADTS, без перекодирования.
[TS playback всех трёх редакций](../docs/ProgressiveTsPlayback.md).

SDK 1.40 расширяет TS parser/ResourceSource/MP4 bridge явным профилем `mpegts-muxed`: один AVC PID и один AAC PID, общий init/media и две logical views. Sharing в каталоге разрешён только этому профилю; старые режимы сохраняют проверки distinct resources. [Контракт и границы](../docs/ProgressiveMuxedTsPlayback.md).

SDK 1.41: `HarvesterTSGrowing.h` и `progressivetsgrowing.h` добавляют один
растущий TS resource с inline PAT/PMT/SDT и двумя scalar views
`mpegts-growing`. Индексы публикуются по завершённым AAC PES и закрытым AVC
GOP, оба decoder view получают virtual fMP4. Partial tail ждёт update, SHA и
ошибки источника общие для обеих дорожек. [Контракт](../docs/ProgressiveGrowingTs.md).

SDK 1.42: `HarvesterMP4Muxed.h` строит отдельные moov/traf для двух дорожек
одного growing fMP4. `progressivemuxmp4.h` соединяет immutable virtual layout,
полный общий mdat и ResourceSource/SHA/lease; обычный `FragmentIndex` проверяет
выбранные samples, затем общий проход исключает пересечения/дыры payload.
[Контракт](../docs/ProgressiveMuxedFmp4.md), H.264/AAC-LC VOD.

SDK 1.43: `progressivemuxscalar.h` общий для shared fMP4 и WebM;
`progressivemuxwebm.h` задаёт VP9/Opus профиль. `HarvesterWebMMuxed.h`
проектирует свою TrackEntry/Cluster, сохраняя исходные Block/BlockGroup
в direct Source spans. `WebMIndex` сохраняет fractional DefaultDuration
и использует Opus TOC с однократным применением padding независимо от
округлённого duration hint. [Контракт](../docs/ProgressiveMuxedWebm.md).

SDK 1.44: shared WebM выбирает VP9/AV1 по неизменному manifest codecHint;
actual TrackEntry/configuration обязаны совпадать. `progressivemuxscalar.h`
сохраняет выбранный codec/mode в каждом атомарно опубликованном snapshot;
fMP4 по-прежнему ограничен AVC/AAC.

SDK 1.45: shared WebM дополнен VP8/Opus. Выбор codec/mode, общий resource
и SHA/leases сохранены; первый VP8 packet проверяет существующий WebMIndex.
Actual codec обязан совпасть с hint, прежний default — VP9.

SDK 1.46: shared scalar bridge хранит обе выбранные codec identities;
WebM поддерживает Opus/Vorbis, fMP4 остаётся AVC/AAC. Vorbis сохраняет state
между Clusters; CodecDelay sample-aligned и bounded, padding вычитается один раз.
