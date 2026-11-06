# Непрерывный AAC: реальные decoder adapters Qt и Classic

6 ноября 2026. Продолжение [parent/cache/index](ProgressiveManagedAac.md).
[Результаты, SHA, точные exit codes и первоначальные отказы](ProgressiveContinuousAacDecoderValidation-2026-11-06.json).
Это внутренний этап: публичный SDK остаётся **1.83**, continuous audio
capability и выбор нового режима в обычном API ещё не добавлены.

## Доставка PCM-плана

`decoderSource(true)` явно разрешает проверенный частный
`continuous-h264-aac-window-v1` и сохраняет неизменяемое PCM-окно из кэша.
Обычный `decoderSource()` по-прежнему принимает только прежние video-only
profiles. До проверенных initialization/fragment/window AAC source не создаётся.
Сам opt-in не заменяет обязанность consumer применить PCM-адаптер.

Qt Player подключает две защищённые fMP4-проекции общего кэша и передаёт
presentation sample bounds существующему управляемому PCM-фильтру до пользовательских
фильтров и преобразования формата. Изменение audio graph сохраняет окно;
попытка подменить `af`/`af-*` в параметрах загрузки отклоняется.
Для аудио подтверждённое количество AAC packets из неизменяемого плана
разрешает отдельный track EOF до завершения длинного видео. Video EOF по-прежнему
требует writer complete и актуальный view последнего cache prefix. Это устраняет
найденный startup stall дорожки длиной в один sample: PCM-фильтр и audio output
больше не ждут конец всего скачивания. Отдельный аудиоконец не завершает кэш
и не освобождает независимые video/source leases. Второго worker и перекодирования нет.

Classic получает две логические fMP4-проекции одного кэша. Метаданные строятся
из проверенного AV-индекса, payload читается через тот же SHA/lease-protected Reader.
Новый отдельный COM-интерфейс `IProgressiveAacPcmControl` передаёт rate/channels,
число полезных samples, priming, seek target и первый raw sample декодирования.
Прежний MP3-интерфейс и его версия не расширены до AAC.

Splitter согласует окно с фактически подключённым декодером непосредственно перед
первым сжатым пакетом. Decoder применяет его после обязательного discontinuity flush.
Flush/NewSegment/Stop очищают состояние; seek повторно согласует окно с preroll.
Неподходящий downstream получает отказ до выдачи PCM. Перемотка за конец короткой
аудиодорожки не повторяет последний sample: звук ждёт подтверждённого EOF,
пока видео продолжается.

## Что проверено

Шесть синтетических H.264/AAC-LC источников: mono/stereo 44,1/48 kHz,
25/30000:1001/60000:1001 fps, B-frames до 16, aligned контроль и один полезный
sample с audio-first порядком. Video длится около 6,137 s; последний PCM sample
может быть неполным AAC packet. Во время growing-прогонов физический хвост файла
существует, но остаётся неопубликованным. Физическое дописывание отдельно
проверено [parent/cache матрицей](ProgressiveManagedAac.md).

- Linux Qt: 31 сценарий реального Player в частных X11/D-Bus/PulseAudio.
- Windows Qt: те же 31 сценарий, software renderer/offscreen.
- Windows Classic: 37 сценариев настоящего DirectShow software A/V graph.

Проверяются complete/growing, ручная pause/resume, seek, отсутствие EOF до
writer complete, stop/cancel, reader receipts и удаление кэша. Два дополнительных
seek-ended сценария каждой редакции требуют ноль PCM после окончания короткого
звука. Classic дополнительно отвергает чужой downstream в шести profiles.
Все основные driver exits равны 0; предельное время Stop остаётся 2 s.

Для полного/growing Qt PCM побитно равен независимому FFmpeg decode исходника
с объявленной presentation длиной. Classic сохраняет точное число samples,
float error против независимого decoder меньше 1e-5; все захваченные NV12-кадры
равны независимому video decode, включая seek. Qt использует настоящий Player
и software video, но этот стенд не экспортирует его видеокадры для попиксельной сверки.

### Эталоны перемотки

Qt/libmpv синхронизирует начало звука с первым показанным видеокадром.
Для 30000/1001 fps после запроса 1 s это 1,001 s; целочисленное clipping
в libmpv может дать разницу до одного sample относительно рационального PTS.
Тест требует **побитного совпадения** continuous Player с обычным Player,
читающим те же законченные video/audio проекции кэша через файловый I/O; дополнительно
проверяет границу относительно независимых video PTS с точностью одного sample.
Это не обещание sample-exact seek к произвольной секунде API.

Classic после same-graph seek сравнивается побитно с отдельным CFFAudioDecoder
без PCM-окна, получающим ту же историю AAC packets и preroll. AAC PNS state
сохраняется при flush, поэтому suffix полного decode не является эталоном
значений PCM после seek. Границы окна и его timestamps при этом проверяются точно.
Порог независимого полного float-сравнения не ослаблялся.

Найденные первоначальные отказы сохранены: неподдерживаемый тестовый вызов
`SetOutputFormat` внутреннего decoder, ошибочные full-file/seek oracles,
округление libmpv, ранний запрос диагностического индекса до публикации initialization,
недостающие параметры частной сборки/окружения и ошибка PowerShell splatting.
Отдельно сохранён реальный growing startup stall короткого AAC в Qt до
добавления независимого track EOF; он исправлен в production adapter.
Исправления стенда перечисляются отдельно от этого изменения production adapter.

Регрессии: по 22 managed AAC + 31 прежнему cache case GCC/ASan-UBSan/MinGW/MSVC;
новые protected cursors проверяются до/после writer complete: ранний audio EOF,
ожидание video, interrupt, размер и принудительный filesClosed без отмены producer;
по 72 MP3 Player checks и 16 AAC worker/adapter rows каждой Qt ОС;
32 MP3 DirectShow checks с AudioSwitcher и по пять прежних no-B/B-frame video graphs.
На каждой ОС прошли также 11 прежних raw callback и четыре reordered decoder cases
с текущим общим cursor. Всего в отчёте — 99 новых, 212 native/cache и 248
проверок совместимости; это разные по объёму сценарии и отдельные assertions.
Декодеры и графы работают с file/null outputs: физический audio output не открывается.
Физическая A/V синхронизация этим не измеряется.

## Воспроизведение проверок

Qt: собрать `foxicmp-continuous-aac-player-probe` и `foxicmp-remux-stream-worker`,
запустить CTest `progressive-continuous-aac-player` через `qt/tests/run_tests.sh`
на Linux. Понадобятся ffmpeg/ffprobe и частные тестовые зависимости wrapper.

Classic: в x64 MSVC Developer Command Prompt с уже собранными зависимостями
выполнить `tests/windows/build_continuous_aac_graph_probe.cmd`. Он собирает
изменённые объекты отдельно и не перезаписывает существующие static libraries.
Затем запустить `tests/progressive_continuous_aac_decoder_test.py` с `--edition classic`,
`--runner _bin/continuous-aac-classic/probe.exe`, соответствующим `--worker`,
`--seek-oracle _bin/continuous-aac-classic/seek-oracle.exe` и новым `--report-directory`.
INI тестовых копий запрещают регистрацию контекстного меню; HKCU проверяется до/после.

## Что остаётся

Следующий этап — public controller/status/UI и SDK opt-in, обычные API playback
сценарии всех трёх редакций, source/worker failure и replacement/re-arm матрица,
публичные пакеты и установка новой версии. Отдельно открыты growing seek в ещё
неподтверждённый диапазон, полная crash/race/stress matrix и прежний Linux latency
outlier. Общий progressive-ТЗ этим этапом не закрыт.

Main EXE, установленные worker и пользовательские INI не изменялись: новой
пользовательской версии на этом этапе нет. 576 защищённых файлов и 158 HKCU shell
entries сохранены; старые SDK ZIP не пересобраны. Harvester, рабочий стол,
микшер и Windows-настройки не менялись; scheduled tasks не создавались.
