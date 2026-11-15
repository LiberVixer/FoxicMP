# SDK 1.101: progressive WebM VP9/Opus с явными длительностями кадров

Отдельный режим `continuous-webm-vp9-opus-vfr-v1` поддерживает подготовленный
VFR-профиль в Linux Qt, Windows Qt и Windows Classic. Видеопакеты имеют
BlockGroup/BlockDuration; DefaultDuration может отсутствовать. Старые CFR,
NTSC и 25 fps контракты сохраняются. Harvester не изменён.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_VFR_MODE
preview = ContinuousRemux(client, absolute_session_directory,
    mode=WEBM_VP9_OPUS_VFR_MODE, video_samples=733,
    audio_samples=1152000)
preview.prepare(snapshot, 'media')
# publish подтверждённых ranges, heartbeat и явный play — как в других режимах.
```

При start обязательны независимые точные `videoSamples` и `audioSamples` —
канонические десятичные строки в IPC, целые числа в SDK. Загрузчик должен
знать эти значения и точную duration; они не вычисляются из средней fps.
`videoRate` в этом режиме запрещён. `videoSamples` возвращается в job/playback
status и проверяется SDK. Повторный requestId сохраняет mode/counts/session
identity, изменение плана вызывает continuous-conflict. Поля manifest не меняются.
Пример [manifest](../tests/fixtures/progressive-v2/continuous-webm-opus-vfr.json)
содержит duration 24s; sample counts передаются отдельно при start.

## Допуск контейнера и времени

- Один shared local WebM, unknown-size Segment, finite/unknown Clusters,
  TimestampScale 1ms. Исходные compressed bytes, PTS/DTS и trim не переписываются.
- VP9 Profile0/8-bit или Profile2/10-bit 4:2:0, включая уже проверенные indexed
  superframes/hidden inter frames; один показанный кадр на packet, PTS=DTS.
- Каждый video packet — BlockGroup с ровно одним положительным BlockDuration
  не больше 1000 Track Ticks. SimpleBlock video в этом режиме отклоняется.
  Первый кадр keyframe, PTS0; key/reference status проверяется по VP9.
  Допускаются до двух неположительных ReferenceBlock; нулевой unknown reference
  должен быть единственным. Lacing, CodecState и смена конфигурации исключены.
- Каждый следующий PTS совпадает с предыдущим endpoint. Нет перекрытий,
  пропущенных/добавленных кадров, gap либо произвольного изменения времени.
  Последний endpoint обязан точно совпасть с `timeline.duration` в 100ns.
- Отсутствующий DefaultDuration разрешён только этому явно выбранному пути
  в worker, cache и повторном Classic FragmentIndex. Старые native/CFR пути
  не получают неявного допуска. Если поле присутствует, оно остаётся bounded.
- Opus 48kHz mono/stereo, 20ms/960 samples, pre-skip312, gain0, mapping0,
  SeekPreRoll80ms; минимум два packets и точный audio presentation sample count.
  CodecDelay/DiscardPadding сохраняются, existing Opus decoder adapters работают
  независимо от длительности видео. One-packet Opus не включён.

Доступная граница берётся из проверенных sample intervals. CFR frame-grid
формула к VFR не применяется. Source/cache/Cluster limits 128/64/16MiB,
50000 суммарных video+audio packets, heartbeat, SHA/ranges и reader receipts
сохраняются. Терминальные ошибки не становятся ложным успешным EOF.

## Проверка

[Отчёт трёх редакций](ProgressiveContinuousWebmOpusVfrValidation-2026-11-15.json)
разделяет ordinary-main playback до complete, PCM captures, worker/fault controls
и прежние режимы. Каждый положительный main case играет не менее 10s по
монотонным часам до complete, проходит pause, growing/queued seek, EOF и release.
Синтетические fixtures содержат изменения cadence на 8-й и 16-й секунде,
50 и 60000/1001 base clocks, 8/10-bit, mono/stereo и оба варианта DefaultDuration.

Первый прогон Classic действительно отказал без DefaultDuration: его повторный
индекс не получал mode-scoped разрешение. Исправление проверяется отдельно от
успеха Qt. Исходная ошибка и отказы среды private D-Bus сохранены в отчёте;
инфраструктурный отказ не считается отказом декодирования или PASS.

## Оставшиеся границы

VFR SimpleBlock без явных длительностей требует отдельного lookahead/final-end
контракта и пока не включён. Unknown total frame count/duration, ненулевые
origins, discontinuities, прочие Opus layouts и полные R01/R04/R15/R17 открыты.
Физический A/V drift не измеряется file/null-output стендами. Это проверенный
профиль кандидата 0.2.0 /SDK1.101, а не новый итоговый GitHub release.
Wayland и отложенное оборудование не проверяются.
