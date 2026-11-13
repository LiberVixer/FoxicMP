# FoxicMP: оставшиеся работы по progressive-ТЗ

Рабочая сводка на 13 ноября 2026, включая bounded AV1/AAC continuous SDK 1.91.
Основание — [ТЗ](ProgressiveTechnicalSpec-2026-10-25.ru.md) и
[подробный чеклист с доказательствами](ProgressiveCompletionChecklist.md).
Эта сводка группирует оставшиеся работы, не меняя объём ТЗ и не объявляя
закрытыми широкие пункты по результатам узких тестов.

В подробном чеклисте 136 выполненных и 77 открытых checkbox. Это число записей,
не процент готовности: общая приёмка повторяет требования нескольких разделов.
Ниже они сведены к пакетам работ с одним проверяемым результатом на пакет.
Обозначения R — оставшаяся работа; H — исторический неатрибутированный отказ;
E — внешняя зависимость. Каждый R остаётся открытым, пока не выполнен его критерий.

## Уже проверено: повторно не реализовывать

- Ограниченные native-progressive fMP4/WebM/TS, обычный head/tail MP4,
  самостоятельные аудиоформаты и их заявленные codec/layout profiles.
  Наличие общего demux probe не означает подключённое playback всех форматов.
- Публичный continuous-remux MP4/H.264, B-frames и AAC PCM-window в заявленных
  профилях; SDK 1.85–1.87 добавляют начальную позицию, ограниченные growing seeks
  и одну ожидающую цель. Неполученный диапазон не декодируется.
- По 31 минуте A/V трёх редакций в трёх отдельных bounded jobs; это не один
  непрерывный 30-minute источник и не измерение физического выхода.
- [Linux memory](ProgressiveLinuxMemory.md), [output receipt contention](ProgressiveReceiptClose.md),
  [original source close/status](ProgressiveSourceClose.md): исправлены и проверены,
  установленный Linux обновлён с сохранением профиля. Actual stalled OS I/O
  и все прочие codec/renderer paths этим не закрыты.

## Порядок разработки

| ID | Осталось сделать | Критерий завершения | Подробные разделы |
| --- | --- | --- | --- |
| R01 | Расширить continuous-remux: MP4/M4A/MOV, WebM/Matroska, MPEG-TS и допустимые потоки за пределами нынешних профилей | Для каждой включённой строки — playback до complete в трёх редакциях, явный режим/ограничение, неизменные compressed payload и source SHA; probe без playback не засчитывается | 2, 3 |
| R02 | Дополнить audio matrix: HE-AAC, оставшиеся rates/layouts, multichannel, Ogg chaining/origins, неподдержанные WAV/FLAC/MPEG варианты; MPEG-2/MPEG-4 Part 2 и video-only без аудиоустройства | Входные параметры проверяются по содержимому; partial packets ждут; реальное декодирование/выбор дорожки/EOF проходят во всех применимых редакциях. Hardware-подтверждение не подменяется null output | 4 |
| R03 | Расширить sparse/out-of-order ranges и большие индексы/каталоги | Holes не читаются как нули, file size не подтверждает bytes; новый формат внешнего индекса остаётся bounded, старт/seek большого каталога измерены; прежние RF64 и tail-moov tests сохранены | 5 |
| R04 | Проверить временную модель всей расширяемой матрицы | VFR/timebase/offsets/preroll/edit lists/gapless, TS discontinuity/rollover и общий интервал обязательных потоков; pause/resume/seek сохраняют одну согласованную шкалу | 6 |
| R05 | Дополнить конечные HLS/DASH через локальные init/media resources | Параллельные публикации, gaps/discontinuity, rebasing, codec/config и длинные каталоги проходят с ограниченными handles/cache; плеер не загружает сеть | 7 |
| R06 | Смена качества и decoder configuration | Переинициализация на допустимой точке; позиция, pause/speed/volume и выбранные дорожки сохранены; неполученная конфигурация даёт точное waiting | 8 |
| R07 | Расширить динамические audio tracks в muxed/segmented источниках | Стабильные ID/language/default/forced, позднее добавление/удаление; optional track не останавливает primary, выбранная partial track ждёт свои bytes; ошибку не скрывает смена языка | 9 |
| R08 | Расширить растущие и embedded subtitles | UTF-8/UTF-16 и partial cues, SRT/WebVTT/ASS/SSA/TTML/JSON3/SRV, embedded и графические форматы; ограниченные fonts/attachments, безопасный разбор и документированная потеря оформления при адаптации | 10 |
| R09 | Дополнить главы и metadata | Container/writer chapters со стабильными ID и late updates без сброса позиции; неизвестная duration и недоступные главы; covers, rotation/aspect и HDR/color metadata применяются либо имеют точное ограничение | 11 |
| R10 | Live/event HLS и dynamic DASH | Неизвестная duration, скользящее окно, reconnect/target delay; отсутствие сегмента не EOF, retirement только после release всех readers, отдельная причина seek вне окна | 12 |
| R11 | Оставшиеся блокирующие операции и управляемая отмена I/O | Сначала воспроизводимый owned test на конкретной границе, затем bounded UI/worker shutdown и честные receipt states; Qt GL/Classic graph/COM и original disk/receipt I/O проверяются отдельно | 2, 13 |
| R12 | Расширить crash/recovery и идентичность источника | Writer/player/worker crash, несколько readers, reopen/resume, rename/finalize и long-path/Unicode cases сохраняют source identity; error не становится EOF и preview не отменяет загрузку | 2, 13 |
| R13 | Завершить security/filesystem matrix | Owner/permissions, symlink/reparse/traversal/substitution и current-user IPC проверены на новых путях; секреты исключены из logs; local fixed drive — базовый допуск, network/removable только после отдельной проверки | 13 |
| R14 | Согласовать API/UI/capabilities расширенных режимов | Точные states/reasonCodes, выбранные дорожки/граница/причина waiting видны в UI; controls/главы отзывчивы, activate=false не перехватывает фокус; compatibility v1/legacy и действующие SDK сохранены | 2, 14 |
| R15 | Свести общую приёмочную матрицу и ресурсные бюджеты | Все обязательные positive rows играют ≥10 s до complete; negative rows имеют причины; partial structures/holes/disk-full/crash, writer pauses10/30/60s, ≥100 cycles и длительные runs всех трёх редакций с точными exits. Нет неподтверждённых PASS/SKIP | 2, 15, численные критерии |
| R16 | Проверить реальные разрешённые YT/VK/Rutube sources | Direct/HLS/DASH, protocol/format IDs и воспроизводимые offline fixtures без секретных URLs; тестовый writer отделён от настоящей Harvester-интеграции | 15 |
| R17 | Собрать и опубликовать итоговую поставку | Три сборки из одного commit; EXE/MSI Windows, DEB Linux, SDK, licenses/build options и matrix; clean install/upgrade/profile/registration, downloaded asset SHA/source commit и GitHub release проверены | 17 |

R01–R10 выполняются последовательными небольшими профилями. При каждом новом
профиле применяются R04 и R11–R14, а не откладываются до конца. R15 пополняется
этими результатами; полный финальный прогон выполняется перед R17. R16 можно
готовить параллельно разработке, без изменения Harvester. R17 не заменяет
проверку недостающих функциональных строк.

## Ближайший пакет: R01

1. Составить таблицу оставшихся container/codec/layout rows: уже native-progressive,
   continuous-remux, probe-only, waiting-index и неподдержанные. Для каждой строки
   указать документ/тест и границу трёх движков, а не выводить её из расширения файла.
2. По этой таблице выбрать первый ещё не поддержанный профиль. Зафиксировать
   partial fixture, ожидаемый output container, negative/waiting cases, caps и
   способ независимого packet/clock сравнения до правок product code.
3. Подключить профиль к worker/cache/decoder и public capability совместимо;
   без transcoding по умолчанию, без обещания чтения отсутствующих bytes/индекса.
4. Проверить ordinary Linux Qt/Windows Qt/Classic, compatibility, source SHA,
   release/forget и сохранение профилей. После проверенной сборки обновить Linux,
   матрицу и отчёт. Затем переходить к следующей строке.

[Инвентаризация шага 1 и объём первого профиля](ProgressiveRemuxInventory.md)
готовы. [Первый профиль реализован в SDK 1.88](ProgressiveContinuousHevc.md):
ordinary faststart MP4 с HEVC Main/Main10 video-only и B-frames. Worker, cache,
decoder, Classic adapter и SDK согласуют один явный mode; старые H.264 не расширены.
54 worker checks, 30 HEVC API rows и 78 прежних regression rows прошли;
Classic использовал частный Null Video Renderer (Uncompressed), видимый вывод
в SSH-сеансе не аттестован. Ошибка false complete при потере последнего пакета
воспроизведена и исправлена сверкой с MP4 sample count.
Установленный Linux обновлён с backup и неизменным INI; [22 дополнительных
сценария установленной копии и clean SDK](ProgressiveContinuousHevcDelivery-2026-11-12.json)
прошли. App 0.2.0/SDK 1.88 source artifact не объявляется новым итоговым release.

[HEVC + AAC-LC подключён отдельным mode SDK 1.89](ProgressiveContinuousHevcAac.md):
36 worker и 36 PCM consumer rows, 39 public API rows и 112 регрессий прошли.
Включены audio-first, one-sample/short audio, точный sample window и cancel
paused playback с ожидающим seek. Classic очищает pending target синхронно;
видимый renderer по-прежнему не аттестован. Video-only mode не меняет поведения.
[Установленный Linux обновлён](ProgressiveContinuousHevcAacDelivery-2026-11-13.json)
с backup и неизменным INI; ещё 25 сценариев установленной копии прошли.

[AV1 video-only подключён отдельным mode SDK 1.90](ProgressiveContinuousAv1.md):
Main8/Main10 av01, 25/30000/1001, один displayed Frame OBU/sample, PTS=DTS.
51 worker и 30 public API rows прошли, ≥10 s playback до complete каждого
положительного профиля. Hidden/show_existing/still/reorder отклоняются;
Classic использует private null renderer, видимый desktop output не аттестован.

[Linux установлен и проверен](ProgressiveContinuousAv1Delivery-2026-11-13.json):
backup, неизменный INI и ещё 22 сценария с clean SDK 1.90 установленной копии.

[AV1 + AAC-LC подключён отдельным mode SDK 1.91](ProgressiveContinuousAv1Aac.md):
Main8/Main10 av01, mono/stereo 44.1/48 kHz, exact PCM-window, audio-first,
one-sample и short audio. 48 worker, 48 PCM consumer, 39 public API rows и 192
регрессии прошли; положительные player fixtures играют ≥10 s до complete.
Classic — private null renderer, не visible/physical A/V acceptance.

R01 остаётся открытым. Следующий профиль — VP9 video-only в ordinary MP4:
fixture/config/clock/copy-remux admission, затем worker/cache/decoder и
отдельная capability трёх редакций. VP9 + audio и другие container/layout
rows из инвентаризации остаются дальнейшими этапами. Старые narrow tests сохраняются.

## Исторические наблюдения: отдельно от новых функций

| ID | Что остаётся неизвестным | Что требуется для атрибуции |
| --- | --- | --- |
| H01 | Первоначальный Linux release >2 s без captured owner/stack | При новом естественном отказе записать стадии close, PID/TID/lock и native I/O. Уже исправленные output/source mutex paths не объявлять причиной старого события |
| H02 | Первичный Classic resource FAIL после heartbeat12 без исходного baseline | Сохранить baseline и failed snapshot до assert; при воспроизведении установить инициатора. Исправленная DWM Event leak и успешные повторы не доказывают связь со старым событием |
| H03 | Старый Windows Qt windowClose:reader-release assertion без исходного JSON | Использовать latched receipt oracle и сохранять первое неуспешное наблюдение; не называть его доказанным зависанием окна |
| H04 | AAC Classic: временные TppWorkerThread, инициирующий callback неизвестен | При новом превышении немедленного budget сохранить thread origins/callback и время; поздний возврат к baseline не подменяет строгий gate |

Старые свидетельства и checkbox этих наблюдений остаются в подробном чеклисте.
Не требуется бесконечно повторять уже прошедший случай вместо R01: повтор
назначается для новой гипотезы или нового нарушения, а не для стирания FAIL.

## Внешние зависимости и границы

| ID | Статус | Что нужно |
| --- | --- | --- |
| E01 | Harvester запрещено менять в этой задаче | Отдельное разрешение на его кнопки/events и затем сквозные start/update/complete/error/cancel/release. До этого максимум — «плеер готов к интеграции» |
| E02 | Физическое A/V измерение открыто, оборудования нет | Отдельно согласованный стенд/методика: абсолютная ошибка ≤80 ms и drift за30min ≤40 ms. File/null/software tests не закрывают этот допуск; оборудование самовольно не возобновлять |
| E03 | Внешний Python launcher с длинным parent TEMP | Ошибка внутри Windows CreateProcess/GetTempPathW описана отдельно; сохранить обычный parent TEMP и задавать long TEMP только child environment. Не менять Python/Harvester/Windows; полная long-path приёмка плеера относится к R12 |

Wayland исключён. Дисковые меню, аналоговое ТВ и физические устройства отложены.
Новых периодических проверок не создавать. Открытый численный критерий нельзя
закрывать только выбором libmpv option либо успешным выходом collector.

## Правила актуализации

- В подробном чеклисте хранить точную проверенную область и ссылки на SHA/exits;
  старые SDK notes описывают состояние на свою дату, а не текущий дефицит.
- В этой сводке менять статус R только по завершённому критерию; частичный
  профиль добавлять к доказательствам, оставляя общий пакет открытым.
- Готовность R15 требует покрытия всех обязательных строк, latency ≤3/2 s для
  ready-start/resume и ≤2 s для управляемого release на согласованном ПК.
  Результат с внешним lock дольше бюджета хранить отдельно, без ложного PASS.
- Полное ТЗ требует не только R01–R17, но и незакрытых внешних зависимостей.
  Документационное упорядочение само по себе не требует сборки/переустановки.
