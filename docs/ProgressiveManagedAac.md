# Непрерывный AAC: управляемый parent, кэш и два индекса

6 ноября 2026. Продолжение [точного PCM-окна AAC](ProgressiveContinuousAacWindow.md).
[Результаты, SHA, exit codes и первоначальные отказы](ProgressiveManagedAacValidation-2026-11-06.json).
Это внутренний компонент; публичный SDK остаётся 1.83 с video-only continuous profiles.

## Родительский процесс

Явный `StreamRemuxProfile::H264AacWindow` выбирает частный
`continuous-h264-aac-window-v1`. Нужны две обязательные scalar MP4 проекции
H.264/AAC-LC одного известного по длине SHA-защищённого ресурса: VOD,
нулевой offset, известная длительность и timebase 100 ns.
Обычный video-only профиль и opt-in reordered video сохраняются.

До первого media write родитель проверяет единственный `audio-window`:
версию, track ID, rate/channels, raw/presentation origins, приминг,
целочисленные границы, padding и SHA компактного описания. Он передаёт план
кэшу и отвечает `audio-window-accepted` только после успешного принятия.
Ранние медиабайты, повторное окно и неверный digest отклоняются без записи.
Итоговый worker result обязан содержать тот же план, точное количество AAC
пакетов и согласованные sample counts/skip/trim flags; source EOF/complete,
суммарные I/O counters, digest и exit code проверяются как раньше.

Отмена, heartbeat, проверка исходника, ожидание подтверждённых ranges и закрытие
исходных reader leases используют существующий managed lifecycle.
Отдельные profiles/EXE/INI/IPC трёх редакций не объединяются.

## Проверка выходного кэша

План принимается один раз, до любых output bytes. В `StreamOutputStore`
он связан с конкретными session/generation и неизменяем на протяжении кэша.
Новый `StreamAvIndex` использует две логические fMP4 проекции и проверяет:

- исходную структуру moov, различные video/audio track IDs, самодостаточные ссылки;
- H.264 и AAC-LC, совпадение AAC track ID, media rate и ASC channels с планом;
- только законченные moof+mdat, последовательность фрагментов и покрытие
  payload всеми samples без дыр, наложений и неопубликованного хвоста;
- независимые video/audio clocks и AAC cadence, предел 50000 общих packets;
- полное ожидаемое количество AAC packets перед успешным завершением кэша.

AAC-пакет декодируется в 1024 samples, но его последняя presentation duration
может быть короче. Индекс допускает обе формы; PCM-окно остаётся точной
границей полезного звука. Округление индекса до 100 ns допускает один tick,
sample count не вычисляется из округлённых clocks.

Новый явный `Layout::ContinuousAv()` разрешает фрагменты с одной из двух
дорожек: короткий звук может закончиться раньше видео. Обычный `Layout`
по-прежнему требует обе дорожки в каждом фрагменте. Незнакомые/повторные ID,
лишние дорожки и неиндексированные bytes не разрешаются ни в одном режиме.

`avSamples()` — внутренний диагностический срез с физическими offsets того же
кэша. Все sample payload читаются через защищённый Reader с SHA и leases.
Он не предоставляет decoder admission и не меняет public API.
Журнал восстановления/очистки кэша сохраняет прежнюю семантику; возобновление
AAC playback из осиротевшего кэша не добавлено.

## Проверки

GCC/Linux, MinGW/Windows, MSVC/Windows и ASan/UBSan parent/cache:
по **22 новых + 31 прежнему cache case**, всего **212**, каждый driver exit 0.
ASan child — прежний проверенный GCC FFmpeg worker; этот прогон проверяет
санитайзерами parent/cache/index, а не одновременно новый instrumented FFmpeg.

Шесть физических growing fixtures каждой сборки: 44,1/48 kHz mono/stereo,
25/30000:1001/60000:1001 fps, B-frames, aligned контроль и один полезный AAC
sample с audio-first порядком. Проверяются один worker PID, дописывание файла,
задержка публикации физически записанного хвоста, частичные fragments,
точные payload/PTS/DTS и двухдорожечные sample envelopes. До writer complete
нет успешного EOF, после complete SHA кэша совпадает с worker digest.

PCM из законченного кэша независимо сравнивается с исходником через FFmpeg
с согласованной конечной границей. Это reference decode, **не проверка
настоящего continuous A/V Player/DirectShow graph**. Отдельные реальные PCM
adapters были проверены [предыдущим этапом](ProgressiveContinuousAacWindow.md).

Отрицательные случаи: отсутствующий план, неверные rate/channels/track/skip,
padding/поля, повторный или поздний план и недостающий последний пакет.
Синтетический child проверяет ранний write, неверный digest, пропущенное поле
и повторное окно в настоящем parent transport; принято ноль медиабайтов.
Отмена до и после окна закрывает исходник за 0,016–0,047 s при прежнем пороге
2 s. Независимый выходной reader удерживает кэш до собственного filesClosed;
после последнего close кэш удаляется, budget возвращается к нулю.

Сохранены исходные отказы: ошибочная проверка последнего shortened packet;
неверное предположение теста о двух traf в aligned fixture; запуск старого
ASan binary до завершения пересборки; отсутствие FFmpeg в частном Windows PATH;
прерывание первого PowerShell build wrapper на stderr до фиксации compiler exit.
Итоговые повторы привязаны к SHA завершённых сборок. Пороги не повышались.

## Поставка и следующие шаги

Это source-only этап: новые main EXE/worker/SDK/пакеты не выпускаются.
Все 576 защищённых файлов, установки Linux/Windows, INI, исходные Classic
библиотеки и immutable SDK ZIP сохранили SHA. Сверены 158 прежних HKCU shell
entries. Все процессы — частные console fixtures, без рабочего стола/микшера,
новых scheduled tasks и изменений Harvester. CTest зарегистрирован на обеих ОС.

Следующее обязательное подключение: доставить неизменяемое PCM-окно в настоящие
continuous Qt/Classic decoder sources, согласовать AAC DirectShow control и
повторное применение окна после flush/seek. Сейчас существующий video-only
`StreamDecoderSource` явно отвергает новый mode; этот отказ проверен, чтобы
не допустить воспроизведение AAC без обрезки.

Затем — growing A/V playback, pause/seek/EOF/reader isolation, временная матрица,
публичные controller/API/capability/SDK и поставка. Физическая A/V-синхронизация,
длительный stress и прежние необъяснённые latency/resource отказы остаются OPEN.
Отложенные оборудование/Wayland не проверялись и не возобновляются.

Воспроизведение native-проверки после сборки соответствующего CMake target:

```sh
python3 tests/progressive_managed_aac_test.py \
  --runner /absolute/path/to/foxicmp-progressive-stream-cache-tests \
  --worker /absolute/path/to/foxicmp-remux-stream-worker \
  --report-directory /absolute/path/to/new-report
```
