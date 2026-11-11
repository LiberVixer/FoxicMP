# Classic: ошибки чтения v2 и AAC-проекции

11 ноября 2026. Следующий ограниченный этап после
[missing-range/sample waits](ProgressiveClassicRangeWait.md).
Изменён Windows Classic SyncRead; Qt и Harvester не менялись.
App 0.2.0 и публичный SDK 1.87 сохранены.
[Результаты, native exits и SHA](ProgressiveClassicProjectionIoValidation-2026-11-11.json).

## Исправление

ReadV2Guarded при постоянной ошибке чтения вызывает Session::Fail, записывает
source error, сигнализирует собственный cancel event для остановки сессии
и возвращает E_FAIL. Внешний SyncRead затем повторно проверял прерывание,
принимал этот служебный cancel за внешнюю отмену и заменял E_FAIL на E_ABORT.
Прямой ReadV2 и raw-file SyncRead при такой ошибке уже возвращали E_FAIL.

Теперь SyncRead сохраняет E_FAIL, зафиксированный вложенным ReadV2Guarded.
Успех и прочие результаты по-прежнему проходят final interrupt guard.
Внешняя отмена/flush/breaker, обнаруженная внутри ReadV2Guarded до фиксации
ошибки, сохраняет E_ABORT и не записывает ложный Session source error.
Поздняя отмена не отзывает уже зафиксированную постоянную ошибку.
Это изменение классификации HRESULT, не новый публичный протокол.

## Два настоящих backend

Fixture создаёт настоящий CHarvesterSession и CHarvesterFileReader и вызывает
оба production entry points: SyncRead и ReadV2.

1. **v2 resources**: два отдельных H.264/AAC-LC fMP4 resources, required tracks,
   complete snapshot и SHA-256 blocks по 65536 байт. OpenPublished использует
   настоящий Fmp4V2Bridge/ResourceSource и pinned files. Независимый второй
   context удерживает свои handles и group lease тех же файлов.
2. **AAC projection**: реальный packet-copy worker создаёт A/V remux output и
   проверенный immutable audio window. Native fixture принимает этот window
   в StreamOutputStore до append, затем использует StreamDecoderSource,
   OpenContinuous и настоящие projected tracks StreamFragmentDecoder.
   Cache producer ещё не объявлен complete: отдельно проверяется подтверждённый
   AAC endpoint при остающемся incomplete video/cache.

Watcher останавливается после initial refresh для заданного порядка событий.
Private hooks перед/после payload call расположены в HarvesterSession.cpp;
normal приложение их не содержит. Общие header-only cache/cursor definitions
не переключаются на другой private macro; разные варианты inline definitions
не смешиваются с обычными decoder libraries.

Это границы **вызова и возврата** readView/readPayload. Внутренний физический
sub-read внутри AAC projection здесь не удерживается; kernel-pending OS I/O
и готовность/перестроение projection под блокировкой не объявляются проверенными.

## Сценарии и данные

По 120 случаев для двух профилей: AAC-LC 48000 Hz mono с H.264 BF2/25 fps
и 44100 Hz stereo с H.264 BF3/30000/1001. Источники созданы локально FFmpeg,
длительность video 24.137 s, audible audio samples = rate×24+137.
Worker не меняет Harvester и не открывает аудиоустройство.

На backend приходится 60 случаев:

- Обе дорожки, SyncRead/ReadV2, 64-byte last chunk и первый/последний chunk
  запроса 131072 байта; control, source cancel, flush, breaker. Для SyncRead
  дополнительно полный BeginFlush→EndFlush — 54.
- Повреждение физического SHA-покрытого файла перед чтением первого AAC
  payload: два entry points × control/flush/breaker — 6.

Контроль целого потока выполняется через настоящий SyncRead блоками до
65536 байт. Сохранённые видео/audio views независимо разбираются FFprobe/ISO
helper: все encoded packet SHA, PTS, DTS и duration должны точно совпасть
с prepared v2 inputs либо worker A/V output. Для v2 дополнительно совпадают
все байты файлов. В отчёте сохранены SHA восьми полных views и packet sequences.
Byte oracle отдельных запросов привязан к этим отдельно проверенным full views.

Каждый случай проверяет HRESULT, реальный отрицательный payload read при
порче, число chunk callbacks, source error, recovery после reset, сохранность
независимого reader и filesClosed/forget двух group readers. Внутренние v2
resource receipts очищаются штатным bridge destructor; после forget остаётся
только registry .guard. Cache retirement возвращает budget к нулю.
После порчи общего cache не обещается успешное чтение тех же испорченных
данных независимым reader: проверяются сохранность его lease и корректное
закрытие. У v2 независимый context продолжает читать неповреждённое video.

| Проверка | Случаи | Native / collector exit | Результат |
| --- | ---: | --- | --- |
| До исправления, оба профиля | 240 | 1 на профиль / 0 | 4 FAIL, ожидаемое воспроизведение |
| Исправленная сборка, оба профиля | 240 | 0 на профиль / 0 | PASS |
| Прежний payload/cache adapter | 31 | 0 / 0 | PASS |
| Прежний raw-file/pending-pipe | 38 | 0 / 0 | PASS |
| Прежний range/sample/complete | 38 | 0 / 0 | PASS |
| Настоящий DirectShow/MTA graph | 12 | все 0 / 0 | PASS, 4200 совпавших кадров |
| Обычный EXE: queued AAC/video/reordered и AAC API | 32 | все 0 | PASS |
| Обычный EXE: schema-1 indexed/fragmented graph | 2 | player 0 / driver 0 | PASS |

Pre-fix collector 0 означает воспроизведённые четыре FAIL, не runtime PASS.
Итоговая пара red/fixed выполнена в новых каталогах без переименования ранних
отчётов. Сохраняются native stdout/stderr/exit, structured result и worker exits.
Первый fixture ошибочно ждал глобального SourceRetirement::waitEmpty, пока
независимый context намеренно оставался жив: native 2/collector 1. Исправлен
именно fixture: сначала подтверждается ReleaseReceipt основного context,
затем работоспособность второго, затем закрытие обоих и пустой retirement pool.
Первый полный native прогон после этого нашёл две реальные ошибки HRESULT
на mono profile (native 1/collector 1); paired pre-fix повтор подтвердил их
на обоих профилях. Эти начальные отказы не засчитаны PASS.

## Сборка и сохранность

Normal EXE собран отдельно без FOXICMP_PRIVATE_CLASSIC_READ_TEST: новый
AsyncReader.obj явно связан с прежними coherent normal objects/static libs.
Layout, defaults и настройки не менялись; это инкрементальная сборка,
не clean rebuild всех библиотек. Private namespace отсутствует в normal objects.
Обычный EXE проверялся в private copies с null audio и Null Video Renderer.
Private INI до первого запуска содержит ContextMenuPrompted=1 и
ContextMenuEnabled=0. Все 587 защищённых файлов, включая Linux EXE/INI,
и 158 HKCU shell entries совпали до/после. Установленные Windows EXE/профили
не заменялись.
Qt production не менялся: нового Linux бинарника нет, переустановка Linux
не требуется. Harvester, микшер/дисплеи/сеанс и scheduler tasks не менялись.

## Воспроизведение

Нужны собранные Classic x64 static dependencies, MSVC x64 Developer Command
Prompt, Python, FFmpeg/FFprobe и обычный remux worker. Из корня checkout:

```bat
tests\windows\build_classic_projection_io_probe.cmd _bin\classic-projection-io-probe
python -B tests\windows\classic_projection_io_test.py --runner _bin\classic-projection-io-probe\projection.exe --worker WORKER_EXE --report-directory REPORT
```

Build script создаёт безопасный projection.ini. Вывод — новый private каталог,
не установленное приложение. Для pre-fix воспроизведения в отдельной временной
копии применить `git apply --unidiff-zero tests/fixtures/classic-projection-io/pre-fix.patch`,
пересобрать fixture в другом каталоге и передать driver `--expect-pre-fix`.
Reverse patch проверен на точное совпадение байтов исходного reader.

## Что остаётся открытым

Не проверены все v2 container/codec backends, HE-AAC/прочие AAC layouts,
принудительный порядок physical sub-reads внутри projections, CMainFrame/STA
и public IPC reentry. Packet/clock equality не заменяет decoded PCM oracle
Classic, физическую A/V sync и полную waiting/replacement/crash/race/stress
приёмку. Неподтверждающий CancelIoEx драйвер, старые resource/latency outliers,
quota 16 и receipt retention остаются прежними открытыми границами.
