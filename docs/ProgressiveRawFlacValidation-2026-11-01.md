# SDK 1.63: проверка native FLAC, 1 ноября 2026

Контракт — [ProgressiveRawFlac.md](ProgressiveRawFlac.md), данные фаз, точные
exit codes, SHA-256 сборок/исходников и установки — [JSON](ProgressiveRawFlacValidation-2026-11-01.json).
Harvester не изменён. Версия приложения остаётся 0.2.0.

Каждая редакция проверяется на восьми настоящих 20-секундных FLAC-файлах:
mono/stereo × 16/24 bit × 44100/48000 Hz. Начальная длина mono неизвестна.
Writer публикует маленький prefix, затем 4 и 12 секунд и complete. Проверяются
ожидание initialization, буферизация, реальное декодирование одного audio
без видео, pause, seek к 2 секундам, ожидание tail, seek к 17 после update,
natural EOF и release с тем же readerId. Исходный файл не меняется.

| Проверка | Linux Qt | Windows Qt | Windows Classic |
| --- | --- | --- | --- |
| Новые raw FLAC runtime profiles | 8 | 8 | 8 |
| Реальные decoded codec/rate/channels и одна audio stream | PASS | PASS | Профиль fixture + продвижение playback graph |
| Pause / seek / update / ожидание / EOF / release | PASS | PASS | PASS |
| Native RawFlacBridge / ResourceSource conformance | 55 | 55 | Общее ядро, runtime отдельно |
| Прежний FLAC fMP4 runtime | 8 | 8 | Сохранён прошлый срез 1.62 |
| Native FLAC fMP4 / Opus / AAC+H.264 regression checks | 71 / 27 / 23 | 71 / 27 / 23 | AAC / Opus / optional Opus / v1 runtime |
| Python SDK tests | 46 | 46 | Общий клиент |

Qt отдельно опрашивает codec/rate/channels и stream count у libmpv во время
воспроизведения. Classic проверяет состояние и продвижение штатного playback
graph; rate/channels в его writer report описывают fixture, а не отдельное
измерение выходного DirectShow audio format.

Native тесты читают всю виртуальную fMP4-проекцию через decoder read API.
Сжатые packets сравниваются с исходным FLAC по SHA-256 и presentation clocks;
длительности сверяются по независимому decoded `nb_samples`. Полученный PCM
сравнивается с отдельным целочисленным сигналом до кодирования: совпадают длина
и SHA-256 во всех восьми профилях. Для 24 bit используется s32le с 24 значащими
битами. Дополнительно проверяются небольшие независимо построенные constant,
verbatim, fixed и LPC frames с независимым декодированием.

Ошибочные marker/metadata/block bounds, variable strategy, header/frame CRC,
отрицательный LPC shift, channels/rate/depth вне профиля, отсутствующий SHA,
неверные kind/codec/offset/duration, optional track, metadata сверх лимита и
усечённый complete отклоняются без утечки reader. Последний неполный frame
ждёт дописывания и только после этого появляется в индексе. Все 55 native
сценариев также пройдены под AddressSanitizer/UndefinedBehaviorSanitizer,
включая leak detection; scope — полный production bridge и ResourceSource.

Первый conformance harness предполагал наличие packet duration в ffprobe MP4
и завершился с exit 1. Исправлен источник длительности: decoded frame samples
и настоящие packet timebases. Первый набор из 46 проверок и первые runtime
прогоны прошли; после дополнительной проверки LPC shift сборки пересобраны,
55 native проверок и все восемь runtime profiles каждой редакции повторены.
В финальном Windows Qt прогоне отдельно подтверждены пять legacy CTest groups
и три новых raw FLAC groups: ошибочный первоначальный regex не засчитан за
выполнение новых тестов.

Linux UI/audio работают в отдельных X11/D-Bus/PulseAudio. Classic запускается
из частной копии с `ContextMenuPrompted=1`, `ContextMenuEnabled=0`; проверены
команды и иконки 158 HKCU shell entries до/после. Обе одноразовые задачи и
частные Classic staging folders удалены после завершения. Установленные
Windows EXE/INI не менялись. Linux EXE заменён атомарно, прежний сохранён;
INI и остальные личные файлы проверены по хешам.

Результат не является измерением физического аудиовыхода. Другие audio
containers, variable blocks, multichannel, прочие rates/depths, live,
реальный Harvester и оставшаяся приёмка ТЗ остаются в общем чеклисте.
