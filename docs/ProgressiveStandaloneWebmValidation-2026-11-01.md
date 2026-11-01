# Проверки SDK 1.59: самостоятельное WebM-аудио

1 ноября 2026, приложение 0.2.0. [Контракт](ProgressiveStandaloneWebm.md),
[машинный отчёт](ProgressiveStandaloneWebmValidation-2026-11-01.json).
Harvester не изменялся.

- Linux Qt, Windows Qt и Windows Classic: четыре настоящих 20-секундных
  источника — WebM Opus mono/stereo и Vorbis mono/stereo, все 48 kHz.
  Начальная длина mono неизвестна. Проверены waiting-init, buffering после
  четырёх Cluster, playing после двенадцати, pause, seek 2 s в паузе, resume,
  ожидание недокачанного tail, complete, seek 17 s, natural EOF и release.
  ReaderId сохраняется, ресурс после теста совпадает с оригиналом.
- В Qt проверены реальный codec decoder, sample rate, число каналов и ровно
  одна audio track без video track. Classic использует штатный Matroska
  splitter/Opus или Vorbis decoder/DirectShow graph, native pause/chapter
  commands; проверены состояния, decoder reads и освобождение ресурсов.
  Физический аудиовыход не измеряется.
- Native Linux и Windows: по 47 проверок. Четыре профиля дают kind-aware
  index, полное число packets после complete, точные decoder bytes, отсутствие
  EOF до complete и last-owner release. Отказы не оставляют readers.
- Отрицательные сценарии включают неверные/пустые codec hints, video kind,
  optional track, AAC вместо WebM-аудио, SHA/prefix/offset; повреждённые
  OpusHead signature/version/channels/mapping, pre-skip/CodecDelay/SeekPreRoll;
  Vorbis Xiph count, identification signature/version/channels/rate/block size,
  framing bits, comment и setup. Header checks применяются к реальному slot 0.
- Те же 47 проверок — ASan/UBSan полного production Fmp4V2Bridge и
  ResourceSource, -O0/-g0, detect_leaks и halt_on_error, exit 0.
- SDK: по 42 v2 unit tests на каждой ОС. Standalone WebM требует отдельной
  versioned capability и точного профиля. Старый standalone fMP4 и общая
  поддержка WebM недостаточны. Video/AAC/external-init/optional не допускаются.
- Совместимость Linux Qt: четыре прежних AAC/H.264 standalone профиля,
  два standalone fMP4 Opus и выбранный optional fMP4 Opus. Windows Qt:
  два standalone fMP4 Opus и выбранный optional fMP4 Opus. Native fMP4 Opus
  conformance также прошёл на обеих ОС.
- Classic дополнительно прошёл standalone fMP4 Opus mono с неизвестной длиной,
  optional Opus/fMP4 и v1 sessionDirectory. Перемотка standalone slot 0 теперь
  применяет audio preroll, а media kind не подменяет source index.
- Перед запуском частной копии Classic: ContextMenuPrompted=1,
  ContextMenuEnabled=0. HKCU commands/icons проверены до/после каждого процесса.
  Одноразовая task и stage удалены, SHA испытанного EXE совпадает со сборкой.
  Установленные Windows EXE/INI остались неизменными.
- Linux UI/audio проверялись через run_tests.sh на частных X11/D-Bus/PulseAudio.
  Проверенный Linux EXE обновлён атомарно с резервной копией и сверкой текущего
  INI/прочих файлов установки. Пользовательский сеанс/микшер не менялись.

Итоговые группы завершились с exit 0. Этот отчёт закрывает ограниченные
standalone WebM audio profiles. WebM video, другие аудиокодеки/контейнеры,
segments/live, произвольные sparse media, реальная интеграция Harvester и
длительная физическая A/V-приёмка остаются открытыми.
