# Classic: границы SyncRead/ReadV2 и повторный вход в DirectShow seek

11 ноября 2026. Продолжение [общих cache I/O-проверок](ProgressiveStreamIoBoundaries.md).
App 0.2.0 и публичный SDK 1.87 сохранены. Изменён только адаптер Windows Classic;
Qt и Harvester не менялись. [Native результаты, exits и SHA](ProgressiveClassicReadBoundariesValidation-2026-11-11.json).

## Исправление

Раньше `CHarvesterSession::ReadV2` проверял отмену перед чтением блока, но мог
вернуть S_OK после последнего блока, даже если за время чтения поступили cancel,
flush или breaker. Ошибка чтения на этой границе могла ошибочно пометить весь
источник неисправным. `CHarvesterFileReader` также не замечал полностью
завершившийся BeginFlush→EndFlush: к следующей проверке event уже был сброшен.

Теперь guarded read проверяет прерывание до/после каждого payload read, перед
классификацией ошибки и перед возвратом успеха. SyncRead запоминает поколение
flush и повторно проверяет его после ожидания диапазона и после чтения.
Каждый Begin/End меняет поколение; младший бит отражает активный flush. Старое
чтение не становится новым чтением после EndFlush. Native-file ветка тоже
проверяет прерывание после ReadFile/GetOverlappedResult.

При обнаруженном прерывании возвращается действующий проектный E_ABORT;
уже скопированный буфер вызывающий код должен игнорировать. Это не обещание
отменить уже совершённый OS read. Старая C++ точка входа ReadV2 и её прямой
zero-length no-op сохранены. Контекст общего источника не отменяется вместе
с отдельным decoder reader; независимый reader продолжает читать.

[Microsoft описывает flush pull-модели](https://learn.microsoft.com/en-us/windows/win32/directshow/flushing)
через IAsyncReader BeginFlush/EndFlush. Конкретный E_ABORT здесь сохраняет
контракт нашего адаптера; это не утверждение о единственном допустимом
HRESULT для всех реализаций SyncRead.

## Доказательство до/после

MSVC fixture вызывает настоящие `CHarvesterSession::OpenContinuous`, ReadV2 и
`CHarvesterFileReader::SyncRead`, настоящий cache reader, SHA и native files.
Private callback выполняется после фактического payload read либо перед ним
для порчи кэша. Watcher остановлен после начального refresh, чтобы стенд
проверял именно заданную границу, а не случайный порядок watcher/read.

31 случай: последние 64 байта, последний и первый блок запроса 131072 байта;
контроль без прерывания, source cancel, flush, breaker и полный flush cycle
(последний — для SyncRead); четыре сочетания реальной ошибки SHA с flush/breaker.
Каждый случай проверяет HRESULT, число чтений, отсутствие ложного source error,
независимый reader и filesClosed/forget/нулевой cache budget. Где применимо,
после reset выполняется успешное новое чтение.

| Проверка | Сценарии | Результат | Native / collector exit |
| --- | ---: | --- | --- |
| Инструментированный исходник до исправления | 31 | 19 FAIL, ожидаемое воспроизведение | 1 / 0 |
| Исправленный адаптер | 31 | PASS | 0 / 0 |
| Реальный DirectShow graph, BF0 и BF2 | 12 | PASS | все 0 / 0 |
| Обычный EXE: public API regression | 74 | PASS | все 0 |
| Обычный EXE: public races | 36 | PASS | 0 |
| Обычный EXE: measured stress | 100 + 4 warmup | PASS в прежних budgets | 0 |

Pre-fix collector exit 0 означает успешно воспроизведённый отказ, а не
прошедшую runtime проверку. Первичные compile/link отказы стенда (оба exit 2)
сохранены в отчёте и не засчитаны PASS. У graph link сохранились LNK4286
предупреждения существующего mixed-CRT набора библиотек; native build exit 0.

## Настоящий graph и пределы reentrancy

FilterGraph соединяет private подкласс штатного CMP4SourceFilter, внутренний
software H.264 decoder и NV12 capture. Callback находится внутри стека
настоящего SetPositions — до/после вызова базового source filter. Владелец
работает в MTA; это не тест message pump главного окна Classic.

В шести режимах проверяются cancel до/после seek, замена цели до/после seek,
вложенный poll и новая revision с той же численной целью. Используется общая
production-транзакция applyQueuedSeek. Вложенный poll не передаёт второй seek
декодеру. Старый seek не стирает cancel/новую revision, последняя цель
исполняется в следующей транзакции.

Сначала опубликовано шесть секунд из 24; graph декодирует неполный источник.
Затем дописывается остаток, подтверждается complete/EOF и выполняется
контрольный seek на 10 секунд. Все 350 кадров после него совпали по SHA с
независимым FFmpeg NV12 decode в каждом из 12 случаев: 4200 проверенных кадров.
Проверены H.264 без B-frames и с двумя B-frames. Аудио/реальный дисплей в этом
стенде отсутствуют. Полный путь worker/controller здесь не проверяется.

## Сборка и изоляция

`FOXICMP_PRIVATE_CLASSIC_READ_TEST` существует только в fixture: callback
объявлен отдельным header, normal shared cache/cursor definitions не меняются.
В private link явно включены инструментированные HarvesterSession.obj и
AsyncReader.obj вместо их архивных копий. Нормальный EXE собран отдельно,
без macro; в трёх пересобранных objects отсутствует private namespace.

Из-за нового поля CHarvesterFileReader пересобраны его реализация, session и
FGManager — единственное production место выделения этого объекта. Для
FGManager отключён старый PCH (`/Y-`), чтобы allocation использовал новый layout.
Нормальные неизменённые библиотеки/остальные app objects взяты из предыдущей
сборки. Это проверенная инкрементальная сборка, не clean rebuild всех зависимостей.

Перед первым запуском private copies подготовлены INI с
ContextMenuPrompted=1 и ContextMenuEnabled=0. Обычный EXE проверялся в private
stage через public API, с Null Video Renderer и null audio; installed Windows
EXE/INI не заменялись. Все 158 HKCU shell entries совпали до/после.

Из 587 защищённых файлов совпали 586. Единственное расхождение — INI
установленного Linux Qt (mtime 06:18:20 +03:00); причина изменения не установлена,
его содержимое не откатывалось. Это исключает утверждение о неизменности всех
профилей. Linux EXE/worker и Windows установки совпали; новые Linux binaries
не собирались, переустановка Linux не требуется. Тестовые drivers запускают
только Classic. Harvester, микшер, дисплеи и Windows Scheduler не менялись;
периодических/одноразовых scheduler tasks не создавалось.

## Воспроизведение

Нужны обычные собранные Classic x64 static dependencies, MSVC x64 Developer
Command Prompt, Windows Python и FFmpeg в PATH. Из корня checkout:

```bat
tests\windows\build_classic_read_boundary_probe.cmd _bin\classic-read-probe
python -B tests\windows\classic_read_boundary_test.py --runner _bin\classic-read-probe\probe.exe --report-directory _bin\classic-read-results
python -B tests\windows\classic_graph_reentry_test.py --runner _bin\classic-read-probe\graph.exe --report-directory _bin\classic-graph-results
```

Build script создаёт оба безопасных private INI до запуска. Каталог вывода
должен быть новым private каталогом, никогда installed directory.

Для pre-fix воспроизведения в отдельной временной копии исходников применить
`git apply --unidiff-zero tests/fixtures/classic-read-boundary/pre-fix.patch`,
затем пересобрать fixture в другом каталоге и запустить только read driver
с `--expect-pre-fix`. Reverse patch проверен на точное совпадение исходных
байтов в temporary copy. Не применять его к рабочему/публикуемому исправлению.

## Оставшиеся границы ТЗ

Kernel-pending OVERLAPPED/CancelIoEx не удерживался искусственно. Новый guard
есть у native-file и другого v2 bridge, но эти ветки не получили такого же
детерминированного after-read стенда. External breaker должен оставаться
валидным/signaled до завершения запроса; короткий pulse этого внешнего event
не покрывается поколением внутреннего flush.

Не закрыты reentry через настоящий CMainFrame/STA message pump и принудительный
порядок public IPC внутри graph, AAC projections, original-source/worker path,
полная codec/superseding/replacement/crash/race matrix и длительная latency/stress
приёмка. Public API regression проверяет обычные interleavings, не внутренний
барьер. AAC repeat PCM bit-exact, физическая A/V sync и старые resource/latency
outliers не объявляются решёнными. Квота 16 и receipt retention остаются прежними.
EOF, revision и frames этого стенда не заменяют эти отдельные пункты.
