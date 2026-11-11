# Детерминированные границы чтения continuous-кэша

11 ноября 2026. Проверочный этап после [защиты commit очереди](ProgressiveQueuedSeekCommit.md).
App 0.2.0 и публичный SDK 1.87 сохранены. Harvester не менялся.
[Результаты, native exits и fingerprints](ProgressiveStreamIoBoundariesValidation-2026-11-11.json).

## Что проверено

Standalone header-only fixture использует настоящий `StreamOutputStore`, SHA,
файлы, reader leases и `StreamDecoderCursor`. Поток останавливается condition
variable на заданной границе; продолжение разрешает сам тест. Случайный порядок
двух публичных IPC-запросов не подменяет эту проверку.

Четыре точки:

1. После проверки file binding/size, непосредственно перед native `ReadAt`.
2. После возврата `ReadAt`, перед проверкой SHA прочитанного chunk.
3. После SHA, до копирования проверенных байтов в буфер reader.
4. После возврата reader, до проверки interrupt и результата decoder cursor.

Во всех четырёх точках проверены interrupt, закрытие cursor, source cancel с
закрытием только decoder readers и сериализация нового byte seek. Старый read
не сбрасывает новую позицию. Закрытие не выдаёт `filesClosed` до фактического
закрытия native файла. После завершённого физического чтения controller может
закрыть файл, пока старый callback ещё стоит на барьере: возобновлённый cursor
возвращает -1 и больше не обращается к этому файлу. Независимый reader сохраняет
свою lease и возможность чтения.

Дополнительные случаи: cancel у подтверждённого EOF и у недокачанного хвоста,
контроль настоящего EOF, повреждение до/после native read и после SHA,
усечение файла, публикация нового complete fragment после старого read и
zero-length probe. Первый read ограничен прежним visible prefix; следующий
читает новые опубликованные данные с правильной позиции. Zero-length probe
возвращает 0 без увеличения EOF counter.

При повреждении после native read разрешён уже прочитанный и проверенный
snapshot исходных байтов; следующий read обнаруживает повреждение. Ошибочные
байты не выдаются. Усечение даёт -1, а не media EOF: на Linux проверяется
`cache-io-error`, на Windows — `resource-io-error`. Эти действующие коды не
унифицировались изменением работающего I/O.

Каждый из 25 случаев закрывает и забывает все принадлежащие ему readers,
удаляет cache output и возвращает cache budget к нулю. Native output сохраняет
фактические `readReturns`, delivered bytes, EOF counter, ошибки и число proofs.

| Native конфигурация | Сценарии | Checks | Driver / native exit |
| --- | ---: | ---: | --- |
| Linux GCC / CMake Release | 25 | 505 | 0 / 0 |
| Linux GCC ASan/UBSan, leak checks включены | 25 | 505 | 0 / 0 |
| Windows MinGW / CMake Release | 25 | 505 | 0 / 0 |
| Windows MSVC / O2 / MT | 25 | 505 | 0 / 0 |

Это 100 native cases и 2020 checks. Генератор создаёт три секунды video-only
H.264 fMP4 без B-frames; SHA исходника совпадает до/после. Разные сборки FFmpeg
не обязаны создавать одинаковый bitstream: каждая проверяется по своему
неизменному input. Оба CTest имени зарегистрированы в Linux и Windows;
Linux CTest выполнил оба теста с exit 0.

## Изоляция от плеера

Барьер существует только при target-local `FOXICMP_PRIVATE_STREAM_IO_TEST=1`.
Этот target не связывается с обычными player/decoder objects: нельзя смешивать
разные inline definitions из instrumented и normal headers. У обычных targets
нет этой compile definition, runtime hooks, новых CLI/API или возможностей SDK.

Для GCC, MinGW и MSVC сравнен normal header-only preprocessing до/после
изменений (`NDEBUG`, private macro не определена). C++ tokens совпали в каждой паре; private hook отсутствует. Сравнение исключает
layout и комментарии, сохраняя содержимое literals; checker прошёл 13 positive
и negative controls, включая raw/multiline literals и обнаружение leaked hook.
Это проверка исключения instrumentation, не замена полной ABI/decoder приёмки.

Все 587 защищённых файлов, включая установленный Linux EXE/INI и Windows
установки, совпали до/после. Совпали все 158 HKCU shell entries. Тесты не
создавали MFC main window, libmpv, audio/display devices или playback endpoint;
сеанс/микшер не менялись. Новая сборка приложения не создавалась, переустановка
не требуется. Периодических и Windows Scheduler задач нет.

Первичные отказы стенда учтены отдельно; где raw-лог не уцелел, это прямо
указано в отчёте: недоступный setup-файл/неверный
cwd до подготовки preprocessing; отсутствие явного MP4 muxer для `.fmp4`;
ошибка regexp checker; первый Windows oracle, ожидавший Linux-код усечения.
Эти exits не засчитаны PASS. Итоговый oracle требует точный код своей ОС;
условия -1 и отсутствие ложного EOF сохранены.

## Что остаётся открытым

Общий cache reader используется всеми тремя редакциями; `StreamDecoderCursor`
в этом стенде представляет Qt-сторону. MSVC PASS не означает проверку
`CHarvesterFileReader::SyncRead` / `CHarvesterSession::ReadV2`, настоящего
DirectShow graph или повторного входа в Classic `SetPositions`.

Не задаётся порядок внутри kernel-pending OVERLAPPED I/O / CancelIoEx. Не
проверены AAC projections, фактический libmpv callback, original-source ranges,
контроллер/worker, decoded A/V и полная codec/crash/race/stress matrix. Здесь
исследуется private output cache, а не допуск к неопубликованным байтам writer.
Искусственные барьеры не являются измерением эксплуатационной latency.

Следующий шаг — отдельный Classic adapter/graph стенд: flush/cancel/breaker
при последнем read chunk, реальная передача данных splitter и reentry внутри
seek. Полное ТЗ и прежние resource/latency outliers остаются открытыми.

## Воспроизведение

После настройки обычного Qt CMake build:

```sh
cmake --build BUILD --target foxicmp-progressive-stream-io-boundaries
ctest --test-dir BUILD -R '^progressive-stream-io-(boundaries|preprocess-checker)$' --output-on-failure
```

Самостоятельный driver:

```sh
python3 -B tests/progressive_stream_io_boundaries.py --runner RUNNER --report-directory REPORT
python3 -B tests/progressive_stream_io_preprocess.py --self-test
```

Этот native fixture не создаёт UI/audio; частный X11/PulseAudio нужен для
отдельных Qt Player/Window проверок, которые продолжают запускаться через
`qt/tests/run_tests.sh`. Обычный shipping target с private macro собирать нельзя.
