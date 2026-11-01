# Проверки SDK 1.56: перемотка после complete

Дата: 1 ноября 2026. Приложение 0.2.0.
[Машинный отчёт](ProgressiveCompletedSeekValidation-2026-11-01.json),
[контракт](ProgressiveCompletedSeek.md). Harvester не изменялся.

- Воспроизведена проблема исходной Qt-сборки: две отдельные schema-2 fMP4
  дорожки открываются недокачанными; после complete переход на 65 s отклоняется.
  Контрольный запуск имеет exit 8. Остальные первоначальные ошибки окончания
  тестового сценария относились к heartbeat после EOF; стенд использует status
  после complete, как действующий SDK helper.
- Linux Qt и Windows Qt: по пять 90-секундных источников H.264/AAC-LC — v1
  ordinary indexed MP4/M4A, v1 fragmented, v2 separate fMP4, v2 shared head MP4
  и v2 shared tail MP4. Это длительность файлов, а не непрерывный просмотр 90 s.
- Общий сценарий: физически неполные файлы → playing → изменение speed/volume/mute
  → ручная pause → complete без снятия pause → 65/2/45 s в paused → resume
  → 88 s → natural EOF/release. Reader identity, аудиовыбор и controls сохраняются;
  исходные media hashes после просмотра совпадают. По пять профилей PASS на каждой ОС.
- Для v2 separate первый дальний target явно проверяется вне decoder cache.
  Кэш ограничен 64 KiB и readahead=0 только в тесте: иначе быстрый декодер иногда
  успевал закэшировать target, делая диагностическое утверждение нестабильным.
  Production cache/defaults не менялись. Проверены реальные меняющиеся framebuffer
  hashes и AAC decode после дальних переходов.
- Windows Classic: три schema-2 профиля (separate/head/tail), тот же сценарий.
  Скорость 1.2, mute и последовательные значения громкости 95/90/85 сохранены;
  в обеих Qt — 1.25, 37%, mute. Это состояние отдельных тестовых проигрывателей,
  не новые defaults. Для Classic проверены native graph rate, IBasicAudio,
  положение, основной audio selection и decoder reads. Framebuffer/физический
  аудиовыход этими Classic-проверками не измеряется.
- Совместимость: tail-index runtime, два Opus/fMP4 regression profiles и RAM
  preview проходят на обеих Qt. Classic дополнительно прошёл четыре fMP4
  compatibility profiles и v1 sessionDirectory.
- Итоговые Qt группы и Classic runner: exit 0; сборки: exit 0. Linux использует
  частные X11/D-Bus/PulseAudio через `qt/tests/run_tests.sh`.
  Один ранний объединённый Linux regression wrapper завершился с exit 143
  после строки о прохождении трёх CTest-групп; группы повторены отдельно,
  exit каждого — 0. Первоначальные диагностические/test-harness ошибки
  сохранены в JSON отдельно от итоговых проверок.
- Windows test-harness до исправления retry получил кратковременный
  PermissionError при чтении атомарно заменяемого ack.json. Добавлено bounded
  ожидание доступа; окончательный прогон всех пяти Windows Qt профилей — PASS.
- Classic запускался из отдельной копии с ContextMenuPrompted=1,
  ContextMenuEnabled=0 до старта; HKCU commands/icons сверены до/после каждого
  процесса. Временные task/stage удалены. Установленные Windows EXE/INI сохранены.
- Linux EXE обновлён атомарно с backup; INI и прочие личные файлы сверены
  по SHA-256. Просмотр не останавливался; integration-файлы установки сохранены.

Новая capability относится к активной сессии до освобождённого natural EOF;
повторный просмотр уже закрытого reader здесь не добавлен. Общая codec matrix,
прочие контейнеры/live, реальные события Harvester и длительная физическая A/V
приёмка остаются открытыми. Новый формат/парсер/ремультиплексор этим этапом
не добавляется. Wayland и оборудование не проверялись.
