# Backend demux: ограниченная диагностика через подтверждённые ranges

SDK 1.51, Linux Qt, Windows Qt и Classic. Это первый проверяемый этап пункта 2
[чеклиста](ProgressiveCompletionChecklist.md), **не новый playback/remux-профиль**.
Нынешние progressive-профили и обычное открытие файлов сохраняются.
YouTubeHarvester не изменён.

## Контракт

`capabilities.backendDemuxProbe` сообщает `version=1`,
`mode=libavformat-avio-probe-v1`, `analysisOnly=true`, `playbackSupported=false`,
`availability=checked-per-request`. Версия контракта не доказывает наличие
вспомогательного EXE, FFmpeg runtime, декодера или возможность воспроизведения.
Наличие runtime проверяется при каждом задании.

```python
from progressive_resources import ResourceProbe

probe = ResourceProbe(client)
probe.start_media(snapshot, session_directory, "clip", "mp4",
                  packet_limit=32, wait_milliseconds=5000)
result = probe.wait()
media = result["media"]
# media.code == "data" означает только успешный разбор первых пакетов.
assert result["playbackSupported"] is False
probe.forget()  # После released=true; старые reader receipts подтверждаются явно.
```

Эквивалентный запрос:

```json
{
  "version": 1,
  "action": "resourceProbe",
  "session": {"...": "полный опубликованный snapshot schemaVersion=2"},
  "sessionDirectory": "/absolute/session",
  "reads": [],
  "media": {"resourceId": "clip", "formatHint": "mp4", "packetLimit": 32},
  "waitMilliseconds": 5000
}
```

Здесь `session` — поясняющий placeholder, а не валидный manifest. Применяются
обычные требования [v2](ProgressiveManifestV2.md), относительных имён,
[SHA-256](ProgressiveIntegrity.md), writer identity/revision и
[reader leases](ProgressiveReaderLeases.md). SHA-256 для этого режима обязателен.
`media` несовместим с `segment` и непустым `reads`. Существующие запросы без
`media` не меняются. SDK проверяет capability до отправки задания.

Статус и управление используют прежние `resourceProbeStatus/Update/Cancel/Forget`,
requestId и защищённое восстановление после потерянного ответа start.
Новое задание не заменяет активный просмотр. `resourceProbeUpdate` публикует новую
revision того же session/generation; при waiting диапазон проверяется снова.
Истечение ожидания не означает EOF или готовность к воспроизведению.

## Вход и результат

Разрешены только подсказки `mp4`, `m4a`, `mov`, `webm`, `matroska`, `mpegts`,
`adts`, `mp3`, `ogg`, `flac`, `wav`. Они выбирают фиксированный FFmpeg demuxer;
свободный выбор demuxer, URL, HLS, concat и lavfi не предоставляется.
Это **список диагностических входов**, а не расширение codec/playback matrix.
Проверенный набор реальных файлов указан в [отчёте](ProgressiveBackendDemuxValidation-2026-10-31.md).

`media.code=data` возвращает:

- дорожки: index, kind, codec, исходный timebase, размер/хеш extradata,
  sample rate/channels либо width/height;
- ограниченный список пакетов: stream index, размер, SHA-256, PTS/DTS/duration
  в исходных ticks, byte position; неизвестное время — null;
- реально загруженные версии avformat/avcodec/avutil и SHA-256 build configuration.

Сами media bytes и extradata через API не передаются. FFmpeg может использовать
декодер внутри `find_stream_info`, но кадры/звук не выдаются и playback плеера
не запускается. Нет проверки качества/полной длины, нормализации timestamps,
выбора GPU или звукового устройства. Версия FFmpeg на Windows может отличаться от Linux.

`waiting-range`/`waiting-resource` содержат нужный offset/length, если ожидание
получено от общего resource I/O. Fast-start MP4 может разобрать первые пакеты до
complete; MP4 с moov в конце запрашивает недоступную область вместо чтения
неподтверждённого хвоста. Выбор приоритетного диапазона writer здесь не реализован.
Sparse hole не выдаётся за нули. EOF ресурса разрешён только при явном complete
и достижении объявленной длины. При ошибке целостности, нехватке данных или
превышении лимита частичный список пакетов не выдаётся как успех.

Другие возможные коды: `invalid-media`, `no-packets`, `backend-worker-unavailable`,
`backend-library-unavailable`, `backend-symbol-unavailable`, `backend-abi-mismatch`,
`backend-worker-exited`, `backend-pipe-error`, `backend-protocol-error`,
`backend-frame-limit`, `backend-byte-limit`, `backend-time-limit`,
`external-io-denied`, а также ошибки общего resource I/O. Отмена завершает только
диагностическое задание. Освобождение файлов предшествует `released=true`.

## Устройство и пределы

Общий код Qt/Classic запускает отдельный `foxicmp-demux-worker[.exe]` рядом с
EXE плеера. Родитель владеет resource handles, проверяет ranges/SHA/identity и
обслуживает только bounded read-запросы. Дочерний процесс получает format hint,
length/complete и байты; исходные пути и URL ему не передаются. FFmpeg работает
через custom AVIO; callback `io_open` запрещает дополнительные источники.
Схема опирается на [официальный пример AVIO](https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/avio_read_callback.c)
и [контракт AVFormatContext](https://www.ffmpeg.org/doxygen/7.1/structAVFormatContext.html).

Лимиты одного запуска: read до 64 KiB, 16 MiB логических данных, 1–128 пакетов,
32 дорожки, 1 MiB extradata на дорожку, frame stdio до 140000 bytes.
Проверка SHA может читать целый объявленный integrity block; 16 MiB — не обещание
точного лимита физических дисковых чтений. `probesize=1 MiB`,
`analyzeduration=1 s`, `max_probe_packets=128`; это параметры анализа, а не
предельное время открытия всех файлов.

Обмен с worker ограничен 4 секундами; его FFmpeg interrupt — 3 секундами.
Linux worker ограничивает address space 1 GiB, CPU 3 s, file descriptors 64;
Windows Job Object ограничивает память процесса 1 GiB и уничтожает worker при
закрытии задания. Windows write использует overlapped pipe с CancelIoEx;
Linux — неблокирующий pipe, poll и отдельную process group. Посторонние handles
не наследуются. Это изоляция процесса с лимитами, **не OS sandbox**.
Дисковое чтение родителя остаётся с прежним `cancellableDiskIo=false`: 4 секунды
не являются гарантией общего времени запроса при зависшем файловом хранилище.

Повторная попытка при waiting выполняется после публикации update, а не бесконечным
циклом запуска FFmpeg. `waitMilliseconds` — общий срок ожидания публикаций,
не новый лимит на синхронный disk I/O. Логирование FFmpeg в worker отключено. Его библиотеки и зависимости живут до
завершения отдельного процесса, без промежуточной выгрузки глобальных registries.

## Сборка и доставка

Qt CMake собирает helper при наличии `libavformat/avformat.h` и зависимых FFmpeg
headers (`libavformat-dev`, `libavcodec-dev`, `libavutil-dev` на Mint/Ubuntu).
Можно задать `-DFOXICMP_AVFORMAT_INCLUDE_DIR=/absolute/headers`. При отсутствии
headers сам плеер собирается, но helper не устанавливается; runtime-ошибка
остаётся явной, capability по-прежнему имеет checked-per-request.

Отдельная сборка, в том числе helper для Classic:

```sh
cmake -S tools/demux -B /absolute/new/demux-build \
  -DFOXICMP_AVFORMAT_INCLUDE_DIR=/absolute/ffmpeg-headers
cmake --build /absolute/new/demux-build --config Release
```

На Windows MSVC нужно передавать каталог с `libavformat/`, `libavcodec/`,
`libavutil/`, **без замены системных CRT/Windows headers заголовками MinGW**.
Helper динамически загружает DLL с compile-time major и проверяет runtime majors.
Для MSYS2 UCRT64 runtime:

```powershell
./tools/demux/deploy_runtime.ps1 -BinDirectory C:/msys64/ucrt64/bin -Destination C:/clean/stage
```

В чистый stage копируются helper и три FFmpeg DLL с их транзитивными зависимостями;
для распространения также нужны соответствующие licenses/build metadata.
`qt/packaging/windows/deploy.ps1` включает этот шаг автоматически при наличии
helper. Linux использует системные библиотеки совпадающего major.
Classic и Qt сохраняют независимые EXE, profiles и endpoints.
SDK ZIP 1.51 содержит клиент/документацию, **без helper, DLL или новых установщиков**.
Установщики 0.2.0 не объявляются содержащими этот этап.

## Следующий этап

Подключить результаты demux к контролируемой локальной подготовке и явно
разделить native-progressive, progressive-remux, waiting, post-download и
unsupported. Затем — bounded remux без перекодирования, обычный MP4 seek и
согласованная codec matrix. До этого отсутствие decoder/playback-adapter не
маскируется диагностическим `data`.
