# R01: подготовка continuous WebM VP9/Opus

Срез 14 ноября 2026, приложение 0.2.0, SDK **1.97 сохраняется**.
[Исходная поддержка video-only](ProgressiveContinuousWebmVp9Superframe.md)
не расширяется на audio. Ниже — результаты diagnostic/reference подготовки
и проект следующего режима, а не объявление нового public playback.
YouTubeHarvester не изменяется.

## Что именно проверяется

[Отчёт](ProgressiveWebmOpusRemuxPreflight-2026-11-14.json) разделяет Linux,
Windows Qt и Windows Classic compiler/native runners. На каждом стенде:

- шесть 24-секундных VP9/Opus fixtures × finite/unknown Clusters;
- VP9 8/10-bit, реальные indexed superframes, Opus mono/stereo 48 kHz;
- packets 20/10/5 ms, обычный и low-delay encoder, короткое audio и один
  presentation sample с audio-first порядком;
- сравнение raw decoded PCM, начального skip, конечного padding, payload SHA,
  clocks и decoded video; native read-only projections полного файла и префикса;
- два CLI MP4 copy references: стандартный и с `-copyts`;
- отказ прежних video-only worker modes от источника с Opus.

Это file/native ResourceIO проверка: ordinary Qt/libmpv или Classic/DirectShow
playback, физический вывод и A/V drift не запускаются. FFmpeg CLI используется
как независимый reference; отличие его сборок не приписывается одной причине.
Source bytes/commands/exits и промежуточные результаты сохраняются в `_bin`;
опубликованный JSON связывает их по SHA. Runtime native registry/session создаются
в private temporary directories, а не на VirtualBox shared filesystem.

## Почему не переносить AAC-профиль в Opus без изменений

[Matroska mapping](https://www.matroska.org/technical/codec_specs.html#a_opus)
задаёт OpusHead в CodecPrivate и число каналов. [Matroska schema](https://raw.githubusercontent.com/ietf-wg-cellar/matroska-specification/master/ebml_matroska.xml)
требует вычесть CodecDelay из времени frame; DiscardPadding задаёт обрезку блока,
а SeekPreRoll — декодирование после discontinuity.
[RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.html#section-4.2) отдельно описывает
начальный pre-skip, конечную обрезку и preroll; последний не заменяет pre-skip.
Это не одинаковые механизмы, и применять их повторно нельзя.

В нашем AAC handshake заложены packets по 1024 samples и AAC-specific skip/end
geometry. Здесь Opus packets имеют другое число samples. Matroska integer-ms
Block clocks и точный codec delay образуют ещё одну отдельную шкалу.
Например, diagnostic source имеет pre-skip 312 samples / CodecDelay 6.5 ms,
а FFmpeg выдаёт первый packet PTS -7 ms. При выводе в MP4 на clock 48000 это
может стать -336 samples; это отличается от точного -312.
[Opus ISO BMFF draft 0.6.8](https://www.opus-codec.org/docs/opus_in_isobmff.html)
описывает dOps и edit list, но наличие dOps само по себе не доказывает точный trim.

В исходном Linux reference стандартный copy-remux сдвинул видео на 7 ms.
`-copyts` сохранил его origin, но обе MP4-копии дали 648 лишних decoded samples
в конце 24-секундного источника. Payload и raw untrimmed PCM сохранились:
проблема относится к trim/clock representation. Матрица в отчёте содержит
результаты других профилей и Windows, без переноса Linux-вывода на другие ОС.

## Отдельный контроль: один audio packet

Один исходный Opus packet с pre-skip312 и terminal discard647 задаёт один
presentation sample из960 decoded samples. В первом прогоне стандартный
FFmpeg decode вернул313 samples на Linux и648 на двух Windows стендах.
Raw decode без автоматического trim вернул960 на всех стендах. Это отдельный
отказ oracle/backend первого-и-последнего packet; он не скрывается обрезкой
фактического decoder output до ожидаемой длины. Финальная матрица хранит
стандартный результат и независимое raw window[312,313) раздельно, вместе
с результатом native projection. Причина не приписывается лишь версии FFmpeg.

В Linux single-packet контроль также показал позднее появление audio bytes:
часть подтверждённого video prefix ещё не содержит ни одного audio packet,
хотя TrackEntry audio идёт первым. Native admission корректно ждёт обе дорожки
и не оставляет reader. Этот prefix фиксируется как OPEN/waiting control,
а не считается успешным воспроизведением звука. Существующий native WebM view
тоже не устраняет single-packet decoder trim сам по себе.

## Выбранный путь следующего режима

Использовать **закрытые finite WebM Clusters** и существующую shared WebM
projection для двух дорожек. Сохранять исходные OpusHead, CodecDelay,
SeekPreRoll, Block timestamps, BlockGroup/DiscardPadding и compressed bytes.
Не переводить audio через AAC window и не исправлять timestamps округлением.

Reference `seal` меняет только size поля Clusters на фактический размер,
сохраняя ширину VINT. Это не готовый managed scanner: для boundaries используется
полная fixture. Prefix proof отдельно записывает подтверждённый следующий
Cluster header. Производственный код должен завершать unknown Cluster только
на структурной границе подтверждённого следующего элемента либо writer complete,
никогда по сигнатуре в payload или одному physical EOF.

Первый предлагаемый профиль — VP9 Profile0/8-bit или Profile2/10-bit, 25 fps,
включая bounded indexed superframes; Opus mono/stereo 48 kHz, mapping0, gain0,
постоянные 20 ms packets, pre-skip312, SeekPreRoll80ms, минимум два audio packets. Результаты 5/10 ms и
short/one-sample — controls для выбора ограничений, а не объявленная capability.

Требуется известное **audio presentation sample count**, закреплённое при start
отдельным mode-scoped параметром. Его нельзя выводить из общей video duration:
контрольные источники содержат короткое audio и один sample. Считать packet count
из Opus TOC, сверять expected encoded count и terminal padding. Так обнаруживается
потеря целого последнего блока, а не только обрыв его bytes. Если точного audio
count нет, этот узкий режим не должен угадывать его по приблизительной duration.
Предлагаемое имя `continuous-webm-vp9-opus-v1` **не объявлено** и SDK constant пока нет.

Сохранять бюджеты: input128MiB, shared cache64MiB, head1MiB, Cluster16MiB,
50000 packets суммарно, bounded depth/counts, подтверждённые ranges/SHA,
один active job, прежние heartbeat/lifetime/update и owner/release правила.
Буферизовать только закрываемый Cluster; partial BlockGroup с terminal padding
не публиковать. После признанного terminal audio блока новые audio packets
должны отвергаться. Исходники не переписывать.

## Оставшаяся реализация

1. Bounded managed EBML sealer двух дорожек: whole-element read, finite/unknown
   Clusters, exact trim/clock admission, expected video/audio counts, fail-closed
   cancel/integrity/unconfirmed EOF и тесты частичных headers/blocks/terminal padding.
2. Отдельный WebM output store/две native projections. Сохранить fMP4/AAC store;
   общее наличие audio отделить от AAC PCM-window. Не подменять format flags.
3. Новый explicit mode/точные capabilities/SDK adapter; обязательные sample count
   и writer ownership входят в immutable start fingerprint.
4. Qt на обеих ОС и Classic: public play/seek/pause/pending cancel/replace/EOF,
   точный PCM и preroll. Отдельно решить first-and-last packet trim и доказать,
   что pre-skip/DiscardPadding применяются ровно один раз. Read-only projection не заменяет этот этап.
5. Обычная приёмка трёх редакций, регрессии, затем Linux update с backup и
   неизменным текущим профилем. До неё не обещать новую функцию Harvester.

R01/R15/R17 и физическая A/V-приёмка остаются открытыми. Wayland, оборудование,
дисковые меню и аналоговое ТВ не возобновляются.
