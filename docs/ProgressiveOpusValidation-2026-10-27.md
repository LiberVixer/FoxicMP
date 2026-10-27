# Проверка progressive Opus — 27 октября 2026

[SDK 1.17 и границы профиля](ProgressiveOpus.md). База:
`36c12cc4ac79e96e1f086172391d7c892f7c021e`.
[Машинный отчёт с SHA-256, состояниями и exit codes](ProgressiveOpusValidation-2026-10-27.json).
YouTubeHarvester не изменялся.

## Итоговая матрица

| Стенд | Результат |
|---|---|
| Linux Qt | API 77 PASS, legacy/source 19 PASS, native media 132; 3 CTest группы, exit 0 |
| Windows Qt | API 78 PASS, native media 132; 2 CTest группы, exit 0 |
| Windows Classic | 8 SDK групп, 15 медиавариантов, async close 5, legacy playback, maintenance 3, opening 4; exit 0 |
| Native media ASan/UBSan | 132 PASS, leak detection включён; exit 0 |
| FragmentIndex / выбор pre-roll под ASan/UBSan | 16 запусков, в том числе signed minimum и увеличенный pre-skip; exit 0 |
| FragmentIndex ↔ FFprobe | 7 тестов, в том числе Opus mono/stereo; exit 0 |
| Python SDK / family contract | 35 / 2; exit 0 |

QtTest totals включают init/cleanup, пропусков нет. Linux UI/audio используют
частные X11/D-Bus/PulseAudio; Windows Qt — отдельные offscreen профили.
Linux source suite не входит в Windows CTest.

## Реальное воспроизведение и переход назад

Матрица включает семь прежних AAC-пар: H.264, HEVC8/10, AV1-8/10, VP9-8/10;
те же семь видео с Opus stereo и H.264 с Opus mono. Новое аудио — 48 kHz,
20 секунд, отдельные каналы 880/440 Hz для stereo, encoder PreSkip=312 samples,
head edit с media_time=312. Все видеокорпусы — 320×180, 25 fps, 20 секунд.

Общий wire-сценарий проверяет probe без оставшихся leases, start/idempotence,
writer/reader guards, cancel/restart, публикацию 5→12 фрагментов и позицию ≥10 с,
пока оба физических файла ещё меньше полной записи. Decoder-read bytes есть
у обеих дорожек, mediaMode/videoCodec/audioCodec совпадают с профилем.

В Opus-сценариях выполняется переход назад до complete, затем наблюдается
продолжение воспроизведения. Qt использует Window::seekTo(2); Classic — обычную
команду перехода назад с JumpDistM=2000 **только в тестовом INI**. Затем проходят
переименование закреплённых файлов, complete, естественный finished и release
после фактического закрытия всех readers.

Qt на обеих ОС подтверждает активный audio-codec-name=opus, audio-params rate
48000, меняющийся decoded video framebuffer до complete и 10-bit decoder pixel
format для соответствующих HEVC/AV1/VP9. Физический аудиовыход и PCM waveform
не измерялись. Classic проверен через DirectShow graph/status и decoder-read
bytes, без оптической записи экрана или физического измерения звука.

## Заголовки, временная шкала и ресурсы

132 native media случая включают прежние кодеки и новые Opus dOps
size/version/channels/mapping, sample-entry channels/sample size/rate/version,
media clock, pre-skip/edit consistency, edit flags/version/count/rate, неверный
hint, external/invalid dref, encrypted entries, ожидание диапазона, init rewrite,
cancel и удержание файлов. Допускаются информационный InputSampleRate=0 и
signed OutputGain. Весь Opus bitstream и payload integrity этим не квалифицируются.

FragmentIndex сравнивает packet offset/size/PTS/DTS/sync с FFprobe, время —
с допуском 1 мкс. Подтверждены начальная отметка −6,5 мс, последний end=duration,
отказ старого v1/legacy и video-only codec flags принимать Opus. Проверен выбор
пакетов, покрывающих 80 мс перед переходом. Дополнительная фикстура с
PreSkip/media_time=4800 подтверждает сохранение пакетов до нуля timeline;
минимальный int64 position не вызывает арифметическое переполнение.

ASan/UBSan инструментирует общий мост/ResourceSource и отдельно native
FragmentIndex/AudioSeekSample с leak detection. Внешние Qt/libmpv/DirectShow
библиотеки в этих запусках не инструментированы.

## Первые прогоны и исправления

Первый Linux CTest имел **exit 8**: увеличенная data-driven функция была прервана
watchdog QtTest; до прерывания 56 PASS/1 fatal, source suite — 19 PASS. У тестов
теперь явные лимиты: CTest 840 с и QTEST_FUNCTION_TIMEOUT=720000 мс; отдельный
writer по-прежнему ограничен 60 с. [Документация QtTest о watchdog](https://doc.qt.io/qt-6/qtest-overview.html).
Итоговый полный прогон выполнен заново на окончательной сборке.

Первый Windows Qt CTest имел **exit 8**, API — 77 PASS/1 failure: после close
тест немедленно ожидал пустой registry в HEVC8. Итоговая проверка отдельно
требует released **и** filesClosed у каждой оставшейся квитанции, лишь после
этого удаляет её и требует пустой registry. Активный reader остаётся ошибкой;
это не разрешение удалять открытые файлы. SDK forget сохраняет прежнюю
best-effort семантику.

Первый Classic runner имел **exit 1**: тест послал WM_COPYDATA seek без master
для /slave, поэтому команда не исполнилась. Стенд переведён на обычную команду
перемотки в отдельном профиле. Второй runner также имел **exit 1**: в VP9/Opus
за deadline не наблюдался playing ≥10 с; последнее состояние — buffering,
позиция 11,0093821 с. Затем на окончательном EXE пройдены отдельный VP9/Opus
с переходом назад и полная матрица. Первоначальные коды сохранены в JSON.

Попытка удалить ещё выполнявшуюся тестовую задачу была отклонена с **exit 1**,
без удаления активного задания. Завершённое задание заменено штатным retry;
окончательная одноразовая задача удалена после завершения, LastTaskResult=0.

Во время проверки pre-roll найден и исправлен отдельный крайний случай:
выбор пакетов больше не ограничивает нижнюю границу нулём timeline. Это
подтверждено новой фикстурой с увеличенным priming и проверкой под UBSan.

## Сборки и установка

Окончательные GCC Linux, MinGW Windows Qt и MSVC Classic: exit 0. После
прерывания рабочей сессии сборки продолжены до подтверждённого exit 0;
незахваченные промежуточные статусы не объявляются успешными.
В Classic пересобраны DSUtil, BaseSplitter, MP4Splitter и приложение.
SHA отдельной тестовой копии совпал с итоговым EXE. UTF-16 INI отключал
context-menu prompt/registration; HKCU команды и иконки установки до/после
совпали.

Linux EXE заменён атомарно с резервной копией; INI сохранил хеш. Точные пути,
SHA и capabilities есть в JSON. Просмотр ради замены не останавливался.
Windows установки и профили не заменялись. Общая версия приложения — 0.2.0,
SDK — 1.17; новые возможности определяются capabilities/хешем EXE.

WebM/MKV/Ogg, multichannel, muxed/remux, metadata/live, реальный Harvester,
физическая A/V-синхронизация и 30-минутный дрейф не закрыты. Оборудование и
Wayland исключены по указанию пользователя. Полное ТЗ ещё не выполнено.
