# Проверки SDK 1.61: самостоятельное HEVC/AV1/VP9 fMP4-видео

1 ноября 2026, приложение 0.2.0. [Контракт](ProgressiveStandaloneFmp4Video.md),
[машинный отчёт](ProgressiveStandaloneFmp4VideoValidation-2026-11-01.json).
Harvester не изменялся.

- Linux Qt, Windows Qt и Windows Classic: шесть реальных 20-секундных источников:
  HEVC Main/hvc1 8-bit 25 fps и Main10/hev1 10-bit 30000/1001 fps с B-frames;
  AV1 Main 8-bit 25 fps и 10-bit 30000/1001 fps; VP9 Profile 0/8-bit 25 fps и
  Profile 2/10-bit 30000/1001 fps с alt-reference encoding. Начальная длина
  fractional profiles неизвестна.
- На каждой редакции: waiting-init, недостаточный начальный буфер, playing
  после публикации 12 s, pause, seek 2 s в паузе, resume, ожидание недокачанного
  tail, complete, seek 17 s, natural EOF и release. ReaderId сохраняется;
  ресурс после теста совпадает с исходником. Writer выбирает целые fragments
  по независимым packet clocks, а не считает fragment эквивалентом секунды.
- Qt проверяет фактический codec, 4:2:0 pixel format/8 или 10 bit и смену
  framebuffer hashes. Ровно один video track без audio track; используется
  недоступный тестовый audio-device. Диагностика сохраняет последний корректный
  codec/pixel format из playing и не теряет его после очистки параметров на EOF.
- Classic использует штатный MP4 splitter/decoder/DirectShow graph и native
  pause/chapter commands. Проверены состояния, чтения и освобождение файлов.
  Framebuffer Classic и физические GPU/HDR/выходы этим этапом не аттестуются.
- Native Linux и Windows: по 69 проверок — один Track/resource, полный индекс,
  точные decoder bytes, EOF только после complete, last-owner release;
  первые восемь presentation clocks/end соответствуют независимым ffprobe PTS
  и duration, включая порядок представления HEVC B-frames.
- Отказы без readers: неверные/пустые codec hints, audio kind, optional track,
  отсутствие конфигурации, SHA, разрыв prefix, ненулевой offset. Проверены
  повреждённые hvcC version/profile/chroma/depth/NAL width/array count и
  неполные hvc1 arrays; av1C marker/profile/twelve-bit/reserved fields;
  vpcC version/flags/profile/depth/chroma/init payload size.
- Те же 69 проверок прошли под ASan/UBSan полного production
  Fmp4V2Bridge/ResourceSource, -O0/-g0, detect_leaks и halt_on_error, exit 0.
- SDK: по 44 v2 unit tests на ОС. Новые кодеки требуют точного standalone
  профиля; прежние H.264/Opus profiles и общая поддержка кодека недостаточны.
- Linux Qt и Windows Qt дополнительно прошли четыре прежних AAC/H.264
  standalone профиля и два standalone Opus/fMP4. Native Opus conformance —
  по 27 проверок на ОС. Classic: прежние H.264 и Opus mono, selected optional
  Opus/fMP4 и v1 sessionDirectory. Итоговые группы exit 0.
- До первого запуска частной Classic-копии заданы ContextMenuPrompted=1 и
  ContextMenuEnabled=0. HKCU commands/icons сверены до/после каждого процесса.
  Одноразовая task/stage удалены; SHA испытанного EXE совпадает со сборкой.
  Установленные Windows EXE/INI остались неизменными.
- Linux UI/audio — частные X11/D-Bus/PulseAudio через run_tests.sh. Проверенный
  EXE установлен атомарно с резервной копией и сверкой текущего INI/остальных
  файлов. Пользовательский сеанс/микшер не менялся, просмотр не прерывался.

Это ограниченные standalone fMP4 video profiles. Остальные codec/container
combinations, segments/live, произвольные sparse media, реальная интеграция
Harvester и длительная физическая A/V-приёмка остаются открытыми.
