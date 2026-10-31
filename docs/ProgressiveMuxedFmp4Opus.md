# Opus в одном растущем fMP4

SDK 1.50 добавляет в Classic, Windows Qt и Linux Qt четыре пары:
`growing-muxed-fmp4-h264-opus`, `growing-muxed-fmp4-hevc-opus`,
`growing-muxed-fmp4-av1-opus`, `growing-muxed-fmp4-vp9-opus`.
Используется [shared fMP4](ProgressiveMuxedFmp4.md): один локальный SHA-256
resource, два виртуальных track views, исходные mdat без перекодирования
и промежуточных media-файлов. Прежние AAC-LC modes/defaults сохраняются.

## Согласование

Нужны обе части capability:
`schema2Playback.muxedPlayback.fmp4.opus.mode = growing-muxed-fmp4-opus`
и точная video/audio pair в `opus.profiles`. Separate/segmented Opus,
shared WebM Opus и общая muxed-галочка этого не разрешают. Opus child mode
не требует объявлять прежние AAC siblings. Python `V2PreviewSession`
проверяет пару, shared scalar форму и SHA до start/update и изменения
last/uncertain state.

В schema 2 ровно два required tracks с одинаковыми `containerHint: fmp4-muxed`
и `source: {resourceId: mux}`. Явные hints: video `h264|hevc|av1|vp9`, audio
`opus`. Без audio hint выбирается прежний AAC-LC profile: actual Opus под
этим hint отвергается. Для Opus video hint обязателен даже для H.264.
Пример — `progressive-v2-muxed-fmp4-opus-vod.json` в SDK. IDs/duration и
сведения о реальных bytes/SHA задаёт writer; пустые ranges не объявляют
данные готовыми.

## Opus и время

- Sample entry `Opus`, dOps версии 0, record ровно 11 bytes,
  channel mapping family 0, mono/stereo.
- Sample entry объявляет 16-bit sample size и 48 kHz; media clock mdhd —
  48000. Информационный original input rate в dOps сохраняется и не
  подменяет частоту декодирования Opus.
- Нужен один media edit с unit rate; media_time равен dOps pre-skip.
  Проверяются версия/размер edit list и отсутствие неоднозначных edits.
- Pre-skip/edit и signed output gain сохраняются в виртуальном header.
  Начальные timestamps, priming и конечные packet durations не исправляются
  неявной нормализацией. Decoder получает прежний штатный Opus path.
- Framing не заменяет entropy decoding. Неподдержанные configuration,
  многоканальные mappings и неправильные sample-entry/clock/edit поля
  отвергаются до публикации reader.

Видеопрофили прежние: H.264, HEVC Main/Main10 8/10-bit hvc1/hev1, AV1 Main
8/10-bit av01, VP9 Profile 0/2 8/10-bit vp09 — каждый в своих проверенных
границах. Поддержаны исходные B-frames, fractional rates и соответствующие
edit lists; тестовые encoder fixtures используют delay_moov. Для Opus
в fixture отключено принудительное смещение отрицательных timestamps,
чтобы сохранить исходные pre-skip и edit list. Плеер FFmpeg не запускает.

## Структура, ожидание и ошибки

Остальные условия shared MP4 прежние: head ftyp/moov, два trak/trex,
пустые nonfragmented sample tables, self-contained dref, default-base-is-moof,
explicit first trun offset, два traf на moof и полное непересекающееся
покрытие mdat. Нужны known-duration VOD, timebase 1/10000000, нулевой offset,
один resource и непрерывный подтверждённый префикс. Длина может быть неизвестна
до complete. Первые samples начинаются около нуля: audio preroll не более
одной секунды. Нет external init, segments/catalog, live, optional tracks
или смены конфигурации.

Неполные initialization/первый A/V fragment дают `waiting_initialization`
с освобождением временного reader. При downloading неполный tail ждёт update;
даже весь записанный файл без explicit complete не означает EOF. Samples
публикуются только после полного moof/mdat обеих дорожек.

Отклонённая metadata до admission сохраняет старое чтение. Ошибки принятых
media/source, включая SHA rewrite, закрывают обе проекции. Hint/configuration
при update не меняются. Отмена/release сохраняют last-owner protocol.
Admission bounds: read 64 KiB, init/moof по 1 MiB, mdat/append по 16 MiB,
8192 fragments, 2 миллиона index samples на дорожку; это не измеренная
предельная производительность.

## Проверки и границы

`progressive-mux-opus-mp4-native` проверяет четыре пары, mono/stereo,
8/10-bit, fractional rates, оригинальные packet SHA/PTS/DTS и decoded
video/PCM, partial publication, immutable prefix, leases и dOps/edit rejection.
Raw 10-bit сравнивается в yuv420p10le. Дополнительно проверяется сохранение
нестандартного согласованного pre-skip, input rate и output gain в header.
`progressive-mux-opus-mp4-playback` проверяет Qt; Classic использует
безопасную отдельную копию с `--mux-opus-mp4-playback-only`.
[Фактические результаты](ProgressiveMuxedFmp4OpusValidation-2026-10-31.md).

Этап не добавляет произвольный CMAF, MP4 с moov в хвосте, live/config changes,
multi-audio, произвольные layouts/rates или universal demux/remux.
Реальные сайты, многочасовая нагрузка и физическая A/V sync остаются в
[чеклисте](ProgressiveCompletionChecklist.md). Harvester не меняется.
SDK ZIP содержит клиент/примеры/документацию; новые EXE собираются из тега.
