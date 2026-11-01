# Проверки SDK 1.58: самостоятельное Opus-аудио

1 ноября 2026, приложение 0.2.0. [Контракт](ProgressiveStandaloneOpus.md),
[машинный отчёт](ProgressiveStandaloneOpusValidation-2026-11-01.json).
Harvester не изменялся.

- Linux Qt, Windows Qt и Windows Classic: реальные 20-секундные Opus mono и
  stereo 48 kHz. Начальная длина mono неизвестна. Проверены waiting-init,
  buffering после 4 s, воспроизведение после 12 s, pause, seek 2 s в паузе,
  resume, ожидание недокачанного tail, complete, seek 17 s, natural EOF и
  освобождение файлов. ReaderId сохраняется, ресурс после теста идентичен.
- Qt проверяет реально выбранный Opus decoder, 48 kHz и mono/stereo; существует
  ровно одна audio track и нет video track. Classic использует штатный MP4
  splitter/Opus decoder/DirectShow graph, обычные pause и chapter commands;
  наблюдаются состояния и чтения декодера. Физический аудиовыход не измеряется.
- Native Linux и Windows: по 27 проверок — один поток/ресурс, role-aware index,
  полный индекс после complete, точные decoder bytes, EOF только после complete,
  last-owner release. Неверные codecHint, dOps version/channels/mapping, sample
  entry rate/channels, media clock, pre-skip/edit и edit rate отклоняются без leases.
- Те же 27 проверок прошли под ASan/UBSan полного production Fmp4V2Bridge и
  ResourceSource, -O0/-g0, detect_leaks и halt_on_error, exit 0.
- SDK: по 41 v2 unit tests на каждой ОС. Opus требует собственного standalone
  профиля; прежняя capability AAC и общая поддержка парного Opus недостаточны.
- Совместимость Linux Qt и Windows Qt: все четыре прежних standalone AAC/H.264
  профиля; выбранный optional Opus/fMP4 в физическом slot 2. Classic дополнительно
  проверен с optional Opus/fMP4 и v1 sessionDirectory. Все итоговые группы exit 0.
- До первого запуска тестовой копии Classic заданы ContextMenuPrompted=1,
  ContextMenuEnabled=0. HKCU commands/icons сверены до/после каждого процесса.
  Одноразовая test task и частная stage удалены; проверены SHA собранного и
  испытанного EXE. Установленные Windows EXE/INI не менялись.
- Linux UI/audio используют частные X11/D-Bus/PulseAudio через run_tests.sh.
  Проверенный Linux EXE заменён атомарно, прежний сохранён для отката,
  текущий INI и прочие файлы установки сверены до/после. Просмотр не прерывался.

Это ограниченное дополнение к scalar fMP4, а не полная аудиоматрица ТЗ.
Самостоятельные WebM/Opus/Vorbis, остальные кодеки/контейнеры, segments/live,
реальный Harvester и физическая длительная A/V-приёмка остаются открытыми.
