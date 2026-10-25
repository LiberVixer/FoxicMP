# SDK предпросмотра загрузки: Linux Qt, Windows Qt и Classic

SDK является частью FoxicMP и не меняет YouTubeHarvester. Python 3.10+,
только стандартная библиотека. Обе Qt-редакции устанавливают файлы в
`integration/`; независимый ZIP для всех редакций создаётся в репозитории командой:

```sh
python3 tools/package_progressive_sdk.py --output /absolute/new/FoxicMP-progressive-sdk.zip
```

В ZIP входят пять Python-модулей, эта инструкция, контракты v1 и draft v2,
пример метаданных v2, отчёты, лицензия и JSON с исходным commit, dirty-флагом и
SHA-256 файлов. Схема воспроизведения остаётся v1:
две отдельные MP4/M4A H.264/AAC дорожки. [Расширенное ТЗ](ProgressiveIntegrationPlan.md)
ещё не выполнено. SDK не скачивает URL и не выполняет remux.

Проверки трёх редакций и ограничения: [отчёт](ProgressiveSdkValidation-2026-10-25.md).
Этот SDK требует новые сборки плеера с `releaseAcknowledgement=true`;
опубликованные установщики 0.2.0 не содержат это расширение.

## Проверка расширенной схемы (SDK 1.3)

Новые сборки отдельно объявляют `validationSchemaVersions:[2]` и
`playbackSchemaVersions:[1]`. [Контракт v2](ProgressiveManifestV2.md) описывает
ресурсы, подтверждённые диапазоны, сегменты, несколько дорожек и поздние главы.
Эта часть SDK проверяет метаданные, не открывая медиа и не прерывая просмотр:

```python
from progressive_schema import unsigned64, signed64

snapshot_v2['revision'] = unsigned64(revision, positive=True)
snapshot_v2['timeline']['timestampOffset'] = signed64(offset_ticks)
reply = client.validate_session(snapshot_v2, previous=previous_snapshot_or_none)
assert reply['validated'] and reply['playbackSupported'] is False
```

Весь JSON-запрос, включая previous, ограничен 65536 байтами. Большие целые —
десятичные строки; float/bool и округление не допускаются. `progressive-v2-vod.json`
в ZIP — синтетический пример метаданных, без соответствующего media-файла.
`PreviewSession.publish()` и writer-стенд по-прежнему используют v1. `start`/`probe`
v2 возвращают `unsupported_schema`: codecs/live/remux ещё не интегрированы.
[Проверки модели](ProgressiveModelValidation-2026-10-25.md) не являются сквозным
тестом Harvester; исходники Harvester не изменялись.

## Подключение к событиям writer

Добавьте каталог SDK в путь импорта Python. Укажите EXE своей редакции:
`foxicmp-qt`, `FoxicMPq.exe` или `FoxicMP64.exe`. Клиент сам выберет транспорт.
Новые интеграции используют player-owned API и не запускают старый provider.

```python
from progressive_client import ProgressiveClient
from progressive_sdk import PreviewSession

client = ProgressiveClient(player_executable, profile_directory)  # profile optional
client.ensure_running()  # Discovery/readiness не открывает видео.
preview = PreviewSession(client, session_directory, title=title, activate=False)

# В worker загрузчика, после flush/записи доступных байтов:
reply = preview.publish(full_v1_snapshot)
# При отсутствии новых байтов живой writer вызывает это каждые <= 5 секунд:
preview.heartbeat()
# Когда обе дорожки полностью записаны, закончены, и ревизия увеличена:
preview.publish(final_snapshot)  # state=complete у сеанса и обеих дорожек
# Если writer завершился с ошибкой:
# preview.writer_error("Ошибка записи локальной дорожки")

# Stop/закрытие предпросмотра, а не отмена скачивания:
receipt = preview.close(timeout=2)
if receipt.get("released") is True:
    pass  # Этот читатель больше не удерживает исходники.
else:
    pass  # Сохранить исходники; позднее повторить close/status.
```

`publish` получает полный снимок со строго возрастающей ревизией. Только реальные
записанные префиксы; сетевой счётчик и размер sparse-файла не являются подтверждением.
Идентификатор и generation неизменны на срок объекта SDK. Другому поколению нужен
новый объект и явный запуск. Пока нет инициализации, SDK возвращает
`waiting-initialization`; затем использует настоящий `probe` плеера. Отказ probe
не маскируется успехом; после новых данных можно опубликовать следующую ревизию.

Вызовы сериализованы, но синхронные: выполнять их в worker, не в GUI-потоке Harvester.
Скрытых потоков и таймеров SDK нет. Ошибка предпросмотра не посылает загрузчику
команду отмены. После `close` дальнейшие события не перезапускают просмотр.
В диагностические сообщения не передавать cookies, подписанные URL и ключи.

## Подтверждение освобождения

SDK 1.4 в новых сборках дополнительно использует [reader leases](ProgressiveReaderLeases.md):
readerId привязан к реальным файлам и OS process identity, сохраняется квитанция
для recovery после crash. `PreviewSession.reader_id` доступен после tracked start;
close использует именно его, подтверждает/забывает квитанцию и кэширует результат.
Ручные методы: `reader_status`, `release_reader`, `forget_reader`, `readers`.
Потерянный ответ start остаётся неопределённым: сохранить файлы, затем обнаружить
читателей по исходным sessionId/generation. [Результаты](ProgressiveReaderValidation-2026-10-25.md).

Новые сборки объявляют `releaseAcknowledgement=true` и действие `release` в
capabilities. `release` прерывает только reader текущего sessionId. `status` и
ответы команд содержат `released`: true ставится после завершения деструктора
источника и закрытия всех медиа-дескрипторов, а не просто при `active=false`.
Повторные release/status после естественного конца безопасны.

`PreviewSession` проверяет эту возможность до старта; старую сборку без неё надо
обновить. Таймаут, отсутствие ответа, неизвестный сеанс и смерть одного из
процессов сами по себе не разрешают удаление. При нескольких окнах/профилях
загрузчик должен получить подтверждения от всех открытых им читателей.
В SDK 1.4 opt-in registry существует для player-owned v1. Для legacy/provider
и динамических ресурсов v2 его наличие не объявляется. Неизвестная/забытая квитанция
и пустой список не заменяют положительное подтверждение конкретных читателей.

## Локальный writer-стенд

В FoxicMP сгенерируйте синтетические fixtures штатным
`qt/tests/generate_media.py`. Укажите отдельную тестовую копию Classic с
отключённой регистрацией контекстного меню либо отдельный профиль Qt.
Каталог вывода должен ещё не существовать:

```sh
python3 progressive_writer.py --player /absolute/foxicmp-qt --profile /absolute/test-profile \
  --ensure --video /absolute/fixtures/progressive/video.mp4 \
  --audio /absolute/fixtures/progressive/audio.m4a --directory /absolute/new-session
```

Стенд читает каждый из двух локальных входов один раз, дописывает выходы целыми
fMP4-фрагментами с fsync и передаёт события через SDK. Он удерживает complete,
проверяет позицию >= 10 секунд до complete, ждёт естественный конец и
`released=true`. JSON-отчёт сохраняется в `report.json`; входы не изменяются,
выходы автоматически не удаляются. Это offline-проверка контракта, не сквозная
проверка реального Harvester и не измерение физической A/V точности.

## Старый manifest/provider и Windows Qt

Совместимый вход Qt сохраняется:

```text
FoxicMPq.exe --harvester-session C:\download\session.json
foxicmp-qt --harvester-session /absolute/session.json
FoxicMP64.exe /harvester-session C:\download\session.json
```

Для этого входа нужен действующий старый provider; один snapshot не заменяет его.
Windows Qt теперь подключается к `FoxicMP.Harvester.<sessionId>`, сверяет аккаунт
сервера, отправляет attached с реальными PID и readerId, проверяет сохранение lease
и только затем открывает дорожки. Состояние JSON и heartbeat обрабатываются
продолжающимся worker, включая complete/failed и новые префиксы. Закрытие файлов
предшествует released старому provider. Внешний Python-процесс не выдаёт себя за
PID плеера. Разрыв соединения не трактуется как успешный EOF и не снимает lease
до подтверждения provider либо смерти процесса.

При разрыве Windows Qt ставит незавершённый просмотр в ожидание и пытается
переподключиться с новой проверкой аккаунта и сохранённого lease. Смена
sessionId/generation либо ошибка writer не маскируются как восстановление.
Первичное получение lease синхронно ограничено двумя секундами; полное вынесение
probe/подготовки источника из UI остаётся работой следующего этапа.

Исходный Windows provider хранит lease по PID, поэтому Qt запрещает одновременно
открывать два legacy-reader одного sessionId в одном процессе. Новая интеграция
не должна использовать этот старый маршрут: единый клиент и события выше заменяют
manifest/provider для всех трёх редакций без Win32-кода в Harvester.
