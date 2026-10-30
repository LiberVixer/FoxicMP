# Подтверждённые диапазоны v2 и асинхронная I/O-проверка

SDK 1.5. Общий код Linux Qt, Windows Qt и Classic. Это этап подготовки ресурсов,
**без декодирования или remux в resourceProbe**. SDK 1.6 отдельно подключает
ResourceSource к [ограниченному медиапрофилю v2](ProgressiveMediaV2.md).
resourceIoPlaybackSupported=false относится к диагностике, а validate v2
сохраняет прежний смысл и не открывает файлы. YouTubeHarvester не изменяется.

SDK 1.30 добавляет [SHA-256 проверку блоков перед возвратом байтов](ProgressiveIntegrity.md). Прежние ограничения проверки payload ниже относятся к ресурсам без `integrity`.

## Что реализовано

`shared/progressiveresourceio.h` предоставляет ResourceSource для диагностики и медиамоста:

- Привязанный на срок источника абсолютный sessionDirectory, безопасные имена v2,
  проверенные локальные файлы и реальные device/file ID. Открытые ресурсы не
  переоткрываются по имени при rename или замене исходного пути.
- Чтение только подтверждённых диапазонов. Соседние диапазоны объединяются;
  short read заканчивается у первого пробела, дыры не выдаются как нули.
- Ожидание публикации/появления файла, прерываемое cancel. Пауза writer, неизвестная
  длина, истёкший wait и отсутствие новых данных не превращаются в EOF.
- EOF только у явно complete-ресурса с известной длиной, после проверки файла;
  ошибка writer/ресурса остаётся ошибкой. Заданные 64-битные смещения не проходят
  через floating point; на обеих ОС проверено реальное sparse-чтение выше 4 ГиБ.
- Обновление снимка через общий Advance, привязка поздних ресурсов при первом
  обращении, запрет нового attach к retired-ресурсу. Удаление ресурсов/сегментов
  из снимка пока запрещено; компактизация live-окна ещё не реализована.
- Отдельная квитанция reader lease на каждый закреплённый ресурс. Она освобождается
  после закрытия его файлового дескриптора. Последний владелец ResourceSource
  определяет окончательное закрытие; cancel сам по себе не означает release.

Один read ограничен 65536 байтами. Read с нулевым count не читает файл и не означает
EOF. Если запрошено больше подтверждённой непрерывной части, возвращаются только
доступные байты; следующий read у пробела получает waiting-range.

На Linux путь открывается по компонентам без symlink, ресурсы — относительно
закреплённого directory FD. Проверяются тип, UID и отсутствие hardlink; NFS,
CIFS/SMB и FUSE исключены из этого адаптера. На Windows требуются local fixed drive,
отсутствие reparse points/hardlinks, закреплённый путь и владелец из текущего token:
SID пользователя либо его OS default owner. Это учитывает файлы повышенного
процесса с default owner Administrators, без разрешения произвольных групп.
Приватные квитанции по-прежнему имеют отдельную user-only ACL/0700/0600.

Усечение подтверждённых данных обнаруживается при read/publish. Дополнительно
сохраняется и сверяется **только последний успешный read каждого файла**, максимум
64 КиБ на ресурс. Это обнаружение перезаписи текущего окна, а не доказательство
неизменности всех опубликованных байтов. Полная защита произвольной перезаписи
ранее прочитанных/непрочитанных данных остаётся работой backend/integrity-слоя.

## Диагностика через API

Capabilities новых сборок:

```json
{"resourceIoProbeVersion":1,"resourceIoSchemaVersions":[2],"resourceIoPlaybackSupported":false}
```

`resourceProbe` запускает worker и быстро возвращает probeId. Рабочее видео не
заменяется. Worker действительно читает заданные участки через ResourceSource,
но API возвращает только коды/счётчики, без содержимого медиа и без запуска decoder.

| Команда | Поля сверх version/action | Результат |
|---|---|---|
| resourceProbe | session (v2), sessionDirectory, reads; optional waitMilliseconds, requestId | Новая асинхронная I/O-проверка |
| resourceProbeStatus | optional probeId | Статус текущей либо указанной задачи; read-only discovery |
| resourceProbeUpdate | probeId, session | Ограниченная очередь полного снимка; I/O-проверка происходит в worker |
| resourceProbeCancel | probeId | Прерывание только этой задачи |
| resourceProbeForget | probeId | Подтверждение закрытия и асинхронный forget её квитанций |

Один профиль обслуживает одну задачу до её подтверждения. reads содержит 1–16
объектов `{resourceId,offset,length}`; offset/length — канонические строки,
length 0…65536. Сумма не превышает 1 МиБ. waitMilliseconds — JSON integer 0…10000,
общий бюджет ожидания для списка. Очередь обновлений — максимум 8 снимков;
переполнение даёт probe_backpressure, без скрытого отбрасывания ревизий.
Общий лимит JSON 65536 байт сохранён.

SDK задаёт случайный requestId до отправки. Точное повторение того же запроса
возвращает ту же задачу, включая её терминальный результат; другой payload с
прежним requestId отклоняется как probe_conflict. После потерянного ответа SDK
проверяет не только sessionId/generation, но и requestId. Чужая проверка того же
поколения не принимается за свою и не отменяется.

Ответ всегда имеет mode=resource-io-only и playbackSupported=false. acceptedRevision
означает принятую в очередь ревизию, appliedRevision — фактически применённую worker.
`ok=true` не означает готовность декодера или успех всех чтений.

Состояния queued/running/closing/finished/cancelled/error/forgetting/forgotten
относятся **к проверке**, а не к медиавоспроизведению. Каждый reads-result содержит
code, bytesRead, offset/length и reason. waiting-range / waiting-resource сохраняют
смысл ожидания; data может быть коротким. allReadable=true требует полного count
каждого read либо подтверждённого EOF и всё равно не доказывает поддержку формата.

released=true появляется после уничтожения I/O-источника. readerIds в терминальном
результате позволяют проверить его закрытые квитанции через readerStatus. Пока
источник жив, активные квитанции видны через readers(sessionId,generation).
resourceProbeForget запрещён до закрытия файлов; worker удаляет только собственные
положительно освобождённые квитанции и переходит в forgotten. Ошибка очистки не
превращается в успех. Shutdown без forget оставляет закрытые записи для recovery.
Ни одна команда не удаляет исходные media-файлы и не останавливает загрузчик.

## Python SDK

```python
from progressive_client import ProgressiveClient
from progressive_resources import ResourceProbe
from progressive_schema import unsigned64

client = ProgressiveClient(player_path, profile_path)
probe = ResourceProbe(client)
probe.start(snapshot_v2, session_directory, [
    {"resourceId":"media", "offset":unsigned64(offset, byte_offset=True), "length":"4096"}
], wait_milliseconds=5000)
# После flush новых байтов, при необходимости:
probe.publish(next_snapshot_v2)
result = probe.wait(timeout=10)  # Timeout не является подтверждением release.
if result["released"]:
    probe.forget()
```

После потери ответа start вызвать status того же объекта: он сверяет requestId.
Для отмены использовать probe.cancel(), затем дождаться released и сделать forget.
readerRelease по квитанции I/O не заменяет отмену задачи; обычные preview-команды
и текущий playback не затрагиваются. Никаких скрытых фоновых таймеров в SDK нет.

CLI принимает snapshot и ограниченный JSON-объект `{"reads":[...]}`:

```text
python3 progressive_resources.py --player PLAYER --profile PROFILE --directory DIRECTORY --snapshot session-v2.json --reads reads.json --wait-ms 5000
```

CLI ждёт результат и подтверждает закрытые квитанции. Плеер должен быть уже запущен.
[Результаты трёх редакций](ProgressiveResourceValidation-2026-10-26.md).

## Что дальше

ResourceSource ещё не подключён к demux/decode, libmpv callbacks или DirectShow
reader. Реальный Harvester, расширенная codec/container-матрица, HLS/DASH/live,
физический A/V drift и полная проверка неизменности опубликованных данных не
проверялись этим этапом. Следующий шаг — использовать этот I/O в backend demux/remux,
с сохранением общей шкалы и проверкой реального воспроизведения до complete.
