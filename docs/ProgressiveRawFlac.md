# SDK 1.63: недокачанный самостоятельный файл FLAC

Новый профиль `standalone-flac` принимает один растущий native `.flac` resource
с одним required audio track. Он дополняет [FLAC в fMP4](ProgressiveStandaloneFlac.md).
Harvester не изменяется. Для подключения нужны сборки плеера из source commit SDK;
обычная поддержка завершённых FLAC-файлов не подтверждает этот API.

Перед start проверить `schema2Playback.standalone.rawFlac`: `version=1`,
`mode=standalone-raw-flac-v1`, `standalone-flac` в `profiles`. Использовать
`kind=audio`, `codecHint=flac`, `containerHint=flac`, `required=true` и один
`source.resourceId`. Пример: `progressive-v2-raw-flac.json`. Пустые ranges в
примере означают ожидание данных; writer должен публиковать настоящие SHA-256
подтверждённых блоков, а не подставлять фиктивные хеши.

Поддерживаются mono/stereo, 16/24 bit, 44100/48000 Hz, fixed blocking strategy
с равными min/max block size в STREAMINFO. Нужны известная VOD duration,
timebase 1/10000000, timestampOffset=0 и непрерывный подтверждённый prefix.
Начальная физическая длина может быть неизвестной. External init, segments,
segmentCatalog и live не включены. Неподдерживаемые параметры отклоняются.

Парсер следует структуре [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html):
проверяет `fLaC`, первый STREAMINFO, metadata lengths, frame headers/номера,
block/rate/channels/depth, структуру subframes и residual, alignment и обе CRC.
Предусмотрены constant/verbatim/fixed/LPC subframes, Rice/escape и stereo
assignments. Entropy reconstruction остаётся штатному декодеру. Пропуск битов
не заменяет CRC или проверку SHA-256 физического ресурса.

Ограничения: до 128 metadata blocks и 1 MiB metadata, до 1 MiB на frame,
до 250000 frames; initial prefix и каждое приращение — не более 16 MiB.
Полностью полученные metadata types 1–6 пропускаются при построении decoder
view; их извлечение в интерфейс, включая обложки, этим этапом не добавлено.
Незавершённый metadata block ожидает initialization; незавершённый последний
frame ожидает update и не попадает в доступный индекс. После complete
неполный frame, лишние данные или несовпадение числа сэмплов являются ошибкой.

Для общих backend создаётся виртуальная однодорожечная fMP4-проекция по
[Xiph FLAC-in-ISOBMFF](https://raw.githubusercontent.com/xiph/flac/master/doc/isoflac.txt).
Служебные boxes находятся в памяти, compressed frames читаются из исходного
ресурса через ResourceSource с SHA-256. STREAMINFO переносится без изменения.
Нет перекодирования, копии всего медиа, фиктивного видео и записи в оригинал.
Status сообщает `mediaMode=standalone-flac`, `audioContainer=flac`,
`decoderContainer=fmp4` и `rawFrames`; decoder byte counters относятся к
виртуальному потоку и поэтому не равны размеру native FLAC-файла.

Pause, подтверждённый seek, chapters, ожидание tail, update, complete,
natural EOF и release используют существующие команды и readerId.
Длительность сверяется с STREAMINFO, если total samples известен; при complete
сверяется также фактически разобранное число сэмплов. Порог буфера не изменён.

[Отчёт трёх редакций](ProgressiveRawFlacValidation-2026-11-01.md) проверяет
packet hashes, clocks и точное PCM относительно независимого исходного сигнала.
Это доказательство сохранности данных; физический аудиовыход и прочая приёмка
из [общего чеклиста](ProgressiveCompletionChecklist.md) остаются отдельными.
Переменный block strategy, multichannel, остальные rates/depths, MP3/WAV/ADTS,
live и реальная интеграция Harvester этим профилем не заявляются.

SDK ZIP содержит клиент, пример и документацию, без EXE/DLL/установщиков и медиа.
