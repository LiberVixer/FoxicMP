# R01: инвентаризация непрерывного remux

Срез 12 ноября 2026, исходная база `6cbd2e9ab`, SDK 1.87.
Это подготовка расширения, а не новый playback-профиль или выпуск.
Позднейшая реализация выбранного HEVC-профиля описана в
[SDK 1.88](ProgressiveContinuousHevc.md). Таблица ниже сохраняет исходный срез
до реализации и причины выбора; старые «нет/не реализован» относятся к этому срезу.
Обычные MP4/HEVC/AV1/VP9 позднее расширены до
[VP9 indexed superframes + AAC-LC SDK 1.95](ProgressiveContinuousVp9SuperframeAac.md);
WebM и прочие container rows ниже остаются отдельными этапами.
[Полный порядок работ](ProgressiveRemainingWork.md),
[ТЗ](ProgressiveTechnicalSpec-2026-10-25.ru.md),
[подробный чеклист](ProgressiveCompletionChecklist.md).
YouTubeHarvester и установленные профили не меняются.

## Как читать таблицу

**Native** — уже проверенный ограниченный progressive-путь с собственным
индексом/проекцией. **Probe** — только диагностика через managed AVIO, максимум
128 пакетов/16 MiB/4 s; подсказка demuxer не обещает поддержку всех его файлов.
**Clip** — ограниченная RAM-подготовка MP4, до 128 пакетов/16 MiB; отдельный
prepared-preview не является непрерывным просмотром всего источника.
**Continuous** — public `continuousRemuxStart` → preparation → явный play commit,
один worker, append-only fMP4 cache и decoder adapter.

Здесь сравниваются недокачанные источники через API. Обычное открытие завершённого
MKV/MP4/MOV штатным плеером не доказывает progressive-поддержку.
Все записи Native относятся только к точным capabilities и условиям документов
в соответствующей строке: это не декартово произведение кодеков и контейнеров.

## Контейнеры и layouts

| Вход | Уже Native в трёх редакциях | Probe / Clip | Уже public Continuous | Что остаётся |
| --- | --- | --- | --- | --- |
| Обычный MP4, head moov, H.264 video-only | Не отдельный indexed-MP4 профиль; standalone H.264 относится к fMP4 | MP4 probe; H.264 clip | Без B-frames и отдельный reordered mode | Расширение MP4 структуры/clock допустимо только с новым доказательством |
| Обычный MP4, head moov, H.264 + AAC-LC | [Indexed MP4](ProgressiveIndexedMp4.md), bounded sample tables/edit lists | MP4 probe; H.264/AAC-LC clip | AAC PCM-window: 44.1/48 kHz, mono/stereo, один общий resource | Прочие AAC profiles/layouts, audio-only и несколько tracks |
| Обычный MP4, HEVC/AV1/VP9 | fMP4-режимы ниже не разрешают обычные sample tables | MP4 probe; Clip этих video codecs не принимает | Нет | Первый новый профиль: HEVC video-only; затем отдельные пары/кодеки |
| MP4 с tail moov | [H.264 + AAC-LC](ProgressiveTailMp4.md), prefix media + опубликованный tail index | Probe/Clip может запросить недоступный index; нужен подтверждённый диапазон | Отдельного tail-index continuous контракта нет | Обобщение sparse/index и структуры — R03; размер файла не заменяет ranges |
| M4A/MOV | fMP4 audio-профиль не доказывает обычный M4A/MOV | Подсказки probe и MP4 clip, только допустимые H.264/AAC-LC | Нет отдельного заявленного входного профиля: public containerHint — `mp4` | Audio-only M4A; MOV variants, sample entries, edits и layout validation |
| Растущий scalar fMP4, video + audio | [H.264/AAC-LC](ProgressiveMuxedFmp4.md), [HEVC](ProgressiveMuxedFmp4Hevc.md), [AV1](ProgressiveMuxedFmp4Av1.md), [VP9](ProgressiveMuxedFmp4Vp9.md); отдельные [пары с Opus](ProgressiveMuxedFmp4Opus.md) | MP4 probe не заменяет native admission | Не отдельный public fMP4 input profile | Не реализовывать повторно существующие пары; прочие codecs/configuration/layouts отдельно |
| Растущий scalar fMP4, один track | [H.264/AAC-LC](ProgressiveStandalone.md), [HEVC/AV1/VP9](ProgressiveStandaloneFmp4Video.md), [Opus](ProgressiveStandaloneOpus.md), [FLAC](ProgressiveStandaloneFlac.md) | MP4 probe; AAC/H.264 clip в пределах своего контракта | Только обычный MP4/H.264 video-only профили выше | Прочие аудиокодеки, rates/layouts; explicit standalone admission сохраняется |
| WebM, video + audio | [VP9/Opus](ProgressiveMuxedWebm.md), [VP8/Opus](ProgressiveMuxedWebmVp8.md), [AV1/Opus](ProgressiveMuxedWebmAv1.md), [эти video + Vorbis](ProgressiveMuxedWebmVorbis.md) | WebM probe, без Clip | Нет | Непрерывный remux, дополнительные EBML/lacing/Cluster формы; существующие native rows не терять |
| WebM, один track | [VP8/VP9/AV1](ProgressiveStandaloneWebmVideo.md), [Opus/Vorbis](ProgressiveStandaloneWebm.md) | WebM probe, без Clip | Нет | Дополнительные формы контейнера/clock; отдельный continuous output contract |
| Matroska вне разрешённого WebM | Общего Matroska progressive playback нет | Только `matroska` probe | Нет | H.264/HEVC и audio codec pairs, CodecPrivate, lacing, cues, attachments, track identity |
| MPEG-TS | [Общий растущий H.264/AAC-LC](ProgressiveGrowingTs.md) и [раздельные проекции](ProgressiveMuxedTsPlayback.md) в ограниченных профилях | `mpegts` probe, без Clip | Нет | Continuous remux, HEVC/MPEG-2/прочее audio; discontinuity/rollover — совместно с R04 |
| ADTS, MP3, Ogg, raw FLAC, WAV, AC-3/E-AC-3 | Ограниченные standalone profiles в следующей таблице | ADTS/MP3/Ogg/FLAC/WAV probe; AC-3/E-AC-3 не входят в список probe hints | Нет audio-only continuous mode | Расширять по конкретному codec/layout, не по расширению файла |
| Separate video/audio, multi-audio, локальные segments/init | Ограниченные прежние [fMP4/WebM/TS и multi-audio пути](FoxicMPFamilyMatrix.md) | Probe одного resource не согласует несколько decoder clocks | Нет: один resource, один video либо H.264 + обязательный AAC | Separate/multi-input remux; HLS/DASH R05, track changes R07, live R10 |

## Кодеки: существующая база и пробелы R01/R02

| Codec/layout | Существующий ограниченный Native | Непрерывный remux и оставшиеся пробелы |
| --- | --- | --- |
| AAC-LC | fMP4, H.264 indexed/tail MP4, MPEG-TS; [standalone ADTS](ProgressiveAdts.md) — 44.1/48 kHz mono/stereo | Public только H.264/AAC-LC MP4 PCM-window; audio-only, дополнительные rates/layouts, HE-AAC отсутствуют в этом контракте |
| Opus | fMP4, WebM и [Ogg](ProgressiveOggOpus.md), точные skip/preroll profiles | Нет continuous audio; multichannel, mapping families, chaining/origins расширяются отдельно |
| Vorbis | WebM и [Ogg](ProgressiveOggVorbis.md), ограниченные mono/stereo | Нет continuous audio; остальные setup/rates/layouts и chaining |
| MP3 | [MPEG-1 Layer III](ProgressiveMp3.md), [VBR index](ProgressiveMp3Indexed.md), [gapless](ProgressiveMp3Gapless.md) | Нет continuous audio; оставшиеся MPEG versions/layers/clock profiles |
| FLAC | [fMP4](ProgressiveStandaloneFlac.md) и [raw FLAC](ProgressiveRawFlac.md): 16/24 bit, 44.1/48 kHz mono/stereo, ограниченная структура | Нет continuous audio; дополнительные block strategies/rates/layouts |
| PCM | [WAV PCM16](ProgressiveWav.md), [PCM24](ProgressiveWav24.md), [extensible PCM24](ProgressiveWavExtensible24.md), [RF64 PCM16/24](ProgressiveRf64.md) и [ограниченный sparse RF64](ProgressiveRf64RangeMap.md) | Нет continuous audio; float/другие depths/rates/channel masks/layouts |
| AC-3/E-AC-3 | [AC-3](ProgressiveAc3.md), [E-AC-3](ProgressiveEac3.md): 48 kHz mono/stereo, без LFE/dependent substreams | Нет continuous audio; multichannel, прочие rates/substreams/containers |
| MPEG-2 / MPEG-4 Part 2 video | Штатный decoder завершённых файлов не является native-progressive admission | Нет public continuous mode; container/configuration/B-frame проверки ещё нужны |

## Где именно требуется расширение

| Слой | Текущее ограничение | Работа для первого HEVC-профиля |
| --- | --- | --- |
| [Capabilities](../shared/progressivestreamcapabilities.h), [SDK helper](../qt/tools/progressive_continuous.py) | Три public H.264 modes | Новый явный mode, точные Main/Main10 ограничения, отсутствие fallback в H.264 |
| [Controller](../shared/progressivestreamcontroller.h), [parent](../shared/progressivestreamremux.h) | H.264 track hints, enum и проверка scalar MP4 | Согласованный codec/layout admission до запуска worker, совместимые старые mode/defaults |
| [Worker](../tools/progressive_stream_remux_worker.cpp) | MOV demuxer, `AV_CODEC_ID_H264`, MP4 output; audio требует отдельного window acknowledgement | Проверить HEVC config, packet-copy и reorder clock; запретить config changes/encryption/незаявленные tracks |
| [Cache](../shared/progressivestreamcache.h) | Video-only FragmentIndex вызывается без разрешения HEVC | Включать HEVC validator только для нового mode, транзакционно публиковать целые fragments |
| [Decoder source](../shared/progressivestreamdecoder.h) | Три допустимых mode, повторная AVC-default индексация | Пронести codec identity и одинаковый HEVC opt-in через оба index layers |
| Qt Linux/Windows | Один общий API/stream adapter и libmpv | Проверить обе ОС на ordinary builds; Linux PASS не засчитывать Windows |
| [Classic adapter](../src/DSUtil/HarvesterSession.cpp) | `OpenContinuous`/`RefreshContinuous` строят отдельное fMP4 представление | Пронести HEVC identity до BuildFragments/MP4 splitter; проверить обычный DirectShow graph |

[FragmentIndex](../src/DSUtil/HarvesterMP4.h) уже имеет **явный** `allowHevc`
и строгий HEVC sample-entry/config validator. Существующее разрешение в native
fMP4 не включается автоматически в continuous cache/decoder. Простое добавление
HEVC в JSON capabilities или в условие worker было бы неполным изменением.

## Первый профиль: согласованный объём реализации

Предложенное имя **`continuous-hevc-reordered-video-v1`** пока не опубликовано.
Выбран HEVC, поскольку для него уже проверены native fMP4 validator и декодеры
трёх редакций, а ordinary MP4 continuous отсутствует. Это снижает объём новых
container primitives, не подменяя проверку нового пути прежними результатами.

- Вход: один ordinary faststart MP4 с head index и одним обязательным HEVC
  video track. Main/Main10, 8/10-bit 4:2:0, single-layer hvc1/hev1, ограниченный
  hvcC с VPS/SPS/PPS и четырёхбайтовыми NAL lengths. Audio не добавляется.
- Время: известный VOD ≤3600 s, session timebase 1/10000000, offset 0;
  video presentation origin 0, отрицательный initial DTS B-frames сохраняется
  вместе с edit list. Fractional PTS/DTS не округляются через double.
- Выход: append-only fMP4, исходные compressed samples, hvcC и clock;
  без encoder/resampler, понижения глубины, media IPC и сетевого fallback.
- Не повышать прежние бюджеты: source/read 128 MiB, output/shared cache 64 MiB,
  50000 packets, 16 MiB/packet, один active worker, 8 pending updates/retained jobs,
  writer heartbeat 30 s и lifetime 1 h. Действуют подтверждённые ranges/SHA.
- Не добавлять этим профилем tail-moov/sparse media, generic MOV/Matroska,
  AVC/HEVC switching, live, HDR/GPU аттестацию или HEVC + AAC. Это следующие
  отдельные строки; старые H.264 modes не должны принимать HEVC.

### Приёмка до объявления capability

1. Четыре положительных ordinary MP4: Main/Main10 × hvc1/hev1, 25 и
   30000/1001 fps, B-frames; не менее 10 s реального просмотра до complete.
2. Публикации внутри moov/hvcC, packet и mdat: ожидание без false EOF и без
   чтения holes. Append продолжает тот же job/reader; missing index не берётся
   из физического размера. Complete с повреждённым хвостом — ошибка.
3. Неизменные packet SHA, рациональные PTS/DTS/durations и decoded frame hashes
   относительно независимого FFmpeg oracle; Main10 остаётся 10-bit.
4. Wrong hint, audio/extra track, unsupported profile/depth/chroma/NAL framing,
   повреждённый hvcC, config change/encryption, source mutation, лимиты и
   явный writer error/cancel: точные причины без ложного playing/complete.
5. Initial position, growing/queued seek, pause/resume/speed/stop; release,
   source/output receipts, cache forget, crash и существующие close-contention
   регрессии. Пользовательский профиль и shell registration неизменны.
6. Ordinary Linux Qt, Windows Qt и Classic — отдельные результаты и native exits.
   Только затем public capability/SDK example, обновление Linux и matrix.

### Подготовленные образцы и исходное поведение

[Preflight script](../tests/progressive_hevc_remux_preflight.py) сохраняет ordinary
MP4, полный reference fMP4, framemd5, worker info, stderr и exit каждой команды.
Он моделирует только подтверждённый 20-second prefix в worker AVIO и проверяет
отказ существующего H.264 mode. Полный FFmpeg copy oracle читает полный источник;
это не доказательство growing playback и не длительный A/V-прогон.

Команда из корня репозитория:

```sh
python3 -B tests/progressive_hevc_remux_preflight.py \
  --worker _bin/continuous-aac-qt-linux/foxicmp-remux-stream-worker \
  --report-directory _bin/r01-hevc-remux-preflight-linux
```

Сохранённый [отчёт подготовки](ProgressiveRemuxInventory-2026-11-12.json) фиксирует
фактические результаты:

| Исполнявшийся worker | Fixture/oracle controls | Harness exit | Ожидаемый отказ H.264 mode |
| --- | --- | --- | --- |
| Linux, обычная Qt build directory | 4/4 PASS | 0 | Четыре раза `unsupported-stream-codec`, worker exit 1, output 0 bytes |
| Windows, отдельный Qt stage | 4/4 PASS | 0 | То же |
| Windows, отдельный Classic stage | 4/4 PASS | 0 | То же |

У workers разные SHA; отчёт хранит их, FFmpeg versions, source/test SHA и точные
exits. Плееры Qt/Classic не запускались, renderer/decoder playback в этой серии
не проверялся. Прежние native fMP4 проверки связаны выше и не засчитываются
как новая приёмка. Документация/тестовая подготовка не требуют bump SDK/EXE
или замены установленного приложения.

Public HEVC continuous playback остаётся **не реализован**;
R01 не закрыт. Следующий шаг — подключение выбранного профиля через перечисленные
слои с отдельной проверкой трёх редакций.
