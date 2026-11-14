# SDK 1.100: continuous WebM VP9/Opus с явной частотой кадров

Режим `continuous-webm-vp9-opus-cfr-v1` расширяет R01 на восемь постоянных
частот: **24000/1001, 24/1, 25/1, 30000/1001, 30/1, 50/1, 60000/1001, 60/1**.
Linux Qt, Windows Qt и Windows Classic используют один bounded parser.
Прежние режимы 25 fps и 30000/1001 fps сохраняют свои контракты.
Harvester и пользовательские настройки не изменяются.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_CFR_MODE
preview = ContinuousRemux(client, absolute_session_directory,
    mode=WEBM_VP9_OPUS_CFR_MODE, video_rate=(30000, 1001),
    audio_samples=1152000)
preview.prepare(snapshot, 'media')
# Затем publish/heartbeat и явный play, как в прежних continuous-режимах.
```

`video_rate` обязательно только в этом режиме. SDK копирует пару в immutable
identity. API получает `videoRate: {"numerator": 30000, "denominator": 1001}`
в `continuousRemuxStart`; поля не добавляются в manifest и update.
Допускаются только канонические пары: 48000/2002 не заменяет 24000/1001.
Булевы, дробные JSON-числа и лишние поля отклоняются. Поле возвращается в job
и playback status, SDK проверяет его при каждом ответе. Изменение rate с тем же
requestId вызывает continuous-conflict. Автоматического выбора fps нет.

## Временная модель

Для частоты n/d DefaultDuration соответствует floor/ceil(1e9*d/n).
Допуск вниз на 1 ns (`defaultDurationToleranceNanoseconds: 1`) учитывает
усечение приближённого double в FFmpeg: для 60000/1001 получено 16683332 ns
вместо точного floor 16683333 ns. Исходное поле сохраняется. Это не ослабляет
проверку каждого кадра: Block PTS в миллисекундах должен быть ближайшим
целым к `frameIndex*1000*d/n`, при точной половине допустимы оба направления.
Первый кадр имеет PTS 0; VFR, пропущенные и лишние кадры отклоняются.

`timeline.duration` задаётся как floor/ceil в 100 ns от `frameCount*1e7*d/n`.
Например, 576 кадров 24000/1001 — 240240000 ticks; 1439 кадров 60000/1001 —
240073166 либо 240073167 ticks. Округлённая длительность в секундах не годится.
Проверяется целое число кадров, общая граница 50000 video+audio packets,
не более часа и существующие byte/cache/Cluster bounds. До complete граница
видео консервативно отступает на 1 ms от проверенной сетки кадров.

Требования к VP9 и Opus сохранены из [SDK 1.98](ProgressiveContinuousWebmOpus.md):
p0/8 и p2/10 bit 4:2:0, indexed superframes с hidden inter frames,
Opus 48 kHz mono/stereo, 20 ms/960 samples, pre-skip 312, gain 0, mapping 0,
SeekPreRoll 80 ms. `audio_samples` задаёт точную независимую длину аудио.
Сохраняются packet payload, PTS/DTS, CodecDelay и DiscardPadding.
Finite и unknown-size Clusters запечатываются структурно; исходник не меняется.

## Проверка и оставшиеся границы

[Отчёт проверки](ProgressiveContinuousWebmOpusCfrValidation-2026-11-14.json)
разделяет native worker, ordinary-main API и PCM captures каждой редакции.
Пример [manifest](../tests/fixtures/progressive-v2/continuous-webm-opus-cfr.json)
содержит метаданные; start.videoRate и audioSamples передаются отдельно.
Fixtures синтетические и воспроизводимые; они не заменяют реальные загрузки.

VFR, другие fps/Opus packet geometries, one-packet Opus, полная R01/R15/R17
матрица и физическое измерение A/V остаются открытыми. Harvester не изменён.
Wayland и отложенное оборудование не проверяются.
