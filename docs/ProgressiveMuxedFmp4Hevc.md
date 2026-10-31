# HEVC в одном растущем fMP4

SDK 1.47 расширяет [shared fMP4](ProgressiveMuxedFmp4.md) профилем
`growing-muxed-fmp4-hevc-aac-lc` в Classic, Windows Qt и Linux Qt.
Используется один локальный SHA-256 resource и две виртуальные track views;
плеер не перекодирует и не создаёт промежуточных media-файлов.

## Согласование

Нужна точная capability
`schema2Playback.muxedPlayback.fmp4.hevc.mode = growing-muxed-fmp4-hevc-aac-lc`.
Поддержка отдельных/сегментных HEVC, общий `muxed` и прежний H.264 mode
этого не разрешают. `V2PreviewSession` проверяет capability, codec pair,
shared scalar форму и SHA до start/update и изменения last/uncertain.

В schema 2 у двух required tracks одинаковые `containerHint: fmp4-muxed`
и `source: {resourceId: mux}`. Video имеет явный `codecHint: hevc`, audio —
`aac-lc`. Без video hint остаётся прежний H.264 profile; actual codec должен
совпасть с hint. Пример — `progressive-v2-muxed-fmp4-hevc-vod.json` в SDK.
IDs/duration и сведения о реально записанных bytes заменяются writer.
Начальный пример с пустыми ranges не объявляет данные готовыми.

Остальные условия прежние: известная положительная VOD duration, timebase
1/10000000, нулевой timestampOffset, ровно один resource и непрерывный
подтверждённый префикс. Длина может быть неизвестна до complete. Нет external
init, сегментных source/catalog, optional tracks, live или смены configuration.

## Поддержанный HEVC

- Main/Main10, 8/10-bit 4:2:0, single-layer, `hvc1` или `hev1`.
- Один sample entry и `hvcC` версии 1, four-byte NAL length.
- Ограниченные массивы VPS/SPS/PPS; разрешены проверенные prefix/suffix SEI.
  Для `hvc1` parameter-set arrays обязательно имеют completeness flag.
- Проверяются reserved bits, profile/chroma/depth, типы/размеры NAL arrays,
  их framing и заголовки; elementary stream декодирует штатный backend.
- Сохраняются исходные samples, PTS/DTS, B-frames, fractional rates и
  поддержанные edit lists. Звук — существующий AAC-LC mono/stereo profile.

Ограничения MP4 структуры и размеров соответствуют shared AVC profile:
head ftyp/moov, пустые nonfragmented tables, два trak/trex и два traf на
каждый moof, default-base-is-moof, explicit first trun offset, полное
непересекающееся покрытие mdat и непрерывный decode clock.

## Ожидание и ошибки

Неполный moov или первый fragment даёт `waiting_initialization` с освобождением
временного reader. При downloading неполные moof/mdat ждут update; index
и доступные samples публикуются только после полного fragment обеих дорожек.
Явный complete с оборванным fragment — ошибка, а не EOF.

Несовпадение actual codec и hint или повреждённый hvcC отвергается с очисткой
lease. Отклонённая metadata до admission сохраняет старый index/чтение;
ошибка уже принятых media/source закрывает обе проекции. Опубликованные bytes
не переписываются. Отмена/release используют прежний last-owner протокол.

## Проверка и границы

Native suite `progressive-mux-hevc-mp4-native` сравнивает оригинал с virtual
tracks: packet SHA, PTS/DTS, decoded video и PCM. Main10 сравнивается в
`yuv420p10le`, без понижения глубины. Проверяются произвольные byte cuts,
immutable prefix, backward reads, один physical resource/lease, codec/config
rejection и сохранение старого чтения при отказе metadata.

Qt suite `progressive-mux-hevc-mp4-playback` проверяет реальное декодирование,
меняющиеся кадры, формат 10-bit, AAC/rate/channels и writer/API lifecycle.
Classic использует безопасную отдельную копию с
`--mux-hevc-mp4-playback-only`, без изменения установленного профиля и shell.
[Фактические результаты трёх редакций](ProgressiveMuxedFmp4HevcValidation-2026-10-31.md).

Этап не добавляет AV1/VP9/Opus в shared fMP4, обычный MP4 с moov в хвосте,
произвольный CMAF, live/config changes или multi-audio. Физическая A/V sync,
многочасовая нагрузка и реальные сайты остаются отдельными пунктами приёмки.
Harvester не меняется. SDK ZIP не содержит новые EXE: их надо собрать из тега.
