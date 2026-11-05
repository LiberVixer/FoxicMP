# Непрерывный remux: внутренний движок и управляющий слой

Внутренние компоненты общего progressive-remux. Приложение остаётся 0.2.0, публичный
контракт SDK — 1.81. **Через IPC трёх плееров подключены внутренние команды
подготовки и просмотра узкого H.264 video-only профиля.**
Новая capability не объявляется; experimental worker не устанавливается в
пользовательские папки и не включён в опубликованные установщики/SDK ZIP.
Внутренние decoder adapters встроены в main EXE; новые команды не объявлены
в публичных capabilities/actions. Для их проверки worker размещается рядом с
тестовым EXE. Harvester не меняется.

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

**Граница первоначального parent-этапа:** выход передавался во внутренний sink;
описанный далее cache тогда ещё не был подключён. В тестах sink — частный файл внутри временной
папки. Код sink вызывается синхронно; pipe deadline не прерывает произвольный
заблокированный callback или зависший filesystem/registry. До подключения
штатного store этот контракт нельзя выдавать за готовую публичную операцию.
`filesClosed` parent относится к оригиналу; output readers появляются в следующем
внутреннем компоненте, отдельно от настоящих decoder adapters.
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

## Ограниченный выходной cache — 4 ноября 2026

`shared/progressivestreamcache.h` добавляет `StreamOutputStore`, его независимых
readers, общий `StreamCacheBudget` и владельца очереди `StreamCacheOwner`. `shared/progressivecachedremux.h` связывает
store с управляющим процессом. Это внутренний компонент: GUI, Qt/libmpv и
Classic/DirectShow к нему ещё не подключены, новой capability в IPC/SDK нет.

Один store создаёт отдельный каталог и `output.fmp4` с доступом только владельцу,
без перезаписи существующего файла. Decoder-style reader получает собственный
pinned file handle и реальную ReaderLease с session/generation оригинала и
физической идентичностью производного ресурса. Пути cache — частная диагностика
владельца/теста, не файловый URL для внешнего декодера или инструкция Harvester.

Append строго последовательный, ≤64 KiB. `written` и `visible` различаются:
частичные заголовки/боксы сохраняются, но readers не получают ftyp отдельно от
полного moov либо moof без целого mdat. Полные пары дополнительно проверяет общий
`harvester::FragmentIndex`: track, clocks, trun/sample bounds, initial keyframe.
Трейлер mfra остаётся скрытым до подтверждённого завершения producer. Нулевые,
неограниченные/неизвестные боксы, orphan mdat и sample за пределами payload
отклоняются. Это фиксированный формат вывода текущего FFmpeg worker, не обещание
поддержки любого произвольного fMP4.

Каждая append-порция имеет SHA-256; при чтении соответствующие порции повторно
сверяются по pinned handle. Подмена, изменение размера/содержимого или потеря
private permissions не превращаются в успешное чтение. При сбое cache связка
останавливает producer, будит ожидающих readers и возвращает причину ошибки.
EOF разрешён только после успешного result родителя, реального source close и
финальной проверки store. До этого граница visible даёт waiting-range, включая
случай, когда физически уже записан неполный следующий fragment.

Пределы компонента:

- До 64 MiB на store и до 64 MiB суммарно в одном общем budget; владелец обязан
  передавать тот же budget всем store своего сервиса. Меньшие budgets допустимы.
  Owner ограничивает число активных/ожидающих удаления store до 16, включая пустые.
- До 8192 append-порций и 50000 samples; metadata/moof ≤1 MiB,
  mdat ≤16 MiB + 16 bytes header; не более 128 reader receipts за жизнь store.
- Память содержит только ограниченные chunks/metadata/index/состояние readers,
  без полной RAM-копии выхода. Числа — ограничения структур, отдельного полного
  RSS/stress бюджета этот короткий native прогон не доказывает.
- Budget резервируется перед записью и возвращается после удаления собственного
  media file. Частично завершившаяся ошибкой запись удерживает резерв до cleanup.
- Windows write ждёт завершения до 2 s с CancelIoEx; native local filesystem и
  receipt I/O не считаются универсально прерываемыми при неисправности ядра/диска.

`retire()` запрещает новых readers, но не удаляет cache при живом producer,
незакрытом источнике или активном output reader. Проверяется registry: исходные
reader IDs и **все** квитанции с той же физической output identity, включая
отдельный процесс с другим session ID. filesClosed подтверждается реальным
закрытием handle; таймеров, предположения по возрасту или ложного release нет.
Read handles закрываются до записи/освобождения leases. Старое имя, под которое
подставили другой файл, не удаляется. Неожиданные посторонние файлы в каталоге
не удаляются рекурсивно.

`StreamCacheOwner` удерживает активные и ожидающие удаления store. При закрытии
managed-сессии store передаётся этому владельцу: внешний reader не оставляет
cache без живого владельца. Очередь повторно проверяется при create/retire и
явном collect, который будущий API adapter должен вызывать на lifecycle/status/
release событиях; фонового периодического таймера нет. Native test закрывает
managed-сессию, пока отдельный процесс держит output, затем подтверждает удаление
и возврат budget через collect после его фактического close. Локальные readers
также удерживают Core после уничтожения непосредственного владельца.

При выходе всего процесса, утрате service owner или ошибке cleanup допускается
**консервативный остаток** частного cache. Ограниченное восстановление журналируемого cache после перезапуска добавлено
следующим этапом ниже; возраст файла не даёт права на его удаление. Этот
ограниченный этап не закрывает всю cache/crash matrix ТЗ.

[Отчёт cache](ProgressiveContinuousRemuxCacheValidation-2026-11-04.json): по 31
сценарию Linux GCC, Windows MinGW, Windows MSVC и Linux ASan/UBSan. Проверены
разрывы внутри boxes, невидимость неполного mdat/trailer, два readers и
cross-process lease, оригинал в retirement group, порча/усечение и path poison,
квоты двух store, 8192 chunks/128 readers, lifetime после owner drop, очередь
retirement с внешним reader и предел 16 store. Настоящий
managed worker выдаёт cache до complete; через reader получен весь output,
600 encoded packet hashes и decoded frame hashes совпадают с исходником,
PTS/DTS сохраняются в пределах одного output tick. Проверены worker crash,
cache corruption во время подготовки, writer error, quota и cancel при
ожидающем reader, без ложного EOF. Disk-full здесь — явно injected producer
failure плюс реальные quota checks; заполнение реального тома не выполнялось.

Прежние 17 parent и 9 worker сценариев Windows/Linux повторены. Main EXE всех
трёх редакций и установленный Linux EXE не менялись; повторной установки новой
версии здесь нет. Linux установленная сборка по-прежнему совпадает с проверенной
сборкой прошлого этапа. Во время проверки её INI изменился при работающем установленном viewer;
отдельная трассировка записавшего процесса не выполнялась. Агент INI не
перезаписывал и не восстанавливал прежние пользовательские значения.
Windows EXE/INI и private Winpthread сохранены, Harvester не изменён.

## Журнал и восстановление после перезапуска — 4 ноября 2026

`shared/progressivestreamjournal.h` добавляет внутренний ownership journal v1.
До первой append-порции store создаёт `journal.next`, записывает/синхронизирует
ограниченный JSON, затем атомарно публикует его как `journal.json`. На Linux
используется renameat2/RENAME_NOREPLACE с fsync каталога; на Windows —
MoveFileEx/WRITE_THROUGH без перезаписи существующего journal. Сам журнал
остаётся locked до конца жизни Core; он не переписывается при каждой порции.

Запись содержит cache/session/generation, PID и OS birth владельца, физические
идентичности каталога, output, самого journal и registry. Для файлов также
сохраняется creation identity: Windows creation FILETIME либо Linux statx BTIME.
Исходные пути, URL, аргументы FFmpeg и произвольные цели удаления не записываются.
Journal принадлежит тому же private каталогу; лимит файла — 16 KiB.
SHA-256 канонического содержимого выявляет повреждение, даже если JSON
остался синтаксически корректным. Это checksum private metadata, не MAC/подпись
против пользователя, который может переписать собственные файлы. Media budget
по-прежнему считает output bytes, bounded служебные файлы учитываются отдельно.

`StreamCacheOwner` удерживает `.service` lock одного владельца на cache root.
Первое create в новом процессе выполняет bounded recovery до выделения нового
store. Участвуют только имена `remux-` + 32 lowercase hex, фиксированные
`output.fmp4`/`journal.json` и явно переданный сервером registry. Корни cache и
registry остаются внутренними параметрами сервиса, новая команда IPC не добавлена.
Ограничения: 16 root на owner, максимум 512 directory entries и 128 кандидатов
за recovery. Граница scan проверяется до изменений; рекурсивного удаления нет.

Для удаления необходимы все подтверждения:

- private local directory и корректный, единственный, неповреждённый journal;
  имена/физические идентичности и creation identities совпадают;
- journal lock свободен; OS подтверждает Exited/ReusedPid владельца, включая
  birth, а не только номер PID. Same/Unknown и занятая блокировка запрещают cleanup;
- registry — тот же физический каталог. Его guard свободен, все квитанции группы
  session/generation и всех readers той же physical output identity released;
- output не заменён, не связан hardlink/reparse/symlink и не вырос сверх лимита;
  неизвестных файлов в leaf-каталоге нет. Journal повторно проверяется под lock.

Сначала удаляется только собственный output, затем журнал и пустой leaf-каталог.
Если процесс уже успел удалить output, но не journal, следующая проверка может
завершить metadata-only cleanup при тех же процессных/lease подтверждениях.
Пустые неотслеживаемые каталоги не удаляются на основании одного имени.
Если journal отсутствует/повреждён/частично опубликован, файл заменён, creation
identity недоступна или proof недостаточен — остаток сохраняется как quarantine.
Возраст не участвует в решении. Активные reader receipts не забываются и не
переписываются recovery; исходные media files никогда не являются целями cleanup.

Непустой непроверенный остаток блокирует новый create с
`cache-recovery-required`, в том числе попытку переключить тот же owner на другой
root. Поэтому новый budget не позволяет игнорировать место старого cache.
После подтверждённого close внешнего reader следующий create повторяет recovery.
Работа с другими, ещё неизвестными этому owner roots и глобальная системная квота
не заявляются; будущий API обязан использовать фиксированные roots своей редакции.

Найден и воспроизведён дефект штатной live-очистки: подмена directory registry
пустым каталогом позволяла удалить cache при живом внешнем reader из прежнего
registry. Теперь journal registry identity проверяется и при live cleanup,
при создании output reader и при admission новой сессии. Failed-before-fix binary
SHA/log/exit 1 и повторный положительный прогон включены в
[отчёт восстановления](ProgressiveContinuousRemuxRecoveryValidation-2026-11-04.json).

Native recovery: Linux GCC/ASan-UBSan — по 33 сценария, Windows MinGW/MSVC —
по 31. Два дополнительных Linux rows проверяют symlinks без запроса Windows
привилегий. Есть реальные kill owner и restart, managed FFmpeg preparation,
выжившие external output/source readers, PID birth, unlocked-but-live owner,
corrupt/missing/oversize/replaced/hardlinked journals, подмена output/registry,
registry contention, scan bound, single-service lock и отказ fresh budget до
закрытия orphan readers. Это native component, не GUI/decoder приёмка.

Повторяются 31 cache, 17 parent и 9 engine сценариев; результаты платформ разделены.
Crash tests сохраняют реальные owner exit -9 Linux / 1 Windows отдельно от exit 0
драйверов. ASan/UBSan покрывает recovery и cache; намеренный SIGKILL не выдаётся за
нормальный выход или sanitizer shutdown. Main EXE, установленные приложения,
Harvester, SDK 1.81 и опубликованные assets этим этапом не изменяются.

Это **удаление доказанно принадлежащих мёртвому owner cache**, не возобновление
незавершённого FFmpeg context или декодирования после crash. Не выполнены
power-loss/неисправный filesystem, полный stress/CPU/RSS бюджет, восстановление
непроверяемого legacy cache без journal или новая пользовательская поставка.
Состояния quarantine требуют отдельной диагностики; автоматическая нормализация
профиля/registry ради успешной очистки запрещена.

## Первые decoder views — 4 ноября 2026

`shared/progressivestreamdecoder.h` добавляет два внутренних представления
подтверждённого output store. Они не открывают `privatePath()` и не принимают
произвольные пути: каждый реальный open получает отдельный native handle и
reader receipt через `StreamOutputStore::Reader`.

- `StreamDecoderCursor`: блокирующее чтение для libmpv, порциями до 64 KiB.
  Нехватка полного фрагмента остаётся ожиданием. Нулевой результат для непустого
  запроса возможен только при подтверждённом EOF; ошибка producer/SHA/reader
  возвращает -1 и сохраняется отдельно. Запрос ошибки не блокируется за read.
- Отмена отдельного курсора — atomic flag, без ожидания его read mutex. Ожидание
  внутри store проверяет отмену через интервалы не более 50 ms. Это граница
  cooperative wait, а не обещание прерывать зависший filesystem syscall.
- Размер неизвестен до успешного `producerClosed`; byte seek ограничен текущими
  полными fragments, с запретом отрицательных/будущих offsets. Это не завершённая
  реализация временной перемотки через пользовательский API.
- `StreamFragmentDecoder`: атомарное обновление штатного Classic `FragmentIndex`
  и SHA-проверенное чтение sample bytes. Владелец сериализует refresh/чтение index;
  этот view ещё не подключён к `CHarvesterSession` и реальному DirectShow graph.

`qt/src/continuousstreams.h` связывает курсоры с настоящими `mpv_stream_cb_*`.
Случайный URI привязан к одному attachment; старый URI/посторонний source не
открывается. `cancel_fn` прерывает reader без libmpv вызовов из callback.
Factory должна жить до возврата `mpv_terminate_destroy`. Detach/замена factory
прерывает только её курсоры, сохраняя независимые readers того же store.
Первая проверка обнаружила ошибочную отмену общего source при закрытии Qt
factory; failing binary/log сохранены, исправление проверяется одновременным
libmpv и fragment reader. Источник может отдельно отменить всю свою группу.

В native fixture `CachedRemuxSession::decoderSource()` подключён к реальному
libmpv. Оригинал H.264 video-only 24 s/25 fps декодируется в 600 PNG через
`vo=image`, затем каждый PNG сравнивается с growing-output вариантом той же ОС
и того же libmpv. Проверяются прямые fragments и полный путь
ResourceSource → один FFmpeg worker → private cache → libmpv, без рестартов
на append. Часть кадров появляется до writer complete, manual pause переживает
публикацию, ранний конец не объявляется, все reader receipts освобождаются
после native close. На том же output параллельно читается Classic fragment view;
для отдельной fMP4 фикстуры проверяются все 600 packet hashes, keyframes и часы
с погрешностью не более 100 ns. Это **не проверка установленного Classic**.

Native test `progressive-stream-remux-decoder`: по 11 сценариев Linux GCC,
Windows MinGW и Windows MSVC; ASan/UBSan проверяет тот же собственный код.
[Отчёт и точные exit codes](ProgressiveContinuousRemuxDecoderValidation-2026-11-04.json).
Windows и Linux PNG сверяются со своим completed-source reference; равенство
изображений разных сборок/ОС не заявлено. Декодирование software/video-only:
звука, A/V sync, GPU, оконного вывода, 30-min/RSS/performance acceptance здесь нет.

Обычные Player/API, `MpvLifetime`, Classic graph и опубликованная capability
этим этапом не меняются. Внешний статус обязан проверять producer/store failure:
одного `MPV_EVENT_END_FILE` недостаточно для признания успешного source EOF.
SDK 1.81, main EXE и установки не менялись; это внутренний decoder-adapter этап.

## Подключение к Player и графу Classic — 4 ноября 2026

Qt `Player::loadContinuousSource` теперь открывает verified cache через обычный
libmpv software-renderer и per-file options. `ContinuousStreams` зарегистрирован
в каждом Player; factory передаётся в `MpvLifetime` и уничтожается только после
native core. Stop/замена/закрытие виджета прерывают её курсоры, не отменяя
независимых readers. Результат `ended(natural)` проверяет producer/store state;
ошибка и незавершённый source не становятся успешным EOF. Эта проверка привязана
к `playlist_entry_id`, чтобы старое end-file событие не приписывалось новой загрузке.

Classic `CHarvesterSession::OpenContinuous` создаёт один H.264 video track из
того же output store. Initialization и первый полный fragment обязательны до
создания графа. `CHarvesterFileReader` читает virtual bytes через подтверждённый
reader, без открытия diagnostic path; обычные v1/v2 readers остаются отдельными.
Snapshot/index обновляются вместе, вне длительной блокировки UI metadata mutex.
Публикации исходника принадлежат managed remux controller, а не этому decoder
adapter. Close session не отменяет shared source; receipt освобождается после
join watcher и закрытия native output handle.

[Проверки](ProgressiveContinuousRemuxPlaybackValidation-2026-11-04.json):

- Linux Qt и Windows Qt: по пять сценариев настоящего `Player` — completion,
  Stop с независимым reader, замена обычным файлом, закрытие виджета и ошибка
  producer. Есть framebuffer, playback до complete, сохранение ручной паузы,
  speed/volume/mute и одного worker, реальный close и released receipts.
  Повторены retirement tests и прежний bounded remux API: по 24 preparation и
  20 playback проверок на ОС. В тесте замены новый файл удерживается на паузе,
  а пути нормализуются: это устойчивое наблюдение результата, не обход проверки.
- Windows Classic: отдельный MFC/DirectShow стенд использует production
  `CHarvesterSession`, MP4 splitter и программный MPC H.264 decoder. Пять графов:
  completed, growing с pause/resume, same-graph seek после complete, Stop и
  producer failure. Все 600 NV12 кадров completed/growing совпали с независимым
  FFmpeg-декодированием оригинала, clocks — в пределах 100 ns. Seek 10 s даёт
  ровно 350 соответствующих кадров; worker/reader не меняются. Аудиоустройство,
  системный display renderer и установленный Classic не запускаются.
- Перед native Classic стендом создан private INI с выключенной регистрацией.
  Все 158 значений команд/иконок установленного Classic в HKCU совпали до/после.
  MSVC builder пересобирает DSUtil, BaseSplitter **и MP4Splitter**, чтобы исключить
  старые inline offsets после изменения layout `CHarvesterSession`.

Все три main EXE пересобраны. Linux обновлён атомарно с rollback EXE; профиль и
263 остальных файла установки сохранены, публичные capabilities прежние.
Windows установки и их профили, Harvester, SDK 1.81 и GitHub release assets не
изменяются. Новый stream worker в пользовательскую папку пока не устанавливается.

Это **внутренние playback entry points**, а не публичный запуск continuous-remux
из Harvester: API controller, асинхронная подготовка/статус, управление producer
и capability согласование ещё впереди. Classic проверен настоящим DirectShow
графом, но не через UI/API установленного приложения. Звук, AAC trim, B-frames,
остальные codec/layout profiles и полная performance/crash приёмка остаются открытыми.

## Общий фоновый контроллер — 4 ноября 2026

`shared/progressivestreamcontroller.h` добавляет общий `ContinuousRemuxController`
для Qt/Classic API adapters. На первом этапе это **внутренний command controller**,
проверенный через native fixture; подключение IPC описано следующим этапом ниже.
Capabilities/SDK остаются 1.81. Harvester не меняется. Код принимает JSON;
filesystem пути cache/registry и executable задаёт владелец сервиса, не запрос.

| Внутренняя команда | Идентификация и действие |
| --- | --- |
| `continuousRemuxStart` | `requestId`, absolute `sessionDirectory`, schema-2 `session`, `resourceId`; немедленно принимает подготовку |
| `continuousRemuxStatus` | Ровно один `jobId` или `requestId`; только опубликованный снимок, без файлового I/O |
| `continuousRemuxUpdate` | `jobId` и новая `session`; проверка Parse/Advance до постановки в очередь |
| `continuousRemuxHeartbeat` | `jobId` + `sessionId/generation/writerInstanceId`; heartbeat реального caller, не внутреннего опроса |
| `continuousRemuxCancel` | Те же fences; запрос отмены без ожидания background I/O/join |
| `continuousRemuxForget` | Те же fences; cancel/retirement и acknowledgement известных receipts после close |

Повтор Start с тем же `requestId` и canonical input возвращает ту же последнюю
задачу; другой input даёт `continuous-conflict`. Новый request требует состояния
`forgotten` прежней задачи. Идемпотентность относится к последней retained job,
а не к бессрочному журналу всех прежних requestId и не к восстановлению playback
после crash. Идентификаторы — 32 lowercase hex; revision/generation/byte counters
передаются decimal strings.

Запрос ограничен 64 KiB. На controller допускается одна retained job, до восьми
ожидающих updates плюс одна выполняющаяся публикация. На процесс — восемь
active/closing job objects и два retirement workers. Source/cache лимиты прежние:
один SHA-protected MP4/H.264 video-only resource, known zero-origin VOD clock до
одного часа, input 128 MiB, output 64 MiB, no transcode/audio/B-frame expansion.
Проверки фактического codec/packets остаются в FFmpeg worker.

`acceptedRevision` отделена от `appliedRevision`, как и принятый/применённый
heartbeat sequence. `preparing/waiting/ready/complete` описывают producer/cache:
`playbackReady` разрешает получить внутренний decoder source, **не означает**
запуск GUI/декодера; `complete` не означает окончание пользовательского просмотра.
`outputComplete` требует настоящего успешного EOF/finalization, `sourceFilesClosed`
и `released` — разные proofs. Нехватка файла может оставаться ожиданием до его
появления и публикации. Недостающие bytes не заменяются размером файла/нулями.

Job thread выполняет pin, remux, SHA/ranges publication, snapshot capture, cache
retirement/recovery и receipt acknowledgement. Status читает immutable view;
admission mutex не удерживается при I/O. Cancel/Forget/деструктор не вызывают
producer/native close на caller: process-owned retirement удерживает job и
root fence до настоящего закрытия output readers. Один service owner переживает
successive jobs, исключая повторное выделение budget через новую root fence.
`StreamCacheOwner::settle` не считает нулевой budget доказательством чистоты
quarantined roots; использует прежние recovery proofs, без TTL/рекурсивного удаления.

[Проверки](ProgressiveContinuousRemuxControllerValidation-2026-11-04.json): по 17
сценариев Linux GCC/ASan-UBSan и Windows MinGW/MSVC. Реальный persistent worker,
metadata replay/fences, queue limit, accepted/applied, blocked preparation и
publication, отдельные writer/required track/resource failures, heartbeat/lifetime, worker kill, SHA error,
late resource, четыре complete/forget cycles и surviving reader при manager
shutdown. Тестовые задержки I/O доказывают быстрые status/heartbeat/cancel/drop;
это не обещание прерывать зависший kernel syscall. При непроверяемой quarantine
closing job остаётся bounded/fenced до восстановления proofs или конца процесса.
Исправлен отказ обязательной track при живом writer: раньше job оставалась в
waiting после принятой публикации; baseline binary/log сохранены. Теперь metadata
failed/cancelled проверяются отдельно от состояния FFmpeg resource reader.
В native cache fixture исправлен отдельный ложный отказ: ответ `status` теперь
берёт output/done/source из одного снимка, а не из двух последовательных чтений.
Исходный CTest exit 8 и SHA старого fixture сохранены в отчёте; это исправление
тестового инструмента, не изменение cache engine. Повторены cache/recovery
регрессии Windows/Linux. Main EXE/установки/INI/SDK ZIP
этим этапом не менялись: отдельной новой пользовательской версии нет.

## Внутренние команды подготовки через обычный IPC — 4 ноября 2026

Qt `ProgressiveApi` обеих ОС и `ClassicProgressiveApi` маршрутизируют шесть
команд из таблицы выше в общий controller через действующий приватный endpoint.
Отдельный новый endpoint не создаётся. Worker задаётся относительно EXE;
registry и cache — относительно приватного runtime endpoint/profile.
Клиент не может подставить executable/cache/registry. Создание controller не
открывает файлы, а подготовка и публикация остаются на его background thread.
Слот обычного media opening не занимается этими командами.

Шесть команд этого этапа выполняют **подготовку без playback commit**: Start/Update/ready не открывают
окно, не создают decoder reader и не заменяют текущий просмотр. Команды не
добавлены в advertised `actions`; capability непрерывного воспроизведения и
SDK не расширены. Экспериментальный worker остаётся в частных тестовых копиях,
не установлен пользователю и не добавлен в опубликованные пакеты.

Проверка через IPC обнаружила гонку: между чтением producer status и heartbeat
producer мог завершиться, после чего controller превращал успешный output в
`errorCode=session-closed` и снимал `playbackReady`. Новый
`heartbeatWhileRunning` проверяет terminal state и обновляет deadline под одной
блокировкой producer; поздний heartbeat становится no-op, не меняя terminal result.
Строгий прежний `heartbeat()` сохранён. API по-прежнему отклоняет новые heartbeat
после опубликованного terminal state; уже принятый heartbeat может быть обработан
после EOF без ошибки. `appliedHeartbeat` в таком случае означает обработку
последовательности, а не продление жизни завершённого producer.

[Отчёт](ProgressiveContinuousRemuxApiValidation-2026-11-04.json): по шесть сценариев
через настоящие endpoints каждой редакции, два самостоятельных плеера, reconnect,
replay/conflict, изоляция job identity, последовательные публикации, 600 packets
одним worker, cancel/forget/receipts, late resource и track/resource failure.
Полный физический хвост не открывает неподтверждённые bytes. Прежний API проверен
по 24 preparation и 20 playback сценариям каждой редакции. По 18 native сценариев
GCC/MinGW/MSVC/ASan-UBSan включают по 12 циклов heartbeat на границе завершения.
Сохранены исходный CTest exit 8, старый Linux EXE и лог фактического отказа;
отдельно записана корректировка тестового poller, который теперь перечитывает
status после `continuous-closed`, сохраняя все проверки конечного результата.

Все три main EXE пересобраны. Linux EXE обновлён атомарно с backup, INI и 264
других файла сохранены; Windows-установки и 158 shell entries не менялись.
Три одноразовые задачи тестов Classic и частные копии удалены. Этот результат
не закрывает запуск из Harvester, UI commit/intent fencing и общую приёмку ТЗ.

## Внутренний запуск и управление просмотром через IPC — 4 ноября 2026

К шести командам подготовки добавлены три внутренних команды. Они используют
тот же приватный transport Qt/Classic; публичные capabilities/actions, SDK ZIP
1.81 и опубликованные установщики этим этапом не расширяются.

| Команда | Поля и действие |
| --- | --- |
| `continuousRemuxPlay` | `jobId`, отдельный `requestId` просмотра, `sessionId/generation/writerInstanceId`; optional `title`, `replace`, `activate`. Требует `playbackReady` и создаёт `playbackId` |
| `continuousPlaybackStatus` | Ровно один `playbackId` или `requestId` просмотра; возвращает состояние GUI, позицию, полный duration, `outputComplete`, `decoderReaders` и output `readerIds` |
| `continuousPlaybackControl` | `playbackId`, `command=play/pause/stop/seek`; только `seek` принимает числовой `seconds` |

Play не ждёт публикации недостающего init/media: возвращает
`continuous-not-ready` без скрытого отложенного запуска. После готовности клиент
может явно послать Play. Принятый запрос с тем же canonical input возвращает тот
же просмотр, включая его terminal state; другой input с тем же requestId или
другая попытка для того же job даёт `continuous-playback-conflict`. Удерживается
последняя попытка, не бессрочный журнал всех requestId. Новый просмотр другого
job допускается после Forget прежнего producer. RequestId подготовки и просмотра
имеют разные области поиска.

`replace=false` по умолчанию требует свободного просмотра, `activate=false` не
переводит окно на передний план. Start/Update подготовки сами не открывают GUI.
Classic готовит `CHarvesterSession` вне UI и выполняет commit через существующий
цикл opening; Qt подключает проверенный cache к настоящему `Window/Player`.
Статусы `opening/buffering/playing/paused/finished/closed/error` относятся к GUI,
а прежний `continuousRemuxStatus` — к producer/cache. Ready не означает playing.

Пауза сохраняется при append/complete, producer не перезапускается из-за append.
API seek разрешён только при подтверждённом complete output, в диапазоне
`0 <= seconds < duration`; управление требует загруженного не завершённого
просмотра. Stop идемпотентен и допускается после terminal state. Повтор Play
после Stop/замены не возобновляет прежний просмотр. Старые controls не закрывают
видео, открытое позже другим путём. Cancel/Forget producer закрывают только
относящийся к нему GUI reader. EOF подтверждается успешным output complete;
последний кадр/reader могут удерживаться после EOF до Stop/Forget. Число decoder
readers не подменяет `sourceFilesClosed` и полное освобождение кэша.

При подготовке обычной замены текущий continuous-просмотр сохраняется до commit
нового источника. Ранее его producer отменялся на admission; позднее закрытие
старого графа могло отменить и новую подготовку. Этот воспроизведённый отказ
Classic исправлен разделением preparation/commit intent в обеих реализациях.
Реальные Stop/принятое закрытие окна отменяют pending preparation; скрытие в tray
сохраняет штатное поведение. Qt после complete проверяет seek относительно полного
объявленного duration, а не ещё короткого native duration прочитанного префикса.

Status использует опубликованные immutable snapshots producer/cache. Они могут
отставать от физического close; успех release требует прежних proofs. Output
reader регистрируется вне mutex stream adapter. GUI detach удерживает только
отдельный atomic interrupt token, чтобы уничтожение последней ссылки cursor с
файловым close не оказалось на UI. Observer последней GUI-попытки может удерживать
retained job; он учитывается в прежнем общем лимите восьми job objects.

[Проверки](ProgressiveContinuousRemuxPlaybackApiValidation-2026-11-04.json): по 14
IPC сценариев каждой Qt ОС и 15 Classic, включая шесть Stop-during-opening циклов,
failed replacement с продолжением прежнего видео, pause/append, complete seek
к 22 s, настоящий EOF, error/cancel/forget и изоляцию двух endpoints. Classic
штатно закрылся с exit 0, обе квитанции получили `files-closed`, кэш был пуст уже
до повторного запуска того же профиля. Это не новая проверка аварийного recovery.
Qt с software renderer отдельно проверяет настоящее закрытие Window и уничтожение API перед Window:
native retirement, фактические receipts и admission деструкторов <200 ms. На ОС
прошли шесть CTest групп, прежние 6 preparation, 24 bounded preparation/20 playback,
18 native controller и retirement проверки. Native decoder — 11 ASan/UBSan
сценариев; весь GUI под sanitizer этим не проверялся. Исходные Qt CTest exit 8
и Classic driver exit 1 сохранены с SHA старых EXE/logs.

Все три main EXE пересобраны; Linux атомарно обновлён с backup, INI и 265 других
файлов сохранены. Windows-установки, 158 shell entries, Harvester и SDK ZIP
сохранены. Четыре одноразовые задачи и частные Classic-копии удалены; периодические
задачи не создавались. Worker по-прежнему только в тестовых поставках.

## Освобождение output readers через обычный API

Внутренний continuous-remux теперь обрабатывает обычный `readerRelease` в Linux
Qt, Windows Qt и Classic. После проверки квитанции в отдельном registry worker
GUI сопоставляет session/generation и неизменные resource/device/file IDs с
конкретным выходным кэшем. Сопоставление не выполняет файловый I/O и не зависит
от запаздывающего списка reader IDs в status. При приёме запроса фиксируется
попытка playback: завершившийся позднее запрос не закрывает её замену.

Запрос живого output reader отменяет подготовку/открытие соответствующего
просмотра и закрывает только принадлежащий ему GUI playback. Квитанции исходного
producer, уже закрытых cursor и чужих outputs лишь запрашиваются; неизвестные
ID и неверные команды не меняют playback. Загрузка и исходные данные не меняются.
Квитанция с `filesClosed=true` также не останавливает просмотр, даже если
наблюдатель ещё не увидел снятие её блокировки и `released` остаётся false.

Ответ на принятую остановку сохраняет последнюю проверенную квитанцию, обычно
ещё с `released=false`. Немедленное повторное чтение реестра исключено: оно могло
столкнуться с native cleanup и дать `reader_registry_busy` после уже применённой
остановки на Windows. Клиент опрашивает `readerStatus`; только фактические
`filesClosed` и снятая блокировка разрешают положительное подтверждение.
Удерживаемый дополнительный reader продолжает закреплять кэш и свою квитанцию.
Повторный release не запускает старую попытку заново и не останавливает замену.

После положительного подтверждения клиент может вызвать обычный `readerForget`.
Следующий `continuousRemuxForget` допускает уже обработанное подтверждение только
после независимых in-process proofs: producer закрыл исходники, число output
readers равно нулю, кэш физически удалён и service owner подтвердил освобождение.
Отсутствующая квитанция не используется как доказательство release. Исправлена
остановка cleanup в `receipt-cleanup-failed` после такого внешнего подтверждения;
native controller и обычный IPC проверяют эту последовательность отдельно.

[Проверки трёх редакций](ProgressiveContinuousRemuxReaderReleaseValidation-2026-11-04.json)
включают ordinary IPC, playing/pause/ожидание неопубликованного хвоста, EOF,
источник вместо output reader, неверные/чужие ID и устаревший release после
замены. Qt Window/API дополнительно проверены с искусственно задержанным registry
worker и дополнительным удерживаемым cache reader. Проверка ожидания хвоста
подтверждает остановку продвижения decoder, ожидание producer и отсутствие EOF;
она не закрывает общую задачу отображения buffering/reasonCodes во всех режимах.

Итоговый прогон: по 17 IPC сценариев Qt каждой ОС и 18 Classic, по 19 native
controller сценариев Windows/Linux, по восемь CTest групп каждой Qt ОС. Qt отдельно
проверил шесть Window/API lifecycle и пять Player сценариев. В трёх release-сценариях
каждой редакции измерен общий интервал до подтверждённого закрытия output/source
и очистки кэша: 0,08–0,27 s, с проверяемым пределом <2 s. Все окончательные build,
driver и CTest exit codes равны 0; прежние отказы и исправления тестового стенда
сохранены в отчёте отдельно. Это не общая приёмка всех форматов/режимов.

Все main EXE пересобраны; Linux обновлён атомарно с backup, INI и 267 остальных
файлов сохранены. Windows-установки и 158 shell entries не изменены. Шесть
одноразовых Classic задач и тестовых поставок удалены. Worker и SDK остаются
прежними пользовательскими поставками, новая capability не объявлена.

## Следующий обязательный этап

SDK 1.82 публикует этот ограниченный профиль и matching worker/runtime:
[публичный контракт и инструкция](ProgressiveContinuousSdk.md),
[проверки SDK и поставок](ProgressiveContinuousSdkValidation-2026-11-04.json).
Новые сборки объявляют отдельный continuousRemux capability и девять действий;
прежние v1/v2 маршруты не заменяются автоматически. Пять реальных SDK сценариев
прошли на каждой редакции, включая lost prepare/play replies, pause/append,
complete seek/EOF, stop, освобождение и отказ audio/B-frames. Qt Windows portable
и извлечённый .deb проверены отдельным реальным SDK прогоном. Worker --info
подтверждает ABI загруженных FFmpeg libraries; декларация capability не заменяет
проверку конкретного input до playbackReady.

1. Проверить полную waiting/resume/seek/replay и shutdown/crash/stress matrix
   через обычный API трёх редакций, не подменяя её успешными короткими сценариями.
   Первый дополнительный [публичный waiting/replay срез](ProgressiveContinuousWaiting.md)
   прошёл по пять сценариев на редакцию: реальные паузы writer 10/30/60 s,
   те же worker/readers, ручная пауза, complete reverse seek/EOF и terminal replay.
   [Отчёт](ProgressiveContinuousWaitingValidation-2026-11-04.json) сохраняет
   точные exit codes и исходные ошибки тестового стенда. Найденное Qt `playing`
   при фактическом ожидании хвоста затем [исправлено и проверено](ProgressiveContinuousBuffering.md)
   на обеих Qt ОС: API/строка состояния, manual pause, optional reasonCode,
   writer-failed и 10/30/60 s waits. [Heartbeat-срез](ProgressiveContinuousHeartbeat.md)
   прошёл по пять сценариев каждой редакции; общий Qt исправлен для producer error
   на ручной паузе. Единичное превышение 2 s release в исправленном Linux остаётся
   открытым несмотря на успешный повтор. Далее — локализация этой задержки,
   shutdown/crash и длительные stress проверки.
2. Затем AAC trim, B-frames, остальные профили и общая cache/performance matrix.

Полная интеграционная приёмка не объявляется выполненной этими ограниченными
проверками. Public SDK 1.82 не расширяет профиль до всех контейнеров и кодеков.
Дисковые меню, оборудование и Wayland не возобновляются; Harvester не меняется.


4 ноября 2026, [continuous player close/crash и same-profile recovery](ProgressiveContinuousShutdown.md):
по восемь сценариев Linux Qt, Windows Qt и Classic, все exit 0; preparation,
playing, paused, buffering, обычный выход и принудительное завершение. Worker exit
подтверждён ОС, независимый просмотр сохраняет IDs и продолжает кадры; writer
физически дописывает оригинал после выхода player, новый prepare восстанавливает
кэш до подтверждения старых receipts, Play создаёт новые IDs. [Результаты](ProgressiveContinuousShutdownValidation-2026-11-04.json).
При части нормальных выходов закрытый journaled cache остаётся до повторного
запуска: это открытый пункт очистки, не доказательство полного teardown ≤2 s.
Прежний Linux heartbeat release outlier также открыт. Следом — normal-exit cache
cleanup, worker crash/mid-I/O/races и stress. EXE/установленные профили, 158 shell
entries, SDK ZIP и Harvester сохранены; одноразовая задача удалена.


4 ноября 2026, [нормальная очистка continuous cache](ProgressiveContinuousExitCleanup.md)
исправлена общим bounded final-exit wait ≤1500 ms после уничтожения владельцев
GUI/API/decoder. Обычные деструкторы/Stop не ждут; живые readers сохраняют fence
и журнал. По восемь усиленных close/crash сценариев каждой редакции: после всех
двенадцати нормальных выходов кэш отсутствует, 0,062–0,235 s. Native controller
проверяет timeout с удерживаемым reader и успешную очистку после реального close.
[Отчёт](ProgressiveContinuousExitCleanupValidation-2026-11-04.json).
Общая latency/crash/stress приёмка и прежний Linux heartbeat outlier открыты.
Windows-установки, профили, shell registration, SDK ZIP и Harvester сохраняются;
проверенная Linux-сборка устанавливается атомарно с backup и сохранением INI.


4 ноября 2026, [аварийное завершение continuous worker](ProgressiveContinuousWorkerCrash.md):
по четыре сценария отдельно Linux Qt, Windows Qt и Classic, все exit 0. Проверенный
OS identity дочернего worker, preparation/playing/paused/buffering, status-only
release без Stop, filesClosed receipts и удаление кэша, независимый просмотр,
реальное дописывание partial и явный retry в том же player с новыми IDs.
Сумма kill/exit и release ожидания 0,047–0,297 s; [результаты и ошибка первого
стендового ожидания](ProgressiveContinuousWorkerCrashValidation-2026-11-04.json).
Production code/EXE, установки, INI, 158 shell entries, SDK ZIP и Harvester
сохранены; временная одноразовая задача удалена. Прежний Linux heartbeat outlier,
mid-I/O/races, расширенный stress и полная приёмка ТЗ остаются открыты.


4 ноября 2026, [гонки continuous-команд и замены просмотра](ProgressiveContinuousCommandRaces.md):
по 16 сценариев Linux Qt, Windows Qt и Classic, все exit 0; publication/Stop,
publication/replacement, readerRelease/replacement и cancel/replacement. Поздние
старые команды не прерывают новый и независимый просмотр, receipts/cache и SHA
проверены. Исправлена общая ложная ошибка MP4 header при registry contention:
сохраняется reader_registry_busy; детерминированный baseline exit 1 сохранён.
На Qt ОС по 13 основных и две multi-reader CTest-группы, по 469 native media
checks; [отчёт](ProgressiveContinuousCommandRacesValidation-2026-11-04.json).
Все main EXE пересобраны, Linux обновлён атомарно с backup и сохранением профиля;
Windows-установки/HKCU/SDK ZIP и Harvester сохранены, одноразовая задача удалена.
Прежний Linux heartbeat outlier, точные mid-I/O schedules, длительный stress и
полная приёмка ТЗ остаются открыты.


5 ноября 2026, [faults незавершённого pipe I/O](ProgressiveContinuousPipeFaults.md):
по 12 native сценариев GCC/Linux, MinGW/Windows, MSVC/Windows и ASan/UBSan,
все exit 0. Тестовый child доказывает partial receive либо backpressure send
до kill/cancel/timeout. Source readers фактически закрыты, partial output не
принят, оригиналы сохранены. Повторены прежние parent/transport проверки Qt ОС
и реальные SDK worker-crash сценарии всех трёх редакций;
[результаты и границы](ProgressiveContinuousPipeFaultsValidation-2026-11-05.json).
Production code/main EXE, установки, профили, HKCU, SDK ZIP и Harvester сохранены;
одноразовая задача удалена. Это компонентная проверка с synthetic child:
настоящий FFmpeg mid file-I/O/cache/decoder, Linux heartbeat outlier и общая
stress/ТЗ-приёмка остаются открытыми.


5 ноября 2026, [авария настоящего FFmpeg в AVIO](ProgressiveContinuousAvioFaults.md):
по восемь public SDK сценариев каждой редакции, всего 24; read/write callbacks,
playing/manual pause и surviving external cache reader. Фактические decoder/
source filesClosed, удержание кэша до последнего close, partial output rejection,
независимый просмотр, SHA и явный retry проверены;
[отчёт с исходными отказами стенда](ProgressiveContinuousAvioFaultsValidation-2026-11-05.json).
Отдельный instrumented test worker собран GCC/MinGW/MSVC, девять engine guards
пройдены под ASan/UBSan. Production worker macro выключен; пересобранный Linux
worker побайтно прежний. Установки/профили, SDK ZIP, HKCU и Harvester сохранены,
одноразовая задача удалена. OS disk/parent file-I/O, прежний Linux heartbeat
outlier и полная stress/ТЗ-приёмка остаются открытыми.


5 ноября 2026, [parent syscall-result faults](ProgressiveContinuousFileIoFaults.md):
50 native scenarios GCC/MinGW/MSVC/ASan-UBSan — source error/short read, cache
write error/disk-full/physical partial write, external readers и POSIX retry.
Исправлена общая диагностика short read: source-truncated вместо ложной
source-rewritten; baseline exit 1 сохранён. По 15 Qt CTest-групп ОС и восемь
настоящих AVIO SDK scenarios Classic;
[результаты, SHA и установка](ProgressiveContinuousFileIoFaultsValidation-2026-11-05.json).
Все main EXE пересобраны, Linux обновлён атомарно с backup и сохранением профиля;
Windows-установки/HKCU, SDK ZIP и Harvester сохранены, одноразовая задача удалена.
Это конечные syscall return faults, не зависший диск: полный cancellation/stress
и прежний Linux heartbeat outlier остаются открытыми.
