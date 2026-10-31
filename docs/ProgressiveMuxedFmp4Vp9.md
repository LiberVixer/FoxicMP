# VP9 в одном растущем fMP4

SDK 1.49 добавляет `growing-muxed-fmp4-vp9-aac-lc` в Classic, Windows Qt и
Linux Qt. Используется [shared fMP4](ProgressiveMuxedFmp4.md): один локальный
SHA-256 resource, два виртуальных track views, исходные mdat без перекодирования
и промежуточных media-файлов. Прежние AVC/HEVC/AV1 modes сохраняются.

## Согласование

Нужна точная capability
`schema2Playback.muxedPlayback.fmp4.vp9.mode = growing-muxed-fmp4-vp9-aac-lc`.
Separate/segmented VP9, shared WebM VP9/Opus, прежние shared fMP4 modes и
общий `muxed` этого не разрешают. Python `V2PreviewSession` проверяет mode,
codec pair, shared scalar форму и SHA до start/update и изменения last/uncertain.

В schema 2 ровно два required tracks с одинаковыми `containerHint: fmp4-muxed`
и `source: {resourceId: mux}`; video имеет явный `codecHint: vp9`, audio —
`aac-lc`. Без video hint сохраняется прежний H.264 profile. Actual codec
обязан совпасть с hint. Пример — `progressive-v2-muxed-fmp4-vp9-vod.json`
в SDK: IDs/duration и сведения о реально записанных bytes/SHA заменяет writer.
Пустые ranges начального примера не объявляют данные готовыми.

## VP9 и структура MP4

- Profile 0/8-bit либо Profile 2/10-bit, 4:2:0, sample entry `vp09`.
- `vpcC` — FullBox version 1, flags 0, фиксированный record 12 bytes,
  codec initialization data отсутствуют.
- Проверяются profile/depth, допустимый level, chroma subsampling,
  допустимые colour primaries/transfer/matrix и отсутствие RGB matrix при 4:2:0.
- Неподдержанные Profile 1/3, 12-bit, 4:2:2/4:4:4, reserved fields и
  обрезанный/отсутствующий configuration record отвергаются.
- Framing не заменяет декодирование elementary stream: фактический результат
  libmpv/DirectShow остаётся частью проверки поддержки.
- Исходные packet bytes, PTS/DTS, fractional rates и поддержанные edit lists
  сохраняются. Звук — существующий ограниченный AAC-LC mono/stereo profile.

Остальная структура соответствует shared MP4: head ftyp/moov, два trak/trex,
пустые nonfragmented sample tables, self-contained dref, default-base-is-moof,
explicit first trun offset, два traf на moof, полное непересекающееся покрытие
mdat и непрерывный decode clock. Нужны known-duration VOD, timebase 1/10000000,
нулевой offset, один resource и непрерывный подтверждённый префикс.
Длина может быть неизвестна до complete. Нет external init, source segments/
catalog, live, optional tracks или смены конфигурации.

Тестовый encoder использует delay_moov, lag-in-frames 25 и auto-alt-ref 1.
Это подготовка fixture; плеер не переделывает файл на диске и не запускает
FFmpeg для воспроизведения.

## Ожидание и ошибки

Неполные initialization/первый A/V fragment дают `waiting_initialization`
с освобождением временного reader. Samples публикуются только после полного
moof/mdat обеих дорожек. При downloading неполный tail ждёт update;
весь записанный файл без explicit complete не означает EOF.

Отклонённая metadata до admission сохраняет прежний index/bytes. Ошибки
принятых media/source, включая SHA rewrite, закрывают обе проекции.
Codec hint/configuration при update не меняются; отмена/release сохраняют
прежний last-owner protocol. Admission bounds остаются прежними:
read 64 KiB, init/moof по 1 MiB, mdat/append по 16 MiB, 8192 fragments,
2 миллиона index samples на дорожку. Предельные memory/throughput не измерены.

## Проверки и границы

`progressive-mux-vp9-mp4-native` сравнивает original и virtual tracks:
packet SHA, PTS/DTS, decoded video/PCM, arbitrary cuts, immutable prefix,
backward reads, lease lifetime и ошибочные configurations. 10-bit сравнивается
в yuv420p10le. `progressive-mux-vp9-mp4-playback` проверяет реальные Qt
кадры/кодеки, глубину, AAC/rate/channels и writer/API lifecycle; Classic —
безопасной отдельной копией с `--mux-vp9-mp4-playback-only`.
[Фактические результаты](ProgressiveMuxedFmp4Vp9Validation-2026-10-31.md).

Этот этап не добавляет Opus в shared fMP4, произвольный CMAF, MP4 с moov
в хвосте, live/config changes или multi-audio. Реальные сайты, многочасовая
нагрузка и физическая A/V sync остаются в [чеклисте](ProgressiveCompletionChecklist.md).
Harvester не меняется. SDK ZIP содержит клиент/примеры/документацию;
новые EXE собираются из соответствующего тега.
