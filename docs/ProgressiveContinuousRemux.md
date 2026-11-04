# Непрерывный remux: внутренний движок и управляющий слой

Внутренние компоненты общего progressive-remux. Приложение остаётся 0.2.0, публичный
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
  ASan отдельно освобождён от address-space cap. Управляемый родитель теперь
  запускает Windows worker в Job Object: память 1 GiB и kill-on-close, только
  три разрешённых стандартных handle. Native test проверяет реальные Job flags.

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
интеграции Harvester. Сам worker не проверяет владельца, ranges/SHA и reader
leases; этим занимается описанный ниже управляющий слой через ResourceSource.

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

## Управляющий слой — 4 ноября 2026

`shared/progressivestreamremux.h`: общий `StreamRemuxSession` с отдельным потоком.
Он владеет ResourceSource и дочерним Process; UI/API к нему пока не подключены.
На входе допускается один video-only MP4 track, H.264/пустой codec hint,
SHA-protected local resource с известной неизменной длиной до 128 MiB.
Unknown length, отсутствие integrity и дополнительные audio tracks отклоняются
до spawn. Actual codec/audio/B-frames дополнительно проверяет сам FFmpeg worker.

Публикации проходят Parse/Advance и ResourceSource::publish. Session/generation,
writer, resource identity, clock и ранее опубликованные SHA blocks не меняются;
revision растёт. Ошибочная публикация не заменяет рабочую. По подтверждённым
ranges читаются только SHA-проверенные bytes; физический неопубликованный хвост
не доступен. Для нехватки диапазона родитель удерживает заблокированный AVIO
worker, без перезапусков и пустых resume. Complete исходника подтверждается
отдельно, включая случай, когда все bytes уже доступны до writer complete.

Heartbeat относится к уже привязанному writer/session; его вызывает будущий
аутентифицированный API adapter. Сейчас это внутренний вызов владельца, новый
публичный heartbeat endpoint не создан. Успешная публикация также обновляет
heartbeat. Default — 30 s, общий lifetime — до 1 h, один pipe exchange — 10 s.
Старый snapshot transport сохраняет 4 s. Лимиты раздельны: пять секунд source
wait не становятся зависшим pipe exchange. Перенос в long-running режим
выполняется только явным параметром; старые demux/remux вызовы его не получают.

Во время ожидания проверяются cancellation, writer/resource failures, heartbeat,
lifetime, ResourceSource и жив ли child. Найден и исправлен дефект первой версии
родителя: после kill worker источник продолжал ждать публикацию. Сохранённый
failed-before-fix log и последующие измерения входят в
[отчёт управляющего слоя](ProgressiveContinuousRemuxParentValidation-2026-11-04.json).

Write frames имеют последовательные offsets, chunk ≤64 KiB, output ≤64 MiB.
Родитель сверяет итоговые bytesRead/bytesWritten и собственный SHA-256 с result,
проверяет source complete, pinned source и реальный exit 0. Ошибка sink не даёт
успешного result. Дочерний процесс уничтожается/дожидается раньше закрытия
источника; `done` публикуется после закрытия media handles. Квитанции остаются
консервативными при ошибке записи registry; native tests отдельно подтверждают
`readerStatus.released` с `reason=files-closed` перед readerForget.

**Граница этапа:** выход передаётся в ограниченный внутренний sink, собственного
cache/decoder store здесь ещё нет. В тестах sink — частный файл внутри временной
папки. Код sink вызывается синхронно; pipe deadline не прерывает произвольный
заблокированный callback или зависший filesystem/registry. До подключения
штатного store этот контракт нельзя выдавать за готовую публичную операцию.
`filesClosed` относится к оригиналу; decoder-output readers ещё отсутствуют.
Пятисекундное ожидание и короткие fault tests не заменяют stress/latency приёмку.

По 17 сценариев: Linux GCC, Windows MinGW, Windows MSVC и Linux ASan/UBSan.
Один PID/context, heartbeat через несколько публикаций, полный fragment до
complete, immutable packet/frame hashes и PTS/DTS, physical rename с отравленным
старым именем, недоступность хвоста, invalid generation/revision, writer error,
heartbeat loss, lifetime, rewrite/SHA/truncation, worker kill, sink failure после
частичного выхода, operation stall/partial frame/bad offset и cancel в receive.
Три source admission negatives проверяются до spawn. Все временные fixtures
удаляются; для намеренных fault code `data` запрещён, result пуст.

Повторены прежние transport, ResourceSource, demux/remux native проверки. После
изменения общего transport пересобраны все три плеера; прежний bounded remux
проверен через их обычные API: 24 preparation checks и 20 preview checks на
редакцию. Это проверка совместимости, **не непрерывное воспроизведение**.
Linux установлен атомарно с backup, личные INI/файлы сохранены. Windows
установленные EXE/INI, HKCU registration и Harvester не меняются.

## Следующий обязательный этап

1. Собственный ограниченный cache/output store, parsing полных fragments,
   original + decoder reader receipts; очистка после фактического filesClosed.
2. Подключение выходного fMP4 к существующим Qt/Classic bridges и общему status,
   без restart playback при каждом append. Отдельная подготовка, waiting и EOF.
3. Публичное согласование capability/SDK и проверки через обычный API всех трёх
   редакций. Затем AAC trim, B-frames и остальные обязательные профили.

Ни один из этих пунктов не объявляется выполненным наличием внутренних
компонентов. Новая capability и новый SDK release не объявлены; экспериментальный
stream worker не установлен. Дисковые меню, оборудование и Wayland не возобновляются.
