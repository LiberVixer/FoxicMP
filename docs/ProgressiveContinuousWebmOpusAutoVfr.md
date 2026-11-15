# SDK1.103: SimpleBlock VFR без заданного числа кадров

Отдельный режим `continuous-webm-vp9-opus-simple-vfr-auto-count-v1` продолжает
[SDK1.102](ProgressiveContinuousWebmOpusSimpleVfr.md). Внешняя программа больше
не задаёт `videoSamples`: плеер считает видеопакеты по подтверждённым bytes.
Конечные source length, timeline endpoint и `audioSamples` остаются обязательными.
Это не режим с неизвестной длительностью, не live и не изменение Harvester.

## Контракт и совместимость

Сохраняется входной профиль SDK1.102: VP9 p0/8-bit или p2/10-bit 4:2:0, indexed
superframes, один displayed кадр в видеопакете, Opus48k mono/stereo по960 samples,
zero-origin PTS=DTS, SimpleBlock video без DefaultDuration, finite/unknown Clusters.
Next-PTS задаёт BlockDuration, последний кадр ждёт подтверждённый writer EOF
и известный endpoint. Сохранены source SHA, compressed payload, clocks и audio trim.

Новый режим запрещает `continuousRemuxStart.videoSamples` с любым значением и
запрещает `videoRate`. Старые VFR режимы по-прежнему требуют точное число кадров
и сравнивают его с источником; их admission не расширяется.

Ограничение ≤50000 packets относится к video+audio вместе. Из известного числа
Opus packets вычитается место в общем бюджете; оставшийся предел ограничивает
производный video count. Нулевое видео и превышение бюджета отвергаются. Cache
проверяет непрерывные video intervals и endpoint; завершение счётчика связано
с подтверждённым source EOF и успешным закрытием output, не с размером файла.
Остальные byte/time/pending/cache bounds наследуются из SDK1.102.

Производный count не сравнивается с независимым заранее известным эталоном.
Целостность источника подтверждается SHA/ranges и завершением writer. Если такое
эталонное число доступно, прежний режим с `videoSamples` сохраняет дополнительную
проверку соответствия ему. Плеер не угадывает отсутствующие bytes или кадры.

## Статус

В job и playback status новый режим возвращает:

| Поле | Значение |
| --- | --- |
| `indexedVideoSamples` | Каноническая decimal string, число опубликованных в cache видеокадров; сначала `"0"` |
| `videoSampleCountFinal` | `false` во время загрузки/индексации, `true` только при `outputComplete` |
| `videoSamples` | Отсутствует: старое поле обозначает внешний frame plan и не переопределяется |

Промежуточное число относится к опубликованным кадрам. Удерживаемый последний
Cluster ещё не входит в него. После успешного EOF значение положительно и точно
равно числу видеопакетов output. SDK проверяет типы/границы и не принимает
промежуточный count как окончательный. Изменение count по мере публикации не
меняет неизменную identity запроса. Pause/seek/queued target/release сохраняются.

## Вызов

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_AUTO_VFR_MODE

preview = ContinuousRemux(client, session_directory,
    mode=WEBM_VP9_OPUS_AUTO_VFR_MODE, audio_samples=1152000)
preview.prepare(snapshot, "media")
# snapshot содержит известные length и timeline duration.
# Writer публикует подтверждённые ranges/heartbeat, play вызывается явно.
```

Число1152000 относится к тестовому audio fixture, а не к любому ролику.
Новая capability требует точного выбора режима; fallback в старый mode или
автоматического запуска нет. Общий Qt-код обслуживает Windows/Linux; Classic
получает тот же canonical output и variable clock с отдельным признаком count.

[Проверки трёх редакций](ProgressiveContinuousWebmOpusAutoVfrValidation-2026-11-15.json):
78 worker /18 ordinary-main /18 PCM cases прошли, включая точный производный
count и отсутствие внешнего frame plan. 447 старых worker и69 main сценариев,
4000 sanitizer mutations и140 SDK units прошли. В Classic использован private
uncompressed null video renderer; видимый/физический A/V этим не аттестован.

Приёмка: [фиксированный чеклист](ProgressiveAutoVfrExecutionChecklist.md).
Физический A/V, неизвестные audio count/duration, другие layouts и широкие
R01/R04/R15/R17 остаются отдельной работой. Приложение —0.2.0.
