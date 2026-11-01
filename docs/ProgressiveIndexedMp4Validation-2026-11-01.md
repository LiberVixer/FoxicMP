# SDK 1.54: проверка обычного растущего fast-start MP4

Контракт: [ProgressiveIndexedMp4.md](ProgressiveIndexedMp4.md).
[Машинный отчёт](ProgressiveIndexedMp4Validation-2026-11-01.json) содержит
точные exit codes, исходные SHA-256, hashes сборок и runtime phases.
Harvester не менялся. Общий FFmpeg remux остальных контейнеров не объявляется
готовым на основании этого native-progressive профиля.

## Результаты

| Срез | Linux Qt | Windows Qt | Windows Classic |
|---|---|---|---|
| Реальное воспроизведение | 3 × 20 s источника | 3 × 20 s источника | 3 × 20 s источника |
| Профили | Main/B-frames, High 30000/1001, VFR | Те же | Те же |
| До complete | Playing, pause, seek, buffering, append/resume | Те же | Те же |
| Reader identity | Стабильна | Стабильна | Стабильна |
| После complete | Tail seek, естественный EOF, actual release | Те же | Те же |
| Проверка декодирования | Меняющиеся framebuffer hashes, H.264 pixels, AAC rate/channels | Те же | Native graph running/paused/position, подтверждённые decoder reads |
| Exit code | 0 | 0 | 0 |

Нативный общий слой отдельно проверен на Linux и Windows: 36 checks,
пять fixtures — 4 s Main/B-frames, fractional High, без B-frames, VFR и
80 s Main/B-frames (2000 видео- и 3751 аудиопакет). Проверены byte-identical
compressed packets, точные PTS/DTS, полное совпадение декодированных видео и PCM
с исходником, backward reads и неизменяемый virtual prefix при append.
Это 80-секундная проверка **проекции/декодирования fixtures**, а не утверждение
о 80-секундном UI-просмотре на каждой платформе.

`ffprobe.packet.duration` может отражать nominal codec frame size в fragmented
AAC/VFR. Критерий здесь — исходные PTS/DTS, правильная presentation boundary,
полная последовательность декодированных кадров и PCM. Физический A/V drift
по этим программным признакам не измеряется.

## Проверки отказа и времени

Неполные head headers/moov возвращают waiting_initialization без оставшихся
читателей. Неизвестная длина, moov в конце, holes в ranges, внешние dref,
несовпадающий реальный codec, нулевая movie timescale, неверные sample/time/chunk
таблицы, overlap дорожек и non-unit edit rate отклоняются. Отмена, SHA rewrite
и truncate закрывают обе проекции; старые ресурсы сохраняются до release.
Успешные тесты проверяют неизменность оригинальных байтов и единственный
физический файл без промежуточных медиа.

На VFR обнаружена и исправлена ошибка предположения sum(stts)=mdhd.duration.
Граница media edit относится к presentation clock. При обрезке выходящего
sample duration следующий projected fragment получает исходный tfdt, чтобы
не сдвинуть последующие DTS. VFR включён и в native decoded-equality, и в
runtime-проверки всех трёх редакций.

Windows FFmpeg терял terminal discard для fragmented AAC: получалось
192512 PCM-сэмплов вместо 192000 при одинаковых compressed packets. Qt audio
получает обычную MP4-проекцию с полным sample index, сохраняя trim. Теперь
сравниваются точные PCM hashes и payloads внутреннего Classic packet reader.

Linux CLI нового indexed bridge дополнительно собран с AddressSanitizer и
UndefinedBehaviorSanitizer; все 36 checks, exit 0. SDK helper: 37 unit tests
на Linux и Windows, exit 0. Qt-регрессии: по 6 CTest groups (новый native/API,
прежние shared fMP4 и WebM/Vorbis, RAM preview 1.53 и два shared fMP4/Opus
playback-профиля). Classic: дополнительно 4 прежних MP4 compatibility profiles
и v1 sessionDirectory contract, exit 0.

## Изоляция и установка

Linux UI/audio: `qt/tests/run_tests.sh`, отдельные X11/D-Bus/PulseAudio.
Windows: частные профили и application mute, без изменения системного микшера.
Classic до первого старта получил ContextMenuPrompted=1, ContextMenuEnabled=0;
установленная HKCU shell registration сверена до/после каждого запуска.
Одноразовая задача и staged-копия удалены после завершения. SHA-256 tested/built
Classic совпадают. Установленные Windows EXE/INI не заменялись.

Проверенный Linux EXE заменён атомарно с backup SDK 1.53. INI, worker,
integration и остальные прежние файлы сохранены с проверкой SHA-256;
процессы просмотра не останавливались. Version/capability установленного EXE
сверены через отдельный временный профиль. SDK 1.54 поставляется отдельным ZIP.

## Воспроизведение проверок

```sh
qt/tests/run_tests.sh /absolute/build /absolute/fixtures \
  -R '^progressive-indexed-mp4-(native|playback)$' -V
python3 tests/progressive_v2_sdk_test.py
```

Windows Classic запускается отдельной копией с изолированным INI:
`tests/windows/progressive_family_test.py --indexed-mp4-only --player ... --fixtures ... --report ...`.
Не запускать этот тест на пользовательской установке.

Ограничения профиля, codec/container coverage, moov-at-tail, sparse media,
video/audio-only, общий непрерывный remux, многочасовая/физическая A/V-приёмка
и реальный Harvester остаются в [чеклисте](ProgressiveCompletionChecklist.md).
