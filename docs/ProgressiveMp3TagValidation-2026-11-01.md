# SDK 1.67: LAME metadata/CRC, 1 ноября 2026

[Контракт](ProgressiveMp3Tag.md) · [exit codes, PCM/status/SHA-256](ProgressiveMp3TagValidation-2026-11-01.json).
Приложение 0.2.0. Harvester не изменён. Полный gapless playback не реализован.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
| --- | --- | --- | --- |
| Сборка | GCC, PASS | MinGW, PASS | MSVC, PASS |
| Новые native CRC/trim-plan checks | 72 | 72 | MSVC scalar parser: 72, ResourceSource runner MinGW |
| Прежний indexed MP3 native | 94 | 94 | Общее ядро, runtime отдельно |
| Прежний plain MP3 native | 70 | 70 | Общее ядро, runtime отдельно |
| Growing Xing/Info/VBRI runtime profiles | 12 | 12 | 12 |
| Legacy runtime | Plain native suite | Plain native suite | Plain MP3 ×2, ADTS/AAC/Opus, optional Opus, API v1 |
| Python SDK tests | 49 | 49 | Общий клиент |

72 новых checks включают 16 четырёхсекундных profiles:

- 12 прежних Xing/Info/VBRI: mono/stereo × 44100/48000 Hz;
- четыре **настоящих libmp3lame** profiles, вызванных через C API: encoder
  сам генерирует окончательный Info tag, без создания валидного CRC тестом.

Каждый профиль проходит независимый table-based CRC oracle и production
status, growing→complete с неизменным планом, reader release и сохранность
оригинала. Для восьми profiles с корректным tag дополнительно проверяется
точное равенство PCM окна полному native MP3 decode, число samples и ticks.
Это scalar bridge/metadata-проверка, не обрезанный playback Classic.

Результаты tags: FFmpeg stereo — `valid`; проверенные FFmpeg mono —
`crc-mismatch`; настоящие libmp3lame mono/stereo — `valid`; VBRI — `absent`.
В FFmpeg mono проблема записанной CRC не превращается в отказ encoded
воспроизведения. В runtime всех редакций точные status states проверены
относительно независимого oracle для каждого фактического файла.

Дополнительные случаи: одиночное повреждение delay/padding, Music CRC,
tag CRC и quality; неизвестные identifier/revision; недостаточный padding;
недоказанные decoder delays и sample rate; bytes за пределами CRC coverage;
неполный полный tag; корректный нулевой checksum; повторный parse после
валидного tag без сохранения старого `verified`. Повреждённый optional tag
не ломает прежний encoded профиль, но всегда даёт `trimPlan.ready=false`.

На Linux ASan/UBSan с leak detection прошли те же 72 проверки через production
bridge/ResourceSource и scalar parser. Linux native всего — 236 проверок,
Windows native — 236. Отдельная сборка scalar parser MSVC проходит 72;
её transport/resource runner — прежний MinGW binary, что явно отделено
от фактической сборки и runtime Classic.

Runtime: initialization/buffer, просмотр до complete, pause, seek к 2 s,
resume, tail wait, update/complete, seek к 17 s, natural EOF/release с тем
же readerId; `gaplessTrimApplied=false` и encoded duration сохраняются.
Все 12 profiles каждой редакции проверяют новые CRC/status/trim diagnostics.
Qt проверяет фактические codec/rate/channels/stream count. Classic использует
fixture metadata и продвижение graph; точный DirectShow PCM не измеряется.

Linux UI/audio изолированы частными X11/D-Bus/PulseAudio. В отдельной копии
Classic до первого запуска записаны `ContextMenuPrompted=1`,
`ContextMenuEnabled=0`. После тестов удалены one-shot task и staging;
установленные Windows EXE/INI и 158 HKCU shell entries не изменились.
Linux EXE обновлён атомарно с backup и проверкой текущего профиля/личных файлов.

Старые capabilities SDK 1.66 совпали после удаления ровно одного нового
`mp3Indexed.encoderTagValidation`. Gapless-контракт не добавлен. В SDK 1.67
включены также ранее проверенные PCM reference/Qt adapter документы.

Открыты: progressive controller с audible clock/chapters/seek/available;
точный PCM/output adapter Classic; growing gapless playback; полный Music
CRC (нынешний header checksum его не заменяет); расширенная матрица decoder
profiles и реальное подключение Harvester. Физические/Wayland проверки не
возобновлялись. Промежуточный SDK не закрывает всё progressive-ТЗ.
