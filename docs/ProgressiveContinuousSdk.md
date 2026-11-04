# SDK 1.82: ограниченный непрерывный remux

Linux Qt, Windows Qt и Windows Classic используют один player-owned контракт
`continuousRemux`, протокол 1, режим `continuous-h264-video-v1`. Это отдельный
явно выбираемый маршрут, не автоматическая замена существующих v1/v2 профилей.
YouTubeHarvester не изменяется. Этот ограниченный режим не закрывает всё ТЗ.

## Поддержанный профиль

- Один локальный MP4, одна обязательная H.264 видеодорожка без audio и B-frames;
  без шифрования, live, сегментных каталогов и смены конфигурации.
- Известные длина и длительность; начало времени 0, timebase 1/10000000.
  Длительность ≤3600 s, источник ≤128 MiB; общий output cache ≤64 MiB.
- Worker ограничен 50000 пакетами, 16 MiB на пакет и 128 MiB суммарных чтений.
  Повторное чтение тоже расходует бюджет. Это пределы, не обещание поддержки
  любого файла, укладывающегося только в размер или длительность.
- SHA-256 подтверждённых ranges обязательны. Незаписанные данные не заменяются
  нулями; file size сам по себе не подтверждает публикацию.
- Исходники не меняются, пакеты копируются без перекодирования. Плеер не получает
  URL/cookies и не скачивает второй экземпляр источника.

Рядом с EXE нужен `foxicmp-remux-stream-worker[.exe]` и соответствующий FFmpeg
runtime. `worker --info` проверяет ABI и возвращает версии реально загруженных
библиотек и contract. На Windows DLL берутся из каталога worker; на Mint —
версионированные системные библиотеки. Linux-пакет требует их явно.

Capability описывает реализацию сборки и обязательные зависимости, не готовность
произвольного входа. `runtimeValidation=per-job-before-playback`: ошибка worker,
недоступная библиотека или неподдержанный поток дают ошибку задания, без успешного
playbackReady. Старые установщики приложения 0.2.0 и SDK ≤1.81 не содержат эту
поставку. Нужна соответствующая новая сборка.

## Явная последовательность

```python
from progressive_client import ProgressiveClient
from progressive_continuous import ContinuousRemux

client = ProgressiveClient(player_path, profile_path)  # Classic: profile_path=None
client.ensure_running()
preview = ContinuousRemux(client, absolute_download_directory)
preview.prepare(first_confirmed_snapshot, resource_id='clip')

# На рабочем потоке загрузчика, после flush и публикации ranges/SHA:
preview.publish(next_snapshot)
preview.heartbeat()  # Только от живого writer, в том числе без новых байтов.

# Только по явному запросу пользователя, после готовности:
if preview.status()['playbackReady']:
    preview.play(title='Предпросмотр', replace=False, activate=False)
    preview.control('pause')
    preview.control('play')
    if preview.status()['outputComplete']:
        preview.control('seek', seconds=5)  # Seek только после outputComplete.

receipt = preview.close(timeout=2)
if receipt['released'] and receipt['sourceFilesClosed']:
    preview.forget(timeout=2)  # Подтвердить обработку, освободить слот задания.
# Без proof сохранять оригиналы и повторить проверку/close позднее.
```

Python 3.10+, стандартная библиотека. Вызовы сериализованы; выполнять на worker
загрузчика, не на GUI. Нет таймеров или автоматического heartbeat от имени writer.
Timeout не разрешает удалить файлы. Одно активное задание на endpoint; для другого
нужны подтверждённые close/forget и новый объект.

Подготовка имеет свои `requestId/jobId`, просмотр — отдельные `requestId/playbackId`.
`sessionId/generation/writerInstanceId` фиксируются при prepare. Чужие ответы не
принимаются и не становятся поводом остановить другое видео.

При потере ответа prepare метод `status()` восстанавливает ID только чтением по
requestId. При потере ответа play — `playback_status()`. Повторный `play()` не
посылается автоматически. publish/heartbeat никогда не запускают или не
возобновляют остановленный просмотр. При потерянном update status различает
`acceptedRevision` и `appliedRevision`; следующую публикацию writer решает явно.

`ok` при подготовке/приёме play не означает `playing`. Проверять status и реальную
позицию. Ожидание байтов не является EOF; outputComplete требует подтверждённого
конца writer и корректного завершения remux. Общая полнота buffering/reasonCodes
и прочая матрица ожиданий остаются задачей приёмки.

В исправленных Qt-сборках `buffering` учитывает реальный `paused-for-cache`
декодера; ручная пауза сохраняет `paused`. Дополнительное необязательное поле
`reasonCode` различает `waiting-range`, `decoder-opening`, `paused` и терминальные
исходы — [описание и проверки](ProgressiveContinuousBuffering.md). Старые сборки
SDK 1.82 и Classic могут не иметь этого поля; наличие SDK helper не доказывает,
что EXE уже содержит исправление.

stop/close терминальны для объекта. close отменяет только этот preview, не download.
Обычный readerRelease output reader также закрывает принадлежащий ему просмотр;
первый ответ консервативен, фактический proof получают через readerStatus.
readerForget после подтверждения совместим с последующим continuousRemuxForget.
Отсутствующая квитанция не считается доказательством освобождения.

## Поставка

Qt CMake install и portable-пакеты включают worker и десять модулей в integration/.
Windows deployment копирует FFmpeg DLL с транзитивными зависимостями и проверяет
--info. Linux .deb сохраняет executable mode worker и требует соответствующие
avformat/avcodec/avutil runtime packages.

Для нового clean stage Classic подготовить runtime payload:

```powershell
./tools/demux/deploy_runtime.ps1 -BinDirectory C:/toolchain/ucrt64/bin `
    -WorkerDirectory E:/build/qt -Destination E:/new/continuous-runtime
```

Содержимое payload размещается рядом с Classic EXE в новом clean stage, integration/
берётся из SDK 1.82 ZIP. Личные INI не копируются. Общий упаковщик
packaging/windows/generate.py включает stage в MSI/EXE обеих Windows-редакций.
Worker/runtime/EXE поставляются вместе; упаковщик не заменяет установленные
приложения и их профили автоматически.

Независимый ZIP создаётся tools/package_progressive_sdk.py; manifest содержит
SDK version, source commit/dirty, hashes файлов и тот же continuousRemux contract.
В нём нет media или EXE. Прежний SDK 1.81 ZIP остаётся неизменным.
