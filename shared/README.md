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
ranges. Пока не подключён к Source/index/decoder/IPC; [границы этапа](../docs/ProgressiveTsTransport.md).

`../src/DSUtil/HarvesterTSCodec.h` — внутренний общий framing/clock/index слой
поверх TS PES: ограниченный fixed-rate AVC, ADTS AAC, declared envelope,
межсегментные invariants, IDR/audio seek и readiness.
[Границы и результаты](../docs/ProgressiveTsAccessIndex.md); в приложения пока не включён.
