# Проверка SDK 1.55: tail-moov MP4

Дата: 1 ноября 2026. Приложение 0.2.0; общий код трёх редакций.
[Машинный отчёт](ProgressiveTailMp4Validation-2026-11-01.json),
[контракт](ProgressiveTailMp4.md). Harvester не изменялся.

- Linux Qt: частные X11/D-Bus/PulseAudio через `qt/tests/run_tests.sh`.
  Windows Qt: отдельные временные профили. Classic: отдельная копия,
  ContextMenuPrompted=1/ContextMenuEnabled=0 до запуска; HKCU shell registration
  сверена до/после. Установленные Windows EXE/INI не изменены.
- Каждая редакция действительно воспроизвела три 20-секундных источника:
  Main/B-frames, High 30000/1001 + 44100 Hz, VFR. Проверены waiting-index без
  reader, header/whole-moov advisory hints, buffering, playing до complete,
  pause, update без снятия pause, seek назад, resume, ожидание доступной границы,
  complete, seek вперёд, natural EOF и release с тем же readerId.
- В обеих Qt дополнительно проверены меняющиеся framebuffer hashes, video codec,
  audio codec/rate/channels. Classic подтверждает native graph state и decoder
  reads; framebuffer/физический аудиовыход этими проверками не измеряется.
- Linux и Windows native: 42 проверки, включая пять реальных fixtures
  (четыре по 4 s и один 80 s), точные compressed packet hashes, PTS/DTS,
  декодированное видео и PCM, стабильность опубликованных virtual bytes,
  SHA/truncation/cancel, malformed tables и освобождение leases.
  Metadata probe отдельно проходит header → whole moov → data, update waiter,
  cancel, повреждение SHA и forget. Он не читает payload пропущенного mdat.
- Совместимость старого head-moov профиля: MP4 с 17 дополнительными free atoms
  открывается с точными packet hashes и без оставшихся readers. Новый предел
  16 atoms относится только к tail-index режиму.
- Дополнительно пять boundary checks на Linux/Windows: третий range, отдельный
  островок media, metadata без media prefix, превышение 1 MiB metadata и 16 atoms.
  Все отвергаются без оставшихся readers. Итого 47 native checks на каждой ОС.
- ASan/UBSan: 35 projection checks и пять boundary checks, exit 0. Focused
  `FOXICMP_TAIL_MP4_ONLY`; production codec validator проверен обычным native
  runner и настоящими плеерами. Sanitizer не заменяет Windows UI-проверки.
- SDK Python: 38 v2 + 23 resource-probe tests на Linux и Windows, exit 0.
  Проверены explicit capability, waiting-index recovery, request identity,
  advisory scope/bounds, отсутствие playback/network команд в diagnostic API.
- Шесть совместимых CTest-групп сохранены: head ordinary MP4 native/playback,
  shared fMP4 native, WebM/Vorbis native, RAM preview и Opus regression.
  Вместе с новым tail native/playback — восемь различных групп на каждой ОС.
  Первый общий прогон имел exit 8 из-за отсутствующего Python SDK import path
  **в новом тестовом writer**. Порядок импорта исправлен; повтор только этой
  playback-группы на каждой ОС дал exit 0, три профиля PASS. Первоначальный exit
  и повтор сохранены отдельно в JSON; остальные семь групп прошли общий прогон.
  После head-only исправления совместимости окончательные сборки обеих Qt
  снова прошли все четыре MP4 группы (head/tail, native/playback), exit 0;
  Classic пересобран и повторно прошёл три tail профиля, четыре fMP4 и v1.
  Остальные неизменённые режимы проверены предыдущим общим прогоном.
- Classic дополнительно: четыре прежних shared fMP4 compatibility profiles
  и один v1 sessionDirectory сценарий, exit 0. Временные task/stage удалены.
- Проверенный Linux EXE установлен атомарно, прежний сохранён для отката;
  SHA INI и остальных личных файлов проверены. Просмотр не останавливался.

Физическая A/V ошибка/drift, 30-минутная приёмка, реальная интеграция Harvester,
произвольные sparse media, все варианты ISO BMFF и остальные codecs/containers
этим отчётом не подтверждаются. Это ограниченный tail-index профиль,
не завершение всего progressive-ТЗ. Wayland/оборудование не проверялись.
