# SDK 1.64: проверка ADTS AAC-LC, 1 ноября 2026

[Контракт](ProgressiveAdts.md), [фазы, exit codes и SHA-256](ProgressiveAdtsValidation-2026-11-01.json).
Версия приложения остаётся 0.2.0. Harvester не изменён.

Четыре настоящих 20-секундных ADTS AAC-LC fixtures на редакцию:
mono/stereo × 44100/48000 Hz, неизвестная начальная длина mono. Writer
публикует маленький prefix, 4 и 12 секунд, затем complete. Проверены ожидание
первого frame, начальная буферизация, playback без видео, pause, seek к 2 s,
resume, ожидание tail, seek к 17 s после update, natural EOF и release с
тем же readerId. Оригинал сохраняется без изменения.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
| --- | --- | --- | --- |
| ADTS runtime profiles | 4 | 4 | 4 |
| Pause / seek / update / tail wait / EOF / release | PASS | PASS | PASS |
| Фактические codec/rate/channels и одна decoded audio stream | PASS | PASS | Fixture metadata + продвижение playback graph |
| Native ADTS checks | 37 | 37 | Общее ядро, runtime отдельно |
| Native FLAC / FLAC fMP4 / Opus / AAC+H.264 regression checks | 55 / 71 / 27 / 23 | 55 / 71 / 27 / 23 | AAC / Opus / optional Opus / v1 runtime |
| FLAC runtime после выделения общего scalar bridge/MP4 projection | 8 | 8 | 8 |
| Python SDK tests | 47 | 47 | Общий клиент |

Qt запрашивает у libmpv реальные codec/rate/channels и track count во время
проигрывания. Classic проверяет состояние и продвижение штатного graph;
rate/channels в его writer report взяты из fixture и не являются отдельным
измерением DirectShow output format. Физический аудиовыход не аттестован.

Все packets виртуального MP4 прочитаны через decoder read API. Их SHA-256
совпадает с AAC payload исходного ADTS после исключения transport header.
Presentation clocks сверены с native ADTS packet timebase; длительности —
с независимыми decoded frame sample counts. PCM из проекции совпадает по
длине и SHA-256 с PCM, декодированным из native ADTS. Для lossy AAC это
сравнение до/после проекции, а не заявление совпадения с PCM до кодирования.

Неполные header/payload ждут update; первый незавершённый frame не оставляет
reader. Проверяются неподходящие sync/MPEG ID/layer/CRC/profile/rate/channels,
несколько raw blocks, смена валидной configuration, нулевая длина frame,
усечённый complete, trailing data/ID3, optional/kind/codec/offset/duration,
отсутствующий SHA, подтверждённый EOF и release последнего владельца.

Под ASan/UBSan с leak detection пройдены 37 ADTS и 55 FLAC checks. Scope —
полный production RawAudioBridge, общая MP4 Projection, parsers и ResourceSource.
Все предыдущие capabilities SDK 1.63 сохранены побайтно по JSON-значениям после
исключения явно добавленных ADTS capability/profile/container.

В первом прототипе исправлены ошибка компиляции dynamic RapidJSON member name
и предположение test harness о наличии MP4 packet duration в ffprobe.
Окончательные сборки и tests имеют exit 0; исправленный harness использует
реальные decoded sample counts и настоящие timebases.

Linux UI/audio изолированы в частных X11/D-Bus/PulseAudio. Classic — частная
копия с ContextMenuPrompted=1 и ContextMenuEnabled=0. После тестов проверены
158 HKCU shell entries (включая команды и иконки), хеши установленных Windows
EXE/INI; они не менялись. Одноразовая Classic task и staging удалены.
Linux EXE обновлён атомарно с backup; текущий INI и другие файлы сохранены.

Duration в этом профиле — точное время закодированных samples, не округлённые
секунды сервиса. CRC-protected/MPEG-2 ADTS, HE-AAC/SBR/PS, PCE, ID3/APE,
configuration changes, остальные layouts/rates, live и фактическое подключение
Harvester остаются в общем чеклисте. Полное ТЗ этим этапом не закрывается.
