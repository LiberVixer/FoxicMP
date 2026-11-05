# Continuous remux: ошибки системного чтения и записи

Проверен общий parent I/O с настоящим FFmpeg worker, private cache и двумя
decoder views: **14 сценариев GCC/Linux, 11 MinGW/Windows, 11 MSVC/Windows и
14 GCC ASan/UBSan — всего 50**. [Результаты, baseline, SHA и exit codes](ProgressiveContinuousFileIoFaultsValidation-2026-11-05.json).

## Исправление диагностики короткого чтения

В `resource_detail::Pin::verify` один Check проверял одновременно длину
физического чтения и равенство bytes прежнему блоку. Поэтому short read ошибочно
становился `source-rewritten`, хотя сравнение содержимого ещё не выполнялось.
Строгий native baseline воспроизвёл это с exit code 1; исходный header, fixture,
результаты и отказ сохранены.

Теперь сначала проверяется полнота чтения (`source-truncated`, с сообщением о
short physical data), затем равенство полученных bytes (`source-rewritten`).
Оба error codes уже существовали; формат API/SDK не меняется. Waiting на
неопубликованном хвосте и настоящее изменение содержимого сохраняют своё
прежнее поведение. Правка общего header применяется к Qt и Classic.

## Методика fault injection

Отдельный `tests/progressive_file_io_fault_cli.cpp` компилирует настоящий
CachedRemuxSession/ResourceSource/StreamOutputStore с перенаправлением только
низкоуровневых pread/pwrite или ReadFile/WriteFile в этой translation unit.
Каждая подстановка ограничена device/file ID одного конкретного временного
source либо output file. Reader receipt I/O включён до перенаправления и
не перехватывается. В production main EXE нет switch, env или API для этих faults.

Каждый отрицательный сценарий выполняется без/с внешним cache reader:

- EIO / ERROR_READ_FAULT при чтении source → `resource-io-error`;
- настоящий укороченный OS read выбранного файла → `source-truncated`;
- EIO / ERROR_WRITE_FAULT при записи cache → `cache-io-error`;
- ENOSPC / ERROR_DISK_FULL → `cache-disk-full`;
- физическая запись части cache append: Linux затем получает EIO, Windows
  завершается с short write count → `cache-io-error`.

Оригиналы при этом не повреждаются и диск не заполняется. Short read/write
действительно выполняет системный вызов меньшего размера; при partial write
проверяется физический рост output.fmp4 при неизменных published written/visible
и побайтно прежнем cache prefix. Остальные errors подставляют конкретный
системный результат/errno/GetLastError; это не throw на уровне готового sink.

Для каждой сборки есть положительный complete control. Linux дополнительно
проверяет однократные EINTR при pread/pwrite и успешную короткую pwrite с записью
остатка циклом: все 600 encoded packets, clocks и decoded frame hashes совпадают
с независимым completed source. Эти POSIX retry cases не выдаются за Windows
семантику. Положительный EOF разрешён только после полного output complete.

## Lifetime и проверяемая граница

Перед fault создаётся настоящий растущий remux до complete. StreamDecoderCursor
ждёт за published tail, StreamFragmentDecoder держит свой reader и sample index.
После ошибки требуются terminal failure без successful result/complete, настоящий
OS exit только owned worker, закрытие source handles/receipts. Ожидающий cursor
возвращает −1 с той же причиной, без EOF; Classic fragment view отказывает с тем
же code. Это настоящие decoder views общего компонента, не новый GUI/DirectShow
fault-injection прогон.

До close этих views кэш остаётся. Внешний helper с другим sessionId и настоящим
file handle удерживает его далее; только после filesClosed receipt всех readers
collect удаляет кэш и возвращает весь budget. После этого исходник остаётся
доступен writer для дописывания; original SHA не меняется parent. Все отрицательные
fault→producer-close измерения ограничены 2 s; native RPC также имеет timeout.

## Проверки и установка

Новый CTest — `progressive-continuous-file-io-faults`, timeout 120 s. В обеих Qt ОС
повторены 15 выбранных CTest-групп: resource/media и controller/decoder/SDK/lifecycle
регрессии, включая предыдущие восемь AVIO сценариев настоящего плеера. Classic
пересобран MSVC и отдельно повторяет восемь AVIO/GUI/DirectShow SDK сценариев.
Это разделяется с 50 native syscall cases, которые не объявляются запуском
установленного GUI с перехваченным системным I/O.

Classic copies защищены ContextMenuPrompted=1/ContextMenuEnabled=0 до первого
запуска; «Форматы» не открываются. 158 HKCU shell entries сверяются до/после,
одноразовая задача и test copies удаляются. Linux UI/audio использует частные
X11/D-Bus/PulseAudio через qt/tests/run_tests.sh. Сохраняются точные exit codes.
Проверенная Linux-сборка устанавливается атомарно с backup прежнего EXE и
проверкой неизменности профиля/личных файлов. Windows-установки, SDK ZIP и
Harvester сохраняются. Версии приложения 0.2.0 и SDK 1.82 не меняются.

Остаются реальные сбои носителя, бесконечно зависший OS I/O и полная cancellation/
stress matrix; подстановка конечного error result не доказывает их обработку.
Физическое оборудование не требуется и не проверяется. Исходный единичный Linux
heartbeat release >2 s остаётся открытым.
