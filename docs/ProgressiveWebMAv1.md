# Progressive AV1/Opus в WebM — SDK 1.19

Schema 2 расширена профилем `separate-webm-av1-opus` для Linux Qt, Windows Qt
и Windows Classic. YouTubeHarvester не изменяется. Проверять профиль в
`capabilities.schema2Playback.profiles`, а не номер приложения 0.2.0.

Два отдельных ресурса имеют `containerHint: "webm"`: video `V_AV1`, audio
`A_OPUS`. `codecHint` — `av1`/`opus` либо пустой; непустые hints проверяются
по содержимому. Python `V2PreviewSession` и протокол v1 сохранены.

## Границы профиля

- AV1 Main, 8/10-bit, 4:2:0, без monochrome, scalability и presentation delay.
  Требуется DefaultDuration видео; размеры кадра — до 16384 по каждой стороне.
- CodecPrivate содержит AV1CodecConfigurationRecord v1 и один начальный
  Sequence Header OBU, затем допускаются Metadata OBUs. Та же ограниченная
  проверка конфигурации используется прежним fMP4-профилем AV1.
- Первый пакет видео — ключевой Temporal Unit. Он содержит совпадающий с
  конфигурацией Sequence Header OBU и один Frame OBU с KEY_FRAME; запрещён
  show_existing_frame. Поддержан reduced still picture header. Проверяются
  границы OBU, LEB128-размеры, reserved bits и extension layer 0.
- Это проверка конфигурации и начального пакета. Декодирование кадров и полная
  проверка AV1 payload остаются задачей libmpv/DirectShow; переименование
  codecHint не подтверждает поддержку потока.
- Opus mono/stereo, mapping family 0, 48 kHz, согласованные CodecDelay/PreSkip
  и 80 мс SeekPreRoll. Новый сквозной корпус использует stereo.

Остальные [ограничения WebM-профиля SDK 1.18](ProgressiveWebM.md) сохраняются:
известная VOD-длительность, непрерывный подтверждённый префикс от нуля,
известный размер Cluster, один TrackEntry на ресурс, без lacing/encryption,
сегментов, live и применения metadata. Дыры и недокачанный конец не являются
EOF; финальный усечённый кластер отклоняется.

## Реализация и проверка

WebMInitialization проверяет CodecPrivate, общий WebMIndex — начальный AV1
пакет и подтверждённые кластеры. Qt использует ResourceSource и Matroska
demuxer libmpv; Classic — свой штатный MatroskaSplitter и DirectShow-декодер
AV1. Выбор Classic CodecID теперь зависит от реально обнаруженного кодека.
Сохранены прежние VP9/WebM, AV1/fMP4 и остальные fMP4-профили.

Корпус включает AV1 Main 8/10-bit, 320×180, 25 fps, 20 с, отдельное Opus
stereo. Проверяются воспроизведение ≥10 с до complete при двух физически
неполных файлах, переход назад, переименование, завершение и filesClosed.
В Qt проверяются меняющиеся кадры, pixel format декодера и Opus 48 kHz.
Classic проверяется отдельно; результат Qt не выдаётся за проверку DirectShow.

[Результаты, хеши сборок и exit codes](ProgressiveWebMAv1Validation-2026-10-28.md).
Смешанные MP4/WebM-пары, VP8/Vorbis, произвольный MKV, unknown-size Cluster,
несколько дорожек, metadata/live, реальный Harvester и физическое A/V ещё
не закрыты. Установщики 0.2.0 этим SDK не обновляются.

Основание формата: [Matroska V_AV1 mapping](https://www.matroska.org/technical/codec_specs.html#v_av1)
и [AV1CodecConfigurationRecord](https://aomediacodec.github.io/av1-isobmff/).
