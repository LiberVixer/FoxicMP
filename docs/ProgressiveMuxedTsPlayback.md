# SDK 1.40: совместные MPEG-TS сегменты H.264/AAC

Каждый media-ресурс этого профиля содержит и видео, и звук. Classic, Windows Qt
и Linux Qt получают две логические дорожки из одного TS-файла через общий
виртуальный MP4-адаптер. Перекодирования, двух копий TS или промежуточного
медиафайла на диске нет. Harvester не изменялся.

Это профиль полных последовательных TS-сегментов. Один растущий файл с
незавершённым PES, muxed fMP4/WebM, произвольный HLS/DASH и live остаются
отдельными задачами. Capability явно содержит `segmentsOnly: true` и
`growingSingleResource: false`.

## Negotiation и модель

Проверяйте `schema2Playback.segmentedPlayback.mpegts.muxed`:

- `mode: "segmented-muxed-mpegts-h264-aac-lc"`;
- `containerHint: "mpegts-muxed"`;
- для страниц — `catalog.mode: "paged-muxed-mpegts-v1"`;
- SHA-256 обязателен для всех ресурсов, включая inline;
- прежняя capability раздельных TS-дорожек не заменяет эту.

Schema 2 сохраняет два required описания `video` и `audio`. В обоих указан
`containerHint: "mpegts-muxed"`, одинаковый `initializationResourceId` и
одинаковая последовательность media resource IDs/sequence. Сам resource
описывается в `resources` один раз. Например, обе логические дорожки ссылаются
на `mux-init`, затем `mux-0`, `mux-1` и т. д. Кодеки — `h264` и `aac-lc`.

У каждого логического представления свои `start`/`duration`: границы video
и AAC в одном TS-файле могут различаться. Они должны совпадать с реальными
access units своей дорожки; при переходе между файлами сохраняется её cadence.
Readiness/state и число сегментов обоих представлений должны совпадать.
Частичное совместное использование init/media, отдельные файлы при muxed hint
или смешение с hint `mpegts` отклоняются.

В странице каталога media resource также описывается один раз, а оба
`page.tracks[].segments` ссылаются на него. Повтор ресурса внутри одной
логической дорожки, повторное архивирование в другой странице и sharing для
прежних fMP4/WebM/раздельных TS-профилей по-прежнему запрещены.

`V2PreviewSession` проверяет новую capability, отдельную capability каталога,
контейнеры/кодеки и наличие integrity до IPC и изменения своего snapshot.
Полная проверка общих списков, страниц и media выполняется плеером; admission
проверяется через `probe` с `sessionDirectory`.

## Transport и codecs

Внешний init содержит одну PAT и одну PMT: один program, ровно два разных ES
PID — AVC `0x1b` и AAC ADTS `0x0f`. PCR принадлежит AVC PID. Порядок ES в PMT
не важен. Раздельный профиль SDK 1.39 по-прежнему требует один ES в каждом init;
разрешение двух PID включается только для нового профиля.

Media состоят из полных 188-byte packets, содержат PCR и полные PES обеих
дорожек. Допускается произвольное чередование их TS-пакетов, в том числе внутри
одного video PES. AAC может встретиться до первого PCR этого сегмента, но PCR
обязательно присутствует и проверяется. PAT/PMT неизменны; continuity каждого
объявленного PID, PSI CRC, PTS/DTS и PES framing проверяются. Третий ES PID,
другая программа, scrambling и смена конфигурации запрещены. Этот bounded
профиль содержит PAT/PMT, объявленные ES и null PID; SDT/NIT и другие таблицы
не объявляются поддержанными. Тестовый producer исключает SDT из своих fixtures.

Сохраняются [codec/clock ограничения SDK 1.39](ProgressiveTsPlayback.md):
fixed-rate progressive AVC Baseline/Main/High 8-bit 4:2:0, AUD, IDR/SPS/PPS
в начале каждого video-сегмента; AAC-LC ADTS mono/stereo 32/44.1/48 kHz.
Общий origin задаётся `timeline.timestampOffset` как точный пересчёт 90 kHz
clock в 100 ns, а segment offset остаётся нулевым. Удобный origin — первый AAC
PTS: тогда video DTS может быть отрицательным, а video presentation сохраняет
исходный lead. Преролл обрабатывает уже проверенный MP4 edit/sample clock.

Для публикации проверяются обе дорожки до выдачи кандидата декодерам. Валидное
видео не позволяет принять сегмент с повреждённым AAC. Ошибка metadata/page до
изменения ResourceSource оставляет прежний читаемый префикс; ошибка bytes/codec
после публикации источника закрывает обе дорожки до нового open.

## Чтение и владение

Один ResourceSource владеет общими физическими файлами. Восемь media/init handles
— общий лимит, не восемь на каждую логическую дорожку. Индексы хранят разные
payload ranges внутри одного виртуального TS byte layout. В decoder data не
попадают заголовки TS или payload другой дорожки; каждый gather проходит SHA.
Окно чтения — до 64 KiB, PES — до 4 MiB, media — до 16 MiB, init — до 1 MiB.

Граф Classic получает разные виртуальные имена video/audio, чтобы выбрать
правильный logical reader. Файлы с этими именами не создаются. Общий file ID
допускается в Qt только после admission нового muxed-профиля; защита от alias
в старых режимах сохраняется. Главный reader содержит два разных logical IDs
и удерживает общую сессию до закрытия последних владельцев/callbacks.

Дыра или неподтверждённый ресурс останавливает обе дорожки на общем byte prefix.
Файл не удаляется после закрытия только audio или video: нужно подтверждение
`released` главного reader ID. EOF требует явного complete, а не наличия
следующего файла на диске.

## Проверка и границы

[Отчёт трёх редакций](ProgressiveMuxedTsValidation-2026-10-31.md). Нативные
проверки включают interleaved PES, обратный порядок PMT entries, audio перед
PCR, отсутствие третьего PID, повреждение только AAC, atomic publication,
SHA/leases и каталог из 97 ресурсов. Реальные fixtures отдельно сверяются с
FFmpeg по декодированным video/PCM hashes и PTS/DTS, включая 80-секундный
каталог из 81 физического файла. Плееры проверяются отдельно на обеих ОС.

Полные пределы модели (8192 ресурса, 4096 сегментов на track, 2 млн access units)
не являются результатами stress-теста. Реальные сайты, многочасовые записи,
физическая A/V sync, live/config changes и один незавершённый растущий файл
не проверены и не объявляются готовыми.
