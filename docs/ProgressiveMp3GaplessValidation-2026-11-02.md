# Проверка progressive MP3 gapless — 2 ноября 2026

Результат текущего среза: **PASS**. SDK 1.68, приложение 0.2.0.
[Контракт](ProgressiveMp3Gapless.md), [машинный отчёт](ProgressiveMp3GaplessValidation-2026-11-02.json).

| Проверка | Linux Qt | Windows Qt | Windows Classic |
|---|---|---|---|
| Новый native профиль | 31 checks, exit 0 | 31 checks, exit 0 | Default backend отказывает |
| Прежние MP3/indexed/tag, ADTS, raw FLAC | 328 checks, exit 0 | 328 checks, exit 0 | Четыре runtime regression profiles, exit 0 |
| Растущий gapless MP3 | Четыре profiles, exit 0 | Четыре profiles, exit 0 | `probe` unsupported / `start` error, без reader |
| Цифровой PCM через настоящий Window/Player | 16 окон, exit 0 | 16 окон, exit 0 | Output adapter остаётся открытым |
| SDK | 16 + 50 tests, exit 0 | 16 + 50 tests, exit 0 | Отказ согласован с capabilities |

31 native check повторён под ASan/UBSan, включая полный production bridge,
ResourceSource и reader registry; exit 0. Leak detection и halt-on-error включены.

Четыре профиля — mono/stereo × 44,1/48 kHz, настоящий libmp3lame 3.100,
валидные encoder tags. Progressive-цикл проверяет буфер 4/12 секунд, реальное
воспроизведение до complete, паузу, seek при паузе, resume, ожидание хвоста,
seek к 17-й секунде и натуральный EOF с release. Полезная длительность —
20 секунд, главы применены на позициях 0/2/17 без encoder offset.

В отдельном реальном controller-тесте четырёхсекундный PCM сравнивается
побайтово по длине и SHA-256 с независимым FFmpeg decode исходного MP3:
старт с sample 0, sample 1, `2*rate+1` и последнего sample. Частный PCM-фильтр
действительно активен. Полный prefix без writer complete не завершает сессию;
complete открывает удержанный хвост. Это проверка цифрового PCM до физического
устройства, без утверждений о системном микшере или A/V drift.

Native negatives покрывают неверный tag CRC, неизвестный encoder, coded duration
в audible-профиле, ненулевой offset, optional track, mono FFmpeg tag с плохим
CRC и VBRI. Default backend отвергает профиль до открытия reader. Прежние
encoded profiles сохранены. На обеих Qt-платформах повторён runtime Info MP3.
Classic отдельно воспроизводит indexed Info MP3, plain MP3, ADTS и raw FLAC.

Linux UI/audio использует частные X11/D-Bus/PulseAudio через `run_tests.sh`.
Windows проверена отдельно, в тестовых копиях. У Classic prompt отключён
в INI до первого запуска; 158 shell entries совпали до/после. Одноразовая
тестовая задача и staging удалены. Установленные Windows EXE/INI не менялись.

Linux EXE заменён атомарно после проверок, прежний сохранён для отката.
INI совпал по SHA-256, 231 сопутствующий файл сохранён, demux worker прежний.
Прежние общие capabilities SDK 1.67 совпадают целиком; Qt объявляет отдельное
дополнение `mp3Gapless`. SDK manifest разделяет общий и Qt-контракт.

В отчёте сохранены исходные неуспешные попытки: ошибка каталога negative fixture,
тип RapidJSON для MSVC, неверное ожидание исключения от `probe`, остаток fixture
при повторе и погрешность float в прежнем deadline assertion Windows. Они
исправлены; финальные коды процессов — 0. Незавершённый ранний прогон не
использован как доказательство успеха.

Открыты Classic PCM/output adapter, другие MPEG profiles, VBRI gapless,
реальный корпус источников и остальные критерии [общего чеклиста](ProgressiveCompletionChecklist.md).
Harvester, Wayland и физические устройства не менялись и не проверялись.
