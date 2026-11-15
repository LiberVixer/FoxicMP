# R01: WebM/Opus без внешнего audioSamples

15 ноября 2026. База — приложение0.2.0 / SDK1.104. Этот срез — подготовка
следующего production этапа, а не новый API или проверка его ordinary playback.
Harvester не изменяется. [Фиксированный чеклист](ProgressiveAudioCountExecutionChecklist.md)
содержит16 подпунктов; подготовка и production учитываются раздельно.

## Зачем нужен следующий режим

SDK1.103/1.104 уже считают видеопакеты самостоятельно, но `audioSamples` требуется
до старта worker. Для скачиваемого файла это число не всегда известно заранее.
Нельзя заменить его размером файла, предполагаемой длительностью аудио или числом
полученных пакетов, не учитывая обрезку. В короткой дорожке аудио также может
закончиться намного раньше видео.

Начальная геометрия сохраняется: Opus48k mono/stereo,960 samples/packet,
pre-skip312, gain0/mapping0, без lacing; VP9 p0/p2, video SimpleBlock VFR без
DefaultDuration, zero-origin и известный video endpoint. Другие packet durations,
one-packet audio, неизвестные source length/общий endpoint пока не включаются.

## Проверяемая временная модель

В ограниченном профиле presentation count равен сумме длительностей Opus packets
минус pre-skip и конечная обрезка. Pre-skip и часы48k описаны в
[RFC7845 §4.2–4.3](https://www.rfc-editor.org/rfc/rfc7845.html#section-4.2),
OpusHead/CodecDelay — в [Matroska A_OPUS](https://www.matroska.org/technical/codec_specs.html#a_opus).
`DiscardPadding` задаёт наносекунды обрезки блока; положительное значение относится
к его концу, отрицательное — к началу. См. [Matroska DiscardPadding](https://www.matroska.org/technical/elements.html#DiscardPadding).

Это не общий допуск всех форм Matroska Opus. В предложенном bounded mode разрешается
только положительная конечная обрезка0–959 samples, округлённая до floor/ceil
наносекунды. Пакет после ненулевого end trim — ошибка именно этого профиля.
Предполагается минимум два packets и649 presentation samples; максимум48000000
samples и50000 суммарных video/audio packets. TOC/framing проверяются по bytes,
не только по codec hint. Полную VP9/Opus bitstream admission проверяет существующий
worker; Python study не заменяет production C++ validators.

Тестовый счётчик удерживает последний аудиопакет: промежуточное значение содержит
только уже безопасные presentation samples. Partial Block не считается пакетом.
Наличие всех bytes файла и даже встреченный DiscardPadding не означают writer EOF.
Итоговый count появляется только после подтверждённого complete; промежуточное
значение не используется как окончательный decoder PCM end.

## Что проверено и чего подготовка не доказывает

[Машинный отчёт](ProgressiveAudioCountPreflight-2026-11-15.json) различает Linux Qt,
Windows Qt и Classic. Пять fixtures × finite/unknown Clusters: полные mono,
короткие stereo, p2 stereo без end trim, минимальные649 samples с audio-first
и переменные video PTS. Для каждого входа проверяются три дробных read patterns,
partial audio packet и совпадение count с числом независимо декодированных PCM samples.

Тестовый incremental prototype получает только подтверждённые порции до65536 bytes.
Он не имеет внешнего audioSamples или карты полного файла. Существующий тестовый
SimpleBlock sealer создаёт canonical BlockDuration video; packet hashes/PTS/DTS,
decoded video и PCM сравниваются с исходником. Контроли проверяют premature EOF,
regressing clock, duplicate/negative/fractional/excessive trim, audio after trim
и sample/packet budgets. Отдельно проверяются one-packet,10ms packet и отсутствие
required audio payload; они не включаются автоматически.

Prototype пока получает известные video frame count и endpoint. Production
auto-video count из SDK1.103 здесь не реализуется повторно; его объединение с
новым audio state ещё должно пройти рабочие scanner/cache/decoder и main tests.

Canonical output проверяется существующим worker и PCM adapters Qt/Classic.
**Только после завершения prototype существующему consumer передаётся вычисленное
audioSamples как известный план.** Это oracle для проверки декодирования результата.
Он не доказывает, что новый unknown-count режим уже умеет играть до complete.
Отсутствующее поле audioSamples штатный worker по-прежнему отклоняет.
Ordinary main нового режима и физический A/V выход здесь не проверяются.

Удаление целого последнего audio packet с перестроением размеров даёт структурно
корректную более короткую дорожку, если writer объявляет новую согласованную длину
и bytes. Prototype считает её фактический PCM; без независимого аудиоплана это
неотличимо от намеренно более короткой дорожки. Контроль **не** утверждает, что
укороченный файл принят с прежней заявленной длиной: premature EOF проверяется
отдельно и отвергается. Идентичность исходного объекта/writer остаётся отдельным
контрактом R12/R13; этот профиль не обещает семантическое восстановление потерянных packets.

## Перенос в рабочий код

Предварительное имя `continuous-webm-vp9-opus-simple-vfr-derived-av-v1` зарезервировано
только этим планом. SDK1.104 не принимает и не рекламирует его.

| Слой | Требуемое изменение |
| --- | --- |
| C++ scanner | Отдельный derived-audio state, контроль TOC/clock/trim по входным bytes; независимые packet/sample budgets. Не вычислять audioExpected из неизвестного числа presentation samples. |
| Worker/controller | Новый explicit mode; audioSamples/videoSamples/videoRate запрещены, video endpoint и length известны. У worker свой final count, у cache — независимая проверка по canonical bytes. Cancel/wait/EOF не превращать в фиктивные нулевые counts. |
| Cache | Сейчас `opusSamples != 0` одновременно означает наличие Opus и задаёт окончательный план. Разделить codec/profile identity, промежуточный count и final count. Убрать ceil(audioSamples/960) только для нового mode. |
| Coverage | Различать безопасный PCM prefix, возможный конец audio track и writer/output EOF. Учесть1ms timestamp rounding. Положительный end trim может быть терминальным маркером только в явно ограниченном профиле; следующий audio packet должен завершать job ошибкой. |
| Short audio | Если trim отсутствует/нулевой, до writer EOF нельзя отличить временную паузу writer от конца дорожки. Задокументировать waiting; не отключать required audio и не угадывать его конец по остановке поступления bytes. Проверить также положительный terminal trim при продолжающемся video. |
| Qt decoder | Не передавать provisional count как окончательный PCM end. Сохранить точную обработку pre-skip/trim и обновление индекса без перезапуска просмотра при каждом пакете. |
| Classic decoder | Нынешний `IProgressivePcmControl` v2 получает конечный end перед первой compressed packet. Новую динамическую семантику согласовать со splitter/decoder отдельно, сохранив MP3 v1 и Opus v2 ABI/admission. Повторное индексирование/seek не должно удваивать pre-skip или trim. |
| API/SDK | Явные промежуточные/окончательные audio counters и точное условие final. Не менять старые known-plan profiles и defaults. False complete, смешение cache identities и replay с другим mode должны отклоняться. |
| Приёмка | Ordinary Linux Qt/Windows Qt/Classic ≥10s до complete; full/short/minimal/zero-trim/VFR, pause/growing/queued seek, EOF/release, malformed/partial/whole-packet omission controls, sanitizer/resource budgets и старые режимы. |
| Поставка | Только после приёмки: три сборки, новая SDK capability, clean SDK и атомарное обновление Linux с backup/неизменным профилем. |

Ключевые текущие границы находятся в [cache](../shared/progressivestreamcache.h),
[worker scanner](../src/DSUtil/HarvesterWebMOpusContinuous.h),
[controller](../shared/progressivestreamcontroller.h) и
[Classic PCM handshake](../src/DSUtil/ProgressivePcmControl.h).

## Воспроизведение подготовки

CTest `progressive-audio-count-preflight` запускается через `qt/tests/run_tests.sh`
с отдельными X11/D-Bus/PulseAudio. Существующие probe/worker должны соответствовать
SDK1.104; новые EXE для этой подготовки не требуются. Прямой test-only вызов:

```text
python3 tests/progressive_audio_count_preflight.py --worker <worker> --runner <PCM-probe> --edition qt --report-directory <private-report>
python3 tests/progressive_audio_count_prototype_test.py
python3 tests/progressive_audio_count_limits_test.py
```

Для Classic — его private PCM probe, `--edition classic`, INI с
ContextMenuPrompted=1/ContextMenuEnabled=0 и HKCU snapshots до/после.
Прототип и fixtures не входят в SDK API. Приложение, SDK, установленный Linux
и Windows-профили этой подготовкой не обновляются. Общие R01/R04/R15/R17 открыты.
