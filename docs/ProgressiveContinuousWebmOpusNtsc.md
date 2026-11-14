# SDK 1.99: continuous WebM VP9/Opus, 30000/1001 fps

Отдельный режим `continuous-webm-vp9-opus-ntsc-v1` расширяет R01 на одну
проверенную дробную частоту кадров. Linux Qt, Windows Qt и Classic используют
общую проверку часов; прежний `continuous-webm-vp9-opus-v1` остаётся ровно25fps.
Harvester, defaults и пользовательские профили не изменены.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_NTSC_MODE
preview = ContinuousRemux(client, absolute_session_directory,
    mode=WEBM_VP9_OPUS_NTSC_MODE, audio_samples=1152000)
preview.prepare(snapshot, 'media')
# Публикации/heartbeat и явный play — как в режиме SDK1.98.
```

Требования Opus/VP9, SHA-256 resources, два обязательных tracks, finite/unknown
Clusters и packet/sample/output bounds сохраняются из
[SDK1.98](ProgressiveContinuousWebmOpus.md). Разрешены VP9p0/8-bit и p2/10-bit,
включая indexed hidden inter frames, Opus48k mono/stereo20ms/pre-skip312.
Точное `audio_samples` по-прежнему обязательно; оно не выводится из видео.

## Временная модель

- Частота — **30000/1001**, а не округлённые29.97 либо30fps. Модель задаётся
  явным именем режима; дополнительных start-полей или автоматического выбора нет.
- WebM TimestampScale равен1000000ns; video DefaultDuration —33366666 либо
  33366667ns. Проверяется содержимое файла, а не codec/container hint.
- Для кадра `n` исходный timestamp должен быть ближайшим целым миллисекундным
  значением `n*1001/30`. При половинном остатке допустимы оба направления.
  Например:0,33,67,100,133,167ms. Первый кадр имеет timestamp0.
- `timeline.duration` — floor либо ceil в100ns от `N*10010000000/30000`.
  Из этой точной величины восстанавливается целое число кадров; пустые,
  неточные и выходящие за packet limit длительности отклоняются.
  Для720 кадров это **240240000 ticks =24.024s**. Значение24s не подходит.
- До complete доступная граница видео вычисляется по полностью проверенным
  кадрам с запасом1ms на округление. При complete публикуется полный endpoint.
  Округление PTS не принимается за отсутствующие байты либо разрыв аудио.

Исходные PTS/DTS, DefaultDuration, packet payload, CodecDelay и DiscardPadding
сохраняются. Нет ускорения/замедления, преобразования в25fps или перекодирования.
Кодеки и декодеры те же, что в SDK1.98; Classic сохраняет свой Opus PCM adapter.

## Проверка и границы

[Результаты трёх редакций](ProgressiveContinuousWebmOpusNtscValidation-2026-11-14.json)
содержат finite/unknown Cluster cases, p0/p2/short audio, exact packet clocks,
decoded frames и PCM, ordinary-main playback≥10s до complete, pause/growing/queued
seek/EOF/release. Отдельно проверяются прежние25fps и AAC modes, отрицательные
clock/timeline cases, SDK capability typing и ASan/UBSan.

Пример metadata: [continuous-webm-opus-ntsc.json](../tests/fixtures/progressive-v2/continuous-webm-opus-ntsc.json).
One-packet Opus, другие frame rates/VFR/Opus geometries, полные R01/R15/R17
и физическое A/V измерение остаются открытыми. Wayland и оборудование исключены.

[Linux обновлён атомарно с backup и неизменным INI; три installed own-SDK профиля прошли](ProgressiveContinuousWebmOpusNtscDelivery-2026-11-14.json). Windows установки сохранены.
