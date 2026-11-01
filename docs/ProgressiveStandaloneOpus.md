# SDK 1.58: самостоятельное Opus-аудио в растущем fMP4

Расширение `standalone-fmp4-v1` для Linux Qt, Windows Qt и Windows Classic.
Перед публикацией проверить наличие `standalone-fmp4-opus` в
`schema2Playback.standalone.profiles`. Наличие общего `audioCodecs: ["opus"]`
не подтверждает поддержку отдельной дорожки. Старые AAC-LC/H.264 профили и v1
остаются совместимыми.

Используется [контракт standalone](ProgressiveStandalone.md): одна required
аудиодорожка, один scalar resource, `containerHint: "fmp4"`,
`codecHint: "opus"`. Пример метаданных: `progressive-v2-standalone-opus.json`.
Нужны известная длительность VOD, нулевой timestampOffset, непрерывный
подтверждённый префикс и SHA-256. Начальная длина файла может быть неизвестной.
Второй поток, URL, декодер или фиктивная видеодорожка не создаются.

Поддержаны Opus sample entry, dOps version 0, mono/stereo, mapping family 0,
частота медиачасов и декодирования 48 kHz. Единственный unit-rate edit list
обязателен: media start равен dOps.preSkip. Исходные dOps, gain и pre-skip
не переписываются. Неподдерживаемые параметры и несовпадающий codecHint
отклоняются до открытия декодера; reader lease после отказа не остаётся.
При перемотке применяется существующий Opus preroll 80 ms.

Поток запускается после обычного порога буфера. Неполный fragment ждёт
публикации, а не превращается в EOF. Pause, seek в подтверждённой области,
возобновление, расширение ресурса, complete и естественное освобождение reader
работают через существующий API. После complete можно перемотать к любому
подтверждённому месту ресурса, сохраняя идентичность reader.

Тесты используют настоящие mono/stereo Opus-файлы по 20 секунд, частную
аудиосистему Qt, native индексы и точное сравнение байтов. Classic проверяется
отдельно через реальный DirectShow graph и native команды. Физические
аудиовыходы не измеряются. WebM/Vorbis, live, самостоятельные сегментированные
источники и прочие codec/container combinations этим этапом не добавлены.
Harvester не изменяется. Итоги — в
[отчёте](ProgressiveStandaloneOpusValidation-2026-11-01.md).
