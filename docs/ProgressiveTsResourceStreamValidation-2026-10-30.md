# Проверка TS ResourceSource/staging — 30 октября 2026

Проверен [внутренний виртуальный поток](ProgressiveTsResourceStream.md).
Плееры и SDK 1.38 не обновлялись: engine/IPC integration ещё впереди.

| Среда | Итоговые сценарии | Exit code |
|---|---|---|
| Linux GCC, CTest | 26 stream + 65 transport + 74 access/index | 0 |
| Windows MinGW, CTest | Те же 26 + 65 + 74 | 0 |
| Windows MSVC, native | 26 stream | 0 |
| Linux ASan/UBSan с leak detection | 26 stream, без ошибок sanitizers | 0 |

26 stream-сценариев проверяют init readiness, готовый префикс с дырой,
добавление сегментов, waiting против EOF, byte reads через границы и обратно,
AVC/AAC encoded/raw sample hashes, сборку PES больше 64 KiB, seek и отмену.
Отдельно проверены отказ неверного metadata-кандидата с сохранением читаемости,
фатальные SHA/codec ошибки нового ресурса, замена/усечение/перезапись
вытесненного файла и неизменность caller buffer при ошибке через границу.

При удержании второго владельца release не закрывает его файлы; последний
владелец закрывает ResourceSource и убирает квитанции. Ошибка конструктора
также не оставляет читателей. Catalog-сценарий содержит 242 ресурса в семи
страницах, временно отсутствующую новую страницу с успешным retry, backward
чтение и максимум восемь одновременно открытых resource leases.

В каждом прогоне две реальные локальные трёхсекундные пары FFmpeg:

- AVC Main 25 fps с B-кадрами + AAC-LC stereo 48 kHz: 75 + 142 access units,
  по три media-сегмента на дорожку.
- AVC High 30000/1001 fps с B-кадрами + AAC-LC stereo 44.1 kHz: 90 + 131
  access units, по четыре media-сегмента на дорожку.

Все 438 encoded packet hashes и PTS/DTS сверены с ffprobe. Виртуальные байты
совпадают с init и исходными сегментами. Elementary потоки собираются через
`TsResourceStream::sample`, затем декодируются внешним FFmpeg: hashes сырых
video и PCM совпадают с декодированием исходных TS. Декодеры Classic/libmpv и
физическая A/V sync этим не проверены. Медиа не загружались из сети; генераторы
CPU и выход FFmpeg в файлы/stdout не меняли UI, аудиомикшер или рабочий сеанс.

Первоначальный расширенный real-media тест завершался 1 из-за таблицы
смещений в Python-стенде: ffprobe пропускает `pos` у внутренних AAC кадров PES,
а затем таблица audio была ошибочно использована для video. Стенд исправлен;
все итоговые сборки и прогоны выше завершились 0. Эти сбои не были ошибками
памяти runtime. [JSON с полными результатами, SHA-256 и exit codes](ProgressiveTsResourceStreamValidation-2026-10-30.json).

SHA-256 всех трёх прежних build EXE, установленного Linux EXE и Linux INI
совпали с сохранённым отчётом SDK 1.38. Установленные Windows-плееры,
Harvester и shell registration не затрагивались; планировщик не использовался.
Новую версию приложения этот внутренний модуль пока не образует.

Ограничения: строгий AVC/AAC VOD-профиль с двумя отдельными дорожками,
известной duration и постоянной конфигурацией. Нет player capability, muxed,
live, clock rollover, реальных сайтов, многопоточного stress/ThreadSanitizer,
многочасовой A/V-приёмки и проверки предельной ёмкости индекса. `publish`
выполняет I/O/разбор и при росте префикса копирует индекс; адаптер должен
выполнять его на worker. Metadata/page ошибки до изменения источника допускают
retry; ошибки после `ResourceSource::publish` требуют закрыть и открыть stream.
