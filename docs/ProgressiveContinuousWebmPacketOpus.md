# SDK1.106: progressive VP9/Opus с переменной длительностью пакетов

Отдельный явный режим `continuous-webm-vp9-opus-packet-clock-v1`. Общая реализация
используется Linux Qt, Windows Qt и Windows Classic; YouTubeHarvester не меняется.
Приёмка отслеживается в [фиксированном чеклисте](ProgressiveOpusGeometryExecutionChecklist.md).
Этот документ описывает контракт; итоговые результаты и точные exits сохраняются
в [отчёте](ProgressiveContinuousWebmPacketOpusValidation-2026-11-16.json).

## Вход и границы

Один подтверждённый локальный WebM resource, VP9 profile0/8-bit либо2/10-bit420
и обязательный Opus48k mono/stereo. OpusHead: version1, mapping0, gain0,
pre-skip120 либо312samples с точно согласованным CodecDelay; SeekPreRoll80ms.
Известны положительная длина источника≤128MiB и конечная точка видео≤3600s.
Внешние `audioSamples`, `videoSamples` и `videoRate` для этого режима запрещены.

Video SimpleBlock без DefaultDuration, один displayed frame/packet, допустимые
indexed superframes с displayed frame last. VFR интервалы определяются next PTS,
последний — известной конечной точкой после writer EOF. Источник имеет zero origin,
TimestampScale1ms, unknown-size Segment; Clusters конечные либо unknown-size.
Неполная структура ждёт подтверждённые bytes; физический размер не заменяет ranges.

Opus duration определяется TOC/framing по содержимому:120–5760samples, шаг120,
не более120ms на packet. Положительная конечная DiscardPadding меньше duration
последнего packet, в целых samples с floor/ceil округлением до наносекунд.
Пакет после ненулевой обрезки в этом профиле отвергается. Отрицательная обрезка,
lacing, config changes, mapping families/multichannel и one-packet audio не
допускаются. Минимум два audio packets и649 presentation samples; максимум48M
presentation samples и50000 суммарных video/audio packets.

Источник и canonical remux output сохраняют compressed packets, исходные PTS/DTS,
OpusHead/CodecDelay и DiscardPadding. Video SimpleBlock преобразуется в явные
BlockDuration/ReferenceBlock; Clusters закрываются структурно. Offset-bearing
metadata и устаревшие CRC удаляются. Encoder/resampler/network fallback нет.

## Сэмплы, ожидание и EOF

Рабочий clock суммирует реальные durations, вычитает pre-skip и конечную обрезку.
Первый packet может быть короче pre-skip: unsigned underflow не допускается.
Clock входных packets сверяется с суммой при допуске1ms container quantization.
Cache независимо повторяет этот подсчёт по canonical bytes и сверяет samples,
число packets и pre-skip из final worker result. Подмена любого из них — отказ.

Последний аудиопакет удерживается до successor либо подтверждённого writer EOF.
Положительный end trim может доказать конец audio track в рамках профиля, но
не завершает video/output/job. Short zero-trim audio нельзя завершать по паузе
writer или появлению более позднего video: оно ждёт writer EOF. Bounded pending
queue отвергает чрезмерное удержание данных. Потеря целого packet с новой
согласованной длиной не восстанавливается по TOC: нужна независимая writer/source
identity. Premature EOF при прежней длине отвергается отдельно.

`indexedAudioSamples` — текущий проверенный prefix; `audioSampleCountFinal`
становится true только при outputComplete. `audioTrackEndObserved` отличается
от writer EOF. Аналогично сохраняются `indexedVideoSamples/videoSampleCountFinal`.
Final result содержит типизированный `audioPreSkipSamples` и derived counters;
самостоятельный recount не может отметить output complete.

## Декодеры и точная обрезка

Classic использует отдельный IID `IProgressivePacketOpusControl`, не изменяя
fixed MP3 v1, Opus v2, AAC или прежний growing-Opus admission. Splitter получает
накопленный raw sample offset выбранного packet из cache, учитывает preroll и
задаёт окно `[preSkip + target, preSkip + presentationCount)`. Safe end может
монотонно расти до поступления следующих bytes. Декодер получает raw Opus PCM
с manual skip, затем применяет это окно ровно один раз. Поэтому ограничение
обычного completed-file Matroska пути, оставляющего DiscardPadding, не переносится
на новый progressive path. Это не изменение обычного открытия файлов Classic.

Qt использует libavformat для отдельной аудиодорожки: native MKV refresh seek
некоторых mpv runtimes может потерять первый negative-PTS packet, полностью
занятый pre-skip. Для Qt callback создаётся **отдельная decoder-only WebM view**
с TimestampScale1000ns и PTS из накопленных Opus samples. Physical payload spans
по-прежнему ссылаются на проверенный cache; source/canonical remux clocks не
переписываются. Аудиометки округляются до ближайшей microsecond (<0.024sample),
а не миллисекунды. DiscardPadding кодируется как положительный **signed EBML**,
сохраняя точное число наносекунд. BlockDuration/номинальные подсказки в этой
внутренней audio view не подменяют duration из TOC.

Qt отключает дополнительную initial-audio-sync обрезку к ближайшему video frame.
При start/seek preroll отбрасывается по точному presentation sample target перед
пользовательскими audio filters; конечную обрезку выполняет Opus/DiscardPadding.
User graph/format остаётся управляемым прежними средствами. Stop/replacement
сбрасывают внутреннее окно, не меняя сохранённые настройки. Полное PCM от начала
и точный count после seek проверяются отдельно: bounded decoder preroll после
seek не обещает побайтовое совпадение всей истории Opus с decode from origin.

## API и совместимость

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_PACKET_CLOCK_MODE

preview = ContinuousRemux(client, session_directory,
                         mode=WEBM_VP9_OPUS_PACKET_CLOCK_MODE)
preview.prepare(snapshot, resource_id)
# После playbackReady — существующий explicit play commit.
preview.play()
```

Writer продолжает публиковать совместимые snapshot revisions/ranges/integrity;
Harvester здесь не реализуется и не меняется. SDK проверяет точную capability,
включая packet clock, диапазон durations, pre-skip list и Qt sample trim contract.
Старые modes/defaults не расширяются молча и сохраняют прежние reason codes.
EXE/INI/IPC endpoints/ProgID трёх приложений остаются независимыми.

Бюджеты не повышены: source/read128MiB, output/shared cache64MiB,
Cluster16MiB, pending32MiB/32Clusters,50000 total packets, один active worker,
8 pending updates/retained jobs,30s heartbeat и1h lifetime. Decoder audio view
ограничена64MiB, её metadata/spans ограничены packet count; она не копирует
compressed payload. Ошибка превышения лимита не превращается в EOF.

Неизвестные source length/video endpoint, остальные контейнеры/Opus layouts,
live, physical A/V output и общие R01/R04/R15/R17 этим профилем не закрываются.
