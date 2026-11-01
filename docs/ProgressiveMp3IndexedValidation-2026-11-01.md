# SDK 1.66: проверка индексных MP3, 1 ноября 2026

[Контракт](ProgressiveMp3Indexed.md), [фазы/exit codes/SHA-256](ProgressiveMp3IndexedValidation-2026-11-01.json).
Версия приложения — 0.2.0; Harvester не изменён. Gapless trim не применяется.

Новые 12 настоящих 20-секундных audio profiles на редакцию:
Xing/Info/VBRI × mono/stereo × 44100/48000 Hz. Xing/Info получены FFmpeg/LAME;
VBRI TOC независимо собрана поверх настоящих MP3 audio frames. Это проверка
поддерживаемого VBRI layout, а не реального корпуса Fraunhofer encoder.
Initial length mono неизвестна; есть начальные ID3v2 и конечный ID3v1.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
| --- | --- | --- | --- |
| Новые Xing/Info/VBRI runtime profiles | 12 | 12 | 12 |
| Pause / seek / update / tail / complete / EOF / release | PASS | PASS | PASS |
| Codec/rate/channels, одна decoded audio stream | PASS | PASS | Fixture metadata + продвижение graph |
| File-local 250 ms preroll во время playback | PASS | PASS | Штатный MP3 SeekSample preroll + runtime |
| Прежний MP3 runtime | 8 | 8 | Mono CBR 44.1 + stereo VBR 48 |
| Новые native checks | 94 | 94 | Общее ядро, runtime отдельно |
| Native всего с прежними MP3/ADTS/FLAC/Opus/AAC+H.264 | 377 | 377 | Legacy AAC/Opus/ADTS, optional Opus и API v1 runtime |
| Python SDK tests | 49 | 49 | Общий клиент |

Во всех runtime profiles проверены initialization wait, buffer, playback до
complete, pause/seek к 2 s, resume, tail wait, update/complete, seek к 17 s,
natural EOF, release, сохранённый readerId и неизменный оригинал. Qt отдельно
опрашивает реальные codec/rate/channels/stream count и seek option. Classic
rate/channels в writer report взяты из fixture, не являются независимым
измерением выходного DirectShow format.

Полная virtual MP4 projection прочитана через decoder API. SHA-256 каждого
настоящего audio frame совпал с исходником; первый non-audio index frame
не передаётся decoder. Packet clocks и decoded sample durations сверены
независимо. PCM совпал по длине и SHA-256 с декодированием полного encoded
span без gapless metadata. Последующий PCM tail после decoder restart с
preroll также совпал. Для lossy MP3 это не сравнение с сигналом до encoding
и **не** обещание совпадения с native gapless-обрезанным output.

Header type/counters, raw encoder delay/padding, encoded timeline и
`gaplessTrimApplied=false` проверяются в status. Отрицательные проверки:
kind/codec/required/SHA/duration, неизвестные Xing flags, отсутствующие/нулевые
counters, decreasing TOC, ненулевая side information index frame,
неизвестная signature, final frame/byte count mismatch, invalid VBRI
version/count/scale/width/step/TOC coverage. Неполный index и первый audio
frame ждут update. Growing overrun fails closed, decoder read возвращает
ошибку и reader освобождается без ложного EOF.

ASan/UBSan с leak detection прошли 94 новые + 70 прежних MP3 checks в полном
production bridge/parser/ResourceSource. Предыдущие capabilities SDK 1.65
сохранены после исключения трёх явно добавленных indexed-MP3 элементов.
SDK тестирует отказ от неверного timeline или обещания gapless в этом mode.

Эксперимент gapless: native PCM 705600 bytes, полный encoded PCM 714240 bytes;
простая MP4 edit list оставила 714240 bytes. Явная sample-выборка совпала с
native PCM. Это зафиксированная граница движка: экспериментальный edit MP4
не включён в production, полная gapless-функция не объявляется готовой.

Linux UI/audio изолированы частными X11/D-Bus/PulseAudio. Classic использует
частную копию с ContextMenuPrompted=1, ContextMenuEnabled=0. Хеши установленных
Windows EXE/INI и 158 HKCU shell entries с командами/иконками проверены и не
изменились. Тестовая one-shot task и staging удалены. Linux EXE обновлён
атомарно с backup и проверкой сохранности текущего INI/других файлов.

Открыты: gapless adapters и точный output sample count Qt/Classic при
start/end/seek; LAME tag CRC; реальный VBRI corpus; остальные MPEG profiles,
CRC/free-format/live и действительное подключение Harvester. Физическая
аудиоприёмка остаётся отдельным пунктом общего чеклиста.
