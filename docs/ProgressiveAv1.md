# Progressive AV1 Main в fMP4

SDK 1.15 добавляет schema-2 профиль separate-fmp4-av1-aac-lc для Linux Qt,
Windows Qt и Windows Classic. YouTubeHarvester не изменяется.
Сохраняются [H.264](ProgressiveMediaV2.md), [HEVC](ProgressiveHevc.md),
публичный Python интерфейс и независимые установки редакций.

## Поддержанный вариант

Один required video AV1 Main, Main tier, 4:2:0, 8 или 10 bit и один required audio
AAC-LC, каждый в отдельном fMP4-ресурсе. Нужны известная duration, VOD,
timebase 1/10000000, неотрицательный offset, подтверждённый префикс от нуля,
head moov не более 1 МиБ. Для видео codecHint=av1, containerHint=fmp4.
Пустой hint допускает определение кодека; неверный непустой hint отклоняется.

В sample entry av01 нужен av1C версии 1 с Sequence Header OBU в начале configOBUs.
Далее допустимы Metadata OBUs. Их framing ограничен: максимум 128 OBU,
размер — LEB128 до восьми байт и в пределах av1C. Проверяются marker/version,
profile/chroma/bit depth/tier, reserved bits, OBU type/flags/layer, длины и
совпадение profile между configuration и Sequence Header. Зашифрованные entries,
внешние dref, monochrome, 12-bit, прочие profiles/tiers и explicit initial
presentation delay в этом профиле не разрешены.

Полный синтаксис Sequence Header, кадров и соответствие всех его полей av1C
обрабатывает штатный backend. Общий валидатор проверяет ограниченный configuration
framing, а не заменяет декодер. Принятие probe не доказывает декодируемость каждого
последующего пакета или криптографическую целостность payload.

ISO AV1 допускает av1C без Sequence Header. Такой вариант **не входит** в данный
ранний профиль: пустой/короткий config не принимается только на основании hint.
Для тестовых файлов FFmpeg используется delay_moov: header записывается после
появления конфигурации encoder, перед первым moof. Простой empty_moov в используемом
FFmpeg может оставить пустой av1C. Это правило генерации фикстуры; плеер ничего
не перекодирует и не меняет входные файлы.

Разбор av1C/configOBUs сверялся с первичными источниками:
[AOM AV1 ISOBMFF v1.3.0, раздел 2.3](https://aomediacodec.github.io/av1-isobmff/v1.3.0.html#av1codecconfigurationbox-section)
и [FFmpeg AV1 muxer helpers](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/av1.c).
Описанные выше ограничения уже, чем весь стандарт AV1 ISOBMFF.

## Обнаружение и использование

Прежний schema2Playback.mode сохраняет H.264 как совместимый default.
Добавляются:

```json
{
  "profiles": [
    "separate-fmp4-h264-aac-lc",
    "separate-fmp4-hevc-aac-lc",
    "separate-fmp4-av1-aac-lc"
  ],
  "videoCodecs": ["h264", "hevc", "av1"],
  "av1Profiles": ["main"],
  "av1BitDepths": [8, 10],
  "av1SampleEntries": ["av01"],
  "av1SequenceHeaderInConfigurationRequired": true
}
```

В status mediaMode=separate-fmp4-av1-aac-lc и videoCodec=av1.
Верхнеуровневый старый videoCodecs и schema 1/legacy этим не расширяются.
AV1 в FragmentIndex и Classic MP4Splitter допускается только через проверенный
v2-мост; обычные файлы продолжают использовать штатные пути.

Загрузчик использует прежний V2PreviewSession.publish/heartbeat/close и
подтверждает записанные bytes/ranges. Qt использует libmpv/FFmpeg MOV demuxer,
Classic — MP4Splitter и DirectShow. Нет нового decoder, remux, перекодирования,
сети или дополнительного процесса загрузки в плеере.

Отмена, буферизация 10/5/1 секунд, writer/reader identities, переименование
закреплённых файлов, complete/EOF и фактическое release сохраняются.

## Оставшиеся этапы

VP9/Opus, WebM/MKV, muxed A/V, audio-only/video-only, универсальный demux/remux,
сегментные HLS/DASH, live и применение metadata v2 не добавлены.
AV1 10-bit decoding не означает HDR passthrough или 10-bit вывод Qt renderer.
Реальная производительность 4K зависит от CPU/GPU и требует отдельной проверки.
Физическая A/V-синхронизация и настоящий Harvester не квалифицируются этим этапом.

[Проверки трёх редакций, фикстуры и ограничения](ProgressiveAv1Validation-2026-10-26.md).
