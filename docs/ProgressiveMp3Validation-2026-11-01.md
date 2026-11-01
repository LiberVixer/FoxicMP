# SDK 1.65: проверка MP3, 1 ноября 2026

[Контракт](ProgressiveMp3.md), [фазы/exit codes/SHA-256](ProgressiveMp3Validation-2026-11-01.json).
Версия приложения — 0.2.0. Harvester не изменён.

Восемь настоящих 20-секундных MP3 profiles на редакцию: mono/stereo × CBR/VBR ×
44100/48000 Hz, без Xing, с ID3v2.3/v2.4 и ID3v1. Начальная длина mono
неизвестна. В каждом профиле проверены initialization wait, buffering,
реальное воспроизведение без видео, pause, seek к 2 s, resume, tail wait,
update/complete, seek к 17 s, natural EOF, release и тот же readerId.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
| --- | --- | --- | --- |
| MP3 runtime profiles | 8 | 8 | 8 |
| Pause / seek / update / tail / EOF / release | PASS | PASS | PASS |
| Реальные decoded codec/rate/channels и одна audio stream | PASS | PASS | Fixture metadata + продвижение штатного graph |
| Фактический file-local 250 ms seek offset | PASS | PASS | Source SeekSample с preroll, runtime отдельно |
| Native MP3 checks | 70 | 70 | Общее ядро, runtime отдельно |
| Прежний ADTS runtime | 4 | 4 | 4 |
| ADTS / native FLAC / FLAC fMP4 / Opus / AAC+H.264 checks | 37 / 55 / 71 / 27 / 23 | 37 / 55 / 71 / 27 / 23 | AAC / Opus / optional Opus / API v1 runtime |
| Python SDK tests | 48 | 48 | Общий клиент |

Native проверка читает всю виртуальную MP4-проекцию через decoder API.
SHA-256 каждого MP3 frame совпадает с соответствующим frame исходника,
presentation clocks — с native MP3 packet timebase. Duration сверяется по
независимому decoded sample count. Полученный PCM равен по длине и SHA-256
PCM из native MP3. Это lossy codec: сигнал до MP3 encoding не является
ожидаемым PCM-результатом. Оригинал вместе с ID3 не изменён.

Декодер дополнительно перезапускается с целого frame перед целью 2 s и
объявленным 250 ms preroll; последующие 0.5 s PCM в конце fixture сравниваются
с полным линейным decode. Это проверка восстановления последующего звука,
не аттестация физического выхода в самый момент seek. Qt во время playback
отдельно проверяет `options/hr-seek-demuxer-offset=0.25`.
Classic rate/channels в writer report описывают fixture, не независимо
измеренный output DirectShow media type.

Проверяются неподходящие MPEG version/layer/protection/rate/bitrate/emphasis,
free-format, смена channels, initial/history reservoir underflow, Xing/Info/VBRI,
неверные ID3 version/flags/synchsafe size/footer и превышение tag limit,
усечённый complete/trailing bytes, required/kind/codec/offset/duration/SHA,
ожидание неполного ID3/header/frame и освобождение последнего владельца.
V2.4 footer проверен и в положительном сценарии.

70 MP3 + 37 ADTS checks пройдены под ASan/UBSan с leak detection, в полном
production RawAudioBridge, MP3/ADTS parsers и ResourceSource. Обе ОС прошли
по 283 native checks. Предыдущие capabilities SDK 1.64 не изменены после
исключения трёх явно добавленных MP3 capability/profile/container элементов.

Linux UI/audio использует частные X11/D-Bus/PulseAudio. Classic запускается
из отдельной копии с ContextMenuPrompted=1, ContextMenuEnabled=0. Проверены
158 HKCU shell entries (команды/иконки) и хеши установленных Windows EXE/INI:
они не менялись. Одноразовая task и staging удалены. Linux EXE обновлён
атомарно с backup, текущий INI и остальные файлы сохранены.

Ограничения профиля сохраняются: точная encoded-sample duration, MPEG-1
Layer III без CRC, 44.1/48 kHz; без Xing/Info/VBRI/gapless trim, free-format,
MPEG-2/2.5, Layers I/II, APE/внутренних tags и live. Реальное подключение
Harvester и физическая приёмка остаются в общем чеклисте.
