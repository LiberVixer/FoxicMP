# Непрерывный remux: внутренний потоковый движок

Первый компонент общего progressive-remux. Приложение остаётся 0.2.0, публичный
контракт SDK — 1.81. **Обычный API трёх плееров ещё не подключён к этому движку.**
Новая capability не объявляется; компонент не устанавливается в пользовательские
папки и не включён в опубликованные установщики/SDK ZIP. Harvester не меняется.

## Что реализовано

`foxicmp-remux-stream-worker` держит один FFmpeg demux/mux context через несколько
публикаций входа. Родитель передаёт подтверждённые локальные bytes по запросам
`offset/length`. При нехватке worker ждёт следующую публикацию внутри AVIO read,
сохраняя разбор контейнера и позицию. Источник не открывается заново, контексты
не склеиваются перезапусками RAM-клипов.

Выход — последовательные порции fMP4 через собственный non-seekable AVIO.
Родитель получает ftyp/moov и полноценные moof/mdat до complete исходника;
неполную последнюю порцию должен хранить до готовности полного фрагмента.
Worker не накапливает весь выход в одном RAM buffer и не знает путей входа,
выхода или URL. Контейнерные опции фиксированы; дополнительные FFmpeg I/O
запрещены callback `io_open`.

Copy-remux использует `avcodec_parameters_copy`, `av_packet_rescale_ts` и
`av_interleaved_write_frame`, без encoder/filter/resampler. Пакеты, PTS/DTS и
duration сверяются отдельно; новое независимое обнуление timestamps отсутствует.
Фрагментация следует keyframes, с empty_moov/default_base_moof/negative_cts_offsets.
Основание — [документация FFmpeg MOV/MP4](https://ffmpeg.org/ffmpeg-formats.html#Fragmentation).

Общий loader FFmpeg вынесен в `tools/progressive_demux_runtime.h`. Существующий
`foxicmp-demux-worker` сохраняет прежние диагностику и bounded MP4-clip protocol,
лимиты и публичные capabilities. Два worker — разные EXE и разные протоколы.

## Первый проверяемый профиль и пределы

- Один H.264 video stream, без audio и B-frame reorder, presentation start 0.
- Обычный head-index MP4, известная неизменная длина. Проверки — 24 s, 160×90,
  25 fps и 30000/1001 fps, соответственно 600 и 720 packets.
- Вход и совокупное чтение — до 128 MiB, выход — до 64 MiB, максимум 50000 packets.
  Один packet — не более 16 MiB; extradata — до 1 MiB, read/write chunk — 64 KiB.
- Входной управляющий JSON/ACK — до 4096 bytes, ответ с read data — до 140000.
  Входная строка читается с ограничением памяти до разбора JSON.
- Clock deadline worker — один час, проверяемый на I/O/packet границах. Чтение stdin
  само по себе блокирующее: принудительная остановка — обязанность родителя.
- Linux: child-local CPU limit 30 s, address space 1 GiB, 64 file descriptors;
  ASan отдельно освобождён от address-space cap. Windows Job Object/память и
  управляемый spawn будущего штатного родителя ещё предстоит подключить.

Пределы — верхние границы, а не гарантия всех сочетаний размеров и времени.
Для AAC нельзя просто перенести этот режим: сначала требуется сохранить
priming/end trim и синхронизацию. Аудио и B-frame источники получают типизированный
отказ до выдачи выхода. Старый bounded remux с AAC и B-frames сохраняется.
HEVC/VP9/AV1, другие layouts, live, unknown length и полный codec matrix здесь
не заявлены.

## Внутренний диалог

Первое сообщение:

```json
{"mode":"continuous-h264-video-v1","formatHint":"mp4","length":"443235","complete":false}
```

`io=read` запрашивает offset/length, родитель отвечает data+hex, waiting-range,
подтверждённым eof либо точной ошибкой. При waiting-range worker публикует
`io=wait`, после чего родитель отвечает resume+complete или cancelled.
Прежние `complete=true` и известная длина не отзываются.

`io=write` содержит следующий offset и hex. ACK обязателен, offset строго
последовательный. Это backpressure без неограниченной выходной очереди внутри
worker. Родитель обязан реально дождаться новых данных/отмены; бесконечные
пустые resume в тестах не считаются ожиданием.

Даже если MP4 sample tables уже дали конец пакетов, worker ждёт explicit writer
complete. Только после него дописывается trailer и возвращается sourceEof=true.
Ответ eof до подтверждённого конца, ошибка integrity или отмена не становятся
успешным завершением. Отправленные ранее bytes остаются только незавершённым
выходом; будущий родитель должен управлять их состоянием и очисткой.

Это **частный протокол дочернего компонента**, не IPC FoxicMP и не инструкция
интеграции Harvester. Здесь нет самостоятельной проверки владельца исходника,
ranges/SHA и reader leases: будущий родитель должен использовать ResourceSource.

## Проверки и наблюдаемость

[Результаты и точные exit codes](ProgressiveContinuousRemuxValidation-2026-11-03.json).
Native test: `progressive-stream-remux-worker`; общий источник теста —
`tests/progressive_stream_remux_worker_test.py`.

Проверяются два полных выхода с независимым ffprobe/FFmpeg: каждый encoded packet
и decoded frame неизменен, PTS/DTS/duration — в пределах одного output tick.
Четыре промежуточных prefix publications разрезают входные packets, первые
32 read replies короткие. Полные выходные fragments доступны до complete.
Один PID/context переживает ожидания; отдельная проверка удерживает его дольше
старого четырёхсекундного snapshot deadline.

Отмена проверяется в input wait и write ACK, принудительное завершение при wait
должно занимать менее 2 s. Проверяются unconfirmed EOF, integrity error,
отказ audio и B-frames. Child exit 1 и -9 Linux/1 Windows для намеренных ошибок
и kill отделяются от exit 0 тестового драйвера.

Windows DLL FFmpeg транзитивно загружает GIO. Трассировка обнаружила его стартовую
фоновую загрузку дерева ассоциаций: CPU около одного ядра на первые две секунды,
потом CPU и handles стабилизируются. Это не цикл AVIO ожидания; поведение
соответствует [инициализации AppInfo в GIO DllMain](https://mail.gnome.org/archives/commits-list/2020-November/msg04911.html).
Неподтверждённые изменения линковки, опробованные при диагностике, не сохраняются.

Тест разделяет этот startup и steady waits: прогрев не более 20 s, два
последовательных секундных окна с CPU delta ≤0.05 s и ростом handles ≤2.
Затем исходный порог каждого измеряемого wait — CPU delta ≤0.1 s;
длительность ожиданий проверяется по monotonic clock, суммарно больше 4 s.
Все startup samples сохраняются. Это не измерение публичных startup/resume
порогов или длительный stress; первоначальная неудачная попытка idle-измерения
и trace остаются отдельными свидетельствами.

## Следующий обязательный этап

1. Штатный асинхронный родитель: ResourceSource, immutable session identity,
   ranges/SHA, heartbeat/writer failures, bounded spawn/cancel и lifetime worker.
2. Собственный ограниченный cache/output store, group reader receipts для
   оригиналов и выходных readers; очистка только после фактического filesClosed.
3. Подключение выходного fMP4 к существующим Qt/Classic bridges и общему status,
   без restart playback при каждом append. Отдельная подготовка, waiting и EOF.
4. Публичное согласование capability/SDK и проверки через обычный API всех трёх
   редакций. Затем AAC trim, B-frames и остальные обязательные профили.

Ни один из этих пунктов не объявляется выполненным наличием native worker.
Установленные приложения и личные профили не меняются; их EXE уже совпадают
с предыдущей проверенной версией. Новый компонент не является новой поставкой
Linux-плеера. Дисковые меню, оборудование и Wayland не возобновляются.
