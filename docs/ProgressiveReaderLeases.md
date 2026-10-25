# Читатели, закреплённые файлы и восстановление

SDK 1.4, player-owned API v1. Работает в Linux Qt, Windows Qt и Classic.
YouTubeHarvester не изменяется. Это runtime-часть этапа 3, а не media backend v2.

## Согласование и открытие

Новые сборки объявляют `readerLeaseProtocol:1`, `readerLeaseOptIn:true`,
`maxReaderReceipts:128` и действия `readerStatus`, `readerRelease`, `readerForget`,
`readers`. Старые вызовы start и прежний releaseAcknowledgement сохраняются.
Учёт включается явным `trackReader:true` во внешнем start, без изменения manifest v1.
Probe/validate не создают записей. Legacy/provider этим расширением не заменяется.

```python
from progressive_client import ProgressiveClient
from progressive_sdk import PreviewSession

client = ProgressiveClient(player_path, profile_path)
client.ensure_running()
preview = PreviewSession(client, download_directory)
reply = preview.publish(snapshot_v1)
reader_id = preview.reader_id  # Сохранить вместе с редакцией/профилем и session/generation.
```

SDK автоматически запрашивает учёт, если он объявлен плеером; для предыдущих сборок
сохраняется старый маршрут. Reader ID — случайные 32 lowercase hex на одно открытие.
Повторный start того же sessionId/generation возвращает тот же reader ID. Новое
поколение требует явного replace и получает новый ID; старый release не закрывает
заменившее его видео. Рабочие endpoints/профили редакций независимы.
SDK также передаёт readerId с update/heartbeat/status/error. Плеер отклоняет
команду прежнего читателя после замены (`reader_mismatch`), поэтому запоздавшая
ошибка старого writer не останавливает новое поколение. В ручных v1-вызовах
readerId допускается как дополнительное поле этих команд, cancel и release.
Если действующий сеанс открыт без учёта, tracked повтор получает reader_not_tracked:
такой сеанс следует явно закрыть перед новым tracked start.

## Что доказывает удержание и освобождение

Плеер записывает под своим OS-пользователем ограниченную квитанцию с sessionId,
точной строковой generation, PID и birth identity, readerId, идентификаторами
ресурсов и реальными device/file ID из закреплённых дескрипторов. Пути, cookies и
сетевые URL в неё не попадают. Клиент не назначает PID, birth token или file ID.

Qt и Classic держат системную блокировку квитанции до уничтожения источника.
ReaderBook связан с этим временем жизни: начало закрытия не снимает удержание;
`filesClosed` вызывается только после закрытия всех исходных media-дескрипторов.
Отдельная ссылка читателя/декодера сохраняет источник и блокировку после Stop.

Квитанция находится в приватном runtime-каталоге профиля/endpoint. На Linux каталог
0700 и файлы 0600, проверяются владелец, отсутствие symlink/hardlink и тип объекта;
операции выполняются относительно закреплённого directory FD. На Windows —
user-only protected DACL, проверка владельца, типов, ссылок и reparse points;
открытые handles не разрешают заменять каталог/квитанцию во время проверки.
Дескрипторы не наследуются при запуске дочерних программ. Граница доверия — тот же
пользователь ОС, как у IPC; это не защита от произвольного кода того же аккаунта.

`readerStatus` возвращает `reader`, в том числе `released` и `reason`:

| reason | released | Основание |
|---|---|---|
| lease-held | false | Квитанция ещё заблокирована источником |
| files-closed | true | Блокировка снята, долговечное подтверждение записано после закрытия файлов |
| process-exited | true | Блокировки нет, ОС подтверждает завершение процесса |
| pid-reused | true | Блокировки нет, PID принадлежит другому экземпляру процесса |
| indeterminate | false | Нет достаточных доказательств освобождения |

Linux проверяет boot ID + `/proc/PID/stat` starttime и состояние процесса;
Windows — creation FILETIME и process handle. PID без birth token недостаточно.
Неизвестное состояние процесса, запрет доступа, отсутствие heartbeat, таймаут или
потеря IPC не освобождают читателя. После crash снятие file lock само по себе
недостаточно: квитанция без filesClosed требует доказательства смерти процесса.
Повреждённая/отсутствующая квитанция и ошибка I/O дают ошибку, а не released=true.
Отсутствие квитанций после очистки temp/reboot также не является подтверждением.

## Закрытие, подтверждение и восстановление

```python
# Только этот reader; другое видео/поколение и сама загрузка не закрываются.
receipt = client.release_reader(reader_id, timeout=2)
if receipt['released']:
    client.forget_reader(reader_id)  # Подтверждение обработано, освободить слот.
```

`readerRelease` закрывает источник только при совпадении readerId текущего сеанса;
для прежнего/чужого этому окну читателя он лишь возвращает его статус.
`release_reader()` использует один общий deadline и опрашивает readerStatus.
Никакой timeout не превращается в разрешение удалить исходники.

`PreviewSession.close()` использует reader ID; после положительного подтверждения
SDK запоминает квитанцию и делает forget. Повторный close возвращает сохранённый
результат. Неудачный forget сохраняет квитанцию на диске и не отменяет уже полученное
доказательство. Для ручной работы используйте методы клиента выше.
Forget живого/неопределённого читателя запрещён (`reader_in_use`). После forget
статус неизвестен (`unknown_reader`), что само по себе не означает освобождения.

После потери ответа start скачивание сохраняет файлы. SDK помечает start как
неопределённый и не выдаёт released=true даже если не получил readerId. После
восстановления API можно обнаружить читателей, сохранив исходный session/generation:

```python
client.ensure_running()  # Discovery/запуск окна не открывают медиа.
readers = client.readers(session_id, generation)  # Только известный профиль/endpoint.
for reader in readers:
    receipt = client.release_reader(reader['readerId'])
    if receipt['released']:
        client.forget_reader(reader['readerId'])
```

Учитывать все читатели во всех известных профилях/редакциях, открытые загрузчиком.
Освобождение одного не разрешает удалять файл, удерживаемый другим. Пустой список
не доказывает отсутствие неизвестного/legacy-reader. API не удаляет media-файлы
и не меняет состояние загрузчика.

## Лимиты и оставшаяся работа

В каталоге не более 128 квитанций, включая ещё не подтверждённые завершённые.
При заполнении новый tracked start отклоняется (`reader_limit`), без тихого
выключения учёта. Создание/forget сериализуются короткой системной блокировкой;
конкурирующая команда может получить `reader_registry_busy` и быть повторена.
Новый start после потери ответа сначала разрешается через enumeration; слепой
replay команды не заменяет recovery.

Raw `readers` требует version/action/sessionId/generation и допускает afterReaderId.
Generation — каноническая положительная uint64-строка. Ответ содержит readers,
hasMore, при необходимости nextReaderId. Страница ограничена 16 квитанциями и
48 KiB данных; Python-клиент обходит страницы, контролируя продвижение курсора.
Строгий общий предел команды/ответа 64 KiB не увеличен.

CLI использует тот же транспорт:

```text
python3 progressive_client.py --player PLAYER --profile PROFILE start --snapshot session.json --session-directory DIRECTORY --track-reader
python3 progressive_client.py --player PLAYER --profile PROFILE readerStatus --reader-id READER_ID
python3 progressive_client.py --player PLAYER --profile PROFILE readers --session-id SESSION_ID --generation 1
python3 progressive_client.py --player PLAYER --profile PROFILE readerRelease --reader-id READER_ID
python3 progressive_client.py --player PLAYER --profile PROFILE readerForget --reader-id READER_ID
```

Область учёта сейчас — явно запрошенные player-owned v1 sources с двумя MP4/M4A
дорожками. Новая схема v2 остаётся validation-only. Подключение этого механизма
к динамическому набору ресурсов v2, безопасной компактизации live-окна, управляемому
I/O и demux/remux ещё не выполнено. Нет автоматического удаления файлов, таймеров
очистки или скрытого перезапуска просмотра.
