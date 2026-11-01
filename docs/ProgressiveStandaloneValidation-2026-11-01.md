# Проверки SDK 1.57: один самостоятельный поток

Дата: 1 ноября 2026. Приложение 0.2.0.
[Контракт](ProgressiveStandalone.md), [машинный отчёт](ProgressiveStandaloneValidation-2026-11-01.json).
Harvester не изменялся.

- Linux Qt и Windows Qt: по четыре реальных 20-секундных источника — AAC mono
  48 kHz с неизвестной начальной длиной, AAC stereo 44.1 kHz, AVC Main/B-frames
  и VFR. Первые metadata bytes дают waiting-initialization, 4 s prefix —
  buffering, 12 s — playing. Проверены pause, backward seek, resume, ожидание
  недокачанного tail, complete, seek 17 s и natural EOF/release с тем же readerId.
- В Qt проверены реальный AAC decode/rate/channels без video track либо H.264
  и меняющиеся framebuffer hashes без audio track. Для video-only задан
  недоступный `audio-device`; аудиовыход для этого потока не требуется.
  Это синтетический backend-тест, не аттестация физического оборудования.
- Classic: четыре профиля с известной длиной, затем отдельный повтор AAC mono
  с неизвестной длиной. Все PASS. Использованы штатные splitter/decoder/graph
  и обычные pause/chapter commands; проверены graph state, decoder reads,
  ожидание и EOF/release. Framebuffer и физический аудиовыход Classic этим
  этапом не измеряются.
- Native Linux и Windows: 23 проверки. Для каждого из четырёх профилей —
  один Track/resource, индекс с фактическим kind, точные bytes, переданные
  декодеру, отсутствие EOF до complete, последний владелец reader и cleanup.
  Семь отрицательных сценариев: optional вместо required, неверный codec,
  несовпадающий actual kind, отсутствие SHA, разрыв prefix, ненулевой offset,
  неподдержанный WebM. Отклоняются без оставшихся readers.
- ASan/UBSan: полный production Fmp4V2Bridge и настоящий ResourceSource/leases,
  23 те же native checks, включая unknown-length mono. Сборка -O0/-g0,
  address+undefined, detect_leaks/halting включены, exit 0.
- SDK: 40 v2 unit tests на каждой ОС, exit 0. Новые проверки требуют отдельную
  versioned capability и отклоняют extra resources/unsupported shapes до start.
- Совместимость обеих Qt: пять completed-source seek profiles SDK 1.56,
  два Opus/fMP4 regression profiles, выбранная optional Opus/fMP4 дорожка.
  Последняя отдельно проверяет физический slot 2: media kind audio нельзя
  использовать как номер дорожки. Все группы — exit 0.
- Classic дополнительно: четыре shared fMP4 compatibility profiles и v1
  sessionDirectory в первом прогоне; optional Opus/fMP4 и v1 во втором.
  Две временные test tasks/stages удалены. ContextMenuPrompted=1 и
  ContextMenuEnabled=0 записаны до первого старта; HKCU commands/icons сверены
  до/после каждого процесса. Установленные Windows EXE/INI сохранены.
- Linux UI/audio изолированы частными X11/D-Bus/PulseAudio через run_tests.sh;
  пользовательский сеанс/микшер не менялись. Проверенный EXE установлен атомарно
  с backup и проверкой INI/прочих личных файлов; просмотр не останавливался.
- Один ранний UI-запуск произошёл до окончания линковки нового тестового EXE и
  получил `Function not found`, exit 8. После завершения сборки окончательные
  UI/native группы прошли; этот ранний harness result отделён от итоговых exit codes.

Это ограниченные scalar fMP4 AAC-LC/H.264 профили. Другие codec/container pairs,
segments/live, произвольные sparse media, реальный Harvester и длительная
физическая A/V-приёмка остаются открытыми. V1 wire format, two-track defaults,
существующие CLI/API и пользовательские настройки не заменялись.
