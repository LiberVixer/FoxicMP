# SDK 1.83: явный continuous H.264 профиль с B-frames

5 ноября 2026. Общий контракт Linux Qt, Windows Qt и FoxicMP Classic.
Основание: [worker](ProgressiveContinuousReorder.md),
[managed cache/декодеры](ProgressiveManagedReorder.md),
[настоящие Player/API](ProgressiveReorderPlayerApi.md).
[Результаты SDK и точные exit codes](ProgressiveReorderedSdkValidation-2026-11-05.json).

## Выбор и совместимость

```python
from progressive_client import ProgressiveClient
from progressive_continuous import ContinuousRemux, REORDERED_MODE

client = ProgressiveClient(player_executable, private_profile_directory)
job = ContinuousRemux(client, session_directory, mode=REORDERED_MODE)
job.prepare(snapshot, resource_id)
# Рабочий поток загрузчика публикует записанные ranges/SHA и heartbeat.
# После job.status()['playbackReady'] пользователь явно вызывает:
job.play(title="Предпросмотр")
```

Для Classic второй аргумент `ProgressiveClient` — `None`; Qt использует
отдельную папку профиля. Ожидание готовности и обработка ошибок выполняются
рабочим потоком по [общей последовательности](ProgressiveContinuousSdk.md).

`mode` задаётся keyword-only при создании helper и сохраняется для всей попытки.
`job.mode` доступен только для чтения. Редактирование `job.capability` не меняет
профиль запросов. Все job/playback/cleanup ответы проверяются по mode и полному
набору идентификаторов; ответ другого профиля не принимается при recovery.

Прежний `ContinuousRemux(client, directory)` сохраняет поведение SDK 1.82 и
не добавляет mode в start payload. Он работает и с прежними плеерами. Новый
профиль разрешён только при явной capability; helper отклоняет отсутствующую или
неполную декларацию до start. Неизвестное имя профиля отклоняется до любого IPC.
Автоматического fallback или запуска после writer update/heartbeat нет.

## Capability

`continuousRemux.mode` и `continuousRemux.bFrames=false` описывают default
`continuous-h264-video-v1`. Общие budgets и правила Source/Reader остаются на
верхнем уровне. `modeSelection=continuousRemuxStart.mode` объявляет selector.

`continuousRemux.profiles` содержит два entry:

| Mode | B-frames | Нужен явный selector |
|---|---:|---:|
| `continuous-h264-video-v1` | нет | нет |
| `continuous-h264-reordered-video-v1` | да | да |

Каждый entry имеет version=1, MP4/H.264 video-only, audio=false и
`inheritsCommonContract=true`. Новый профиль принимает и no-B видео; загрузчику
не требуется угадывать число B-frames перед выбором поддержанного профиля.
`worker --info`, API discovery и SDK manifest содержат тот же общий контракт.
Это описание реализации, не гарантия готовности произвольного файла или GPU.
Runtime проверяется до playback commit для каждого задания.

Остались прежние ограничения: одна локальная H.264 video track, known-length
fast-start MP4, zero-origin VOD, SHA/ranges, источник ≤128 MiB, output/shared cache
≤64 MiB, ≤50000 packets, seek после output complete. Audio/AAC trim, live,
шифрование и смена конфигурации этим профилем не добавляются. Все прежние native
и segmented v1/v2 маршруты сохраняются.

## Проверки и поставка

23 SDK unit checks: старый payload, отказ старого player для нового mode,
строгая декларация capability, read-only mode, lost-reply recovery без повторного
start/play и проверка mode до принятия чужого job ID.

На реальных endpoints каждой редакции: шесть новых SDK сценариев, пять default
и пять с неизменёнными модулями из SDK 1.82 ZIP. Подтверждены explicit play,
pause/append/complete/seek/EOF, Stop, независимый endpoint, filesClosed/forget,
отказ audio и сохранение default запрета B-frames; новый профиль также играет
no-B source. Дополнительно повторены 18 playback API cases каждой редакции.
Для пакетов проверяются распакованные runtime и импорт именно упакованных
SDK-модулей, их SHA и совпадение с source manifest.

SDK ZIP не содержит media/EXE. Windows portable Classic/Qt включают проверенные
EXE, worker/runtime/licenses и integration; Classic поставляет factory INI,
который не запускается тестами. Linux tar/.deb содержат executable workers и
зависимости соответствующего FFmpeg ABI. Личные INI/история в пакеты не попадают.
Прежние SDK ZIP сохраняются неизменными. Обычные установщики приложения 0.2.0
из более ранних выпусков не приобретают этот профиль от одного обновления SDK.

[Опубликованные пакеты SDK 1.83](https://github.com/LiberVixer/FoxicMP/releases/tag/progressive-api-v1.83)
и [аттестация поставки](ProgressiveReorderedSdkPackagesValidation-2026-11-05.json):
39 сценариев распакованных пакетов, 11 установленного Linux, серверные SHA-256
всех семи assets; SDK ZIP и оба манифеста скачаны обратно. Полная загрузка
бинарных архивов с GitHub в этой аттестации не заявляется.

## Границы

Полная ТЗ-приёмка остаётся открытой. Исходный SDK/UI срез — H.264 25 fps.
Дополнительно [проверены публичные SDK/Player fractional/VFR профили](ProgressiveReorderedSdkTiming.md)
во всех трёх редакциях: 25, 30000/1001, 60000/1001 fps и VFR, 36 seek, точные
packet/pixel эталоны managed cache. Native проверки изображений самих декодеров
остаются отдельным доказательством; физическая точность вывода окна не измерялась.
Audio, многочасовой stress, физический A/V и все crash/latency варианты не закрыты.
Первый [внутренний AAC aligned worker](ProgressiveContinuousAacAligned.md) ещё
не подключён к SDK: его частные проверки не расширяют публичную capability.

При повторении Windows UI теста выявлены недостоверные раздельные проверки
registry после уже полученного proof: metadata API допускает
`reader_registry_busy`, а QTRY может заново вычислить выражение после успеха.
Тест теперь сохраняет успешную квитанцию, проверяет её flags/reason вместе и
отдельно повторяет idempotent release в прежнем bounded интервале. Native guard
детерминированно проверяет busy status/release и последующее восстановление.
Production reader code и лимиты не менялись. Самый первый отказ без полного
JSON не объявляется объяснённым; исторические Windows/Linux/Classic outliers
сохранены в чеклисте. Wayland и отложенное оборудование не проверяются.

YouTubeHarvester не изменялся. Этот SDK позволяет другому проекту отдельно
подключить профиль; факт реальной интеграции Harvester этим выпуском не заявлен.


6 ноября: [частный AAC sample-window компонент](ProgressiveContinuousAacWindow.md)
проверен с реальными PCM adapters Qt обеих ОС и внутренним decoder Classic.
Managed continuous AV parent/cache/entry points пока не подключены;
публичный SDK 1.83 и его video-only capability остаются прежними.
