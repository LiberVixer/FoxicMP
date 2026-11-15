# SDK1.105: WebM/VP9/Opus без внешних videoSamples/audioSamples

Новый явный режим `continuous-webm-vp9-opus-simple-vfr-derived-av-v1` выводит video
packet count и audio presentation samples из подтверждённых bytes. Внешние
`videoSamples`, `audioSamples`, `videoRate` запрещены. Конечные source length и
video endpoint пока остаются известными. Приложение0.2.0; Harvester не изменяется.

Это production продолжение [подготовки](ProgressiveAudioCountRemuxPlan.md),
отслеживаемое в том же [фиксированном чеклисте](ProgressiveAudioCountExecutionChecklist.md).
Старые known-plan modes сохраняют прежние контракт и проверки.

## Ограниченный профиль

- Один SHA-protected локальный resource ≤128 MiB, unknown-size Segment и
  finite/unknown Clusters. VP9 p0/8-bit либо p2/10-bit420, indexed superframes
  с одним показанным последним кадром, PTS=DTS от нуля, без lacing/reorder.
- Video — SimpleBlock без DefaultDuration. Интервалы1–1000ms выводятся по next PTS,
  последний кадр — по известному endpoint после подтверждённого writer EOF.
- Opus48k mono/stereo, mapping0/gain0/pre-skip312, по960 samples/packet.
  Presentation count = decoded samples −312 −final trim. Минимум2 packets/649
  presentation samples, максимум48000000 samples и50000 video+audio packets вместе.
- Положительный DiscardPadding должен точно соответствовать floor/ceil наносекундам
  обрезки0–959 samples. После ненулевого trim новые audio packets запрещены этим
  профилем. Другие Opus packet durations и one-packet audio не включаются.
- Head ≤1 MiB, Cluster ≤16 MiB, pending ≤32 MiB/32 Clusters, output/cache ≤64 MiB.
  Устаревшие SeekHead/Cues/CRC/Position/PrevSize удаляются при структурной конвертации.

## Worker и независимая проверка кэша

Рабочий scanner удерживает Cluster с последним аудиопакетом до следующего audio
packet либо writer EOF. Проверенный положительный terminal trim снимает это
ожидание для аудио; последний видеокадр всё равно ждёт EOF. Признак конца audio
track не завершает resource или cache и не освобождает video reader.

Worker возвращает `derivedAudioSamples` и `audioPackets` только с окончательным
результатом. Cache независимо проверяет canonical packet bytes/TOC/clock/trim,
пересчитывает число сэмплов и сверяет его с результатом worker. Несовпадение,
ошибка закрытия исходника либо digest mismatch не становятся complete.

Публичные job/playback counters:

| Поле | Значение |
| --- | --- |
| indexedVideoSamples | Число опубликованных video packets, decimal string |
| videoSampleCountFinal | true только при outputComplete |
| indexedAudioSamples | Подтверждённое число presentation samples в опубликованном audio prefix, decimal string |
| audioSampleCountFinal | true только при outputComplete |
| audioTrackEndObserved | Проверен terminal trim либо подтверждён полный output EOF; это отдельный признак от окончательности всего job |

## Декодеры и короткое аудио

Qt использует существующие managed WebM views с исходными OpusHead/CodecDelay и
DiscardPadding. Classic использует отдельный внутренний IID
`IProgressiveGrowingOpusControl`: ограниченная граница PCM продвигается перед
доставкой каждого пакета, без сброса decoded cursor. MP3 v1, фиксированный Opus v2
и AAC handshake не меняются. Preroll/seek используют ту же sample clock; обрезка
не применяется повторно при смене границы.

Если короткая аудиодорожка имеет положительный terminal trim, видео может
продолжаться до полного EOF. Если trim отсутствует/равен нулю, остановку поступления
audio нельзя считать концом дорожки: остаётся waiting до writer EOF/нового пакета.
Длинный такой разрыв может достичь pending queue budget и дать точный отказ.
Режим не отключает required audio и не угадывает его конец по времени ожидания.

Как и в подготовке, целый удалённый packet при новой согласованной длине/bytes
неотличим от намеренно короткой дорожки без независимой writer/source identity.
Прежняя заявленная длина с недостающими bytes проверяется отдельно как truncated
source. Неизвестный endpoint, иные codecs/layouts и полный R01 остаются открытыми.

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_DERIVED_AV_MODE

preview = ContinuousRemux(client, session_directory,
                         mode=WEBM_VP9_OPUS_DERIVED_AV_MODE)
preview.prepare(snapshot, "media")
# Продолжать публикацию SHA-проверенных ranges/heartbeat; play вызывается явно.
```

[Приёмка трёх редакций](ProgressiveContinuousWebmDerivedAudioValidation-2026-11-15.json):
117 worker /30 ordinary main /30 complete PCM /18 growing PCM /624 old worker /
69 old main,145 SDK units и4000 sanitizer mutations. Qt growing PCM сохраняет
обязательное видео с null VO; audio-only test мог корректно закончиться раньше
всего A/V job и не доказывал whole-output EOF. Classic использует private null
video, видимый desktop/physical A/V не аттестованы.
Поставка отмечается отдельно после обновления установленной копии.
