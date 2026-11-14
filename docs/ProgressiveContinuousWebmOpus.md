# SDK 1.98: continuous WebM VP9/Opus

Player-only режим `continuous-webm-vp9-opus-v1` подключён к Linux Qt,
Windows Qt и Windows Classic. YouTubeHarvester не изменён. Прежние modes,
профили, defaults и IPC endpoints сохраняются. Это ограниченный новый профиль,
а не завершение всей матрицы контейнеров/аудио ТЗ.

## Вызов

```python
from progressive_continuous import ContinuousRemux, WEBM_VP9_OPUS_MODE

preview = ContinuousRemux(client, absolute_session_directory,
    mode=WEBM_VP9_OPUS_MODE, audio_samples=1152000)
preview.prepare(snapshot, 'media')
# Writer публикует только записанные SHA-256 ranges, revisions и heartbeat.
preview.publish(next_snapshot)
# После status()['playbackReady'] — отдельное явное решение пользователя:
preview.play(title='Предпросмотр загрузки')
# После прекращения просмотра:
preview.close(timeout=2)
preview.forget(timeout=2)
```

`audio_samples` — точное число слышимых PCM-сэмплов **на канал**, после
pre-skip/end trim. Для прямого IPC это строка `audioSamples` команды
`continuousRemuxStart`. Поле обязательно только в новом mode и входит в
неизменяемую идентичность запроса. Его нет в schema-2 manifest. Например,
1152000 означает 24 секунды при 48000 Hz. Нельзя получать эту величину из
длительности видео: аудиодорожка может быть короче. Если точное число ещё
неизвестно, этот профиль выбирать нельзя. Отдельный пример manifest:
[continuous-webm-opus.json](../tests/fixtures/progressive-v2/continuous-webm-opus.json).

## Допустимая область

- Один локальный append-only SHA-256 resource, известная длина ≤128 MiB,
  две обязательные дорожки `video/vp9` и `audio/opus`, обе `containerHint=webm`.
- VOD с нулевым timestamp offset, timebase 1/10000000; видео ровно 25 fps,
  фиксированные coded/render размеры, VP9 Profile0/8-bit или Profile2/10-bit,
  4:2:0. Разрешены проверенные indexed superframes с hidden inter frames,
  одним последним displayed frame, без show-existing.
- Opus: 48000 Hz, mono/stereo, mapping family0, gain0, pre-skip312,
  CodecDelay6500000 ns, SeekPreRoll80000000 ns, пакет ровно960 samples/20 ms.
  Сохраняются исходные packet clocks с миллисекундным округлением WebM.
- `648 < audio_samples <= 48000000`; не менее двух audio packets.
  Число пакетов равно ceil((312+audio_samples)/960). Конечный DiscardPadding
  обязан точно соответствовать этому плану; пропуск целого пакета не маскируется.
- Unknown-size Segment, конечные и unknown-size Clusters. Без lacing,
  invisible/reserved block flags, encrypted/external tracks и смены конфигурации.
  Video — SimpleBlock, audio — SimpleBlock либо проверенный BlockGroup.
- Общий максимум50000 packets, init≤1 MiB/32 elements, Cluster≤16 MiB,
  ≤8192 Clusters, ≤32768 top-level elements, ≤65536 children/Cluster.
  Приватный output/shared cache≤64 MiB; writer heartbeat30s, lifetime≤1h.

10/5 ms packets, другие pre-skip/gain/mapping, fractional video clocks,
Vorbis/AV1/VP8 в этом mode и одиночный Opus packet отклоняются. One-packet
trim имеет различающееся поведение FFmpeg runtimes; он остаётся открытым
отдельным профилем. Это не запрет обычного воспроизведения этих файлов.

## Кластеры, кэш и декодеры

Обработчик читает EBML структурно, с ограниченным буфером и одним Cluster
в памяти. Unknown-size Cluster закрывается следующим подтверждённым
заголовком Segment child либо явным writer-complete; совпадение байтов
внутри payload и физический EOF границей не считаются. Меняется только
size VINT Cluster, его ширина сохраняется. Payload, timestamps, OpusHead,
CodecDelay, SeekPreRoll и BlockGroup/DiscardPadding не перекодируются.
Старые SeekHead/Cues не используются для доступа к неопубликованным байтам.

Общий нативный WebM index строит две виртуальные однодорожечные WebM views
поверх того же защищённого cache. Decoder admission требует обеих дорожек.
Qt читает их через libmpv callbacks; контейнерный Opus trim сохраняется.
Classic использует Matroska splitter и внутренний Opus decoder. Только для
этого continuous source его PCM adapter получает raw window
`[312+target,312+audio_samples)`, отключает автоматический повторный skip
libavcodec и обрезает до пользовательских фильтров. Внутренний handshake
`IProgressivePcmControl` version2 предназначен только для Opus48k;
version1/MP3 и независимый AAC control не расширены.

Доступный аудиодиапазон вычисляется по проверенным последовательным
960-sample packets, с консервативным запасом одного ms до завершения audio.
Миллисекундные зазоры PTS не превращаются в ложную остановку покрытия.
Короткая полностью индексированная audio view может закончиться раньше
видео; общий source EOF требует writer-complete. Никакого сетевого fallback.

[Результаты проверок](ProgressiveContinuousWebmOpusValidation-2026-11-14.json)
разделяют структурный worker, настоящие ordinary main и PCM consumer.
File/null/private PulseAudio не являются физическим измерением A/V drift.
