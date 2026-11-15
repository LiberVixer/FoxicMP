# SDK1.104: SimpleBlock VFR с номинальным DefaultDuration

Отдельный режим `continuous-webm-vp9-opus-simple-vfr-nominal-v1` расширяет набор
входных layouts после [SDK1.103](ProgressiveContinuousWebmOpusAutoVfr.md).
Видеодорожка содержит ненулевой DefaultDuration, но явно выбранная политика
этого режима строит непрерывные интервалы по следующим PTS и последнему endpoint.
Внешний videoSamples не нужен; source length, endpoint и audioSamples известны.
Приложение0.2.0, Harvester не изменён.

## Явная политика длительностей

Matroska определяет DefaultDuration в наносекундах на кадр. Правило вывода
длительности из следующего timestamp при отсутствии BlockDuration в спецификации
сформулировано для случая без DefaultDuration. Поэтому новая политика — **явный
opt-in FoxicMP**, а не утверждение, что любой файл с DefaultDuration обязан
интерпретироваться именно так. См. [спецификацию Matroska](https://www.matroska.org/technical/elements.html#BlockDuration).

Пакет видео показывается до следующего PTS, последний — до известного endpoint.
Worker записывает эту длительность в BlockGroup/BlockDuration и сохраняет исходный
DefaultDuration в заголовке. Payload, PTS/DTS, порядок кадров и аудио сохраняются;
номинальные packet durations могут отличаться от canonical BlockDuration.
Сохранение намеренных пустых интервалов между видеокадрами этим режимом не обещано.
Это нормализация покрытия видео, а не общий lossless-remux всех timing semantics.

Режим не выбирается автоматически. Прежние SimpleBlock VFR modes по-прежнему
требуют отсутствие DefaultDuration. Старый explicit BlockDuration VFR сохраняет
свои условия и остаётся отдельным вариантом для уже заданных длительностей.

## Ограниченный вход

- VP9 p0/8-bit либо p2/10-bit 4:2:0 с проверенными indexed superframes, Opus48k
  mono/stereo по960 samples, pre-skip312/gain0/mapping0. Один displayed кадр на
  видеопакет, PTS=DTS с нулевой точки, без lacing/show_existing/reorder.
- Video DefaultDuration обязателен, ровно один, от1000000 до1000000000 ns
  включительно. Нулевой, отсутствующий, дублированный или вне диапазона — отказ.
  Значение не задаёт fixed frame grid и не используется для подсчёта кадров.
- Только SimpleBlock video; audio BlockGroups и DiscardPadding сохраняются.
  Реальный интервал соседних video PTS —1–1000 ms; последний кадр ждёт writer EOF.
- Один SHA-protected local resource с известной длиной ≤128 MiB, known endpoint
  в100 ns, кратный миллисекунде, ≤1 часа. Известный audioSamples649–48000000;
  общий video+audio packet budget ≤50000. videoSamples и videoRate запрещены.
- Unknown-size Segment, finite/unknown Clusters; head ≤1 MiB, Cluster ≤16 MiB,
  pending queue ≤32 MiB/32 Clusters, output/cache ≤64 MiB. Только подтверждённые bytes.

Новый флаг проходит через controller/worker/cache/decoder identity. Нельзя
связать nominal source mode с cache прежнего absent-DefaultDuration режима.
Общий Qt-код работает на Linux/Windows; Classic сохраняет свою цепочку декодеров.

## Статус и вызов

Поля `indexedVideoSamples`/`videoSampleCountFinal` сохраняют смысл SDK1.103:
промежуточное число опубликованных кадров и финальный статус только при
outputComplete. Старого поля внешнего frame plan `videoSamples` нет.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_NOMINAL_VFR_MODE

preview = ContinuousRemux(client, session_directory,
    mode=WEBM_VP9_OPUS_NOMINAL_VFR_MODE, audio_samples=1152000)
preview.prepare(snapshot, "media")
# Writer публикует подтверждённые ranges/heartbeat, play вызывается явно.
```

1152000 — число сэмплов конкретного тестового аудио, не универсальное значение.
SDK проверяет точный контракт nominal hint и требует явный mode; fallback нет.
Pause, growing seek, один queued target, EOF/release наследуются из SDK1.103.
SeekHead/Cues, изменившиеся Cluster CRC/Position/PrevSize удаляются; заголовок
DefaultDuration сохраняется. Общая проверка CRC произвольного входа не заявлена.

Приёмка: [фиксированный чеклист](ProgressiveNominalVfrExecutionChecklist.md).
Неизвестные endpoint/audioSamples, другие codecs/layouts, физический A/V и
общие R01/R04/R15/R17 остаются отдельной работой.


[SDK1.104: проверка перед поставкой](ProgressiveContinuousWebmOpusNominalVfrValidation-2026-11-15.json):
99 worker /24 ordinary main (включая6 boundary hint) /18 PCM /525 legacy worker /
69 legacy main,142 SDK units и4000 sanitizer mutations прошли. Endpoint/audioSamples
остаются известными. Три редакции проверены отдельно; номинальные packet durations
и намеренные gaps не объявлены сохранёнными. Установки/HKCU и профиль сохранены.
