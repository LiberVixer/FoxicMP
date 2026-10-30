# SDK 1.39: сегментный MPEG-TS AVC/AAC

Classic, Windows Qt и Linux Qt используют общий TS → виртуальный fMP4 адаптер.
Он не перекодирует media и не создаёт промежуточный медиафайл: H.264 Annex B
преобразуется в length-prefixed NAL units, с AAC снимается ADTS, а moov/moof/mdat
заголовки создаются в памяти. Декодеры получают данные через прежние MP4 Source
и libmpv callbacks. Все исходные пакеты проходят ResourceSource и SHA-256.
YouTubeHarvester не изменён.

## Negotiation

Проверяйте `schema2Playback.segmentedPlayback.mpegts`:

- `mode: segmented-mpegts-h264-aac-lc`;
- `profiles: ["segmented-mpegts-h264-aac-lc"]`;
- отдельные required `video`/`audio`, обе `containerHint: "mpegts"`;
- `codecHint: "h264"` для видео, `"aac-lc"` для аудио;
- SHA-256 обязателен у всех ресурсов, включая inline;
- для страниц отдельно нужен `catalog.mode: "paged-mpegts-v1"`.

Старые TS transport/index тесты и `segmentProbe` не заменяют эту capability.
`V2PreviewSession` проверяет TS, каталог и наличие integrity до IPC и изменения
своего принятого snapshot. Полную проверку структуры и реальных media выполняет
плеер; admission — `probe` с `sessionDirectory`.

## Ограниченный файловый профиль

Schema 2, VOD, известная duration, timebase 1/10000000, ровно две отдельные
дорожки. Внешний init каждой дорожки содержит PAT/PMT одного program и одного
elementary stream. Media состоят из полных 188-byte TS packets и PES. Init не
содержит media. Первые полные video/audio сегменты нужны для извлечения codec
configuration; до их появления возвращается `waiting_initialization`.

AVC: Baseline/Main/High, 8-bit 4:2:0, progressive, явное fixed-rate VUI,
POC 0/2, AUD и один access unit на PES. Каждый видеосегмент начинается IDR с
неизменными SPS/PPS. AAC: MPEG-4 ADTS LC, 32/44.1/48 kHz, mono/stereo,
без CRC, один raw data block на ADTS frame, целые кадры в PES; один PES может
содержать несколько AAC кадров.

Общий `timeline.timestampOffset` — исходный неотрицательный 33-bit 90 kHz
origin, пересчитанный в 100 ns; это не произвольная поправка PTS. Например,
origin 54000 задаётся строкой `"6000000"`. Из PTS/DTS вычитается этот origin.
Сегментные `start`/`duration` описывают уже относительную timeline, а
`segment.timestampOffset` равен нулю. Первая video presentation начинается
в пределах 0..1 s, первая audio — в пределах −1 s..одного TS tick.

Clock/index проверяет фактическую cadence, а не доверяет метаданным.
Виртуальный MP4 использует непрерывный rational decode clock; AVC presentation
сохраняет исходные относительные PTS. Видео использует clock 100 ns, AAC —
собственную частоту с точной длительностью 1024 samples на кадр. Это необходимо
и для правильного `skip_samples` при отрицательном аудиопреролле. Audio origin
округляется один раз до ближайшего сэмпла. Разница с принятым TS index ограничена
одним 90 kHz tick плюс половиной audio sample; округление ffprobe у AAC внутри
PES учитывается отдельно. Это не измерение физической A/V sync.

Нет muxed A/V, произвольного HLS/DASH, VFR, interlaced, POC1, смены codec/init,
discontinuity, timestamp rebasing, clock rollover или live. Указание `h264`
не отключает проверку байтов. Полные entropy данные проверяет реальный декодер.

Пределы: 1 MiB init, 16 MiB media, 4 MiB PES, 65536 access units на media,
до 2 млн access units и payload ranges в индексе. Каталог: до 128 страниц по
64 KiB, 8192 ресурсов и 4096 сегментов на дорожку; максимум восемь открытых
media/init файлов. Пределы модели не выданы за тест всех максимальных размеров.

## Обновления, ожидание и release

Нужен непрерывный префикс полных, подтверждённых media. Дыра закрывает доступ
к последующим сегментам даже при наличии файлов. До explicit complete хвост
возвращает ожидание, а не EOF. Init и уже принятые fragment bytes виртуального
MP4 неизменяемы при append; backward seek использует существующий MP4 index.

Неверная metadata/page revision до изменения источника оставляет старый stream
читаемым и допускает retry. Ошибка media/integrity/codec после публикации
ResourceSource закрывает сессию до нового open; она не маскируется как EOF.
Отмена сохраняет прежние границы bounded I/O, но не даёт жёсткого системного
deadline. В памяти остаются metadata и по одному последнему собранному payload
на дорожку; исходные ресурсы остаются под reader lifecycle до release.

Не удаляйте опубликованные файлы по вытеснению их из восьмифайлового cache:
главный reader ID удерживает всю сессию. Окончательное разрешение удаления —
`released` после закрытия последних callback/graph владельцев.

## Проверка

[Отчёт трёх редакций и точные exit codes](ProgressiveTsPlaybackValidation-2026-10-30.md).
Проверяются игра до complete, pause/update, backward seek в паузе, ожидание
хвоста, forward seek и natural EOF/release. Нативный адаптер отдельно сверяет
FragmentIndex, byte-prefix immutability, PTS/DTS, video/PCM hashes с FFmpeg.
Реальные YouTube/Rutube/VK, многочасовая приёмка и физическая A/V sync остаются
следующей работой; это ограниченный producer/player contract.
