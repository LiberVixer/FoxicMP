# SDK1.102: bounded WebM VP9/Opus SimpleBlock VFR

Новый явный режим `continuous-webm-vp9-opus-simple-vfr-v1` обрабатывает видео
в SimpleBlock без DefaultDuration и BlockDuration. Он дополняет, а не расширяет
admission прежнего `continuous-webm-vp9-opus-vfr-v1`, которому нужны BlockGroups
с явными длительностями. Общий проект FoxicMP, приложение 0.2.0.

## Входной контракт

- Один local SHA-protected resource с известной длиной ≤128 MiB; неизвестный
  размер Segment, конечные или unknown-size Clusters. Только подтверждённые ranges.
- Два обязательных track: VP9 profile0/8-bit либо profile2/10-bit 4:2:0 и Opus
  48 kHz mono/stereo. Геометрия и indexed superframes проверяются существующим
  VP9 validator, не только codec hints. PTS=DTS, первый displayed кадр ключевой
  в нулевой точке, без reorder/show_existing/lacing.
- Video DefaultDuration отсутствует (включая запрет явного нуля); video
  BlockGroups не принимаются. TimestampScale — 1000000 ns. Next-PTS задаёт
  интервал 1–1000 ms; последнему кадру нужен подтверждённый writer EOF и endpoint.
- `continuousRemuxStart.videoSamples` — точный положительный frame count ≤50000;
  timeline duration — известный endpoint в 100 ns, кратный миллисекунде, ≤1 часа.
  `videoRate` не допускается. Counts/endpoint входят в неизменную identity job.
- `audioSamples` — точное число presentation samples 649–48000000. Opus packets
  по960 samples, pre-skip312, gain0/mapping0, CodecDelay6500000 ns,
  SeekPreRoll80000000 ns; минимум два пакета, точный последний DiscardPadding.
  Суммарно video+audio packets ≤50000. Другие Opus layouts остаются отдельной работой.

## Потоковая обработка

Общий C++ `ContinuousWebmOpus` включает отдельный SimpleBlock state machine.
Он читает последовательные подтверждённые bytes исходника и сохраняет один
unresolved видеопакет. После следующего PTS проверяет VP9 payload/clock и создаёт
BlockGroup с BlockDuration; для inter-frame добавляет ReferenceBlock0.
Закрытый предыдущий Cluster может выйти сразу после тела следующего видеопакета,
без ожидания конца следующего Cluster. Последний Cluster удерживается до EOF.

Input и output имеют независимые размеры. Обработчик не знает конечную длину
canonical output; общий worker sink и cache принимают последовательные append,
считают SHA и публикуют только структурно проверенные фрагменты. Завершение
проверяет точные video/audio counts и последний endpoint.

Head ≤1 MiB/32 элемента, Cluster ≤16 MiB/65536 детей, всего ≤8192 Clusters и
32768 top-level elements. Pending queue ≤32 MiB/32 Clusters; source read buffer
≤65536 bytes, выходной worker/cache budget ≤64 MiB. Частичное поле ждёт bytes,
отмена завершает ожидание существующим managed-worker протоколом.

SeekHead/Cues, Segment/Cluster CRC и Position/PrevSize удаляются после изменения
структуры. CodecPrivate, packet payload, timestamps, CodecDelay и audio trim
сохраняются; перепаковка не перекодирует медиа. CRC произвольного входного файла
полностью не аттестуется. Tags допускаются до первого Cluster; post-media Tags
отклоняются явно. Это bounded WebM профиль, не общий Matroska remux.

Дополнительный регрессионный тест воспроизводит прежнюю проблему мелких чтений:
старый вариант выполнял 8442 source IPC для fixture, исправленный —16 при том же
ограничении в65536 bytes/request. Это бюджет количества запросов, не обещание
универсальной задержки старта на любом компьютере.

Qt Windows/Linux используют общий decoder adapter. Classic получает тот же
canonical output и variable clock при повторной FragmentIndex индексации.
Существующие pause/seek/queued target/release API сохраняются.

## Вызов из внешней программы

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_SIMPLE_VFR_MODE

preview = ContinuousRemux(client, session_directory,
    mode=WEBM_VP9_OPUS_SIMPLE_VFR_MODE,
    audio_samples=1152000, video_samples=733)
preview.prepare(snapshot, "media")
# Writer публикует только подтверждённые ranges и поддерживает heartbeat.
# После playbackReady вызывающий явно запускает preview.play().
```

Числа выше относятся к конкретному fixture; внешняя программа должна знать
настоящие counts/endpoint собственного источника. SDK сверяет capability до
запуска. Автоматического выбора режима, загрузки сети и изменений Harvester нет.
Плейбек тестируется отдельно от parser/PCM; физический A/V и общий SimpleBlock
с неизвестными counts/duration этим bounded режимом не объявляются готовыми.

[Итоговая проверка трёх редакций](ProgressiveContinuousWebmOpusSimpleVfrValidation-2026-11-15.json):
18 ordinary-main сценариев с ≥10 s до complete, pause/growing seek/queued target,
EOF/release, 18 PCM captures и75 worker cases прошли. 372 старых worker и60
main сценариев сохранили совместимость; 4000 sanitizer mutations и137 SDK units
прошли. В Classic использован private null video renderer; физический A/V
этим не измерен. Первые неудачные попытки и устранённые причины записаны в отчёте.

Состояние приёмки: [фиксированный production чеклист](ProgressiveSimpleVfrProductionChecklist.md).
Подготовка и техническое основание: [предыдущий этап](ProgressiveSimpleVfrRemuxPlan.md).
