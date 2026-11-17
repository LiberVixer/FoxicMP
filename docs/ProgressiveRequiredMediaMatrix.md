# Зафиксированная медиа-матрица progressive-приёмки

17 ноября 2026. Задача **P02.C149.01**, база SDK1.106.
Машиночитаемый источник: [ProgressiveRequiredMediaMatrix.json](ProgressiveRequiredMediaMatrix.json).

Зафиксировано 108 существующих ограниченных профилей: 86 native и 22 continuous-remux.
Остаются 30 именованных входных семейств; к ним применяются 8 сквозных групп и 12 классов отрицательных исходов.
Это строки корпуса приёмки, **не новые задачи реестра**. Каждая привязана к существующим ID.
Новый playback этой работой не реализуется; общие P02.C148.01–03 остаются открытыми.

## Порядок и критерий закрытия

Матрица нужна перед расширением общего demux. Следующий конкретный вход —
**P02.C149.02: обычный M4A/audio-only**, строки M01/M02; первоначально AAC-LC,
затем оставшиеся кодеки строки. Новый codec/profile не закрывает семейство целиком.
Готовность реализации требует всех применимых положительных и отрицательных
строк её владельца на Linux Qt, Windows Qt и Classic отдельно.

Неподдержанный сейчас обязательный профиль остаётся OPEN. Наличие похожего native
профиля, demuxer FFmpeg, probe или воспроизведение готового файла не закрывают строку.
Потерянный обязательный fixture — блокер, а не успешный SKIP. Приёмка final release
и физический A/V выход считаются отдельно.

## Общие проверки каждой положительной строки

- **confirmed-io**: Публикуемые ranges/SHA; partial structures ждут bytes, file size не подтверждение.
- **packet-copy**: Compressed packets/PTS/DTS сохранены; без transcoding и изменения оригиналов.
- **decode-reference**: Независимые frames/PCM, priming/end trim и sample count.
- **incomplete-playback**: Ordinary main каждой редакции: ≥10s до complete; не только готовый файл или probe.
- **timeline-controls**: Pause/start/growing/queued seek, выбранные track IDs и metadata.
- **lifecycle**: EOF/error/cancel/replace/release/filesClosed без ложных terminal states.
- **negative-cases**: Reason class/state проверены отдельно от ok принятия команды.
- **resource-bounds**: Измеренные I/O/cache/memory/handles/thread/latency limits; нагрузка описана.

## Оставшиеся входные семейства

| Строка | Вход | Пункты реестра | Условие |
|---|---|---|---|
| M01 | Обычный M4A AAC | P02.C149.02 | обязательно |
| M02 | Обычный MP4 audio-only | P02.C149.02, P04.C210.04 | обязательно |
| M03 | MOV: именованные основные пары | P02.C149.03 | обязательно |
| M04 | Обычный MP4: оставшиеся структуры | P02.C149.10, P06.C255.04 | обязательно |
| W01 | WebM VP8/AV1 в непрерывном remux | P02.C149.04 | обязательно |
| W02 | WebM Vorbis в непрерывном remux | P02.C149.05 | обязательно |
| W03 | WebM/Opus: не покрытые SDK1.106 layouts | P02.C149.10, P04.C210.02 | обязательно |
| K01 | Matroska: H.264/HEVC и AAC | P02.C149.06 | обязательно |
| K02 | Matroska: оставшиеся перечисленные кодеки | P02.C149.06, P04.C210.04 | обязательно |
| K03 | Matroska: lacing и структуры | P02.C149.07 | обязательно |
| T01 | MPEG-TS: HEVC и дополнительные аудио | P02.C149.08, P06.C256.01 | обязательно |
| T02 | MPEG-2/Part2: предусмотренные локальные входы | P02.C149.08, P04.C238.01, P04.C238.02, P04.C238.03 | предусмотренный локальный/сервисный корпус |
| T03 | Транспортные часы TS | P06.C256.02, P06.C256.03 | обязательно |
| A01 | HE-AAC вне обычного M4A | P04.C210.01, P04.C211.01 | обязательно |
| A02 | Opus: дополнения геометрии и каналов | P04.C210.02, P04.C211.06 | обязательно |
| A03 | Vorbis: setup/rates/layouts | P04.C210.03, P04.C211.03, P04.C211.06 | обязательно |
| A04 | MPEG audio версии | P04.C211.02 | обязательно |
| A05 | MPEG LayerI/II: только подтверждённый корпус | P04.C211.02 | предусмотренный локальный/сервисный корпус |
| A06 | FLAC: дополнительные разрешённые стратегии | P04.C211.04, P04.C211.06 | обязательно |
| A07 | WAV/RF64: PCM/float/layouts | P04.C211.05, P04.C211.06 | обязательно |
| A08 | AC-3/E-AC-3: multichannel и substreams | P04.C210.04, P04.C211.06 | обязательно |
| I01 | Несколько входов remux | P02.C149.09 | обязательно |
| S01 | Расширение конечных HLS/DASH | P07.C262.01, P07.C263.01, P07.C263.02, P07.C263.03 | обязательно |
| I02 | Multi-audio и выбор языка | P09.C275.01, P09.C275.02, P09.C275.03 | обязательно |
| V01 | VFR/offsets/edits и неизвестный конец | P06.C255.01, P06.C255.02, P06.C255.03, P06.C255.04 | обязательно |
| Q01 | Смена качества/configuration | P08.C269.01, P08.C269.02, P08.C270.01, P08.C270.02, P08.C271.01, P08.C271.02 | обязательно |
| U01 | Растущие текстовые субтитры | P10.C282.01, P10.C282.02, P10.C282.03, P10.C282.04, P10.C282.05, P10.C283.01, P10.C283.02, P10.C283.03 | обязательно |
| U02 | Встроенные субтитры | P10.C284.01, P10.C284.02, P10.C284.03, P10.C284.04, P10.C285.01, P10.C285.02, P10.C285.03, P10.C286.01, P10.C286.02, P10.C286.03 | обязательно |
| D01 | Главы и metadata | P11.C290.01, P11.C290.02, P11.C291.01, P11.C291.02, P11.C292.01, P11.C292.02, P11.C292.03, P11.C292.04 | обязательно |
| L01 | Live: локальный writer timeline | P12.C296.01, P12.C296.02, P12.C297.01, P12.C297.02, P12.C298.01, P12.C298.02, P12.C299.01, P12.C299.02 | обязательно |

### Точные входы и варианты

В JSON массивы не образуют произвольное декартово произведение. Например, AC-3
не получает положительный7.1 случай из общего списка AC-3/E-AC-3; MP3 версии
имеют отдельные наборы частот. Допустимость пары и CodecPrivate проверяются по bytes.
Не перечисленная здесь комбинация не объявляется навсегда неподдерживаемой.

**M01. Обычный M4A AAC**

Входы: `{"containers":["m4a"],"audio":["aac-lc","he-aac"],"tracks":["audio-only"]}`.
Варианты: head-moov, published-tail-moov, short-audio, priming-end-padding.

**M02. Обычный MP4 audio-only**

Входы: `{"containers":["mp4"],"audio":["aac-lc","he-aac","opus","flac","mp3","ac3","eac3"],"tracks":["audio-only"]}`.
Варианты: head-moov, published-tail-moov, edit-list, source-identity.

**M03. MOV: именованные основные пары**

Входы: `{"containers":["mov"],"pairs":[["h264","aac-lc"],["hevc","aac-lc"],["mpeg4-part2","aac-lc"]]}`.
Варианты: video-only, muxed-av, head-index, published-tail-index, edit-list, configuration-validation.

**M04. Обычный MP4: оставшиеся структуры**

Входы: `{"containers":["mp4"],"pairs":[["h264","aac-lc"],["hevc","aac-lc"],["vp9","aac-lc"],["av1","aac-lc"]]}`.
Варианты: head-index-beyond-current-bounds, published-tail-index, unknown-final-size, unknown-video-endpoint, track-end-before-session-end.

**W01. WebM VP8/AV1 в непрерывном remux**

Входы: `{"containers":["webm"],"pairs":[["vp8","opus"],["av1","opus"]]}`.
Варианты: video-only, muxed-av, finite-clusters, unknown-clusters, simple-block, block-group, vfr.

**W02. WebM Vorbis в непрерывном remux**

Входы: `{"containers":["webm"],"pairs":[["vp8","vorbis"],["vp9","vorbis"],["av1","vorbis"]]}`.
Варианты: muxed-av, finite-clusters, unknown-clusters, audio-preroll, vfr.

**W03. WebM/Opus: не покрытые SDK1.106 layouts**

Входы: `{"containers":["webm"],"audio":["opus"],"tracks":["audio-only","muxed-vp9-opus"]}`.
Варианты: one-packet-audio, one-presentation-sample, preskip-other-than120or312, nonzero-origin, one-frame-video.

**K01. Matroska: H.264/HEVC и AAC**

Входы: `{"containers":["matroska"],"pairs":[["h264","aac-lc"],["h264","he-aac"],["hevc","aac-lc"],["hevc","he-aac"]]}`.
Варианты: video-only, muxed-av, codec-private, finite-clusters, unknown-clusters, cues-present, cues-absent.

**K02. Matroska: оставшиеся перечисленные кодеки**

Входы: `{"containers":["matroska"],"pairs":[["h264","opus"],["hevc","opus"],["h264","vorbis"],["hevc","vorbis"],["h264","mp3"],["hevc","flac"],["h264","pcm"],["h264","ac3"],["hevc","eac3"],["vp9","opus"],["av1","opus"]]}`.
Варианты: audio-only, muxed-av, preroll, codec-private, timestamps.

**K03. Matroska: lacing и структуры**

Входы: `{"containers":["matroska"],"pairs":[["h264","aac-lc"],["hevc","opus"]]}`.
Варианты: xiph-lacing, fixed-lacing, ebml-lacing, default-duration, block-duration, codec-state, cues-before-after-media, bounded-attachments.

**T01. MPEG-TS: HEVC и дополнительные аудио**

Входы: `{"containers":["mpegts"],"pairs":[["hevc","aac-lc"],["hevc","ac3"],["hevc","eac3"],["h264","he-aac"],["h264","ac3"],["h264","eac3"]]}`.
Варианты: video-only, muxed-av, pes-cross-packet, partial-ts-packet, pmts.

**T02. MPEG-2/Part2: предусмотренные локальные входы**

Входы: `{"containers":["mpegts","matroska","mp4"],"explicitCases":[["mpegts","mpeg2","aac-lc"],["matroska","mpeg2","ac3"],["mp4","mpeg4-part2","aac-lc"],["matroska","mpeg4-part2","mp3"]]}`.
Варианты: video-only, muxed-av, b-frames, configuration-validation.

**T03. Транспортные часы TS**

Входы: `{"containers":["mpegts"],"pairs":[["h264","aac-lc"],["hevc","aac-lc"]]}`.
Варианты: pts-dts-wrap33, discontinuity-signalled, discontinuity-unannounced-reason, seek-across-boundary.

**A01. HE-AAC вне обычного M4A**

Входы: `{"containers":["adts","fmp4","mpegts"],"audio":["he-aac"],"tracks":["audio-only","separate-av","muxed-av"]}`.
Варианты: sbr, parametric-stereo, decoded-sample-clock, priming-end-padding.

**A02. Opus: дополнения геометрии и каналов**

Входы: `{"containers":["ogg","webm","fmp4"],"audio":["opus"],"sampleRates":[48000],"channels":["mono","stereo","5.1","7.1"]}`.
Варианты: mapping-family1, preskip0-and16bit-boundaries, one-packet, chained-ogg, independent-origin, exact-discard-padding.

**A03. Vorbis: setup/rates/layouts**

Входы: `{"containers":["ogg","webm"],"audio":["vorbis"],"sampleRates":[44100,48000,96000],"channels":["mono","stereo","5.1","7.1"]}`.
Варианты: setup-validation, short-long-block-transitions, chaining, preroll.

**A04. MPEG audio версии**

Входы: `{"containers":["mp3"],"audio":["mp3-mpeg1","mp3-mpeg2","mp3-mpeg2.5"],"sampleRates":[8000,11025,12000,16000,22050,24000,32000,44100,48000],"channels":["mono","stereo"]}`.
Варианты: cbr, vbr, xing-info-vbri, lame-gapless, partial-frame.

**A05. MPEG LayerI/II: только подтверждённый корпус**

Входы: `{"containers":["mpegts","matroska"],"audio":["mp1","mp2"]}`.
Варианты: explicit-layer-identification, frame-clock, decoder-availability.

**A06. FLAC: дополнительные разрешённые стратегии**

Входы: `{"containers":["flac","fmp4"],"audio":["flac"],"bits":[16,24],"sampleRates":[44100,48000,88200,96000],"channels":["mono","stereo","5.1","7.1"]}`.
Варианты: fixed-block, variable-block, metadata-limit, frame-crc, unknown-total-samples.

**A07. WAV/RF64: PCM/float/layouts**

Входы: `{"containers":["wav","rf64"],"audio":["pcm-u8","pcm-s16le","pcm-s24le","pcm-s32le","pcm-f32le","pcm-f64le"],"sampleRates":[44100,48000,96000],"channels":["mono","stereo","5.1","7.1"]}`.
Варианты: channel-mask, block-align, ds64-64bit, sparse-ranges, partial-frame.

**A08. AC-3/E-AC-3: multichannel и substreams**

Входы: `{"containers":["ac3","eac3","matroska","mpegts"],"audio":["ac3","eac3"],"sampleRates":[32000,44100,48000],"channels":["mono","stereo","5.1","7.1"]}`.
Варианты: lfe, valid-layout-per-codec, dependent-substream-eac3, partial-syncframe.

**I01. Несколько входов remux**

Входы: `{"containers":["fmp4","webm","mpegts"],"explicitCases":[["fmp4","h264","aac-lc"],["fmp4","hevc","aac-lc"],["webm","vp9","opus"],["webm","av1","opus"],["mpegts","h264","aac-lc"]]}`.
Варианты: separate-resources, audio-video-unequal-write-speed, common-time-origin, one-source-fails, separate-track-eof.

**S01. Расширение конечных HLS/DASH**

Входы: `{"containers":["fmp4","webm","mpegts"],"pairs":[["h264","aac-lc"],["hevc","aac-lc"],["vp9","opus"],["av1","opus"],["vp8","vorbis"],["vp9","vorbis"],["av1","vorbis"]]}`.
Варианты: local-init-media-only, parallel-segments, gaps, discontinuity, rebasing, codec-state, paged-catalog, bounded-handles.

**I02. Multi-audio и выбор языка**

Входы: `{"containers":["mp4","matroska","fmp4-segments","webm-segments"],"audio":["aac-lc","opus"],"tracks":["two-required-languages","optional-late-audio"]}`.
Варианты: stable-track-ids, partial-selected-track, add-remove-track, optional-error-isolation.

**V01. VFR/offsets/edits и неизвестный конец**

Входы: `{"containers":["mp4","mov","webm","matroska"],"pairs":[["h264","aac-lc"],["hevc","aac-lc"],["vp9","opus"],["av1","opus"]]}`.
Варианты: negative-preroll, nonzero-origin, different-timebases, vfr, edit-list, gapless, unknown-length, unknown-endpoint.

**Q01. Смена качества/configuration**

Входы: `{"containers":["fmp4-segments","webm-segments","mpegts-segments"],"pairs":[["h264","aac-lc"],["hevc","aac-lc"],["vp9","opus"]]}`.
Варианты: new-init, safe-keyframe, preserve-position-pause-speed-volume-tracks, waiting-new-init.

**U01. Растущие текстовые субтитры**

Входы: `{"containers":["srt","webvtt","ass","ssa","ttml","json3","srv"],"tracks":["external-text"]}`.
Варианты: utf8-utf16-bom, partial-cue, late-append, no-duplicate-events, timeline-origin.

**U02. Встроенные субтитры**

Входы: `{"explicitCases":[["mp4","mov_text"],["mp4","webvtt"],["matroska","ass"],["matroska","subrip"],["matroska","pgs"],["matroska","dvd-subtitle"]]}`.
Варианты: headers-palettes, language-default-forced, bounded-fonts, styles-preserved-or-loss-declared.

**D01. Главы и metadata**

Входы: `{"containers":["mp4","mov","webm","matroska","local-segments"]}`.
Варианты: container-plus-writer-chapters, stable-id, late-update, unknown-duration, unavailable-chapter, covers, rotation-aspect, hdr-color-metadata.

**L01. Live: локальный writer timeline**

Входы: `{"containers":["hls-local-event","dash-local-dynamic"],"pairs":[["h264","aac-lc"],["vp9","opus"]]}`.
Варианты: unknown-duration, moving-window, target-delay, reconnect, missing-segment-not-eof, writer-end, reader-retirement, seek-outside-window.

## Сквозные сценарии

- **X01 — Подтверждённая запись и структуры**: sequential-prefix, parallel-out-of-order, sparse-hole, partial-header, partial-packet, writer-pause10, writer-pause30, writer-pause60, unequal-audio-video-speeds.
  Реестр: P05.C248.01, P05.C248.02, P05.C248.03, P05.C249.01, P05.C249.02, P15.C335.01, P15.C335.02, P15.C335.03.
- **X02 — Большие индексы и каталоги**: external-digest-index, large-media-index, paged-catalog, bounded-json, bounded-handles, reopen-without-full-scan, rf64-64bit-boundary.
  Реестр: P05.C250.01, P05.C250.02, P05.C250.03, P05.C251.01, P05.C251.02, P07.C265.01, P07.C265.02, P07.C265.03.
- **X03 — Crash, identity и ошибки I/O**: writer-crash, worker-crash, player-crash, disk-full, rename-finalize, identity-matched-resume, identity-mismatch-refusal, multiple-readers, preview-error-does-not-cancel-download.
  Реестр: P13.C308.01, P13.C308.02, P13.C308.03, P13.C308.04, P13.C309.01, P13.C309.02, P15.C335.04.
- **X04 — Latency и UI**: reference-pc-method, ready-start-le3s, ready-resume-le2s, remux-preparation-separate, managed-release-le2s, ui-responsive, bounded-worker-termination, no-audio-device-video-only.
  Реестр: P13.C303.01, P13.C303.02, P13.C303.03, P13.C304.01, P13.C304.02, P13.C304.03, P13.C304.04, P15.C368.01, P15.C368.02, P15.C368.03.
- **X05 — Filesystem и границы безопасности**: local-fixed-drive, unicode-long-path, owner-permissions, current-user-ipc, symlink-reparse, traversal, source-substitution, no-secrets-in-logs.
  Реестр: P13.C321.01, P13.C321.02, P13.C321.03, P13.C321.04, P13.C322.01, P13.C322.02, P13.C322.03.
- **X06 — Общая приёмка и ресурсы**: ordinary-main-all-three, min10s-before-complete, exact-native-exits, all-required-positive-rows, reasoned-negatives, no-unexplained-skip, long-runs, min100-cycles, rss-private-cpu-handles-threads-cache-temp-budgets.
  Реестр: P15.C334.01, P15.C334.02, P15.C334.03, P15.C334.04, P15.C344.01, P15.C344.02, P15.C344.03, P15.C344.04.
- **X07 — Реальные сервисные входы**: youtube, vk, rutube, direct-hls-dash-when-actually-available, extractor-protocol-format-ids, offline-fixtures, no-secret-urls.
  Реестр: P15.C336.01, P15.C336.02, P15.C336.03, P15.C336.04.
- **X08 — Неизменность пользователя и итоговая поставка**: completed-file-regressions, private-profiles, unchanged-user-ini, unchanged-shell-registration, three-independent-installs, same-source-commit, windows-exe-msi, linux-deb, sdk-client-writer-instructions.
  Реестр: P15.C345.01, P15.C345.02, P15.C345.03, P17.C361.01, P17.C361.02, P17.C361.03, P17.C361.04, P17.C361.05.

## Отрицательные исходы

- **N01 / encrypted-source**: DRM/шифрование не расшифровывается.
- **N02 / corrupt-source**: Повреждённые atom/EBML/PES/codec headers.
- **N03 / missing-decoder**: Нет декодера конкретной сборки.
- **N04 / waiting-initialization**: Не опубликован обязательный init.
- **N05 / waiting-index**: Не опубликован обязательный индекс.
- **N06 / waiting-keyframe**: Нет доступной точки декодирования.
- **N07 / waiting-range**: Требуемые bytes/segment ещё не подтверждены.
- **N08 / source-identity-error**: Подмена/усечение/перезапись опубликованных bytes.
- **N09 / resource-limit**: Превышен согласованный RAM/cache/packet/JSON limit.
- **N10 / unsupported-layout**: Вход вне согласованной положительной строки.
- **N11 / writer-error-not-eof**: Ошибка/отмена writer не successful EOF.
- **N12 / post-download**: Документированное ограничение, никогда не positive progressive PASS.

Это классы результата приёмки. Конкретный wire reasonCode берётся из
контракта режима; нельзя переименовывать действующий API ради этой таблицы.

## Отложенное и объективные границы

- **E01 / objective-negative**: drm-decryption, encrypted-source. Не обходить DRM; требуется точный отрицательный результат, а не положительный playback.
- **E02 / deferred-by-user**: physical-av-latency, gpu-hdr-output, receivers, capture-devices, disc-menus, analog-tv. Пользователь отложил аппаратные/дисковые проверки; metadata retention остаётся положительным программным требованием.
- **E03 / excluded-by-user**: wayland. Wayland исключён из текущих проверок.
- **E04 / external-blocked**: real-harvester-events-button, external-python-long-parent-temp. Не менять Harvester/Python/Windows; player-owned API и тестовый writer разрешены.
- **E05 / conditional-admission**: network-filesystem, removable-filesystem. Поддержку не объявлять до отдельных identity/disconnect тестов; обязательная база local fixed drive.
- **E06 / outside-positive-core-not-universal-unsupported**: unlisted-codecs, unlisted-pairs, unlisted-rates-layouts. Не превращать отсутствие строки в вечный отказ валидному формату: новый реальный вход добавляется явной revision/ID/причиной по ТЗ.

## Уже заявленные ограниченные профили

Список ниже сверяется с исходными capabilities, а не с расширениями файлов.
Он сохраняет ограничения отчётов на их дату. Новая общая приёмка не заявляется.
Полный admission continuous-профилей и fingerprints native contracts сохранены в JSON.

| Profile ID | Путь | Декларация редакций | Контракт/отчёт |
|---|---|---|---|
| `growing-muxed-fmp4-av1-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Av1.md](ProgressiveMuxedFmp4Av1.md) |
| `growing-muxed-fmp4-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Opus.md](ProgressiveMuxedFmp4Opus.md) |
| `growing-muxed-fmp4-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4.md](ProgressiveMuxedFmp4.md) |
| `growing-muxed-fmp4-h264-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Opus.md](ProgressiveMuxedFmp4Opus.md) |
| `growing-muxed-fmp4-hevc-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Hevc.md](ProgressiveMuxedFmp4Hevc.md) |
| `growing-muxed-fmp4-hevc-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Opus.md](ProgressiveMuxedFmp4Opus.md) |
| `growing-muxed-fmp4-vp9-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Vp9.md](ProgressiveMuxedFmp4Vp9.md) |
| `growing-muxed-fmp4-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedFmp4Opus.md](ProgressiveMuxedFmp4Opus.md) |
| `growing-muxed-mp4-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveIndexedMp4.md](ProgressiveIndexedMp4.md) |
| `growing-muxed-mpegts-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveGrowingTs.md](ProgressiveGrowingTs.md) |
| `growing-muxed-webm-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedWebmAv1.md](ProgressiveMuxedWebmAv1.md) |
| `growing-muxed-webm-av1-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedWebmVorbis.md](ProgressiveMuxedWebmVorbis.md) |
| `growing-muxed-webm-vp8-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedWebmVp8.md](ProgressiveMuxedWebmVp8.md) |
| `growing-muxed-webm-vp8-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedWebmVorbis.md](ProgressiveMuxedWebmVorbis.md) |
| `growing-muxed-webm-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedWebm.md](ProgressiveMuxedWebm.md) |
| `growing-muxed-webm-vp9-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedWebmVorbis.md](ProgressiveMuxedWebmVorbis.md) |
| `growing-tail-mp4-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveTailMp4.md](ProgressiveTailMp4.md) |
| `segmented-fmp4-av1-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentAv1.md](ProgressiveSegmentAv1.md) |
| `segmented-fmp4-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md) |
| `segmented-fmp4-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentPlayback.md](ProgressiveSegmentPlayback.md) |
| `segmented-fmp4-h264-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md) |
| `segmented-fmp4-hevc-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentHevc.md](ProgressiveSegmentHevc.md) |
| `segmented-fmp4-hevc-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md) |
| `segmented-fmp4-vp9-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md) |
| `segmented-fmp4-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveSegmentVp9Opus.md](ProgressiveSegmentVp9Opus.md) |
| `segmented-mpegts-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveTsPlayback.md](ProgressiveTsPlayback.md) |
| `segmented-muxed-mpegts-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMuxedTsPlayback.md](ProgressiveMuxedTsPlayback.md) |
| `segmented-webm-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWebmSegments.md](ProgressiveWebmSegments.md) |
| `segmented-webm-vp8-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWebmSegments.md](ProgressiveWebmSegments.md) |
| `segmented-webm-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWebmSegments.md](ProgressiveWebmSegments.md) |
| `separate-fmp4-av1-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveAv1.md](ProgressiveAv1.md) |
| `separate-fmp4-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveOpus.md](ProgressiveOpus.md) |
| `separate-fmp4-h264-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMediaV2.md](ProgressiveMediaV2.md) |
| `separate-fmp4-h264-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveOpus.md](ProgressiveOpus.md) |
| `separate-fmp4-hevc-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveHevc.md](ProgressiveHevc.md) |
| `separate-fmp4-hevc-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveOpus.md](ProgressiveOpus.md) |
| `separate-fmp4-vp9-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp9.md](ProgressiveVp9.md) |
| `separate-fmp4-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveOpus.md](ProgressiveOpus.md) |
| `separate-fmp4-webm-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-fmp4-webm-av1-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-fmp4-webm-h264-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-fmp4-webm-h264-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-fmp4-webm-hevc-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-fmp4-webm-hevc-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-fmp4-webm-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-fmp4-webm-vp9-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-webm-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWebMAv1.md](ProgressiveWebMAv1.md) |
| `separate-webm-av1-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-webm-fmp4-av1-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-webm-fmp4-av1-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-webm-fmp4-vp8-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-webm-fmp4-vp8-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-webm-fmp4-vp9-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-webm-fmp4-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMixedContainers.md](ProgressiveMixedContainers.md) |
| `separate-webm-vp8-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-webm-vp8-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `separate-webm-vp9-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWebM.md](ProgressiveWebM.md) |
| `separate-webm-vp9-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveVp8Vorbis.md](ProgressiveVp8Vorbis.md) |
| `standalone-ac3` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveAc3.md](ProgressiveAc3.md) |
| `standalone-adts-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveAdts.md](ProgressiveAdts.md) |
| `standalone-eac3` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveEac3.md](ProgressiveEac3.md) |
| `standalone-flac` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveRawFlac.md](ProgressiveRawFlac.md) |
| `standalone-fmp4-aac-lc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandalone.md](ProgressiveStandalone.md) |
| `standalone-fmp4-av1` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneFmp4Video.md](ProgressiveStandaloneFmp4Video.md) |
| `standalone-fmp4-flac` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneFlac.md](ProgressiveStandaloneFlac.md) |
| `standalone-fmp4-h264` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandalone.md](ProgressiveStandalone.md) |
| `standalone-fmp4-hevc` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneFmp4Video.md](ProgressiveStandaloneFmp4Video.md) |
| `standalone-fmp4-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneOpus.md](ProgressiveStandaloneOpus.md) |
| `standalone-fmp4-vp9` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneFmp4Video.md](ProgressiveStandaloneFmp4Video.md) |
| `standalone-mp3` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMp3.md](ProgressiveMp3.md) |
| `standalone-mp3-gapless` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMp3Gapless.md](ProgressiveMp3Gapless.md) |
| `standalone-mp3-indexed` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveMp3Indexed.md](ProgressiveMp3Indexed.md) |
| `standalone-ogg-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveOggOpus.md](ProgressiveOggOpus.md) |
| `standalone-ogg-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveOggVorbis.md](ProgressiveOggVorbis.md) |
| `standalone-rf64-pcm16` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveRf64.md](ProgressiveRf64.md) |
| `standalone-rf64-pcm24` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveRf64.md](ProgressiveRf64.md) |
| `standalone-rf64-sparse-pcm16` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveRf64RangeMap.md](ProgressiveRf64RangeMap.md) |
| `standalone-rf64-sparse-pcm24` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveRf64RangeMap.md](ProgressiveRf64RangeMap.md) |
| `standalone-wav-extensible-pcm24` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWavExtensible24.md](ProgressiveWavExtensible24.md) |
| `standalone-wav-pcm16` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWav.md](ProgressiveWav.md) |
| `standalone-wav-pcm24` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveWav24.md](ProgressiveWav24.md) |
| `standalone-webm-av1` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneWebmVideo.md](ProgressiveStandaloneWebmVideo.md) |
| `standalone-webm-opus` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneWebm.md](ProgressiveStandaloneWebm.md) |
| `standalone-webm-vorbis` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneWebm.md](ProgressiveStandaloneWebm.md) |
| `standalone-webm-vp8` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneWebmVideo.md](ProgressiveStandaloneWebmVideo.md) |
| `standalone-webm-vp9` | native-progressive | linux-qt, windows-qt, windows-classic | [ProgressiveStandaloneWebmVideo.md](ProgressiveStandaloneWebmVideo.md) |
| `continuous-h264-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousSdk.md](ProgressiveContinuousSdk.md) |
| `continuous-h264-reordered-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveReorderedSdk.md](ProgressiveReorderedSdk.md) |
| `continuous-h264-aac-window-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousAacApi.md](ProgressiveContinuousAacApi.md) |
| `continuous-hevc-reordered-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousHevc.md](ProgressiveContinuousHevc.md) |
| `continuous-hevc-aac-window-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousHevcAac.md](ProgressiveContinuousHevcAac.md) |
| `continuous-av1-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousAv1.md](ProgressiveContinuousAv1.md) |
| `continuous-av1-aac-window-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousAv1Aac.md](ProgressiveContinuousAv1Aac.md) |
| `continuous-vp9-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousVp9.md](ProgressiveContinuousVp9.md) |
| `continuous-vp9-aac-window-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousVp9Aac.md](ProgressiveContinuousVp9Aac.md) |
| `continuous-vp9-superframe-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousVp9Superframe.md](ProgressiveContinuousVp9Superframe.md) |
| `continuous-vp9-superframe-aac-window-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousVp9SuperframeAac.md](ProgressiveContinuousVp9SuperframeAac.md) |
| `continuous-webm-vp9-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmVp9.md](ProgressiveContinuousWebmVp9.md) |
| `continuous-webm-vp9-superframe-video-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmVp9Superframe.md](ProgressiveContinuousWebmVp9Superframe.md) |
| `continuous-webm-vp9-opus-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpus.md](ProgressiveContinuousWebmOpus.md) |
| `continuous-webm-vp9-opus-ntsc-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpusNtsc.md](ProgressiveContinuousWebmOpusNtsc.md) |
| `continuous-webm-vp9-opus-cfr-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpusCfr.md](ProgressiveContinuousWebmOpusCfr.md) |
| `continuous-webm-vp9-opus-vfr-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpusVfr.md](ProgressiveContinuousWebmOpusVfr.md) |
| `continuous-webm-vp9-opus-simple-vfr-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpusSimpleVfr.md](ProgressiveContinuousWebmOpusSimpleVfr.md) |
| `continuous-webm-vp9-opus-simple-vfr-auto-count-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpusAutoVfr.md](ProgressiveContinuousWebmOpusAutoVfr.md) |
| `continuous-webm-vp9-opus-simple-vfr-nominal-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmOpusNominalVfr.md](ProgressiveContinuousWebmOpusNominalVfr.md) |
| `continuous-webm-vp9-opus-simple-vfr-derived-av-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmDerivedAudio.md](ProgressiveContinuousWebmDerivedAudio.md) |
| `continuous-webm-vp9-opus-packet-clock-v1` | progressive-remux | linux-qt, windows-qt, windows-classic | [ProgressiveContinuousWebmPacketOpus.md](ProgressiveContinuousWebmPacketOpus.md) |

## Изменение матрицы

Изменение capability или списка профилей требует явного обновления этой матрицы.
Обязательные строки не удаляются ради зелёного отчёта. Новый формат из реального
YT/VK/Rutube корпуса добавляется revision с причиной и соответствующим ID реестра;
его отсутствие в этой базе не отменяет требование ТЗ обработать новый вход.
Медиа-строка и отдельный повтор теста не создают автоматически новую задачу.
Проверка и генерация: `python3 tools/verify_progressive_media_matrix.py --write`.

Основания: [ТЗ, разделы5–14](ProgressiveTechnicalSpec-2026-10-25.ru.md),
[Matroska Codec Mapping](https://www.matroska.org/technical/codec_specs.html),
[FFmpeg libavformat API](https://ffmpeg.org/doxygen/trunk/avformat_8h.html).
Спецификация контейнера и наличие demuxer сами по себе не доказывают playback FoxicMP.
