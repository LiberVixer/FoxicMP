# Progressive WebM: отдельные VP9 и Opus дорожки

Расширение schema 2 плеера для `separate-webm-vp9-opus`. Публичные Python
методы SDK и протокол v1 сохраняются. YouTubeHarvester не изменяется.
Перед запуском загрузчик проверяет `capabilities.schema2Playback.profiles`
текущего EXE; номер приложения 0.2.0 сам по себе не подтверждает поддержку.

## Профиль

- Два отдельных локальных ресурса: один обязательный video `V_VP9`, один audio
  `A_OPUS`. В обеих дорожках `containerHint: "webm"`. Можно опустить codecHint;
  ошибочный непустой hint отклоняется после чтения настоящего заголовка.
- VP9 Profile 0/8-bit или Profile 2/10-bit, 4:2:0, первый пакет — keyframe.
  Для определения доступного времени видео требуется DefaultDuration.
- Opus mono/stereo, mapping family 0, 48 kHz, OpusHead version 1.
  CodecDelay согласован с PreSkip, SeekPreRoll равен 80 мс.
- Известная VOD-длительность, timebase 1/10000000, неотрицательный
  timestampOffset; без live/window, внешнего init и списка segments.
- DocType `webm`, один Segment и TrackEntry на ресурс. Info и Tracks идут до
  Cluster. Segment может иметь неизвестный размер; Cluster — известный.
  Lacing, шифрование, ContentEncodings и CodecState не поддерживаются.
- Инициализация ограничена 1 MiB, кластер/последующий элемент — 16 MiB,
  индекс — двумя миллионами пакетов на дорожку. Неизвестные варианты
  отклоняются, а не подменяются воспроизведением готового файла.

Ключ `opusEditListRequired` в прежних capabilities относится к fMP4-профилям.
В WebM используются CodecDelay и SeekPreRoll, MP4 edit list отсутствует.

## Вызов

```python
from progressive_v2 import V2PreviewSession

# snapshot — полный schema-2 снимок с writer/timeline/resources/tracks.
snapshot['tracks'][0].update(containerHint='webm', codecHint='vp9')
snapshot['tracks'][1].update(containerHint='webm', codecHint='opus')
assert 'separate-webm-vp9-opus' in client.request({
    'version': 1, 'action': 'capabilities'
})['schema2Playback']['profiles']
preview = V2PreviewSession(client, session_directory, title='Предпросмотр')
preview.publish(snapshot)
```

Загрузчик публикует только физически записанные подтверждённые диапазоны.
Индекс учитывает лишь полностью записанные кластеры; незавершённый кластер
ожидает дальнейшей публикации. При complete неполный последний элемент —
ошибка. Ожидание подтверждённого конца во время скачивания не становится EOF.
Инициализацию после публикации изменять нельзя.

Сохраняются закреплённая OS-идентичность ресурса, переименование открытого
файла, reader leases, отмена и filesClosed/release acknowledgement. Метаданные
и главы из WebM в интерфейс progressive этим этапом не переносятся.

## Реализация и проверка

Общий ограниченный EBML/Block-индекс используется тремя редакциями. Qt
передаёт исходные байты через ResourceSource в libmpv/Matroska demuxer;
Classic выбирает штатный MatroskaSplitter и выдаёт пакеты подтверждённого
индекса через DirectShow. Обычные файлы и v1 MP4 сохраняют прежние пути.
Перемотка Opus сохраняет пакеты для 80 мс preroll.

`tests/harvester/test_webm_index.py` сравнивает SHA-256 каждого payload,
размер, PTS и sync с FFprobe. Matroska packet pos у FFprobe указывает на
Block, поэтому совпадение байтов проверяется хешем полезной нагрузки.
Временные метки Opus допускают округление CodecDelay до 1 мс у FFprobe.

Правила времени: [WebM Container Guidelines](https://www.webmproject.org/docs/container/),
[Matroska timestamps](https://www.matroska.org/technical/notes.html),
[CodecDelay/DiscardPadding](https://www.matroska.org/technical/elements.html).

Это ограниченный профиль, а не универсальный WebM/MKV demux/remux. AV1/VP8
в WebM, Vorbis, произвольный MKV/Ogg, muxed-файлы, неизвестный размер Cluster,
полная payload integrity, реальные источники Harvester и измерение A/V-дрейфа
остаются отдельными этапами. Wayland и физическое оборудование не проверяются.
