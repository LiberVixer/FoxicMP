# Проверка progressive WebM — 28 октября 2026

[Профиль SDK 1.18](ProgressiveWebM.md). База исходников:
`93e6e3ca2d3edb8da5627b89f04f5d50afb67aea`.
[Машинный отчёт с SHA-256, состояниями и exit codes](ProgressiveWebMValidation-2026-10-28.json).
YouTubeHarvester не изменялся.

## Итоговая матрица

| Стенд | Результат |
|---|---|
| Linux Qt | 79 API PASS, 19 повторных v2 playback PASS, 19 legacy/source PASS; 4 CTest группы, exit 0 |
| Linux Qt, окончательная пересборка | Два целевых WebM playback-теста, exit 0 |
| Windows Qt | 80 API PASS и 159 native media случаев; 2 CTest группы, exit 0 |
| Windows Classic | 8 SDK групп, 17 медиавариантов, отдельный WebM и legacy provider; exit 0 |
| Native media ASan/UBSan | 159 PASS, leak detection включён; exit 0 |
| WebMIndex ↔ FFprobe | 3 теста, Opus mono/stereo, повреждённые блоки и v1-граница; exit 0 |
| WebMIndex/seek ASan/UBSan | 8 запусков; exit 0 |
| Python SDK и family contract | 29 PASS; exit 0 |

QtTest totals включают init/cleanup; пропусков нет. Все три редакции отдельно
подтвердили 17 медиапрофилей; новый WebM-корпус включает два VP9 bit depth.

## Сквозной сценарий

В общий корпус добавлены отдельные WebM VP9 Profile 0/8-bit и Profile 2/10-bit
видео, Opus stereo 48 kHz. Обе дорожки физически неполные: сначала пять,
затем двенадцать полностью записанных кластеров, общая длительность 20 секунд.

В Linux Qt, Windows Qt и Classic проверяются probe без оставшихся leases,
start/idempotence, writer/reader guards, отмена и повторный start. Затем плеер
проходит позицию 10 секунд до complete, пока оба файла меньше полной записи;
читает байты обеих дорожек, возвращает `separate-webm-vp9-opus` и выполняет
переход назад. После переименования открытых файлов, дописывания и complete
проходит естественный finished; release подтверждает фактическое закрытие.

Qt дополнительно подтверждает меняющиеся кадры декодера, VP9 pixel format
8/10-bit, `audio-codec-name=opus` и 48 kHz до complete. Это не проверка 10-bit
конечной поверхности Qt. Classic проверен через DirectShow-граф, статус и
счётчики чтения; физический звук и оптическая запись экрана не измерялись.

## Индекс и повреждённые источники

159 native media случаев включают прежние fMP4-кодеки и 27 новых WebM
проверок: настоящие заголовки, обнаружение кодеков без hints, закреплённые
leases, переименование, ожидание подтверждённого конца, отмена, DocType,
версии/ширины EBML, CodecID, TimestampScale, размеры кадра, DefaultDuration,
CodecDelay/SeekPreRoll, OpusHead version/channels/mapping, неверные hints,
смешанные контейнеры, неполная и изменённая инициализация.
Весь этот native мост пройден под ASan/UBSan с leak detection.

Три отдельных теста WebMIndex генерируют VP9 8/10-bit и Opus mono/stereo.
С FFprobe сравниваются SHA-256 полезной нагрузки каждого пакета, размер, PTS
и sync. FFprobe Matroska pos не является смещением payload, поэтому совпадение
байтов проверяется хешем. PTS Opus допускает разницу до 0,501 мс из-за округления
CodecDelay в FFprobe к stream timebase 1 мс; индекс хранит собственные 100 нс
отметки. Проверены начальные −6,5 мс, финальный end, 80 мс preroll, минимальный
signed seek, отказ принимать WebM через v1/fMP4 flags, lacing, другой track,
первый неключевой блок и усечённый последний кластер. Дополнительные восемь
запусков индекса/seek выполнены под ASan/UBSan.

Полная целостность каждого кодека и физическая A/V-синхронизация этим не
подтверждаются. Mono проверено на уровне индекса; новый сквозной WebM-корпус
трёх редакций использует stereo.

## Сборка и изоляция

GCC Linux Qt, MinGW Windows Qt и MSVC Classic собраны отдельно. Classic
пересобирает DSUtil, BaseSplitter, MP4Splitter, MatroskaSplitter и приложение.
Его тестовая копия имеет отдельный UTF-16 INI с ContextMenuPrompted=1 и
ContextMenuEnabled=0. Общий тест сравнивает HKCU shell commands/icons до и после;
завершённая одноразовая задача удаляется, LastTaskResult=0. Хеш проверенной
копии совпадает с итоговым EXE.

Linux UI/audio запускается через `qt/tests/run_tests.sh` с частными
X11/D-Bus/PulseAudio. Недостающие Xvfb/xauth и команда для проверки окружения
распакованы локально в игнорируемый `_bin/webm-test-tools`; системные пакеты,
микшер и рабочий сеанс не менялись. Первые два запуска runner имели exit 2
из-за отсутствующих зависимостей. Ранние native проверки использовали устаревший
бинарник и затем были повторены после сборки; исправлена длина одного
повреждённого CodecID в тесте.

В первом Windows прогоне CTest сохранил 80 PASS, но SSH-обёртка не смогла
декодировать CP1251-предупреждение Qt после teardown. Exit CTest того запуска
не захвачен и не считается подтверждённым нулём. Повторный запуск сохраняет
лог и фактический exit непосредственно на Windows. Ранняя MSVC-сборка также
обнаружила конфликт имени локальной переменной `small` с Windows RPC macro;
переменная переименована в `bits32`.

Linux EXE обновляется атомарно после окончательных тестов, с резервной копией
и проверкой неизменности INI. Windows установки и их профили не заменяются.
Версия приложения остаётся 0.2.0; расширение определяется capabilities и SHA
EXE, версия интеграционного SDK — 1.18.

## Оставшиеся границы

Поддержан объявленный профиль двух отдельных WebM VP9/Opus дорожек с известными
размерами Cluster, DefaultDuration видео и без lacing. Другие WebM-кодеки,
произвольный MKV/Ogg, muxed/remux, segments/live, несколько дорожек, metadata,
реальный Harvester, измерение A/V и 30-минутного дрейфа остаются отдельными
этапами. Wayland и физическое оборудование исключены пользователем.
Полное ТЗ ещё не выполнено.
