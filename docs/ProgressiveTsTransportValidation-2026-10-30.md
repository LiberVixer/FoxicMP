# Проверка внутреннего MPEG-TS parser — 30 октября 2026

Это transport staging, не TS playback. `HarvesterTS.h` не подключён к движкам,
Source, индексам или IPC. SDK остаётся 1.38, TS capability не объявлена.

| Проверка | Результат | Exit code |
|---|---|---|
| Linux GCC, CTest | 65 сценариев | 0 |
| Windows MinGW, CTest | Те же 65 сценариев | 0 |
| Windows MSVC, отдельный native executable | Те же 65 сценариев | 0 |
| Linux ASan/UBSan + leak detection | Те же 65 сценариев, без ошибок sanitizers | 0 |

В каждом прогоне также выполнены 512 детерминированных мутаций media: 389
сохранили transport layout, 123 отклонены. Это проверка устойчивости контейнерного
слоя; валидность изменённых H.264/AAC payload ещё не проверяется.

Проверены CRC PAT/PMT, continuity, adaptation fields, PCR/PTS/DTS marker bits,
секции и PES headers через границы TS-пакетов, pointer tails, повтор таблиц,
разделение elementary payload ranges, размеры и количество PES. Ошибка кандидата
оставляет прежнее валидное состояние native-стенда. Проверены границы init 1 MiB,
media 16 MiB, PES 4 MiB, 65536 PES. Полный перечень и SHA-256 — в
[JSON](ProgressiveTsTransportValidation-2026-10-30.json).

Локальные источники ремультиплексированы FFmpeg с `-t 3`. ffprobe независимо
подтвердил все elementary bytes и PTS/DTS:

- AVC: 77 PES и 77 видеопакетов ffprobe;
- AAC без группировки: 141 PES и 141 AAC access unit;
- AAC с группировкой: 18 PES и 141 AAC access unit. Время PES сверяется с первым
  входящим в него кадром, байты каждого ADTS access unit — с ffprobe.

Детерминированные проверки, распределение мутаций и реальные fixture hashes
совпали во всех четырёх прогонах. Первая GCC-компиляция завершилась 1 из-за
опечатки `Data.size()` вместо поля `Data.size`; после исправления все указанные
сборки и тесты завершились 0.

Плееры не пересобирались и не запускались: хеши трёх прежних build EXE,
установленного Linux EXE и Linux INI совпали с доказательством установки SDK 1.38.
Публичные playback/SDK source files также неизменны. Установки Windows и Harvester
не затрагивались. UI/audio, оборудование, Wayland и задачи планировщика не
использовались; временные файлы native-стенда удалены после тестов.

До воспроизведения остаются codec/access-unit validation, конфигурация, frame
end/envelope, межсегментный clock/index, ResourceSource/staging и движки трёх
редакций. Несколько ES в одном TS, HEVC, rollover, live и реальные источники —
отдельные расширения; физическое измерение A/V sync здесь не выполнялось.
