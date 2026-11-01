# Проверки SDK 1.60: самостоятельное WebM-видео

1 ноября 2026, приложение 0.2.0. [Контракт](ProgressiveStandaloneWebmVideo.md),
[машинный отчёт](ProgressiveStandaloneWebmVideoValidation-2026-11-01.json).
Harvester не изменялся.

- Linux Qt, Windows Qt и Windows Classic: шесть реальных 20-секундных источников:
  VP8 25 fps и 30000/1001 fps, VP9 Profile 0/8-bit 25 fps и Profile 2/10-bit
  30000/1001 fps, AV1 Main 8-bit 25 fps и 10-bit 30000/1001 fps.
  Начальная длина VP8 и всех fractional profiles неизвестна.
- На каждой редакции проверены waiting-init, недостаточный начальный буфер,
  воспроизведение подтверждённых 12 s, pause, seek 2 s в паузе, resume,
  ожидание недокачанного tail, complete, seek 17 s, natural EOF и release.
  ReaderId не меняется; ресурс после теста совпадает с исходником.
- Qt: реальный video codec и 4:2:0 pixel format/8 или 10 bit проверяются
  во время playing, framebuffer hashes должны меняться. Ровно один video track
  без audio track; задан недоступный тестовый audio-device. Некоторые последние
  диагностические polls уже попали после очистки decoder params на EOF;
  retained decoded/changing assertions прошли. Пустая финальная диагностика
  отмечена в JSON и не подменена выдуманным наблюдением.
- Classic: штатный Matroska splitter/video decoder/DirectShow graph и native
  pause/chapter commands; проверены graph state, decoder reads и освобождение.
  Framebuffer Classic и физические GPU/HDR/выходы этим этапом не аттестуются.
- Native Linux и Windows: по 65 проверок — один реальный Track/resource,
  kind-aware index, полное число packets, точные decoder bytes, EOF только после
  complete, last-owner release; первые восемь packet clocks и отдельно
  округлённые end соответствуют независимому ffprobe и DefaultDuration.
- Отрицательные случаи: неверные/пустые codec hints, audio kind, optional track,
  неподдержанный codec/container, отсутствие SHA, разрыв prefix, ненулевой offset,
  нулевые duration/width/height, первый packet без keyframe flag, laced block,
  повреждённая AV1 configuration. После отказа/закрытия нет readers.
- Те же 65 проверок — ASan/UBSan полного production Fmp4V2Bridge/ResourceSource,
  -O0/-g0, detect_leaks и halt_on_error. Итоговый exit 0.
- SDK: по 43 v2 unit tests на каждой ОС, включая независимую versioned capability
  WebM video и отказ на неподходящих media kind/profiles до start.
- Совместимость Linux Qt: четыре прежних standalone AAC/H.264 и четыре WebM
  Opus/Vorbis mono/stereo. Windows Qt: четыре WebM Opus/Vorbis. Native WebM audio
  conformance — 47 проверок на каждой ОС. Classic: Opus mono, Vorbis stereo,
  selected optional Opus/fMP4 и v1 sessionDirectory. Все итоговые группы exit 0.
- До первого запуска частной Classic-копии заданы ContextMenuPrompted=1 и
  ContextMenuEnabled=0. HKCU commands/icons сверены до/после каждого процесса.
  Одноразовая task/stage удалены; SHA испытанного Classic EXE совпадает со сборкой.
  Установленные Windows EXE/INI не изменены.
- Linux UI/audio — частные X11/D-Bus/PulseAudio через run_tests.sh. Проверенный
  EXE установлен атомарно с backup и проверкой текущего INI/прочих файлов.
  Пользовательский сеанс и микшер не менялись, просмотр не прерывался.

## Ранние результаты и исправления

Первый Classic build получил C2026, exit 1: capability JSON вырос до 16602 bytes,
что превысило лимит одного MSVC string literal. Соседние raw literals сохраняют
единый runtime JSON; SDK extractor поддерживает их и сверяется с установленным
плеером. Финальные сборки и проверки прошли.

Первый Windows Qt прогон — exit 8. Тестовый writer считал 12 Cluster за 12 секунд,
хотя muxer также делит их по размеру и опубликовал лишь 7 секунд. Исправлен выбор
целых Cluster по независимо прочитанным packet PTS, без изменения порогов плеера.
Проверка 10-bit также приведена к обозначению libmpv `yuv420p10` вместо файлового
`yuv420p10le`. Итоговый прогон проверяет реальную глубину/цветовую субдискретизацию,
выводит доступные наблюдения и завершается с exit 0.

Это ограниченные standalone WebM video profiles. Остальные codec/container
combinations, segments/live, произвольные sparse media, реальная интеграция
Harvester и длительная физическая A/V-приёмка остаются открытыми.
