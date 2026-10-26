# Progressive HEVC в fMP4

SDK 1.14 расширяет schema-2 медиапрофиль во всех трёх редакциях FoxicMP.
YouTubeHarvester не изменялся. H.264/AAC остаётся совместимым; новые возможности
определяются capabilities текущего EXE, а не наличием нового Python SDK.

## Поддержанный профиль

Одна отдельная required HEVC-видеодорожка и одна required AAC-LC-аудиодорожка,
каждая в собственном fMP4-ресурсе. HEVC Main 8-bit и Main10 10-bit, 4:2:0,
sample entry hvc1 или hev1, четырёхбайтовые NAL lengths. Полные VPS/SPS/PPS должны
уже находиться в head moov/hvcC; для hvc1 их массивы отмечены complete.
Изменение codec configuration в ходе одного поколения не поддерживается.

Сохраняются границы прежнего v2-профиля: VOD, известная duration, timebase
1/10000000, неотрицательный timestampOffset, непрерывный подтверждённый префикс,
head initialization не более 1 МиБ. Обе физические дорожки могут быть недокачаны.
Старт/возобновление/буферизация используют общую A/V-границу и параметры 10/5/1 с.

В track передаётся containerHint=fmp4, codecHint=hevc для видео и aac-lc для звука.
Пустой codecHint допускает определение по реальному sample entry. Противоречие
непустой подсказки фактическому формату отклоняется с unsupported_media.
Остальная структура снимка — как в [первом медиапрофиле v2](ProgressiveMediaV2.md).

## Capabilities и совместимость

schema2Playback дополняется, сохраняя прежний mode как совместимый default:

```json
{
  "mode": "separate-fmp4-h264-aac-lc",
  "profiles": ["separate-fmp4-h264-aac-lc", "separate-fmp4-hevc-aac-lc"],
  "videoCodecs": ["h264", "hevc"],
  "hevcProfiles": ["main", "main10"],
  "hevcBitDepths": [8, 10],
  "hevcSampleEntries": ["hvc1", "hev1"]
}
```

В status, включая завершение, mediaMode отражает фактический профиль;
videoCodec равен hevc либо h264. Общий верхнеуровневый videoCodecs по-прежнему
описывает старый v1-путь; новые кодеки надо искать внутри schema2Playback.
Schema 1/legacy не получают HEVC автоматически. Python V2PreviewSession использует
прежний интерфейс publish/heartbeat/close; менять transport или делать Win32-вызовы
загрузчику не требуется. Старые EXE продолжают отклонять новый профиль.

## Путь данных

Общий Fmp4V2Bridge проверяет sample entry, self-contained dref и структуру hvcC:
версию, поддержанный профиль/chroma/bit depth, reserved bits, длины и типы NAL,
наличие VPS/SPS/PPS. Зашифрованные entries, внешние ссылки, multilayer и
неподдержанные конфигурации отвергаются. Это ограниченная проверка framing;
полный разбор/декодирование SPS и видеопотока выполняет существующий backend.

Общий FragmentIndex принимает HEVC только после явного допуска из v2-моста.
Он сохраняет offsets, sizes, PTS/DTS и sync flags из подтверждённых фрагментов.
Qt передаёт байты существующему libmpv/FFmpeg MOV demuxer. Classic сохраняет
MP4Splitter и свой DirectShow decoder. Новый собственный декодер, remux,
перекодирование и повторная сетевая загрузка не добавлены.

Формат HEVCDecoderConfigurationRecord и различие hvc1/hev1 сверялись с первичными
исходниками FFmpeg: [HEVC config writer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/hevc.c)
и [MOV muxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c).

## Что этим этапом не закрывается

Не добавлены VP9/AV1/Opus, WebM/MKV, muxed A/V, audio-only/video-only,
универсальный demux/remux, сегменты HLS/DASH, live и применение metadata v2.
Main10 decoding не означает HDR passthrough или 10-bit вывод: Qt software
renderer по-прежнему имеет собственные ограничения формата вывода.
Наличие профиля не гарантирует realtime 4K на любом CPU/GPU.

Reader leases, отмена, защита подтверждённых диапазонов и release сохраняются.
Полная криптографическая integrity-модель payload не добавлена.
[Сценарии и границы проверки](ProgressiveHevcValidation-2026-10-26.md).
