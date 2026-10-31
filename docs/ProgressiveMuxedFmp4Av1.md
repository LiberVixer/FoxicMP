# AV1 в одном растущем fMP4

SDK 1.48 добавляет `growing-muxed-fmp4-av1-aac-lc` в Classic, Windows Qt и
Linux Qt. Это расширение [shared fMP4](ProgressiveMuxedFmp4.md): один локальный
SHA-256 resource, два виртуальных track views, исходные mdat без перекодирования
или промежуточных media-файлов. Прежние H.264/HEVC modes сохраняются.

## Согласование

Нужна точная capability
`schema2Playback.muxedPlayback.fmp4.av1.mode = growing-muxed-fmp4-av1-aac-lc`.
Separate/segmented AV1, shared WebM AV1/Opus, shared HEVC и общий `muxed`
этого не разрешают. Python `V2PreviewSession` проверяет mode, codec pair,
shared scalar форму и SHA до start/update и изменения last/uncertain.

Schema 2: ровно два required tracks с одинаковыми `containerHint: fmp4-muxed`
и `source: {resourceId: mux}`; video имеет явный `codecHint: av1`, audio —
`aac-lc`. Без video hint сохраняется прежний H.264 profile; actual codec
обязан совпасть с hint. Пример — `progressive-v2-muxed-fmp4-av1-vod.json`
в SDK. Writer заменяет IDs/duration и публикует сведения о реальных bytes/SHA;
пустые ranges в начальном примере не объявляют данные готовыми.

## AV1 и структура MP4

- AV1 Main, Main tier, 8/10-bit 4:2:0, sample entry `av01`.
- `av1C` marker/version 1, bounded low-overhead configuration OBU framing;
  до 128 OBUs, первый и единственный Sequence Header обязателен.
- Разрешены только проверенные sequence/metadata configuration OBUs,
  слой 0, согласованный profile. Неправильные flags/varint/size, High profile,
  12-bit, monochrome, неподдержанные chroma и presentation delay отвергаются.
- Проверка framing не заменяет декодирование elementary stream: результат
  libmpv/DirectShow остаётся обязательной частью проверки поддержки.
- Исходные packet bytes, PTS/DTS, fractional rates и поддержанные edit lists
  сохраняются. Звук — существующий ограниченный AAC-LC mono/stereo profile.

Условия прежнего shared MP4: head ftyp/moov, ровно два trak/trex, пустые
nonfragmented sample tables, self-contained dref, default-base-is-moof,
explicit first trun offset, два traf на moof и полное непересекающееся
покрытие mdat. Нужны known-duration VOD, timebase 1/10000000, нулевой offset,
один resource и непрерывный подтверждённый префикс. Длина может быть неизвестна
до complete. Нет external init, source segments/catalog, live, optional tracks
или смены конфигурации. Тестовый writer использует delay_moov, чтобы FFmpeg
успел включить Sequence Header в av1C; это подготовка fixture, не переделка
файла внутри плеера.

## Ожидание и ошибки

Неполные initialization/первый A/V fragment дают `waiting_initialization`
с освобождением временного reader. Новые samples видны только после полного
moof/mdat обеих дорожек; неполный хвост при downloading ждёт update.
Даже весь записанный файл без explicit complete не означает EOF.

Отклонённая metadata до admission сохраняет прежний index/bytes. Ошибки
принятых media/source, включая SHA rewrite, закрывают обе проекции.
Codec hint и configuration при update не меняются. Отмена/release используют
прежний last-owner protocol. Пределы shared fMP4 остаются прежними:
read 64 KiB, init/moof по 1 MiB, mdat/append по 16 MiB, 8192 fragments,
2 миллиона index samples на дорожку. Это admission bounds, не измеренный
результат предельной нагрузки.

## Проверки и границы

`progressive-mux-av1-mp4-native` сравнивает original и virtual tracks:
packet SHA, PTS/DTS, decoded video/PCM, arbitrary cuts, immutable prefix,
backward reads, lease lifetime и ошибочные конфигурации. Main10 сравнивается
в yuv420p10le. `progressive-mux-av1-mp4-playback` проверяет реальные Qt
кадры/кодеки, 10-bit, AAC/rate/channels и lifecycle writer/API; Classic —
безопасной отдельной копией с `--mux-av1-mp4-playback-only`.
[Фактические результаты](ProgressiveMuxedFmp4Av1Validation-2026-10-31.md).

Этот этап не добавляет VP9/Opus в shared fMP4, произвольный CMAF, MP4 с moov
в хвосте, live/config changes или multi-audio. Реальные сайты, многочасовая
нагрузка и физическая A/V sync остаются в [чеклисте](ProgressiveCompletionChecklist.md).
Harvester не меняется. SDK ZIP содержит клиент/примеры/документацию;
новые EXE собираются из соответствующего тега.
