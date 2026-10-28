# Проверка progressive AV1 в WebM — 28 октября 2026

[Профиль SDK 1.19](ProgressiveWebMAv1.md). База исходников:
`dbce4c9bc0ca025833ea10d5d3eb1a7bb4d61986`.
[Машинный отчёт с SHA-256, состояниями и exit codes](ProgressiveWebMAv1Validation-2026-10-28.json).
YouTubeHarvester не изменялся.

## Итоговая матрица

| Стенд | Результат |
|---|---|
| Linux Qt | 81 API PASS, 19 legacy/source PASS, 186 native media; 3 CTest группы, exit 0 |
| Linux Qt, начальная проверка | Два целевых AV1 WebM playback-теста, exit 0 |
| Windows Qt | 82 API PASS и 186 native media случаев; 2 CTest группы, exit 0 |
| Windows Classic | 8 SDK групп, 19 медиавариантов, отдельный AV1 WebM и legacy provider; exit 0 |
| Native media ASan/UBSan | 186 PASS, leak detection включён; exit 0 |
| WebMIndex ↔ FFprobe | 4 теста, включая AV1 8/10-bit и повреждённые OBU; exit 0 |
| WebMIndex ASan/UBSan | Те же 4 теста и 4 дополнительных индексных запуска; exit 0 |
| Python SDK и family contract | 29 PASS; exit 0 |

QtTest totals включают init/cleanup; пропусков нет. Все три редакции отдельно
подтвердили 19 медиапрофилей, включая два новых AV1 WebM варианта.

## Сквозной сценарий

К прежним 17 вариантам добавлены WebM AV1 Main 8/10-bit + Opus stereo, 48 kHz.
Видео — 320×180, 25 fps, 20 секунд. Проверяется probe без оставшихся leases,
start/idempotence, writer/reader guards, cancel/restart, рост пяти→двенадцати
кластеров и playing ≥10 с, пока оба физических файла ещё не дописаны.
В обоих AV1 WebM вариантах выполняется переход назад до complete.

Затем открытые файлы переименовываются и дописываются, публикуется complete,
проверяются естественный finished и filesClosed/release acknowledgement.
Qt подтверждает меняющиеся кадры до complete, AV1 pixel format 8/10-bit
декодера и активное Opus 48 kHz. Classic проверяется отдельно через граф,
статус и счётчики чтения декодера. Это не измерение физического звука или
подтверждение 10-bit конечного вывода Qt.

## Общий код и повреждённые данные

Проверка AV1CodecConfigurationRecord вынесена в общий HarvesterAV1.h для
MP4 и WebM. Сохраняются ограничения прежнего fMP4-профиля. Дополнительно
WebMIndex требует в первом ключевом пакете совпадающий Sequence Header и
Frame OBU с KEY_FRAME. Это ограниченная проверка, не собственный AV1-декодер.

186 native media случаев содержат все прежние 159 сценариев и 27 новых:
AV1 8/10-bit, обнаружение без hint, leases/переименование/ожидание/отмена,
неправильные marker/version/profile/level/depth/chroma, reserved bits,
presentation delay, повреждённые OBU header/size/type, отсутствие CodecPrivate,
несогласованный hint и изменение опубликованной инициализации. Корпус пройден
также под ASan/UBSan с leak detection.

Четыре теста WebMIndex сверяют с FFprobe полезную нагрузку каждого пакета
через SHA-256, размеры, PTS и sync, включая прежние VP9 и Opus mono/stereo.
Проверяются preroll, минимальный signed seek и граница v1/fMP4. Отдельные
AV1 мутации исключают Sequence Header/Frame OBU, меняют sequence payload,
frame_type/show_existing_frame и forbidden bit. Тот же полный корпус
индекса повторяется с ASan/UBSan. Дополнительный ASan-прогон читает все четыре
видеофикстуры VP9/AV1 8/10-bit по возрастающему подтверждённому префиксу.

Первый запуск генератора unit-корпуса имел exit 5: FFmpeg вернул exit 183
при прямом кодировании libaom в WebM, до выполнения тестов. Генерация исправлена:
сначала delayed-moov MP4, затем копирование реальных AV1-пакетов в WebM.
Таким же способом создаётся основной медиакорпус. Ошибка генератора не
объявлена успешной проверкой плеера.

## Сборки, изоляция и установка

GCC Linux Qt, MinGW Windows Qt и MSVC Classic проверяются отдельно. Classic
пересобирает DSUtil, BaseSplitter, MP4Splitter, MatroskaSplitter и приложение.
Тестовая копия получает отдельный UTF-16 INI с ContextMenuPrompted=1,
ContextMenuEnabled=0 и тестовым JumpDistM=2000. Сравниваются HKCU shell
commands/icons до и после. Одноразовая задача удаляется после завершения,
хеш проверенной копии сверяется с итоговой сборкой.

Linux UI/audio использует `qt/tests/run_tests.sh` с частными X11/D-Bus/PulseAudio;
Windows Qt — отдельные offscreen профили. Настройки, микшер и сеансы рабочего
стола не меняются. Linux EXE заменяется атомарно с резервной копией и проверкой
неизменности INI; Windows установки и профили не обновляются этим этапом.

Версия приложения остаётся 0.2.0, версия SDK — 1.19; поддержку определяют
capabilities и SHA сборки. Проверенные результаты и точные коды выхода
зафиксированы в JSON, без переноса Linux-результатов на Windows.

## Оставшиеся границы

Две отдельные WebM AV1/Opus дорожки, один Frame OBU в начальном ключевом TU,
известные размеры кластеров, DefaultDuration видео, Main 8/10-bit 4:2:0.
Смешанные контейнеры, VP8/Vorbis, произвольный MKV, unknown-size Cluster,
lacing, несколько дорожек, metadata/live, реальный Harvester и физическое
измерение A/V/30-минутного дрейфа остаются отдельными этапами. Wayland и
физическое оборудование исключены пользователем. Полное ТЗ не закрыто.
